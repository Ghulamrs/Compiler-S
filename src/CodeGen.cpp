#include "CodeGen.h"

#include "Resolve.h"

#include "Builtin.h"
#include "backend/Emitter.h"

#include "../runtime/shmrt.h"

namespace shalimar {

std::string CodeGen::mangle(const std::string &name) {
    if (name == "main") return "shm_user_main";

    if (name.empty()) return "shm_init_globals";
    return "shmf_" + name;
}

void CodeGen::run(Program &program, const std::string &sourceName,
                  const std::vector<std::string> &units) {
    emitter_.beginModule(sourceName);
    emitter_.defineGlobals(program.globalSlots());
    units_ = units;

    std::set<int> named;
    for (std::unique_ptr<Function> &f : program.functions()) {
        if (f->proto().unit > 0) named.insert(f->proto().unit);
    }
    for (StmtPtr &g : program.globals()) {
        if (g->unit() > 0) named.insert(g->unit());
    }

    named.insert(0);
    generateFileNames(named);

    reachableFromGlobals(program);

    Function &initializer = program.initializer();
    initializer.body().clear();
    for (StmtPtr &declaration : program.globals()) {
        initializer.body().push_back(StmtPtr(declaration.release()));
    }
    generate(initializer);

    for (std::unique_ptr<Function> &f : program.functions()) generate(*f);
    emitter_.endModule();
}

void CodeGen::generateFileNames(const std::set<int> &units) {
    emitter_.beginFunction("shm_name_files");
    for (int unit : units) {
        if (static_cast<size_t>(unit) >= units_.size()) continue;
        const int id = newBytesId();
        emitter_.defineBytes(id, units_[static_cast<size_t>(unit)] + std::string(1, '\0'));
        emitter_.loadBytesAddress(id);
        emitter_.setArg(Slot::Wide, 1);
        emitter_.loadIntConstant(unit);
        emitter_.setArg(Slot::Int, 0);
        emitter_.call("shm_name_file");
    }
    emitter_.endFunction(0);
}

void CodeGen::reachableFromGlobals(Program &program) {
    initializerCanReach_.clear();

    std::vector<std::string> wanted;
    for (StmtPtr &g : program.globals()) {
        if (!g) continue;
        for (const std::string &name : calledNamesIn(*g)) wanted.push_back(name);
    }

    while (!wanted.empty()) {
        const std::string name = wanted.back();
        wanted.pop_back();
        if (!initializerCanReach_.insert(name).second) continue;
        for (std::unique_ptr<Function> &f : program.functions()) {
            if (f->proto().name != name) continue;
            for (const std::string &next : calledNamesIn(*f)) wanted.push_back(next);
        }
    }
}

void CodeGen::generate(Function &function) {

    checkGlobalReads_ = function.proto().name.empty() ||
                        initializerCanReach_.count(function.proto().name) > 0;

    const Prototype &proto = function.proto();
    evaluationBase_ = function.frame().evaluationBase();
    depth_ = 0;
    deepest_ = 0;
    current_ = &function;
    exitLabel_ = newLabel();

    emitter_.beginFunction(mangle(proto.name));

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

    if (function.resultSlot() >= 0) {
        emitter_.loadSlot(slotKind(proto.outputs[0]), function.resultSlot());
    }
    emitter_.endFunction(function.frame().variables() + deepest_);
    current_ = nullptr;
}

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

    emitter_.setLine(statement.unit(), statement.line());
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

int CodeGen::reserve() {
    const int slot = evaluationBase_ + depth_++;
    if (depth_ > deepest_) deepest_ = depth_;
    return slot;
}

void CodeGen::release() { --depth_; }

bool CodeGen::isReal(const Type *type) {
    return type && type->kind() == Type::Kind::Real;
}

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

    if (checkGlobalReads_ && node.symbol()->isGlobal()) {
        const int id = newBytesId();
        emitter_.defineBytes(id, node.symbol()->name() + std::string(1, '\0'));
        emitter_.loadBytesAddress(id);
        emitter_.setArg(Slot::Wide, 1);
        emitter_.loadIntConstant(node.symbol()->slot());
        emitter_.setArg(Slot::Int, 0);
        emitter_.call("shm_globals_ready");
    }
    readSymbol(*node.symbol());
}

void CodeGen::visit(StrLit &node) {
    emitter_.defineBytes(node.id(), node.text() + std::string(1, '\0'));
    emitter_.loadIntConstant(static_cast<int32_t>(node.text().size()));
    emitter_.setArg(Slot::Int, 1);
    emitter_.loadBytesAddress(node.id());
    emitter_.setArg(Slot::Wide, 0);
    emitter_.call("shm_array_from_text");
}

void CodeGen::visit(Blank &) {

}

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

