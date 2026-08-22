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

names=()
for case in tests/cases/*.shm; do
    name=$(basename "$case" .shm)
    [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue
    [ -f "tests/cases/$name.expected" ] || continue
    ./shc "$case" --target=x86_64-windows -S -o "$WORK/$name.asm" || continue
    names+=("$name")
done

[ ${#names[@]} -eq 0 ] && { echo "nothing to run"; exit 0; }

scp -q "$WORK"/*.asm "$HOSTNAME_:$REMOTE\\work\\"

pass=0; fail=0
for name in "${names[@]}"; do
    got=$(ssh -n "$HOSTNAME_" "cmd /c $REMOTE\\build.bat $name" 2>&1 | \
          sed -n '/---RUN---/,$p' | tail -n +2 | sed 's/\r$//')
    want=$(cat "tests/cases/$name.expected")
    if [ "$got" = "$want" ]; then
        pass=$((pass+1))
    else
        echo "FAIL $name"
        diff <(echo "$want") <(echo "$got") | sed -n '1,12p'
        fail=$((fail+1))
    fi
done

echo
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
