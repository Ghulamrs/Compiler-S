// The runtime's entry point, and the recursion ceiling.
#include "Internal.h"

#ifdef SHM_DEBUG
#include "Debug.h"
#endif

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

    int32_t total() const { return total_; }

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

// Stepping over a call and refusing to recurse for ever are the same question
// asked twice, so they are answered from the same counter.
int32_t callDepth() { return Depth::shared().total(); }

}  // namespace shm

extern "C" {

void shm_begin(void) {
    // Precision is state that outlives a print, so a program starts from the
    // default whatever the last one left set.
    shm::Console::shared().resetPlaces();
#ifdef SHM_DEBUG
    shm::Debug::shared().begin();
#endif
}

int shm_end(void) {
    shm::Console::shared().flush();
#ifdef SHM_DEBUG
    shm::Debug::shared().ending(0);
#endif
    return 0;
}

void shm_enter(int32_t id, int32_t limit, const char *name) {
    shm::Depth::shared().enter(id, limit, name);
}

void shm_leave(int32_t id) { shm::Depth::shared().leave(id); }

}  // extern "C"

int main(void) {
    // The names first: a session is told the numbering before it is asked
    // for a breakpoint in it.
    shm_name_files();
    shm_begin();
    shm_init_globals();
    shm_user_main();
    return shm_end();
}
