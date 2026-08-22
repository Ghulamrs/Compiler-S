#include "Check.h"

namespace shalimar {

bool Checker::check(Program &program) {
    // Execution begins at main(), which takes no inputs.
    if (!program.find("main")) diag_.error(0, "No main() function defined");

    for (std::unique_ptr<Function> &f : program.functions()) check(*f);
    return !diag_.hasErrors();
}

void Checker::check(Function &function) {
    function_ = &function;
    scope_.clear();
    scope_.push();
    for (StmtPtr &s : function.body()) check(*s);
    scope_.pop();
    function_ = nullptr;
}

void Checker::check(Stmt &statement) {
    line_ = statement.line();
    statement.accept(*this);
}

void Checker::deeper() {
    if (function_) function_->frame().needEvaluationDepth(++depth_);
}

void Checker::shallower() {
    if (function_) --depth_;
}

const Type *Checker::typeOf(ExprPtr &expr) {
    expr->accept(*this);
    return expr->type();
}

void Checker::coerce(ExprPtr &expr, const Type *to) {
    if (!to || !expr->type() || expr->type() == to) return;
    ExprPtr inner(expr.release());
    expr.reset(new Convert(std::move(inner), to));
}

// int beside real widens to real. That is the whole rule at this size, and
// writing it as a function rather than as an if in three places is what will
// keep it one rule when char and the arrays arrive.
const Type *Checker::common(const Type *a, const Type *b) const {
    if (!a || !b) return nullptr;
    if (a == b) return a;
    const bool numeric = (a == Type::intType() || a == Type::realType()) &&
                         (b == Type::intType() || b == Type::realType());
    if (!numeric) return nullptr;
    return Type::realType();
}

void Checker::visit(IntLit &node) {
    node.setType(Type::intType());
}

void Checker::visit(RealLit &node) {
    node.setType(Type::realType());
}

void Checker::visit(Var &node) {
    const Symbol *symbol = scope_.lookup(node.name());
    if (!symbol) {
        diag_.error(line_, "Undefined variable '" + node.name() + "'");
        return;
    }
    node.resolve(symbol);
    node.setType(symbol->type());
}

// A Convert the checker made carries its own answer; one it is asked to
// re-walk only needs its operand typed.
void Checker::visit(Convert &node) {
    typeOf(node.operand());
}

void Checker::visit(Binary &node) {
    const Type *left = typeOf(node.left());
    deeper();
    const Type *right = typeOf(node.right());
    shallower();
    if (!left || !right) return;

    const Type *operands = common(left, right);
    if (!operands) {
        diag_.error(line_, std::string("'") + Binary::spelling(node.op()) +
                               "' does not apply to " + left->spelling());
        return;
    }
    coerce(node.left(), operands);
    coerce(node.right(), operands);
    node.setType(Binary::yieldsInt(node.op()) ? Type::intType() : operands);
}

void Checker::visit(Declare &node) {
    if (scope_.definedHere(node.name())) {
        diag_.error(line_, "'" + node.name() + "' is already declared");
        return;
    }
    if (node.initial()) {
        typeOf(node.initial());
        // Both directions are silent at a declared destination. real to int
        // drops the fraction with no diagnostic, which is the language's
        // sharpest edge and is the document's word rather than an oversight.
        coerce(node.initial(), node.declaredType());
    }
    Symbol *symbol = function_->declare(node.name(), node.declaredType());
    scope_.define(node.name(), symbol);
    node.resolve(symbol);
}

// A scalar may be created by assigning to it; its type is inferred from the
// expression. An existing one keeps the type it was declared with, and the
// value converts into it.
void Checker::visit(Assign &node) {
    const Type *value = typeOf(node.expr());

    Var &target = static_cast<Var &>(*node.target());
    const Symbol *existing = scope_.lookup(target.name());
    if (!existing) {
        if (!value) return;
        Symbol *created = function_->declare(target.name(), value);
        scope_.define(target.name(), created);
        existing = created;
    }
    target.resolve(existing);
    target.setType(existing->type());
    coerce(node.expr(), existing->type());
    node.resolve(existing);
}

void Checker::visit(Print &node) {
    for (ExprPtr &item : node.items()) typeOf(item);
}

}  // namespace shalimar
