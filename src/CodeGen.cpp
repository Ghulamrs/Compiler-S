#include "CodeGen.h"

#include "Builtin.h"
#include "backend/Emitter.h"

#include "../runtime/shmrt.h"

namespace shalimar {

std::string CodeGen::mangle(const std::string &name) {
    if (name == "main") return "shm_user_main";
    // The one that has no Shalimar name: the globals' initializer.
    if (name.empty()) return "shm_init_globals";
    return "shmf_" + name;
}

void CodeGen::run(Program &program, const std::string &sourceName) {
    emitter_.beginModule(sourceName);
    emitter_.defineGlobals(program.globalSlots());

    // The globals are created in file order, before main() runs, which is
    // where a global whose initializer calls a function that reads a later
    // global still fails - a cycle rather than an ordering, and the one case
    // the checker leaves to the run.
    Function &initializer = program.initializer();
    initializer.body().clear();
    for (StmtPtr &declaration : program.globals()) {
        initializer.body().push_back(StmtPtr(declaration.release()));
    }
    generate(initializer);

    for (std::unique_ptr<Function> &f : program.functions()) generate(*f);
    emitter_.endModule();
}

void CodeGen::generate(Function &function) {
    const Prototype &proto = function.proto();
    evaluationBase_ = function.frame().evaluationBase();
    depth_ = 0;
    deepest_ = 0;
    current_ = &function;
    exitLabel_ = newLabel();

    emitter_.beginFunction(mangle(proto.name));

    // Parameters come first and in order, so argument n goes to slot n; the
    // addresses of the outputs follow when there is more than one. A
    // reference parameter's slot holds the caller's address rather than the
    // value, which is why it is spilled wide whatever the parameter's type.
    //
    // Every argument that came in a register is spilled before any that came
    // in the block, because unpacking the block needs a register to borrow
    // and at this point the argument registers are all still live.
    const std::vector<Place> places = planArguments(proto);
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < places.size(); ++i) {
            if (places[i].overflow != (pass == 1)) continue;
            const int slot = i < proto.inputs.size()
                                 ? static_cast<int>(i)
                                 : function.outPointerBase() +
                                       static_cast<int>(i - proto.inputs.size());
            if (places[i].overflow) {
                emitter_.spillOverflowArgument(places[i].kind, places[i].index, slot);
            } else {
                emitter_.spillArgument(places[i].kind, places[i].index, slot);
            }
        }
    }

    // The recursion ceiling. Its message names the function, so the name goes
    // into the module's read-only bytes. The globals' initializer is not a
    // Shalimar function and cannot recurse, so it is left out of the count.
    const bool counted = !proto.name.empty();
    if (counted) {
        const int nameId = newBytesId();
        emitter_.defineBytes(nameId, proto.name + std::string(1, '\0'));
        const int limit = 256 / (static_cast<int>(proto.inputs.size()) + 1);
        emitter_.loadBytesAddress(nameId);
        emitter_.setArg(Slot::Wide, 2);
        emitter_.loadIntConstant(limit < 1 ? 1 : limit);
        emitter_.setArg(Slot::Int, 1);
        emitter_.loadIntConstant(proto.id);
        emitter_.setArg(Slot::Int, 0);
        emitter_.call("shm_enter");
    }

    for (StmtPtr &s : function.body()) generate(*s);

    emitter_.label(exitLabel_);
    if (counted) {
        emitter_.loadIntConstant(proto.id);
        emitter_.setArg(Slot::Int, 0);
        emitter_.call("shm_leave");
    }
    // The result is fetched after the counter comes down, because that is a
    // call and a call does not preserve an accumulator.
    if (function.resultSlot() >= 0) {
        emitter_.loadSlot(slotKind(proto.outputs[0]), function.resultSlot());
    }
    emitter_.endFunction(function.frame().variables() + deepest_);
    current_ = nullptr;
}

