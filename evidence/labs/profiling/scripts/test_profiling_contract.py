#!/usr/bin/env python3
"""Isolated C++17 functional regression; timing ratios are never pass criteria.

Run from the repository root:
  python3 evidence/labs/profiling/scripts/test_profiling_contract.py
Optionally preserve raw output in a NEW directory (never overwrite archives):
  python3 evidence/labs/profiling/scripts/test_profiling_contract.py --record-dir PATH
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile


FIXTURE = r'''
#define main profiling_benchmark_main
#include "src/profiling_overhead.cpp"
#undef main

int main() {
    int failures = 0;
    int cases = 0;
    auto expect = [&](bool ok, const char* name) {
        ++cases;
        if (!ok) { ++failures; std::printf("FIXTURE FAIL %s\n", name); }
    };
    auto ratio = [&](double a, double b, double threshold, Comparison comparison,
                     Observation expected, const char* name) {
        expect(ClassifyRatio(a, b, threshold, comparison) == expected, name);
    };
    ratio(0.5, 1, 0.5, Comparison::AtLeast, Observation::Supported, "P1 inclusive boundary");
    ratio(0.49, 1, 0.5, Comparison::AtLeast, Observation::Unsupported, "P1 below boundary");
    ratio(1, 1, 1, Comparison::Greater, Observation::Unsupported, "P2 strict boundary");
    ratio(2, 1, 1, Comparison::Greater, Observation::Supported, "P2 above boundary");
    ratio(178.36, 49.42, 5, Comparison::Greater, Observation::Unsupported, "P3 archived failed hypothesis");
    ratio(250, 50, 5, Comparison::Greater, Observation::Unsupported, "P3 strict boundary");
    ratio(251, 50, 5, Comparison::Greater, Observation::Supported, "P3 above boundary");
    ratio(1, 1, 1, Comparison::Greater, Observation::Unsupported, "P4 strict boundary");
    ratio(50, 5, 10, Comparison::Greater, Observation::Unsupported, "P5 strict boundary");
    ratio(51, 5, 10, Comparison::Greater, Observation::Supported, "P5 above boundary");
    ratio(16.6, 16.6, 1, Comparison::Greater, Observation::Unsupported, "P6 strict boundary");
    ratio(17, 16.6, 1, Comparison::Greater, Observation::Supported, "P6 above budget");
    ratio(16.6, 16.6, 1, Comparison::Less, Observation::Unsupported, "P7 strict boundary");
    ratio(16, 16.6, 1, Comparison::Less, Observation::Supported, "P7 below budget");
    ratio(1, 0, 1, Comparison::Greater, Observation::Unavailable, "zero denominator");
    ratio(0, 1, 1, Comparison::Greater, Observation::Unavailable, "zero numerator");
    ratio(-1, 1, 1, Comparison::Greater, Observation::Unavailable, "negative measurement");
    ratio(kUnavailable, 1, 1, Comparison::Greater, Observation::Unavailable, "NaN measurement");
    ratio(std::numeric_limits<double>::infinity(), 1, 1, Comparison::Greater,
          Observation::Unavailable, "infinite measurement");
    ratio(1, 1, kUnavailable, Comparison::Greater, Observation::Unavailable, "invalid threshold");
    bool called = false;
    auto unused = [&](int) { called = true; };
    expect(!Measure("invalid batch", 0, 1, unused).valid, "invalid batch rejected");
    expect(!Measure("invalid rounds", 1, 0, unused).valid, "invalid rounds rejected");
    expect(!called, "invalid harness never executes callback");
    expect(!IsPositiveFinite(0) && !IsPositiveFinite(kUnavailable) && IsPositiveFinite(1),
           "measurement validity semantics");
    ObserveRatio("fixture", "failed hypothesis", 178.36, 49.42, 5, Comparison::Greater);
    expect(gUnsupported == 1 && gSupported == 0 && gUnavailable == 0,
           "unsupported observation reported separately");
    expect(gFail == 0 && FunctionalExitCode() == 0, "failed hypothesis does not fail functionality");
    Check(false, "fixture intentional failure", "tests nonzero functional verdict; no real I/O in fixture");
    expect(gFail == 1 && FunctionalExitCode() != 0, "real failure gives nonzero exit");
    std::printf("FIXTURE_RESULT cases=%d fail=%d\n", cases, failures);
    return failures == 0 ? 0 : 1;
}
'''


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, cwd):
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True,
                          timeout=120, check=False)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--record-dir", type=Path, help="new directory for raw stdout/stderr and provenance")
    args = parser.parse_args()
    record_dir = args.record_dir
    if record_dir:
        # Refuse an existing directory rather than replace any previous evidence.
        record_dir.mkdir(parents=True, exist_ok=False)
    lab = Path(__file__).resolve().parents[1]
    source = lab / "src/profiling_overhead.cpp"
    archives = [lab / "results/profiling_overhead.txt", lab / "results/hitch_and_budget.txt"]
    before = {p.name: sha256(p) for p in archives}
    build_before = (lab / "build").exists()
    records = {}
    metadata = {
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "host": platform.platform(),
        "architecture": platform.machine(),
        "logical_cpus": os.cpu_count(),
        "python": platform.python_version(),
        "source": "evidence/labs/profiling/src/profiling_overhead.cpp",
        "source_sha256": sha256(source),
        "test_sha256": sha256(Path(__file__).resolve()),
        "original_results_sha256_before": before,
        "samples": {"batch_calls": 20000, "rounds": 200, "normal_runs": 1, "missing_build_runs": 1},
        "perf_executed": False,
        "notes": "Fresh temporary workspace; source copied unchanged. No CPU pinning or performance counters. Timing observations are machine/build/sample-local.",
        "commands": {},
    }
    errors = []
    try:
        require(shutil.which("g++") is not None, "g++ is required")
        with tempfile.TemporaryDirectory(prefix="profiling-contract-") as tmp:
            work = Path(tmp)
            (work / "src").mkdir()
            shutil.copyfile(source, work / "src/profiling_overhead.cpp")
            (work / "fixture.cpp").write_text(FIXTURE, encoding="utf-8")
            metadata["compiler"] = run(["g++", "--version"], work).stdout.splitlines()[0]
            metadata["target"] = run(["g++", "-dumpmachine"], work).stdout.strip()
            flags = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-pedantic"]
            commands = {
                "compile_benchmark": ["g++", *flags, "src/profiling_overhead.cpp", "-o", "profiling_overhead"],
                "compile_fixture": ["g++", *flags, "fixture.cpp", "-o", "fixture"],
            }
            for name, command in commands.items():
                result = run(command, work)
                records[name] = result
                metadata["commands"][name] = {"cwd": "temporary workspace", "argv": command, "exit_code": result.returncode}
                require(result.returncode == 0, f"{name} failed: {result.stderr}")
            result = run(["./fixture"], work)
            records["fixture"] = result
            metadata["commands"]["fixture"] = {"cwd": "temporary workspace", "argv": ["./fixture"], "exit_code": result.returncode}
            require(result.returncode == 0 and re.search(r"FIXTURE_RESULT cases=\d+ fail=0", result.stdout),
                    "deterministic fixture failed")
            for case in ("normal", "missing_build"):
                case_dir = work / case
                case_dir.mkdir()
                if case == "normal":
                    (case_dir / "build").mkdir()
                result = run(["../profiling_overhead"], case_dir)
                records[case] = result
                metadata["commands"][case] = {"cwd": f"temporary workspace/{case}", "argv": ["../profiling_overhead"], "exit_code": result.returncode}
                summary = re.search(r"^FUNCTIONAL_RESULT pass=(\d+) fail=(\d+)$", result.stdout, re.M)
                require(summary is not None, f"{case}: functional summary missing")
                observations = re.findall(r"^OBSERVATION (P[1-7]) (SUPPORTED|UNSUPPORTED|UNAVAILABLE)\b", result.stdout, re.M)
                require([p for p, _ in observations] == [f"P{i}" for i in range(1, 8)], f"{case}: seven observations missing")
                require("SKIPPED" not in result.stdout, f"{case}: required work silently skipped")
                require(not (case_dir / "build/profiling_overhead_tmp.log").exists(), f"{case}: temporary log leaked")
                if case == "normal":
                    require(result.returncode == 0 and summary.groups() == ("13", "0"), "normal: functional contract failed")
                    require(all(status != "UNAVAILABLE" for _, status in observations), "normal: measurement unavailable")
                    require(len(re.findall(r"^FUNCTIONAL PASS F\d{2}\b", result.stdout, re.M)) == 13,
                            "normal: functional coverage incomplete")
                    # No requirement on which timing hypotheses this machine supports.
                else:
                    require(result.returncode != 0 and int(summary.group(2)) > 0, "missing_build: real file failure returned success")
                    require("FUNCTIONAL FAIL F05 file opens" in result.stdout, "missing_build: file-open failure not diagnosed")
                    require(dict(observations)["P4"] == "UNAVAILABLE" and dict(observations)["P6"] == "UNAVAILABLE",
                            "missing_build: absent file measurements used as observations")
                    require(not (case_dir / "build").exists(), "missing_build: test created the missing directory")
            print("PASS deterministic ratio/exit-code fixture")
            print("PASS normal functional contract (13 checks; timing hypotheses are observations)")
            print("PASS missing-build negative case (required file-open failure returns nonzero)")
    except (RuntimeError, subprocess.TimeoutExpired, OSError) as exc:
        errors.append(str(exc))
    finally:
        after = {p.name: sha256(p) for p in archives}
        metadata["original_results_sha256_after"] = after
        metadata["lab_build_existence_unchanged"] = (lab / "build").exists() == build_before
        if after != before:
            errors.append("existing result archives changed")
        if not metadata["lab_build_existence_unchanged"]:
            errors.append("repository-local build directory existence changed")
        metadata["status"] = "FAIL" if errors else "PASS"
        metadata["errors"] = errors
        if record_dir:
            for name, result in records.items():
                (record_dir / f"{name}.stdout.txt").write_text(result.stdout, encoding="utf-8")
                (record_dir / f"{name}.stderr.txt").write_text(result.stderr, encoding="utf-8")
            (record_dir / "provenance.json").write_text(json.dumps(metadata, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    for error in errors:
        print(f"FAIL {error}")
    print(f"CONTRACT_TEST_RESULT {'FAIL' if errors else 'PASS'}; archived results preserved={before == after}; perf_executed=false")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
