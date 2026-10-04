#!/usr/bin/env bash
# Legacy five-model entry. The Python driver owns output safety and failure handling.
# This explicit entry still runs each model's default main; it is not the focused
# Inventory contract command and may run historical benchmarks in the other models.
set -u
if [ "$#" -ne 2 ] || [ "$1" != "--output-dir" ] || [ -z "$2" ]; then
  printf 'usage: %s --output-dir NEW_EXTERNAL_DIRECTORY\n' "$0" >&2
  exit 2
fi
REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd -P)" || exit 1
PYTHON="${PYTHON:-python3}"
if [ -z "${CXX:-}" ] && [ -x /c/msys64/mingw64/bin/g++.exe ]; then
  CXX=/c/msys64/mingw64/bin/g++.exe
fi
CXX="${CXX:-g++}"
exec "$PYTHON" -B "$REPO_ROOT/evidence/tests/gameplay-core/scripts/run_inventory_contract.py" \
  --legacy-five-targets --output-dir "$2" --root "$REPO_ROOT" --cxx "$CXX"