// Arguments in order: the declared inputs, then one pointer for each output
// when there is more than one. Microsoft's convention is positional, so
// spending an integer slot spends the matching SSE one; System V's and
// Apple's keep two counts. Whatever the registers cannot take goes into the
// overflow block, numbered in argument order.
std::vector<CodeGen::Place> CodeGen::planArguments(const Prototype &proto) const {
    std::vector<Slot> kinds;
    for (const Param &parameter : proto.inputs) {
        kinds.push_back(parameter.byReference || parameter.type->isArray()
                            ? Slot::Wide : slotKind(parameter.type));
    }
    if (proto.returnsByPointer()) {
        for (size_t i = 0; i < proto.outputs.size(); ++i) kinds.push_back(Slot::Wide);
    }

    std::vector<Place> places;
    int ints = 0;
    int reals = 0;
    int positional = 0;
    int overflow = 0;
    for (Slot kind : kinds) {
        const bool real = kind == Slot::Real;
        const int capacity = real ? emitter_.realArgCapacity() : emitter_.intArgCapacity();
        int index = 0;
        bool fits = false;
        if (emitter_.positionalArguments()) {
            fits = positional < capacity;
            index = positional;
            if (fits) ++positional;
        } else {
            int &count = real ? reals : ints;
            fits = count < capacity;
            index = count;
            if (fits) ++count;
        }
        if (fits) places.push_back(Place{kind, false, index});
        else places.push_back(Place{kind, true, overflow++});
    }
    return places;
}

void CodeGen::generate(Stmt &statement) {
    // A runtime error names the statement it happened in, so the runtime is
    // told which one that is before the statement runs. One call per
    // statement rather than one per expression is exactly the resolution the
    // language promises.
    emitter_.setLine(statement.line());
    statement.accept(*this);
}

void CodeGen::generate(Block &body) {
    for (StmtPtr &s : body) generate(*s);
}

void CodeGen::evaluate(Expr &expr) {
    expr.accept(*this);
}

void CodeGen::evaluateCondition(Expr &expr) {
    evaluate(expr);
    if (!isReal(expr.type())) return;
    emitter_.setArg(Slot::Real, 0);
    emitter_.call("shm_real_truth");
}

// How many of these a function needs is not predicted anywhere: it is
// counted here, as they are taken, and told to the emitter when the body is
// finished. A frame sized by a second party's estimate is a store past its
// end the first time the two disagree.
int CodeGen::reserve() {
    const int slot = evaluationBase_ + depth_++;
    if (depth_ > deepest_) deepest_ = depth_;
    return slot;
}

void CodeGen::release() { --depth_; }

bool CodeGen::isReal(const Type *type) {
    return type && type->kind() == Type::Kind::Real;
}

// The scalar at the bottom of however many array layers. The runtime builds
// the layers between for itself, given the rank.
int CodeGen::elementKind(const Type *arrayType) {
    switch (arrayType->scalar()->kind()) {
    case Type::Kind::Real: return SHM_REAL;
    case Type::Kind::Char: return SHM_CHAR;
    default:               return SHM_INT;
    }
}

const char *CodeGen::getterFor(const Type *elementType) {
    if (elementType->isArray()) return "shm_get_ref";
    switch (elementType->kind()) {
    case Type::Kind::Real: return "shm_get_real";
    case Type::Kind::Char: return "shm_get_char";
    default:               return "shm_get_int";
    }
}

const char *CodeGen::setterFor(const Type *elementType) {
    if (elementType->isArray()) return "shm_set_ref";
    switch (elementType->kind()) {
    case Type::Kind::Real: return "shm_set_real";
    case Type::Kind::Char: return "shm_set_char";
    default:               return "shm_set_int";
    }
}

// An array travels as a pointer, which is a wide integer; a char is a byte
// held in an int; everything else is what it looks like.
Slot CodeGen::slotKind(const Type *type) {
    if (type && type->isArray()) return Slot::Wide;
    return isReal(type) ? Slot::Real : Slot::Int;
}

void CodeGen::store(const Type *type, int slot) {
    emitter_.storeSlot(slotKind(type), slot);
}

void CodeGen::load(const Type *type, int slot) {
    emitter_.loadSlot(slotKind(type), slot);
}

void CodeGen::passAccumulator(const Type *type, int index) {
    emitter_.setArg(slotKind(type), index);
}

void CodeGen::passSlot(const Type *type, int slot, int index) {
    emitter_.loadSlotIntoArg(slotKind(type), slot, index);
}

void CodeGen::visit(IntLit &node) {
    emitter_.loadIntConstant(node.value());
}

void CodeGen::visit(RealLit &node) {
    emitter_.loadRealConstant(node.value());
}

void CodeGen::readSymbol(const Symbol &symbol) {
    if (symbol.isGlobal()) emitter_.loadGlobal(slotKind(symbol.type()), symbol.slot());
    else if (symbol.isReference()) emitter_.loadThroughPointer(slotKind(symbol.type()), symbol.slot());
    else emitter_.loadSlot(slotKind(symbol.type()), symbol.slot());
}

