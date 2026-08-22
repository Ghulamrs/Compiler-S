// Arithmetic, conversions, loop control and the built-in functions.
//
// Every int operation is checked the same way: the arithmetic is done wide
// and refused if the answer does not fit. Int32 in Int64 has room for any
// sum, difference or product of two of them, so there is no case where the
// check itself can overflow.
#include "Internal.h"
#include "Shortest.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace shm {
namespace {

[[noreturn]] void overflow(const char *op) {
    static char message[64];
    std::snprintf(message, sizeof message, "int overflow in '%s' - use real", op);
    fail(message);
}

int32_t narrow(int64_t wide, const char *op) {
    if (wide < -2147483648LL || wide > 2147483647LL) overflow(op);
    return static_cast<int32_t>(wide);
}

}  // namespace

}  // namespace shm

using shm::fail;
using shm::Shortest;

extern "C" {

int32_t shm_int_add(int32_t a, int32_t b) { return shm::narrow(static_cast<int64_t>(a) + b, "+"); }
int32_t shm_int_sub(int32_t a, int32_t b) { return shm::narrow(static_cast<int64_t>(a) - b, "-"); }
int32_t shm_int_mul(int32_t a, int32_t b) { return shm::narrow(static_cast<int64_t>(a) * b, "*"); }

int32_t shm_int_div(int32_t a, int32_t b) {
    if (b == 0) fail("Division by zero");
    return shm::narrow(static_cast<int64_t>(a) / b, "/");
}

int32_t shm_int_mod(int32_t a, int32_t b) {
    if (b == 0) fail("Division by zero");
    return shm::narrow(static_cast<int64_t>(a) % b, "%");
}

// Repeated multiplication rather than a call to pow(), so that every partial
// product is checked and the answer is exact. A negative exponent has no int
// to answer with, which is a refusal rather than a zero.
int32_t shm_int_pow(int32_t a, int32_t b) {
    if (b < 0) fail("Negative power needs reals");
    int32_t result = 1;
    for (int32_t i = 0; i < b; ++i) {
        result = shm::narrow(static_cast<int64_t>(result) * a, "^");
    }
    return result;
}

int32_t shm_int_neg(int32_t a) { return shm::narrow(-static_cast<int64_t>(a), "-"); }

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

int32_t shm_real_truth(double value) { return value != 0 ? 1 : 0; }

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

// A char is one byte, so a value outside 0..255 is an error rather than being
// wrapped: a wrapped code point is a wrong character that looks like a right
// one.
int32_t shm_int_to_char(int32_t value) {
    if (value < 0 || value > 255) {
        char message[64];
        std::snprintf(message, sizeof message, "%d is not a char (0 to 255)",
                      static_cast<int>(value));
        fail(message);
    }
    return value;
}

int32_t shm_real_to_char(double value) {
    const double truncated = std::trunc(value);
    if (!(truncated >= 0.0 && truncated <= 255.0)) {
        char text[400];
        Shortest::write(text, sizeof text, value);
        char message[440];
        std::snprintf(message, sizeof message, "%s is not a char (0 to 255)", text);
        fail(message);
    }
    return static_cast<int32_t>(truncated);
}

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

double shm_fn_sqrt(double x)  { return std::sqrt(x); }
double shm_fn_log(double x)   { return std::log(x); }
double shm_fn_exp(double x)   { return std::exp(x); }
double shm_fn_hypot(double x, double y) { return std::hypot(x, y); }
double shm_fn_sin(double x)   { return std::sin(x); }
double shm_fn_cos(double x)   { return std::cos(x); }
double shm_fn_tan(double x)   { return std::tan(x); }
double shm_fn_asin(double x)  { return std::asin(x); }
double shm_fn_acos(double x)  { return std::acos(x); }
double shm_fn_atan(double x)  { return std::atan(x); }
double shm_fn_atan2(double y, double x) { return std::atan2(y, x); }
double shm_fn_pow(double x, double y)   { return std::pow(x, y); }
double shm_fn_round(double x) { return std::round(x); }
double shm_fn_ceil(double x)  { return std::ceil(x); }
double shm_fn_floor(double x) { return std::floor(x); }
double shm_fn_trunc(double x) { return std::trunc(x); }

double shm_fn_abs_real(double x) { return std::fabs(x); }

// abs() of the most negative int has no int to answer with, so it is the same
// refusal as any other overflow rather than a value that wraps to itself.
int32_t shm_fn_abs_int(int32_t x) { return shm::narrow(-static_cast<int64_t>(x) < 0
                                                       ? static_cast<int64_t>(x)
                                                       : -static_cast<int64_t>(x), "abs"); }

double  shm_fn_max_real(double a, double b) { return a > b ? a : b; }
double  shm_fn_min_real(double a, double b) { return a < b ? a : b; }
int32_t shm_fn_max_int(int32_t a, int32_t b) { return a > b ? a : b; }
int32_t shm_fn_min_int(int32_t a, int32_t b) { return a < b ? a : b; }

}  // extern "C"
