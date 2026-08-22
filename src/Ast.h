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
class Print;

class NodeVisitor {
public:
    virtual ~NodeVisitor() = default;

    virtual void visit(const IntLit &) = 0;
    virtual void visit(const Print &) = 0;
};

class Node {
public:
    virtual ~Node() = default;
    virtual void accept(NodeVisitor &v) const = 0;

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

private:
    const Type *type_ = nullptr;
};

using ExprPtr = std::unique_ptr<Expr>;

class IntLit : public Expr {
public:
    explicit IntLit(int32_t value) : value_(value) {}

    int32_t value() const { return value_; }
    void accept(NodeVisitor &v) const override { v.visit(*this); }

private:
    int32_t value_;
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

    const std::vector<ExprPtr> &items() const { return items_; }
    bool newline() const { return newline_; }
    void accept(NodeVisitor &v) const override { v.visit(*this); }

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

class Function {
public:
    Function(Prototype proto, Block body)
        : proto_(std::move(proto)), body_(std::move(body)) {}

    const Prototype &proto() const { return proto_; }
    const Block &body() const { return body_; }

private:
    Prototype proto_;
    Block body_;
};

// Execution begins at main(), which takes no inputs. Definitions may be
// written in any order.
class Program {
public:
    void add(std::unique_ptr<Function> f) { functions_.push_back(std::move(f)); }

    const std::vector<std::unique_ptr<Function>> &functions() const { return functions_; }
    const Function *find(const std::string &name) const;

private:
    std::vector<std::unique_ptr<Function>> functions_;
};

}  // namespace shalimar
