#!/bin/sh
# Build this program against the C library in Compiler-C.
#
# The library is not built here - it belongs to the other repository. Build it
# there first:  cd ../../../Compiler-C/examples/shalimar-library && ./build.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
SHC="${SHC:-$here/../../shc.exe}"
LIB="${LIB:-$here/../../../Compiler-C/examples/shalimar-library/libstats.a}"
[ -x "$SHC" ] || { echo "no $SHC - build shc first, or set SHC="; exit 2; }
[ -f "$LIB"  ] || { echo "no $LIB - build it first:
  cd ../../../Compiler-C/examples/shalimar-library && ./build.sh"; exit 2; }
"$SHC" "$here/prog.shm" --with="$LIB" -o "$here/prog"
echo "built prog"
echo
"$here/prog"
