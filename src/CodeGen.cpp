#include "CodeGen.h"

#include "backend/Emitter.h"

namespace shalimar {

std::string CodeGen::mangle(const std::string &name) {
    return name == "main" ? "shm_user_main" : "shmf_" + name;
}

void CodeGen::run(const Program &program, const std::string &sourceName) {
    emitter_.beginModule(sourceName);
    for (const std::unique_ptr<Function> &f : program.functions()) generate(*f);
    emitter_.endModule();
}

void CodeGen::generate(const Function &function) {
    emitter_.beginFunction(mangle(function.proto().name()));
    for (const StmtPtr &s : function.body()) generate(*s);
    emitter_.endFunction();
}

void CodeGen::generate(const Stmt &statement) {
    statement.accept(*this);
}

void CodeGen::evaluate(const Expr &expr) {
    expr.accept(*this);
}

void CodeGen::visit(const IntLit &node) {
    emitter_.loadInt(node.value());
}

// Each item is printed followed by a single space; the newline is appended
// once at the end. So '?' always leaves a trailing space before its newline,
// and '??' leaves the line open for whatever prints next.
void CodeGen::visit(const Print &node) {
    for (const ExprPtr &item : node.items()) {
        evaluate(*item);
        emitter_.setIntArg(0);
        emitter_.callRuntime("shm_print_int");
    }
    if (node.newline()) emitter_.callRuntime("shm_line_end");
}

}  // namespace shalimar
