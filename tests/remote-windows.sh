#!/usr/bin/env bash
#
# The x86_64-windows suite: MASM assembled by ml64 and run natively.
#
# The compiler itself stays on this machine - it cross-compiles - and only
# the assembly travels. That is the honest test of the MASM backend: nothing
# on the far side reads the tree, so a case can only pass if what ml64 was
# handed was correct.
#
# Two habits are load-bearing on this box and both are learned rather than
# chosen: ssh lands in PowerShell, where '&&' is not a separator and nested
# quotes mangle, so the far side runs a .bat under cmd; and the runtime is
# built there once by cl rather than relayed.
#
#   ./tests/remote-windows.sh [name]

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

HOSTNAME_="${SHM_WINDOWS_HOST:-windows}"
REMOTE='C:\shalimar'
FILTER="${1:-}"

[ -x ./shc ] || { echo "no ./shc - run make first" >&2; exit 2; }

WORK="${TMPDIR:-/tmp}/shm-windows"
rm -rf "$WORK"; mkdir -p "$WORK"

# The runtime is rebuilt there every time. It is one file and a few seconds,
# and the alternative is a suite that silently tests the previous runtime
# against this compiler's calls - which fails as a link error if you are lucky
# and as a wrong answer if you are not.
scp -q runtime/*.cpp runtime/*.h "$HOSTNAME_:$REMOTE\\runtime\\"
ssh -n "$HOSTNAME_" "cmd /c $REMOTE\\setup.bat" | grep -q RUNTIME_BUILT || {
    echo "the runtime did not build on $HOSTNAME_" >&2; exit 2; }

# A case the compiler refuses has no assembly to send, and its diagnostics
# are the host compiler's work rather than the target's - those are covered
# by tests/run.sh. Only cases that produce a program travel.
# Two parallel arrays rather than one associative one: the bash macOS ships
# is 3.2, which has no 'declare -A'.
names=()
sources=()
skipped=0
for case in tests/cases/*.shm tests/load/*.shm; do
    name=$(basename "$case" .shm)
    [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue
    [ -f "${case%.shm}.expected" ] || continue
    if ! ./shc "$case" --target=x86_64-windows -S -o "$WORK/$name.asm" >/dev/null 2>&1; then
        skipped=$((skipped+1))
        continue
    fi
    names+=("$name")
    sources+=("$case")
done

[ ${#names[@]} -eq 0 ] && { echo "nothing to run"; exit 0; }

scp -q "$WORK"/*.asm "$HOSTNAME_:$REMOTE\\work\\"

pass=0; fail=0
for i in "${!names[@]}"; do
    name="${names[$i]}"
    case="${sources[$i]}"
    # The compiler's own output is produced here and the program's over
    # there, so they are joined in the order the recorded file has them.
    diagnostics=$(./shc "$case" --target=x86_64-windows -S \
                       -o "$WORK/$name.asm" 2>/dev/null)
    got=$(ssh -n "$HOSTNAME_" "cmd /c $REMOTE\\build.bat $name" 2>&1 | \
          sed -n '/---RUN---/,$p' | tail -n +2 | sed 's/\r$//')
    [ -n "$diagnostics" ] && got="$diagnostics
$got"
    want=$(cat "${case%.shm}.expected")
    if [ "$got" = "$want" ]; then
        pass=$((pass+1))
    else
        echo "FAIL $name"
        diff <(echo "$want") <(echo "$got") | sed -n '1,12p'
        fail=$((fail+1))
    fi
done

echo
echo "$pass passed, $fail failed, $skipped diagnostic-only cases not sent"
[ $fail -eq 0 ]
