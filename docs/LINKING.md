# Why a Shalimar group is a whole program

The editor in `../RStudio` grew a compiler per group, so that one target can
hold C and C++ together: each group compiles to objects with its own compiler
and the objects meet at the linker. The obvious next question was whether
Shalimar could be a third group in that mixture. `shc -c` exists, after all,
and it writes a perfectly ordinary object file.

**It cannot, and this is the answer rather than a note about what is not
finished yet.** `tests/linking.sh` checks every fact below, so a change to the
compiler that made any of it untrue would be caught rather than leaving this
page quietly wrong.

## What a Shalimar object contains

```
$ shc lib.shm -c -o lib.o
$ nm -g lib.o
0000000000000024 T _shm_init_globals
0000000000000000 T _shm_name_files
0000000000000fe4 T _shm_user_main
0000000000000034 T _shmf_twice
                 U _shm_real_mul
                 U _shm_line
                 ...
```

The encouraging half is `_shmf_twice`. A user function *is* an external symbol,
and for a scalar its calling convention is the machine's own — `fun <real> =
twice(x: real)` arrives in `d0` and leaves in `d0`, which is exactly `double
twice(double)`. So the question is not whether a C caller could reach the code.

The deciding half is the three above it. **Every unit exports the same three
names, whatever file it came from**: the program's entry point, its own file's
globals, and the table of file names a diagnostic uses.

## Three reasons, and each one is enough

**1. Two Shalimar objects collide.**

```
$ c++ -o two a.o b.o lib/shmrt-arm64-darwin.a
duplicate symbol '_shm_name_files' in: a.o b.o
duplicate symbol '_shm_user_main'  in: a.o b.o
duplicate symbol '_shm_init_globals' in: a.o b.o
```

Not "does not work yet" — cannot, by construction. Every unit claims the same
three names, so a second one is always a link error.

**2. The runtime owns `main`.** It is what sets the globals up, names the
files and calls `shm_user_main`. So a C program with a `main` of its own cannot
link the Shalimar runtime:

```
$ c++ -o mixed caller.o lib.o lib/shmrt-arm64-darwin.a
duplicate symbol '_main' in: caller.o  shmrt-arm64-darwin.a(Runtime.o)
```

And a Shalimar object without the runtime has nothing to call — `shm_line`,
`shm_real_mul` and the rest are all undefined in it. There is no arrangement of
the two that links.

**3. There is nothing to check a call against.** Shalimar has no `include`, no
`import`, and no declarations of any kind; `docs/CROSSFILE.md` explains why it
is not getting one. What it has instead is the search: a call this file does not
define is *looked for in the project's other files and compiled in*, so the
whole program is typed together. A call that crossed a link could not be checked
at all — and rule 1 of that document, that a pulled function brings its own
file's globals with it, has no meaning once the two files are separate objects
with separate `shm_init_globals`.

The first two are mechanical and could be fixed. The third is the language.

## So what is `shc -c` for

Stopping after assembling, for one program, so that the object can be looked at
or handed to a linker by hand — the same reason `-S` exists. `-o` names the
object exactly as it names the assembly under `-S`. (It used to have `.o` put on
the end of whatever was given, so `-c -o f.o` wrote `f.o.o`; that is fixed, and
`tests/linking.sh` keeps it fixed.)

It is not a step towards separate compilation and there is not going to be one.

## What the editor does with this

`Project::targetParts` in `../RStudio/src/project.cpp` refuses Shalimar in a
mixture, and refuses it twice because there are two different mistakes:

- **In one group with C or C++** — no compiler takes both, so naming one cannot
  help. The message names the group, because splitting the list is the fix.
- **In a group of its own beside C or C++** — where every other pair of
  languages now builds. The message is about what a Shalimar object is, and
  points here.

A project that wants Shalimar beside C is a project that builds two programs,
not one program made of two languages.

## What would have to change

Written down so that nobody has to work it out again in order to decide against
it a second time:

1. `shm_user_main`, `shm_init_globals` and `shm_name_files` would have to carry
   their unit — `shm_init_globals_<unit>` and so on — and something would have
   to call every unit's initialiser before any of them ran.
2. `main` would have to leave the runtime archive for a separate startup object,
   so that a C `main` could link the runtime without colliding with it.
3. An array or string crossing the boundary would need a documented shape. A
   `real[]` is a handle the runtime makes and reads (`shm_array_make`,
   `shm_get_real`); nothing about that is written down as an ABI, and it would
   have to be.
4. And the language would need declarations, so that a call across a link could
   be checked — which is the one that is not a matter of effort.
