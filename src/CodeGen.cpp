#include "CodeGen.h"

#include "backend/Emitter.h"

namespace shalimar {

std::string CodeGen::mangle(const std::string &name) {
    return name == "main" ? "shm_user_main" : "shmf_" + name;
}

void CodeGen::run(Program &program, const std::string &sourceName) {
    emitter_.beginModule(sourceName);
    for (std::unique_ptr<Function> &f : program.functions()) generate(*f);
    emitter_.endModule();
}

void CodeGen::generate(Function &function) {
    const Prototype &proto = function.proto();
    evaluationBase_ = function.frame().evaluationBase();
    depth_ = 0;
    current_ = &function;
    exitLabel_ = newLabel();

    emitter_.beginFunction(mangle(proto.name), function.frame().slots());

    // Parameters come first and in order, so argument n goes to slot n. A
    // reference parameter's slot holds the caller's address rather than the
    // value, which is why it is spilled wide whatever the parameter's type.
    for (size_t i = 0; i < proto.inputs.size(); ++i) {
        const Slot kind = proto.inputs[i].byReference ? Slot::Wide
                                                      : slotKind(proto.inputs[i].type);
        emitter_.spillArgument(kind, registerIndex(proto, i), static_cast<int>(i));
    }
    // Then the addresses of the outputs, when there is more than one.
    if (proto.returnsByPointer()) {
        for (size_t i = 0; i < proto.outputs.size(); ++i) {
            emitter_.spillArgument(Slot::Wide,
                                   registerIndex(proto, proto.inputs.size() + i),
                                   function.outPointerBase() + static_cast<int>(i));
        }
    }

    // The recursion ceiling. Its message names the function, so the name goes
    // into the module's read-only bytes.
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

    for (StmtPtr &s : function.body()) generate(*s);

    emitter_.label(exitLabel_);
    emitter_.loadIntConstant(proto.id);
    emitter_.setArg(Slot::Int, 0);
    emitter_.call("shm_leave");
    // The result is fetched after the counter comes down, because that is a
    // call and a call does not preserve an accumulator.
    if (function.resultSlot() >= 0) {
        emitter_.loadSlot(slotKind(proto.outputs[0]), function.resultSlot());
    }
    emitter_.endFunction();
    current_ = nullptr;
}

int CodeGen::registerIndex(const Prototype &proto, size_t position) const {
    if (emitter_.positionalArguments()) return static_cast<int>(position);

    // Count the arguments of the same file that come before it. An output
    // pointer is an integer whatever it points at.
    const bool wantReal = position < proto.inputs.size() &&
                          !proto.inputs[position].byReference &&
                          isReal(proto.inputs[position].type);
    int index = 0;
    for (size_t i = 0; i < position && i < proto.inputs.size(); ++i) {
        const bool isRealArg = !proto.inputs[i].byReference && isReal(proto.inputs[i].type);
        if (isRealArg == wantReal) ++index;
    }
    if (position >= proto.inputs.size() && !wantReal) {
        index += static_cast<int>(position - proto.inputs.size());
    }
    return index;
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

int CodeGen::reserve() { return evaluationBase_ + depth_++; }
void CodeGen::release() { --depth_; }

bool CodeGen::isReal(const Type *type) {
    return type && type->kind() == Type::Kind::Real;
}

Slot CodeGen::slotKind(const Type *type) {
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
    if (symbol.isReference()) emitter_.loadThroughPointer(slotKind(symbol.type()), symbol.slot());
    else emitter_.loadSlot(slotKind(symbol.type()), symbol.slot());
}

void CodeGen::writeSymbol(const Symbol &symbol) {
    if (symbol.isReference()) emitter_.storeThroughPointer(slotKind(symbol.type()), symbol.slot());
    else emitter_.storeSlot(slotKind(symbol.type()), symbol.slot());
}

void CodeGen::visit(Var &node) {
    readSymbol(*node.symbol());
}

void CodeGen::visit(StrLit &node) {
    emitter_.defineBytes(node.id(), node.text() + std::string(1, '\0'));
    emitter_.loadBytesAddress(node.id());
}

void CodeGen::visit(Convert &node) {
    evaluate(node.expr());
    const Type *from = node.expr().type();
    if (from == node.type()) return;
    passAccumulator(from, 0);
    emitter_.call(isReal(node.type()) ? "shm_int_to_real" : "shm_real_to_int");
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
    release();

    // Descending order: argument 1 is taken from the accumulator, which
    // argument 0 would otherwise have overwritten on a target where the two
    // share a register.
    passAccumulator(operands, 1);
    passSlot(operands, slot, 0);
    emitter_.call(Binary::runtimeFor(node.op(), operands));
}

// A call: every argument into a slot of its own first, then the registers
// filled from those slots. Evaluating straight into the registers would not
// work - an argument's own evaluation may itself be a call.
void CodeGen::generateCall(Call &node) {
    const Prototype &proto = *node.prototype();
    const size_t count = node.arguments().size();

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
            emitter_.storeSlot(slotKind(proto.inputs[i].type), slots[i]);
        }
    }
    for (size_t i = 0; i < count; ++i) release();

