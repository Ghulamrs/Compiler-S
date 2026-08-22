// The code generator.
//
// One walk of the tree, speaking only to an Emitter. Everything that is the
// same on every machine is here - the order items are evaluated in, what a
// print statement costs, which runtime entry point a value of a given type
// goes to. Everything that is not is a method on Emitter.
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
    void visit(Binary &node) override;
    void visit(Print &node) override;

    // What a Shalimar function is called once it is a linker symbol.
    // Shalimar's main() is not the program's entry point - the runtime's is -
    // so it is given the name the runtime calls, and every other function
    // takes a prefix that cannot collide with it.
    static std::string mangle(const std::string &name);

private:
    Emitter &emitter_;

    void generate(Function &function);
    void generate(Stmt &statement);
    void evaluate(Expr &expr);

    // Where an operand waits while its sibling is evaluated. Slots are
    // numbered from zero and nest with the expression; how many a function
    // needs was measured by the checker, which walked the same nesting.
    // That is slower than a register allocator and it is the same shape on
    // every machine, which is what lets one generator drive three.
    int depth_ = 0;
    int reserve();
    void release();
};

}  // namespace shalimar
