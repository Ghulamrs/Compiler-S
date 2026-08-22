# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this is

A compiler for **Shalimar**, the small numeric language the iOS app in
`../Shalimar` interprets. It reads a `.shm` program and writes native
assembly for three targets: `arm64-darwin`, `x86_64-linux` and
`x86_64-windows`.

**`../Shalimar/SHALIMAR_LANGUAGE.md` is the specification.** Where this
compiler and that document disagree, the document wins and this compiler is
what gets fixed. Where the *app's interpreter* and the document disagree, the
document still wins - the interpreter is the oracle the tests are recorded
from, not the authority. Every such divergence is written down in
`docs/CONFORMANCE.md`; nothing is left as an undocumented difference.

## The language this is written in

`src/` and `runtime/` are **ISO C++14**, and anything added to them must be
too. All three toolchains pin it: `-std=c++14 -Wall -Wextra -Werror
-pedantic` in the `Makefile`, `/std:c++14 /W4 /WX` for `cl`.

**A Mac cannot enforce this.** Apple's libc++ hands you C++17 names under
`-std=c++14`, so a C++17-ism compiles clean here, passes the host suite, and
is refused only when it reaches real g++. `./tests/remote-linux.sh` builds
with g++ 11 on the Linux box, which is what actually says whether the sources
are C++14. Run it before believing a change is finished.

One more thing a Mac will not catch: **trigraphs are still live in C++14**.
`"use '??' or ..."` in a string literal becomes `use '^ ...` under
`-pedantic`. Write `'?\?'`.

## Build and test

```
make                      build shc and the host runtime
./tests/run.sh            the host suite
./tests/remote-linux.sh   build with real g++ and run the suite on the box
./tests/remote-windows.sh assemble with ml64 and run on the Windows box
./tests/record.sh         re-record expected output from the app's interpreter
./tests/cross.sh          finding the rest of the program in the other files
./tests/debug.sh          stopping a program from inside itself
./tests/linking.sh        what an object holds, and why it is a whole program
./tests/build-windows.sh  build shc itself on the Windows box, with cl
```

`build.bat` is the MSVC build, and `cl` is the third toolchain the C++14 claim
has to hold under. On that host the driver names `ml64` and `link` rather than
`c++`, so shc there needs the Visual Studio environment at run time too.

A case is `tests/cases/<name>.shm` beside `tests/cases/<name>.expected`. The
expected file is what the app's interpreter printed, recorded by
`tests/record.sh` on a Mac and committed, so the other two machines need
neither Swift nor the app's checkout.

## Architecture

```
Driver -> tokenize() -> Parser -> Checker -> CodeGen -> Emitter -> assembly
```

**`CodeGen` is one walk of the tree and there are three `Emitter`s.** That is
the opposite of giving each target its own walk, and it is deliberate: the
order values are evaluated in, where a jump goes, which runtime entry point a
type reaches are all the same on every machine and are written once.
Everything a target may differ over is a virtual method on `Emitter`.

**Three things vary per target and they are separate axes.** `Abi` answers
how arguments travel; `Spelling` answers how an instruction is written down;
the target class answers what the assembler wants around the instructions.
One instruction stream serves both x86-64 targets - `GnuSpelling` and
`MasmSpelling` are the whole of what differs between them - so keeping
"Windows" from becoming a synonym for "Intel syntax" is a live concern, not a
tidiness one.

**The runtime does the work that is not arithmetic or control flow.**
`runtime/Runtime.cpp` owns printing, and will own arrays, strings and
diagnostics. It is C++14 with C linkage, compiled once per platform, and it
also owns `main()`: the compiler emits Shalimar's `main` as `shm_user_main`
so the C entry point stays the runtime's.

## Decisions that are settled

**No debugger support.** No `-g`, no DWARF, no CodeView, and none planned -
asked and answered on 2026-08-22. What stands in for it is `shm_line`, which
lets a runtime error name its line and its function, and `-S`. Extend those
if a program is hard to see into; do not start a line table.

## How this is built

**One language feature at a time, green on all three targets before the
next.** The skeleton came up as `? 1 2 3` compiled and run natively on all
three machines before anything else was written, and each feature since has
gone in the same way. A backend that is brought up late is a backend that is
ported rather than written, and the two do not produce the same code.

## Verification

**Prove the artefact, not the exit status.** A green suite proves nothing
until you know what it ran against - `make` with nothing to rebuild, or a
relayed tree that never updated, will both report success.

- `[ shc -nt src/Parser.cpp ]` before believing a remote run
- **a stale object file is a heap corruptor, not a link error.** The Makefile
  carries `-MMD -MP` for this reason. Without header dependencies an edited
  header rebuilds only some translation units, the rest keep the previous
  definition of a class, the link succeeds because the mangled names still
  match, and the program corrupts its heap somewhere unrelated. A sanitiser
  build cannot reproduce it - a sanitiser build is a clean build. The remote
  suites therefore build from clean, because a relayed tree is not a
  checked-out one.
- grep the emitted assembly for a token the change introduces; it is the
  cheapest confirmation there is
- when a suite fails on one host and passes on another, **suspect the host's
  tools before the compiler**. `timeout` is GNU coreutils and absent on macOS

## The machines

| Machine | Reached by | What it is for |
| --- | --- | --- |
| this Mac | - | writing, `arm64-darwin` natively |
| Linux box | `ssh -i ~/Documents/Claude/myMorningWalk.pem ec2-user@52.202.164.123` | real g++, `x86_64-linux` natively |
| Windows box | `ssh windows` | `ml64` and `link`, `x86_64-windows` natively |

The Windows shell is PowerShell, `git` is not on its `PATH`, and nested
quotes mangle - the far side runs a `.bat` under `cmd`, which is the only
shape that has never surprised.
