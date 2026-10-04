#!/usr/bin/env bash
# Thin wrapper: --out must name a NEW directory outside the repository.
set -eu
SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
exec "${PYTHON:-python3}" -B "$SCRIPT_DIR/run_damage_contract.py" "$@"
