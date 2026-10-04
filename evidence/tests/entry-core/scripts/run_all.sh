#!/usr/bin/env bash
# 新输出目录必填；不覆盖历史raw，不扫描旧results，默认运行四个独立合同模型（含JIP）。
set -euo pipefail
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PYTHON="${PYTHON:-python3}"
export PYTHONDONTWRITEBYTECODE=1
exec "$PYTHON" -B "$HERE/test_runner_contract.py" "$@"
