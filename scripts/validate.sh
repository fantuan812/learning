#!/usr/bin/env bash
# validate.sh —— 知识库门禁一键校验（Windows 外/WSL/Linux 友好）。
# 运行全部可用的校验：Python 健康检查 + git whitespace + OKF/仓库门禁（有 pwsh 时）。
# 用法: bash scripts/validate.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
echo "== [1/4] Python 健康检查（编码/BOM/围栏/H1/相对链接）"
python3 "$ROOT/scripts/check_links.py" "$ROOT"

echo "== [2/4] git diff --check"
git -C "$ROOT" diff --check

if command -v pwsh >/dev/null 2>&1; then
  echo "== [3/4] OKF Strict（profile-scope）"
  pwsh -NoProfile -File "$ROOT/scripts/check_okf.ps1" -Root "$ROOT" -Mode Strict
  echo "== [4/4] check_repo（仓库门禁）"
  pwsh -NoProfile -File "$ROOT/scripts/check_repo.ps1" -Root "$ROOT"
else
  echo "== [3-4/4] 跳过：未找到 pwsh（OKF/仓库门禁需 PowerShell；Windows 可用 powershell.exe，或安装 pwsh）"
fi

echo "VALIDATE: PASS"