// The Shalimar runtime.
//
// Compiled code calls into this for everything that is not a load, a store or
// a branch. It is declared with C linkage because the caller is assembly, and
// its name is what the assembly writes: nothing here may be overloaded,
// inlined away, or renamed by a compiler's mangling.
//
// The compiler includes this header too. There is one declaration of the
// interface between the two halves, so a change to it cannot be made on one
// side only.
#ifndef SHALIMAR_RUNTIME_H
#define SHALIMAR_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// What an array holds. The compiler names these when it asks for one.
#define SHM_INT   0
#define SHM_REAL  1
#define SHM_CHAR  2
#define SHM_REF   3   /* an array of arrays: rank two and above */

typedef struct ShmArray ShmArray;

// Called by the runtime's own main(), around the program.
void shm_begin(void);
int  shm_end(void);

// The compiler emits Shalimar's main() under this name, so the C entry point
// stays the runtime's.
void shm_user_main(void);

// Which statement is executing. Set once per statement, because a runtime
// error names the statement containing it rather than the expression: only
// statements carry a line, and a per-expression one would need a call stack
// to be worth having.
void shm_line(int32_t line);

// Arithmetic. Every int operation can fail, which is why they are here rather
// than inline: passing an int limit is always an error, never a wrapped value
// that looks right, and the diagnostic belongs beside the rule it enforces.
int32_t shm_int_add(int32_t a, int32_t b);
int32_t shm_int_sub(int32_t a, int32_t b);
int32_t shm_int_mul(int32_t a, int32_t b);
int32_t shm_int_div(int32_t a, int32_t b);
int32_t shm_int_mod(int32_t a, int32_t b);
int32_t shm_int_pow(int32_t a, int32_t b);
int32_t shm_int_neg(int32_t a);

int32_t shm_int_eq(int32_t a, int32_t b);
int32_t shm_int_ne(int32_t a, int32_t b);
int32_t shm_int_lt(int32_t a, int32_t b);
int32_t shm_int_gt(int32_t a, int32_t b);
int32_t shm_int_le(int32_t a, int32_t b);
int32_t shm_int_ge(int32_t a, int32_t b);
int32_t shm_int_and(int32_t a, int32_t b);
int32_t shm_int_or(int32_t a, int32_t b);

// None of the real operations can fail - division by zero is an infinity
// here, not an error, and that is IEEE's answer rather than a decision taken
// in this language.
double shm_real_add(double a, double b);
double shm_real_sub(double a, double b);
double shm_real_mul(double a, double b);
double shm_real_div(double a, double b);
double shm_real_mod(double a, double b);
double shm_real_pow(double a, double b);

int32_t shm_real_eq(double a, double b);
int32_t shm_real_ne(double a, double b);
int32_t shm_real_lt(double a, double b);
int32_t shm_real_gt(double a, double b);
int32_t shm_real_le(double a, double b);
int32_t shm_real_ge(double a, double b);
int32_t shm_real_and(double a, double b);
int32_t shm_real_or(double a, double b);

// Conversions. Widening always succeeds; narrowing is refused rather than
// wrapped, because a wrapped value is a wrong answer that looks like a right
// one. 'checked' says whether the caller wrote int() or char() rather than
// reaching a declared destination - the message differs, not the rule.
double  shm_int_to_real(int32_t value);
int32_t shm_real_to_int(double value);
int32_t shm_int_to_char(int32_t value);
int32_t shm_real_to_char(double value);

// Truthiness. A condition may be any scalar: zero is false and anything else
// is true. An int needs no help, but a real does - 0.5 is true, and
// converting it to an int first would make it false.
int32_t shm_real_truth(double value);

// Loop control. The pass number is kept and the counter recomputed from it,
// rather than the counter being accumulated: that is what the app does, and
// anything else drifts from it in the last digits of a real loop. The int
// loop's pass value is 64 bits wide because stepping a 32-bit counter can
// pass the end of its range where the language says the loop should finish.
void    shm_loop_int_check(int32_t step);
int32_t shm_loop_int_run(int64_t value, int32_t end, int32_t step);
int64_t shm_loop_int_advance(int64_t value, int32_t step);

