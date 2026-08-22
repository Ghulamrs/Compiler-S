#!/usr/bin/env bash
#
# Builds shc itself on the Windows box, with MSVC.
#
# This is a different thing from tests/remote-windows.sh and does not replace
# it. That one keeps the compiler here and sends only assembly, which is the
# honest test of the MASM backend: nothing on the far side reads the tree, so a
# case can only pass if what ml64 was handed was correct. Keep it.
#
# What this adds is shc.exe existing there at all, which is what a person or an
# editor on that machine needs in order to compile Shalimar without a Mac in
# the room. ../RStudio skipped every Shalimar case on that box for want of it.
#
# It also builds with cl, which is the third of the three toolchains this
# project claims to be ISO C++14 under, and the only one nothing was checking.
#
#   ./tests/build-windows.sh              build shc.exe and the runtimes
#   ./tests/build-windows.sh examples     and compile and run every example
#   ./tests/build-windows.sh clean        remove what a build leaves there
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

HOSTNAME_="${SHM_WINDOWS_HOST:-windows}"
REMOTE="${SHM_WINDOWS_SRC:-Compiler-S}"
WHAT="${1:-build}"

# ssh lands in PowerShell on that box, where '&&' is not a statement separator
# and nested quotes mangle - so the far side runs the .bat under cmd, which is
# the same habit tests/remote-windows.sh keeps and for the same reason.
say() { printf '%s\n' "$*"; }

if [ "$WHAT" = clean ]; then
    ssh -n "$HOSTNAME_" "cd $REMOTE; cmd /c build.bat clean"
    exit $?
fi

say "copying to $HOSTNAME_:$REMOTE"
ssh -n "$HOSTNAME_" "powershell -NoProfile -Command \"New-Item -ItemType Directory -Force -Path '\$HOME\\$REMOTE\\src\\backend','\$HOME\\$REMOTE\\runtime','\$HOME\\$REMOTE\\examples','\$HOME\\$REMOTE\\docs' | Out-Null\"" || exit 2

# The build scripts and the sources both, every time. A stale build.bat on that
# box has silently reported an old, smaller suite for this family of project
# before now - see ../RStudio's own relay, which learned it the hard way.
scp -q src/*.cpp src/*.h "$HOSTNAME_:$REMOTE/src/" || exit 2
scp -q src/backend/*.cpp src/backend/*.h "$HOSTNAME_:$REMOTE/src/backend/" || exit 2
scp -q runtime/*.cpp runtime/*.h "$HOSTNAME_:$REMOTE/runtime/" || exit 2
scp -q examples/*.shm "$HOSTNAME_:$REMOTE/examples/" || exit 2
scp -q build.bat README.md CLAUDE.md "$HOSTNAME_:$REMOTE/" || exit 2
scp -q docs/*.md "$HOSTNAME_:$REMOTE/docs/" 2>/dev/null

say "build.bat $WHAT"
ssh -n "$HOSTNAME_" "cd $REMOTE; cmd /c build.bat $WHAT"
