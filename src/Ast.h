// The syntax tree.
//
// This grows one language feature at a time. Every node that is here is
// reachable from a program that compiles and runs on all three targets; a
// node is not added until the feature it carries is being finished.
//
// Nodes are polymorphic and are walked by double dispatch. A pass is a class
// deriving from NodeVisitor - the checker, the dumper, the code generator -
// so adding a pass costs no change here, and adding a node makes the compiler
// name every pass that has not yet handled it. That is the point of the
// pure-virtual list below: a forgotten pass is a build error rather than a
// wrong answer at run time.
//
// Only statements carry a line. An error inside a long expression names the
// statement containing it, because that is the useful answer and a
// per-expression line would need a call stack to be worth having.
#pragma once

#include "Type.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace shalimar {

class IntLit;
class Binary;
class Print;

class NodeVisitor {
public:
    virtual ~NodeVisitor() = default;

    // Non-const, because a pass may annotate what it walks: the checker
    // records the type it worked out on the node itself, so the code
    // generator can read it back rather than deciding it a second time. A
    // pass that only reads simply does not write.
    virtual void visit(IntLit &) = 0;
    virtual void visit(Binary &) = 0;
    virtual void visit(Print &) = 0;
};

class Node {
public:
    virtual ~Node() = default;
    virtual void accept(NodeVisitor &v) = 0;

protected:
    Node() = default;
};

// ---------------------------------------------------------------- expressions

class Expr : public Node {
public:
    // Filled in by the checker; null until it has run.
    const Type *type() const { return type_; }
    void setType(const Type *t) { type_ = t; }

    // A reference argument must be addressable - a variable or an element,
    // not a computed value - so 'bump(1+2)' can be refused. Nodes that are
    // say so by overriding this.
    virtual bool isAddressable() const { return false; }

    // Asked by the parser, which folds a negated literal into the literal
    // rather than emitting a subtraction from zero.
    virtual bool isIntLiteral() const { return false; }

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

// Every operator with two operands, including the comparisons and the two
// logical connectives. Neither '&' nor '|' short-circuits: both sides are
// evaluated before either is asked, which is what makes them ordinary binary
// operators rather than control flow.
//
// Unary minus is not here. It folds into its operand as it is parsed and
// arrives as its own node, which is why '-2^2' is '(-2)^2' and not '-(2^2)':
// the negation is a finished term before the '^' is seen.
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
    // Const on the node, not on what it owns: a pass that annotates the tree
    // reaches the operands through here.
    Expr &lhs() const { return *lhs_; }
    Expr &rhs() const { return *rhs_; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

    // What the programmer wrote, for a diagnostic to quote back.
    static const char *spelling(Op op);
    // The runtime entry point that performs it on two ints.
    static const char *intRuntime(Op op);

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

// What a function's frame has to hold. Filled in by the checker, which is
// already walking everything and is the only pass that knows both how many
// names a function declares and how deeply its expressions nest.
//
// One slot is eight bytes on every target, whatever it holds. A tagged value
// would need more and a packed one less, but a uniform slot is what lets the
// three emitters agree on an offset without a table.
class Frame {
public:
    static const int slotBytes = 8;

    // Where an operand waits while its sibling is evaluated. Slots nest with
    // the expression, so the deepest expression in the function fixes how
    // many are needed.
    void needEvaluationDepth(int depth) {
        if (depth > evaluationDepth_) evaluationDepth_ = depth;
    }

    int evaluationDepth() const { return evaluationDepth_; }
    int slots() const { return evaluationDepth_; }
    int bytes() const { return slots() * slotBytes; }

private:
    int evaluationDepth_ = 0;
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

private:
    Prototype proto_;
    Block body_;
    Frame frame_;
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
