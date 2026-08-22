// The syntax tree.
//
// This grows one language feature at a time. Every node here is reachable
// from a program that compiles and runs on all three targets; a node is not
// added until the feature it carries is being finished.
//
// Nodes are polymorphic and are walked by double dispatch. A pass is a class
// deriving from NodeVisitor - the checker, the code generator - so adding a
// pass costs no change here, and adding a node makes the compiler name every
// pass that has not yet handled it. That is the point of the pure-virtual
// list below: a forgotten pass is a build error rather than a wrong answer at
// run time.
//
// Only statements carry a line. An error inside a long expression names the
// statement containing it, because that is the useful answer and a
// per-expression line would need a call stack to be worth having.
#pragma once

#include "Type.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace shalimar {

class IntLit;
class RealLit;
class Var;
class Convert;
class Binary;
class Declare;
class Assign;
class Print;

class NodeVisitor {
public:
    virtual ~NodeVisitor() = default;

    // Non-const, because a pass may annotate what it walks: the checker
    // records the type it worked out on the node itself, so the code
    // generator can read it back rather than deciding it a second time. A
    // pass that only reads simply does not write.
    virtual void visit(IntLit &) = 0;
    virtual void visit(RealLit &) = 0;
    virtual void visit(Var &) = 0;
    virtual void visit(Convert &) = 0;
    virtual void visit(Binary &) = 0;
    virtual void visit(Declare &) = 0;
    virtual void visit(Assign &) = 0;
    virtual void visit(Print &) = 0;
};

class Node {
public:
    virtual ~Node() = default;
    virtual void accept(NodeVisitor &v) = 0;

protected:
    Node() = default;
};

// ------------------------------------------------------------------ symbols

// A name, once the checker has resolved it. Storage says where the value
// lives; `slot` is which eight-byte place in the frame holds it.
class Symbol {
public:
    Symbol(std::string name, const Type *type, int slot)
        : name_(std::move(name)), type_(type), slot_(slot) {}

    const std::string &name() const { return name_; }
    const Type *type() const { return type_; }
    int slot() const { return slot_; }

private:
    std::string name_;
    const Type *type_;
    int slot_;
};

// ---------------------------------------------------------------- expressions

class Expr : public Node {
public:
    // Filled in by the checker; null until it has run.
    const Type *type() const { return type_; }
    void setType(const Type *t) { type_ = t; }

    // A reference argument must be addressable - a variable or an element,
    // not a computed value - so 'bump(1+2)' can be refused.
    virtual bool isAddressable() const { return false; }

    // Asked by the parser, which folds a negated literal into the literal
    // rather than emitting a subtraction from zero.
    virtual bool isIntLiteral() const { return false; }
    virtual bool isRealLiteral() const { return false; }

private:
    const Type *type_ = nullptr;
};

using ExprPtr = std::unique_ptr<Expr>;

class IntLit : public Expr {
public:
    explicit IntLit(int32_t value) : value_(value) {}

    int32_t value() const { return value_; }
    bool isIntLiteral() const override { return true; }
    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    int32_t value_;
};

// A numeral containing a point or an exponent is a real; anything else is an
// int. That is a lexical decision, not a typing one, which is why the two
// literals are different nodes.
class RealLit : public Expr {
public:
    explicit RealLit(double value) : value_(value) {}

    double value() const { return value_; }
    bool isRealLiteral() const override { return true; }
    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    double value_;
};

class Var : public Expr {
public:
    explicit Var(std::string name) : name_(std::move(name)) {}

    const std::string &name() const { return name_; }
    const Symbol *symbol() const { return symbol_; }
    void resolve(const Symbol *s) { symbol_ = s; }

    bool isAddressable() const override { return true; }
    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    std::string name_;
    const Symbol *symbol_ = nullptr;
};

// Inserted by the checker wherever the language converts, which it does in
// both directions and mostly in silence. Nothing else in the tree is
// implicit by the time the code generator sees it: every value has one type,
// and where it changed there is a node saying so.
class Convert : public Expr {
public:
    Convert(ExprPtr expr, const Type *to) : expr_(std::move(expr)) { setType(to); }

    Expr &expr() const { return *expr_; }
    ExprPtr &operand() { return expr_; }
    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    ExprPtr expr_;
};

// Every operator with two operands, including the comparisons and the two
// logical connectives. Neither '&' nor '|' short-circuits: both sides are
// evaluated before either is asked, which is what makes them ordinary binary
// operators rather than control flow.
//
// Unary minus is not here. It folds into its operand as it is parsed and
// arrives as a literal or as a subtraction from zero, which is why '-2^2' is
// '(-2)^2' and not '-(2^2)': the negation is a finished term before the '^'
// is seen.
class Binary : public Expr {
public:
    enum class Op {
        Add, Subtract, Multiply, Divide, Modulus, Power,
        Equal, NotEqual, Less, Greater, LessEqual, GreaterEqual,
        And, Or
    };

    Binary(Op op, ExprPtr lhs, ExprPtr rhs)
        : op_(op), lhs_(std::move(lhs)), rhs_(std::move(rhs)) {}

