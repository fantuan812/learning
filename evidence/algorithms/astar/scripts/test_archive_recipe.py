#!/usr/bin/env python3
"""Execute the README's archive recipes on isolated copies; never rerun C++.

Only the standard library is needed. Each subprocess gets the exact extracted
Python body through stdin, with the documented -B - arguments and this Python
interpreter. Additional -O or inherited PYTHONOPTIMIZE tests must not turn a
negative control green. JSONL diagnostics retain real inputs and process results.
"""
from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock


BASE = Path(__file__).resolve().parents[1]
REPO = BASE.parents[2]
LAB = Path("evidence/algorithms/astar")
RESULT = Path("results/astar-measurement-2026-10-04")
RAW_SHA256 = "d3db5d3b3e67d33415e6b38639ec6a4380e9f72eeb8eaea0e687f3e965c51a1b"
GZIP_SHA256 = "a995624d3476a2994123e4f7d3274e19249c3706078a634ac7a008f0dc9e7019"
MODES = {
    "normal": ([], None, 0),
    "explicit-O": (["-O"], None, 1),
    "inherited-PYTHONOPTIMIZE=1": ([], "1", 1),
}
CASE_TIMEOUT = 30.0
LOG = None


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def emit(record: dict) -> None:
    line = json.dumps(record, ensure_ascii=True, sort_keys=True, allow_nan=False)
    print(line, flush=True)
    if LOG is not None:
        LOG.write(line + "\n")
        LOG.flush()


def snapshot(root: Path) -> dict:
    return {p.relative_to(root).as_posix(): {"sha256": sha256(p.read_bytes()),
                                           "bytes": p.stat().st_size}
            for p in sorted(root.rglob("*")) if p.is_file()}


def open_new_log(path: Path):
    # Resolve only for containment; opening the original path with x also keeps
    # O_EXCL's no-follow protection if a symlink appears after the precheck.
    target = path.expanduser().absolute()
    if target.exists() or target.is_symlink():
        raise FileExistsError("diagnostic log already exists; refusing overwrite")
    if target.resolve().is_relative_to(REPO):
        raise ValueError("--log-jsonl must be outside the repository")
    return target.open("x", encoding="utf-8", newline="\n")


def extract_recipes(text: str) -> dict:
    """Fail closed on absent/duplicate/unknown markers or a changed shell wrapper."""
    expected = [f"<!-- astar-archive-recipe:{name}:{edge} -->"
                for name in ("restore", "recompute") for edge in ("start", "end")]
    markers = re.findall(r"<!-- astar-archive-recipe:[^\n]*", text)
    if markers != expected or text.count("<!-- astar-archive-recipe:") != len(expected):
        raise ValueError("archive recipe markers missing, duplicated, malformed, or out of order")
    recipes = {}
    for name in ("restore", "recompute"):
        start = f"<!-- astar-archive-recipe:{name}:start -->"
        end = f"<!-- astar-archive-recipe:{name}:end -->"
        block = text.split(start, 1)[1].split(end, 1)[0].strip()
        match = re.fullmatch(r"```bash\n(python3 -B - <<'PY')\n(.+)\nPY\n```", block, re.DOTALL)
        if match is None or "```" in match[2] or "\nPY\n" in match[2]:
            raise ValueError(f"archive recipe command structure changed: {name}")
        code = match[2] + "\n"
        compile(code, f"README.md:{name}", "exec")
        recipes[name] = {"documented_command": match[1], "code": code,
                         "code_sha256": sha256(code.encode("utf-8"))}
    return recipes


def mode_environment(mode: str) -> dict:
    env = os.environ.copy()
    for key in ("PYTHONOPTIMIZE", "ASTAR_RESULT_DIR", "ASTAR_RESTORE_CSV"):
        env.pop(key, None)
    if MODES[mode][1] is not None:
        env["PYTHONOPTIMIZE"] = MODES[mode][1]
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env["PYTHONUTF8"] = "1"
    return env


