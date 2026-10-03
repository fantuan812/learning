#!/usr/bin/env bash
# No installation/network access; dependencies are supplied explicitly.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON="${PYTHON:-python3}"
: "${PROTOC:?Set PROTOC to the protoc 36.0 executable}"
: "${PROTOBUF_PYTHON_ROOT:?Set PROTOBUF_PYTHON_ROOT to the extracted protobuf 7.36.0 pure-Python wheel}"
PROTOC_VERSION="$("$PROTOC" --version)"
[[ "$PROTOC_VERSION" == 'libprotoc 36.0' ]] || { echo "Expected libprotoc 36.0, got $PROTOC_VERSION" >&2; exit 1; }
mkdir -p "$HERE/build/generated"
"$PROTOC" --proto_path="$HERE/src" --python_out="$HERE/build/generated" old.proto new.proto
cmp "$HERE/src/generated/old_pb2.py" "$HERE/build/generated/old_pb2.py"
cmp "$HERE/src/generated/new_pb2.py" "$HERE/build/generated/new_pb2.py"
# Process-scoped import path/backend; do not alter system Python or shell PATH.
export PYTHONPATH="$HERE/build/generated:$PROTOBUF_PYTHON_ROOT"
export PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python
export PYTHONDONTWRITEBYTECODE=1
"$PYTHON" -s -c 'import sys, google.protobuf; from google.protobuf.internal import api_implementation; sys.exit(0 if sys.version_info >= (3, 10) and google.protobuf.__version__ == "7.36.0" and api_implementation.Type() == "python" else "Requires Python 3.10+, protobuf 7.36.0, python backend")'
LOG="${RESULT_LOG:-$HERE/build/python_linux.txt}"
{
  echo '# evidence: protobuf-evolution (two schemas; one Python runtime)'
  echo "# generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# host: $(uname -s -m)"
  echo "# compiler: $PROTOC_VERSION"
  "$PYTHON" -s -c 'import platform,google.protobuf; from google.protobuf.internal import api_implementation; print("# python: " + platform.python_version()); print("# protobuf: " + google.protobuf.__version__); print("# backend: " + api_implementation.Type())'
  "$PYTHON" -s - "$HERE" <<'PY'
import hashlib
import pathlib
import sys
root = pathlib.Path(sys.argv[1])
for relative in ("src/old.proto", "src/new.proto", "src/generated/old_pb2.py", "src/generated/new_pb2.py", "tests/test_evolution.py", "scripts/run_all.sh", "data/toolchain.json"):
    print("# sha256 " + relative + " " + hashlib.sha256((root / relative).read_bytes()).hexdigest())
PY
  echo '# generation: fresh protoc output matches checked-in generated code byte-for-byte'
  echo '# command: python3 -s tests/test_evolution.py'
  "$PYTHON" -s "$HERE/tests/test_evolution.py"
  echo '# command: python3 -s -O tests/test_evolution.py'
  "$PYTHON" -s -O "$HERE/tests/test_evolution.py"
  echo '# exit_code: 0 (both runs)'
} 2>&1 | tee "$LOG"
