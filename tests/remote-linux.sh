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

tar --no-mac-metadata -czf "${TMPDIR:-/tmp}/shm-src.tgz" \
    src runtime tests/cases tests/run.sh Makefile 2>/dev/null

ssh -n -i "$KEY" "$BOX" "rm -rf ~/$DIR/src ~/$DIR/tests && mkdir -p ~/$DIR"
scp -q -i "$KEY" "${TMPDIR:-/tmp}/shm-src.tgz" "$BOX:~/$DIR/"
ssh -n -i "$KEY" "$BOX" "cd ~/$DIR && tar xzf shm-src.tgz 2>/dev/null && find . -name '._*' -delete && \
    chmod +x tests/run.sh && make 2>&1 | grep -E 'error|Error' ; \
    [ shc -nt src/Parser.cpp ] || { echo 'shc is older than its sources'; exit 2; } ; \
    ./tests/run.sh ${1:-}"
