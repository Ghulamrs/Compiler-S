// Failing, and where the program was when it did.
#include "Internal.h"

#include <cstdio>
#include <cstdlib>

namespace shm {

Position &Position::shared() {
    static Position instance;
    return instance;
}

void fail(const char *message) {
    Console::shared().flush();
    if (Position::shared().line() > 0) {
        std::printf("Error: line %d: %s\n", static_cast<int>(Position::shared().line()), message);
    } else {
        std::printf("Error: %s\n", message);
    }
    std::fflush(stdout);
    std::exit(1);
}

}  // namespace shm

extern "C" void shm_line(int32_t line) { shm::Position::shared().set(line); }
