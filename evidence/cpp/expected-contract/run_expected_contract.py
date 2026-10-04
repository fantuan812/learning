#!/usr/bin/env python3
"""Compile/run the adjacent C++23 probe; standard-library Python only.

Default logs use a fresh system temporary directory. Explicit --out must name
a new directory. No package installation, network access, or shell expansion.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def invoke(command, timeout):
    try:
        result = subprocess.run(command, capture_output=True, text=True,
                                timeout=timeout, check=False)
        return dict(command=command, returncode=result.returncode,
                    stdout=result.stdout, stderr=result.stderr)
    except (OSError, subprocess.TimeoutExpired) as error:
        def partial(name):
            value = getattr(error, name, None) or ""
            return value.decode(errors="replace") if isinstance(value, bytes) else value
        return dict(command=command, returncode=None, stdout=partial("stdout"),
                    stderr=partial("stderr"), error=str(error))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=shutil.which("g++-14") or "c++",
                        help="GCC/Clang-compatible compiler executable")
    parser.add_argument("--out", type=Path, help="new output directory only")
    args = parser.parse_args()
    try:
        if args.out is None:
            output = Path(tempfile.mkdtemp(prefix="expected-contract-"))
        else:
            # Do not resolve the last path component: mkdir must reject existing
            # paths, including a dangling symlink, instead of following it.
            output = args.out.absolute()
            output.mkdir(parents=True, exist_ok=False)
    except OSError as error:
        parser.error("output directory must be fresh and writable: " + str(error))
    print("Output directory: " + str(output), flush=True)
    runner = Path(__file__).resolve()
    source = runner.with_name("expected_contract.cpp")
    report = dict(
        created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        platform=platform.platform(), python_version=sys.version,
        source_path=str(source), source_sha256=sha(source),
        runner_path=str(runner), runner_sha256=sha(runner),
        output_path=str(output),
        compiler_version=invoke([args.cxx, "--version"], 15),
        compiler_target=invoke([args.cxx, "-dumpmachine"], 15),
        runs=[],
    )
    with tempfile.TemporaryDirectory(prefix="build-", dir=output) as build:
        for mode in ("O0", "O2"):
            executable = str(Path(build) / ("expected-" + mode))
            command = [args.cxx, "-std=c++23", "-Wall", "-Wextra", "-Wpedantic",
                       "-Werror", "-" + mode, str(source), "-o", executable]
            compiled = invoke(command, 90)
            for stream in ("stdout", "stderr"):
                (output / (mode + ".compile." + stream + ".txt")).write_text(
                    compiled[stream], encoding="utf-8")
            entry = dict(mode=mode, compile=compiled, passed=False)
            if compiled["returncode"] == 0:
                executed = invoke([executable], 30)
                entry["execute"] = executed
                for stream in ("stdout", "stderr"):
                    (output / (mode + "." + stream + ".txt")).write_text(
                        executed[stream], encoding="utf-8")
                print(mode + ":\n" + executed["stdout"], end="")
                if executed["stderr"]:
                    print(executed["stderr"], file=sys.stderr, end="")
                if "error" in executed:
                    print(executed["error"], file=sys.stderr)
                entry["passed"] = executed["returncode"] == 0
            else:
                print(mode + " compilation failed:\n" + compiled["stderr"],
                      file=sys.stderr, end="")
                if "error" in compiled:
                    print(compiled["error"], file=sys.stderr)
            report["runs"].append(entry)
    report["passed"] = all(entry["passed"] for entry in report["runs"])
    (output / "report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("PASS" if report["passed"] else "FAIL")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
