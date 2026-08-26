#!/bin/sh
# Compile and run every program in examples/, and check each against the app's
# interpreter where that can be built.
#
#   ./tests/examples.sh
#   SHC=/path/to/shc.exe ./tests/examples.sh
#
# **This exists because examples/ rotted silently.** When `uses` landed, the
# migration walked tests/ and the app's Examples and missed this directory:
# eight of its twelve programs stopped compiling and nothing said so, because
# nothing here built them. A directory no suite reaches is a directory that
# goes stale without anybody being told.
#
# It is not a differential suite. tests/run.sh already compares against a
# recorded answer; this asks a smaller and different question - does the
# example a reader is pointed at still work - and the answer is worth having
# separately, because these files are documentation as much as code.
#
# The interpreter comparison is a bonus rather than the point: it needs swiftc
# and the app's checkout, so it is skipped with a word where those are absent.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SHC="${SHC:-$ROOT/shc.exe}"
OUT="$ROOT/tests/out-examples"
SHALIMAR="${SHALIMAR:-$ROOT/../Shalimar}"

[ -x "$SHC" ] || { echo "examples: no $SHC - run make first, or set SHC="; exit 2; }

rm -rf "$OUT" && mkdir -p "$OUT"

# The reference interpreter, if this machine can build one. Same binary and the
# same staleness rule as tests/record.sh - all five sources, not just one.
REF="${TMPDIR:-/tmp}/shalimar-reference"
compare=1
if [ ! -f "$SHALIMAR/Shalimar/Interpreter.swift" ] || ! command -v swiftc >/dev/null 2>&1; then
    compare=0
else
    stale=0
    [ -x "$REF" ] || stale=1
    for f in TokenKind Node Parse Check Interpreter; do
        [ "$SHALIMAR/Shalimar/$f.swift" -nt "$REF" ] && stale=1
    done
    if [ "$stale" = 1 ]; then
        swiftc -O "$SHALIMAR/Shalimar/TokenKind.swift" "$SHALIMAR/Shalimar/Node.swift" \
               "$SHALIMAR/Shalimar/Parse.swift" "$SHALIMAR/Shalimar/Check.swift" \
               "$SHALIMAR/Shalimar/Interpreter.swift" "$SHALIMAR/Tests/harness/main.swift" \
               -o "$REF" >/dev/null 2>&1 || compare=0
    fi
fi

pass=0
fail=0
for case in "$ROOT"/examples/*.shm; do
    [ -e "$case" ] || continue
    name=$(basename "$case" .shm)

    # Its own directory. shc compiles the other files beside a program when it
    # needs them, and twelve programs in one directory would each pull in the
    # other eleven's names.
    work="$OUT/$name"
    mkdir -p "$work"
    cp "$case" "$work/"

    if ! "$SHC" "$work/$name.shm" -o "$work/$name.bin" > "$work/compile" 2>&1; then
        echo "FAIL $name: shc refused it - $(head -1 "$work/compile")"
        fail=$((fail + 1))
        continue
    fi

    if ! "$work/$name.bin" > "$work/actual" 2>&1; then
        echo "FAIL $name: compiled, then failed to run"
        fail=$((fail + 1))
        continue
    fi

    if [ "$compare" = 1 ]; then
        "$REF" "$case" > "$work/reference" 2>&1
        if ! cmp -s "$work/actual" "$work/reference"; then
            echo "FAIL $name: shc and the interpreter disagree"
            diff "$work/reference" "$work/actual" | head -6
            fail=$((fail + 1))
            continue
        fi
    fi

    pass=$((pass + 1))
done

# ---- the ones that need a library ------------------------------------------
#
# examples/*/prog.shm, each with its own build.sh and a C library in
# Compiler-C. They are not in the loop above because they are in
# subdirectories and because they need --with=, and they are checked here
# rather than nowhere for the reason this whole file exists: an example
# nothing builds is an example that rots.
#
# Skipped with a word where the library cannot be built - a machine without
# cc1 or a host cc is not a failure, it is a machine that cannot ask.
linked=0
# Overridable, because Compiler-C is not called that everywhere - on the Linux
# box it is ~/ansicc, and this looked for a directory that does not exist there
# and then skipped the whole block **without saying so**. A check that quietly
# did not run is the shape of green this project does not accept, so an absent
# library directory is now a sentence rather than a silence.
LIBDIR="${LIBDIR:-$ROOT/../Compiler-C/examples/shalimar-library}"
if [ ! -f "$LIBDIR/build.sh" ]; then
    echo "  (no $LIBDIR - the library examples are not checked here;"
    echo "   name it with \$LIBDIR if Compiler-C is somewhere else)"
elif true; then
    if ( cd "$LIBDIR" && CC1="${CC1:-$ROOT/../Compiler-C/cc1.exe}" \
                         RT="$ROOT/runtime" ./build.sh >"$OUT/lib.log" 2>&1 ) ||
       ( cd "$LIBDIR" && CC=cc RT="$ROOT/runtime" ./build.sh >>"$OUT/lib.log" 2>&1 ); then
        for dir in "$ROOT"/examples/*/; do
            [ -f "$dir/prog.shm" ] || continue
            name=$(basename "$dir")
            work="$OUT/$name"; mkdir -p "$work"
            if "$SHC" "$dir/prog.shm" --with="$LIBDIR/libstats.a" -o "$work/prog" \
                   > "$work/compile" 2>&1 && "$work/prog" > "$work/out" 2>&1; then
                linked=$((linked + 1))
            else
                echo "FAIL $name: $(head -1 "$work/compile" 2>/dev/null)"
                fail=$((fail + 1))
            fi
        done
    else
        echo "  (the C library would not build here - the library examples are not checked)"
    fi
fi

echo
# The two counts are kept apart because they are different claims. A plain
# example is checked against the interpreter; one that calls a library cannot
# be - the app has no link step - so it is only compiled, linked and run.
if [ "$compare" = 1 ]; then
    echo "examples  $pass compiled, ran and matched the interpreter, $fail failed"
else
    echo "examples  $pass compiled and ran, $fail failed (no interpreter here to compare against)"
fi
[ "$linked" -gt 0 ] && echo "          $linked more linked a C library and ran (not comparable - see docs/CONFORMANCE.md 5)"
[ "$fail" = 0 ] || exit 1
