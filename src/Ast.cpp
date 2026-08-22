#include "Ast.h"

namespace shalimar {

const Function *Program::find(const std::string &name) const {
    for (const std::unique_ptr<Function> &f : functions_) {
        if (f->proto().name() == name) return f.get();
    }
    return nullptr;
}

}  // namespace shalimar
