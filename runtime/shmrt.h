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

// One print item: the value followed by a single space. '?' then ends the
// line, '??' leaves it open.
void shm_print_int(int32_t value);
void shm_line_end(void);

#ifdef __cplusplus
}
#endif

#endif