void CodeGen::writeSymbol(const Symbol &symbol) {
    if (symbol.isGlobal()) emitter_.storeGlobal(slotKind(symbol.type()), symbol.slot());
    else if (symbol.isReference()) emitter_.storeThroughPointer(slotKind(symbol.type()), symbol.slot());
    else emitter_.storeSlot(slotKind(symbol.type()), symbol.slot());
}

void CodeGen::visit(Var &node) {
    if (node.isNamedConstant()) {
        emitter_.loadRealConstant(node.constant());
        return;
    }
    readSymbol(*node.symbol());
}

// A string literal is a char array made fresh each time it is evaluated,
// from bytes that live in the module's read-only data.
void CodeGen::visit(StrLit &node) {
    emitter_.defineBytes(node.id(), node.text() + std::string(1, '\0'));
    emitter_.loadIntConstant(static_cast<int32_t>(node.text().size()));
    emitter_.setArg(Slot::Int, 1);
    emitter_.loadBytesAddress(node.id());
    emitter_.setArg(Slot::Wide, 0);
    emitter_.call("shm_array_from_text");
}

void CodeGen::visit(Blank &) {
    // A blank contributes nothing: the array it sits in arrived zeroed.
}

// One level at a time, each an array of its own. Building the whole shape in
// one call would need every extent at once, and a literal's deeper extents
// are only known by looking inside it - so the recursion that reads the
// literal is the recursion that builds it.
void CodeGen::buildLiteral(ArrayLit &literal) {
    const Type *type = literal.type();
    const Type *element = type->element();
    const int slot = reserve();

    const int dims = reserve();
    emitter_.loadIntConstant(static_cast<int32_t>(literal.elements().size()));
    emitter_.widenAccumulator();
    emitter_.storeSlot(Slot::Wide, dims);
    emitter_.loadSlotAddress(dims);
    emitter_.setArg(Slot::Wide, 2);
    emitter_.loadIntConstant(1);
    emitter_.setArg(Slot::Int, 1);
    emitter_.loadIntConstant(element->isArray() ? SHM_REF : elementKind(type));
    emitter_.setArg(Slot::Int, 0);
    emitter_.call("shm_array_make");
    release();
    emitter_.storeSlot(Slot::Wide, slot);

    for (size_t i = 0; i < literal.elements().size(); ++i) {
        Expr &item = *literal.elements()[i];
        if (dynamic_cast<Blank *>(&item)) continue;
        if (ArrayLit *nested = dynamic_cast<ArrayLit *>(&item)) buildLiteral(*nested);
        else evaluate(item);
        emitter_.setArg(slotKind(element), 2);
        emitter_.loadIntConstant(static_cast<int32_t>(i));
        emitter_.setArg(Slot::Int, 1);
        emitter_.loadSlotIntoArg(Slot::Wide, slot, 0);
        emitter_.call(setterFor(element));
    }
    release();
    emitter_.loadSlot(Slot::Wide, slot);
}

void CodeGen::visit(ArrayLit &node) {
    buildLiteral(node);
}

void CodeGen::visit(Index &node) {
    const int slot = reserve();
    evaluate(node.base());
    emitter_.storeSlot(Slot::Wide, slot);
    evaluate(node.index());
    release();
    emitter_.setArg(Slot::Int, 1);
    emitter_.loadSlotIntoArg(Slot::Wide, slot, 0);
    emitter_.call(getterFor(node.type()));
}

void CodeGen::visit(Dim &node) {
    const int slot = reserve();
    evaluate(*node.base());
    emitter_.storeSlot(Slot::Wide, slot);
    evaluate(*node.axis());
    release();
    emitter_.setArg(Slot::Int, 1);
    emitter_.loadSlotIntoArg(Slot::Wide, slot, 0);
    emitter_.call("shm_array_dim");
}

void CodeGen::visit(Precision &node) {
    evaluate(*node.places());
    emitter_.setArg(Slot::Int, 0);
    emitter_.call("shm_print_places");
}

void CodeGen::visit(Convert &node) {
    evaluate(node.expr());
    const Type *from = node.expr().type();
    const Type *to = node.type();
    if (!from || from == to) return;
    passAccumulator(from, 0);

    // A char is held in an int register, so widening out of one is free and
    // only the narrowings need asking about.
    if (to == Type::realType()) {
        if (from == Type::realType()) return;
        emitter_.call("shm_int_to_real");
    } else if (to == Type::intType()) {
        if (from == Type::charType()) return;
        emitter_.call("shm_real_to_int");
    } else if (to == Type::charType()) {
        emitter_.call(from == Type::realType() ? "shm_real_to_char" : "shm_int_to_char");
    }
}

