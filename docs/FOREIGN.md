# Borrowing a library function

Shalimar has nineteen maths functions built in — `sin`, `sqrt`, `pow` and the
rest — and every one of them is available to every program whether it wants it
or not. This replaces that with a request:

```
uses sin, cos, tan
```

A file says what it borrows, and borrows nothing else.

Written before any of it was built, so that the decisions are on the page
rather than in the code, and so that the parts deliberately left out are
recorded once instead of being re-argued.

## Why ask, when it already works without asking

**The names.** Shalimar reserves thirteen words and gave back the twenty-five
it used to reserve. A program may call a variable `abs`, or `pi`, and the
compiler steps aside. Making forty more library functions available by default
would take forty more names from every program that never wanted one of them —
the exact trade that was already refused once.

`uses` inverts it. `fmod` is an ordinary identifier until a file asks for it.
Adding the whole of `<math.h>` costs no program a single name.

**It scales.** There is no version of "built in" that reaches forty functions
without the namespace paying for it. There is no version of `uses` that does
not.

## What it compiles to: nothing

This is the part that matters most and is the least obvious.

`shc` already emits a call by naming an undecorated symbol and letting the
backend spell it for the target:

```cpp
emitter_.call(intAnswer ? fn.intSymbol : fn.realSymbol);
```

`Arm64DarwinEmitter::call` writes `bl _sin`, the x86-64 emitter writes what
its own target wants, and neither `CodeGen` nor the table has to know. Today
the table names `shm_fn_sin`, a wrapper in the runtime archive that is one line
of `return std::sin(x);`. **`uses` changes that string to `sin` and stops
there.**

Proven before writing any of it, by taking the assembly `shc` emits today,
replacing `_shm_fn_sin` with `_sin`, and assembling it:

```
$ shc -S p.shm -o p.s
$ sed 's/_shm_fn_sin/_sin/' p.s > direct.s
$ c++ -c direct.s -o direct.o && c++ -o direct direct.o lib/shmrt-arm64-darwin.a
$ ./direct
0.4794255 0.8775826         # identical to the wrapper version
```

So:

- **no `.c` file is generated**, inline or otherwise;
- **no C compiler is invoked** — `shc` alone still compiles a Shalimar program;
- **no object is produced** beyond the one `shc` was already writing;
- **nothing binary is added to this repository or to what it ships.**

An earlier sketch had `shc` generate a small C file per program, compile it and
link the result. It would have worked, and it was dropped for those four
reasons — the first and last especially. A build step that emits a binary is a
build step somebody has to trust.

**The runtime gets smaller, not larger.** `runtime/Numbers.cpp` is mostly
forwarding wrappers; once calls go direct, sixteen of them are dead code and
`shmrt.a` loses them. Forty new borrowable functions would add zero bytes to
it.

## The table, and why there is one

`uses sin` gives no types. Something has to know that `sin` takes a `real` and
answers one, so `shc` carries a table: the name, its Shalimar signature, and
the symbol to call.

That table is not an implementation detail — **it is the boundary of the
feature**, and it is what makes the boundary honest rather than a matter of
policy. A row can only exist if the function's C signature can be written in
Shalimar's types, and Shalimar's types are `int`, `real`, `char` and arrays of
them. There are no pointers; the word does not appear in the specification.

So:

```
uses memset
Error: line 1: 'memset' takes a pointer, which Shalimar has no type for
```

Refused **by name, with the reason**, at the point of interception — not left
to fail at the link, and never assembled into a call that would corrupt memory.
The same answer covers `strlen`, `malloc`, `fopen`, `qsort` and `printf`:
every one of them needs a type the language does not have.

This is worth stating plainly because it is the question the feature invites. A
person who writes `uses memset` is not being unreasonable; they are asking
whether the door is wider than it is. The table answers, and the answer is
always the same shape.

## The rules

**1. `uses` is per file.** A file borrows for itself. This follows
`CROSSFILE.md` rule 1 — a function pulled in from another file brings its own
file's globals — for the same reason: what a file depends on travels with it,
and a file compiled in from elsewhere must not need something the file that
called it happened to declare.

**2. It goes in global space**, beside the globals, before or between them.
Several names to a line, commas between:

```
uses sin, cos, tan
uses fmod
```

