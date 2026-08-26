# How an array crosses into C

`LINKING.md` item 3 says an array crossing the boundary "would need a
documented shape", and that nothing about it was written down. This is that
document. It turns out less was missing than the sentence implies — the shape
is already right, and what it lacked was being stated, tested, and reachable.

## What a `real[]` is, at the boundary

**One pointer.** A Shalimar array parameter is passed exactly as a scalar is,
in the first argument register — `x0` on arm64, `rcx`/`rdi` by platform
convention elsewhere. There is no descriptor, no length beside it, no second
register. `fun <real> = f(a[]: real)` is `double f(const ShmArray *)`.

**Opaque.** `ShmArray` is an incomplete type in `runtime/shmrt.h`:

```c
typedef struct ShmArray ShmArray;
```

The layout is not part of this ABI and is not promised. A C function reads and
writes through the accessors and nothing else. That is what makes this ABI
cheap to keep: the runtime may change how an array is stored whenever it likes,
and no C caller notices.

## The accessors

Everything a C function may do with a Shalimar array, from `shmrt.h`:

```c
int32_t shm_array_dim (const ShmArray *a, int32_t axis);   /* extent of an axis */

int32_t shm_get_int   (const ShmArray *a, int32_t index);
double  shm_get_real  (const ShmArray *a, int32_t index);
int32_t shm_get_char  (const ShmArray *a, int32_t index);

void    shm_set_int   (ShmArray *a, int32_t index, int32_t value);
void    shm_set_real  (ShmArray *a, int32_t index, double  value);
void    shm_set_char  (ShmArray *a, int32_t index, int32_t value);

ShmArray *shm_array_make(int32_t element, int32_t rank, const int64_t *dims);
```

`element` is one of `SHM_INT`, `SHM_REAL`, `SHM_CHAR`, `SHM_REF`.

**A rank-2 array is nested, not flat.** This is the part worth getting right,
and the first draft of this page got it backwards.

`shm_array_dim` understands the logical shape — for `real[2][3]`, axis 0 is 2
and axis 1 is 3. But the *elements* of the outer array are references to rows,
not doubles. So:

```c
int32_t rows = shm_array_dim(a, 0);      /* 2 */
int32_t cols = shm_array_dim(a, 1);      /* 3 */
ShmArray *row = shm_get_ref(a, r);       /* the r'th row  */
double    v   = shm_get_real(row, c);    /* a[r][c]       */
```

`shm_get_real(a, i)` on the outer array is **not** `a[0][i]` and is not
diagnosed as a mistake — it reads a reference as a double and hands back
whatever that is. A two-by-three read that way printed `2.16e-314` twice and
then stopped with `Index 2 out of range 0...1`, which is the outer extent
complaining. That is the trap this section exists for.

A rank-1 array holds its elements directly, so `shm_get_real(a, i)` is right
there and only there. `shm_get_ref` is the accessor that says which case you
are in, and it is in the header for exactly this reason.

## Reading the header from C89

`shmrt.h` used to open with `#include <stdint.h>`. That is C99, and **cc1 does
not ship it** — so until 2026-08-26 the one C compiler written alongside this
runtime could not compile a line against its own project's header, and nobody
had tried. The include is conditional now, and C89's own types stand in where
it is absent.

That matters more than it sounds. cc1 is the compiler a user of these tools
already has, and a runtime header its own compiler cannot read is not an ABI —
it is a C++ implementation detail with a `.h` on the end.

## Building a library against it

**No compiler makes a library.** cc1, cl and gcc make *objects*; `ar` or
`lib.exe` makes a library out of objects. Two steps, and the compiler owns the
first.

```
                 compile the C          make the library
  Mac      cc1 -c / clang -c            ar rcs libmine.a mine.o
  Linux    cc1 -c / gcc -c              ar rcs libmine.a mine.o
  Windows  cc1 -c / cl /c               lib /out:mine.lib mine.obj
```

All six combinations were run before this document was written, each linked
into a Shalimar program and executed, each returning the same answer from the
same three-element array.

**`shc` does not make a library today**, and an earlier draft of this page
said it never could. That was too strong, and worth correcting rather than
quietly dropping.

What is actually true, established by trying it:

- `shc` **refuses a file with no `main()`** — `Check.cpp`, a checker rule
  rather than anything about objects. Lifting that check makes the compiler
  crash, because codegen assumes an entry point, so the rule is load-bearing
  as things stand.
- Two symbols are emitted **per unit under fixed names**: `shm_init_globals`
  and `shm_name_files`. Two Shalimar objects collide on those whether or not
  either has a `main`, so removing `main` removes one collision of three.

Both are `LINKING.md`'s items 1 and 2, which that page itself calls mechanical.
Its item 4 — that the language has no declarations for a call across a link to
be checked against, "the one that is not a matter of effort" — **does not apply
when the caller is C**, because C declares what it calls. So a Shalimar library
consumed by a C program is a different and much smaller problem than a Shalimar
library consumed by Shalimar.

None of that is built. It is written here so the next person weighing it starts
from what is true rather than from the sentence this replaced.

## Linking it

The Shalimar object, the library, and the runtime archive meet at the linker.
The C object must have no `main`: the runtime owns that, and a second one is
`LINKING.md` reason 2.

```
$ shc prog.shm -c -o prog.o
$ c++ -o whole prog.o libmine.a lib/shmrt-arm64-darwin.a -lm
```

On Windows, with `link` and the `.lib`s, which is what `shc`'s own driver does
for the runtime.

## What is still missing, and it is not the ABI

**A Shalimar program cannot yet call any of this.** `uses` names functions from
a curated table of C standard library functions, and not one of those takes an
array — `<math.h>` is entirely scalar, and everything in `<stdlib.h>` or
`<string.h>` that touches memory does so through a pointer Shalimar has no type
for. So a table row taking `real[]` would have nothing to point at.

The demonstrations above reach the C by pointing an emitted call at it, which
is a thing a test can do and a program cannot. Closing that needs `uses` to be
able to name a user's own function from a named library — the foreign function
interface `FOREIGN.md` deferred as "a separate decision with a separate
document". This page is half of what that decision would need; it deliberately
does not make it.

## What the suite checks

`tests/abi.sh`, run by `make test`:

1. a C file compiles against `shmrt.h` **with cc1** where cc1 is present, and
   with the host compiler otherwise — which is the check that would have caught
   the `<stdint.h>` fault the day it appeared;
2. every function this document names is actually exported by the runtime
   archive, so a rename or removal fails here rather than in somebody's build;
3. the C object archives, links beside a Shalimar object with no collision, and
   **runs**, returning the value the Shalimar side would have returned.
