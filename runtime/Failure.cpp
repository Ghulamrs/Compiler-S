// Failing, and where the program was when it did.
#include "Internal.h"

#include <cstdio>
#include <cstdlib>

namespace shm {

Position &Position::shared() {
    static Position instance;
    return instance;
}

void Position::name(int32_t unit, const char *file) {
    if (unit > 0 && unit < capacity) names_[unit] = file;
}

const char *Position::file() const {
    if (unit_ > 0 && unit_ < capacity && names_[unit_]) return names_[unit_];
    return "";
}

void fail(const char *message) {
    Console::shared().flush();
    const Position &where = Position::shared();
    if (where.line() > 0) {
        // The file is named only when it is not the program's own, so that a
        // single-file program prints exactly what the app prints.
        std::printf("Error: %s%sline %d: %s\n", where.file(),
                    *where.file() ? " " : "", static_cast<int>(where.line()), message);
    } else {
        std::printf("Error: %s\n", message);
    }
    std::fflush(stdout);
    std::exit(1);
}

}  // namespace shm

extern "C" void shm_line(int32_t unit, int32_t line) {
    shm::Position::shared().set(unit, line);
}

extern "C" void shm_name_file(int32_t unit, const char *name) {
    shm::Position::shared().name(unit, name);
}
