#!/usr/bin/env python3
"""Two runner safety regressions with a real compiler; never edits the package."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++-14")
    parser.add_argument("--out", type=Path, help="new diagnostic directory only")
    args = parser.parse_args()
    compiler = shutil.which(args.cxx)
    if compiler is None:
        parser.error("real compiler not found: " + args.cxx)
    if args.out is None:
        root = Path(tempfile.mkdtemp(prefix="expected-runner-safety-"))
    else:
        root = args.out.absolute()
        try:
            root.mkdir(parents=True, exist_ok=False)
        except OSError as error:
            parser.error("diagnostic directory must be fresh: " + str(error))
    runner = Path(__file__).with_name("run_expected_contract.py").resolve()
    records = []
    def invoke(script, output):
        command = [sys.executable, "-B", str(script), "--cxx", compiler,
                   "--out", str(output)]
        result = subprocess.run(command, capture_output=True, text=True,
                                timeout=120, check=False)
        record = dict(command=command, returncode=result.returncode,
                      stdout=result.stdout, stderr=result.stderr)
        records.append(record)
        return record

    existing = root / "existing"
    existing.mkdir()
    refused_empty = invoke(runner, existing)
    require(refused_empty["returncode"] == 2 and not list(existing.iterdir()),
            "existing empty output was not rejected unchanged")
    sentinel = existing / "keep.txt"
    sentinel.write_text("KEEP\n", encoding="utf-8")
    refused_full = invoke(runner, existing)
    require(refused_full["returncode"] == 2
            and sentinel.read_bytes() == b"KEEP\n"
            and sorted(p.name for p in existing.iterdir()) == ["keep.txt"],
            "existing nonempty output was changed")
    print("PASS: existing output directories rejected (empty and nonempty)")

    fixture = root / "compile-failure-fixture"
    fixture.mkdir()
    fixture_runner = fixture / runner.name
    shutil.copy2(runner, fixture_runner)
    require(fixture_runner.read_bytes() == runner.read_bytes(), "runner copy changed")
    (fixture / "expected_contract.cpp").write_text(
        "#error EXPECTED_CONTRACT_REAL_COMPILE_FAILURE\nint main() { return 0; }\n",
        encoding="utf-8")
    failed = invoke(fixture_runner, root / "compile-failure-results")
    require(failed["returncode"] == 1, "compile failure did not return runner exit 1")
    report_path = root / "compile-failure-results/report.json"
    report = json.loads(report_path.read_text())
    require(report["passed"] is False and len(report["runs"]) == 2, "invalid failure report")
    for entry in report["runs"]:
        require(entry["compile"]["returncode"] not in (0, None)
                and "EXPECTED_CONTRACT_REAL_COMPILE_FAILURE" in entry["compile"]["stderr"]
                and entry["passed"] is False and "execute" not in entry,
                "real compilation did not fail before execution")
    print("PASS: real compiler failures return nonzero and do not execute")
    diagnostics = dict(
        passed=True, safety_tests=2, runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
        invocations=records,
        failure_report_sha256=hashlib.sha256(report_path.read_bytes()).hexdigest())
    (root / "validation.json").write_text(json.dumps(diagnostics, indent=2) + "\n")
    print("Diagnostics: " + str(root))
    return 0


if __name__ == "__main__":
    sys.exit(main())
