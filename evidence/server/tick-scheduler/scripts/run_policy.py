#!/usr/bin/env python3
"""Capture deterministic Tick policy contracts into a NEW directory; not CPU timings."""
from __future__ import annotations
import argparse
import csv
from datetime import datetime, timezone
from fractions import Fraction
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
SOURCE = ROOT / "src/tick_scheduler.cpp"
HEADER = ROOT / "src/tick_policy.hpp"
ARTICLE = REPO / "知识/07-网络与游戏服务端/运行调度与过载保护/01-ServerMainLoop与TickScheduler.md"
LEGACY = ROOT / "results/tick_scheduler_win_x64_msvc.txt"
LEGACY_SHA256 = "7a8a5e6022f06840b4cf445889a0788a5e944849fbdfaeab2fc57a208b887051"
Q = 1_000_000_000
MAX_DELTA = 60_000_000_000
I64_MAX = 2**63 - 1
FIELDS = "scenario,step,hz,cap,max_delta_ns,raw_ns,accepted_ns,clamped_ns,phase_before,due,executed,extra_ticks,catchup_iteration,dropped_ticks,drop_event,phase_after,conservation".split(",")
TEST_NAMES = (
    "units_20_30_60_one_second", "threshold_and_one_nanosecond", "zero_elapsed_keeps_phase",
    "cap_checked_before_execution", "fraction_survives_whole_tick_drop", "extra_ticks_and_catch_iterations_differ",
    "clamp_and_drop_are_separate", "invalid_configuration_rejected", "negative_elapsed_rejected_without_state_change",
    "int64_max_elapsed_clamped_safely", "checked_arithmetic_boundary_values", "checked_arithmetic_rejects_before_overflow",
    "partition_without_clamp_or_drop", "partition_changes_cap_work_not_time_account", "partition_can_change_clamped_input",
    "value_copy_is_independent_phase_snapshot", "deterministic_random_conservation")
MUTATIONS = {
    "cap_after": ("TICK_POLICY_TEST_MUTANT_CAP_AFTER", {
        "cap_checked_before_execution", "fraction_survives_whole_tick_drop", "int64_max_elapsed_clamped_safely",
        "partition_changes_cap_work_not_time_account", "deterministic_random_conservation"}),
    "drop_fraction": ("TICK_POLICY_TEST_MUTANT_DROP_FRACTION", {
        "fraction_survives_whole_tick_drop", "deterministic_random_conservation"}),
}

class RunFailure(RuntimeError):
    pass

def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")

def run_command(argv: list[str], timeout: float, cwd: Path | None = None) -> dict:
    record = {"argv": argv, "cwd": str(cwd) if cwd else None,
              "started_utc": datetime.now(timezone.utc).isoformat(), "timeout_seconds": timeout,
              "returncode": None, "timed_out": False, "stdout": "", "stderr": ""}
    begin = time.perf_counter()
    try:
        child = subprocess.Popen(argv, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 start_new_session=(os.name == "posix"))
    except OSError as error:
        record["launch_error"] = str(error)
        record["elapsed_seconds"] = time.perf_counter() - begin
        return record
    try:
        stdout, stderr = child.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        record["timed_out"] = True
        if os.name == "posix":
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        else:
            # Terminate only this runner-owned process tree, including compiler children.
            try:
                killed = subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"],
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
                record["timeout_tree_kill_returncode"] = killed.returncode
            except (OSError, subprocess.SubprocessError) as error:
                record["termination_error"] = str(error)
            if child.poll() is None:
                child.kill()
        try:
            stdout, stderr = child.communicate(timeout=10)
        except subprocess.TimeoutExpired as error:
            # Bound cleanup too; preserve the partial capture instead of dropping this command.
            record["cleanup_timeout"] = True
            stdout, stderr = error.output or b"", error.stderr or b""
            if child.stdout:
                child.stdout.close()
            if child.stderr:
                child.stderr.close()
    record.update(returncode=child.returncode, elapsed_seconds=time.perf_counter() - begin,
                  stdout=stdout.decode("utf-8", errors="replace"), stderr=stderr.decode("utf-8", errors="replace"))
    return record

def require_command(record: dict, expected: int = 0) -> None:
    if record.get("launch_error") or record["timed_out"] or record["returncode"] != expected:
        raise RunFailure(f"command failed: expected {expected}, got {record['returncode']}, timeout={record['timed_out']}: {record['argv']}")

