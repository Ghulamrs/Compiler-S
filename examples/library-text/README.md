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
```

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
