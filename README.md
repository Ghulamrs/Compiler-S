# Compiler-S

A compiler for **Shalimar**, the small numeric language the iOS app in
`../Shalimar` interprets. It reads a `.shm` program and writes native
assembly for three targets:

| Target | Object format | Assembler syntax | Calling convention |
| --- | --- | --- | --- |
| `arm64-darwin` | Mach-O | GNU | Apple's AAPCS64 |
| `x86_64-linux` | ELF | GNU | System V |
| `x86_64-windows` | COFF | MASM, for `ml64` | Microsoft x64 |

```
make
./shc examples/gcd.shm -o gcd
./gcd
gcd of 48 18
is 6
```

```
shc [options] file.shm
  -o <path>          where the result goes
  -S                 stop after writing assembly
  -c                 stop after assembling
  --target=<name>    arm64-darwin | x86_64-linux | x86_64-windows
  --runtime=<path>   the runtime archive to link against
```

Every target is built into every copy of the compiler; the host only decides
which one it can also assemble and link. `-S` works for all three from
anywhere.

## What it compiles

All of the 3.0 language: `int`, `real`, `char` and arrays of any rank, the
silent widening and narrowing between the numbers, functions with several
outputs and reference parameters, both `for` forms, `break` and `continue`,
text as `char[]` with join and the six comparisons, the twenty built-in
functions, `pi` and `e`, `prec(n)`, and the grid printer.

**Every program in `examples/` compiles and runs on all three machines, and
prints what the app's interpreter prints for it, byte for byte.** That is the
claim the suite exists to make; see *Testing*.

`../Shalimar/SHALIMAR_LANGUAGE.md` is the specification. The one place this
compiler deliberately disagrees with the app - the document says `t : s`
copies a string and the interpreter aliases it - is written down in
[docs/CONFORMANCE.md](docs/CONFORMANCE.md), along with three differences that
follow from being a compiler rather than an interpreter.

## How it is put together

```
Driver → tokenize() → Parser → Checker → CodeGen → Emitter → assembly
                                                        ↓
                                            arm64 / GNU x86-64 / MASM
```

**One walk of the tree, three emitters.** `CodeGen` is a single pass that
speaks only to the abstract `Emitter`. The order operands are evaluated in,
which runtime entry point a type reaches, where a jump goes - all the same on
every machine, so all written once. Everything a target may differ over is a
virtual method. Giving each target its own walk is the arrangement this is
the opposite of, and deliberately: two walks of the same tree drift.

**Three axes vary per target and they are kept apart.** `Abi` answers how
arguments travel; `Spelling` answers how an instruction is written down; the
target class answers what the assembler wants around the instructions. One
instruction stream serves both x86-64 targets - `GnuSpelling` and
`MasmSpelling` are the whole of what differs between them - so "Windows" not
becoming a synonym for "Intel syntax" is a live concern rather than a
tidiness one.

**The machine model is small on purpose.** One accumulator per kind of value,
and anything that has to outlive the evaluation of something else waits in a
numbered eight-byte slot in the frame. Slower than a register allocator, and
the same shape on all three machines - which is what lets one generator drive
them. Arithmetic goes through the runtime rather than being emitted inline,
because every int operation can fail: passing an int limit is an error here,
never a wrapped value that looks right, and the check belongs beside the rule
it enforces rather than written out three times in three instruction sets.
Inlining the operations that cannot fail is an optimisation nobody has needed
yet.

**The runtime does everything that is not a load, a store or a branch.**
`runtime/` is the same ISO C++14, compiled once per platform, exported with C
linkage because its caller is assembly. It owns arithmetic, arrays, text,
printing, the recursion ceiling and `main()` - the compiler emits Shalimar's
`main` as `shm_user_main` so the C entry point stays the runtime's.
`runtime/shmrt.h` is included by both halves, so the interface between them
has one definition.

**An array above rank one is an array of arrays.** Not a representation
preference: the language measures dimensions rather than declaring them, so
after `real A[2][5]` and `A[0] : {1.,2.}` the answer to `A[0].row` is 2. A
row is a value that can be replaced, so a row has to be a thing.

## Finding the rest of the program

Shalimar has no `include` and no `import`. A call to a function this file does
not define is looked for in the project's other files, and what is found is
compiled in — so a program becomes a library by renaming its `main()` to
something a caller can say, and nothing else about it changes.

```
$ shc main.shl -o main
shc: also compiled geometry.shl
```

Only functions are looked for; a brought-in function carries its own file's
globals and can see no others. Nothing arrives that was not asked for, which
is why a directory of programs is the ordinary case rather than a collision.
A wanted name in two files is refused naming both. A diagnostic names the file
when it is not the program's own, at compile time and at run time alike, and a
single-file program therefore prints exactly what it always printed.

The whole of it, including what it costs — a program that reaches into another
file no longer runs in the app — is [docs/CROSSFILE.md](docs/CROSSFILE.md).

## Testing