def check_self_test(record: dict, mode: str, expected_failures: set[str]) -> None:
    require_command(record, 1 if expected_failures else 0)
    matches = re.findall(r"^(PASS|FAIL) ([a-z0-9_]+)(?::.*)?$", record["stdout"], re.M)
    if [name for _, name in matches] != list(TEST_NAMES):
        raise RunFailure("missing, duplicate, or unexpected functional test names")
    failures = {name for status, name in matches if status == "FAIL"}
    if failures != expected_failures:
        raise RunFailure(f"mutation failed for unexpected reasons: {sorted(failures)}")
    expected = f"SELF_TEST passed={len(TEST_NAMES)-len(failures)} failed={len(failures)} mode={mode}"
    if record["stdout"].splitlines()[-1] != expected:
        raise RunFailure("functional summary disagrees with named results")

def extract_example(text: str) -> str:
    blocks = [block for block in re.findall(r"```cpp[ \t]*\n(.*?)```", text, re.S)
              if "// tick-policy: complete-example" in block]
    if len(blocks) != 1:
        raise RunFailure("article must contain exactly one marked complete C++ example")
    if "int main(" not in blocks[0]:
        raise RunFailure("article example has no complete main")
    return blocks[0]

def fixtures() -> list[tuple[str, tuple[int, int, int], list[int]]]:
    result = [(f"units_{hz}", (hz, 1000, MAX_DELTA), [Q]) for hz in (20, 30, 60)]
    result += [
        ("boundaries_60", (60, 1000, MAX_DELTA), [0, 16666666, 1, 0, 16666666, 1]),
        ("cap3_spike", (60, 3, MAX_DELTA), [200000000, 0, 16000000, 1000000]),
        ("fractional_drop", (20, 1, MAX_DELTA), [125000000, 25000000, 125000000, 25000000]),
        ("clamp_separate", (30, 3, 100000000), [2000000000, 0, 50000000, 50000000]),
        ("whole_second", (60, 1000, MAX_DELTA), [Q]),
        ("partition_second", (60, 1000, MAX_DELTA), [333333333, 333333333, 333333334]),
        ("whole_cap", (30, 3, MAX_DELTA), [Q]),
        ("partition_cap", (30, 3, MAX_DELTA), [100000000] * 10),
        ("whole_clamp", (30, 1000, 100000000), [Q]),
        ("partition_clamp", (30, 1000, 100000000), [100000000] * 10),
        ("max_elapsed", (1000, 1000, MAX_DELTA), [I64_MAX, 0, 1]),
    ]
    for index, config in enumerate(((30, 3, 500000000), (60, 10, 100000000), (20, 1, Q), (1000, 1000, MAX_DELTA))):
        state, sequence, mask = 20261004 + index, [], 2**64 - 1
        for _ in range(256):
            state = (state ^ (state >> 12)) & mask
            state = (state ^ (state << 25)) & mask
            state = (state ^ (state >> 27)) & mask
            sequence.append(((state * 2685821657736338717) & mask) % 2000000001)
        result.append((f"random_{config[0]}", config, sequence))
    return result

def reference_step(config: tuple[int, int, int], phase: Fraction, raw: int) -> tuple[dict, Fraction]:
    """Independent exact rational Tick units, not the C++ credit arithmetic helpers."""
    hz, cap, maximum = config
    if not 0 <= raw <= I64_MAX or not (1 <= hz <= 1000 and 1 <= cap <= 1000 and 1 <= maximum <= MAX_DELTA):
        raise RunFailure("reference input outside bounded contract")
    accepted = min(raw, maximum)
    available = phase + Fraction(accepted * hz, Q)
    due = available.numerator // available.denominator
    following = available - due
    executed = min(due, cap)
    result = {"hz": hz, "cap": cap, "max_delta_ns": maximum, "raw_ns": raw,
              "accepted_ns": accepted, "clamped_ns": raw-accepted, "phase_before": int(phase*Q),
              "due": due, "executed": executed, "extra_ticks": max(executed-1, 0),
              "catchup_iteration": int(executed > 1), "dropped_ticks": due-executed,
              "drop_event": int(due > executed), "phase_after": int(following*Q), "conservation": 1}
    if (phase*Q).denominator != 1 or (following*Q).denominator != 1:
        raise RunFailure("reference phase is not representable in credits")
    return result, following