def run_python(mode: str, code: str, cwd: Path, env: dict, details: dict) -> dict:
    argv = [sys.executable, "-B", *MODES[mode][0], "-"]
    started = time.perf_counter()
    record = {"kind": "subprocess", "mode": mode, "argv": argv, "cwd": str(cwd),
              "stdin_sha256": sha256(code.encode("utf-8")),
              "environment": {key: env.get(key) for key in (
                  "PYTHONOPTIMIZE", "PYTHONDONTWRITEBYTECODE", "PYTHONUTF8",
                  "ASTAR_RESULT_DIR", "ASTAR_RESTORE_CSV")},
              "timeout_seconds": CASE_TIMEOUT, **details}
    try:
        result = subprocess.run(argv, input=code, cwd=cwd, env=env, capture_output=True,
                                text=True, encoding="utf-8", timeout=CASE_TIMEOUT)
        record.update(returncode=result.returncode, timed_out=False,
                      stdout=result.stdout, stderr=result.stderr)
    except subprocess.TimeoutExpired as error:
        def decoded(value):
            return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value or ""
        record.update(returncode=None, timed_out=True,
                      stdout=decoded(error.stdout), stderr=decoded(error.stderr))
    except OSError as error:
        record.update(returncode=None, timed_out=False, stdout="", stderr=str(error),
                      launch_error=type(error).__name__)
    record["elapsed_seconds"] = time.perf_counter() - started
    emit(record)
    return record


class DiagnosticLogTests(unittest.TestCase):
    def test_existing_log_is_rejected_and_preserved(self):
        with tempfile.TemporaryDirectory(prefix="astar existing log ") as temporary:
            target = Path(temporary) / "existing.jsonl"
            original = b"existing diagnostic bytes\x00\xff\n"
            target.write_bytes(original)
            with self.assertRaisesRegex(FileExistsError, "diagnostic log already exists"):
                open_new_log(target)
            self.assertEqual(target.read_bytes(), original)
            emit({"kind": "diagnostic-log-negative", "case": "existing-file",
                  "preserved_sha256": sha256(target.read_bytes())})

    def test_symlink_guard_runs_before_resolution(self):
        # A platform-independent unit probe, not a claim that Windows grants
        # this process permission to create real filesystem symlinks.
        with tempfile.TemporaryDirectory(prefix="astar symlink guard ") as temporary:
            target = Path(temporary) / "dangling.jsonl"
            with mock.patch.object(Path, "is_symlink", return_value=True), \
                    mock.patch.object(Path, "resolve", side_effect=AssertionError("must not resolve a symlink")):
                with self.assertRaisesRegex(FileExistsError, "diagnostic log already exists"):
                    open_new_log(target)
            self.assertFalse(target.exists())
            emit({"kind": "diagnostic-log-negative", "case": "symlink-before-resolution",
                  "probe": "mocked filesystem state; no symlink privileges required"})

    def test_new_external_log_is_exclusively_created(self):
        with tempfile.TemporaryDirectory(prefix="astar new log ") as temporary:
            target = Path(temporary) / "new.jsonl"
            with open_new_log(target) as output:
                output.write("diagnostic\n")
            self.assertEqual(target.read_bytes(), b"diagnostic\n")
            with self.assertRaises(FileExistsError):
                open_new_log(target)
            self.assertEqual(target.read_bytes(), b"diagnostic\n")


