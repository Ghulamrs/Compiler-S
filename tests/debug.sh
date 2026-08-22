#!/usr/bin/env bash
#
# Stopping a Shalimar program, from inside it.
#
# There is no debug information here and there is not going to be any. What
# the program has instead is shm_line(unit, line) before every statement,
# which exists so a runtime error can name where it happened - and which is
# exactly what a debugger needs. These cases drive that, by writing commands
# to a program's standard input and reading what it says back on standard
# error.
#
# The boundary being checked as much as the stepping: a release build has no
# code for any of this, and what the compiler emits is the same either way.
# See docs/DEBUGGING.md.
#
#   ./tests/debug.sh

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

[ -x ./shc.exe ] || { echo "no ./shc.exe - run make first" >&2; exit 2; }

WORK="${TMPDIR:-/tmp}/shm-debug"
rm -rf "$WORK"; mkdir -p "$WORK"

pass=0
fail=0

check() {
    local what="$1" got="$2" want="$3"
    if [ "$got" = "$want" ]; then
        pass=$((pass+1))
    else
        echo "FAIL $what"
        echo "  wanted: $want"
        echo "  got:    $got"
        fail=$((fail+1))
    fi
}

contains() {
    local what="$1" got="$2" want="$3"
    if [[ "$got" == *"$want"* ]]; then
        pass=$((pass+1))
    else
        echo "FAIL $what"
        echo "  wanted to see: $want"
        echo "  got:"
        echo "$got" | sed 's/^/    /'
        fail=$((fail+1))
    fi
}

# Only what the session said, with the program's own printing left out. The
# '#file' lines are dropped: they are checked once, on their own, and would
# otherwise be the first two words of every expectation below.
session() {
    printf '%b' "$2" | SHM_DEBUG=1 "$1" 2>&1 >/dev/null |
        grep -v '^#file ' | tr '\n' ' ' | sed 's/[[:space:]]*$//'
}

# Only what the program printed. A print leaves a trailing space before its
# newline, so the tail is trimmed rather than compared.
printed() {
    printf '%b' "$2" | SHM_DEBUG=1 "$1" 2>/dev/null | tr '\n' ' ' |
        sed 's/[[:space:]]*$//'
}

cp tests/debug/steps.shl "$WORK/steps.shl"

# ---- the boundary ---------------------------------------------------------

./shc.exe "$WORK/steps.shl" -S -o "$WORK/release.s" >/dev/null 2>&1
./shc.exe "$WORK/steps.shl" --debug -S -o "$WORK/debug.s" >/dev/null 2>&1
if diff -q "$WORK/release.s" "$WORK/debug.s" >/dev/null; then
    pass=$((pass+1))
else
    echo "FAIL what the compiler emits is the same in both"
    fail=$((fail+1))
fi

./shc.exe "$WORK/steps.shl" -o "$WORK/release" >/dev/null 2>&1
./shc.exe "$WORK/steps.shl" --debug -o "$WORK/debug" >/dev/null 2>&1

# A release build has no code for any of this, so arming it does nothing.
check "a release build ignores being armed" \
      "$(printed "$WORK/release" 'c\n')" "1 2  4"
check "and says nothing on the channel" \
      "$(session "$WORK/release" 'c\n')" ""

# A debug build not armed is a program like any other.
check "a debug build run normally is just a program" \
      "$(SHM_DEBUG= "$WORK/debug" | tr '\n' ' ' | sed 's/[[:space:]]*$//')" "1 2  4"

# ---- stopping and walking -------------------------------------------------

check "it says it is ready before the first statement" \
      "$(session "$WORK/debug" 'c\n')" "#ready #exit 0"

# Which number a file has depends on which files the compiler was given, and
# nobody outside the compiler can work that out - so the program says, before
# it can be asked for a breakpoint in one.
check "it names its files before saying it is ready" \
      "$(printf 'c\n' | SHM_DEBUG=1 "$WORK/debug" 2>&1 >/dev/null | head -1)" \
      "#file 0 steps.shl"

