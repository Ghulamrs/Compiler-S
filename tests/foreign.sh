#!/bin/sh
# `uses <real> = f(...)` - a function this compiler never sees, provided by a
# library the link is given.
#
#   ./tests/foreign.sh
#   SHC=... CC1=... ./tests/foreign.sh
#
# **Not in tests/cases, and that is the point.** The differential suite asserts
# that shc and the app's interpreter agree. On a foreign declaration they do
# not and cannot: the app has no link step, so it refuses the program by name.
# That is docs/CONFORMANCE.md 5, a divergence to be stated rather than closed,
# and a suite built on agreement is the wrong place to state it.
#
# So this asks the questions that suite cannot:
#
#   the whole chain works    - C compiled, archived, linked by shc, right answer
#   the declaration is the contract - arity and types checked against it
#   a missing library is caught by shc, not left to the linker
#   the app refuses it, by name
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SHC="${SHC:-$ROOT/shc.exe}"
OUT="$ROOT/tests/out-foreign"
RT="$ROOT/runtime"

[ -x "$SHC" ] || { echo "foreign: no $SHC - run make first, or set SHC="; exit 2; }

rm -rf "$OUT" && mkdir -p "$OUT"
fail=0
bad() { echo "FAIL foreign: $1"; fail=$((fail + 1)); }

# ---- the library ------------------------------------------------------------
cat > "$OUT/mine.c" <<'C'
#include "shmrt.h"
double c_total(const ShmArray *a)
{
    int32_t n = shm_array_dim(a, 0), i;
    double  s = 0.0;
    for (i = 0; i < n; i++) s += shm_get_real(a, i);
    return s;
}
C

CC1="${CC1:-$ROOT/../Compiler-C/cc1.exe}"
if [ -x "$CC1" ]; then "$CC1" -c -I "$RT" "$OUT/mine.c" -o "$OUT/mine.o" > "$OUT/cc" 2>&1
else                   cc     -c -I "$RT" "$OUT/mine.c" -o "$OUT/mine.o" > "$OUT/cc" 2>&1; fi
[ -f "$OUT/mine.o" ] || { bad "could not compile the library's C"; sed -n '1,3p' "$OUT/cc"; echo; echo "foreign  $fail failed"; exit 1; }
ar rcs "$OUT/libmine.a" "$OUT/mine.o" 2>/dev/null || bad "ar would not archive it"

# ---- the program ------------------------------------------------------------
cat > "$OUT/prog.shm" <<'S'
uses <real> = c_total(a[]: real)

fun <> = main() {
  real v[4] : { 1.0, 2.0, 3.0, 4.5 }
  ? c_total(v)
}
S

# 1. the whole chain, with shc doing the link
if "$SHC" "$OUT/prog.shm" --with="$OUT/libmine.a" -o "$OUT/prog" > "$OUT/build" 2>&1; then
    answer=$("$OUT/prog" 2>&1 | tr -d ' \n')
    [ "$answer" = "10.5000000" ] || bad "the foreign call answered '$answer', wanted 10.5000000"
else
    bad "shc could not build and link the program"; sed -n '1,3p' "$OUT/build"
fi

# 2. the declaration is the contract
cat > "$OUT/arity.shm" <<'S'
uses <real> = c_total(a[]: real)
fun <> = main() {
  ? c_total()
}
S
"$SHC" "$OUT/arity.shm" --with="$OUT/libmine.a" -o "$OUT/x" > "$OUT/arity" 2>&1
grep -q "takes 1, got 0" "$OUT/arity" || bad "a wrong arity was not checked against the declaration"

cat > "$OUT/type.shm" <<'S'
uses <real> = c_total(a[]: real)
fun <> = main() {
  ? c_total(1)
}
S
"$SHC" "$OUT/type.shm" --with="$OUT/libmine.a" -o "$OUT/x" > "$OUT/type" 2>&1
grep -q "must be real\[\]" "$OUT/type" || bad "a wrong argument type was not checked against the declaration"

# 3. a missing library is this compiler's to report, not the linker's
"$SHC" "$OUT/prog.shm" -o "$OUT/nolib" > "$OUT/nolib.log" 2>&1
if grep -q "no$" "$OUT/nolib.log" || grep -q "library was named" "$OUT/nolib.log"; then
    :
else
    bad "a missing --with= was left to the linker to complain about"
    sed -n '1,3p' "$OUT/nolib.log"
fi

# 4. the app refuses it, by name. Only where an interpreter can be built.
SHALIMAR="${SHALIMAR:-$ROOT/../Shalimar}"
REF="${TMPDIR:-/tmp}/shalimar-reference"
if [ -f "$SHALIMAR/Shalimar/Interpreter.swift" ] && command -v swiftc >/dev/null 2>&1; then
    stale=0
    [ -x "$REF" ] || stale=1
    for f in TokenKind Node Parse Check Interpreter; do
        [ "$SHALIMAR/Shalimar/$f.swift" -nt "$REF" ] && stale=1
    done
    [ "$stale" = 1 ] && swiftc -O "$SHALIMAR/Shalimar/TokenKind.swift" "$SHALIMAR/Shalimar/Node.swift" \
        "$SHALIMAR/Shalimar/Parse.swift" "$SHALIMAR/Shalimar/Check.swift" \
        "$SHALIMAR/Shalimar/Interpreter.swift" "$SHALIMAR/Tests/harness/main.swift" \
        -o "$REF" >/dev/null 2>&1
    if [ -x "$REF" ]; then
        "$REF" "$OUT/prog.shm" > "$OUT/app" 2>&1
        grep -q "c_total" "$OUT/app" || bad "the app's refusal does not name the function"
        grep -q "cannot link" "$OUT/app" || bad "the app's refusal does not say why"
    fi
fi

echo
if [ "$fail" = 0 ]; then
    echo "foreign  the declaration links, checks, and is refused by the app as it should be"
else
    echo "foreign  $fail failed"
    exit 1
fi
