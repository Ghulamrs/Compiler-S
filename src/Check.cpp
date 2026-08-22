#include "Check.h"

#include "Builtin.h"

namespace shalimar {
namespace {

const Type *charArray() { return Type::arrayOf(Type::charType()); }

bool isText(const Type *type) { return type == charArray(); }

}  // namespace

bool Checker::check(Program &program) {
    program_ = &program;

    // Functions are collected before any body is checked, so main() may call
    // something written after it.
    int id = 0;
    for (std::unique_ptr<Function> &f : program.functions()) {
        f->proto().id = id++;
        if (f->proto().name == "main" && !f->proto().inputs.empty()) {
            diag_.error(f->proto().line, "main() takes no inputs");
        }
        if (findBuiltin(f->proto().name) >= 0) {
            diag_.error(f->proto().line,
                        "'" + f->proto().name + "' is a built-in function");
        }
        // The one clash contextual matching cannot resolve is a function
        // named 'prec', which is refused outright rather than left silently
        // unreachable.
        if (f->proto().name == "prec") {
            diag_.error(f->proto().line, "'prec' cannot be a function name");
        }
        for (const Param &parameter : f->proto().inputs) {
            refuseConstant(parameter.name, "a parameter name");
        }
    }

    Function *entry = program.find("main");
    if (!entry) diag_.error(0, "No main() function defined");
    else entry->markCalled();

    for (std::unique_ptr<Function> &f : program.functions()) check(*f);

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

// A block returns on every path if some statement in it does. An 'if' counts
// only when it has an else and every branch returns.
bool Checker::alwaysReturns(const Block &body) {
    for (const StmtPtr &s : body) {
        if (dynamic_cast<const Return *>(s.get())) return true;
        If *branch = const_cast<If *>(dynamic_cast<const If *>(s.get()));
        if (!branch || !branch->hasElse()) continue;
        bool all = true;
        for (If::Branch &arm : branch->branches()) {
            if (!alwaysReturns(arm.body)) { all = false; break; }
        }
        if (all && alwaysReturns(branch->elseBody())) return true;
    }
    return false;
}

void Checker::check(Stmt &statement) {
    line_ = statement.line();
    statement.accept(*this);
}

const Type *Checker::typeOf(ExprPtr &expr) {
    expr->accept(*this);
    return expr->type();
}

void Checker::coerce(ExprPtr &expr, const Type *to) {
    if (!to || !expr->type() || expr->type() == to) return;
    if (expr->type()->isArray() || to->isArray()) return;
    ExprPtr inner(expr.release());
    expr.reset(new Convert(std::move(inner), to));
}

// int beside real widens to real. A char with a number keeps the char, which
// is what refuses the mixture: an int will not convert into a char silently,
// so the arithmetic rule below catches it and the comparison rule lets it
// through as a character comparison.
const Type *Checker::common(const Type *a, const Type *b) const {
    if (!a || !b) return nullptr;
    if (a == b) return a;
    if (a == Type::intType() && b == Type::realType()) return Type::realType();
    if (a == Type::realType() && b == Type::intType()) return Type::realType();
    return a;
}

bool Checker::refuseConstant(const std::string &name, const char *what) {
    if (!isConstant(name)) return false;
    diag_.error(line_, "'" + name + "' cannot be " + what);
    return true;
}

// --------------------------------------------------------------- expressions

void Checker::visit(IntLit &node) { node.setType(Type::intType()); }
void Checker::visit(RealLit &node) { node.setType(Type::realType()); }

void Checker::visit(StrLit &node) {
    node.setId(strings_++);
    node.setType(charArray());
}

void Checker::visit(Blank &node) {
    // A blank carries no type. Its place is fixed by the commas around it and
    // its value is the zero of whatever the literal turns out to hold.
    node.setType(nullptr);
}

const Type *Checker::literalType(ArrayLit &node) {
    for (ExprPtr &slot : node.elements()) {
        if (dynamic_cast<Blank *>(slot.get())) continue;
        if (ArrayLit *nested = dynamic_cast<ArrayLit *>(slot.get())) {
            const Type *inner = literalType(*nested);
            return inner ? Type::arrayOf(inner) : nullptr;
        }
        const Type *scalar = typeOf(slot);
        return scalar ? Type::arrayOf(scalar) : nullptr;
    }
    return nullptr;
}

void Checker::coerceLiteral(ArrayLit &node, const Type *arrayType) {
    node.setType(arrayType);
    if (!arrayType || !arrayType->isArray()) return;
    const Type *element = arrayType->element();
    for (ExprPtr &slot : node.elements()) {
        if (dynamic_cast<Blank *>(slot.get())) continue;
        if (ArrayLit *nested = dynamic_cast<ArrayLit *>(slot.get())) {
            coerceLiteral(*nested, element);
            continue;
        }
        typeOf(slot);
        coerce(slot, element);
    }
}

void Checker::visit(ArrayLit &node) {
    coerceLiteral(node, literalType(node));
}

void Checker::visit(Var &node) {
    if (isConstant(node.name())) {
        node.resolveConstant(constantValue(node.name()));
        node.setType(Type::realType());
        return;
    }
    const Symbol *symbol = scope_.lookup(node.name());
    if (!symbol) {
        diag_.error(line_, "Undefined variable '" + node.name() + "'");
        return;
    }
    node.resolve(symbol);
    node.setType(symbol->type());
}

void Checker::visit(Index &node) {
    const Type *base = typeOf(node.baseRef());
    const Type *index = typeOf(node.indexRef());
    if (!base) return;
    if (!base->isArray()) {
        diag_.error(line_, "Cannot index a scalar");
        return;
    }
    if (index && index != Type::intType()) coerce(node.indexRef(), Type::intType());
    node.setType(base->element());
}

void Checker::visit(Dim &node) {
    const Type *base = typeOf(node.base());
    const Type *axis = typeOf(node.axis());
    // Asking a scalar is a type confusion rather than a rank one, so it is an
    // error where an axis the array does not have is simply -1.
    if (base && !base->isArray()) {
        diag_.error(line_, "'." + node.spelling() + "' needs an array");
    }
    if (axis && axis != Type::intType()) {
        diag_.error(line_, "'." + node.spelling() + "' needs an int axis");
    }
    node.setType(Type::intType());
}

void Checker::visit(Precision &node) {
    const Type *places = typeOf(node.places());
    if (places && places != Type::intType()) {
        diag_.error(line_, "prec() needs an int");
    }
    node.setType(Type::intType());
}

void Checker::visit(Convert &node) {
    const Type *from = typeOf(node.operand());
    if (from && from->isArray()) {
        diag_.error(line_, "Cannot convert an array");
    }
}

void Checker::visit(Binary &node) {
    const Type *left = typeOf(node.left());
    const Type *right = typeOf(node.right());
    if (!left || !right) return;

    // Two strings are the one array pairing the operators accept.
    if (isText(left) && isText(right)) {
        switch (node.op()) {
        case Binary::Op::Equal:
        case Binary::Op::NotEqual:
        case Binary::Op::Less:
        case Binary::Op::Greater:
        case Binary::Op::LessEqual:
        case Binary::Op::GreaterEqual:
            node.setType(Type::intType());
            return;
        case Binary::Op::Add:
            node.setType(charArray());
            return;
        default:
            diag_.error(line_, std::string("'") + Binary::spelling(node.op()) +
                                   "' does not apply to strings");
            node.setType(Type::intType());
            return;
        }
    }

    if (left->isArray() || right->isArray()) {
        diag_.error(line_, std::string("'") + Binary::spelling(node.op()) +
                               "' needs scalars, got " + left->spelling() +
                               " and " + right->spelling());
        node.setType(Type::intType());
        return;
    }

    // A char never joins arithmetic. That wall is what keeps '? c' printing a
    // letter rather than a number; comparison and ordering stay legal, and
    // '&' and '|' read truthiness, which every scalar has.
    switch (node.op()) {
    case Binary::Op::Add:
    case Binary::Op::Subtract:
    case Binary::Op::Multiply:
    case Binary::Op::Divide:
    case Binary::Op::Modulus:
    case Binary::Op::Power:
        if (left == Type::charType() || right == Type::charType()) {
            diag_.error(line_, std::string("'") + Binary::spelling(node.op()) +
                                   "' does not apply to char");
            node.setType(Type::intType());
            return;
        }
        break;
    default:
        break;
    }

    const Type *operands = common(left, right);
    coerce(node.left(), operands);
    coerce(node.right(), operands);
    node.setType(Binary::yieldsInt(node.op()) ? Type::intType() : operands);
}

void Checker::visit(Call &node) {
    const int which = findBuiltin(node.callee());
    if (which >= 0) {
        const Builtin &fn = builtin(which);
        node.resolveBuiltin(which);
        if (static_cast<int>(node.arguments().size()) != fn.arity) {
            diag_.error(line_, "'" + node.callee() + "' takes " +
                                   std::to_string(fn.arity) + " arguments");
        }
        std::vector<const Type *> given;
        for (size_t i = 0; i < node.arguments().size(); ++i) {
            given.push_back(typeOf(node.arguments()[i]));
        }

        if (fn.shape == Builtin::Shape::Length) {
            if (!given.empty() && given[0] && !given[0]->isArray()) {
                diag_.error(line_, "len() needs an array");
            }
            node.setType(Type::intType());
            return;
        }
        bool allInt = true;
        for (const Type *type : given) {
            if (type != Type::intType()) allInt = false;
        }
        const bool intAnswer = fn.shape == Builtin::Shape::IntOrReal && allInt;
        const Type *want = intAnswer ? Type::intType() : Type::realType();
        for (ExprPtr &argument : node.arguments()) coerce(argument, want);
        node.setType(want);
        return;
    }

    Function *callee = program_->find(node.callee());
    if (!callee) {
        diag_.error(line_, "Unknown function '" + node.callee() + "'");
        for (ExprPtr &argument : node.arguments()) typeOf(argument);
        return;
    }
    callee->markCalled();
    const Prototype &proto = callee->proto();
    node.resolve(&proto);

    if (node.arguments().size() != proto.inputs.size()) {
        diag_.error(line_, "'" + node.callee() + "' takes " +
                               std::to_string(proto.inputs.size()) + " arguments");
    }

    for (size_t i = 0; i < node.arguments().size(); ++i) {
        const Type *given = typeOf(node.arguments()[i]);
        if (i >= proto.inputs.size() || !given) continue;
        const Param &parameter = proto.inputs[i];

        if (parameter.byReference || parameter.type->isArray()) {
            // A reference must be addressable and must match exactly. A
            // converted copy would silently stop being the caller's.
            if (!node.arguments()[i]->isAddressable() && !parameter.type->isArray()) {
                diag_.error(line_, "'" + parameter.name + "' needs a variable");
            } else if (given != parameter.type) {
                diag_.error(line_, "'" + parameter.name + "' must be " +
                                       parameter.type->spelling());
            }
            continue;
        }
        coerce(node.arguments()[i], parameter.type);
    }

    int scratch = 0;
    if (proto.returnsByPointer()) scratch += static_cast<int>(proto.outputs.size());
    for (const Param &parameter : proto.inputs) {
        if (parameter.byReference) ++scratch;
    }
    if (scratch > 0) {
        const int base = function_->addHiddenSlot();
        for (int i = 1; i < scratch; ++i) function_->addHiddenSlot();
        node.setScratchBase(base);
    }

    node.setType(proto.outputs.empty() ? nullptr : proto.outputs[0]);
}

// ---------------------------------------------------------------- statements

void Checker::visit(Declare &node) {
    if (refuseConstant(node.name(), "declared")) return;
    if (scope_.definedHere(node.name())) {
        diag_.error(line_, "'" + node.name() + "' is already declared");
        return;
    }

    const Type *type = node.declaredType();
    for (size_t i = 0; i < node.extents().size(); ++i) type = Type::arrayOf(type);
    if (!type->isWellFormed()) {
        diag_.error(line_, "'" + node.name() + "': char is one-dimensional");
        type = charArray();
    }
    node.setDeclaredType(type);

    for (ExprPtr &extent : node.extents()) {
        const Type *given = typeOf(extent);
        if (given && given != Type::intType()) {
            diag_.error(line_, "'" + node.name() + "': size must be int");
        }
        // A size the checker can fold is refused before the program runs; one
        // that genuinely depends on a variable is checked at run time, where
        // the message can afford to report the value it computed.
        double folded = 0.0;
        if (constantNumber(*extent, folded) && folded < 1) {
            diag_.error(line_, "'" + node.name() + "': size must be 1 or more");
        }
    }
    if (!node.extents().empty()) {
        const int base = function_->addHiddenSlot();
        for (size_t i = 1; i < node.extents().size(); ++i) function_->addHiddenSlot();
        node.setExtentBase(base);
    }

    if (node.initial()) {
        if (ArrayLit *literal = dynamic_cast<ArrayLit *>(node.initial().get())) {
            coerceLiteral(*literal, type);
        } else {
            const Type *given = typeOf(node.initial());
            if (given && given->isArray() && given != type) {
                diag_.error(line_, "'" + node.name() + "' is " + type->spelling());
            }
            coerce(node.initial(), type);
        }
    }

    Symbol *symbol = function_->declare(node.name(), type);
    scope_.define(node.name(), symbol);
    node.resolve(symbol);
}

void Checker::visit(Assign &node) {
    Index *element = dynamic_cast<Index *>(node.target().get());
    if (element) {
        const Type *target = typeOf(node.target());
        const Type *value = typeOf(node.expr());
        if (target && value && !target->isArray()) coerce(node.expr(), target);
        return;
    }

    Var &target = static_cast<Var &>(*node.target());
    if (refuseConstant(target.name(), "assigned")) return;

    const Symbol *existing = scope_.lookup(target.name());

    // An array literal carries its own shape, which is what lets it create an
    // array where an arbitrary expression cannot.
    ArrayLit *literal = dynamic_cast<ArrayLit *>(node.expr().get());
    const Type *value = nullptr;
    if (literal && existing && existing->type()->isArray()) {
        coerceLiteral(*literal, existing->type());
        value = existing->type();
    } else if (literal) {
        value = literalType(*literal);
        if (!value) {
            diag_.error(line_, "An all-blank literal cannot create '" + target.name() + "'");
            return;
        }
        coerceLiteral(*literal, value);
    } else {
        value = typeOf(node.expr());
    }
    if (!value) return;

    if (!existing) {
        // char[] is the exception, and deliberately so: a string carries its
        // own length wherever it comes from, so there is nothing to infer.
        if (value->isArray() && !literal && !isText(value)) {
            diag_.error(line_, "Declare the array '" + target.name() + "' first");
            return;
        }
        Symbol *created = function_->declare(target.name(), value);
        scope_.define(target.name(), created);
        existing = created;
        node.setCreates(true);
    }
    target.resolve(existing);
    target.setType(existing->type());
    if (!existing->type()->isArray()) coerce(node.expr(), existing->type());
    else if (value != existing->type()) {
        diag_.error(line_, "'" + target.name() + "' is " + existing->type()->spelling());
    }
    node.resolve(existing);
}

void Checker::visit(CompoundAssign &node) {
    const Type *target = typeOf(node.target());
    const Type *value = typeOf(node.expr());
    if (!target || !value) return;

    // '+:' on a string appends, which is how one is built in a loop. '-:' has
    // no meaning there, and neither has either operator on any other array.
    if (isText(target)) {
        if (!node.isAdd()) {
            diag_.error(line_, "'-:' does not apply to strings");
        }
        return;
    }
    if (target->isArray()) {
        diag_.error(line_, std::string("'") + (node.isAdd() ? "+:" : "-:") +
                               "' needs a single value");
        return;
    }
    coerce(node.expr(), target);
}

void Checker::visit(MultiAssign &node) {
    typeOf(node.call());

    Call &call = static_cast<Call &>(*node.call());

    // A built-in has no prototype but still returns exactly one value, and
    // '<s> : sqrt(16.)' still has to define 's'.
    std::vector<const Type *> outputs;
    if (call.builtin() >= 0) outputs.push_back(call.type());
    else if (call.prototype()) outputs = call.prototype()->outputs;
    else return;

    if (node.names().size() != outputs.size()) {
        diag_.error(line_, "'" + call.callee() + "' returns " +
                               std::to_string(outputs.size()) + " values");
        return;
    }
    for (size_t i = 0; i < node.names().size(); ++i) {
        if (refuseConstant(node.names()[i], "assigned")) return;
        const Symbol *target = scope_.lookup(node.names()[i]);
        if (!target) {
            Symbol *created = function_->declare(node.names()[i], outputs[i]);
            scope_.define(node.names()[i], created);
            target = created;
        }
        node.targets().push_back(target);
    }
}

void Checker::visit(CallStmt &node) { typeOf(node.call()); }

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

void Checker::visit(Print &node) {
    for (ExprPtr &item : node.items()) typeOf(item);
}

void Checker::checkCondition(ExprPtr &expr) {
    const Type *type = typeOf(expr);
    if (type && type->isArray()) diag_.error(line_, "A condition must be a scalar");
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

void Checker::visit(For &node) {
    const Type *counterType = common(typeOf(node.start()), typeOf(node.end()));
    if (node.step()) counterType = common(counterType, typeOf(node.step()));
    if (!counterType || counterType->isArray()) {
        if (counterType) diag_.error(line_, "Loop counter cannot be " + counterType->spelling());
        counterType = Type::intType();
    }

    coerce(node.start(), counterType);
    coerce(node.end(), counterType);
    if (node.step()) coerce(node.step(), counterType);

    warnIfLoopNeverRuns(node);
    refuseConstant(node.variable(), "used as a loop counter");

    const int base = function_->addHiddenSlot();
    for (int i = 1; i < For::HiddenSlotCount; ++i) function_->addHiddenSlot();
    node.setHiddenBase(base);

    scope_.push();
    Symbol *counter = function_->declare(node.variable(), counterType);
    scope_.define(node.variable(), counter);
    node.resolve(counter);
    checkBlock(node.body());
    scope_.pop();
}

void Checker::visit(Break &) {}
void Checker::visit(Continue &) {}

// Folds what the two loop-bound rules need and nothing more. Division is left
// out deliberately: '/' means one thing between ints and another between
// reals, and a folder that got that wrong would report a bound the program
// never uses.
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
// runs zero times. Nothing about that is illegal, so this is a warning.
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
    if (step == 0) return;
    if (step > 0 ? start <= end : start >= end) return;

    diag_.warning(line_, "Loop never runs: '" + node.variable() + "' starts at " +
                             number(start) + " and step " + number(step) +
                             " moves away from " + number(end));
}

}  // namespace shalimar
