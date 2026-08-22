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

    # The compiler's own output comes first and the program's after it, which
    # is the order the app's interpreter produces them in: it reports what the
    # checker found and then runs. A warning therefore belongs in the recorded
    # file above the program's first line, and a case that does not compile is
    # compared on the diagnostics alone.
    ./shc "$case" -o "$OUT/$name" > "$OUT/$name.compile" 2>/dev/null
    compiled=$?
    cp "$OUT/$name.compile" "$OUT/$name.actual"
    if [ $compiled -ne 0 ]; then
        if diff -u "$expected" "$OUT/$name.actual" > "$OUT/$name.diff" 2>&1; then
            pass=$((pass+1)); continue
        fi
        echo "FAIL $name (did not compile)"
        sed -n '1,12p' "$OUT/$name.diff"
        failed+=("$name"); fail=$((fail+1)); continue
    fi

    "$OUT/$name" >> "$OUT/$name.actual" 2>&1
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
