#!/usr/bin/env python3
"""Run the A*/LRU lab into a NEW directory; standard library, no speed gate.

All compilation artifacts live in an automatically cleaned system temporary directory.
The seven approved source/result names are documented in README.md. No historic result
is overwritten. A failed command retains its exact output and a failed provenance state.
"""
from __future__ import annotations
import argparse
import csv
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
from collections import defaultdict
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
SOURCE = ROOT / "src/astar_benchmark.cpp"
LEGACY = ROOT / "results/astar_benchmark_win_x64_msvc.txt"
LEGACY_SHA256 = "297fc4cecd624d83b7582e5ab6aab61f30013fb02b40890404994b71195e1b1f"


class RunFailure(RuntimeError):
    pass


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def create_output_dir(path: Path) -> None:
    # mkdir without exist_ok is the atomic no-overwrite gate, also rejects symlinks.
    path.mkdir(parents=True, exist_ok=False)


def run_command(argv: list[str], timeout: float, cwd: Path | None = None) -> dict:
    started = datetime.now(timezone.utc).isoformat()
    begin = time.perf_counter()
    child = subprocess.Popen(argv, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             start_new_session=(os.name == "posix"))
    timed_out = False
    try:
        stdout, stderr = child.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        if os.name == "posix":
            os.killpg(child.pid, signal.SIGKILL)
        else:
            child.kill()
        stdout, stderr = child.communicate()
    return {"argv": argv, "cwd": str(cwd) if cwd else None, "started_utc": started,
            "timeout_seconds": timeout, "elapsed_seconds": time.perf_counter() - begin,
            "returncode": child.returncode, "timed_out": timed_out,
            "stdout": stdout.decode("utf-8", errors="replace"),
            "stderr": stderr.decode("utf-8", errors="replace")}


def require_command(record: dict, expected: int = 0) -> None:
    if record["timed_out"] or record["returncode"] != expected:
        raise RunFailure(f"command failed: rc={record['returncode']}, timeout={record['timed_out']}: {record['argv']}")


def percentile(values: list[float], fraction: float) -> float:
    """Nearest-rank quantile; no interpolation; nonempty finite sample required."""
    if not values or not 0 <= fraction <= 1 or any(not math.isfinite(v) for v in values):
        raise RunFailure("invalid percentile input")
    ordered = sorted(values)
    return ordered[max(0, math.ceil(fraction * len(ordered)) - 1)]