// Left into a slot, right into the accumulator, then the runtime is asked
// for the answer.
//
// Every operation goes through the runtime. The int ones can fail - passing
// an int limit is an error here, never a wrapped value that looks right - and
// the real ones cannot, but they take the same shape so that the generator
// has one thing to say and the check sits beside the rule it enforces rather
// than being written out three times in three instruction sets. Inlining the
// ones that cannot fail is an optimisation, and deliberately not this pass's
// business yet.
void CodeGen::visit(Binary &node) {
    const Type *operands = node.lhs().type();

    const int slot = reserve();
    evaluate(node.lhs());
    store(operands, slot);
    evaluate(node.rhs());

    // Descending order: argument 1 is taken from the accumulator, which
    // argument 0 would otherwise have overwritten on a target where the two
    // share a register.
    passAccumulator(operands, 1);
    passSlot(operands, slot, 0);

    // Two strings join or compare. The comparison comes back as a number
    // below, at or above zero, and the operator decides which of the six
    // answers that is - so one runtime entry point serves all six.
    if (operands->isArray()) {
        if (node.op() == Binary::Op::Add) {
            release();
            emitter_.call("shm_text_concat");
            return;
        }
        emitter_.call("shm_text_compare");
        // The answer is parked before the zero is loaded. On a target where
        // the accumulator is also the first argument register, loading the
        // zero would otherwise destroy the very number being compared - and
        // it did: 't < "b"' answered 0 for every pair of strings.
        emitter_.storeSlot(Slot::Int, slot);
        emitter_.loadIntConstant(0);
        emitter_.setArg(Slot::Int, 1);
        emitter_.loadSlotIntoArg(Slot::Int, slot, 0);
        release();
        emitter_.call(Binary::runtimeFor(node.op(), Type::intType()));
        return;
    }
    release();
    emitter_.call(Binary::runtimeFor(node.op(), operands));
}

// A call: every argument into a slot of its own first, then the registers
// filled from those slots. Evaluating straight into the registers would not
// work - an argument's own evaluation may itself be a call.
void CodeGen::generateCall(Call &node) {
    const Prototype &proto = *node.prototype();
    const size_t count = node.arguments().size();
    const std::vector<Place> places = planArguments(proto);

    // The overflow block is reserved before anything else, so that its slots
    // are next to each other: the callee is handed one address and counts
    // from it.
    int overflowCount = 0;
    for (const Place &place : places) {
        if (place.overflow) ++overflowCount;
    }
    int blockBase = -1;
    for (int i = 0; i < overflowCount; ++i) {
        const int slot = reserve();
        if (i == 0) blockBase = slot;
    }

    std::vector<int> slots;
    int scratch = node.scratchBase();
    if (proto.returnsByPointer()) scratch += static_cast<int>(proto.outputs.size());

    std::vector<int> referenceSlots(count, -1);
    for (size_t i = 0; i < count; ++i) {
        slots.push_back(reserve());
        evaluate(*node.arguments()[i]);
        if (proto.inputs[i].byReference) {
            // Copy-in: the callee works on its own box, and the value is
            // written back when the call returns. A converted copy would
            // silently stop being the caller's, which is why the checker
            // insisted the types match exactly.
            referenceSlots[i] = scratch++;
            emitter_.storeSlot(slotKind(proto.inputs[i].type), referenceSlots[i]);
        } else {
            emitter_.storeSlot(places[i].kind, slots[i]);
        }
    }

    // The overflow places are filled first, because filling them uses the
    // accumulator and the registers must be left standing.
    for (size_t i = 0; i < places.size(); ++i) {
        if (!places[i].overflow) continue;
        if (i < count && referenceSlots[i] >= 0) {
            emitter_.loadSlotAddress(referenceSlots[i]);
        } else if (i < count) {
            emitter_.loadSlot(places[i].kind, slots[i]);
        } else {
            emitter_.loadSlotAddress(node.scratchBase() + static_cast<int>(i - count));
        }
        emitter_.storeSlot(places[i].kind, blockBase + places[i].index);
    }

    // Then the registers, descending, so that a target which shares one with
    // the accumulator fills it last.
    for (size_t i = places.size(); i-- > 0;) {
        if (places[i].overflow) continue;
        if (i >= count) {
            emitter_.loadSlotAddress(node.scratchBase() + static_cast<int>(i - count));
            emitter_.setArg(Slot::Wide, places[i].index);
        } else if (referenceSlots[i] >= 0) {
            emitter_.loadSlotAddress(referenceSlots[i]);
            emitter_.setArg(Slot::Wide, places[i].index);
        } else {
            emitter_.loadSlotIntoArg(places[i].kind, slots[i], places[i].index);
        }
    }
    if (overflowCount > 0) emitter_.setOverflowBlock(blockBase);

    emitter_.call(mangle(proto.name));

    for (size_t i = 0; i < count; ++i) release();
    for (int i = 0; i < overflowCount; ++i) release();

    // Copy-back, to the variable or the element the argument named.
    for (size_t i = 0; i < count; ++i) {
        if (referenceSlots[i] < 0) continue;
        emitter_.loadSlot(slotKind(proto.inputs[i].type), referenceSlots[i]);
        Var &target = static_cast<Var &>(*node.arguments()[i]);
        writeSymbol(*target.symbol());
    }
}


