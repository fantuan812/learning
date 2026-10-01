#!/usr/bin/env bash
# Run the independent stdlib SQLite model; no package installation, no C++ outputs overwritten.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON="${PYTHON:-python3}"
SOURCE="$HERE/src/idempotency_sqlite.py"
"$PYTHON" -c 'import sqlite3, sys; sys.exit(0 if sys.version_info >= (3, 11) and sqlite3.sqlite_version_info >= (3, 35, 0) else "Requires Python 3.11+ and SQLite 3.35+")'
mkdir -p "$HERE/results"
LOG="$HERE/results/idempotency_sqlite_linux.txt"
{
  echo '# evidence: gameplay-core/idempotency_sqlite (local model only)'
  echo "# generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# host: $(uname -s -m)"
  "$PYTHON" -c 'import hashlib,pathlib,sys; print("# source_sha256: " + hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest())' "$SOURCE"
  echo "# command: $PYTHON src/idempotency_sqlite.py"
  "$PYTHON" "$SOURCE"
  echo "# command: $PYTHON -O src/idempotency_sqlite.py"
  "$PYTHON" -O "$SOURCE"
  echo '# exit_code: 0 (both runs)'
} 2>&1 | tee "$LOG"
