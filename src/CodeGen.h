
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

    static std::string mangle(const std::string &name);

private:
    Emitter &emitter_;
    std::vector<std::string> units_;

    int evaluationBase_ = 0;
    int depth_ = 0;
    int deepest_ = 0;
    int reserve();
    void release();

    void generate(Function &function);

    void generateFileNames(const std::set<int> &units);
    void generate(Stmt &statement);
    void generate(Block &body);
    void evaluate(Expr &expr);

    void evaluateCondition(Expr &expr);

    int newLabel() { return nextLabel_++; }
    int nextLabel_ = 0;

    int newBytesId() { return nextBytesId_++; }
    int nextBytesId_ = 100000;

    struct LoopLabels { int breakTo; int continueTo; };
    std::vector<LoopLabels> loops_;

    void generateIntLoop(For &node);
    void generateRealLoop(For &node);

    void generateCall(Call &node);

    struct Place {
        Slot kind;
        bool overflow;
        int index;
    };
    std::vector<Place> planArguments(const Prototype &proto) const;

    void readSymbol(const Symbol &symbol);
    void writeSymbol(const Symbol &symbol);

    static int elementKind(const Type *arrayType);
    static const char *getterFor(const Type *elementType);
    static const char *setterFor(const Type *elementType);

    void buildLiteral(ArrayLit &literal);

    void makeArray(const Type *type, std::vector<ExprPtr> &extents, int extentBase);

    void markGlobalMade(const Symbol &symbol);
    void reachableFromGlobals(Program &program);
    std::set<std::string> initializerCanReach_;
    bool checkGlobalReads_ = false;

    void convertAccumulator(const Type *from, const Type *to);
    void assignElement(Index &target, Expr &value);
    void generateBuiltin(Call &node);

    int exitLabel_ = 0;
    const Function *current_ = nullptr;

    static bool isReal(const Type *type);
    static Slot slotKind(const Type *type);
    void store(const Type *type, int slot);
    void load(const Type *type, int slot);
    void passAccumulator(const Type *type, int index);
    void passSlot(const Type *type, int slot, int index);
};

}
