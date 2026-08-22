// The code generator.
//
// One walk of the tree, speaking only to an Emitter. Everything that is the
// same on every machine is here - the order operands are evaluated in, which
// runtime entry point a type reaches, where a value waits while its sibling
// is computed. Everything that is not is a virtual method on Emitter.
//
// A pass rather than a family of passes: there is one of these and three
// Emitters, which is the opposite of giving each target its own walk and is
// what stops the three drifting apart.
#pragma once

#include "Ast.h"

#include <string>

namespace shalimar {

class Emitter;

class CodeGen : public NodeVisitor {
public:
    explicit CodeGen(Emitter &emitter) : emitter_(emitter) {}

    void run(Program &program, const std::string &sourceName);

    void visit(IntLit &node) override;
    void visit(RealLit &node) override;
    void visit(Var &node) override;
    void visit(Convert &node) override;
    void visit(Binary &node) override;
    void visit(Declare &node) override;
    void visit(Assign &node) override;
    void visit(Print &node) override;

    // What a Shalimar function is called once it is a linker symbol.
    // Shalimar's main() is not the program's entry point - the runtime's is -
    // so it is given the name the runtime calls, and every other function
    // takes a prefix that cannot collide with it.
    static std::string mangle(const std::string &name);

private:
    Emitter &emitter_;

    // Evaluation slots sit above the named variables in the frame, so the
    // base moves with the function.
    int evaluationBase_ = 0;
    int depth_ = 0;
    int reserve();
    void release();

    void generate(Function &function);
    void generate(Stmt &statement);
    void evaluate(Expr &expr);

    // The accumulator a value of this type occupies, and the slot traffic
    // that goes with it. Asking the type rather than branching at each use is
    // what keeps the int and real paths from drifting apart.
    static bool isReal(const Type *type);
    void store(const Type *type, int slot);
    void load(const Type *type, int slot);
    void passAccumulator(const Type *type, int index);
    void passSlot(const Type *type, int slot, int index);
};

}  // namespace shalimar
