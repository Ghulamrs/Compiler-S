#!/usr/bin/env bash
#
# The host suite: compile every case for this machine, run it, and compare
# with the output recorded beside it.
#
# The recorded output comes from the app's own interpreter (tests/record.sh),
# so a case that passes here says the compiler and the interpreter agree -
# which is the only claim worth making about a second implementation of a
# language that already has one.
#
#   ./tests/run.sh              every case
#   ./tests/run.sh gcd          cases whose name contains "gcd"

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

FILTER="${1:-}"
OUT=tests/out
mkdir -p "$OUT"

[ -x ./shc ] || { echo "no ./shc - run make first" >&2; exit 2; }

pass=0
fail=0
failed=()

for case in tests/cases/*.shm; do
    name=$(basename "$case" .shm)
    [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue

    expected="tests/cases/$name.expected"
    if [ ! -f "$expected" ]; then
        echo "SKIP $name (nothing recorded)"
        continue
    fi

    if ! ./shc "$case" -o "$OUT/$name" > "$OUT/$name.compile" 2>&1; then
        # A case may be a diagnostic case: the compiler's own output is then
        # what is compared, and there is no program to run.
        if diff -q "$expected" "$OUT/$name.compile" >/dev/null 2>&1; then
            pass=$((pass+1)); continue
        fi
        echo "FAIL $name (did not compile)"
        failed+=("$name"); fail=$((fail+1)); continue
    fi

    "$OUT/$name" > "$OUT/$name.actual" 2>&1
    if diff -u "$expected" "$OUT/$name.actual" > "$OUT/$name.diff" 2>&1; then
        pass=$((pass+1))
    else
        echo "FAIL $name"
        sed -n '1,12p' "$OUT/$name.diff"
        failed+=("$name"); fail=$((fail+1))
    fi
done

echo
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