void CodeGen::generateBuiltin(Call &node) {
    const Builtin &fn = builtin(node.builtin());

    // 'len(A)' is the first dimension, asked of the array itself. The axis
    // goes in first: on a target where the accumulator is also the first
    // argument register, loading the axis afterwards would overwrite the
    // array it had just been put in.
    if (fn.shape == Builtin::Shape::Length) {
        const int slot = reserve();
        evaluate(*node.arguments()[0]);
        emitter_.storeSlot(Slot::Wide, slot);
        release();
        emitter_.loadIntConstant(0);
        emitter_.setArg(Slot::Int, 1);
        emitter_.loadSlotIntoArg(Slot::Wide, slot, 0);
        emitter_.call("shm_array_dim");
        return;
    }

    // Two arguments at most, so no built-in ever overflows the registers.
    const bool intAnswer = node.type() == Type::intType();
    const Slot kind = intAnswer ? Slot::Int : Slot::Real;

    std::vector<int> slots;
    for (size_t i = 0; i < node.arguments().size(); ++i) {
        slots.push_back(reserve());
        evaluate(*node.arguments()[i]);
        emitter_.storeSlot(kind, slots[i]);
    }
    for (size_t i = 0; i < node.arguments().size(); ++i) release();
    for (size_t i = node.arguments().size(); i-- > 0;) {
        emitter_.loadSlotIntoArg(kind, slots[i], static_cast<int>(i));
    }
    emitter_.call(intAnswer ? fn.intSymbol : fn.realSymbol);
}

void CodeGen::visit(Call &node) {
    if (node.builtin() >= 0) { generateBuiltin(node); return; }
    generateCall(node);
    // In an expression a call is worth its first output, which a
    // multi-output function left in the first scratch slot.
    if (node.prototype()->returnsByPointer()) {
        emitter_.loadSlot(slotKind(node.type()), node.scratchBase());
    }
}

void CodeGen::visit(CallStmt &node) {
    Call &call = static_cast<Call &>(*node.call());
    if (call.builtin() >= 0) { generateBuiltin(call); return; }
    generateCall(call);
}

void CodeGen::visit(Return &node) {
    const Prototype &proto = current_->proto();
    if (proto.returnsByPointer()) {
        for (size_t i = 0; i < node.exprs().size(); ++i) {
            evaluate(*node.exprs()[i]);
            emitter_.storeThroughPointer(slotKind(proto.outputs[i]),
                                         current_->outPointerBase() + static_cast<int>(i));
        }
    } else if (!node.exprs().empty()) {
        evaluate(*node.exprs()[0]);
        emitter_.storeSlot(slotKind(proto.outputs[0]), current_->resultSlot());
    }
    emitter_.jump(exitLabel_);
}

void CodeGen::visit(MultiAssign &node) {
    Call &call = static_cast<Call &>(*node.call());
    if (call.builtin() >= 0) {
        generateBuiltin(call);
        writeSymbol(*node.targets()[0]);
        return;
    }
    generateCall(call);
    if (!call.prototype()->returnsByPointer()) {
        writeSymbol(*node.targets()[0]);
        return;
    }
    for (size_t i = 0; i < node.targets().size(); ++i) {
        emitter_.loadSlot(slotKind(node.targets()[i]->type()),
                          call.scratchBase() + static_cast<int>(i));
        writeSymbol(*node.targets()[i]);
    }
}

