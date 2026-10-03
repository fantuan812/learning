#!/usr/bin/env python3
"""Isolated regression tests; Git mutations happen only in temporary fixtures."""
from __future__ import annotations

import contextlib
import io
import json
import os
from pathlib import Path, PureWindowsPath
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

import migrate_knowledge as migration


class MigrationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.home = Path(self.temporary.name)
        self.root = self.home / "repo"
        self.root.mkdir()
        self.run_git("init", "-q")
        self.write("old/topic.md", "# Topic\n\n[asset](../asset.png)\n[own](#topic)\n")
        self.write("asset.png", b"fixture")
        self.write("README.md", "[topic](old/topic.md)\n")
        self.write("知识/domain/README.md", "# Domain\n")
        self.graph = {"version": 1, "domains": [{"id": "systems", "entrypoint": "知识/domain/README.md"}], "documents": [{"id": "kb-topic", "path": "old/topic.md", "domain": "systems", "kind": "concept"}], "concepts": [], "relations": []}
        self.save_graph()
        self.run_git("add", ".")
        self.run_git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-qm", "fixture")
        self.operation = {"id": "kb-topic", "source": "old/topic.md", "source_domain": "systems", "domain": "systems", "kind": "concept", "target": "知识/domain/semantic/topic.md", "action": "move", "preserve_text_except_local_links": True}
        self.plan = {"version": 1, "base_commit": self.run_git("rev-parse", "HEAD").strip(), "operations": [self.operation]}
        self.plan_path = self.home / "plan.json"
        self.report_path = self.home / "report.json"
        self.save_plan()

    def run_git(self, *args):
        result = subprocess.run(["git", "-C", str(self.root), *args], capture_output=True, check=True)
        return result.stdout.decode()

    def write(self, path, content):
        destination = self.root / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(content if isinstance(content, bytes) else content.encode())

    def save_graph(self):
        self.write(".kb/knowledge-map.json", json.dumps(self.graph, ensure_ascii=False))

    def save_plan(self):
        self.plan_path.write_text(json.dumps(self.plan, ensure_ascii=False), encoding="utf-8")

    def prepare(self):
        self.save_plan()
        return migration.make_plan(self.root, self.plan_path, ".kb/knowledge-map.json")

    def cli(self, apply=False):
        self.save_plan()
        args = ["--root", str(self.root), "--plan", str(self.plan_path), "--report", str(self.report_path)]
        if apply:
            args.append("--apply")
        with contextlib.redirect_stdout(io.StringIO()):
            result = migration.main(args)
        return result, json.loads(self.report_path.read_text())

    def snapshot(self):
        return {p.relative_to(self.root).as_posix(): p.read_bytes() for p in self.root.rglob("*") if p.is_file() and ".git" not in p.relative_to(self.root).parts}

    def test_snapshot_keys_are_git_style_for_windows_paths(self):
        file = Mock()
        file.relative_to.return_value = PureWindowsPath("old/topic.md")
        file.is_file.return_value = True
        file.read_bytes.return_value = b"original bytes"
        with patch.object(Path, "rglob", return_value=[file]):
            self.assertEqual(self.snapshot(), {"old/topic.md": b"original bytes"})

    def assert_rejected(self, fragment, apply=True):
        before = self.snapshot()
        result, report = self.cli(apply=apply)
        self.assertEqual(result, 1)
        self.assertFalse(report["ok"])
        self.assertFalse(report["applied"])
        self.assertIn(fragment, json.dumps(report["errors"], ensure_ascii=False))
        self.assertEqual(self.snapshot(), before)

    def test_dry_run_is_read_only_and_inverse_proofs_match(self):
        before = self.snapshot()
        result, report = self.cli()
        self.assertEqual(result, 0, report["errors"])
        self.assertEqual(self.snapshot(), before)
        self.assertFalse(report["applied"])
        self.assertEqual(report["mode"], "dry-run")
        self.assertEqual(len(report["files"]), 2)
        self.assertEqual(len(report["inverse_transform_proof_sha256"]), 64)
        for file in report["files"]:
            self.assertEqual(file["source_sha256"], file["inverse_proof_sha256"])
            self.assertEqual(file["source_sha256"], migration.digest(before[file["source"]]))
            for edit in file["link_edits"]:
                self.assertEqual(before[file["source"]][edit["start_byte"]:edit["end_byte"]].decode(), edit["old"])

    def test_apply_moves_source_and_updates_relative_assets(self):
        result, report = self.cli(apply=True)
        self.assertEqual(result, 0, report["errors"])
        self.assertTrue(report["applied"])
        self.assertFalse((self.root / "old/topic.md").exists())
        self.assertEqual((self.root / self.operation["target"]).read_text(), "# Topic\n\n[asset](../../../asset.png)\n[own](#topic)\n")
        self.assertIn("知识/domain/semantic/topic.md", (self.root / "README.md").read_text())
        self.assertEqual(self.run_git("rev-parse", "HEAD").strip(), self.plan["base_commit"])
        self.assertEqual(self.run_git("diff", "--cached", "--name-only"), "")

    def test_all_standard_forms_and_excluded_syntax(self):
        source = '''---
link: "[front](old/topic.md)"
---
# Navigation

[bare](old/topic.md?x=1#topic "Title")
[angle](<old/topic.md#topic>)
![image](old/topic.md)
[reference][r]
![reference image][r]

[r]: <old/topic.md?x=2#topic> 'Reference title'

<a href="old/topic.md?x=1&amp;y=2#topic">HTML</a>
<img src='old/topic.md'/>
<div>
  <a href=old/topic.md>block</a>
</div>

> [quote](old/topic.md)

- [list](old/topic.md)

`[inline code](old/topic.md)`

````md
[fenced code](old/topic.md)
````

    [indented code](old/topic.md)

<!-- [comment](old/topic.md) <a href="old/topic.md"> -->

An arbitrary old/topic.md string remains unchanged.
'''
        self.write("README.md", source)
        report, snapshots, outputs, _ = self.prepare()
        self.assertTrue(report["ok"], report["errors"])
        result = outputs["README.md"][1].decode()
        self.assertIn('link: "[front](old/topic.md)"', result)
        for preserved in ('`[inline code](old/topic.md)`', '[fenced code](old/topic.md)', '    [indented code](old/topic.md)', '<!-- [comment](old/topic.md) <a href="old/topic.md"> -->', 'An arbitrary old/topic.md string remains unchanged.'):
            self.assertIn(preserved, result)
        self.assertIn('?x=1&amp;y=2#topic', result)
        self.assertIn('?x=2#topic', result)
        file = next(f for f in report["files"] if f["source"] == "README.md")
        self.assertEqual(len(file["link_edits"]), 9)
        inverse = result
        for edit in reversed(file["link_edits"]):
            inverse = inverse[:edit["output_start"]] + edit["old"] + inverse[edit["output_end"]:]
        self.assertEqual(inverse.encode(), snapshots["README.md"])

    def test_unicode_spaces_encoded_hash_and_parentheses(self):
        old = "old/中文 (a)#x.md"
        self.write(old, "# Title\n")
        self.graph["documents"][0]["path"] = old
        self.save_graph()
        self.operation["source"] = old
        self.operation["target"] = "知识/domain/semantic/中文 (a)#x.md"
        self.write("README.md", '[escaped](old/中文%20\\(a\\)%23x.md?q=%20#Title)\n[angle](<old/中文 (a)%23x.md>)\n[encoded](old/%E4%B8%AD%E6%96%87%20(a)%23x.md)\n')
        report, _, outputs, _ = self.prepare()
        self.assertTrue(report["ok"], report["errors"])
        result = outputs["README.md"][1].decode()
        self.assertIn("知识/domain/semantic/中文%20%28a%29%23x.md?q=%20#Title", result)
        self.assertEqual(len(next(f for f in report["files"] if f["source"] == "README.md")["link_edits"]), 3)

    def test_balanced_parentheses_and_reference_multiline(self):
        old = "old/topic(a(b)).md"
        self.write(old, "# Topic\n")
        self.graph["documents"][0]["path"] = old
        self.save_graph()
        self.operation["source"] = old
        self.operation["target"] = "知识/domain/semantic/topic(a(b)).md"
        self.write("README.md", "[balanced](old/topic(a(b)).md)\n\n[ref]\n\n[ref]:\n  <old/topic(a(b)).md>\n  'title'\n")
        report, _, outputs, _ = self.prepare()
        self.assertTrue(report["ok"], report["errors"])
        self.assertIn("topic%28a%28b%29%29.md", outputs["README.md"][1].decode())

    def test_both_source_and_target_move(self):
        self.write("old/other.md", "[topic](topic.md)\n")
        self.graph["documents"].append({"id": "kb-other", "path": "old/other.md", "domain": "systems", "kind": "concept"})
        self.save_graph()
        other = dict(self.operation, id="kb-other", source="old/other.md", target="知识/domain/different/other.md")
        self.plan["operations"].append(other)
        report, _, outputs, _ = self.prepare()
        self.assertTrue(report["ok"], report["errors"])
        self.assertEqual(outputs["old/other.md"][1], b"[topic](../semantic/topic.md)\n")

    def test_current_authorized_source_bytes_and_crlf_are_preserved(self):
        original = b"---\r\ncustom: value\r\n---\r\n# Reviewed edit\r\n\r\n[asset](../asset.png)\r\n"
        self.write("old/topic.md", original)
        report, snapshots, outputs, _ = self.prepare()
        self.assertTrue(report["ok"], report["errors"])
        self.assertEqual(snapshots["old/topic.md"], original)
        self.assertEqual(outputs["old/topic.md"][1], original.replace(b"../asset.png", b"../../../asset.png"))

    def test_protected_books_and_logs_fail_before_writes(self):
        for path in ("读书笔记/book.md", "工作日志/2026.md", "learning/log.md", "learning/lessons.md", "log.md"):
            with self.subTest(path=path):
                prefix = "../" if "/" in path else ""
                self.write(path, f"[topic]({prefix}old/topic.md)\n")
                self.assert_rejected("protected book/log")
                (self.root / path).unlink()

    def test_evidence_readme_links_can_update_but_evidence_cannot_move(self):
        self.write("evidence/README.md", "[topic](../old/topic.md)\n")
        report, _, outputs, _ = self.prepare()
        self.assertTrue(report["ok"], report["errors"])
        self.assertIn("evidence/README.md", outputs)
        self.write("evidence/topic.md", "# Topic\n")
        self.operation["source"] = "evidence/topic.md"
        self.graph["documents"][0]["path"] = "evidence/topic.md"
        self.save_graph()
        self.assert_rejected("protected source cannot move")

    def test_existing_target_rejected(self):
        self.write(self.operation["target"], "user data\n")
        self.assert_rejected("overwrite existing target")

    def test_path_escape_rejected(self):
        self.operation["target"] = "../outside/topic.md"
        self.assert_rejected("unsafe repository-relative")

    def test_symlink_ancestor_rejected(self):
        (self.root / "知识/domain/semantic").symlink_to(self.home, target_is_directory=True)
        self.assert_rejected("symlink/reparse")

    def test_symlink_source_rejected(self):
        (self.root / "old/topic.md").unlink()
        (self.root / "old/topic.md").symlink_to(self.root / "README.md")
        self.assert_rejected("symlink/reparse")

    def test_case_collision_rejected(self):
        self.write("知识/domain/Semantic/existing.md", "# Existing\n")
        self.assert_rejected("case/Unicode")

    def test_unicode_normalization_collision_rejected(self):
        self.operation["target"] = "知识/domain/café/topic.md"
        self.write("知识/domain/cafe\u0301/existing.md", "# Existing\n")
        self.assert_rejected("case/Unicode")

    def test_missing_graph_source_id_rejected(self):
        self.operation["id"] = "kb-missing"
        self.assert_rejected("graph source/id disagreement")

    def test_source_domain_and_explicit_target_domain(self):
        self.graph["domains"].append({"id": "engineering", "entrypoint": "知识/other/README.md"})
        self.write("知识/other/README.md", "# Other\n")
        self.save_graph()
        self.operation.update(domain="engineering", target="知识/other/semantic/topic.md")
        report, *_ = self.prepare()
        self.assertTrue(report["ok"], report["errors"])
        self.operation["source_domain"] = "engineering"
        self.assert_rejected("source_domain disagreement")

    def test_nonignored_untracked_links_are_included_ignored_are_not(self):
        self.write("extra.md", "[topic](old/topic.md)\n")
        self.write(".gitignore", "ignored.md\n")
        self.write("ignored.md", "[broken](missing.md)\n")
        report, snapshots, outputs, _ = self.prepare()
        self.assertTrue(report["ok"], report["errors"])
        self.assertIn("extra.md", outputs)
        self.assertNotIn("ignored.md", snapshots)

    def test_missing_local_link_fails(self):
        self.write("extra.md", "[broken](missing.md)\n")
        self.assert_rejected("missing path")

    def test_changed_snapshot_blocks_apply(self):
        report, snapshots, outputs, baseline = self.prepare()
        self.assertTrue(report["ok"], report["errors"])
        self.write("old/topic.md", "# Concurrent edit\n")
        with self.assertRaisesRegex(migration.Invalid, "source changed"):
            migration.apply_plan(self.root, self.plan_path, ".kb/knowledge-map.json", report, snapshots, outputs, baseline, report_path=self.report_path)
        self.assertFalse((self.root / self.operation["target"]).exists())

    def test_atomic_failure_restores_original_files(self):
        report, snapshots, outputs, baseline = self.prepare()
        before = self.snapshot()
        real = migration.atomic_write
        calls = 0
        def fail_once(path, data, **kwargs):
            nonlocal calls
            if path.is_relative_to(self.root):
                calls += 1
            if calls == 2 and path.is_relative_to(self.root):
                raise OSError("injected write failure")
            return real(path, data, **kwargs)
        with patch.object(migration, "atomic_write", side_effect=fail_once):
            with self.assertRaisesRegex(OSError, "injected"):
                migration.apply_plan(self.root, self.plan_path, ".kb/knowledge-map.json", report, snapshots, outputs, baseline, report_path=self.report_path)
        self.assertEqual(self.snapshot(), before)
        self.assertFalse(report["applied"])
        self.assertEqual(report["rollback_errors"], [])

    def test_success_retains_original_inodes_and_recovery_journal(self):
        before = self.snapshot()
        inodes = {name: (self.root / name).stat().st_ino for name in ("README.md", "old/topic.md")}
        result, report = self.cli(apply=True)
        self.assertEqual(result, 0, report["errors"])
        recovery = report["recovery"]
        backup = Path(recovery["backup_directory"])
        self.assertFalse(backup.is_relative_to(self.root))
        self.assertEqual(backup.stat().st_dev, self.root.stat().st_dev)
        if os.name != "nt":
            self.assertEqual(backup.stat().st_mode & 0o777, 0o700)
        self.assertTrue(Path(recovery["journal"]).is_file())
        for record in recovery["files"]:
            retained = Path(record["backup"])
            self.assertEqual(retained.read_bytes(), before[record["source"]])
            self.assertEqual(retained.stat().st_ino, inodes[record["source"]])
            self.assertEqual(record["state"], "retained_after_success")

    def assert_capture_race_is_recoverable(self, victim):
        report, snapshots, outputs, baseline = self.prepare()
        marker = b"# Concurrent bytes immediately before rename\n"
        real = migration.rename_noreplace
        injected = False
        def racing_rename(source, target):
            nonlocal injected
            if Path(source) == self.root / victim and not injected:
                injected = True
                Path(source).write_bytes(marker)
            return real(source, target)
        with patch.object(migration, "rename_noreplace", side_effect=racing_rename):
            with self.assertRaisesRegex(migration.Invalid, "source changed during capture"):
                migration.apply_plan(self.root, self.plan_path, ".kb/knowledge-map.json", report, snapshots, outputs, baseline, report_path=self.report_path)
        self.assertTrue(injected)
        self.assertFalse(report["applied"])
        record = next(item for item in report["recovery"]["files"] if item["source"] == victim)
        self.assertEqual(Path(record["backup"]).read_bytes(), marker)
        self.assertEqual((self.root / victim).read_bytes(), marker)
        self.assertNotEqual(record["expected_sha256"], record["captured_sha256"])

    def test_moved_source_check_to_capture_race_preserves_edit(self):
        self.assert_capture_race_is_recoverable("old/topic.md")

    def test_in_place_check_to_replace_race_preserves_edit(self):
        self.assert_capture_race_is_recoverable("README.md")

    def test_edit_first_source_while_checking_second_target_is_not_lost(self):
        self.write("old/zother.md", "# Second\n")
        self.graph["documents"].append({"id": "kb-other", "path": "old/zother.md", "domain": "systems", "kind": "concept"})
        self.save_graph()
        other = dict(self.operation, id="kb-other", source="old/zother.md", target="知识/domain/semantic/zother.md")
        self.plan["operations"].append(other)
        report, snapshots, outputs, baseline = self.prepare()
        marker = b"# User edit after source1 check, during target2 check\n"
        real = Path.read_bytes
        injected = False
        def racing_read(path):
            nonlocal injected
            data = real(path)
            if path == self.root / other["target"] and not injected:
                injected = True
                (self.root / "old/topic.md").write_bytes(marker)
            return data
        with patch.object(Path, "read_bytes", racing_read):
            with self.assertRaisesRegex(migration.Invalid, "concurrent writer recreated|source changed"):
                migration.apply_plan(self.root, self.plan_path, ".kb/knowledge-map.json", report, snapshots, outputs, baseline, report_path=self.report_path)
        self.assertTrue(injected)
        self.assertFalse(report["applied"])
        self.assertEqual((self.root / "old/topic.md").read_bytes(), marker)
        original = next(item for item in report["recovery"]["files"] if item["source"] == "old/topic.md")
        self.assertEqual(Path(original["backup"]).read_bytes(), snapshots["old/topic.md"])
        self.assertTrue(report["rollback_errors"])

    def test_concurrent_in_place_destination_creation_is_not_overwritten(self):
        report, snapshots, outputs, baseline = self.prepare()
        marker = b"# New independent README\n"
        real = migration.atomic_write
        injected = False
        def racing_install(path, data, **kwargs):
            nonlocal injected
            if path == self.root / "README.md" and kwargs.get("exclusive") and not injected:
                injected = True
                path.write_bytes(marker)
            return real(path, data, **kwargs)
        with patch.object(migration, "atomic_write", side_effect=racing_install):
            with self.assertRaises(FileExistsError):
                migration.apply_plan(self.root, self.plan_path, ".kb/knowledge-map.json", report, snapshots, outputs, baseline, report_path=self.report_path)
        self.assertTrue(injected)
        self.assertFalse(report["applied"])
        self.assertEqual((self.root / "README.md").read_bytes(), marker)
        record = next(item for item in report["recovery"]["files"] if item["source"] == "README.md")
        self.assertEqual(Path(record["backup"]).read_bytes(), snapshots["README.md"])
        self.assertEqual(record["state"], "backup_retained_restore_blocked")

    def test_capture_rename_never_clobbers_existing_backup(self):
        source, target = self.home / "source", self.home / "target"
        source.write_bytes(b"new")
        target.write_bytes(b"retained")
        with self.assertRaises(FileExistsError):
            migration.rename_noreplace(source, target)
        self.assertEqual(source.read_bytes(), b"new")
        self.assertEqual(target.read_bytes(), b"retained")

    def assert_backup_filesystem_mismatch_blocks_before_repo_writes(self):
        report, snapshots, outputs, baseline = self.prepare()
        before = self.snapshot()
        real = Path.stat
        # Match the production path contract, including Windows short-name or
        # lexical aliases, before patching stat. Never resolve inside the mock.
        report_parent = self.report_path.resolve().parent
        hits = 0
        def wrong_device(path, *args, **kwargs):
            nonlocal hits
            result = real(path, *args, **kwargs)
            if path == report_parent:
                hits += 1
                fields = list(result)
                fields[2] += 1
                return os.stat_result(fields)
            return result
        with patch.object(Path, "stat", wrong_device):
            with self.assertRaisesRegex(migration.Invalid, "repository filesystem"):
                migration.apply_plan(self.root, self.plan_path, ".kb/knowledge-map.json", report, snapshots, outputs, baseline, report_path=self.report_path)
        self.assertGreater(hits, 0, "filesystem mismatch mock never reached the report directory")
        self.assertEqual(self.snapshot(), before)

    def test_backup_filesystem_mismatch_blocks_before_repo_writes(self):
        self.assert_backup_filesystem_mismatch_blocks_before_repo_writes()

    def test_backup_filesystem_mock_matches_resolved_report_parent(self):
        (self.home / "alias").mkdir()
        self.report_path = self.home / "alias" / ".." / "report.json"
        self.assertNotEqual(self.report_path.parent, self.report_path.resolve().parent)
        self.assert_backup_filesystem_mismatch_blocks_before_repo_writes()

    def test_report_must_be_outside_repository(self):
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                migration.main(["--root", str(self.root), "--plan", str(self.plan_path), "--report", str(self.root / "report.json")])
        self.assertFalse((self.root / "report.json").exists())


if __name__ == "__main__":
    unittest.main()
