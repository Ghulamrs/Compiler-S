# Where this compiler and the app differ

`../Shalimar/SHALIMAR_LANGUAGE.md` is the specification, and it says so
itself: *"when the interpreter and this document disagree, that is a
conformance bug in the interpreter."* This compiler is the language's second
implementation, and the app's interpreter is the oracle its tests are
recorded from - which makes every place the two behave differently something
that has to be written down rather than discovered.

There are two kinds of entry here, and they are not the same kind of thing.

---

## 1. The document wins, and the app is wrong

### `t : s` copies; the interpreter aliases

§7.1 of the document:

> `t : s`  — creates a copy of s

The interpreter does not. `store(_:into:)` binds a name that does not yet
exist straight to the value it was given, and an `ArrayRef` is a class, so
both names end up on one array:

```
fun <> = main() {
  s : "hello"
  t : s
  t[0] : char(74)
  ? s                    // the app prints Jello; this compiler prints hello
  ? t                    // Jello either way
}
```

**This compiler copies**, because the document says copy. The recorded
expectation for `tests/cases/text_copy.shm` is therefore written by hand
rather than taken from `tests/record.sh`, and re-recording it would put the
app's answer back. That is the one file in the suite the recorder must not
be allowed to overwrite.

The same rule reaches `u : s + " there"` and `s : "hello"`, but those cannot
show the difference: a join and a literal are both fresh arrays already.

### A multi-assign converts at an existing target; the interpreter refuses

§5.2:

> `real` → `int` is **automatic at a declared destination and silent**

§7.3 says only that *"targets that do not exist are created with the declared
output types"*, and is silent about ones that do. A variable already declared
is a declared destination, so §5.2 governs and the conversion is automatic.

```
fun <real> = half(n: int) { return n / 2. }

fun <> = main() {
  int b : 0
  <b> : half(9)
  ? b                 // the document: 4.  The interpreter: a check error
}
```

The interpreter reports `'b' is int, not real` and stops. `shc` narrows
silently and prints `4`.

**The interpreter disagrees with itself here, which is what settles it.** The
same operation written as an ordinary assignment - `b : sqrt(16.)` with `b`
declared `int` - narrows silently in the interpreter and prints `4`, matching
the document and this compiler. Only the multi-assign spelling is refused,
and nothing in the document distinguishes the two.

`tests/cases/multi_convert.expected` is therefore hand-written to what the
document requires, not recorded from the oracle. Re-running `tests/record.sh`
over it would replace it with three check errors.

Found by a conformance review on 2026-08-24, together with the defect that hid
it: `shc` used to load each returned value with the *target's* kind rather
than the output's, so the value was reinterpreted rather than converted and
the answer was garbage either way. That is fixed; this entry is about what it
was fixed *to*.

---

## 2. The document quotes an approximation of a message

The document's §13.3 lists the three warnings by their sense rather than
their exact text. The app's own wording is what a reader sees, and what
this compiler emits:

| The document says | The app says, and so does this |
| --- | --- |
| `Function 'f' is defined but never called` | `'f' is never called` |

Nothing about the behaviour differs; only how it is written. The suite
compares text, so it had to be one or the other, and the one a person
actually reads won.

---

## 3. Deliberate differences, because this is a compiler

These are not conformance gaps - the language does not speak about them -
but they are differences a program can feel.

**Nothing is freed.** The app's interpreter is garbage collected; this
runtime allocates arrays and strings and never gives them back. A program
that joins strings inside a long loop will grow. The alternative would be a
scheme that has to decide who owns a row that has been handed to a function
by reference, which is a question the language does not otherwise ask - see
the note at the top of `runtime/Array.cpp`.

**A runtime error exits the process.** The app catches it and prints it into
its console, so the next program in the same process starts clean. Here the
message goes to the same stream the program's own output went to, in the same
order, and the process ends with status 1.

**Arguments beyond the registers use a convention of this compiler's own.**
A block of frame slots, whose address travels in a scratch register. Both
ends of such a call are code this compiler wrote, so no platform rule is
broken; runtime calls never reach it, because none takes more than three
arguments and every convention here carries at least four.

---

## 4. Something the app does that this does not

**The app's parser is exponential in the depth of nested calls.**
`f(f(f(...)))` twenty-four deep takes its interpreter nine seconds and
twenty-six deep does not finish, whether or not the expression is ever
executed - so it is the parse rather than the run. `shc` compiles thirty deep
without pausing.

That is why `tests/load/nested_calls.shm` stops at twenty: the limit is the
oracle's, not this compiler's. It is a defect in the app rather than a
difference in the language, and it has not been reported upstream from here.

---

## 5. Something this compiler does that the app cannot

**A program that declares a foreign function has no interpreter.**

```
uses <real> = c_total(a[]: real)
```

says that something outside this program provides `c_total`, and `shc` links a
library that does — `--with=libmine.a`. The app has no link step and never
will: it interprets a source file, and there is nowhere for a `.a` to go.

So this is a divergence that cannot be closed, only stated. Both readers parse
the declaration — they must agree on what a program *is* — and the app refuses
it by name rather than mangling the diagnostic:

```
Error: line 1: 'c_total' comes from a library - shc can build this program,
                the app cannot link one
```

It reads the head far enough to say **which** function before refusing, because
"unsupported syntax" would send a reader looking at their spelling.

**This is the only construct in the language with that property.** Everything
else in `SHALIMAR_LANGUAGE.md` runs both ways; a foreign declaration is the one
thing whose meaning is supplied at link time, and the app has no link time. It
is written here rather than in the specification because the specification
describes the language, and the language is not what differs — the two hosts
are.

`uses sin, cos` is untouched by any of this: the app has those functions
already, so it reads that form and carries on.
