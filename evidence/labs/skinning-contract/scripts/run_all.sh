#!/usr/bin/env bash
set -euo pipefail
LAB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$LAB_DIR"
mkdir -p build
{
  printf 'Skinning contract numeric model\n'
  date -u '+UTC: %Y-%m-%dT%H:%M:%SZ'
  python3 --version
  python3 -c 'import platform; print("Platform:", platform.system(), platform.machine()); print("Backend: stdlib Python floats; no Unreal/glTF runtime")'
  printf '\nInput SHA-256\n'
  sha256sum src/skinning.py tests/test_skinning.py data/cases.json scripts/run_all.sh
  printf '\nCommand: python3 -B tests/test_skinning.py\n'
  python3 -B tests/test_skinning.py
  printf '\nCommand: python3 -B -O tests/test_skinning.py\n'
  python3 -B -O tests/test_skinning.py
} 2>&1 | tee build/python_linux.txt
