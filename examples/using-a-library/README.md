# Calling a C library from Shalimar

```
cd ../../../Compiler-C/examples/shalimar-library && ./build.sh   # make the library
cd -                                             && ./build.sh   # build and run this
```

```
total    20.0000000
mean     4.0000000
largest  6.0000000
grid     21.0000000
sqrt of the mean  2.0000000
after scaling by ten, total  200.0000000
```

## The two forms of `uses`

```
uses sqrt                                  // from the table shc carries
uses <real> = stats_mean(a[]: real)        // from a library the link is given
```

The first borrows a C standard library function whose signature `shc` already
knows. The second declares one it has never seen: **the declaration is the
whole contract**, and it is checked exactly as a function you wrote would be —

```
stats_mean()          ->  'stats_mean' takes 1, got 0
stats_mean(1)         ->  Argument 1 of 'stats_mean' must be real[]
```

Nothing is inferred and nothing is trusted. `shc` will not go looking for the
function's real signature, because it has no way to.

## Where the library comes from

`--with=<path>`, on the command line, not in the source. The program says *what*
it calls; the build says *where that lives* — the same split C makes between a
header and `-l`. The same source then serves a machine where the library sits
somewhere else.

Forget it and `shc` says so before the linker gets a chance:

```
shc: 'stats_total' is declared with 'uses' and comes from a library, but no
     library was named. Add --with=<path> - see docs/FOREIGN.md.
```

## Arrays cross too

`real[]` arrives in C as an opaque `ShmArray *`, and `real[][]` as an array of
row references. Shalimar passes arrays by reference, so `stats_scale` writing
through the handle is visible here — that is the last line of the output.

`../../docs/ARRAY-ABI.md` is the contract.

## One thing this program cannot do

**It will not run in the Shalimar app.** The app interprets a source file and
has no link step, so there is nowhere for a `.a` to go. It refuses by name
rather than by complaining about syntax:

```
Error: line 13: 'stats_total' comes from a library - shc can build this
                program, the app cannot link one
```

That is the only construct in the language with that property, and it is
recorded in `../../docs/CONFORMANCE.md`.
