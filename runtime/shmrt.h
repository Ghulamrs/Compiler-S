// The Shalimar runtime.
//
// Compiled code calls into this for everything that is not arithmetic or
// control flow. It is declared with C linkage because the caller is assembly,
// and its name is what the assembly writes: nothing here may be overloaded,
// inlined away, or renamed by a compiler's mangling.
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

// The compiler emits the Shalimar main() under this name, so that the C
// entry point stays the runtime's.
void shm_user_main(void);

// One print item: the value followed by a single space. '?' then ends the
// line, '??' leaves it open.
void shm_print_int(int32_t value);
void shm_line_end(void);

#ifdef __cplusplus
}
#endif

#endif
