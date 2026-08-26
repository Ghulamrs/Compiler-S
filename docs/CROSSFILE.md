# Finding the rest of the program

Shalimar has no `include`, no `import` and no `using`, and it is not getting
one. Telling the compiler where something is, is the compiler's work rather
than the programmer's — so **a call to a function this file does not define is
looked for in the project's other files**, and what is found is compiled in.

What that buys is reuse without ceremony:

```
// geometry.shl - a program until its main() was renamed
uses abs

real tolerance : 1e-9

fun <real> = area(w: real, h: real) { return w * h }

fun <int> = nearly(a: real, b: real) {
  if abs(a - b) < tolerance { return 1 }
  return 0
}

fun <> = demo() { ? "geometry demo" area(2.0, 3.0) }
```

```
// main.shl
fun <> = main() {
  ? area(3.0, 4.0)
  ? nearly(0.1 + 0.2, 0.3)
  demo()
}
```

```
$ shc main.shl -o main
shc: also compiled geometry.shl
```

There is no header to write, no list to keep up to date, and no line at the
top of the file that has to be right. **A program becomes a library by
renaming its `main()` to something a caller can say** — that rename is the
whole of it, because a function called `main` is the one name nothing can
ask for.

## The rules

**0. A file borrows for itself.** `geometry.shl` says `uses abs` because
`geometry.shl` calls it; `main.shl` says nothing, and does not need to know what
the file it calls into reaches for. See `FOREIGN.md` - this is the same rule as
the one below, applied to the library rather than to globals.

**1. Only functions are looked for.** A global belongs to the file that
declares it. A function that is brought in brings its own file's globals with
it — the ones it actually reads, and no others — which is why `nearly` above
can see `tolerance`. Without that rule two files could reach into each other's
state and the order they were found in would start to matter.

**2. Nothing arrives that was not asked for.** A function is brought in
because something called it, and then whatever it calls in turn. A file's
`main()` is therefore never brought in, and a directory full of programs is
the ordinary case rather than a collision — the app ships twelve of them in
one directory, and `rotations.shm` and `rotmat.shm` both define `ROX`, `ROY`
and `ROZ` without either being wrong.

**3. Two files answering to a name something wants is refused, naming both.**

```
Error: 'area' is in geometry.shl and rival.shl - it can be in one
```

Picking a winner would make the order the compiler happened to read the files
in into a part of the language. A name **nobody wants** may be in as many
files as it likes; rule 2 is what makes that safe.

**4. A diagnostic names the file when it is not the program's own.**

```
Error: faulty.shl line 2: Undefined variable 'missing'
Error: faulty.shl line 2: Division by zero
```

Compile time and run time both. A line number pointing into a file the reader
is not looking at is worse than no line number at all. The program's own file
is never named, so **a single-file program prints exactly what it always
printed** — which is what keeps every recorded expectation in `tests/cases`
valid.

## Where it looks

| given | looks in |
| --- | --- |
| `shc main.shl` | the Shalimar files beside `main.shl` |
| `shc main.shl a.shl b.shl` | `a.shl` and `b.shl`, and nowhere else |
| `shc main.shl --no-search` | nowhere: one file, as before |

RStudio always names the files, because a project knows which files are its
own and a directory does not. The bare form is for the command line, where a
directory is usually the honest answer.

## What it costs

**A program that reaches into another file no longer runs in the app.** The
app has one file and no project, so `area` would be an undefined function
there. This is the one place where a Shalimar program can be written for the
desktop and not for the phone, and there is no way round it short of the app
growing a project of its own.

That is why the compiler says `also compiled geometry.shl` on standard error
every time it happens. A dependency that arrived silently is one that will be
discovered when the file is moved without it.

**A typo can find something.** Call `ares(1.0, 2.0)` where you meant `area`
and, if some other file happens to define `ares`, it is compiled in rather
than reported. Rule 3 catches the case where two files define it; nothing
catches the case where exactly one does. The line the compiler prints is the
defence: a program that suddenly reaches into a file it never used before says
so.

## What is not shared

Globals, as rule 1 says. Also: nothing is *linked* in the C sense. `shc` still
compiles one program in one pass — the other files' functions are moved into
it before checking, so there is no separate compilation, no object file per
source, and no link order. The whole program is typed together, which is what
lets a function in one file be checked against a call in another.
