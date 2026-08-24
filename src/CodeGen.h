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
#include <set>
#include <vector>

namespace shalimar {

class Emitter;

class CodeGen : public NodeVisitor {
public:
    explicit CodeGen(Emitter &emitter) : emitter_(emitter) {}

    void run(Program &program, const std::string &sourceName,
             const std::vector<std::string> &units);

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
    void visit(Call &node) override;
    void visit(Return &node) override;
    void visit(MultiAssign &node) override;
    void visit(CallStmt &node) override;
    void visit(StrLit &node) override;
    void visit(ArrayLit &node) override;
    void visit(Blank &node) override;
    void visit(Index &node) override;
    void visit(Dim &node) override;
    void visit(Precision &node) override;
    void visit(CompoundAssign &node) override;

    // What a Shalimar function is called once it is a linker symbol.
    // Shalimar's main() is not the program's entry point - the runtime's is -
    // so it is given the name the runtime calls, and every other function
    // takes a prefix that cannot collide with it.
    static std::string mangle(const std::string &name);

private:
    Emitter &emitter_;
    std::vector<std::string> units_;

    // Evaluation slots sit above the named variables in the frame, so the
    // base moves with the function.
    int evaluationBase_ = 0;
    int depth_ = 0;
    int deepest_ = 0;
    int reserve();
    void release();

    void generate(Function &function);
    // A function of nothing but shm_name_file calls, run before the
    // program so that a session knows what the unit numbers mean.
    void generateFileNames(const std::set<int> &units);
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

    // Byte arrays the module needs that no string literal in the program
    // asked for - a function's name, for the recursion message. Numbered
    // above the literals, which the checker numbered from zero.
    int newBytesId() { return nextBytesId_++; }
    int nextBytesId_ = 100000;

    // Where 'break' and 'continue' go. A stack, because loops nest, and both
    // words bind to the innermost - there are no labels in the language, so
    // neither can leave two loops at once.
    struct LoopLabels { int breakTo; int continueTo; };
    std::vector<LoopLabels> loops_;

    void generateIntLoop(For &node);
    void generateRealLoop(For &node);

    // A call, evaluated and made. Returns the type its value has, or null
    // when the function declares no outputs.
    void generateCall(Call &node);

    // Where each of a call's arguments travels: a register of its own file,
    // or a place in the overflow block when the registers have run out.
    //
    // The caller and the callee both work this out from the prototype and
    // from the same target, which is what makes them agree without anything
    // being written down between them.
    struct Place {
        Slot kind;
        bool overflow;
        int index;        // the register in its file, or the place in the block
    };
    std::vector<Place> planArguments(const Prototype &proto) const;

    // A name is read and written through its slot, unless it is a reference
    // parameter, in which case the slot holds the caller's address.
    void readSymbol(const Symbol &symbol);
    void writeSymbol(const Symbol &symbol);

    // Arrays. The element kind is what the runtime is told when it is asked
    // for one; the accessor is which of the four getters or setters an
    // element of this type reaches.
    static int elementKind(const Type *arrayType);
    static const char *getterFor(const Type *elementType);
    static const char *setterFor(const Type *elementType);

    // Builds a literal into a fresh array and leaves the pointer in the
    // accumulator.
    void buildLiteral(ArrayLit &literal);
    // Writes the extents into consecutive slots and asks for the array.
    void makeArray(const Type *type, std::vector<ExprPtr> &extents, int extentBase);

    // 'A[i] : v' - the base and the index are computed, then the value.
    void convertAccumulator(const Type *from, const Type *to);
    void assignElement(Index &target, Expr &value);
    void generateBuiltin(Call &node);

    // Where a function's epilogue is, so that 'return' is a jump.
    int exitLabel_ = 0;
    const Function *current_ = nullptr;

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
