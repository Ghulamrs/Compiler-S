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

int CodeGen::reserve() { return depth_++; }
void CodeGen::release() { --depth_; }

void CodeGen::visit(IntLit &node) {
    emitter_.loadInt(node.value());
}

// Left into a slot, right into the accumulator, then the runtime is asked
// for the answer.
//
// Every int operation can fail - passing an int limit is an error, never a
// wrapped value that looks right - so all of them go through the runtime,
// where the check and the message it produces sit beside the rule they
// enforce. Inlining the ones that cannot fail is an optimisation, and it is
// deliberately not this pass's business yet.
void CodeGen::visit(Binary &node) {
    const int slot = reserve();
    evaluate(node.lhs());
    emitter_.spillInt(slot);
    evaluate(node.rhs());
    release();

    // Descending order: argument 1 is taken from the accumulator, which
    // argument 0 would otherwise have overwritten on a target where the two
    // share a register.
    emitter_.setIntArg(1);
    emitter_.loadSlotIntoIntArg(slot, 0);
    emitter_.callRuntime(Binary::intRuntime(node.op()));
}

// Each item is printed followed by a single space; the newline is appended
// once at the end. So '?' always leaves a trailing space before its newline,
// and '??' leaves the line open for whatever prints next.
void CodeGen::visit(Print &node) {
    for (ExprPtr &item : node.items()) {
        evaluate(*item);
        emitter_.setIntArg(0);
        emitter_.callRuntime("shm_print_int");
    }
    if (node.newline()) emitter_.callRuntime("shm_line_end");
}

}  // namespace shalimar
