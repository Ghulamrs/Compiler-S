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

echo
if [ "$compare" = 1 ]; then
    echo "examples  $pass compiled, ran and matched the interpreter, $fail failed"
else
    echo "examples  $pass compiled and ran, $fail failed (no interpreter here to compare against)"
fi
[ "$fail" = 0 ] || exit 1
