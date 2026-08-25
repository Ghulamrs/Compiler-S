
#include "Internal.h"

#ifdef SHM_DEBUG
#include "Debug.h"
#endif

#include <cstdio>
#include <string>
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

        std::printf("Error: %s%sline %d: %s\n", where.file(),
                    *where.file() ? " " : "", static_cast<int>(where.line()), message);
    } else {
        std::printf("Error: %s\n", message);
    }
    std::fflush(stdout);
#ifdef SHM_DEBUG

    Debug::shared().ending(1);
#endif
    std::exit(1);
}

}

extern "C" void shm_line(int32_t unit, int32_t line) {
    shm::Position::shared().set(unit, line);
#ifdef SHM_DEBUG
    shm::Debug::shared().at(unit, line);
#endif
}

static int32_t globalsMade = 0;

extern "C" void shm_globals_made(int32_t upto) { globalsMade = upto; }

extern "C" void shm_globals_ready(int32_t slot, const char *name) {
    if (slot < globalsMade) return;
    std::string said = "Undefined variable '";
    said += name ? name : "";
    said += "'";
    shm::fail(said.c_str());
}

extern "C" void shm_name_file(int32_t unit, const char *name) {
    shm::Position::shared().name(unit, name);
}
