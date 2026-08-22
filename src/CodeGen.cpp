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

void CodeGen::evaluate(Expr &expr) {
    expr.accept(*this);
}

int CodeGen::reserve() { return evaluationBase_ + depth_++; }
void CodeGen::release() { --depth_; }

bool CodeGen::isReal(const Type *type) {
    return type && type->kind() == Type::Kind::Real;
}

void CodeGen::store(const Type *type, int slot) {
    if (isReal(type)) emitter_.storeRealSlot(slot);
    else emitter_.storeIntSlot(slot);
}

void CodeGen::load(const Type *type, int slot) {
    if (isReal(type)) emitter_.loadRealSlot(slot);
    else emitter_.loadIntSlot(slot);
}

void CodeGen::passAccumulator(const Type *type, int index) {
    if (isReal(type)) emitter_.setRealArg(index);
    else emitter_.setIntArg(index);
}

void CodeGen::passSlot(const Type *type, int slot, int index) {
    if (isReal(type)) emitter_.loadRealSlotIntoArg(slot, index);
    else emitter_.loadIntSlotIntoArg(slot, index);
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

}  // namespace shalimar
