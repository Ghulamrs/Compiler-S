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
# `source/Compiler-S`, not `Compiler-S`. The box keeps the four projects as
# SIBLINGS under ~/source - RStudio, Compiler-C, Compiler-S, Converter-C2S -
# because RStudio.sln names ..\Compiler-C\msvc\cc1.vcxproj and friends. A
# default of plain "Compiler-S" put a SECOND tree in the home directory, which
# then aged apart from the one every other tool on that box reads: a build that
# passes against a tree nobody else uses is the stale-artefact hazard CLAUDE.md
# warns about, wearing a green tick.
#
# Forward slashes here because scp wants them; ssh gets the backslash form,
# since cmd reads a leading / as a switch rather than a separator.
REMOTE="${SHM_WINDOWS_SRC:-source/Compiler-S}"
REMOTE_WIN="${REMOTE//\//\\}"
WHAT="${1:-build}"

# The far side runs a .bat named by its path and nothing else - no `cmd /c`,
# no `cd X;`, no nested quotes - which is the habit tests/remote-windows.sh
# keeps and for the same reason. This script used to assume ssh landed in
# PowerShell: it made its directories with a quoted New-Item, and started the
# build with `cd DIR; cmd /c build.bat`. The box rebuilt on 2026-08-25 defaults
# to cmd, where `;` is not a separator and those quotes collapse, so the first
# scp failed with "remote mkdir: No such file or directory" and the build never
# started. What is here now works under either shell.
say() { printf '%s\n' "$*"; }

if [ "$WHAT" = clean ]; then
    ssh -n "$HOSTNAME_" "$REMOTE_WIN\\msvcbuild.bat clean"
    exit $?
fi

say "copying to $HOSTNAME_:$REMOTE"
# One directory to a command, and failure ignored: cmd's mkdir makes the
# intermediates and objects harmlessly when the path is already there. ssh
# starts in the home directory on both shells, so these are relative to it -
# the same place the scp lines below land.
for d in "src\\backend" "runtime" "examples" "docs"; do
    ssh -n "$HOSTNAME_" "mkdir $REMOTE_WIN\\$d" >/dev/null 2>&1
done

# The build scripts and the sources both, every time. A stale build.bat on that
# box has silently reported an old, smaller suite for this family of project
# before now - see ../RStudio's own relay, which learned it the hard way.
scp -q src/*.cpp src/*.h "$HOSTNAME_:$REMOTE/src/" || exit 2
scp -q src/backend/*.cpp src/backend/*.h "$HOSTNAME_:$REMOTE/src/backend/" || exit 2
scp -q runtime/*.cpp runtime/*.h "$HOSTNAME_:$REMOTE/runtime/" || exit 2
scp -q examples/*.shm "$HOSTNAME_:$REMOTE/examples/" || exit 2
scp -q build.bat README.md CLAUDE.md "$HOSTNAME_:$REMOTE/" || exit 2
scp -q docs/*.md "$HOSTNAME_:$REMOTE/docs/" 2>/dev/null

scp -q tests/windows/msvcbuild.bat "$HOSTNAME_:$REMOTE/" || exit 2

say "build.bat $WHAT"
ssh -n "$HOSTNAME_" "$REMOTE_WIN\\msvcbuild.bat $WHAT"
