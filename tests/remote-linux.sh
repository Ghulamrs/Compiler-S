#!/usr/bin/env bash
#
# The x86_64-linux suite, run on the Linux box.
#
# The Mac can assemble this target but cannot link or run it, so the whole
# tree goes over and the suite runs there natively. That also builds the
# compiler with real g++ - which is the only thing that can say whether the
# sources are ISO C++14, since Apple's libc++ hands you C++17 names under
# -std=c++14 and a Mac will therefore accept what the standard does not.
#
#   ./tests/remote-linux.sh [name]

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

KEY="${SHM_LINUX_KEY:-$HOME/Documents/Claude/myMorningWalk.pem}"
BOX="${SHM_LINUX_BOX:-ec2-user@52.202.164.123}"
DIR=shalimar

# **tar's status is checked and its stderr kept**, and this is not tidiness.
# bsdtar packs what it can read, skips what it cannot, writes a VALID archive
# and exits 1 - so a partial pack is not a truncated file that fails somewhere
# downstream, it is a smaller, perfectly good tarball that extracts cleanly:
#
#   tar -czf out.tgz good             rc=0  440 bytes
#   tar -czf out.tgz good nosuchdir   rc=1  440 bytes, extracts, holds good/
#
# If `src` comes up short the guard below catches it, because make then leaves
# no shc.exe. If only `tests/cases` or `tests/load` comes up short, NOTHING
# caught it: make succeeds, the guard passes, and run.sh globs whatever arrived
# and passes every case of a suite that is quietly smaller. Green against the
# wrong tree, which is the hazard CLAUDE.md names under Verification, reached
# here through tar rather than through a stale build script.
#
# The old archive is removed first for the neighbouring reason: the path is
# fixed, so a tar that fails outright would otherwise leave the PREVIOUS run's
# tarball sitting there to be relayed as though it were this one's.
TARBALL="${TMPDIR:-/tmp}/shm-src.tgz"
rm -f "$TARBALL"
# `--no-xattrs` as well as `--no-mac-metadata`. Now that tar's stderr is kept,
# the provenance xattr macOS stamps on files under ~/Documents would otherwise
# put 287 lines of `Ignoring unknown extended header keyword` on every run - and
# noise that reliably says nothing is how a message that says something gets
# missed. Not packing them is the cure; silencing tar again would put back the
# fault above.
tar --no-mac-metadata --no-xattrs -czf "$TARBALL" \
    src runtime tests/cases tests/load tests/cross tests/debug \
    tests/run.sh tests/cross.sh tests/debug.sh tests/linking.sh Makefile || {
    echo "the sources would not pack - see tar's complaint above" >&2; exit 2; }

# A count is the one thing a smaller-but-valid archive cannot fake, so it is
# what actually closes the hole: the far side is made to agree with this number
# before a single case runs. Counted from the recorded expectations rather than
# the .shm files, because a case without one is not run at either end.
CASES=$(ls tests/cases/*.expected tests/load/*.expected 2>/dev/null | wc -l | tr -d ' ')
[ "$CASES" -gt 0 ] || { echo "no recorded cases here to send" >&2; exit 2; }

# A relayed tree is not a checked-out one. Everything the tarball carries is
# removed first, and the build is done from clean - an object left behind from
# a previous relay is compiled against the previous headers, and the link
# succeeds because the mangled names still match. That is not hypothetical: it
# corrupted the heap here once, some way from anything that looked wrong.
ssh -n -i "$KEY" "$BOX" "rm -rf ~/$DIR/src ~/$DIR/tests ~/$DIR/obj ~/$DIR/lib ~/$DIR/shc.exe && mkdir -p ~/$DIR"
scp -q -i "$KEY" "${TMPDIR:-/tmp}/shm-src.tgz" "$BOX:~/$DIR/"
ssh -n -i "$KEY" "$BOX" "cd ~/$DIR && tar xzf shm-src.tgz && find . -name '._*' -delete && \
    there=\$(ls tests/cases/*.expected tests/load/*.expected 2>/dev/null | wc -l | tr -d ' ') ; \
    [ \"\$there\" -eq $CASES ] || { echo \"the relayed tree has \$there recorded cases and this one has $CASES - the pack was short\"; exit 2; } ; \
    chmod +x tests/run.sh && make 2>&1 | grep -E 'error|Error' ; \
    [ shc.exe -nt src/Parser.cpp ] || { echo 'shc.exe is older than its sources'; exit 2; } ; \
    chmod +x tests/cross.sh tests/debug.sh tests/linking.sh && ./tests/run.sh ${1:-} && \
    ./tests/cross.sh && ./tests/debug.sh && ./tests/linking.sh"