// Extents go into consecutive slots so that their address can be handed over
// as an array. Slot n sits eight bytes above slot n-1, so extent i goes in
// slot i and the runtime reads them in the order they were written.
void CodeGen::makeArray(const Type *type, std::vector<ExprPtr> &extents, int extentBase) {
    const int rank = static_cast<int>(extents.size());
    for (int i = 0; i < rank; ++i) {
        evaluate(*extents[i]);
        emitter_.widenAccumulator();
        emitter_.storeSlot(Slot::Wide, extentBase + i);
    }
    emitter_.loadSlotAddress(extentBase);
    emitter_.setArg(Slot::Wide, 2);
    emitter_.loadIntConstant(rank);
    emitter_.setArg(Slot::Int, 1);
    emitter_.loadIntConstant(elementKind(type));
    emitter_.setArg(Slot::Int, 0);
    emitter_.call("shm_array_make");
}

void CodeGen::visit(Declare &node) {
    if (node.declaredType()->isArray()) {
        makeArray(node.declaredType(), node.extents(), node.extentBase());
        writeSymbol(*node.symbol());
        if (!node.initial()) return;

        // An initializer shorter than the array fills what it covers and
        // leaves the rest zero, which is what shm_array_fill does.
        const int slot = reserve();
        evaluate(*node.initial());
        emitter_.storeSlot(Slot::Wide, slot);
        release();
        emitter_.loadSlotIntoArg(Slot::Wide, slot, 1);
        readSymbol(*node.symbol());
        emitter_.setArg(Slot::Wide, 0);
        emitter_.call("shm_array_fill");
        return;
    }

    if (!node.initial()) {
        // A declaration with no initializer is the zero of its type, and the
        // frame is not zeroed for us.
        if (isReal(node.declaredType())) emitter_.loadRealConstant(0.0);
        else emitter_.loadIntConstant(0);
    } else {
        evaluate(*node.initial());
    }
    writeSymbol(*node.symbol());
}

// 'A[i] : v'. The container and the index are computed first and parked,
// because evaluating the value may itself be a call.
void CodeGen::assignElement(Index &target, Expr &value) {
    const Type *element = target.type();
    const int container = reserve();
    const int index = reserve();

    evaluate(target.base());
    emitter_.storeSlot(Slot::Wide, container);
    evaluate(target.index());
    emitter_.storeSlot(Slot::Int, index);
    evaluate(value);
    emitter_.setArg(slotKind(element), 2);
    emitter_.loadSlotIntoArg(Slot::Int, index, 1);
    emitter_.loadSlotIntoArg(Slot::Wide, container, 0);
    emitter_.call(setterFor(element));

    release();
    release();
}

void CodeGen::visit(Assign &node) {
    if (Index *element = dynamic_cast<Index *>(node.target().get())) {
        assignElement(*element, *node.expr());
        return;
    }

    const Symbol &symbol = *node.symbol();
    if (!symbol.type()->isArray()) {
        evaluate(*node.expr());
        writeSymbol(symbol);
        return;
    }

    // Assigning an array into a variable that already holds one copies into
    // the storage already there rather than rebinding it: extents are fixed
    // at declaration, and an array may be shared by reference with a caller,
    // so swapping the storage would resize it underneath them.
    // The value stays in its slot until the last use of it, which is the
    // fill below. Releasing it earlier hands the same slot to the next
    // reservation, and the source is then overwritten by the size that was
    // measured from it.
    const int slot = reserve();
    evaluate(*node.expr());
    emitter_.storeSlot(Slot::Wide, slot);

    if (node.creates()) {
        // A name being made by this assignment has no storage to copy into.
        // A literal or a joined string is already fresh; a plain variable is
        // not, so it is copied - 't : s' makes a copy of s.
        if (dynamic_cast<ArrayLit *>(node.expr().get()) ||
            dynamic_cast<StrLit *>(node.expr().get()) ||
            dynamic_cast<Binary *>(node.expr().get())) {
            emitter_.loadSlot(Slot::Wide, slot);
            writeSymbol(symbol);
            release();
            return;
        }
        // Sized to the source, then filled from it.
        const int dims = reserve();
        emitter_.loadIntConstant(0);
        emitter_.setArg(Slot::Int, 1);
        emitter_.loadSlotIntoArg(Slot::Wide, slot, 0);
        emitter_.call("shm_array_dim");
        emitter_.widenAccumulator();
        emitter_.storeSlot(Slot::Wide, dims);
        emitter_.loadSlotAddress(dims);
        emitter_.setArg(Slot::Wide, 2);
        emitter_.loadIntConstant(1);
        emitter_.setArg(Slot::Int, 1);
        emitter_.loadIntConstant(elementKind(symbol.type()));
        emitter_.setArg(Slot::Int, 0);
        emitter_.call("shm_array_make");
        release();
        writeSymbol(symbol);
    }

    emitter_.loadSlotIntoArg(Slot::Wide, slot, 1);
    readSymbol(symbol);
    emitter_.setArg(Slot::Wide, 0);
    emitter_.call("shm_array_fill");
    release();
}

