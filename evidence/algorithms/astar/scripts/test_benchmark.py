#!/usr/bin/env python3
"""Runner safety and measurement-contract negative tests; no speed assertions."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).with_name("run_benchmark.py").resolve()
spec = importlib.util.spec_from_file_location("astar_measurement_runner",SCRIPT)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class RunnerContract(unittest.TestCase):
    def test_existing_output_is_rejected_and_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            output=Path(temporary)/"existing";output.mkdir();marker=output/"original.txt"
            marker.write_bytes(b"must remain unchanged\n")
            before=runner.digest(marker)
            record=runner.run_command([sys.executable,"-B",str(SCRIPT),"--output-dir",str(output)],10)
            self.assertNotEqual(record["returncode"],0)
            self.assertIn("refusing overwrite",record["stderr"])
            self.assertEqual(before,runner.digest(marker))
            self.assertEqual([p.name for p in output.iterdir()],["original.txt"])

    def test_missing_compiler_fails_not_skips(self):
        with tempfile.TemporaryDirectory() as temporary:
            output=Path(temporary)/"new"
            record=runner.run_command([sys.executable,"-B",str(SCRIPT),"--output-dir",str(output),
                                       "--compiler",str(Path(temporary)/"nonexistent-compiler")],10)
            self.assertNotEqual(record["returncode"],0)
            self.assertIn("compiler unavailable (not skipped)",record["stderr"])
            self.assertFalse(output.exists())

    def test_timeout_is_a_failure_with_partial_output(self):
        record=runner.run_command([sys.executable,"-u","-c","import time; print('started'); time.sleep(5)"],.2)
        self.assertTrue(record["timed_out"])
        self.assertIn("started",record["stdout"])
        with self.assertRaises(runner.RunFailure): runner.require_command(record)

    def test_nonzero_is_a_failure(self):
        record=runner.run_command([sys.executable,"-c","raise SystemExit(7)"],10)
        self.assertEqual(record["returncode"],7)
        with self.assertRaises(runner.RunFailure): runner.require_command(record)

    def test_nearest_rank_quantiles(self):
        self.assertEqual(runner.percentile([4.,1.,3.,2.],.5),2.)
        self.assertEqual(runner.percentile([4.,1.,3.,2.],.99),4.)
        for values in ([],[float("nan")],[float("inf")]):
            with self.assertRaises(runner.RunFailure): runner.percentile(values,.5)

    def test_missing_measurement_cannot_pass(self):
        with self.assertRaises(runner.RunFailure): runner.summarize([],64,2)

    def test_invalid_timing_denominator_is_rejected(self):
        row={"row_type":"timing","scenario":"warm-hit","density":"25","method":"cache-copy-consume",
             "repeat":"0","first_query":"0","operations":"32","total_ns":"640","ns_per_operation":"640",
             "hits":"32","scheduled_repeats":"32","repeat_hits":"32","cost_sum":"1","checksum":"1","path_nodes":"1"}
        with self.assertRaisesRegex(runner.RunFailure,"batch denominator"): runner.summarize([row],64,2)

    def test_nonfinite_duration_is_rejected(self):
        row={"row_type":"calibration","total_ns":"nan"}
        with self.assertRaisesRegex(runner.RunFailure,"calibration"): runner.summarize([row],64,2)

    def test_duplicate_sample_is_rejected(self):
        row={"row_type":"timing","scenario":"search","density":"25","method":"heap-copy-consume",
             "repeat":"0","first_query":"0","operations":"1","total_ns":"20","ns_per_operation":"20",
             "hits":"0","scheduled_repeats":"0","repeat_hits":"0","cost_sum":"1","checksum":"1","path_nodes":"1"}
        with self.assertRaisesRegex(runner.RunFailure,"duplicate"): runner.summarize([row,row],64,2)

    def test_hit_count_cannot_exceed_requests(self):
        row={"row_type":"timing","scenario":"warm-hit","density":"25","method":"cache-copy-consume",
             "repeat":"0","first_query":"0","operations":"32","total_ns":"640","ns_per_operation":"20",
             "hits":"33","scheduled_repeats":"32","repeat_hits":"32","cost_sum":"1","checksum":"1","path_nodes":"1"}
        with self.assertRaisesRegex(runner.RunFailure,"counts"): runner.summarize([row],64,2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
