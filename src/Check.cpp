#include "Check.h"

namespace shalimar {

bool Checker::check(Program &program) {
    // Execution begins at main(), which takes no inputs.
    if (!program.find("main")) diag_.error(0, "No main() function defined");

    for (std::unique_ptr<Function> &f : program.functions()) check(*f);
    return !diag_.hasErrors();
}

void Checker::check(Function &function) {
    frame_ = &function.frame();
    for (StmtPtr &s : function.body()) check(*s);
    frame_ = nullptr;
}

void Checker::check(Stmt &statement) {
    line_ = statement.line();
    statement.accept(*this);
}

// Typing an expression is a walk that leaves the answer on the node, which
// the code generator reads back rather than deciding a second time. It also
// measures: the depth reached here is what the frame has to reserve.
const Type *Checker::typeOf(Expr &expr) {
    expr.accept(*this);
    return expr.type();
}

void Checker::visit(IntLit &node) {
    node.setType(Type::intType());
}

// The left operand waits in a slot while the right is evaluated, so the depth
// the frame must hold is one more than the deeper of the two sides.
void Checker::visit(Binary &node) {
    typeOf(node.lhs());
    if (frame_) frame_->needEvaluationDepth(++depth_);
    typeOf(node.rhs());
    if (frame_) --depth_;
    node.setType(Type::intType());
}

void Checker::visit(Print &node) {
    for (ExprPtr &item : node.items()) typeOf(*item);
}

}  // namespace shalimar