void CodeGen::convertAccumulator(const Type *from, const Type *to) {
    if (!from || !to || from == to) return;
    passAccumulator(from, 0);

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

void CodeGen::visit(Convert &node) {
    evaluate(node.expr());
    convertAccumulator(node.expr().type(), node.type());
}

void CodeGen::visit(Binary &node) {
    const Type *operands = node.lhs().type();

    const int slot = reserve();
    evaluate(node.lhs());
    store(operands, slot);
    evaluate(node.rhs());

    passAccumulator(operands, 1);
    passSlot(operands, slot, 0);

    if (operands->isArray()) {
        if (node.op() == Binary::Op::Add) {
            release();
            emitter_.call("shm_text_concat");
            return;
        }
        emitter_.call("shm_text_compare");

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

void CodeGen::generateCall(Call &node) {
    const Prototype &proto = *node.prototype();
    const size_t count = node.arguments().size();
    const std::vector<Place> places = planArguments(proto);

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

            referenceSlots[i] = scratch++;
            emitter_.storeSlot(slotKind(proto.inputs[i].type), referenceSlots[i]);
        } else {
            emitter_.storeSlot(places[i].kind, slots[i]);
        }
    }

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

    // A foreign function keeps the name its own compiler gave it. mangle()
    // would make it shmf_, which marks a function this compiler wrote.
    emitter_.call(proto.isForeign ? proto.name : mangle(proto.name));

    for (size_t i = 0; i < count; ++i) release();
    for (int i = 0; i < overflowCount; ++i) release();

    for (size_t i = 0; i < count; ++i) {
        if (referenceSlots[i] < 0) continue;
        const Type *type = proto.inputs[i].type;
        Expr &argument = *node.arguments()[i];

        if (Index *element = dynamic_cast<Index *>(&argument)) {

            const int container = reserve();
            const int index = reserve();

            evaluate(element->base());
            emitter_.storeSlot(Slot::Wide, container);
            evaluate(element->index());
            emitter_.storeSlot(Slot::Int, index);

            emitter_.loadSlotIntoArg(slotKind(type), referenceSlots[i], 2);
            emitter_.loadSlotIntoArg(Slot::Int, index, 1);
            emitter_.loadSlotIntoArg(Slot::Wide, container, 0);
            emitter_.call(setterFor(type));

            release();
            release();
            continue;
        }

        emitter_.loadSlot(slotKind(type), referenceSlots[i]);
        Var &target = static_cast<Var &>(argument);
        writeSymbol(*target.symbol());
    }
}

void CodeGen::generateBuiltin(Call &node) {
    const Builtin &fn = builtin(node.builtin());

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
        convertAccumulator(call.type(), node.targets()[0]->type());
        writeSymbol(*node.targets()[0]);
        return;
    }

    const Prototype &proto = *call.prototype();
    generateCall(call);
    if (!proto.returnsByPointer()) {
        convertAccumulator(proto.outputs.empty() ? call.type() : proto.outputs[0],
                           node.targets()[0]->type());
        writeSymbol(*node.targets()[0]);
        return;
    }

    for (size_t i = 0; i < node.targets().size(); ++i) {
        const Type *output = proto.outputs[i];
        emitter_.loadSlot(slotKind(output), call.scratchBase() + static_cast<int>(i));
        convertAccumulator(output, node.targets()[i]->type());
        writeSymbol(*node.targets()[i]);
    }
}

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

void CodeGen::markGlobalMade(const Symbol &symbol) {
    if (!symbol.isGlobal()) return;
    emitter_.loadIntConstant(symbol.slot() + 1);
    emitter_.setArg(Slot::Int, 0);
    emitter_.call("shm_globals_made");
}

void CodeGen::visit(Declare &node) {
    if (node.declaredType()->isArray()) {
        makeArray(node.declaredType(), node.extents(), node.extentBase());
        writeSymbol(*node.symbol());

        markGlobalMade(*node.symbol());
        if (!node.initial()) return;

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

        if (isReal(node.declaredType())) emitter_.loadRealConstant(0.0);
        else emitter_.loadIntConstant(0);
    } else {
        evaluate(*node.initial());
    }
    writeSymbol(*node.symbol());
    markGlobalMade(*node.symbol());
}

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

    const int slot = reserve();
    evaluate(*node.expr());
    emitter_.storeSlot(Slot::Wide, slot);

    if (node.creates()) {

        if (dynamic_cast<ArrayLit *>(node.expr().get()) ||
            dynamic_cast<StrLit *>(node.expr().get()) ||
            dynamic_cast<Binary *>(node.expr().get())) {
            emitter_.loadSlot(Slot::Wide, slot);
            writeSymbol(symbol);
            release();
            return;
        }

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

void CodeGen::visit(Print &node) {
    for (ExprPtr &item : node.items()) {
        evaluate(*item);

        if (dynamic_cast<Precision *>(item.get())) continue;
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

}
