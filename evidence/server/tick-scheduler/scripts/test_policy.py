#!/usr/bin/env python3
"""Runner failure semantics and exact-reference contract tests, not speed assertions."""
import copy
from fractions import Fraction
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).with_name("run_policy.py").resolve()
spec = importlib.util.spec_from_file_location("tick_policy_runner", SCRIPT)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

def valid_reference_rows():
    rows = []
    for name, config, inputs in runner.fixtures():
        phase = Fraction(0)
        for index, raw in enumerate(inputs):
            expected, phase = runner.reference_step(config, phase, raw)
            rows.append({"scenario": name, "step": str(index), **{key: str(value) for key, value in expected.items()}})
    return rows

class RunnerContracts(unittest.TestCase):
    def test_existing_directory_is_rejected_and_unchanged(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)/"existing"
            output.mkdir()
            marker = output/"original.txt"
            marker.write_bytes(b"retain exact bytes\n")
            before = runner.digest(marker)
            record = runner.run_command([sys.executable, "-B", str(SCRIPT), "--output-dir", str(output)], 10)
            self.assertNotEqual(record["returncode"], 0)
            self.assertIn("refusing overwrite", record["stderr"])
            self.assertEqual(before, runner.digest(marker))
            self.assertEqual([path.name for path in output.iterdir()], ["original.txt"])

    def test_missing_compiler_is_failure_not_skip(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)/"new"
            record = runner.run_command([sys.executable, "-B", str(SCRIPT), "--output-dir", str(output),
                                         "--compiler", str(Path(temporary)/"absent-compiler")], 10)
            self.assertNotEqual(record["returncode"], 0)
            self.assertIn("compiler unavailable (not skipped)", record["stderr"])
            self.assertFalse(output.exists())

    def test_launch_failure_is_a_recorded_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            record = runner.run_command([str(Path(temporary)/"absent-program")], 10)
            self.assertIn("launch_error", record)
            self.assertIsNone(record["returncode"])
            with self.assertRaises(runner.RunFailure):
                runner.require_command(record)

    def test_timeout_keeps_partial_output_and_fails(self):
        record = runner.run_command([sys.executable, "-u", "-c", "import time; print('started'); time.sleep(5)"], 1)
        self.assertTrue(record["timed_out"])
        self.assertIn("started", record["stdout"])
        with self.assertRaises(runner.RunFailure):
            runner.require_command(record)

    def test_nonzero_does_not_pass(self):
        record = runner.run_command([sys.executable, "-c", "raise SystemExit(7)"], 10)
        self.assertEqual(record["returncode"], 7)
        with self.assertRaises(runner.RunFailure):
            runner.require_command(record)

    def test_marked_article_example_must_be_unique_and_complete(self):
        block = "```cpp\n// tick-policy: complete-example\nint main() {}\n```\n"
        self.assertIn("int main()", runner.extract_example(block))
        for text in ("", block+block, block.replace("int main()", "void helper()")):
            with self.assertRaises(runner.RunFailure):
                runner.extract_example(text)

    def test_reference_has_no_frequency_truncation(self):
        first, phase = runner.reference_step((60, 1000, runner.MAX_DELTA), Fraction(0), 16666666)
        second, phase = runner.reference_step((60, 1000, runner.MAX_DELTA), phase, 1)
        self.assertEqual((first["executed"], first["phase_after"]), (0, 999999960))
        self.assertEqual((second["executed"], second["phase_after"]), (1, 20))
        self.assertEqual(phase, Fraction(1, 50_000_000))

    def test_reference_rejects_invalid_input(self):
        for raw in (-1, runner.I64_MAX+1):
            with self.assertRaises(runner.RunFailure):
                runner.reference_step((60, 3, runner.MAX_DELTA), Fraction(0), raw)
        with self.assertRaises(runner.RunFailure):
            runner.reference_step((60, 0, runner.MAX_DELTA), Fraction(0), 1)

    def test_complete_reference_fixture_is_accepted(self):
        summary, validation = runner.validate_rows(valid_reference_rows())
        self.assertEqual(validation["groups"], 18)
        self.assertFalse(validation["cpu_performance_claim"])
        group = next(value for value in summary["groups"] if value["scenario"] == "max_elapsed")
        self.assertEqual(group["raw_ns_total_decimal"], str(runner.I64_MAX+1))

    def test_missing_row_does_not_pass(self):
        rows = valid_reference_rows()
        with self.assertRaisesRegex(runner.RunFailure, "missing"):
            runner.validate_rows(rows[:-1])

    def test_duplicate_identity_does_not_pass(self):
        rows = valid_reference_rows()
        rows[-1] = copy.deepcopy(rows[-2])
        with self.assertRaisesRegex(runner.RunFailure, "identity"):
            runner.validate_rows(rows)

    def test_wrong_fraction_is_rejected(self):
        rows = valid_reference_rows()
        selected = next(row for row in rows if row["scenario"] == "fractional_drop" and row["step"] == "0")
        selected["phase_after"] = "0"
        with self.assertRaisesRegex(runner.RunFailure, "phase_after"):
            runner.validate_rows(rows)

    def test_wrong_counter_denominator_is_rejected(self):
        rows = valid_reference_rows()
        selected = next(row for row in rows if row["scenario"] == "cap3_spike" and row["step"] == "0")
        selected["catchup_iteration"] = "2"
        with self.assertRaisesRegex(runner.RunFailure, "catchup_iteration"):
            runner.validate_rows(rows)

    def test_non_integer_fields_and_unexpected_input_are_rejected(self):
        for bad in ("nan", "-1", "1e9", "01"):
            rows = valid_reference_rows()
            rows[0]["raw_ns"] = bad
            with self.assertRaisesRegex(runner.RunFailure, "unsigned decimal"):
                runner.validate_rows(rows)
        rows = valid_reference_rows()
        rows[0]["raw_ns"] = str(runner.I64_MAX+1)
        with self.assertRaisesRegex(runner.RunFailure, "reference mismatch"):
            runner.validate_rows(rows)

    def test_any_failure_is_not_a_successful_mutation(self):
        stdout = "\n".join("PASS "+name for name in runner.TEST_NAMES)
        record = {"argv": [], "returncode": 1, "timed_out": False,
                  "stdout": stdout+"\nSELF_TEST passed=17 failed=0 mode=cap_after\n"}
        with self.assertRaisesRegex(runner.RunFailure, "unexpected reasons"):
            runner.check_self_test(record, "cap_after", runner.MUTATIONS["cap_after"][1])

if __name__ == "__main__":
    unittest.main(verbosity=2)