There are two suites and they ask different questions. **`tests/cases`** asks
whether a rule of the language is obeyed - each program is small and each one
is about one rule. **`tests/load`** asks whether the rules are still obeyed
when the program is large, and the answers were not the same: it found three
defects on the day it was written, none of which a small program could reach.

- an arm64 frame past 4095 bytes, because `sub sp, sp, #n` takes twelve bits;
- a call with more arguments than the registers carry, which read past the
  end of the register table - a five-argument function was handed `%xmm0`
  for its fifth argument on Windows, silently;
- every string comparison answering 0, because loading the zero to compare
  against destroyed the comparison's own result on a target where the
  accumulator is also the first argument register.

`tests/generate.py` writes that suite and is deterministic, so the committed
programs can be diffed after a change to it.

A case is `<name>.shm` beside `<name>.expected` in either directory. The
expected file holds the compiler's own output followed by the program's,
which is the order the app produces them in - it reports what the checker
found and then runs. It is recorded from the app's interpreter on a Mac and
committed, so neither of the other machines needs Swift or the app.

```
./tests/generate.py         rewrite tests/load
./tests/run.sh              the host suite: tests/cases and tests/load
./tests/remote-linux.sh     build with real g++ and run the suite on the box
./tests/remote-windows.sh   assemble with ml64 and run on the Windows box
./tests/record.sh           re-record expected output from the interpreter
./tests/cross.sh            finding the rest of the program in the other files
./tests/debug.sh            stopping and stepping a program from inside itself
./tests/linking.sh          what an object holds, and why it is a whole program
./tests/shortest.sh         the compact spelling of a double, against Swift
```

The standing result, all three green: **74 cases plus 11 cross-file, 19
debugging and 12 linking ones on the host and on Linux, 42 on Windows** - the other 32 are diagnostics, which the compiler produces on
whichever machine it runs on and the Windows box therefore never sees.

`tests/remote-linux.sh` is not only a second target. It is the only thing
that can say whether the sources are ISO C++14: Apple's libc++ hands you
C++17 names under `-std=c++14`, so a C++17-ism compiles clean on a Mac,
passes the host suite, and is refused only when it reaches real g++.

## The three machines

| Machine | Reached by | What it is for |
| --- | --- | --- |
| this Mac | — | writing; `arm64-darwin` natively |
| Linux box | `ssh -i ~/Documents/Claude/myMorningWalk.pem ec2-user@52.202.164.123` | real g++; `x86_64-linux` natively |
| Windows box | `ssh windows` | `ml64` and `link`; `x86_64-windows` natively |

The Windows shell is PowerShell, `git` is not on its `PATH`, and nested
quotes mangle: the far side runs a `.bat` under `cmd`, which is the only
shape that has never surprised. Its runtime is built there by `cl` and kept
in `C:\shalimar\runtime`; `tests/remote-windows.sh` rebuilds it every time,
because a suite that silently tested the previous runtime against this
compiler's calls would fail as a link error if you were lucky and as a wrong
answer if you were not.

## Known limitations

0. **A Shalimar program is a whole program and cannot be a piece of one.**
   `shc -c` writes an ordinary object file and that object still cannot be
   linked beside another Shalimar object or beside C: every unit exports the
   same three startup symbols, the runtime archive owns `main`, and the
   language has no declarations for a call across a link to be checked
   against. This is settled rather than pending, with the evidence and what
   would have to change in [docs/LINKING.md](docs/LINKING.md), and it is
   checked by `tests/linking.sh` so that the page cannot quietly go out of
   date. **Do not offer separate compilation on the grounds that `-c` exists.**
1. **Nothing is freed.** The interpreter is garbage collected and this is
   not. A program that joins strings inside a long loop will grow. The
   alternative would have to decide who owns a row that has been handed to a
   function by reference, which is a question the language does not ask.
2. **Arithmetic is a call.** Correct on all three targets and slower than it
   needs to be. Inlining the operations that cannot fail - and the overflow
   branch for the ones that can - is the obvious next thing, and it wants the
   assembly fingerprinted first so that "meant to change nothing" can be
   checked.
3. **No limit on how many arguments a call may take** - arguments beyond the
   registers travel in a block of frame slots whose address is handed over in
   a scratch register. That is this compiler's own convention rather than the
   platform's, which is allowed because both ends of such a call are code it
   wrote; no runtime call ever overflows, because none takes more than three
   arguments. It avoids the stack-argument rules, which is where the three
   platforms differ most.
4. **`shm_line` is a call per statement.** It is how a runtime error names
   its line. A store to a global would be cheaper and is three spellings.
5. **No debug information, and this is settled rather than pending.** There
   is no `-g`, no DWARF and no CodeView, and none is planned: the language is
   asked for by people writing numeric programs on a phone, and a symbolic
   debugger is not something they reach for. What replaces it is already
   here - a runtime error names its line and its function, and `-S` shows
   what was emitted - and those are what to extend if a program is ever hard
   to see into. **Do not add a line table on the grounds that the entry
   below used to read like a gap.**
