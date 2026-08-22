// Stopping a Shalimar program, from inside it.
//
// There is no debug information and there is not going to be any. What there
// is instead is shm_line(unit, line), which the compiler emits before every
// statement so that a runtime error can name where it happened - and which is
// exactly what a debugger needs. So this is not a second description of the
// program bolted to the side of it; it is the program's own position,
// listened to.
//
// **Compiled into the debug runtime only.** The boundary is in
// docs/DEBUGGING.md and it matters: the compiler's output does not change
// between debug and release by so much as a byte, and a release binary has no
// code for any of this and therefore no channel either. What differs is which
// archive was linked.
//
// Even in a debug build this is dormant until the environment arms it, so a
// debug build run normally is a program that reads one bool per statement and
// does nothing with it.
#pragma once

#include <cstdint>

namespace shm {

class Debug {
public:
    static Debug &shared();

    // Read once, at startup. Everything below is a no-op until this has said
    // yes, which is what lets a debug build be run without a debugger.
    void begin();
    bool armed() const { return armed_; }

    // Offered every statement. Returns at once unless this one is a place to
    // stop - a breakpoint, or a step that has come due.
    void at(int32_t unit, int32_t line);

    // The program is over, one way or the other.
    void ending(int status);

private:
    // A project here is expected to hold about five files, and a program to
    // want a handful of breakpoints in them. A hundred is past anything
    // anyone will set by hand, and going past it loses the newest rather than
    // failing - a debugger that refused to run because of a stale breakpoint
    // would be worse than one that quietly ignored it.
    static const int capacity = 100;

    enum Mode {
        Running,     // only a breakpoint stops it
        Stepping,    // the next statement, wherever it is
        Over,        // the next statement at this depth or shallower
        Out          // the next statement shallower than this
    };

    bool armed_ = false;
    Mode mode_ = Running;
    int32_t restDepth_ = 0;     // the depth 'over' and 'out' are measured from
    int32_t count_ = 0;
    int32_t units_[capacity] = {0};
    int32_t lines_[capacity] = {0};

    bool isBreakpoint(int32_t unit, int32_t line) const;
    void addBreakpoint(int32_t unit, int32_t line);
    void removeBreakpoint(int32_t unit, int32_t line);

    // Says where it is, then reads commands until one of them means go. The
    // program is standing still throughout, which is the whole point.
    void converse(int32_t unit, int32_t line);
    void say(const char *form, int32_t a, int32_t b, int32_t c);
};

// How many calls deep the program is. The recursion ceiling already counts
// this; stepping over a call is the same question asked differently.
int32_t callDepth();

}  // namespace shm
