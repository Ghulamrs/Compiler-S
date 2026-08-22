// The checker: types the whole program before anything runs.
//
// It behaves unlike the lexer and the parser and that is its point. **It does
// not stop at the first problem.** It types everything it can, records every
// diagnostic it finds, and only then says whether the program may run. That
// is why several messages can appear at once, and why warnings appear for
// programs that run anyway.
//
// It also rewrites: a conversion the language performs silently is inserted
// here as a node, so that by the time the code generator sees the tree, every
// value has one type and nothing is implicit.
#pragma once

#include "Ast.h"
#include "Diag.h"

namespace shalimar {

class Checker : public NodeVisitor {
public:
    explicit Checker(Diagnostics &diagnostics) : diag_(diagnostics) {}

    // False when the program must not run. Diagnostics hold the reasons, and
    // there may be several.
    bool check(Program &program);

    void visit(IntLit &node) override;
    void visit(Binary &node) override;
    void visit(Print &node) override;

private:
    Diagnostics &diag_;
    int line_ = 0;               // the statement being checked
    Frame *frame_ = nullptr;     // the frame being measured
    int depth_ = 0;              // evaluation slots in use right now

    void check(Function &function);
    void check(Stmt &statement);
    const Type *typeOf(Expr &expr);
};

}  // namespace shalimar
