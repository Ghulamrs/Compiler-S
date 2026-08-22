// The runtime's entry point, and the recursion ceiling.
#include "Internal.h"

#include <cstdio>

namespace shm {
namespace {

// One counter per function plus one for the whole program. The per-function
// counter is a depth rather than a lifetime tally, and it comes down on the
// way out however the call ended - otherwise one deep call would poison every
// later one.
class Depth {
public:
    static Depth &shared() {
        static Depth instance;
        return instance;
    }

    void enter(int32_t id, int32_t limit, const char *name) {
        if (total_ >= overall) fail("Call stack too deep (limit 1024)");
        if (id >= 0 && id < capacity) {
            if (perFunction_[id] >= limit) {
                char message[128];
                std::snprintf(message, sizeof message,
                              "Recursion too deep in '%s' (limit %d)", name,
                              static_cast<int>(limit));
                fail(message);
            }
            ++perFunction_[id];
        }
        ++total_;
    }

    void leave(int32_t id) {
        if (id >= 0 && id < capacity && perFunction_[id] > 0) --perFunction_[id];
        if (total_ > 0) --total_;
    }

private:
    static const int overall = 1024;
    // More functions than any program the language is for will hold. A
    // program with more simply goes uncounted per function; the overall
    // ceiling still catches it.
    static const int capacity = 1024;

    int32_t perFunction_[capacity] = {0};
    int32_t total_ = 0;
};

}  // namespace
}  // namespace shm

extern "C" {

void shm_begin(void) {
    // Precision is state that outlives a print, so a program starts from the
    // default whatever the last one left set.
    shm::Console::shared().resetPlaces();
}

int shm_end(void) {
    shm::Console::shared().flush();
    return 0;
}

void shm_enter(int32_t id, int32_t limit, const char *name) {
    shm::Depth::shared().enter(id, limit, name);
}

void shm_leave(int32_t id) { shm::Depth::shared().leave(id); }

}  // extern "C"

int main(void) {
    shm_begin();
    shm_user_main();
    return shm_end();
}