void    shm_loop_real_check(double start, double end, double step);
double  shm_loop_real_value(double start, double step, double pass);
int32_t shm_loop_real_run(double value, double end, double step);

// Recursion. Unbounded recursion exhausts the native stack as a segmentation
// fault, which cannot be caught and takes the program down with an empty
// console. Two ceilings prevent it: a per-function one of 256 / (inputs + 1)
// frames, and an overall 1024 that catches mutual recursion where no single
// function approaches its own cap.
void shm_enter(int32_t id, int32_t limit, const char *name);
void shm_leave(int32_t id);

// Arrays.
//
// 'dims' points at the extents in order, each one sixty-four bits wide
// because the caller writes them into frame slots and a slot is eight bytes.
// An extent must be at least 1, which is checked here for the ones only a run
// can know.
ShmArray *shm_array_make(int32_t element, int32_t rank, const int64_t *dims);
ShmArray *shm_array_from_text(const char *bytes, int32_t length);

int32_t shm_array_dim(const ShmArray *array, int32_t axis);

int32_t shm_get_int(const ShmArray *array, int32_t index);
double  shm_get_real(const ShmArray *array, int32_t index);
int32_t shm_get_char(const ShmArray *array, int32_t index);
ShmArray *shm_get_ref(const ShmArray *array, int32_t index);

void shm_set_int(ShmArray *array, int32_t index, int32_t value);
void shm_set_real(ShmArray *array, int32_t index, double value);
void shm_set_char(ShmArray *array, int32_t index, int32_t value);
void shm_set_ref(ShmArray *array, int32_t index, ShmArray *value);

// Assigning an array into one that already exists copies into the storage
// already there rather than rebinding it: extents are fixed at declaration,
// and an array may be shared by reference with a caller, so swapping the
// storage would resize it underneath them. The destination is cleared first,
// which is what makes a gap inside a literal and a literal that stops short
// mean the same thing.
void shm_array_fill(ShmArray *destination, const ShmArray *source);

// Text. Comparison reads up to the terminator rather than to the declared
// capacity, so the same name in a char[20] and a char[128] is equal.
ShmArray *shm_text_concat(const ShmArray *a, const ShmArray *b);
int32_t   shm_text_compare(const ShmArray *a, const ShmArray *b);

// Built-in functions and the two constants.
double  shm_fn_sqrt(double x);
double  shm_fn_log(double x);
double  shm_fn_exp(double x);
double  shm_fn_hypot(double x, double y);
double  shm_fn_sin(double x);
double  shm_fn_cos(double x);
double  shm_fn_tan(double x);
double  shm_fn_asin(double x);
double  shm_fn_acos(double x);
double  shm_fn_atan(double x);
double  shm_fn_atan2(double y, double x);
double  shm_fn_pow(double x, double y);
double  shm_fn_round(double x);
double  shm_fn_ceil(double x);
double  shm_fn_floor(double x);
double  shm_fn_trunc(double x);
double  shm_fn_abs_real(double x);
int32_t shm_fn_abs_int(int32_t x);
double  shm_fn_max_real(double a, double b);
double  shm_fn_min_real(double a, double b);
int32_t shm_fn_max_int(int32_t a, int32_t b);
int32_t shm_fn_min_int(int32_t a, int32_t b);

// One print item: the value followed by a single space. '?' then ends the
// line, '??' leaves it open. A numeric array prints as a grid; a char array
// prints as text, inline.
void shm_print_int(int32_t value);
void shm_print_real(double value);
void shm_print_char(int32_t value);
void shm_print_array(const ShmArray *array);
void shm_print_places(int32_t places);
void shm_line_end(void);

#ifdef __cplusplus
}
#endif

#endif