def summarize(rows: list[dict], queries: int, repeats: int) -> tuple[dict, dict]:
    timing = []
    calibration = []
    inputs = defaultdict(list)
    seen = set()
    grouped = defaultdict(list)
    pairs = defaultdict(dict)
    for row in rows:
        kind = row["row_type"]
        if kind == "calibration":
            ns = float(row["total_ns"])
            if not math.isfinite(ns) or ns < 0:
                raise RunFailure("invalid clock calibration")
            calibration.append(ns)
            continue
        if kind == "input":
            inputs[(row["scenario"], int(row["density"]))].append(row)
            continue
        if kind != "timing":
            raise RunFailure("unknown sample row type")
        scenario, density, method = row["scenario"], int(row["density"]), row["method"]
        repeat, first, operations = int(row["repeat"]), int(row["first_query"]), int(row["operations"])
        ns, per_op = float(row["total_ns"]), float(row["ns_per_operation"])
        hits, repeated, repeat_hits = int(row["hits"]), int(row["scheduled_repeats"]), int(row["repeat_hits"])
        if not (operations > 0 and 0 <= hits <= operations and 0 <= repeat_hits <= min(hits, repeated)
                and 0 <= repeated <= operations and 0 <= repeat < repeats):
            raise RunFailure("invalid operation/hit/repeat counts")
        if not (math.isfinite(ns) and ns >= 0 and math.isfinite(per_op) and math.isclose(per_op, ns/operations, rel_tol=1e-12, abs_tol=1e-9)):
            raise RunFailure("invalid elapsed time or batch denominator")
        if not math.isfinite(float(row["cost_sum"])):
            raise RunFailure("nonfinite consumed cost")
        identity = scenario, density, repeat, first, method
        if identity in seen:
            raise RunFailure("duplicate timing sample")
        seen.add(identity)
        grouped[(scenario, density, method)].append(row)
        if scenario != "lookup-only":
            pairs[(scenario, density, repeat, first)][method] = row
        timing.append(row)
    if len(calibration) != 1000 or not any(v > 0 for v in calibration):
        raise RunFailure("missing/nonpositive clock calibration")
    for density in (25, 40):
        for scenario in ("search", "cold-fill", "warm-hit", "mixed-pressure"):
            expected = 64 if scenario == "cold-fill" else queries
            rs = inputs[(scenario, density)]
            if len(rs) != expected or sorted(int(r["first_query"]) for r in rs) != list(range(expected)):
                raise RunFailure(f"missing workload inputs: {scenario}/{density}")
    expected_methods = {
        "search": ("heap-copy-consume", "linear-copy-consume"),
        "cold-fill": ("cache-copy-consume", "uncached-copy-consume"),
        "warm-hit": ("cache-copy-consume", "uncached-copy-consume"),
        "mixed-pressure": ("cache-copy-consume", "uncached-copy-consume"),
        "lookup-only": ("borrowed-metadata",),
    }
    expected_groups = {(s, d, m) for s, ms in expected_methods.items() for d in (25, 40) for m in ms}
    if set(grouped) != expected_groups:
        raise RunFailure("missing/unexpected measurement groups")
    for (scenario, density, method), rs in grouped.items():
        per_round = queries if scenario == "search" else 64 if scenario == "cold-fill" else 32 if scenario == "lookup-only" else math.ceil(queries/32)
        if len(rs) != per_round * repeats:
            raise RunFailure("incomplete sample count")
        if not any(float(r["total_ns"]) > 0 for r in rs):
            raise RunFailure("clock could not resolve this measurement group")
        expected_ops = queries if scenario in ("search", "warm-hit", "mixed-pressure") else 64 if scenario == "cold-fill" else 8192
        for repeat in range(repeats):
            if sum(int(r["operations"]) for r in rs if int(r["repeat"]) == repeat) != expected_ops:
                raise RunFailure("incomplete per-round operations")
    for (scenario, _, _, _), methods in pairs.items():
        if set(methods) != set(expected_methods[scenario]):
            raise RunFailure("unpaired measurements")
        left, right = methods.values()
        # Equal cost is a runtime consistency check, not an independent optimality proof.
        if int(left["operations"]) != int(right["operations"]) or not math.isclose(float(left["cost_sum"]), float(right["cost_sum"]), rel_tol=1e-9, abs_tol=1e-9):
            raise RunFailure("paired outputs disagree")
        if scenario != "search" and (left["checksum"] != right["checksum"] or left["path_nodes"] != right["path_nodes"]):
            raise RunFailure("cache copy/consumption differs")
    summary = {"quantile_method": "nearest rank ceil(q*N), no interpolation", "speed_gate": None,
               "clock": {"samples": len(calibration), "minimum_positive_ns": min(v for v in calibration if v > 0),
                         "empty_pair_p50_ns": percentile(calibration, .5), "empty_pair_p99_ns": percentile(calibration, .99),
                         "interpretation": "Clock-pair elapsed observations, not guaranteed resolution or a constant to subtract"},
               "groups": []}
    for (scenario, density, method), rs in sorted(grouped.items()):
        values = [float(r["ns_per_operation"]) for r in rs]
        total_ops = sum(int(r["operations"]) for r in rs)
        total_ns = sum(float(r["total_ns"]) for r in rs)
        hits = sum(int(r["hits"]) for r in rs)
        repeated = sum(int(r["scheduled_repeats"]) for r in rs)
        repeat_hits = sum(int(r["repeat_hits"]) for r in rs)
        is_cache = method == "cache-copy-consume"
        if is_cache and scenario == "cold-fill" and hits != 0:
            raise RunFailure("cold-fill was not cold")
        if is_cache and scenario == "warm-hit" and hits != total_ops:
            raise RunFailure("primed warm workload did not hit")
        per_round = []
        for repeat in range(repeats):
            selected = [r for r in rs if int(r["repeat"]) == repeat]
            per_round.append({"repeat": repeat, "total_ns": sum(float(r["total_ns"]) for r in selected),
                              "operations": sum(int(r["operations"]) for r in selected)})
        summary["groups"].append({"scenario": scenario, "density_percent_threshold": density, "method": method,
            "sample_unit": "single query incl returned path consumption" if all(int(r["operations"]) == 1 for r in rs)
                else "batch mean ns/operation, NOT individual-request latency",
            "samples": len(rs), "operations": total_ops, "weighted_mean_ns": total_ns/total_ops,
            "p50_sample_ns": percentile(values,.5), "p95_sample_ns": percentile(values,.95), "p99_sample_ns": percentile(values,.99),
            "zero_duration_samples": sum(float(r["total_ns"]) == 0 for r in rs),
            "samples_at_or_below_empty_clock_p50": sum(float(r["total_ns"]) <= percentile(calibration,.5) for r in rs),
            "total_hits": hits if is_cache else None, "total_hit_rate": hits/total_ops if is_cache else None,
            "scheduled_repeat_requests": repeated if is_cache else None,
            "scheduled_repeat_hit_rate": repeat_hits/repeated if is_cache and repeated else None,
            "mean_path_nodes": sum(int(r["path_nodes"]) for r in rs)/total_ops,
            "mean_expanded": sum(int(r["expanded"]) for r in rs)/total_ops,
            "found": sum(int(r["found"]) for r in rs), "unreachable": sum(int(r["unreachable"]) for r in rs),
            "invalid": sum(int(r["invalid"]) for r in rs),
            "total_pops": sum(int(r["pops"]) for r in rs), "total_stale_pops": sum(int(r["stale_pops"]) for r in rs),
            "rounds": per_round})
    validation = {"sample_schema_and_pairing": "PASS", "input_rows": sum(map(len,inputs.values())),
                  "timing_rows": len(timing), "calibration_rows": len(calibration),
                  "optimality_scope": "Independent Bellman-Ford is in small-grid functional tests, not these 100x100 performance rows",
                  "latency_scope": "Search/cold-fill individual calls; warm/mixed/lookup-only batch means"}
    return summary, validation