class RecipeExtractionTests(unittest.TestCase):
    def test_exactly_two_executable_recipes(self):
        recipes = extract_recipes((BASE / "README.md").read_text(encoding="utf-8"))
        self.assertEqual(set(recipes), {"restore", "recompute"})
        for recipe in recipes.values():
            self.assertTrue(recipe["code"].strip())
            self.assertEqual(recipe["documented_command"], "python3 -B - <<'PY'")

    def test_extraction_errors_cannot_be_silently_skipped(self):
        original = (BASE / "README.md").read_text(encoding="utf-8")
        cases = {}
        for name in ("restore", "recompute"):
            for edge in ("start", "end"):
                marker = f"<!-- astar-archive-recipe:{name}:{edge} -->"
                cases[f"missing-{name}-{edge}"] = original.replace(marker, "", 1)
                cases[f"duplicate-{name}-{edge}"] = original.replace(marker, marker + "\n" + marker, 1)
        cases.update({
            "unknown-marker": original + "\n<!-- astar-archive-recipe:other:start -->",
            "malformed-marker": original.replace("recipe:restore:start -->", "recipe:restore:start-->", 1),
            "wrong-fence": original.replace("```bash\npython3 -B - <<'PY'", "```python\npython3 -B - <<'PY'", 1),
            "missing-no-bytecode-option": original.replace("python3 -B - <<'PY'", "python3 - <<'PY'", 1),
            "missing-heredoc-end": original.replace("\nPY\n```", "\n```", 1),
        })
        for case, text in cases.items():
            with self.subTest(case=case):
                with self.assertRaises((ValueError, SyntaxError)) as failure:
                    extract_recipes(text)
                emit({"kind": "extraction-negative", "case": case,
                      "input_sha256": sha256(text.encode("utf-8")),
                      "error": str(failure.exception)})


class ArchivedRecipeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before = snapshot(BASE)
        cls.recipes = extract_recipes((BASE / "README.md").read_text(encoding="utf-8"))
        paths = [Path("scripts/run_benchmark.py"), *(
            RESULT / name for name in ("samples.csv.gz", "provenance.json", "summary.json", "validation.json"))]
        cls.original = {path: (BASE / path).read_bytes() for path in paths}
        cls.archive = cls.original[RESULT / "samples.csv.gz"]
        cls.raw = gzip.decompress(cls.archive)
        if sha256(cls.archive) != GZIP_SHA256 or sha256(cls.raw) != RAW_SHA256:
            raise ValueError("real archived input fingerprint changed; not a synthetic success fixture")
        if len(list(csv.DictReader(io.StringIO(cls.raw.decode("utf-8"), newline="")))) != 30008:
            raise ValueError("real archived input must contain 30,008 data records plus its header")
        emit({"kind": "suite-inputs", "python": sys.version, "platform": sys.platform,
              "repository_inputs": cls.before, "recipes": cls.recipes,
              "raw_csv": {"sha256": sha256(cls.raw), "bytes": len(cls.raw), "data_records": 30008}})

    @classmethod
    def tearDownClass(cls):
        after = snapshot(BASE)
        emit({"kind": "source-preservation", "unchanged": after == cls.before,
              "before": cls.before, "after": after})
        if after != cls.before:
            raise AssertionError("archive tests changed repository files")

    def exercise(self, recipe: str, mode: str, case: str, *, expected_error: str | None = None,
                 mutate=None, target_policy: str = "new", relocated: bool = False):
        with tempfile.TemporaryDirectory(prefix="astar archive recipes ") as temporary:
            root = Path(temporary).resolve()
            self.assertFalse(root.is_relative_to(REPO))
            repo = root / "repository"
            lab = repo / LAB
            result = root / "input copy" if relocated else lab / RESULT
            for path, data in self.original.items():
                destination = result / path.name if path.parent == RESULT else lab / path
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
            if mutate is not None:
                mutate(result)
            target = root / "restored samples.csv"
            if target_policy == "existing":
                target.write_bytes(b"existing recovery must remain unchanged\x00\xff\n")
            elif target_policy == "inside":
                target = repo / "must-not-exist.csv"
            original_target = target.read_bytes() if target.exists() else None
            env = mode_environment(mode)
            if target_policy != "missing":
                env["ASTAR_RESTORE_CSV"] = "relative.csv" if target_policy == "relative" else str(target)
            if relocated:
                env["ASTAR_RESULT_DIR"] = str(result)
            before = snapshot(root)
            record = run_python(mode, self.recipes[recipe]["code"], repo, env, {
                "recipe": recipe, "case": case, "expected_error": expected_error,
                "fixture_inputs": before, "documented_command": self.recipes[recipe]["documented_command"]})
            self.assertFalse(record["timed_out"], "a timeout is a failed test, never the expected negative")
            self.assertNotIn("launch_error", record)
            if expected_error is None:
                self.assertEqual(record["returncode"], 0, record["stderr"])
                success = ("PASS: verified archived CSV restored to " if recipe == "restore" else
                           "PASS: CSV-derived summary and sample validation match archived observations; no C++ rerun")
                self.assertIn(success, record["stdout"])
                self.assertEqual(record["stderr"], "")
                if recipe == "restore":
                    self.assertEqual(target.read_bytes(), self.raw)
                    # The sole permitted fixture change is this external new file.
                    before[target.relative_to(root).as_posix()] = {"sha256": sha256(self.raw), "bytes": len(self.raw)}
            else:
                self.assertNotEqual(record["returncode"], 0)
                self.assertIn(expected_error, record["stderr"])
                self.assertNotIn("PASS:", record["stdout"] + record["stderr"])
            if original_target is not None:
                self.assertEqual(target.read_bytes(), original_target)
            elif expected_error is not None or recipe != "restore":
                self.assertFalse(target.exists(), "failed/read-only recipe created a recovery file")
            self.assertEqual(snapshot(root), before, "recipe wrote outside its one new recovery target")

    def test_interpreter_modes_are_real(self):
        with tempfile.TemporaryDirectory(prefix="astar mode probe ") as temporary:
            for mode, (_, _, optimize) in MODES.items():
                with self.subTest(mode=mode):
                    record = run_python(mode, "import sys; print(sys.flags.optimize)\n",
                                        Path(temporary), mode_environment(mode), {"case": "optimization-probe"})
                    self.assertFalse(record["timed_out"])
                    self.assertEqual(record["returncode"], 0)
                    self.assertEqual(record["stdout"].strip(), str(optimize))

    def test_real_archive_restores_and_recomputes(self):
        for mode in MODES:
            for recipe in self.recipes:
                for relocated in (False, True):
                    with self.subTest(mode=mode, recipe=recipe, relocated=relocated):
                        self.exercise(recipe, mode, "real-archive", relocated=relocated)

    def test_corrupt_archives_fail_before_creating_output(self):
        changed_header = bytearray(self.archive)
        changed_header[4] ^= 1  # Valid gzip, same raw data, different archive identity.
        cases = {
            "valid-gzip-altered-raw": (gzip.compress(self.raw + b"\n", mtime=0), "raw CSV SHA-256 mismatch"),
            "not-gzip": (b"not a gzip file", "BadGzipFile"),
            "truncated-gzip": (self.archive[:-8], "EOFError"),
            "valid-gzip-altered-header": (bytes(changed_header), "archive SHA-256 mismatch"),
        }
        for mode in MODES:
            for recipe in self.recipes:
                for case, (data, error) in cases.items():
                    with self.subTest(mode=mode, recipe=recipe, case=case):
                        self.exercise(recipe, mode, case, expected_error=error,
                                      mutate=lambda result, data=data: (result / "samples.csv.gz").write_bytes(data))

    def test_existing_recovery_is_rejected_and_preserved(self):
        for mode in MODES:
            with self.subTest(mode=mode):
                self.exercise("restore", mode, "existing-recovery", target_policy="existing",
                              expected_error="restore target already exists; refusing overwrite")

    def test_recovery_requires_a_configured_external_absolute_path(self):
        for mode in MODES:
            for target in ("missing", "relative", "inside"):
                with self.subTest(mode=mode, target=target):
                    self.exercise("restore", mode, f"target-{target}", target_policy=target,
                                  expected_error="set ASTAR_RESTORE_CSV" if target == "missing" else
                                  "restore target must be an external absolute path")

    def test_wrong_summary_cannot_pass(self):
        def corrupt(result):
            path = result / "summary.json"
            data = json.loads(path.read_text(encoding="utf-8"))
            data["groups"][0]["weighted_mean_ns"] = 1e12
            path.write_text(json.dumps(data), encoding="utf-8")
        for mode in MODES:
            with self.subTest(mode=mode):
                self.exercise("recompute", mode, "wrong-weighted-mean", mutate=corrupt,
                              expected_error="recomputed summary mismatch")

    def test_wrong_or_missing_csv_derived_validation_cannot_pass(self):
        keys = ("sample_schema_and_pairing", "input_rows", "timing_rows", "calibration_rows",
                "optimality_scope", "latency_scope")
        for mode in MODES:
            for key in keys:
                for missing in (False, True):
                    def corrupt(result, key=key, missing=missing):
                        path = result / "validation.json"
                        data = json.loads(path.read_text(encoding="utf-8"))
                        if missing:
                            del data[key]
                        else:
                            data[key] = "deliberately incorrect derived value"
                        path.write_text(json.dumps(data), encoding="utf-8")
                    with self.subTest(mode=mode, key=key, missing=missing):
                        self.exercise("recompute", mode, f"{'missing' if missing else 'wrong'}-derived-{key}",
                                      mutate=corrupt, expected_error=f"CSV-derived validation mismatch: {key}")

    def test_wrong_or_missing_recorded_raw_hash_cannot_pass(self):
        for mode in MODES:
            for missing in (False, True):
                def corrupt(result, missing=missing):
                    path = result / "provenance.json"
                    data = json.loads(path.read_text(encoding="utf-8"))
                    if missing:
                        del data["samples_sha256"]
                    else:
                        data["samples_sha256"] = "0" * 64
                    path.write_text(json.dumps(data), encoding="utf-8")
                with self.subTest(mode=mode, missing=missing):
                    self.exercise("recompute", mode, f"{'missing' if missing else 'wrong'}-provenance-raw-hash",
                                  mutate=corrupt, expected_error="KeyError: 'samples_sha256'" if missing else
                                  "provenance raw CSV SHA-256 mismatch")

    def test_historical_functional_status_is_not_recertified(self):
        def change_historical_fields(result):
            path = result / "validation.json"
            data = json.loads(path.read_text(encoding="utf-8"))
            for key in ("functional", "negative_lru_no_touch", "runner_tests", "existing_astar_contract"):
                data[key] = "NOT REEXECUTED BY THIS RECIPE"
            path.write_text(json.dumps(data), encoding="utf-8")
            path = result / "provenance.json"
            data = json.loads(path.read_text(encoding="utf-8"))
            data["state"] = "NOT REEXECUTED BY THIS RECIPE"
            path.write_text(json.dumps(data), encoding="utf-8")
        for mode in MODES:
            with self.subTest(mode=mode):
                self.exercise("recompute", mode, "historical-status-is-outside-recomputation",
                              mutate=change_historical_fields)


def main() -> int:
    global CASE_TIMEOUT, LOG
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case-timeout", type=float, default=30.0,
                        help="seconds per child; any timeout fails (default: 30)")
    parser.add_argument("--log-jsonl", type=Path,
                        help="optional NEW external diagnostic file; never overwrite")
    args = parser.parse_args()
    if not math.isfinite(args.case_timeout) or args.case_timeout <= 0:
        parser.error("--case-timeout must be finite and positive")
    CASE_TIMEOUT = args.case_timeout
    if args.log_jsonl is not None:
        LOG = open_new_log(args.log_jsonl)
    try:
        suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        successful = result.wasSuccessful() and result.testsRun > 0 and not result.skipped
        emit({"kind": "suite-result", "successful": successful,
              "tests_run": result.testsRun, "failures": len(result.failures),
              "errors": len(result.errors), "skipped": len(result.skipped)})
        return 0 if successful else 1
    finally:
        if LOG is not None:
            LOG.close()


if __name__ == "__main__":
    raise SystemExit(main())
