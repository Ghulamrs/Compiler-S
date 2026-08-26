#!/bin/sh
# The array ABI: can a C function still be handed a Shalimar array?
#
#   ./tests/abi.sh
#   SHC=... CC1=... ./tests/abi.sh
#
# docs/ARRAY-ABI.md is what this checks. Three questions, in the order they
# actually broke:
#
#   1. Does a C file COMPILE against runtime/shmrt.h? That header asked for
#      <stdint.h> until 2026-08-26, which is C99, so cc1 - the C89 compiler
#      written alongside this runtime - could not read its own project's
#      header. Nobody had tried. This is the check that would have said so.
#
#   2. Does the runtime archive still EXPORT everything the document names? A
#      rename here is invisible to every other suite and breaks somebody's
#      build rather than ours.
#
#   3. Does the whole chain still WORK - archive, link beside a Shalimar
#      object, run, and give the answer the Shalimar side would have given?
#
# **cc1 is preferred over the host compiler on purpose.** It is the compiler a
# user of these tools already has, and it is stricter: C89, and its own headers.
# The host compiler would have accepted the broken header without a murmur.
#
# The call is reached by pointing an emitted call at the C function, which a
# test may do and a program may not - `uses` cannot name a user's function.
# When that changes this file should be the first thing rewritten.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SHC="${SHC:-$ROOT/shc.exe}"
OUT="$ROOT/tests/out-abi"
RT="$ROOT/runtime"

[ -x "$SHC" ] || { echo "abi: no $SHC - run make first, or set SHC="; exit 2; }

# The runtime archive this shc will link. LIBDIR follows BINDIR, so it sits
# beside the compiler wherever that was put.
LIBDIR=$(dirname "$SHC")/lib
case "$(uname -s)" in
    Darwin) TARGET=arm64-darwin ;;
    *)      TARGET=x86_64-linux ;;
esac
ARCHIVE="$LIBDIR/shmrt-$TARGET.a"
[ -f "$ARCHIVE" ] || { echo "abi: no $ARCHIVE - build the runtime first"; exit 2; }

rm -rf "$OUT" && mkdir -p "$OUT"

fail=0
note() { echo "  $1"; }
bad()  { echo "FAIL abi: $1"; fail=$((fail + 1)); }

# ---- 2. the exported surface ------------------------------------------------
#
# Named one at a time rather than counted, so the message says which one went.
for symbol in shm_array_dim shm_array_make shm_get_int shm_get_real \
              shm_get_char shm_get_ref shm_set_int shm_set_real \
              shm_set_char shm_set_ref; do
    if ! nm -g "$ARCHIVE" 2>/dev/null | grep -q "[ 	]_\{0,1\}$symbol\$"; then
        bad "the runtime archive no longer exports $symbol (docs/ARRAY-ABI.md names it)"
    fi
done

# ---- 1. the header, read by a C compiler ------------------------------------
cat > "$OUT/mine.c" <<'C'
#include "shmrt.h"

/* Rank 1: elements are doubles, read directly. */
double c_total(const ShmArray *a)
{
    int32_t n = shm_array_dim(a, 0);
    double  sum = 0.0;
    int32_t i;
    for (i = 0; i < n; i++) sum += shm_get_real(a, i);
    return sum;
}
C

CC1="${CC1:-$ROOT/../Compiler-C/cc1.exe}"
if [ -x "$CC1" ]; then
    compiler="cc1"
    "$CC1" -c -I "$RT" "$OUT/mine.c" -o "$OUT/mine.o" > "$OUT/compile" 2>&1
else
    compiler="the host cc"
    cc -c -I "$RT" "$OUT/mine.c" -o "$OUT/mine.o" > "$OUT/compile" 2>&1
fi
if [ ! -f "$OUT/mine.o" ]; then
    bad "$compiler could not compile against runtime/shmrt.h"
    sed -n '1,4p' "$OUT/compile"
    echo
    echo "abi  $fail failed"
    exit 1
fi
note "$compiler compiled a C file against runtime/shmrt.h"

# ---- 3. archive, link, run --------------------------------------------------
ar rcs "$OUT/libmine.a" "$OUT/mine.o" 2>/dev/null || bad "ar would not archive the object"

cat > "$OUT/prog.shm" <<'S'
uses len

fun <real> = total(a[]: real) {
  real s : 0.0
  for i < len(a) {
    s : s + a[i]
  }
  return s
}

fun <> = main() {
  real v[4] : { 1.0, 2.0, 3.0, 4.5 }
  ? total(v)
}
S

# What the Shalimar side answers on its own, which is what the C must match.
"$SHC" "$OUT/prog.shm" -o "$OUT/pure" > "$OUT/build" 2>&1 || bad "shc would not build the program"
[ -x "$OUT/pure" ] && "$OUT/pure" > "$OUT/expected" 2>&1

# The same program with the call pointed at the C function. Matched on the
# mnemonic AND the callee, so the definition's own label is left alone - and
# without assuming the leading underscore, which Mach-O has and ELF does not.
"$SHC" -S "$OUT/prog.shm" -o "$OUT/prog.s" >> "$OUT/build" 2>&1
awk '/(^|[ \t])(bl|call)[ \t]+_?shmf_total[ \t]*$/ { sub(/shmf_total/, "c_total") } { print }' \
    "$OUT/prog.s" > "$OUT/patched.s"
if ! grep -q "c_total" "$OUT/patched.s"; then
    bad "could not point the emitted call at the C function - has the call spelling changed?"
else
    c++ -c "$OUT/patched.s" -o "$OUT/patched.o" >> "$OUT/build" 2>&1 || bad "the patched assembly would not assemble"
    if c++ -o "$OUT/mixed" "$OUT/patched.o" "$OUT/libmine.a" "$ARCHIVE" -lm >> "$OUT/build" 2>&1; then
        "$OUT/mixed" > "$OUT/actual" 2>&1
        if cmp -s "$OUT/actual" "$OUT/expected"; then
            note "a C function read the Shalimar array and gave the same answer"
        else
            bad "the C function disagreed with the Shalimar one"
            echo "    Shalimar: $(cat "$OUT/expected")"
            echo "    C:        $(cat "$OUT/actual")"
        fi
    else
        bad "the C object would not link beside a Shalimar object"
        sed -n '$p' "$OUT/build"
    fi
fi

echo
if [ "$fail" = 0 ]; then
    echo "abi  the array ABI holds: header, exports, link and answer"
else
    echo "abi  $fail failed"
    exit 1
fi
