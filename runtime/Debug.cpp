#include "Debug.h"

#include "Internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace shm {
namespace {

// Standard error, and never standard output. The program's printing is what
// standard output is for, and a session that interleaved itself with it would
// not only be unreadable - it would change what the program appeared to
// print, which is the one thing a debugger must not do.
void tell(const char *line) {
    std::fputs(line, stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

// Two whole numbers, read by hand. sscanf would do it, and MSVC refuses to
// compile a call to it without a warning that /WX turns into an error - so
// this is four lines instead of a define that switches the warning off.
bool readTwo(const char *text, int32_t &first, int32_t &second) {
    int32_t *into[2] = {&first, &second};
    for (int which = 0; which < 2; ++which) {
        while (*text == ' ' || *text == '\t') ++text;
        if (*text < '0' || *text > '9') return false;
        int32_t value = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (*text - '0');
            ++text;
        }
        *into[which] = value;
    }
    return true;
}

}  // namespace

Debug &Debug::shared() {
    static Debug instance;
    return instance;
}

// Asked once. getenv is what every other platform has and what MSVC refuses
// to compile a call to without a warning that /WX makes an error, so Windows
// is asked in its own words rather than the whole runtime being built with
// the deprecation switched off.
namespace {

bool askedToDebug() {
#ifdef _WIN32
    char value[16];
    const DWORD n = GetEnvironmentVariableA("SHM_DEBUG", value, sizeof value);
    if (n == 0 || n >= sizeof value) return false;
    return value[0] != '\0' && std::strcmp(value, "0") != 0;
#else
    const char *asked = std::getenv("SHM_DEBUG");
    return asked && *asked && std::strcmp(asked, "0") != 0;
#endif
}

}  // namespace

void Debug::begin() {
    armed_ = askedToDebug();
    if (!armed_) return;

    // The files, before anything can be asked about them. Which number a
    // file has depends on which files the compiler was given, and nobody
    // outside the compiler can work that out - so the program says.
    const Position &where = Position::shared();
    for (int32_t unit = 0; unit < where.named(); ++unit) {
        const char *name = where.nameOf(unit);
        if (!*name) continue;
        char text[300];
        std::snprintf(text, sizeof text, "#file %d %s", static_cast<int>(unit), name);
        tell(text);
    }

    // Before the first statement, and before the globals are made - so a
    // breakpoint can be set inside a global's initializer, which is a place a
    // program can genuinely fail.
    tell("#ready");
    converse(0, 0);
}

bool Debug::isBreakpoint(int32_t unit, int32_t line) const {
    for (int32_t i = 0; i < count_; ++i)
        if (units_[i] == unit && lines_[i] == line) return true;
    return false;
}

void Debug::addBreakpoint(int32_t unit, int32_t line) {
    if (isBreakpoint(unit, line) || count_ >= capacity) return;
    units_[count_] = unit;
    lines_[count_] = line;
    ++count_;
}

void Debug::removeBreakpoint(int32_t unit, int32_t line) {
    for (int32_t i = 0; i < count_; ++i) {
        if (units_[i] != unit || lines_[i] != line) continue;
        units_[i] = units_[count_ - 1];
        lines_[i] = lines_[count_ - 1];
        --count_;
        return;
    }
}

void Debug::at(int32_t unit, int32_t line) {
    if (!armed_) return;

    const int32_t depth = callDepth();
    bool stop = false;
    switch (mode_) {
    case Running:  stop = false; break;
    case Stepping: stop = true; break;
    case Over:     stop = depth <= restDepth_; break;
    case Out:      stop = depth < restDepth_; break;
    }
    if (!stop && !isBreakpoint(unit, line)) return;

    converse(unit, line);
}

void Debug::say(const char *form, int32_t a, int32_t b, int32_t c) {
    char text[64];
    std::snprintf(text, sizeof text, form, static_cast<int>(a), static_cast<int>(b),
                  static_cast<int>(c));
    tell(text);
}

void Debug::converse(int32_t unit, int32_t line) {
    // What the program has printed so far, before it is said to have stopped.
    // Standard output is buffered and standard error is not, so without this
    // the whole of a program's printing arrives at the end - and a debugger
    // that shows you the output only after the run is over is not showing you
    // anything you stopped for.
    Console::shared().flush();

    const int32_t depth = callDepth();
    if (line > 0) say("#stop %d %d %d", unit, line, depth);

    char command[256];
    while (std::fgets(command, sizeof command, stdin)) {
        size_t n = std::strlen(command);
        while (n > 0 && (command[n - 1] == '\n' || command[n - 1] == '\r')) command[--n] = '\0';

        switch (command[0]) {
        case 'c':
            mode_ = Running;
            return;
        case 's':
            mode_ = Stepping;
            return;
        case 'n':
            mode_ = Over;
            restDepth_ = depth;
            return;
        case 'o':
            mode_ = Out;
            restDepth_ = depth;
            return;
        case 'w':
            say("#at %d %d %d", unit, line, depth);
            break;
        case 'b':
        case 'd': {
            int32_t at = 0, which = 0;
            if (!readTwo(command + 1, at, which)) break;
            if (command[0] == 'b') addBreakpoint(at, which);
            else removeBreakpoint(at, which);
            break;
        }
        case 'q':
            Console::shared().flush();
            tell("#exit 130");
            std::exit(130);
        default:
            break;
        }
    }

    // The other end has gone. Carrying on under a debugger nobody is holding
    // would leave a program stopped for ever on a machine nobody is watching.
    mode_ = Running;
    armed_ = false;
}

void Debug::ending(int status) {
    if (!armed_) return;
    char text[32];
    std::snprintf(text, sizeof text, "#exit %d", status);
    tell(text);
}

}  // namespace shm
