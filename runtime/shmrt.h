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
//
// It grows with the language. Every entry point here is reachable from a
// program that runs on all three targets.
#ifndef SHALIMAR_RUNTIME_H
#define SHALIMAR_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

// Arithmetic. Every one of these can fail, which is why they are here rather
// than inline: passing an int limit is always an error, never a wrapped value
// that looks right, and the diagnostic belongs beside the rule it enforces.
int32_t shm_int_add(int32_t a, int32_t b);
int32_t shm_int_sub(int32_t a, int32_t b);
int32_t shm_int_mul(int32_t a, int32_t b);
int32_t shm_int_div(int32_t a, int32_t b);
int32_t shm_int_mod(int32_t a, int32_t b);
int32_t shm_int_pow(int32_t a, int32_t b);
int32_t shm_int_neg(int32_t a);

// Comparison and logic answer 1 or 0. Neither '&' nor '|' short-circuits -
// both sides are evaluated before either is asked - so they are ordinary
// calls like the rest.
int32_t shm_int_eq(int32_t a, int32_t b);
int32_t shm_int_ne(int32_t a, int32_t b);
int32_t shm_int_lt(int32_t a, int32_t b);
int32_t shm_int_gt(int32_t a, int32_t b);
int32_t shm_int_le(int32_t a, int32_t b);
int32_t shm_int_ge(int32_t a, int32_t b);
int32_t shm_int_and(int32_t a, int32_t b);
int32_t shm_int_or(int32_t a, int32_t b);

// Real arithmetic. None of these can fail - division by zero is an infinity
// here, not an error, and that is IEEE's answer rather than a decision taken
// in this language - but they live beside the int ones so that one shape of
// call serves both and the code generator has one thing to say.
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

// Conversions. int to real always succeeds; real to int truncates toward
// zero and is refused outside the range, which is the one narrowing the
// language performs silently and can still fail at run time.
double  shm_int_to_real(int32_t value);
int32_t shm_real_to_int(double value);

// Truthiness. A condition may be any scalar: zero is false and anything else
// is true. An int needs no help, but a real does - 0.5 is true, and
// converting it to an int first would make it false.
int32_t shm_real_truth(double value);

// Loop control.
//
// The pass number is kept and the counter recomputed from it, rather than the
// counter being accumulated. That is what the app does, and doing anything
// else would drift from it in the last digits of a real loop.
//
// The int loop's pass value is 64 bits wide on purpose. Stepping a 32-bit
// counter can pass the end of its range where the language says the loop
// should simply finish, and a wide value has nowhere to wrap.
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
// function approaches its own cap. Both are deliberate under-approximations
// of what the stack could take.
void shm_enter(int32_t id, int32_t limit, const char *name);
void shm_leave(int32_t id);

// One print item: the value followed by a single space. '?' then ends the
// line, '??' leaves it open.
void shm_print_int(int32_t value);
void shm_print_real(double value);
void shm_print_text(const char *text);
void shm_line_end(void);

#ifdef __cplusplus
}
#endif

#endif
