
#include "Internal.h"

#ifdef SHM_DEBUG
#include "Debug.h"
#endif

#include <cstdio>

namespace shm {
namespace {

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

    static const int capacity = 1024;

    int32_t perFunction_[capacity] = {0};
    int32_t total_ = 0;
};

}

int32_t callDepth() { return Depth::shared().total(); }

}

extern "C" {

void shm_begin(void) {

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

}

int main(void) {

    shm_name_files();
    shm_begin();
    shm_init_globals();
    shm_user_main();
    return shm_end();
}