// The target is walked twice: once as a value and once as a destination.
// Sharing one tree rather than copying it is what keeps an index expression
// from having to be duplicated, and evaluating it twice is what the app's
// interpreter does too.
void CodeGen::visit(CompoundAssign &node) {
    const Type *type = node.target()->type();
    const int slot = reserve();
    evaluate(*node.target());
    store(type, slot);
    evaluate(*node.expr());
    release();
    passAccumulator(node.expr()->type(), 1);
    passSlot(type, slot, 0);

    if (type->isArray()) emitter_.call("shm_text_concat");
    else emitter_.call(Binary::runtimeFor(node.isAdd() ? Binary::Op::Add : Binary::Op::Subtract,
                                          type));

    if (Index *element = dynamic_cast<Index *>(node.target().get())) {
        // Recomputing the container and the index is the same double
        // evaluation the read side did.
        const int value = reserve();
        store(type, value);
        const int container = reserve();
        const int index = reserve();
        evaluate(element->base());
        emitter_.storeSlot(Slot::Wide, container);
        evaluate(element->index());
        emitter_.storeSlot(Slot::Int, index);
        load(type, value);
        emitter_.setArg(slotKind(type), 2);
        emitter_.loadSlotIntoArg(Slot::Int, index, 1);
        emitter_.loadSlotIntoArg(Slot::Wide, container, 0);
        emitter_.call(setterFor(type));
        release();
        release();
        release();
        return;
    }

    Var &target = static_cast<Var &>(*node.target());
    if (type->isArray()) {
        // Appending fits the result into the storage the string already has.
        const int joined = reserve();
        emitter_.storeSlot(Slot::Wide, joined);
        emitter_.loadSlotIntoArg(Slot::Wide, joined, 1);
        readSymbol(*target.symbol());
        emitter_.setArg(Slot::Wide, 0);
        emitter_.call("shm_array_fill");
        release();
        return;
    }
    writeSymbol(*target.symbol());
}

// Each item is printed followed by a single space; the newline is appended
// once at the end. So '?' always leaves a trailing space before its newline,
// and '??' leaves the line open for whatever prints next.
void CodeGen::visit(Print &node) {
    for (ExprPtr &item : node.items()) {
        evaluate(*item);
        if (dynamic_cast<Precision *>(item.get())) {
            evaluate(*item);          // a directive: it prints nothing itself
            continue;
        }
        const Type *type = item->type();
        if (type && type->isArray()) {
            emitter_.setArg(Slot::Wide, 0);
            emitter_.call("shm_print_array");
            continue;
        }
        passAccumulator(type, 0);
        if (type == Type::charType()) emitter_.call("shm_print_char");
        else emitter_.call(isReal(type) ? "shm_print_real" : "shm_print_int");
    }
    if (node.newline()) emitter_.call("shm_line_end");
}

void CodeGen::visit(If &node) {
    const int done = newLabel();
    for (If::Branch &branch : node.branches()) {
        const int next = newLabel();
        evaluateCondition(*branch.condition);
        emitter_.jumpIfZero(next);
        generate(branch.body);
        emitter_.jump(done);
        emitter_.label(next);
    }
    if (node.hasElse()) generate(node.elseBody());
    emitter_.label(done);
}

void CodeGen::visit(While &node) {
    const int top = newLabel();
    const int done = newLabel();
    emitter_.label(top);
    evaluateCondition(*node.condition());
    emitter_.jumpIfZero(done);

    // A 'continue' here goes back to the test, which is where the next pass
    // begins. In a 'for' it goes to the step instead, because the step
    // belongs to the loop rather than to the body.
    loops_.push_back(LoopLabels{done, top});
    generate(node.body());
    loops_.pop_back();

    emitter_.jump(top);
    emitter_.label(done);
}

void CodeGen::visit(For &node) {
    if (isReal(node.start()->type())) generateRealLoop(node);
    else generateIntLoop(node);
}

