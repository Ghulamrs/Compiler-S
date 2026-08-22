#!/usr/bin/env bash
#
# What a Shalimar object is, and why it is not a piece of something larger.
#
# The editor in ../RStudio grew a compiler per group so that a project can hold
# C and C++ together, and the obvious next question was whether Shalimar could
# be a third group. It cannot, and the reason is not the editor's and not a
# missing feature - it is what shc's output *is*. This suite is that answer,
# checked rather than asserted, so that a change to the compiler that made any
# of it untrue would be caught here rather than believed in docs/LINKING.md
# forever.
#
#   ./tests/linking.sh

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

[ -x ./shc.exe ] || { echo "no ./shc.exe - run make first" >&2; exit 2; }

WORK="${TMPDIR:-/tmp}/shm-linking"
rm -rf "$WORK"; mkdir -p "$WORK"

pass=0
fail=0

ok() {
    local what="$1"; shift
    if "$@" > "$WORK/said" 2>&1; then
        pass=$((pass+1))
    else
        echo "FAIL $what"
        sed 's/^/    /' "$WORK/said"
        fail=$((fail+1))
    fi
}

saw() {
    local what="$1" want="$2" got="$3"
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

# Only the host can be assembled, so this is a host-only suite - which costs
# nothing, because every fact in it is about symbol names and those are the
# same on all three targets.

cat > "$WORK/a.shm" <<'EOF'
fun <> = main() {
  ? 1
}
EOF

cat > "$WORK/b.shm" <<'EOF'
fun <> = main() {
  ? 2
}
EOF

cat > "$WORK/lib.shm" <<'EOF'
real scale : 3.0

fun <real> = twice(x: real) {
  return x * scale
}

fun <> = main() {
  ? twice(2.5)
}
EOF

echo "what -o names"

# -o names the object, exactly as it names the assembly under -S. It used to
# have .o appended to whatever was given, so `-c -o f.o` wrote f.o.o - a
# nuisance by hand and fatal to anything that has to name the object again in
# order to link it.
ok "-c honours -o as given" ./shc.exe "$WORK/a.shm" -c -o "$WORK/named.o"
if [ -f "$WORK/named.o" ]; then
    pass=$((pass+1))
else
    echo "FAIL the object lands where -o said"
    fail=$((fail+1))
fi
if [ -f "$WORK/named.o.o" ]; then
    echo "FAIL -o had .o put on the end of it again"
    fail=$((fail+1))
else
    pass=$((pass+1))
fi

# And without -o the object is the input's name with .o, which is what cc -c
# does and what anything driving the compiler will expect.
( cd "$WORK" && "$OLDPWD/shc.exe" a.shm -c > /dev/null 2>&1 )
if [ -f "$WORK/a.o" ]; then
    pass=$((pass+1))
else
    echo "FAIL without -o the object is named after the input"
    fail=$((fail+1))
fi

echo "what one object exports"

./shc.exe "$WORK/lib.shm" -c -o "$WORK/lib.o" > /dev/null 2>&1
symbols=$(nm -g "$WORK/lib.o" 2>/dev/null || echo "no nm")

# A user function is external and could be called from C - for a scalar the
# calling convention is the machine's own, a double in and a double out. That
# is the part that looks encouraging, and it is not the part that decides.
saw "a user function is an external symbol" "shmf_twice" "$symbols"

# These three are what decide. Every unit exports the same three names,
# whichever file it came from: the program's entry, the globals of that file,
# and the table of file names a diagnostic uses.
saw "every unit exports shm_user_main" "shm_user_main" "$symbols"
saw "every unit exports shm_init_globals" "shm_init_globals" "$symbols"
saw "every unit exports shm_name_files" "shm_name_files" "$symbols"

echo "two units in one program"

./shc.exe "$WORK/a.shm" -c -o "$WORK/a.o" > /dev/null 2>&1
./shc.exe "$WORK/b.shm" -c -o "$WORK/b.o" > /dev/null 2>&1
runtime=$(ls lib/shmrt-*.a 2>/dev/null | grep -v -- -debug | head -1)
two=$(c++ -o "$WORK/two" "$WORK/a.o" "$WORK/b.o" "$runtime" 2>&1)

# Not "does not work yet" - cannot. The three names above collide by
# construction, so two Shalimar objects in one program is a link error and
# always will be until those names carry their unit.
saw "two Shalimar objects collide on the entry point" "shm_user_main" "$two"
if [ -f "$WORK/two" ]; then
    echo "FAIL two Shalimar objects linked, which docs/LINKING.md says they cannot"
    fail=$((fail+1))
else
    pass=$((pass+1))
fi

echo "a C program calling into Shalimar"

cat > "$WORK/caller.c" <<'EOF'
#include <stdio.h>
double shmf_twice(double);
int main(void) { printf("%f\n", shmf_twice(2.5)); return 0; }
EOF

cc -c "$WORK/caller.c" -o "$WORK/caller.o" > /dev/null 2>&1
mixed=$(c++ -o "$WORK/mixed" "$WORK/caller.o" "$WORK/lib.o" "$runtime" 2>&1)

# The runtime archive owns main - it is what sets the globals up, names the
# files and calls shm_user_main. So a C program that has its own main cannot
# link the runtime at all, and a Shalimar object without the runtime has
# nothing to call. There is no order of the two that works.
saw "the runtime owns main, so a C main collides with it" "main" "$mixed"
if [ -f "$WORK/mixed" ]; then
    echo "FAIL a C main linked against the Shalimar runtime"
    fail=$((fail+1))
else
    pass=$((pass+1))
fi

echo
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
