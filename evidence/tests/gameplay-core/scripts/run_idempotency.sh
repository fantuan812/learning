#!/usr/bin/env bash
# Linux/Bash evidence runner. Explicit fresh output outside this repository only.
set -euo pipefail
if [[ $# -ne 1 ]]; then
  echo 'usage: run_idempotency.sh /absolute/outside-repository/new-output.log' >&2
  exit 64
fi
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
ROOT="$(cd "$HERE/../../.." && pwd -P)"
PYTHON="${PYTHON:-python3}"
# PYTHON is one executable path, not a shell command. No eval or package installs.
exec "$PYTHON" -B - "$ROOT" "$HERE/src/idempotency_sqlite.py" "${BASH_SOURCE[0]}" "$1" <<'PY'
import datetime
import hashlib
import math
import os
from pathlib import Path
import platform
import re
import shlex
import signal
import sqlite3
import subprocess
import sys


def main():
    if sys.version_info < (3, 11) or sqlite3.sqlite_version_info < (3, 35, 0):
        raise ValueError("requires Python 3.11+ and SQLite 3.35+")
    root, source, runner = (Path(arg).resolve() for arg in sys.argv[1:4])
    output = Path(sys.argv[4])
    if not output.is_absolute():
        raise ValueError("output must be an absolute path")
    if os.path.lexists(output):
        raise ValueError("refusing existing output (file, directory, or symlink)")
    parent = output.parent.resolve(strict=True)
    destination = parent / output.name
    if destination == root or root in destination.parents:
        raise ValueError("output must be outside the repository, including symlink aliases")
    timeout = float(os.environ.get("IDEMPOTENCY_TIMEOUT_SECONDS", "60"))
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("IDEMPOTENCY_TIMEOUT_SECONDS must be finite and positive")
    # Exclusive creation closes the ordinary check/create race; no overwrite/tee.
    # This local evidence tool assumes output ancestors are not maliciously swapped.
    with destination.open("xb") as log:
        def emit(data):
            if isinstance(data, str):
                data = data.encode("utf-8")
            log.write(data)
            log.flush()  # Detect write failures before reporting success.
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()

        emit("# evidence: gameplay-core/idempotency_sqlite (synthetic local databases only)\n")
        emit(f"# generated: {datetime.datetime.now(datetime.timezone.utc).isoformat()}\n")
        emit(f"# host: {platform.system()} {platform.machine()}\n")
        emit(f"# python: {platform.python_version()} sqlite: {sqlite3.sqlite_version}\n")
        emit(f"# source_sha256: {hashlib.sha256(source.read_bytes()).hexdigest()}\n")
        emit(f"# runner_sha256: {hashlib.sha256(runner.read_bytes()).hexdigest()}\n")
        emit(f"# timeout_seconds_per_run: {timeout}\n")
        counts = []
        for mode in ([], ["-O"]):
            command = [sys.executable, "-B", *mode, str(source)]
            emit(f"# command: {shlex.join(command)}\n")
            child_env = os.environ.copy()
            child_env.pop("PYTHONOPTIMIZE", None)  # normal=0 and explicit -O=1, even if inherited.
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       start_new_session=True, env=child_env)
            timed_out = False
            try:
                stdout, stderr = process.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                os.killpg(process.pid, signal.SIGKILL)
                stdout, stderr = process.communicate()
            emit("# stdout_begin\n")
            emit(stdout)
            emit("\n# stdout_end\n# stderr_begin\n")
            emit(stderr)
            emit(f"\n# stderr_end\n# process_exit_code: {process.returncode}\n")
            if timed_out:
                emit("# timed_out: true\n# runner_exit_code: 124\n")
                return 124
            if process.returncode:
                code = process.returncode if process.returncode > 0 else 128-process.returncode
                emit(f"# runner_exit_code: {code}\n")
                return code
            # Exit 0 alone is not evidence: require one nonempty standard unittest
            # report and its final OK line. This is a trusted-test output protocol,
            # not protection against a program deliberately forging an OK report.
            text = stderr.decode("utf-8", errors="replace")
            reports = re.findall(r"^Ran ([0-9]+) tests? in [0-9.]+s$", text, re.MULTILINE)
            if len(reports) != 1 or int(reports[0]) == 0 or not text.rstrip().endswith("\nOK"):
                emit("# invalid_or_empty_unittest_report\n# runner_exit_code: 65\n")
                return 65
            counts.append(int(reports[0]))
            emit(f"# verified_tests_run: {counts[-1]}\n")
        if counts[0] != counts[1]:
            emit("# normal_optimized_test_count_mismatch\n# runner_exit_code: 65\n")
            return 65
        emit("# runner_exit_code: 0 (both nonempty unittest runs verified)\n")
    return 0


try:
    raise SystemExit(main())
except (OSError, ValueError) as exc:
    print(f"run_idempotency: {type(exc).__name__}: {exc}", file=sys.stderr)
    raise SystemExit(1)
PY