**3. A borrowed name is an ordinary name until borrowed.** `uses` does not
reserve anything. A file that does not borrow `fmod` may use `fmod` for a
variable, and a file that borrows it may not — the same rule a function name
already follows.

**4. Borrowing something never called is not an error.** It costs nothing, no
code is emitted for it, and a file that borrows a set for a project it is part
of should not be nagged about the two it did not reach for today.

**5. Every library function is borrowed, including the nineteen.** `sin` and
`sqrt` are not special cases that work without asking. One rule, no exceptions
— which is a breaking change to every existing program that calls one, and is
being made now precisely because "every existing program" is twenty files in
these repositories and nothing outside them.

## What this is not

**Not a foreign function interface.** A program cannot declare an arbitrary C
function and call it. The table is curated; a name that is not in it is
refused, however well the caller believes they know its signature.

**Not a way to link your own C.** That already works and needs no language
feature: `shc -c` writes an ordinary object, a C object with no `main` links
beside it, and the two meet at the linker —

```
$ shc prog.shm -c -o prog.o && cc -c helper.c -o helper.o
$ c++ -o mixed prog.o helper.o lib/shmrt-arm64-darwin.a
```

— which was checked before this document was written. What is missing there is
only the ability to *call* the C from the Shalimar, and `uses` does not supply
it, because a user's own function is not in the table. If that is ever wanted,
it is a separate decision with a separate document.

**Not a step towards separate compilation.** `LINKING.md` says why a Shalimar
group cannot be one of several in a mixed link, and nothing here changes any of
its three reasons. Two of them do not apply to `uses` at all: there is still
one Shalimar unit, and the runtime still owns `main`.

## Arrays, which are not in phase one

An array could cross the boundary — it is already a handle the runtime makes
and reads — but nothing about that handle is written down as an ABI. It is
whatever `shm_array_make` and `shm_get_real` currently agree on, which is
`LINKING.md`'s item 3. A borrowable function taking `real[]` waits on that
being specified, and specifying it is the work, not the calling.

## The trap to expect on Linux

libm is inside `libSystem` on a Mac, so `_sin` resolves with nothing named on
the link line. On the Linux box it is a separate `-lm`. It links today only
because `runtime/Numbers.cpp` includes `<cmath>` and drags it in — and this
change **deletes those wrappers**, so the archive may stop referencing libm at
all and the link line will need `-lm` explicitly.

That is a fault that passes on the Mac and fails on the box, which is the
shape this repository has been caught by before. Test the Linux link before
believing the Mac one.


## Built, and the two things that were not on this page

Phases as written: `uses` parses, then gates, then the table points at libm.
Green on all three machines. Two faults turned up that this document had not
foreseen, and both are the kind that only appear on one machine.

**`fabs` is an x87 instruction.** Borrowing `abs` means calling libm's `fabs`,
and `FABS` is a mnemonic ml64 has known since the 8087 - so `EXTRN fabs:PROC`
is read as an instruction and answers `A2008: syntax error : fabs`. It
assembles cleanly on both Unix targets and fails only under MASM.
`OPTION NOKEYWORD:<fabs>` is MASM's own answer, emitted only in a module that
actually calls it. Safe module-wide because this compiler emits no x87 at all -
every float goes through SSE.

**Five of the twenty are not library calls and must not become them.** The
redirection looked like a mechanical rename and is not:

- `shm_fn_abs_int` traps on `INT_MIN` rather than negating it; C's `abs()` is
  undefined there, so it is a different function that shares a name.
- `shm_fn_max_real` and `shm_fn_min_real` are `a > b ? a : b`, which propagates
  NaN. `fmax`/`fmin` return the non-NaN operand instead - a real difference in
  answers, not a spelling.
- `shm_fn_max_int` and `shm_fn_min_int` have no C library equivalent at all.

So seventeen wrappers went and five stayed. `shmrt-arm64-darwin.a` fell from
46,336 bytes to 44,456, and the borrowable set can now grow without it growing
at all.

**`-lm` is named rather than relied upon.** After the wrappers went, the
archive references libm only through `std::fmod` and `std::pow`, which the `%`
and `^` operators still use. If those ever move, nothing in the archive would
pull libm in and the flag becomes the only thing that does. It is a no-op on a
Mac and on glibc 2.34 and later, and the difference between linking and not on
anything older.
