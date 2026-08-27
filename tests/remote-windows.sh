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
# chosen: the far side runs a .bat, named by its full path and nothing else,
# because nested quotes mangle; and the runtime is built there once by cl
# rather than relayed.
#
# **Never `cmd /c <batch>`.** That was right while ssh landed in PowerShell,
# where a .bat needed cmd to run it at all. The box rebuilt on 2026-08-25
# defaults to cmd, so the prefix nested cmd inside cmd and a quote leaked into
# the batch's %1: `build.bat sort` arrived as `sort"`, every case failed its
# `if` on the first line, and the suite said 0 passed, 57 failed - a whole-suite
# failure that reads as a compiler fault and was a shell quoting one. Naming the
# .bat alone runs under either shell, so this no longer cares which the box has.
#
#   ./tests/remote-windows.sh [name]

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

HOSTNAME_="${SHM_WINDOWS_HOST:-windows}"
REMOTE='C:\shalimar'
FILTER="${1:-}"

[ -x ./shc.exe ] || { echo "no ./shc.exe - run make first" >&2; exit 2; }

WORK="${TMPDIR:-/tmp}/shm-windows"
rm -rf "$WORK"; mkdir -p "$WORK"

# The runtime is rebuilt there every time. It is one file and a few seconds,
# and the alternative is a suite that silently tests the previous runtime
# against this compiler's calls - which fails as a link error if you are lucky
# and as a wrong answer if you are not.
# The remote tree is made here rather than by hand once. It WAS made by hand
# once, and the rebuild of 2026-08-25 took it: the suite then failed with
# `scp: remote mkdir "C:\shalimar\runtime\": No such file or directory`, which
# reads as a network fault rather than as a missing scaffold. cmd's mkdir makes
# intermediate directories, and objects harmlessly if they already exist.
ssh -n "$HOSTNAME_" "mkdir $REMOTE\\runtime" >/dev/null 2>&1
ssh -n "$HOSTNAME_" "mkdir $REMOTE\\work"    >/dev/null 2>&1

# setup.bat and build.bat travel with the sources for the same reason. They used
# to live only on the box, which made the box a thing that had to be set up and
# remembered; now a freshly installed one needs nothing done to it.
scp -q tests/windows/setup.bat tests/windows/build.bat "$HOSTNAME_:$REMOTE\\"
scp -q runtime/*.cpp runtime/*.h "$HOSTNAME_:$REMOTE\\runtime\\"
ssh -n "$HOSTNAME_" "$REMOTE\\setup.bat" | grep -q RUNTIME_BUILT || {
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
    if ! ./shc.exe "$case" --target=x86_64-windows -S -o "$WORK/$name.asm" >/dev/null 2>&1; then
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
    diagnostics=$(./shc.exe "$case" --target=x86_64-windows -S \
                       -o "$WORK/$name.asm" 2>/dev/null)
    got=$(ssh -n "$HOSTNAME_" "$REMOTE\\build.bat $name" 2>&1 | \
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