    // Descending, so that a target which shares a register with the
    // accumulator is filled last.
    if (proto.returnsByPointer()) {
        for (size_t i = proto.outputs.size(); i-- > 0;) {
            emitter_.loadSlotAddress(node.scratchBase() + static_cast<int>(i));
            emitter_.setArg(Slot::Wide, registerIndex(proto, proto.inputs.size() + i));
        }
    }
    for (size_t i = count; i-- > 0;) {
        const int where = registerIndex(proto, i);
        if (proto.inputs[i].byReference) {
            emitter_.loadSlotAddress(referenceSlots[i]);
            emitter_.setArg(Slot::Wide, where);
        } else {
            emitter_.loadSlotIntoArg(slotKind(proto.inputs[i].type), slots[i], where);
        }
    }

    emitter_.call(mangle(proto.name));

    // Copy-back, to the variable or the element the argument named.
    for (size_t i = 0; i < count; ++i) {
        if (referenceSlots[i] < 0) continue;
        emitter_.loadSlot(slotKind(proto.inputs[i].type), referenceSlots[i]);
        Var &target = static_cast<Var &>(*node.arguments()[i]);
        writeSymbol(*target.symbol());
    }
}

void CodeGen::visit(Call &node) {
    generateCall(node);
    // In an expression a call is worth its first output, which a
    // multi-output function left in the first scratch slot.
    if (node.prototype()->returnsByPointer()) {
        emitter_.loadSlot(slotKind(node.type()), node.scratchBase());
    }
}

void CodeGen::visit(CallStmt &node) {
    generateCall(static_cast<Call &>(*node.call()));
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
    generateCall(call);
    for (size_t i = 0; i < node.targets().size(); ++i) {
        emitter_.loadSlot(slotKind(node.targets()[i]->type()),
                          call.scratchBase() + static_cast<int>(i));
        writeSymbol(*node.targets()[i]);
    }
}

void CodeGen::visit(Declare &node) {
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

void CodeGen::visit(Assign &node) {
    evaluate(*node.expr());
    writeSymbol(*node.symbol());
}

// Each item is printed followed by a single space; the newline is appended
// once at the end. So '?' always leaves a trailing space before its newline,
// and '??' leaves the line open for whatever prints next.
void CodeGen::visit(Print &node) {
    for (ExprPtr &item : node.items()) {
        evaluate(*item);
        const Type *type = item->type();
        if (type && type->isArray()) {
            emitter_.setArg(Slot::Wide, 0);
            emitter_.call("shm_print_text");
            continue;
        }
        passAccumulator(type, 0);
        emitter_.call(isReal(type) ? "shm_print_real" : "shm_print_int");
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
