#include "Ast.h"

namespace shalimar {

const char *Binary::spelling(Op op) {
    switch (op) {
    case Op::Add:          return "+";
    case Op::Subtract:     return "-";
    case Op::Multiply:     return "*";
    case Op::Divide:       return "/";
    case Op::Modulus:      return "%";
    case Op::Power:        return "^";
    case Op::Equal:        return "=";
    case Op::NotEqual:     return "!=";
    case Op::Less:         return "<";
    case Op::Greater:      return ">";
    case Op::LessEqual:    return "<=";
    case Op::GreaterEqual: return ">=";
    case Op::And:          return "&";
    case Op::Or:           return "|";
    }
    return "?";
}

const char *Binary::intRuntime(Op op) {
    switch (op) {
    case Op::Add:          return "shm_int_add";
    case Op::Subtract:     return "shm_int_sub";
    case Op::Multiply:     return "shm_int_mul";
    case Op::Divide:       return "shm_int_div";
    case Op::Modulus:      return "shm_int_mod";
    case Op::Power:        return "shm_int_pow";
    case Op::Equal:        return "shm_int_eq";
    case Op::NotEqual:     return "shm_int_ne";
    case Op::Less:         return "shm_int_lt";
    case Op::Greater:      return "shm_int_gt";
    case Op::LessEqual:    return "shm_int_le";
    case Op::GreaterEqual: return "shm_int_ge";
    case Op::And:          return "shm_int_and";
    case Op::Or:           return "shm_int_or";
    }
    return "shm_int_add";
}

const Function *Program::find(const std::string &name) const {
    for (const std::unique_ptr<Function> &f : functions_) {
        if (f->proto().name() == name) return f.get();
    }
    return nullptr;
}

}  // namespace shalimar
