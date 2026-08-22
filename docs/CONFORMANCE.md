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

**More than eight arguments to one call is not compiled.** Every target here
passes arguments in registers only, and neither has more than eight of the
kind a Shalimar argument uses. The compiler says so in its own words rather
than emitting something that will not work.
