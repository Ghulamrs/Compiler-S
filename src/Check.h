// The checker: types the whole program before anything runs.
//
// It behaves unlike the lexer and the parser and that is its point. **It does
// not stop at the first problem.** It types everything it can, records every
// diagnostic it finds, and only then says whether the program may run. That
// is why several messages can appear at once, and why warnings appear for
// programs that run anyway.
//
// It also rewrites. A conversion the language performs silently is inserted
// here as a node, so that by the time the code generator sees the tree every
// value has one type and nothing is implicit. And it measures: how many
// names a function declares and how deeply its expressions nest are both
// answered by this walk, because it is the only pass that sees both.
#pragma once

#include "Ast.h"
#include "Diag.h"

#include <map>
#include <string>
#include <vector>

namespace shalimar {

// The names visible at one point in a function. Blocks nest, so a variable
// first assigned inside an 'if' is gone after it - which is the app's rule
// and not an obvious one, since a declaration may only appear at the top of a
// function body and so always outlives every block.
class Scope {
public:
    void push() { levels_.push_back(Level()); }
    void pop() { levels_.pop_back(); }
    void clear() { levels_.clear(); }

    void define(const std::string &name, const Symbol *symbol) {
        levels_.back()[name] = symbol;
    }

    // Innermost first; null when the name is not in scope at all.
    const Symbol *lookup(const std::string &name) const {
        for (size_t i = levels_.size(); i-- > 0;) {
            Level::const_iterator found = levels_[i].find(name);
            if (found != levels_[i].end()) return found->second;
        }
        return nullptr;
    }

    // Only the innermost level, which is what a redeclaration has to ask.
    bool definedHere(const std::string &name) const {
        return !levels_.empty() && levels_.back().count(name) != 0;
    }

private:
    using Level = std::map<std::string, const Symbol *>;
    std::vector<Level> levels_;
};

class Checker : public NodeVisitor {
public:
    explicit Checker(Diagnostics &diagnostics) : diag_(diagnostics) {}

    // False when the program must not run. Diagnostics hold the reasons, and
    // there may be several.
    bool check(Program &program);

    void visit(IntLit &node) override;
    void visit(RealLit &node) override;
    void visit(Var &node) override;
    void visit(Convert &node) override;
    void visit(Binary &node) override;
    void visit(Declare &node) override;
    void visit(Assign &node) override;
    void visit(Print &node) override;

private:
    Diagnostics &diag_;
    int line_ = 0;               // the statement being checked
    Function *function_ = nullptr;
    Scope scope_;
    int depth_ = 0;              // evaluation slots in use right now

    void check(Function &function);
    void check(Stmt &statement);

    // Types the expression and returns its type; null when it could not be
    // typed, which is how a diagnostic already reported is not reported again.
    const Type *typeOf(ExprPtr &expr);

    // Wraps the expression in a Convert if it is not already the wanted type.
    // Every implicit conversion in the language comes through here, in both
    // directions, so there is one place that decides what one means.
    void coerce(ExprPtr &expr, const Type *to);

    // Where an int and a real meet, the int widens. Null when they cannot.
    const Type *common(const Type *a, const Type *b) const;

    // Tracks how deep the evaluation stack has to be for this function: the
    // left operand waits in a slot while the right is evaluated.
    void deeper();
    void shallower();
};

}  // namespace shalimar
