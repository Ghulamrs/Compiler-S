// The Shalimar runtime.
//
// Written in the same C++14 as the compiler and exported with C linkage. The
// classes below are not decoration: the state a print needs is about to grow
// - a multi-row grid starts on its own line, a precision directive applies
// from the middle of a print list onwards - and the state a diagnostic needs
// is a line number the whole program shares. Both belong to an object with a
// single instance rather than to loose file statics.
#include "shmrt.h"

#include <cstdio>
#include <cstdlib>

namespace {

// Where the program is. A runtime error names the statement executing, which
// is what the compiler sets before each one.
class Position {
public:
    void set(int32_t line) { line_ = line; }
    int32_t line() const { return line_; }

private:
    int32_t line_ = 0;
};

Position &position() {
    static Position instance;
    return instance;
}

// A runtime failure. Output already printed survives - the error follows it
// rather than replacing it - so the console is flushed before the message is
// written, and the message goes to the same stream so the order is kept.
//
// There is deliberately no stage name in the text. Which stage caught a
// problem is an implementation detail, and the vocabulary of a compiler's
// internals has no place in a diagnostic aimed at someone writing a program.
[[noreturn]] void fail(const char *message) {
    std::fflush(stdout);
    if (position().line() > 0) {
        std::printf("Error: line %d: %s\n", static_cast<int>(position().line()), message);
    } else {
        std::printf("Error: %s\n", message);
    }
    std::fflush(stdout);
    std::exit(1);
}

[[noreturn]] void overflow(const char *op) {
    static char message[64];
    std::snprintf(message, sizeof message, "int overflow in '%s' - use real", op);
    fail(message);
}

// Every int operation is checked the same way: the arithmetic is done wide
// and refused if the answer does not fit. Int32 in Int64 has room for any
// sum, difference or product of two of them, so there is no case where the
// check itself can overflow.
int32_t narrow(int64_t wide, const char *op) {
    if (wide < -2147483648LL || wide > 2147483647LL) overflow(op);
    return static_cast<int32_t>(wide);
}

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

void shm_line(int32_t line) { position().set(line); }

int32_t shm_int_add(int32_t a, int32_t b) {
    return narrow(static_cast<int64_t>(a) + b, "+");
}

int32_t shm_int_sub(int32_t a, int32_t b) {
    return narrow(static_cast<int64_t>(a) - b, "-");
}

int32_t shm_int_mul(int32_t a, int32_t b) {
    return narrow(static_cast<int64_t>(a) * b, "*");
}

int32_t shm_int_div(int32_t a, int32_t b) {
    if (b == 0) fail("Division by zero");
    return narrow(static_cast<int64_t>(a) / b, "/");
}

int32_t shm_int_mod(int32_t a, int32_t b) {
    if (b == 0) fail("Division by zero");
    return narrow(static_cast<int64_t>(a) % b, "%");
}

// Repeated multiplication rather than a call to pow(), so that every partial
// product is checked and the answer is exact. A negative exponent has no int
// to answer with, which is a refusal rather than a zero.
int32_t shm_int_pow(int32_t a, int32_t b) {
    if (b < 0) fail("Negative power needs reals");
    int32_t result = 1;
    for (int32_t i = 0; i < b; ++i) result = narrow(static_cast<int64_t>(result) * a, "^");
    return result;
}

int32_t shm_int_neg(int32_t a) { return narrow(-static_cast<int64_t>(a), "-"); }

int32_t shm_int_eq(int32_t a, int32_t b) { return a == b ? 1 : 0; }
int32_t shm_int_ne(int32_t a, int32_t b) { return a != b ? 1 : 0; }
int32_t shm_int_lt(int32_t a, int32_t b) { return a <  b ? 1 : 0; }
int32_t shm_int_gt(int32_t a, int32_t b) { return a >  b ? 1 : 0; }
int32_t shm_int_le(int32_t a, int32_t b) { return a <= b ? 1 : 0; }
int32_t shm_int_ge(int32_t a, int32_t b) { return a >= b ? 1 : 0; }

int32_t shm_int_and(int32_t a, int32_t b) { return (a != 0 && b != 0) ? 1 : 0; }
int32_t shm_int_or(int32_t a, int32_t b)  { return (a != 0 || b != 0) ? 1 : 0; }

void shm_print_int(int32_t value) { console().printInt(value); }

void shm_line_end(void) { console().endLine(); }

}  // extern "C"

int main(void) {
    shm_begin();
    shm_user_main();
    return shm_end();
}