def read_optional(path: str) -> str | None:
    try:
        return Path(path).read_text().strip()
    except OSError:
        return None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--compiler", default=os.environ.get("CXX", "cl" if os.name == "nt" else "g++"))
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--queries", type=int, default=1000)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--seed", type=int, default=20261004)
    args = parser.parse_args(argv)
    if args.timeout <= 0 or not math.isfinite(args.timeout) or not 64 <= args.queries <= 5000 or not 2 <= args.repeats <= 20 or not 0 <= args.seed < 2**64-40:
        raise RunFailure("invalid bounded benchmark options")
    output = args.output_dir.absolute()
    if output.exists() or output.is_symlink():
        raise RunFailure(f"output already exists; refusing overwrite: {output}")
    compiler = shutil.which(args.compiler)
    if not compiler:
        raise RunFailure(f"compiler unavailable (not skipped): {args.compiler}")
    if digest(LEGACY) != LEGACY_SHA256:
        raise RunFailure("protected historical result hash mismatch")
    tracked = [SOURCE, Path(__file__).resolve(), ROOT/"scripts/test_benchmark.py", LEGACY]
    before = {str(p.relative_to(REPO)): digest(p) for p in tracked}
    create_output_dir(output)
    commands: list[dict] = []
    provenance = {"state": "RUNNING", "started_utc": datetime.now(timezone.utc).isoformat(),
        "system": platform.platform(), "machine": platform.machine(), "python": sys.version,
        "cpu_model": next((s.split(":",1)[1].strip() for s in (read_optional("/proc/cpuinfo") or "").splitlines() if s.startswith("model name")), None),
        "cpu_quota": read_optional("/sys/fs/cgroup/cpu.max"), "memory_limit": read_optional("/sys/fs/cgroup/memory.max"),
        "cpuset": read_optional("/sys/fs/cgroup/cpuset.cpus.effective"), "compiler": compiler,
        "options": {"queries":args.queries,"repeats":args.repeats,"seed":args.seed,"timeout_seconds":args.timeout},
        "source_hashes_before": before, "historical_result_unchanged": None,
        "contract": {"grid":"100x100 performance; 3x3 exhaustive and 5x5 functional", "orthogonal_cost":1,
            "diagonal_cost":"sqrt(2), double", "performance_corner_cutting":False,"cache_capacity":128,
            "cache_key":"start, goal, profile, nav epoch; cache scoped to one dataset", "ownership":"returned independent vector",
            "timing":"steady_clock wall elapsed includes path consumption; no fixed fake hit time; lookup-only separate",
            "workload":"cold 64 distinct keys; warm 64 primed keys; mixed 256-key pool plus every fourth delayed-repeat request",
            "warmup":"32 search queries per algorithm, excluded; warm-hit prefilled outside timing", "measurement_order":"alternates algorithm order per query/batch and round"},
        "not_verified":["UE/NavMesh","production Bot/AI scheduling and AOI","Linux perf","other CPU/standard libraries","Windows/MSVC unless this actual run records cl"]}
    write_json(output/"provenance.json",provenance)
    def execute(label: str, command: list[str], expected: int = 0, cwd: Path | None = None) -> dict:
        record=run_command(command,args.timeout,cwd);record["label"]=label;record["expected_returncode"]=expected
        commands.append(record);write_json(output/"commands.json",commands);require_command(record,expected);return record
    try:
        with tempfile.TemporaryDirectory(prefix="astar-measurement-") as temporary:
            temp=Path(temporary);exe=temp/("astar.exe" if os.name=="nt" else "astar")
            msvc=Path(compiler).stem.lower()=="cl"
            if msvc:
                version=run_command([compiler],args.timeout,temp);version["label"]="compiler_version";commands.append(version)
                if version["timed_out"]: raise RunFailure("compiler version timed out")
                build=[compiler,"/nologo","/utf-8","/O2","/std:c++17","/EHsc","/W4","/WX",str(SOURCE),f"/Fe:{exe}",f"/Fo:{temp}/"]
            else:
                execute("compiler_version",[compiler,"--version"])
                build=[compiler,"-std=c++17","-O2","-Wall","-Wextra","-Wformat=2","-Werror","-pedantic",str(SOURCE),"-o",str(exe)]
            execute("compile",build,cwd=temp);provenance["binary_sha256"]=digest(exe)
            normal=execute("functional",[str(exe),"--self-test"])
            if not re.search(r"SELF_TEST passed=13 failed=0 exhaustive_queries=23040",normal["stdout"]): raise RunFailure("functional summary missing")
            negative=execute("negative_lru_no_touch",[str(exe),"--self-test","--mutant-no-touch"],1)
            if "FAIL lru_hit_touches_recency" not in negative["stdout"] or "passed=12 failed=1" not in negative["stdout"]: raise RunFailure("mutation did not fail for intended reason")
            execute("runner_contract_tests",[sys.executable,"-B",str(ROOT/"scripts/test_benchmark.py")],cwd=temp)
            contract=REPO/"evidence/algorithms/astar-contract"
            execute("existing_astar_contract_examples",[sys.executable,"-B",str(contract/"astar_contract.py")],cwd=temp)
            execute("existing_astar_contract_tests",[sys.executable,"-B",str(contract/"test_astar_contract.py")],cwd=temp)
            execute("benchmark",[str(exe),"--csv",str(output/"samples.csv"),"--queries",str(args.queries),"--repeats",str(args.repeats),"--seed",str(args.seed)],cwd=temp)
        with (output/"samples.csv").open(newline="",encoding="utf-8") as source:
            summary,validation=summarize(list(csv.DictReader(source)),args.queries,args.repeats)
        validation.update({"functional":"13 groups; 23040 query contracts x 2 implementations; both corner policies", "negative_lru_no_touch":"expected exit 1; exactly the recency test failed", "runner_tests":"PASS", "existing_astar_contract":"PASS"})
        write_json(output/"summary.json",summary);write_json(output/"validation.json",validation)
        after={str(p.relative_to(REPO)):digest(p) for p in tracked};provenance["source_hashes_after"]=after
        if before!=after: raise RunFailure("source or protected historical result changed during run")
        provenance["historical_result_unchanged"]=True;provenance["state"]="PASS_FUNCTIONAL_AND_CAPTURED_OBSERVATIONS"
        provenance["samples_sha256"]=digest(output/"samples.csv")
        print(f"PASS functional contracts and captured observations: {output}")
        return 0
    except Exception as error:
        provenance["state"]="FAIL";provenance["error"]=str(error)
        raise
    finally:
        provenance["finished_utc"]=datetime.now(timezone.utc).isoformat()
        write_json(output/"commands.json",commands);write_json(output/"provenance.json",provenance)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RunFailure, OSError, ValueError, KeyError) as error:
        print(f"FAIL: {error}",file=sys.stderr)
        raise SystemExit(1)
