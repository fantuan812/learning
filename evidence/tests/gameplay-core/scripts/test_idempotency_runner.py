#!/usr/bin/env python3
"""Linux runner regressions: real subprocesses, real unittest, fresh temp files.

The success fixture is the actual SQLite model. Failure/timeout fixtures execute
real Python tests or sleep; only unsupported-version metadata is simulated.
No assert statements: unittest checks remain active under python -O.
"""
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
REAL_SOURCE = HERE.parent / "src/idempotency_sqlite.py"
SUCCESS = b"# runner_exit_code: 0 (both nonempty unittest runs verified)"


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="learning idem runner ")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.repo = self.base / "fixture repo"
        self.core = self.repo / "evidence/tests/gameplay-core"
        (self.core / "scripts").mkdir(parents=True)
        (self.core / "src").mkdir()
        (self.core / "results").mkdir()
        self.runner = self.core / "scripts/run_idempotency.sh"
        self.source = self.core / "src/idempotency_sqlite.py"
        shutil.copyfile(HERE / "run_idempotency.sh", self.runner)
        shutil.copyfile(REAL_SOURCE, self.source)
        self.old_raw = self.core / "results/idempotency_sqlite_linux.txt"
        self.old_raw.write_bytes(b"synthetic historical raw: must not be overwritten\n")
        self.output = self.base / "fresh output.log"
        self.calls = 0

    def invoke(self, output=None, extra_env=None, arguments=None):
        self.calls += 1
        env = os.environ.copy()
        env["PYTHON"] = sys.executable
        env["IDEMPOTENCY_TIMEOUT_SECONDS"] = "10"
        if extra_env:
            env.update(extra_env)
        target = self.output if output is None else output
        command = ["bash", str(self.runner), *(arguments if arguments is not None else [str(target)])]
        hashes = {str(path.relative_to(self.repo)): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in (self.runner, self.source, self.old_raw)}
        result = subprocess.run(command, capture_output=True, env=env, timeout=25)
        # Keep exact child streams in the calling test log, even for expected failures.
        print(f"\n# case: {self.id()} call={self.calls}", flush=True)
        print(f"# command: {shlex.join(command)}", flush=True)
        print(f"# fixture_sha256: {hashes}", flush=True)
        print(f"# selected_environment: PYTHON={env['PYTHON']!r} "
              f"IDEMPOTENCY_TIMEOUT_SECONDS={env['IDEMPOTENCY_TIMEOUT_SECONDS']!r} "
              f"PYTHONOPTIMIZE={env.get('PYTHONOPTIMIZE')!r}", flush=True)
        for name, data in (("stdout", result.stdout), ("stderr", result.stderr)):
            sys.stdout.buffer.write(f"# {name}_begin\n".encode())
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.write(f"\n# {name}_end\n".encode())
            sys.stdout.buffer.flush()
        print(f"# actual_exit_code: {result.returncode}", flush=True)
        for path in (self.runner, self.source, self.old_raw):
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                             hashes[str(path.relative_to(self.repo))])
        return result

    def assert_failure(self, result, code=None):
        self.assertNotEqual(result.returncode, 0)
        if code is not None:
            self.assertEqual(result.returncode, code)
        self.assertNotIn(SUCCESS, result.stdout + result.stderr)

    def test_01_actual_sqlite_suite_runs_normal_and_optimized_from_space_paths(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(SUCCESS, result.stdout)
        self.assertEqual(self.output.read_bytes(), result.stdout)
        self.assertIn(b"optimize=0", result.stdout)
        self.assertIn(b"optimize=1", result.stdout)
        self.assertEqual(result.stdout.count(b"# verified_tests_run:"), 2)
        self.assertIn(b"test_11_eight_concurrent_connections_commit_one_effect", result.stdout)
        self.assertIn(b"test_26_real_trigger_zero_terminal_rows", result.stdout)

    def test_02_actual_unittest_failure_in_each_mode_propagates(self):
        for optimize in (0, 1):
            with self.subTest(optimize=optimize):
                self.source.write_text(f'''import sys, unittest
class FailingTest(unittest.TestCase):
    def test_actual_failure(self):
        self.assertNotEqual(sys.flags.optimize, {optimize}, "intentional unittest failure")
unittest.main(verbosity=2)
''')
                output = self.base / f"failure-{optimize}.log"
                result = self.invoke(output)
                self.assert_failure(result, 1)
                self.assertIn(b"FAILED (failures=1)", result.stdout)
                self.assertIn(b"# process_exit_code: 1", result.stdout)
                self.assertEqual(output.read_bytes(), result.stdout)

    def test_03_nonzero_exit_code_is_preserved(self):
        self.source.write_text("import sys\nprint('deliberate exit 23')\nsys.exit(23)\n")
        self.assert_failure(self.invoke(), 23)

    def test_04_actual_timeout_in_each_mode_is_failure(self):
        for optimize in (0, 1):
            with self.subTest(optimize=optimize):
                self.source.write_text(f'''import sys, time, unittest
class SlowTest(unittest.TestCase):
    def test_real_sleep(self):
        if sys.flags.optimize == {optimize}:
            print("entered actual sleeping test", flush=True)
            time.sleep(60)
        self.assertTrue(True)
unittest.main(verbosity=2)
''')
                result = self.invoke(self.base / f"timeout-{optimize}.log",
                                     {"IDEMPOTENCY_TIMEOUT_SECONDS": "0.5"})
                self.assert_failure(result, 124)
                self.assertIn(b"entered actual sleeping test", result.stdout)
                self.assertIn(b"# timed_out: true", result.stdout)

    def version_wrapper(self, assignment):
        wrapper = self.base / "version fixture python"
        code = ("import sys, sqlite3; " + assignment +
                "; sys.argv=sys.argv[2:]; exec(compile(sys.stdin.read(), '<runner>', 'exec'))")
        wrapper.write_text("#!/usr/bin/env bash\nexec " + shlex.quote(sys.executable) +
                           " -B -c " + shlex.quote(code) + ' "$@"\n')
        wrapper.chmod(0o755)
        return str(wrapper)

    def test_05_simulated_unsupported_python_version_preflight(self):
        result = self.invoke(extra_env={"PYTHON": self.version_wrapper("sys.version_info=(3, 10, 99)")})
        self.assert_failure(result)
        self.assertIn(b"requires Python 3.11+ and SQLite 3.35+", result.stderr)
        self.assertFalse(self.output.exists())

    def test_06_simulated_unsupported_sqlite_version_preflight(self):
        result = self.invoke(extra_env={"PYTHON": self.version_wrapper("sqlite3.sqlite_version_info=(3, 34, 99)")})
        self.assert_failure(result)
        self.assertIn(b"requires Python 3.11+ and SQLite 3.35+", result.stderr)
        self.assertFalse(self.output.exists())

    def test_07_missing_python_is_failure(self):
        self.assert_failure(self.invoke(extra_env={"PYTHON": str(self.base / "absent python")}), 127)
        self.assertFalse(self.output.exists())

    def test_08_existing_file_is_not_overwritten(self):
        self.output.write_bytes(b"keep me")
        self.assert_failure(self.invoke())
        self.assertEqual(self.output.read_bytes(), b"keep me")

    def test_09_existing_directory_is_not_reused(self):
        self.output.mkdir()
        self.assert_failure(self.invoke())
        self.assertEqual(list(self.output.iterdir()), [])

    def test_10_existing_and_dangling_symlinks_are_rejected(self):
        target = self.base / "symlink target"
        for exists in (False, True):
            with self.subTest(target_exists=exists):
                if exists:
                    target.write_bytes(b"keep target")
                self.output.symlink_to(target)
                self.assert_failure(self.invoke())
                self.assertTrue(self.output.is_symlink())
                self.assertEqual(target.exists(), exists)
                if exists:
                    self.assertEqual(target.read_bytes(), b"keep target")
                self.output.unlink()

    def test_11_old_raw_and_new_repository_outputs_are_rejected(self):
        self.assert_failure(self.invoke(self.old_raw))
        fresh = self.core / "results/new.log"
        self.assert_failure(self.invoke(fresh))
        self.assertFalse(fresh.exists())

    def test_12_symlink_parent_into_repository_is_rejected(self):
        alias = self.base / "repo alias"
        alias.symlink_to(self.core / "results", target_is_directory=True)
        self.assert_failure(self.invoke(alias / "new.log"))
        self.assertFalse((self.core / "results/new.log").exists())

    def test_13_missing_parent_and_file_parent_cannot_be_written(self):
        parent = self.base / "bad parent"
        for exists in (False, True):
            with self.subTest(parent_is_file=exists):
                if exists:
                    parent.write_bytes(b"not a directory")
                self.assert_failure(self.invoke(parent / "new.log"))
                if exists:
                    self.assertEqual(parent.read_bytes(), b"not a directory")

    def test_14_actual_log_write_limit_failure_cannot_report_success(self):
        # RLIMIT_FSIZE affects only this runner child; Python ignores SIGXFSZ
        # so a real append/flush returns EFBIG. No host-wide limits are changed.
        wrapper = self.base / "limited python"
        wrapper.write_text("#!/usr/bin/env bash\nulimit -f 1\nexec " +
                           shlex.quote(sys.executable) + ' "$@"\n')
        wrapper.chmod(0o755)
        result = self.invoke(extra_env={"PYTHON": str(wrapper)})
        self.assert_failure(result)
        self.assertTrue(self.output.exists())
        self.assertNotIn(SUCCESS, self.output.read_bytes())
        self.assertIn(b"File too large", result.stderr)

    def test_15_required_absolute_output_argument(self):
        self.assert_failure(self.invoke(arguments=[]), 64)
        self.assert_failure(self.invoke(arguments=["a", "b"]), 64)
        self.assert_failure(self.invoke(arguments=["relative.log"]))
        self.assertFalse(self.output.exists())

    def test_16_invalid_timeout_values_are_rejected(self):
        for value in ("0", "-1", "nan", "inf", "bad"):
            with self.subTest(value=value):
                self.assert_failure(self.invoke(extra_env={"IDEMPOTENCY_TIMEOUT_SECONDS": value}))
                self.assertFalse(self.output.exists())

    def test_17_real_empty_unittest_suite_is_not_success(self):
        self.source.write_text("import unittest\nunittest.TextTestRunner(verbosity=2).run(unittest.TestSuite())\n")
        result = self.invoke()
        self.assert_failure(result, 65)
        self.assertIn(b"Ran 0 tests", result.stdout)
        self.assertIn(b"# process_exit_code: 0", result.stdout)

    def test_18_missing_completion_report_is_not_success(self):
        self.source.write_text("print('program exited without running unittest')\n")
        self.assert_failure(self.invoke(), 65)

    def test_19_real_failed_unittest_with_exit_zero_is_not_success(self):
        self.source.write_text('''import unittest
class FailingTest(unittest.TestCase):
    def test_failure(self):
        self.fail("real failure with deliberately missing process exit propagation")
unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(FailingTest))
''')
        result = self.invoke()
        self.assert_failure(result, 65)
        self.assertIn(b"FAILED (failures=1)", result.stdout)
        self.assertIn(b"# process_exit_code: 0", result.stdout)

    def test_20_real_suite_with_corrupt_completion_report_is_not_success(self):
        self.source.write_text('''import sys, unittest
class CorruptStream:
    def write(self, text):
        sys.stderr.write(text.replace("Ran 1 test", "BROKEN completion"))
    def flush(self):
        sys.stderr.flush()
class PassingTest(unittest.TestCase):
    def test_pass(self):
        self.assertEqual(1+1, 2)
unittest.TextTestRunner(stream=CorruptStream(), verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(PassingTest))
''')
        result = self.invoke()
        self.assert_failure(result, 65)
        self.assertIn(b"BROKEN completion", result.stdout)

    def test_21_normal_and_optimized_suite_count_must_match(self):
        self.source.write_text('''import sys, unittest
class PassingTest(unittest.TestCase):
    def test_pass(self):
        self.assertEqual(1+1, 2)
suite = unittest.TestSuite(PassingTest("test_pass") for _ in range(1+sys.flags.optimize))
result = unittest.TextTestRunner(verbosity=2).run(suite)
sys.exit(0 if result.wasSuccessful() else 1)
''')
        result = self.invoke()
        self.assert_failure(result, 65)
        self.assertIn(b"normal_optimized_test_count_mismatch", result.stdout)

    def test_22_inherited_optimization_cannot_change_requested_modes(self):
        for inherited in ("1", "2"):
            with self.subTest(inherited=inherited):
                result = self.invoke(self.base / f"inherited-{inherited}.log",
                                     {"PYTHONOPTIMIZE": inherited})
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(SUCCESS, result.stdout)
                self.assertEqual(result.stdout.count(b"optimize=0"), 1)
                self.assertEqual(result.stdout.count(b"optimize=1"), 1)
                self.assertNotIn(b"optimize=2", result.stdout)

    def test_23_optimized_empty_missing_and_failed_reports_are_not_success(self):
        fixtures = {
            "empty": "unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite())",
            "missing": "print('missing optimized report')",
            "failed": "unittest.TextTestRunner(verbosity=2).run(unittest.FunctionTestCase(lambda: 1/0))",
            "corrupt": "sys.stderr.write('Ran INVALID tests in 0.01s\\n\\nOK\\n')",
        }
        for name, optimized_action in fixtures.items():
            with self.subTest(report=name):
                self.source.write_text(
                    "import sys, unittest\n"
                    "if sys.flags.optimize:\n    " + optimized_action + "\n"
                    "else:\n    unittest.TextTestRunner(verbosity=2).run("
                    "unittest.FunctionTestCase(lambda: None))\n")
                result = self.invoke(self.base / f"optimized-{name}.log")
                self.assert_failure(result, 65)
                self.assertEqual(result.stdout.count(b"# process_exit_code: 0"), 2)



if __name__ == "__main__":
    print(f"runner_regression_python={sys.version.split()[0]} optimize={sys.flags.optimize}", flush=True)
    unittest.main(verbosity=2)
