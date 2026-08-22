# Stopping a Shalimar program

Shalimar has no debug information — no DWARF, no CodeView, and none planned.
It does not need any, because it already tells its runtime where it is:

```
shm_line(unit, line)      before every statement, in every build
```

That call exists so a runtime error can name the file and line it happened
in. It is also, exactly, what a debugger needs. So the debugger is not a
second description of the program bolted to the side of it — it is the
program's own position, listened to.

What that buys over a line table:

- **statement granularity, not an approximation.** The compiler is telling
  the truth about where it is, rather than a table describing where it was;
- **the same thing on all three targets.** `x86_64-windows` is where cc1's
  debug information stops — it generates MASM, MASM carries no line table,
  and the assembler cannot spell the relocations CodeView would want. None of
  that matters here, because there is no debug format involved;
- **no debugger.** No gdb, no lldb, no cdb, nothing to find or install;
- **stdin is free.** Shalimar has no input at all, so nothing competes for the
  channel the debugger talks on.

## The boundary

The line number is not a debug feature. `Error: line 45: Index 1 out of range`
is a language rule and works in every build, so **`shm_line` is emitted
always** and the compiler's output does not change between debug and release
by so much as a byte.

What changes is which runtime is linked.

| | release | debug |
| --- | --- | --- |
| what `shc` emits | `shm_line(unit, line)` per statement | **the same, byte for byte** |
| runtime linked | `shmrt-<target>.a` | `shmrt-<target>-debug.a` |
| `shm_line` does | records the position | records it, then offers it to the session |
| an error names file and line | yes | yes |
| breakpoints, stepping | **not possible** - no code for it | when armed |

So there are three states rather than two:

1. **Release.** No debugger code in the binary at all: no branch per
   statement, no channel, nothing to switch on.
2. **Debug, run normally.** The code is there and dormant. One bool read per
   statement, which is nothing beside the call it is inside.
3. **Debug, run by the editor.** Armed by the environment, and talking.

Three consequences worth being explicit about:

- **A release build cannot be debugged.** That is the point of the boundary,
  not a limitation of it. Rebuild in debug - it is the same source and the
  same compiler output.
- **A debug build is not slower to any degree worth measuring**, and is not a
  different program. There is no `-g`, no define, and no `#ifdef` in anything
  a Shalimar programmer writes.
- **The assembly is the same either way**, so nothing that checks the
  compiler's output has to know this feature exists.

## Arming it

```
SHM_DEBUG=1 ./program
```

Anything else, and a debug build runs exactly as a release build does. The
variable is read once, at startup.

## The protocol

Line-oriented both ways. **Commands arrive on standard input; the program
answers on standard error.** Standard output is the program's own and is never
written to by the session — a debugger that interleaved itself with the
program's printing would be unreadable, and worse, would change what the
program appeared to print.

The program says:

| | |
| --- | --- |
| `#ready` | armed, and about to run the first statement |
| `#stop <unit> <line> <depth>` | stopped, and waiting for a command |
| `#at <unit> <line> <depth>` | the answer to `w` |
| `#exit <status>` | the program is over |

The editor says:

| | |
| --- | --- |
| `b <unit> <line>` | set a breakpoint |
| `d <unit> <line>` | clear one |
| `c` | continue |
| `s` | step: stop at the next statement, wherever it is |
| `n` | next: the next statement at this depth or shallower |
| `o` | out: the next statement shallower than this |
| `w` | where |
| `q` | stop the program now |

`unit` is 0 for the program's own file and higher for one the compiler went
looking in - the same numbering a diagnostic uses, so a breakpoint and an
error name the same file the same way. `depth` is how many calls deep the
program is, which is the counter the recursion ceiling already keeps.

## What it does not do

Reading a variable. That needs something the compiler does not emit yet - a
table of a function's names against its frame slots - and it is the next
thing rather than part of this. Everything else a numeric program's debugger
wants, stopping and walking, is here.

Stepping into the runtime, machine registers and a disassembly view are not
here and are not planned. This is a debugger for Shalimar, not for what
Shalimar compiles to.
