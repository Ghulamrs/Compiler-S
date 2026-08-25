
#pragma once

#include "Ast.h"
#include "Diag.h"

#include <memory>
#include <string>
#include <vector>

namespace shalimar {

struct Unit {
    std::string name;
    std::unique_ptr<Program> program;
};

class Resolver {
public:
    Resolver(Diagnostics &diagnostics) : diag_(diagnostics) {}

    bool resolve(Unit &main, std::vector<Unit> &others);

    const std::vector<std::string> &used() const { return used_; }

private:
    Diagnostics &diag_;
    std::vector<std::string> used_;
};

std::vector<std::string> calledNames(Program &program);

std::vector<std::string> mentionedNames(Function &function);

std::vector<std::string> calledNamesIn(Stmt &statement);
std::vector<std::string> calledNamesIn(Function &function);

}