    Op op() const { return op_; }
    Expr &lhs() const { return *lhs_; }
    Expr &rhs() const { return *rhs_; }
    ExprPtr &left() { return lhs_; }
    ExprPtr &right() { return rhs_; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

    static const char *spelling(Op op);
    // Whether the result is a truth value rather than a number of the
    // operands' own type.
    static bool yieldsInt(Op op);
    // The runtime entry point that performs it on two operands of a type.
    static const char *runtimeFor(Op op, const Type *operands);

private:
    Op op_;
    ExprPtr lhs_;
    ExprPtr rhs_;
};

// ----------------------------------------------------------------- statements

class Stmt : public Node {
public:
    int line() const { return line_; }

protected:
    explicit Stmt(int line) : line_(line) {}

private:
    int line_;
};

using StmtPtr = std::unique_ptr<Stmt>;
using Block = std::vector<StmtPtr>;

// 'int n : 5'. A declaration may appear only at the top level of a function
// body, or at global scope; the rule keeps every local's lifetime the whole
// call, which is what lets the checker type a function in one pass.
class Declare : public Stmt {
public:
    Declare(const Type *type, std::string name, ExprPtr initial, int line)
        : Stmt(line), type_(type), name_(std::move(name)), initial_(std::move(initial)) {}

    const Type *declaredType() const { return type_; }
    const std::string &name() const { return name_; }
    ExprPtr &initial() { return initial_; }        // may be null
    const Symbol *symbol() const { return symbol_; }
    void resolve(const Symbol *s) { symbol_ = s; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    const Type *type_;
    std::string name_;
    ExprPtr initial_;
    const Symbol *symbol_ = nullptr;
};

// 'x : expr'. '=' in the same position is a fallback spelling accepted
// silently, and '+:' / '-:' arrive here too, already expanded: 'x +: e' is
// 'x : x + e' and there is nothing left for the code generator to know about
// the difference.
class Assign : public Stmt {
public:
    Assign(ExprPtr target, ExprPtr expr, int line)
        : Stmt(line), target_(std::move(target)), expr_(std::move(expr)) {}

    ExprPtr &target() { return target_; }
    ExprPtr &expr() { return expr_; }
    const Symbol *symbol() const { return symbol_; }
    void resolve(const Symbol *s) { symbol_ = s; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    ExprPtr target_;
    ExprPtr expr_;
    const Symbol *symbol_ = nullptr;
};

// '?' prints its items and appends a newline; '??' prints them and does not.
// Each item is followed by a single space either way, so '?' always leaves a
// trailing space before its newline.
class Print : public Stmt {
public:
    Print(bool newline, int line) : Stmt(line), newline_(newline) {}

    void add(ExprPtr item) { items_.push_back(std::move(item)); }

    std::vector<ExprPtr> &items() { return items_; }
    const std::vector<ExprPtr> &items() const { return items_; }
    bool newline() const { return newline_; }
    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    std::vector<ExprPtr> items_;
    bool newline_;
};

// ------------------------------------------------------------------- program

// What a function's frame has to hold. Filled in by the checker, which is
// already walking everything and is the only pass that knows both how many
// names a function declares and how deeply its expressions nest.
//
// One slot is eight bytes on every target, whatever it holds. A tagged value
// would need more and a packed one less, but a uniform slot is what lets the
// three emitters agree on an offset without a table. A name gets a slot of
// its own and never gives it back: a frame here is small, and reusing slots
// would buy nothing but a way to be wrong.
class Frame {
public:
    static const int slotBytes = 8;

    int addVariable() { return variables_++; }

    // Where an operand waits while its sibling is evaluated. These sit above
    // the variables, so the depth is counted separately and added at the end.
    void needEvaluationDepth(int depth) {
        if (depth > evaluationDepth_) evaluationDepth_ = depth;
    }

    int variables() const { return variables_; }
    int evaluationBase() const { return variables_; }
    int slots() const { return variables_ + evaluationDepth_; }

private:
    int variables_ = 0;
    int evaluationDepth_ = 0;
};

// 'fun <outputs> = name(inputs)'. The output list holds types, not names.
class Prototype {
public:
    Prototype(std::string name, int line) : name_(std::move(name)), line_(line) {}

    const std::string &name() const { return name_; }
    int line() const { return line_; }

private:
    std::string name_;
    int line_;
};

class Function {
public:
    Function(Prototype proto, Block body)
        : proto_(std::move(proto)), body_(std::move(body)) {}

    const Prototype &proto() const { return proto_; }
    Block &body() { return body_; }
    const Block &body() const { return body_; }
    Frame &frame() { return frame_; }
    const Frame &frame() const { return frame_; }

    // The function owns every name declared inside it, however deeply. A
    // symbol outlives the scope that introduced it because the frame slot
    // does: lifetimes here are the whole call.
    Symbol *declare(const std::string &name, const Type *type) {
        symbols_.push_back(std::unique_ptr<Symbol>(
            new Symbol(name, type, frame_.addVariable())));
        return symbols_.back().get();
    }

private:
    Prototype proto_;
    Block body_;
    Frame frame_;
    std::vector<std::unique_ptr<Symbol>> symbols_;
};

// Execution begins at main(), which takes no inputs. Definitions may be
// written in any order.
class Program {
public:
    void add(std::unique_ptr<Function> f) { functions_.push_back(std::move(f)); }

    std::vector<std::unique_ptr<Function>> &functions() { return functions_; }
    const std::vector<std::unique_ptr<Function>> &functions() const { return functions_; }
    const Function *find(const std::string &name) const;

private:
    std::vector<std::unique_ptr<Function>> functions_;
};

}  // namespace shalimar
