// The Shalimar runtime.
//
// Written in the same C++14 as the compiler and exported with C linkage. The
// state a print needs - whether anything has been written on the current
// line - lives in one object rather than in loose file statics, because the
// rules that consult it are about to multiply: a multi-row grid starts on its
// own line, and the precision a real prints to is set by a directive in the
// middle of a print list.
#include "shmrt.h"

#include <cstdio>

namespace {

class Console {
public:
    void printInt(int32_t value) {
        std::printf("%d ", static_cast<int>(value));
        lineHasText_ = true;
    }

    void endLine() {
        std::putchar('\n');
        lineHasText_ = false;
    }

    // Whether anything stands on the line so far. A '??' leaves the line
    // open, so this is not the same as "this statement has printed".
    bool lineHasText() const { return lineHasText_; }

    void flush() { std::fflush(stdout); }

private:
    bool lineHasText_ = false;
};

Console &console() {
    static Console instance;
    return instance;
}

}  // namespace

extern "C" {

void shm_begin(void) {}

int shm_end(void) {
    console().flush();
    return 0;
}

void shm_print_int(int32_t value) { console().printInt(value); }

void shm_line_end(void) { console().endLine(); }

}  // extern "C"

int main(void) {
    shm_begin();
    shm_user_main();
    return shm_end();
}
