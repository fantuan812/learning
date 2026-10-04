#!/usr/bin/env python3
"""Standard-library-only functional runner. Explicit fresh out directory, fail closed."""
import argparse
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[4]
HERE = ROOT / "evidence/tests/damage-core"
FLAGS = {
    "o0": ["-O0", "-DNDEBUG"],
    "o2": ["-O2"],
    "ubsan": ["-O1", "-g", "-fsanitize=undefined,float-cast-overflow",
              "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"],
}


def utc():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_new(path, data):
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def validate_result(stdout, stderr):
    text = stdout.decode("utf-8", errors="strict")
    errors = stderr.decode("utf-8", errors="strict")
    lines = text.splitlines()
    result_lines = [line for line in lines if "RESULT" in line]
    if len(result_lines) != 1:
        raise ValueError("missing or duplicate RESULT")
    match = re.fullmatch(r"RESULT pass=(\d+) fail=(\d+)", result_lines[0])
    if match is None:
        raise ValueError("malformed RESULT")
    passed, failed = map(int, match.groups())
    checks = [line for line in lines if line.startswith("PASS ")]
    if re.search(r"\bFAIL\b", text + "\n" + errors) or failed != 0:
        raise ValueError("failure text or nonzero failure count")
    if passed == 0 or passed != len(checks) or len(set(checks)) != len(checks):
        raise ValueError("PASS count mismatch, empty suite, or duplicate PASS")
    if any(not line[5:].strip() for line in checks):
        raise ValueError("empty PASS identifier")
    if "RESULT" in errors:
        raise ValueError("unexpected RESULT on stderr")
    return {"pass": passed, "fail": failed}


def execute(command, label, out, timeout, records):
    record = {"stage": label, "command": command, "cwd": str(out), "started_utc": utc()}
    records.append(record)
    start = time.monotonic()
    stdout, stderr = b"", b""
    try:
        result = subprocess.run(command, cwd=out, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=timeout, check=False)
        stdout, stderr = result.stdout, result.stderr
        record["returncode"] = result.returncode
    except subprocess.TimeoutExpired as error:
        stdout, stderr = error.stdout or b"", error.stderr or b""
        record.update(returncode=None, error="timeout")
    except OSError as error:
        record.update(returncode=None, error=f"{type(error).__name__}: {error}")
    record["elapsed_seconds"] = time.monotonic() - start
    record["finished_utc"] = utc()
    # A write failure propagates, including when the child otherwise returned 0.
    for stream, data in (("stdout", stdout), ("stderr", stderr)):
        path = out / f"{label}.{stream}.txt"
        write_new(path, data)
        record[stream] = {"file": path.name, "sha256": sha(path), "bytes": len(data)}
    print(f"STAGE {label} returncode={record['returncode']} error={record.get('error', '')}", flush=True)
    if record["returncode"] != 0:
        raise RuntimeError(f"stage failed: {label}: {record}")
    return stdout, stderr


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path, help="new directory outside the repository; parent must exist")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--mode", choices=("strict", "ubsan", "all"), default="strict")
    parser.add_argument("--source", type=Path, default=HERE / "src/damage_pipeline.cpp")
    parser.add_argument("--test", type=Path, default=HERE / "tests/damage_contract.cpp")
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("timeout must be finite and positive")
    # Lexical existence rejects existing directories, files, and even broken links.
    if os.path.lexists(args.out):
        print("ERROR output already exists; no files changed", file=sys.stderr)
        return 1
    out = args.out.resolve()
    if out == ROOT or ROOT in out.parents:
        print("ERROR output must be outside repository", file=sys.stderr)
        return 1
    try:
        out.mkdir()  # atomic exclusive creation; no exist_ok and no parent mutation
    except OSError as error:
        print(f"ERROR cannot create output: {error}", file=sys.stderr)
        return 1
    manifest = {"schema": 1, "run_id": str(uuid.uuid4()), "started_utc": utc(),
                "os": platform.platform(), "python": sys.version, "runner_sha256": sha(Path(__file__)),
                "out": str(out), "mode": args.mode, "stages": [], "outcome": "failed"}
    try:
        source, test = args.source.resolve(strict=True), args.test.resolve(strict=True)
        manifest["inputs"] = {"source": {"path": str(source), "sha256": sha(source)},
                              "test": {"path": str(test), "sha256": sha(test)}}
        revision = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=args.timeout, check=False)
        manifest["source_revision"] = revision.stdout.decode().strip() if revision.returncode == 0 else "unavailable"
        manifest["source_revision_note"] = "worktree source/test hashes identify actual inputs; HEAD alone does not"
        version, _ = execute([args.cxx, "--version"], "compiler", out, args.timeout, manifest["stages"])
        manifest["compiler"] = version.decode("utf-8", errors="replace")
        modes = ["o0", "o2"] if args.mode == "strict" else ["ubsan"] if args.mode == "ubsan" else list(FLAGS)
        for mode in modes:
            binary = out / (f"damage-{mode}.exe" if os.name == "nt" else f"damage-{mode}")
            command = [args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic"] + FLAGS[mode]
            command += [f'-DDAMAGE_SOURCE="{source.as_posix()}"', str(test), "-o", str(binary)]
            execute(command, f"compile-{mode}", out, args.timeout, manifest["stages"])
            if not binary.is_file() or binary.is_symlink() or not os.access(binary, os.X_OK):
                raise RuntimeError(f"compiler did not produce a new executable: {binary}")
            manifest.setdefault("binaries", {})[mode] = {"file": binary.name, "sha256": sha(binary)}
            stdout, stderr = execute([str(binary)], f"run-{mode}", out, args.timeout, manifest["stages"])
            manifest["stages"][-1]["result"] = validate_result(stdout, stderr)
        manifest["outcome"] = "passed"
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        manifest["error"] = f"{type(error).__name__}: {error}"
        print(f"ERROR {manifest['error']}", file=sys.stderr)
    manifest["finished_utc"] = utc()
    try:
        write_new(out / "run-manifest.json", (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode())
    except OSError as error:
        print(f"ERROR manifest output failure: {error}", file=sys.stderr)
        return 1
    if manifest["outcome"] != "passed":
        return 1
    print(f"COMPLETED run_id={manifest['run_id']} manifest={out / 'run-manifest.json'}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"ERROR runner failure: {error}", file=sys.stderr)
        sys.exit(1)
