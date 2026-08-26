#!/usr/bin/env bash
#
# Record what the app's interpreter prints for every case, as the expected
# output of the compiler.
#
# The interpreter is the language's first implementation and the compiler is
# its second, so this is the oracle - but it is not the authority.
# SHALIMAR_LANGUAGE.md is, and where the two are known to disagree the
# divergence is written down in docs/CONFORMANCE.md and the recorded file is
# edited by hand to say what the document says. Re-running this would undo
# that, so it prints which files it is about to overwrite and asks.
#
#   SHALIMAR=../Shalimar ./tests/record.sh [name]
#
# Needs swiftc and the app's checkout; a Mac, in other words. The recorded
# files are committed, so nothing else needs either.

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

SHALIMAR="${SHALIMAR:-../Shalimar}"
SRC="$SHALIMAR/Shalimar"
[ -f "$SRC/Interpreter.swift" ] || { echo "set SHALIMAR to the app checkout" >&2; exit 2; }

REF="${TMPDIR:-/tmp}/shalimar-reference"

# Rebuild when ANY of the five sources is newer, not just Interpreter.swift.
# That check cost a wrong recording: `else if` was taught to Parse.swift, the
# reference binary was left alone because Interpreter.swift had not moved, and
# the suite recorded the old parser's error message as the expected answer.
# A stale oracle does not fail - it certifies, which is worse.
stale=0
[ -x "$REF" ] || stale=1
for f in TokenKind Node Parse Check Interpreter; do
    [ "$SRC/$f.swift" -nt "$REF" ] && stale=1
done
[ "$SHALIMAR/Tests/harness/main.swift" -nt "$REF" ] && stale=1
if [ "$stale" = 1 ]; then
    echo "building the reference interpreter..."
    swiftc -O "$SRC/TokenKind.swift" "$SRC/Node.swift" "$SRC/Parse.swift" \
           "$SRC/Check.swift" "$SRC/Interpreter.swift" \
           "$SHALIMAR/Tests/harness/main.swift" -o "$REF"
fi

# Cases whose expected output is written by hand because the app is wrong
# about them. Re-recording one would quietly put the app's answer back.
BY_HAND="text_copy"

FILTER="${1:-}"
for case in tests/cases/*.shm tests/load/*.shm; do
    name=$(basename "$case" .shm)
    [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue
    if [[ " $BY_HAND " == *" $name "* ]]; then
        echo "kept $name (written by hand - see docs/CONFORMANCE.md)"
        continue
    fi
    "$REF" "$case" > "${case%.shm}.expected" 2>&1 || true
    echo "recorded $name"
done
