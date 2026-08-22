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

// A condition may be any scalar. An array is not a scalar and is refused -
// including a string, so 'if "a"' is an error rather than silently true.
void Checker::checkCondition(ExprPtr &expr) {
    const Type *type = typeOf(expr);
    if (type && type->isArray()) {
        diag_.error(line_, "A condition must be a scalar");
    }
}

void Checker::checkBlock(Block &body) {
    scope_.push();
    for (StmtPtr &s : body) check(*s);
    scope_.pop();
}

void Checker::visit(If &node) {
    for (If::Branch &branch : node.branches()) {
        checkCondition(branch.condition);
        checkBlock(branch.body);
    }
    if (node.hasElse()) checkBlock(node.elseBody());
}

void Checker::visit(While &node) {
    checkCondition(node.condition());
    checkBlock(node.body());
}

// If every bound is int the loop runs in ints; a real anywhere widens the
// counter and the loop runs in reals. Because the short form was expanded
// before this decision, 'for i < 2.9' runs 0 to 1.9 and stops at 1.
void Checker::visit(For &node) {
    const Type *counterType = common(typeOf(node.start()), typeOf(node.end()));
    if (node.step()) counterType = common(counterType, typeOf(node.step()));
    if (!counterType) counterType = Type::intType();

    coerce(node.start(), counterType);
    coerce(node.end(), counterType);
    if (node.step()) coerce(node.step(), counterType);

    warnIfLoopNeverRuns(node);

    const int base = function_->addHiddenSlot();
    for (int i = 1; i < For::HiddenSlotCount; ++i) function_->addHiddenSlot();
    node.setHiddenBase(base);

    // The counter belongs to the loop: a scope of its own, so it never
    // collides with an outer name and is gone afterwards.
    scope_.push();
    Symbol *counter = function_->declare(node.variable(), counterType);
    scope_.define(node.variable(), counter);
    node.resolve(counter);
    checkBlock(node.body());
    scope_.pop();
}

void Checker::visit(Break &) {}
void Checker::visit(Continue &) {}

bool Checker::constantNumber(const Expr &expr, double &value) const {
    if (expr.isIntLiteral())  { value = static_cast<const IntLit &>(expr).value();  return true; }
    if (expr.isRealLiteral()) { value = static_cast<const RealLit &>(expr).value(); return true; }
    if (const Convert *conversion = dynamic_cast<const Convert *>(&expr)) {
        return constantNumber(conversion->expr(), value);
    }
    const Binary *binary = dynamic_cast<const Binary *>(&expr);
    if (!binary) return false;
    double left = 0.0;
    double right = 0.0;
    if (!constantNumber(binary->lhs(), left)) return false;
    if (!constantNumber(binary->rhs(), right)) return false;
    switch (binary->op()) {
    case Binary::Op::Add:      value = left + right; return true;
    case Binary::Op::Subtract: value = left - right; return true;
    case Binary::Op::Multiply: value = left * right; return true;
    default: return false;
    }
}

std::string Checker::number(double value) {
    if (value == static_cast<double>(static_cast<long long>(value)) &&
        value >= -9.2e18 && value <= 9.2e18) {
        return std::to_string(static_cast<long long>(value));
    }
    return std::to_string(value);
}

// 'for i : 10 to 1 step 1' counts up from a start already past its end, so it
// runs zero times - the step points away from the end rather than toward it.
// Nothing about that is illegal, so this is a warning and the program runs.
//
// Only a loop whose three bounds all fold is judged. That is the case where
// the direction is written into the source and nothing else could have been
// meant; where a bound is computed, an empty pass may be exactly what that
// run intends, and 'for j < v.col' over a vector - which expands to 0 to -2 -
// is the language's own example of one.
void Checker::warnIfLoopNeverRuns(For &node) {
    double start = 0.0;
    double end = 0.0;
    double step = 1.0;
    if (!constantNumber(*node.start(), start)) return;
    if (!constantNumber(*node.end(), end)) return;
    if (node.step() && !constantNumber(*node.step(), step)) return;

    // Zero is refused at run time with a message of its own; it has no
    // direction to report and would read here as a step that moves away from
    // everything.
    if (step == 0) return;
    if (step > 0 ? start <= end : start >= end) return;

    diag_.warning(line_, "Loop never runs: '" + node.variable() + "' starts at " +
                             number(start) + " and step " + number(step) +
                             " moves away from " + number(end));
}

}  // namespace shalimar
