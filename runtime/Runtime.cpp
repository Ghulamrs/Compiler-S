// The Shalimar runtime.
//
// Written in the same C++14 as the compiler and exported with C linkage. The
// classes below are not decoration: the state a print needs is about to grow
// - a multi-row grid starts on its own line, a precision directive applies
// from the middle of a print list onwards - and the state a diagnostic needs
// is a line number the whole program shares. Both belong to an object with a
// single instance rather than to loose file statics.
#include "shmrt.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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

// How a real is written when fixed notation will not do.
//
// Past 1e15 a double has no significant digits left to land after the point,
// so fixed notation would print hundreds of fabricated ones - 1e300 comes out
// as 309 characters. Those values, and the non-finite ones, keep the compact
// spelling instead, and the compact spelling has to be the same one the app
// prints or the two implementations disagree about what a number looks like.
//
// That spelling is the shortest decimal that reads back as the same double:
// the digits are found by asking for more of them until the answer round
// trips, which is what the app's Swift gets from its own conversion. Then
// they are laid out positionally while the exponent still allows it, and in
// exponent form after that - 1e15 prints as 1000000000000000.0 and 1e16 as
// 1e+16.
class Shortest {
public:
    // Writes into `out`, which must hold at least 40 characters.
    static void write(char *out, size_t size, double v) {
        if (std::isnan(v)) { std::snprintf(out, size, "nan"); return; }
        if (std::isinf(v)) { std::snprintf(out, size, v < 0 ? "-inf" : "inf"); return; }

        char digits[32];
        int exponent = 0;
        const int count = shortestDigits(v, digits, exponent);
        const bool negative = std::signbit(v);

        // exponent is the power of ten of the first digit plus one, so that
        // the value is 0.d1d2... x 10^exponent. Positional layout is used
        // while the point still falls inside or just after the digits, which
        // is where the app's conversion puts the boundary.
        std::string text;
        if (exponent > 16 || exponent < -3) {
            text += digits[0];
            // A single digit takes no point at all in exponent form: the app
            // prints 1e+16, not 1.0e+16. Positional form is the opposite way
            // round and always keeps one, which is why the two are written
            // out separately rather than sharing a tail.
            if (count > 1) {
                text += '.';
                text.append(digits + 1, static_cast<size_t>(count - 1));
            }
            const int e = exponent - 1;
            char tail[16];
            std::snprintf(tail, sizeof tail, "e%c%02d", e < 0 ? '-' : '+', e < 0 ? -e : e);
            text += tail;
        } else if (exponent <= 0) {
            text += "0.";
            for (int i = 0; i < -exponent; ++i) text += '0';
            text.append(digits, static_cast<size_t>(count));
        } else if (count <= exponent) {
            text.append(digits, static_cast<size_t>(count));
            for (int i = count; i < exponent; ++i) text += '0';
            text += ".0";
        } else {
            text.append(digits, static_cast<size_t>(exponent));
            text += '.';
            text.append(digits + exponent, static_cast<size_t>(count - exponent));
        }

        std::snprintf(out, size, "%s%s", negative ? "-" : "", text.c_str());
    }

private:
    // The fewest significant digits that read back as the same double. Asked
    // for one more each time rather than computed, because correctness here
    // is what matters and seventeen tries is not a cost anyone can measure.
    static int shortestDigits(double v, char *digits, int &exponent) {
        char buffer[64];
        for (int precision = 1; precision <= 17; ++precision) {
            std::snprintf(buffer, sizeof buffer, "%.*e", precision - 1, v);
            if (std::strtod(buffer, nullptr) == v) return unpack(buffer, digits, exponent);
        }
        std::snprintf(buffer, sizeof buffer, "%.17e", v);
        return unpack(buffer, digits, exponent);
    }

    // Takes '-1.2340e+05' apart into the digits '1234' and the exponent 6,
    // dropping the trailing zeros printf insisted on.
    static int unpack(const char *buffer, char *digits, int &exponent) {
        int count = 0;
        const char *p = buffer;
        if (*p == '-' || *p == '+') ++p;
        for (; *p && *p != 'e' && *p != 'E'; ++p) {
            if (*p >= '0' && *p <= '9') digits[count++] = *p;
        }
        while (count > 1 && digits[count - 1] == '0') --count;
        exponent = (*p ? std::atoi(p + 1) : 0) + 1;
        return count;
    }
};

class Console {
public:
    void printInt(int32_t value) {
        std::printf("%d ", static_cast<int>(value));
        lineHasText_ = true;
    }

    // A real prints to a fixed number of decimal places rather than to the
    // shortest spelling that round trips. Fixed, because a column is read
    // down its digits and '1.0' beside '0.3333333333333333' cannot be.
    // A char[] is not a grid - it prints as text, inline.
    void printText(const char *text) {
        std::printf("%s ", text);
        lineHasText_ = true;
    }

