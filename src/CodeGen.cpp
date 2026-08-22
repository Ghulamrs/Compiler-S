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
    evaluationBase_ = function.frame().evaluationBase();
    depth_ = 0;
    emitter_.beginFunction(mangle(function.proto().name()), function.frame().slots());
    for (StmtPtr &s : function.body()) generate(*s);
    emitter_.endFunction();
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
    emitter_.callRuntime("shm_real_truth");
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

void CodeGen::visit(Var &node) {
    load(node.type(), node.symbol()->slot());
}

void CodeGen::visit(Convert &node) {
    evaluate(node.expr());
    const Type *from = node.expr().type();
    if (from == node.type()) return;
    passAccumulator(from, 0);
    emitter_.callRuntime(isReal(node.type()) ? "shm_int_to_real" : "shm_real_to_int");
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
    emitter_.callRuntime(Binary::runtimeFor(node.op(), operands));
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
    store(node.declaredType(), node.symbol()->slot());
}

void CodeGen::visit(Assign &node) {
    evaluate(*node.expr());
    store(node.symbol()->type(), node.symbol()->slot());
}

// Each item is printed followed by a single space; the newline is appended
// once at the end. So '?' always leaves a trailing space before its newline,
// and '??' leaves the line open for whatever prints next.
void CodeGen::visit(Print &node) {
    for (ExprPtr &item : node.items()) {
        evaluate(*item);
        const Type *type = item->type();
        passAccumulator(type, 0);
        emitter_.callRuntime(isReal(type) ? "shm_print_real" : "shm_print_int");
    }
    if (node.newline()) emitter_.callRuntime("shm_line_end");
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
    emitter_.callRuntime("shm_loop_int_check");

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
    emitter_.callRuntime("shm_loop_int_run");
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
    emitter_.callRuntime("shm_loop_int_advance");
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
    emitter_.callRuntime("shm_loop_real_check");

    emitter_.loadRealConstant(0.0);
    emitter_.storeSlot(Slot::Real, passSlot);

    const int top = newLabel();
    const int step = newLabel();
    const int done = newLabel();

    emitter_.label(top);
    emitter_.loadSlotIntoArg(Slot::Real, passSlot, 2);
    emitter_.loadSlotIntoArg(Slot::Real, stepSlot, 1);
    emitter_.loadSlotIntoArg(Slot::Real, startSlot, 0);
    emitter_.callRuntime("shm_loop_real_value");
    emitter_.storeSlot(Slot::Real, node.counter()->slot());

    emitter_.loadSlotIntoArg(Slot::Real, stepSlot, 2);
    emitter_.loadSlotIntoArg(Slot::Real, endSlot, 1);
    emitter_.loadSlotIntoArg(Slot::Real, node.counter()->slot(), 0);
    emitter_.callRuntime("shm_loop_real_run");
    emitter_.jumpIfZero(done);

    loops_.push_back(LoopLabels{done, step});
    generate(node.body());
    loops_.pop_back();

    emitter_.label(step);
    emitter_.loadRealConstant(1.0);
    emitter_.setArg(Slot::Real, 1);
    emitter_.loadSlotIntoArg(Slot::Real, passSlot, 0);
    emitter_.callRuntime("shm_real_add");
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