def validate_rows(rows: list[dict]) -> tuple[dict, dict]:
    cases = fixtures()
    if len(rows) != sum(len(inputs) for _, _, inputs in cases):
        raise RunFailure("missing or unexpected scenario rows")
    groups, offset = [], 0
    for name, config, inputs in cases:
        phase = Fraction(0)
        totals = {key: 0 for key in ("raw_ns", "accepted_ns", "clamped_ns", "executed", "extra_ticks",
                                    "catchup_iteration", "dropped_ticks", "drop_event")}
        for index, raw in enumerate(inputs):
            row = rows[offset]
            offset += 1
            if set(row) != set(FIELDS) or row["scenario"] != name or row["step"] != str(index):
                raise RunFailure("scenario schema/order/step identity mismatch")
            values = {}
            for key in FIELDS[1:]:
                if not isinstance(row[key], str) or not re.fullmatch(r"0|[1-9][0-9]*", row[key]):
                    raise RunFailure(f"expected unsigned decimal field: {key}")
                values[key] = int(row[key])
            expected, phase = reference_step(config, phase, raw)
            for key, value in expected.items():
                if values[key] != value:
                    raise RunFailure(f"Fraction reference mismatch: {name}/{index}/{key}")
            if values["phase_before"] + values["accepted_ns"]*config[0] != (
                    values["executed"]+values["dropped_ticks"])*Q + values["phase_after"]:
                raise RunFailure("independent credit conservation failed")
            for key in totals:
                totals[key] += values[key]
        dropped = Fraction(totals["dropped_ticks"], config[0])
        groups.append({"scenario": name, "hz": config[0], "cap": config[1], "max_delta_ns": config[2],
            "steps": len(inputs), "raw_ns_total_decimal": str(totals["raw_ns"]),
            "accepted_ns_total_decimal": str(totals["accepted_ns"]), "clamped_ns_total_decimal": str(totals["clamped_ns"]),
            "executed_ticks": totals["executed"], "extra_ticks": totals["extra_ticks"],
            "catchup_iterations": totals["catchup_iteration"], "dropped_ticks": totals["dropped_ticks"],
            "drop_events": totals["drop_event"], "final_phase_credits": int(phase*Q),
            "dropped_seconds_exact": {"numerator": dropped.numerator, "denominator": dropped.denominator}})
    summary = {"kind": "deterministic_policy_model_not_CPU_measurement", "credits_per_tick": Q,
        "phase_unit": "elapsed_nanoseconds * Hz; 1e9 credits = one Tick",
        "large_integer_storage": "CSV decimal integers; JSON nanosecond totals are decimal strings, never binary floats",
        "partition_boundary": "Equal admitted time conserves executed+dropped and phase; per-call caps/clamps can change execution or admitted time",
        "groups": groups}
    validation = {"fixed_fixture_order_and_inputs": "PASS", "fraction_reference_every_field": "PASS",
        "every_step_credit_and_clamp_conservation": "PASS", "rows": len(rows), "groups": len(groups),
        "cpu_performance_claim": False}
    return summary, validation

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--compiler", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        raise RunFailure("timeout must be finite and positive")
    output = args.output_dir.absolute()
    if output.exists() or output.is_symlink():
        raise RunFailure(f"output already exists; refusing overwrite: {output}")
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise RunFailure(f"compiler unavailable (not skipped): {args.compiler}")
    if os.name == "nt" and not shutil.which("taskkill"):
        raise RunFailure("Windows process-tree timeout cleanup requires taskkill")
    if Path(compiler).stem.lower() == "cl":
        raise RunFailure("this runner uses GCC-compatible flags; no MSVC execution is claimed")
    if digest(LEGACY) != LEGACY_SHA256:
        raise RunFailure("protected legacy result hash mismatch")
    example = extract_example(ARTICLE.read_text(encoding="utf-8"))
    sources = [SOURCE, HEADER, Path(__file__).resolve(), ROOT/"scripts/test_policy.py", ARTICLE, LEGACY]
    before = {str(path.relative_to(REPO)): digest(path) for path in sources}
    output.mkdir(parents=True, exist_ok=False) # Atomic refusal, including any race-created output directory.
    commands = []
    provenance = {"state": "RUNNING", "kind": "pure_policy_model", "started_utc": datetime.now(timezone.utc).isoformat(),
        "system": platform.platform(), "python": sys.version, "compiler": compiler, "language_standard": "C++17",
        "source_hashes_before": before, "article_example_sha256": hashlib.sha256(example.encode()).hexdigest(),
        "timeout_seconds": args.timeout, "fixture_seed": 20261004,
        "contract": {"elapsed_type": "int64_t nonnegative nanoseconds", "phase_type": "uint64_t ns*Hz credits",
            "Hz_range": [1, 1000], "cap_range": [1, 1000], "maximum_delta_ns_range": [1, MAX_DELTA],
            "overflow": "checks before unsigned multiply/add", "cap_zero": "invalid configuration",
            "drop": "discard whole debt only; retain fractional Tick phase", "mutation_capture": "forbidden"},
        "not_verified": ["real CPU work/service-cost feedback", "wall clock wake precision", "production Tick capacity",
            "UE integration", "threaded Stop/callback execution", "network or memory overload effects", "MSVC"]}
    write_json(output/"provenance.json", provenance)
    def execute(label: str, command: list[str], expected: int = 0, cwd: Path | None = None) -> dict:
        record = run_command(command, args.timeout, cwd)
        record.update(label=label, expected_returncode=expected)
        commands.append(record)
        write_json(output/"commands.json", commands)
        require_command(record, expected)
        return record
    try:
        execute("compiler_version", [compiler, "--version"])
        with tempfile.TemporaryDirectory(prefix="tick-policy-") as temporary:
            temp = Path(temporary)
            suffix = ".exe" if os.name == "nt" else ""
            common = [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wformat=2", "-Werror", "-pedantic"]
            normal = temp/("policy"+suffix)
            execute("compile_policy", common+[str(SOURCE), "-o", str(normal)], cwd=temp)
            provenance["binary_sha256"] = digest(normal)
            check_self_test(execute("functional", [str(normal), "--self-test"], cwd=temp), "none", set())
            for mode, (macro, failures) in MUTATIONS.items():
                binary = temp/("mutant_"+mode+suffix)
                execute("compile_"+mode, common+["-D"+macro, str(SOURCE), "-o", str(binary)], cwd=temp)
                record = execute("negative_"+mode, [str(binary), "--self-test"], 1, cwd=temp)
                check_self_test(record, mode, failures)
                invalid_output = temp/(mode+"-must-not-exist.csv")
                record = execute("reject_capture_"+mode, [str(binary), "--scenarios", str(invalid_output)], 2, cwd=temp)
                if invalid_output.exists() or "mutation builds cannot produce evidence" not in record["stderr"]:
                    raise RunFailure("mutation capture was not rejected before file creation")
            example_source = temp/"article_example.cpp"
            example_source.write_text(example, encoding="utf-8")
            example_binary = temp/("article_example"+suffix)
            execute("compile_article_example", common+["-I", str(HEADER.parent), str(example_source), "-o", str(example_binary)], cwd=temp)
            example_record = execute("run_article_example", [str(example_binary)], cwd=temp)
            if example_record["stdout"].strip() != "EXAMPLE total_executed=2 dropped_ticks=1 phase=0":
                raise RunFailure("article example output mismatch")
            execute("runner_contract_tests", [sys.executable, "-B", str(ROOT/"scripts/test_policy.py")], cwd=temp)
            execute("generate_policy_scenarios", [str(normal), "--scenarios", str(output/"scenarios.csv")], cwd=temp)
        with (output/"scenarios.csv").open(newline="", encoding="utf-8") as source:
            reader = csv.DictReader(source)
            if reader.fieldnames != FIELDS:
                raise RunFailure("unexpected or duplicated CSV headers")
            summary, validation = validate_rows(list(reader))
        validation.update(functional="17 named groups, including 10000 deterministic conservation steps",
                          cap_mutation="exactly 5 named failures, exit 1", fraction_mutation="exactly 2 named failures, exit 1",
                          mutation_capture_rejected="both builds exit 2 before output creation",
                          article_example="extracted, compiled, executed with expected output", runner_contract_tests="PASS")
        write_json(output/"summary.json", summary)
        write_json(output/"validation.json", validation)
        after = {str(path.relative_to(REPO)): digest(path) for path in sources}
        provenance["source_hashes_after"] = after
        if before != after:
            raise RunFailure("source/article/legacy changed during capture")
        provenance.update(state="PASS_POLICY_CONTRACTS_NO_CPU_MEASUREMENT", historical_result_unchanged=True,
                          scenarios_sha256=digest(output/"scenarios.csv"))
        print(f"PASS: policy contracts and exact model records: {output}")
        return 0
    except Exception as error:
        provenance.update(state="FAIL", error=str(error))
        raise
    finally:
        provenance["finished_utc"] = datetime.now(timezone.utc).isoformat()
        write_json(output/"commands.json", commands)
        write_json(output/"provenance.json", provenance)

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RunFailure, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
