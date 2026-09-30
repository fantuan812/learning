#!/usr/bin/env bash
# Profiling evidence runner.
# Toolchain used on the reference machine: MSYS2 MinGW-w64 g++ (C:\msys64\mingw64).
# Writes the raw, unedited program output into results/.
set -u

BASH_HERE="$(cd "$(dirname "$0")/.." && pwd)"
if [ -x /c/msys64/mingw64/bin/g++.exe ]; then
  export PATH="/c/msys64/mingw64/bin:$PATH"
fi
CXX="${CXX:-g++}"

cd "$BASH_HERE" || exit 1
mkdir -p build results

STAMP="$(date +%Y-%m-%dT%H:%M:%S%z)"
TARGET="$("$CXX" -dumpmachine 2>/dev/null || echo unknown)"
CORES="$(nproc 2>/dev/null || echo unknown)"
OSLINE="$(uname -s -m 2>/dev/null || echo unknown)"

status=0
for t in profiling_overhead hitch_and_budget; do
  if ! "$CXX" -std=c++17 -O2 -o "build/$t.exe" "src/$t.cpp"; then
    echo "compile failed: $t" >&2
    status=1
    continue
  fi
  {
    echo "# evidence   : labs/profiling/$t"
    echo "# generated  : $STAMP"
    echo "# host       : $OSLINE (cores=$CORES)"
    echo "# target     : $TARGET"
    echo "# compiler   : $("$CXX" --version | head -1)"
    echo "# command    : $CXX -std=c++17 -O2 -o build/$t.exe src/$t.cpp && ./build/$t.exe"
    echo "#"
    ./"build/$t.exe"
    echo "# exit_code  : $?"
  } > "results/$t.txt" 2>&1
  echo "wrote results/$t.txt"
done

exit $status