check "a breakpoint stops it, with the depth it is at" \
      "$(session "$WORK/debug" 'b 0 9\nc\nc\n')" "#ready #stop 0 9 1 #exit 0"

check "one that was set and cleared does not" \
      "$(session "$WORK/debug" 'b 0 9\nd 0 9\nc\n')" "#ready #exit 0"

check "'w' answers where without moving" \
      "$(session "$WORK/debug" 'b 0 9\nc\nw\nc\n')" \
      "#ready #stop 0 9 1 #at 0 9 1 #exit 0"

check "'s' steps into a call, which is what makes the depth rise" \
      "$(session "$WORK/debug" 's\ns\ns\nc\n')" \
      "#ready #stop 0 7 1 #stop 0 8 1 #stop 0 2 2 #exit 0"

check "'n' stays at this depth and comes back out to it" \
      "$(session "$WORK/debug" 's\ns\ns\nn\nn\nc\n')" \
      "#ready #stop 0 7 1 #stop 0 8 1 #stop 0 2 2 #stop 0 3 2 #stop 0 9 1 #exit 0"

check "'o' leaves the call it is in" \
      "$(session "$WORK/debug" 's\ns\ns\no\nc\n')" \
      "#ready #stop 0 7 1 #stop 0 8 1 #stop 0 2 2 #stop 0 9 1 #exit 0"

check "'q' ends it there and then" \
      "$(session "$WORK/debug" 'b 0 9\nc\nq\n')" \
      "#ready #stop 0 9 1 #exit 130"

# The other end going away must not leave a program stopped for ever on a
# machine nobody is watching. It stops being a debug session at that point and
# becomes a program, which is why nothing more is said.
check "the channel closing lets it run to the end" \
      "$(session "$WORK/debug" '')" "#ready"
check "and it still prints what it was going to print" \
      "$(printed "$WORK/debug" '')" "1 2  4"

# ---- what the program printed ---------------------------------------------

# Output has to arrive before the stop that follows it. Standard output is
# buffered and standard error is not, so without a flush the whole of a
# program's printing turns up after the run - and a debugger that shows you
# the output only when it is over is showing you nothing you stopped for.
both=$(printf 'b 0 10\nc\nc\n' | SHM_DEBUG=1 "$WORK/debug" 2>&1 | tr '\n' '|')
contains "the printing arrives before the stop that follows it" \
         "$both" "1 2 |#stop 0 10 1"

# ---- a failure is an ending too -------------------------------------------

cat > "$WORK/bad.shl" <<'SHM'
fun <> = main() {
  ? 1 / 0
}
SHM
./shc.exe "$WORK/bad.shl" --debug -o "$WORK/bad" >/dev/null 2>&1
contains "a program that fails says so on the channel" \
         "$(session "$WORK/bad" 'c\n')" "#exit 1"

# ---- a second file -------------------------------------------------------

# The unit numbering a breakpoint uses is the numbering a diagnostic uses, so
# a breakpoint and an error name the same file the same way.
cat > "$WORK/helper.shl" <<'SHM'
fun <int> = thrice(n: int) {
  return n + n + n
}
SHM
cat > "$WORK/uses.shl" <<'SHM'
fun <> = main() {
  ? thrice(2)
}
SHM
# Named rather than searched for, so the numbering is this line's and not the
# directory listing's - which is exactly what the editor does.
./shc.exe "$WORK/uses.shl" "$WORK/helper.shl" --debug -o "$WORK/uses" >/dev/null 2>&1
check "the second file is named as unit 1" \
      "$(printf 'c\n' | SHM_DEBUG=1 "$WORK/uses" 2>&1 >/dev/null | sed -n '2p')" \
      "#file 1 helper.shl"
check "and a breakpoint in it stops the program there" \
      "$(session "$WORK/uses" 'b 1 2\nc\nc\n')" "#ready #stop 1 2 2 #exit 0"

echo
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