    void printReal(double value) {
        char text[400];
        if (std::isfinite(value) && (value < 1e15 && value > -1e15)) {
            std::snprintf(text, sizeof text, "%.*f", scalarPlaces_, value);
        } else {
            Shortest::write(text, sizeof text, value);
        }
        std::printf("%s ", text);
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
    int scalarPlaces_ = 7;
};

Console &console() {
    static Console instance;
    return instance;
}

// One counter per function plus one for the whole program. The per-function
// counter is a depth rather than a lifetime tally, and it comes down on the
// way out however the call ended - otherwise one deep call would poison every
// later one.
class Depth {
public:
    void enter(int32_t id, int32_t limit, const char *name) {
        if (total_ >= 1024) fail("Call stack too deep (limit 1024)");
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
    // More functions than any program the language is for will hold, and a
    // program with more simply goes uncounted per function - the overall
    // ceiling still catches it.
    static const int capacity = 1024;
    int32_t perFunction_[capacity] = {0};
    int32_t total_ = 0;
};

Depth &depth() {
    static Depth instance;
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

double shm_real_add(double a, double b) { return a + b; }
double shm_real_sub(double a, double b) { return a - b; }
double shm_real_mul(double a, double b) { return a * b; }
double shm_real_div(double a, double b) { return a / b; }
double shm_real_mod(double a, double b) { return std::fmod(a, b); }
double shm_real_pow(double a, double b) { return std::pow(a, b); }

int32_t shm_real_eq(double a, double b) { return a == b ? 1 : 0; }
int32_t shm_real_ne(double a, double b) { return a != b ? 1 : 0; }
int32_t shm_real_lt(double a, double b) { return a <  b ? 1 : 0; }
int32_t shm_real_gt(double a, double b) { return a >  b ? 1 : 0; }
int32_t shm_real_le(double a, double b) { return a <= b ? 1 : 0; }
int32_t shm_real_ge(double a, double b) { return a >= b ? 1 : 0; }
int32_t shm_real_and(double a, double b) { return (a != 0 && b != 0) ? 1 : 0; }
int32_t shm_real_or(double a, double b)  { return (a != 0 || b != 0) ? 1 : 0; }

double shm_int_to_real(int32_t value) { return static_cast<double>(value); }

// The one narrowing the language performs silently, and it can still fail:
// the fraction is dropped without a word, but a magnitude no int can hold is
// refused rather than wrapped.
int32_t shm_real_to_int(double value) {
    const double truncated = std::trunc(value);
    if (!(truncated >= -2147483648.0 && truncated <= 2147483647.0)) {
        char text[400];
        Shortest::write(text, sizeof text, value);
        char message[440];
        std::snprintf(message, sizeof message, "Cannot convert %s to int", text);
        fail(message);
    }
    return static_cast<int32_t>(truncated);
}

int32_t shm_real_truth(double value) { return value != 0 ? 1 : 0; }

void shm_loop_int_check(int32_t step) {
    if (step == 0) fail("Step value cannot be zero");
}

int32_t shm_loop_int_run(int64_t value, int32_t end, int32_t step) {
    if (step > 0) return value <= end ? 1 : 0;
    return value >= end ? 1 : 0;
}

int64_t shm_loop_int_advance(int64_t value, int32_t step) { return value + step; }

// A bound that arrives non-finite is an error naming which one and its value.
// That matters because such bounds come out of ordinary arithmetic:
// 'for i : 1. to sqrt(0.-1.)', 'to 1./0.', 'to pow(10.,400.)'.
void shm_loop_real_check(double start, double end, double step) {
    static const char *roles[] = {"start", "end", "step"};
    const double values[] = {start, end, step};
    for (int i = 0; i < 3; ++i) {
        if (std::isfinite(values[i])) continue;
        char text[400];
        Shortest::write(text, sizeof text, values[i]);
        char message[440];
        std::snprintf(message, sizeof message, "Loop %s out of range: %s", roles[i], text);
        fail(message);
    }
    if (step == 0) fail("Step value cannot be zero");
}

double shm_loop_real_value(double start, double step, double pass) {
    return start + pass * step;
}

int32_t shm_loop_real_run(double value, double end, double step) {
    if (step > 0) return value <= end ? 1 : 0;
    return value >= end ? 1 : 0;
}

void shm_enter(int32_t id, int32_t limit, const char *name) {
    depth().enter(id, limit, name);
}

void shm_leave(int32_t id) { depth().leave(id); }

void shm_print_int(int32_t value) { console().printInt(value); }

void shm_print_text(const char *text) { console().printText(text); }

void shm_print_real(double value) { console().printReal(value); }

void shm_line_end(void) { console().endLine(); }

}  // extern "C"

int main(void) {
    shm_begin();
    shm_user_main();
    return shm_end();
}
