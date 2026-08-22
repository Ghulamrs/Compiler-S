#include "Check.h"

namespace shalimar {

bool Checker::check(Program &program) {
    program_ = &program;

    // Functions are collected before any body is checked, so main() may call
    // something written after it. A global cannot be used that way and is a
    // different rule; there are none yet.
    int id = 0;
    for (std::unique_ptr<Function> &f : program.functions()) {
        f->proto().id = id++;
        if (f->proto().name == "main" && !f->proto().inputs.empty()) {
            diag_.error(f->proto().line, "main() takes no inputs");
        }
    }

    // Execution begins at main(), which takes no inputs.
    Function *entry = program.find("main");
    if (!entry) diag_.error(0, "No main() function defined");
    else entry->markCalled();

    for (std::unique_ptr<Function> &f : program.functions()) check(*f);

    // A function defined and never called is a warning, not an error.
    for (std::unique_ptr<Function> &f : program.functions()) {
        if (!f->isCalled()) {
            diag_.warning(f->proto().line,
                          "Function '" + f->proto().name + "' is defined but never called");
        }
    }
    return !diag_.hasErrors();
}

void Checker::check(Function &function) {
    function_ = &function;
    scope_.clear();
    scope_.push();

    // Parameters come first and in order, so the backend can spill argument n
    // into the slot of parameter n without a second table.
    for (const Param &parameter : function.proto().inputs) {
        Symbol *symbol = function.declare(parameter.name, parameter.type);
        if (parameter.byReference) symbol->makeReference();
        scope_.define(parameter.name, symbol);
    }
    if (function.proto().returnsByPointer()) {
        const int base = function.addHiddenSlot();
        for (size_t i = 1; i < function.proto().outputs.size(); ++i) function.addHiddenSlot();
        function.setOutPointerBase(base);
    } else if (function.proto().outputs.size() == 1) {
        function.setResultSlot(function.addHiddenSlot());
    }

    for (StmtPtr &s : function.body()) check(*s);

    if (!function.proto().outputs.empty() && !alwaysReturns(function.body())) {
        diag_.error(function.proto().line,
                    "'" + function.proto().name + "' must return on every path");
    }

    scope_.pop();
    function_ = nullptr;
}

// A block returns on every path if its last reachable statement does. An 'if'
// counts only when it has an else and every branch returns.
bool Checker::alwaysReturns(const Block &body) {
    for (const StmtPtr &s : body) {
        if (dynamic_cast<const Return *>(s.get())) return true;
        const If *branch = dynamic_cast<const If *>(s.get());
        if (!branch || !const_cast<If *>(branch)->hasElse()) continue;
        bool all = true;
        for (If::Branch &arm : const_cast<If *>(branch)->branches()) {
            if (!alwaysReturns(arm.body)) { all = false; break; }
        }
        if (all && alwaysReturns(const_cast<If *>(branch)->elseBody())) return true;
    }
    return false;
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

void Checker::visit(StrLit &node) {
    node.setId(strings_++);
    node.setType(Type::arrayOf(Type::charType()));
}

void Checker::visit(Call &node) {
    Function *callee = program_->find(node.callee());
    if (!callee) {
        diag_.error(line_, "Unknown function '" + node.callee() + "'");
        for (ExprPtr &argument : node.arguments()) typeOf(argument);
        return;
    }
    callee->markCalled();
    const Prototype &proto = callee->proto();
    node.resolve(&proto);

    // Argument count is checked exactly. Too few and too many are both
    // errors; in 2.x a surplus was evaluated and dropped with a warning, so a
    // call with its arguments in the wrong order still ran.
    if (node.arguments().size() != proto.inputs.size()) {
        diag_.error(line_, "'" + node.callee() + "' takes " +
                               std::to_string(proto.inputs.size()) + " arguments");
    }

    for (size_t i = 0; i < node.arguments().size(); ++i) {
        // Each argument waits in a slot while the next is evaluated, so the
        // frame has to be deep enough for all of them at once.
        deeper();
        const Type *given = typeOf(node.arguments()[i]);
        if (i >= proto.inputs.size() || !given) continue;
        const Param &parameter = proto.inputs[i];

        if (parameter.byReference) {
            // A reference must be addressable and must match exactly. A
            // converted copy would silently stop being the caller's.
            if (!node.arguments()[i]->isAddressable()) {
                diag_.error(line_, "'" + parameter.name + "' needs a variable");
            } else if (given != parameter.type) {
                diag_.error(line_, "'" + parameter.name + "' must be " +
                                       parameter.type->spelling());
            }
            continue;
        }
        coerce(node.arguments()[i], parameter.type);
    }
    for (size_t i = 0; i < node.arguments().size(); ++i) shallower();

    // Scratch slots: one per extra output, plus one per reference argument
    // for the copy the callee works on.
    int scratch = 0;
    if (proto.returnsByPointer()) scratch += static_cast<int>(proto.outputs.size());
    for (const Param &parameter : proto.inputs) if (parameter.byReference) ++scratch;
    if (scratch > 0) {
        const int base = function_->addHiddenSlot();
        for (int i = 1; i < scratch; ++i) function_->addHiddenSlot();
        node.setScratchBase(base);
    }

    // In an expression a call is worth its first output. A call with none has
    // no value and is only legal as a statement, which is checked there.
    node.setType(proto.outputs.empty() ? nullptr : proto.outputs[0]);
}

void Checker::visit(CallStmt &node) {
    typeOf(node.call());
}

void Checker::visit(Return &node) {
    const size_t declared = function_ ? function_->proto().outputs.size() : 0;
    if (node.exprs().size() != declared) {
        diag_.error(line_, "'" + function_->proto().name + "' returns " +
                               std::to_string(declared) + " values");
    }
    for (size_t i = 0; i < node.exprs().size(); ++i) {
        typeOf(node.exprs()[i]);
        if (i < declared) coerce(node.exprs()[i], function_->proto().outputs[i]);
    }
}

// '<a,b> : f(...)'. The count must match the output list exactly; targets
// that do not exist are created with the declared output types.
void Checker::visit(MultiAssign &node) {
    typeOf(node.call());

    Call &call = static_cast<Call &>(*node.call());
    if (!call.prototype()) return;
    const Prototype &proto = *call.prototype();

    if (node.names().size() != proto.outputs.size()) {
        diag_.error(line_, "'" + call.callee() + "' returns " +
                               std::to_string(proto.outputs.size()) + " values");
        return;
    }
    for (size_t i = 0; i < node.names().size(); ++i) {
        const Symbol *target = scope_.lookup(node.names()[i]);
        if (!target) {
            Symbol *created = function_->declare(node.names()[i], proto.outputs[i]);
            scope_.define(node.names()[i], created);
            target = created;
        }
        node.targets().push_back(target);
    }
}

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
