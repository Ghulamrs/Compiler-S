#!/usr/bin/env bash
#
# The compact spelling of a double, against Swift's own conversion.
#
# The runtime prints a real to a fixed number of places, except past 1e15 and
# for the non-finite ones, where it uses the shortest decimal that reads back
# as the same double. That spelling has to be the app's, or the two
# implementations disagree about what a number looks like - and the boundary
# cases are not guessable: 1e15 is 1000000000000000.0, 1e16 is 1e+16, and a
# single digit takes no point at all in exponent form.
#
# Needs swiftc, so it runs on a Mac and nowhere else. It is not part of the
# suites for that reason; run it when runtime/Shortest.cpp changes.

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

WORK="${TMPDIR:-/tmp}/shm-shortest"
mkdir -p "$WORK"

VALUES='1e15, 1.5e15, 1e16, 1.5e16, 1e17, 2e16, 1e21, 1e300, -1e300,
        1234567890123456.0, 9.007199254740992e15, 1.2345e18,
        3.14159265358979e15, 1.0/0.0, -1.0/0.0'

cat > "$WORK/probe.cpp" <<CPP
#include "Shortest.h"
#include <cmath>
#include <cstdio>
int main() {
    const double vs[] = {$VALUES, std::nan("")};
    for (double v : vs) std::printf("%s\n", shm::Shortest::of(v).c_str());
}
CPP

c++ -std=c++14 -o "$WORK/probe" "$WORK/probe.cpp" runtime/Shortest.cpp \
    -I "$PWD/runtime"
"$WORK/probe" > "$WORK/cpp.txt"

cat > "$WORK/probe.swift" <<SWIFT
let vs: [Double] = [$VALUES, Double.nan]
for v in vs { print("\(v)") }
SWIFT
swift "$WORK/probe.swift" > "$WORK/swift.txt"

if diff -u "$WORK/swift.txt" "$WORK/cpp.txt"; then
    echo "$(wc -l < "$WORK/cpp.txt" | tr -d ' ') values agree with Swift"
else
    echo "the compact spelling has drifted from the app's" >&2
    exit 1
fi
