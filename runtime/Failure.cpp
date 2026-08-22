// Failing, and where the program was when it did.
#include "Internal.h"

#ifdef SHM_DEBUG
#include "Debug.h"
#endif

#include <cstdio>
#include <cstdlib>

namespace shm {

Position &Position::shared() {
    static Position instance;
    return instance;
}

void Position::name(int32_t unit, const char *file) {
    if (unit < 0 || unit >= capacity) return;
    names_[unit] = file;
    if (unit + 1 > named_) named_ = unit + 1;
}

const char *Position::nameOf(int32_t unit) const {
    if (unit < 0 || unit >= capacity || !names_[unit]) return "";
    return names_[unit];
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
#ifdef SHM_DEBUG
    // The other end is waiting to be told the program is over, and a failure
    // is one of the two ways that happens.
    Debug::shared().ending(1);
#endif
    std::exit(1);
}

}  // namespace shm

// Where the program is, before every statement. In a release build that is
// all it is, and there is no code here for anything else; in a debug build
// the same position is offered to the session, which is dormant until the
// environment arms it. The compiler emits the same call either way - see
// docs/DEBUGGING.md.
extern "C" void shm_line(int32_t unit, int32_t line) {
    shm::Position::shared().set(unit, line);
#ifdef SHM_DEBUG
    shm::Debug::shared().at(unit, line);
#endif
}

extern "C" void shm_name_file(int32_t unit, const char *name) {
    shm::Position::shared().name(unit, name);
}
