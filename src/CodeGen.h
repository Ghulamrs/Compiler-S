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
#include "backend/Emitter.h"

#include <string>
#include <vector>

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
    void visit(If &node) override;
    void visit(While &node) override;
    void visit(For &node) override;
    void visit(Break &node) override;
    void visit(Continue &node) override;

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
    void generate(Block &body);
    void evaluate(Expr &expr);

    // A condition, reduced to an int the branch can test. Zero is false and
    // anything else is true, which for a real is not the same as converting
    // it: 0.5 is true and int(0.5) is not.
    void evaluateCondition(Expr &expr);

    // Labels are numbers; the emitter decides how to spell one.
    int newLabel() { return nextLabel_++; }
    int nextLabel_ = 0;

    // Where 'break' and 'continue' go. A stack, because loops nest, and both
    // words bind to the innermost - there are no labels in the language, so
    // neither can leave two loops at once.
    struct LoopLabels { int breakTo; int continueTo; };
    std::vector<LoopLabels> loops_;

    void generateIntLoop(For &node);
    void generateRealLoop(For &node);

    // The accumulator a value of this type occupies, and the slot traffic
    // that goes with it. Asking the type rather than branching at each use is
    // what keeps the int and real paths from drifting apart.
    static bool isReal(const Type *type);
    static Slot slotKind(const Type *type);
    void store(const Type *type, int slot);
    void load(const Type *type, int slot);
    void passAccumulator(const Type *type, int index);
    void passSlot(const Type *type, int slot, int index);
};

}  // namespace shalimar