// The pass value is kept sixty-four bits wide and narrowed into the counter
// each time round. Stepping a 32-bit counter can pass the end of its range
// where the language says the loop should simply finish - 'for i :
// 2147483646 to 2147483647 step 2' is the case - and a wide value has
// nowhere to wrap.
void CodeGen::generateIntLoop(For &node) {
    const int endSlot = node.hidden(For::EndSlot);
    const int stepSlot = node.hidden(For::StepSlot);
    const int passSlot = node.hidden(For::PassSlot);

    evaluate(*node.end());
    emitter_.storeSlot(Slot::Int, endSlot);

    if (node.step()) evaluate(*node.step());
    else emitter_.loadIntConstant(1);
    emitter_.storeSlot(Slot::Int, stepSlot);
    emitter_.setArg(Slot::Int, 0);
    emitter_.call("shm_loop_int_check");

    evaluate(*node.start());
    emitter_.widenAccumulator();
    emitter_.storeSlot(Slot::Wide, passSlot);

    const int top = newLabel();
    const int step = newLabel();
    const int done = newLabel();

    emitter_.label(top);
    emitter_.loadSlotIntoArg(Slot::Int, stepSlot, 2);
    emitter_.loadSlotIntoArg(Slot::Int, endSlot, 1);
    emitter_.loadSlotIntoArg(Slot::Wide, passSlot, 0);
    emitter_.call("shm_loop_int_run");
    emitter_.jumpIfZero(done);

    // The counter the program sees is the low half of the wide pass value.
    emitter_.loadSlot(Slot::Wide, passSlot);
    emitter_.storeSlot(Slot::Int, node.counter()->slot());

    loops_.push_back(LoopLabels{done, step});
    generate(node.body());
    loops_.pop_back();

    emitter_.label(step);
    emitter_.loadSlotIntoArg(Slot::Int, stepSlot, 1);
    emitter_.loadSlotIntoArg(Slot::Wide, passSlot, 0);
    emitter_.call("shm_loop_int_advance");
    emitter_.storeSlot(Slot::Wide, passSlot);
    emitter_.jump(top);

    emitter_.label(done);
}

// start + n * step, recomputed each pass rather than accumulated. That is
// what the app does, and an accumulating loop drifts from it in the last
// digits - which a program that prints its counter would show.
void CodeGen::generateRealLoop(For &node) {
    const int startSlot = node.hidden(For::StartSlot);
    const int endSlot = node.hidden(For::EndSlot);
    const int stepSlot = node.hidden(For::StepSlot);
    const int passSlot = node.hidden(For::PassSlot);

    evaluate(*node.start());
    emitter_.storeSlot(Slot::Real, startSlot);
    evaluate(*node.end());
    emitter_.storeSlot(Slot::Real, endSlot);
    if (node.step()) evaluate(*node.step());
    else emitter_.loadRealConstant(1.0);
    emitter_.storeSlot(Slot::Real, stepSlot);

    emitter_.loadSlotIntoArg(Slot::Real, stepSlot, 2);
    emitter_.loadSlotIntoArg(Slot::Real, endSlot, 1);
    emitter_.loadSlotIntoArg(Slot::Real, startSlot, 0);
    emitter_.call("shm_loop_real_check");

    emitter_.loadRealConstant(0.0);
    emitter_.storeSlot(Slot::Real, passSlot);

    const int top = newLabel();
    const int step = newLabel();
    const int done = newLabel();

    emitter_.label(top);
    emitter_.loadSlotIntoArg(Slot::Real, passSlot, 2);
    emitter_.loadSlotIntoArg(Slot::Real, stepSlot, 1);
    emitter_.loadSlotIntoArg(Slot::Real, startSlot, 0);
    emitter_.call("shm_loop_real_value");
    emitter_.storeSlot(Slot::Real, node.counter()->slot());

    emitter_.loadSlotIntoArg(Slot::Real, stepSlot, 2);
    emitter_.loadSlotIntoArg(Slot::Real, endSlot, 1);
    emitter_.loadSlotIntoArg(Slot::Real, node.counter()->slot(), 0);
    emitter_.call("shm_loop_real_run");
    emitter_.jumpIfZero(done);

    loops_.push_back(LoopLabels{done, step});
    generate(node.body());
    loops_.pop_back();

    emitter_.label(step);
    emitter_.loadRealConstant(1.0);
    emitter_.setArg(Slot::Real, 1);
    emitter_.loadSlotIntoArg(Slot::Real, passSlot, 0);
    emitter_.call("shm_real_add");
    emitter_.storeSlot(Slot::Real, passSlot);
    emitter_.jump(top);

    emitter_.label(done);
}

void CodeGen::visit(Break &) {
    emitter_.jump(loops_.back().breakTo);
}

void CodeGen::visit(Continue &) {
    emitter_.jump(loops_.back().continueTo);
}

}  // namespace shalimar
