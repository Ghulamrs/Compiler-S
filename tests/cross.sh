#!/usr/bin/env bash
#
# Finding the rest of the program in the other files beside it.
#
# Shalimar has no include and no import: a call to a function this file does
# not define is looked for in the project's other files, and what is found is
# compiled in. There is nothing here to declare and nothing to keep up to
# date, and a program becomes a library by renaming its main().
#
# These cases live apart from tests/cases because they are about several files
# at once, which the recorder and the two remote suites both assume there are
# not. The app's interpreter cannot run them at all - it has one file and no
# project - which is the cost written down in docs/CROSSFILE.md.
#
#   ./tests/cross.sh

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

[ -x ./shc ] || { echo "no ./shc - run make first" >&2; exit 2; }

WORK="${TMPDIR:-/tmp}/shm-cross"
rm -rf "$WORK"; mkdir -p "$WORK"

pass=0
fail=0

# `want` is matched against everything the compiler and the program said.
run() {
    local what="$1" file="$2" want="$3"
    local got
    got=$(./shc "$file" -o "$WORK/out" 2>&1; "$WORK/out" 2>&1)
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

refuse() {
    local what="$1" file="$2" want="$3"
    local got
    got=$(./shc "$file" -o "$WORK/out" 2>&1)
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

cp tests/cross/*.shl "$WORK/"

run "a function from another file"            "$WORK/together.shl" "12.0000000"
run "one it calls in turn, brought along"     "$WORK/together.shl" "5.0000000"
run "the global that function reads"          "$WORK/together.shl" "1"
run "a renamed main, called as a function"    "$WORK/together.shl" "geometry demo"
run "and the files used are said out loud"    "$WORK/together.shl" "also compiled geometry.shl"

# A second program in the directory is not a clash: nothing can call a
# function named main, so none is ever looked for.
cat > "$WORK/other.shl" <<'SHM'
fun <> = main() {
  ? "a program of its own"
}
SHM
run "another program beside it is not a clash" "$WORK/together.shl" "12.0000000"

# Two files answering to a name something wants is refused, naming both.
cat > "$WORK/rival.shl" <<'SHM'
fun <real> = area(w: real, h: real) {
  return 0.0
}
SHM
refuse "a wanted name in two files is refused" "$WORK/together.shl" \
       "'area' is in geometry.shl and rival.shl - it can be in one"
rm "$WORK/rival.shl"

# But a name nobody wants may be in as many files as it likes: a directory of
# programs may hold two helpers called rad() without either being wrong, and
# the app ships twelve programs in one directory.
cat > "$WORK/spare.shl" <<'SHM'
fun <real> = area(w: real, h: real) {
  return 0.0
}
fun <> = main() {
  ? area(1.0, 1.0)
}
SHM
refuse "a name two files define but nobody wants" "$WORK/spare.shl" ""
rm "$WORK/spare.shl"

# --no-search puts it back to one file.
got=$(./shc "$WORK/together.shl" --no-search -o "$WORK/out" 2>&1)
if [[ "$got" == *"Unknown function 'area'"* ]]; then pass=$((pass+1)); else
    echo "FAIL --no-search compiles the one file alone"; fail=$((fail+1)); fi

# Where an error is matters as much as which: a line number pointing into a
# file the reader is not looking at is worse than no line number.
cat > "$WORK/faulty.shl" <<'SHM'
fun <int> = wrong(n: int) {
  return n + missing
}
SHM
cat > "$WORK/asks.shl" <<'SHM'
fun <> = main() {
  ? wrong(1)
}
SHM
refuse "a compile error names the file it is in" "$WORK/asks.shl" \
       "Error: faulty.shl line 2: Undefined variable 'missing'"

cat > "$WORK/faulty.shl" <<'SHM'
fun <int> = wrong(n: int) {
  return n / 0
}
SHM
run "and so does a runtime one" "$WORK/asks.shl" \
    "Error: faulty.shl line 2: Division by zero"

echo
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
