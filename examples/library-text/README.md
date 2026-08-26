# Text across the boundary

```
cd ../../../Compiler-C/examples/shalimar-library && ./build.sh
cd -                                             && ./build.sh
```

```
extent of the array  14
length of the text   13
how many l           3
fraction that are l  0.2307692
same as other        1
same as different    0
upper-cased          HELLO, SAILOR
42 letters is really 16.0000000
rotated              XUBBE, IQYBEH
and back again       HELLO, SAILOR
cosh^2 - sinh^2      1.0000000
```

## Both forms of `uses`, in one program

```
uses len, fmod, sinh, cosh                 // the table shc carries
uses <int> = text_length(s[]: char)        // the library the link is given
```

Nothing at a call site says which is which, and nothing needs to.

`fmod` earns its place: a rotation of 42 letters is a rotation of 16, and
wrapping it is what a modulo is for. It happens on the Shalimar side so that
`text_shift` can trust what it is handed. `%` would do the same and need no
borrow — `fmod` is available for a program that would rather name the
operation.

`sinh` and `cosh` have **nothing to do with text**, and are here for one honest
reason: to show a borrowed function being *checked* rather than decorated.
`cosh(x)² - sinh(x)²` is 1 for every x, so if the two were swapped, or either
went to the wrong symbol, the last line would stop saying `1.0000000`. Reading
the numbers themselves would never reveal that.

The rotation is checked the same way — turned by 16 and then by 10, and the
text has to come back.

## Two things this example exists to say

**A literal carries a terminator.** `char greeting[14] : "hello, sailor"` is
fourteen elements — thirteen codes and a nought. So `len(greeting)` is 14 and
`text_length(greeting)` is 13, and a C function that trusts the extent reads
one element too many. That is the first two lines of the output, side by side
on purpose.

**A char is a code, not a byte.** `shm_get_char` answers `int32_t`. There is no
`char *` anywhere in this, and there could not be: Shalimar has no pointer
type, so a function taking one could never have been declared.

## Why the library carries a comparison

Shalimar's `=` on two `char[]` compares **addresses**, not text. `greeting` and
`other` hold the same thirteen characters and are not the same array, so
`text_same` is the only way to ask the question that was meant. That is a
documented divergence in the language rather than anything to do with the
boundary — see `SHALIMAR_LANGUAGE.md` — and it is why a text library is worth
having at all.

The C is `text.c` in the library directory.
