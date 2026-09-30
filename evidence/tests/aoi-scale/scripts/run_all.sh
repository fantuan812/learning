#!/usr/bin/env bash
# 匹配到对局证据包 runner。
# 参考机工具链：MSYS2 MinGW-w64 g++ (C:\msys64\mingw64)。
# 相对路径编译（原生 g++ 不认 /c/... 形式的路径），原始输出写入 results/。
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
for t in aoi_scale dynamic_shard; do
  if ! "$CXX" -std=c++17 -O2 -Wall -o "build/$t.exe" "src/$t.cpp"; then
    echo "compile failed: $t" >&2
    status=1
    continue
  fi
  {
    echo "# evidence   : tests/aoi-scale/$t"
    echo "# generated  : $STAMP"
    echo "# host       : $OSLINE (cores=$CORES)"
    echo "# target     : $TARGET"
    echo "# compiler   : $("$CXX" --version | head -1)"
    echo "# command    : $CXX -std=c++17 -O2 -Wall -o build/$t.exe src/$t.cpp && ./build/$t.exe"
    echo "#"
    ./"build/$t.exe"
    echo "# exit_code  : $?"
  } > "results/$t.txt" 2>&1
  echo "wrote results/$t.txt"
done

echo
echo "=== summary ==="
grep -H "^RESULT" results/*.txt || true

exit $status
