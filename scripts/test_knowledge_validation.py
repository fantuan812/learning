#!/usr/bin/env python3
"""Isolated negative/positive regression fixtures; Git writes only in temp dirs."""
from __future__ import annotations

from copy import deepcopy
from datetime import date
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from validate_knowledge import (
    DOMAINS, Invalid, Validator, frontmatter, metadata_errors, parse_markdown,
    safe_path, strict_json,
)

VALID = "---\ntype: Concept\ntitle: Fixture\nmaturity: L2\nstatus: stable\nupdated: 2026-10-03\nverified: []\n---\n"


class Fixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="kb-knowledge-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.git("init", "--quiet")
        self.write("README.md", VALID + "# Home\n")
        self.write("notes/a.md", VALID + "# Alpha\n")
        self.write("notes/b.md", VALID + "# Beta\n")
        self.graph = {
            "version": 1,
            "domains": [{"id": name, "title": name.title(), "entrypoint": "README.md"} for name in sorted(DOMAINS)],
            "documents": [
                {"id": "doc-a", "path": "notes/a.md", "domain": "systems", "kind": "concept"},
                {"id": "doc-b", "path": "notes/b.md", "domain": "ai", "kind": "source-analysis"},
            ],
            "concepts": [{"id": "concept-a", "title": "Alpha", "primary": "doc-a", "related": ["doc-b"]}],
            "relations": [{"from": "doc-a", "to": "doc-b", "type": "prerequisite"}],
            "coverage_roots": ["notes"],
        }
        self.save_graph()

    def git(self, *args):
        return subprocess.check_output(["git", "-c", f"safe.directory={self.root}", "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "-C", str(self.root), *args], stderr=subprocess.STDOUT).decode()

    def write(self, path, text):
        full = self.root / path
        full.parent.mkdir(parents=True, exist_ok=True)
        full.write_text(text, encoding="utf-8")

    def save_graph(self):
        self.write(".kb/knowledge-map.json", json.dumps(self.graph, ensure_ascii=False))

    def validate(self, base_ref=None):
        self.save_graph()
        return Validator(self.root, base_ref=base_ref).run()

    def assert_valid(self, report=None):
        report = report if report is not None else self.validate()
        self.assertTrue(report["ok"], json.dumps(report["errors"], ensure_ascii=False, indent=2))

    def assert_error(self, code, contains="", report=None):
        report = report if report is not None else self.validate()
        self.assertFalse(report["ok"])
        self.assertTrue(any(error["code"] == code and contains in error["message"] for error in report["errors"]), report["errors"])

    def test_valid_untracked_inventory(self):
        self.assert_valid()

    def test_valid_tracked_inventory(self):
        self.git("add", "--", ".")
        self.assert_valid()

    def test_duplicate_json_keys_nested(self):
        for content in ('{"version":1,"version":1}', '{"x":{"id":"a","id":"b"}}', '{"x":NaN}'):
            with self.subTest(content=content), self.assertRaises((Invalid, ValueError)):
                strict_json(content)

    def test_malformed_json(self):
        self.write(".kb/knowledge-map.json", '{"version":')
        self.assert_error("graph", report=Validator(self.root).run())

    def test_wrong_graph_shapes_and_version(self):
        cases = [("version", True), ("documents", {}), ("concepts", None), ("relations", "bad"), ("coverage_roots", {})]
        for key, value in cases:
            with self.subTest(key=key):
                old = self.graph[key]
                self.graph[key] = value
                self.assertFalse(self.validate()["ok"])
                self.graph[key] = old

    def test_duplicate_document_id_and_path(self):
        self.graph["documents"].append(deepcopy(self.graph["documents"][0]))
        self.assert_error("graph", "duplicate")

    def test_case_conflicting_graph_ids(self):
        self.graph["documents"][1]["id"] = "DOC-A"
        self.assert_error("graph", "conflicting")

    def test_unicode_conflicting_concept_titles(self):
        self.graph["concepts"][0]["title"] = "Caf\u00e9"
        self.graph["concepts"].append({"id": "concept-b", "title": "Cafe\u0301", "primary": "doc-b", "related": []})
        self.assert_error("graph", "conflicting")

    def test_multiple_concepts_can_share_primary(self):
        self.graph["concepts"].append({"id": "concept-b", "title": "Another concept", "primary": "doc-a", "related": []})
        self.assert_valid()

    def test_unknown_primary_related_and_endpoints(self):
        for field in ("primary", "related", "endpoint"):
            with self.subTest(field=field):
                previous = deepcopy(self.graph)
                if field == "primary":
                    self.graph["concepts"][0][field] = "unknown"
                elif field == "related":
                    self.graph["concepts"][0][field] = ["unknown"]
                else:
                    self.graph["relations"][0]["to"] = "unknown"
                self.assert_error("graph")
                self.graph = previous

    def test_duplicate_concept_id(self):
        self.graph["concepts"].append(deepcopy(self.graph["concepts"][0]))
        self.assert_error("graph", "duplicate")

    def test_array_primary_is_invalid(self):
        self.graph["concepts"][0]["primary"] = ["doc-a", "doc-b"]
        self.assert_error("graph", "primary")

    def test_bad_domain_kind_and_relation_type(self):
        for section, key in (("documents", "domain"), ("documents", "kind"), ("relations", "type")):
            with self.subTest(key=key):
                old = self.graph[section][0][key]
                self.graph[section][0][key] = "not-supported"
                self.assert_error("graph", "invalid")
                self.graph[section][0][key] = old

    def test_optional_technology_slugs(self):
        self.assert_valid()
        self.graph["documents"][0]["technologies"] = ["unreal-engine", "cpp"]
        self.assert_valid()

    def test_invalid_technology_shapes_slugs_and_duplicates(self):
        for value in ("unreal-engine", {}, [], ["unreal-engine", "unreal-engine"], [""], ["Unreal-Engine"], ["bad_slug"], [7], [None], [{"bad": True}]):
            with self.subTest(value=value):
                self.graph["documents"][0]["technologies"] = value
                self.assert_error("graph", "technologies")

    def test_unknown_document_fields_rejected(self):
        self.graph["documents"][0]["arbitrary"] = True
        self.assert_error("graph", "requires only")

    def test_missing_domain(self):
        self.graph["domains"].pop()
        self.assert_error("graph", "exactly")

    def test_missing_domain_entrypoint(self):
        self.graph["domains"][0]["entrypoint"] = "missing.md"
        self.assert_error("graph", "missing")

    def test_explicit_directional_relation_names(self):
        for kind in ("prerequisite", "implemented_by", "applied_in", "verified_by", "related", "reads"):
            with self.subTest(kind=kind):
                self.graph["relations"][0]["type"] = kind
                self.assert_valid()
        for old_kind in ("implements", "applies"):
            with self.subTest(kind=old_kind):
                self.graph["relations"][0]["type"] = old_kind
                self.assert_error("graph", "invalid relation type")

    def test_prerequisite_cycle(self):
        self.graph["relations"].append({"from": "doc-b", "to": "doc-a", "type": "prerequisite"})
        self.assert_error("graph", "cycle")

    def test_prerequisite_self_cycle(self):
        self.graph["relations"][0]["to"] = "doc-a"
        self.assert_error("graph", "cycle")

    def test_other_relation_cycles_allowed(self):
        self.graph["relations"] = [{"from": "doc-a", "to": "doc-b", "type": "related"}, {"from": "doc-b", "to": "doc-a", "type": "related"}]
        self.assert_valid()

    def test_graph_path_escape_and_nonportable_paths(self):
        for value in ("../escape.md", "/tmp/escape.md", "notes/../notes/a.md", "notes\\a.md", "C:/file.md", "notes//a.md"):
            with self.subTest(path=value):
                self.graph["documents"][0]["path"] = value
                self.assert_error("graph", "unsafe")

    def test_case_conflicting_directory_components(self):
        self.write("Notes/c.md", VALID + "# Other\n")
        if (self.root / "notes/c.md").exists():
            self.skipTest("filesystem is case-insensitive")
        self.assert_error("path", "conflict")

    def test_unicode_conflicting_paths(self):
        self.write("caf\u00e9.md", VALID + "# One\n")
        self.write("cafe\u0301.md", VALID + "# Two\n")
        if len([p for p in self.root.iterdir() if p.name.startswith("caf")]) != 2:
            self.skipTest("filesystem normalizes Unicode filenames")
        self.assert_error("path", "Unicode")

    def test_wrong_scalar_types_do_not_crash_graph_validation(self):
        for section, field in (("documents", "id"), ("documents", "path"), ("documents", "domain"), ("documents", "kind"), ("concepts", "id"), ("concepts", "primary"), ("concepts", "title"), ("relations", "from"), ("domains", "entrypoint")):
            with self.subTest(section=section, field=field):
                previous = deepcopy(self.graph)
                self.graph[section][0][field] = {"bad": []}
                self.assert_error("graph")
                self.graph = previous

    def test_wrong_case_path(self):
        self.graph["documents"][0]["path"] = "Notes/a.md"
        self.assert_error("graph")

    def test_symlink_document_and_ancestor(self):
        link = self.root / "linked"
        try:
            link.symlink_to(self.root / "notes", target_is_directory=True)
        except OSError as exc:
            self.skipTest(f"OS cannot create a symlink: {exc}")
        self.graph["documents"][0]["path"] = "linked/a.md"
        self.assert_error("graph", "symlink")
        with self.assertRaises(Invalid):
            safe_path(self.root, "linked/a.md")

    def test_dangling_symlink_rejected(self):
        link = self.root / "notes" / "dangling.md"
        try:
            link.symlink_to(self.root / "absent.md")
        except OSError as exc:
            self.skipTest(f"OS cannot create a symlink: {exc}")
        with self.assertRaises(Invalid):
            safe_path(self.root, "notes/dangling.md")

    def test_ignored_document_cannot_be_graph_node(self):
        self.write(".gitignore", "notes/b.md\n")
        self.assert_error("graph", "tracked or nonignored")

    def test_coverage_missing_document_and_reserved_names(self):
        self.write("notes/extra.md", VALID + "# Extra\n")
        self.assert_error("coverage", "exactly once")
        for filename in ("README.md", "index.md", "log.md", "SKILL.md"):
            self.write("notes/" + filename, "## 2026-10-03\n" if filename in ("index.md", "log.md") else VALID + "# Navigation\n")
        self.graph["documents"].append({"id": "doc-extra", "path": "notes/extra.md", "domain": "engineering", "kind": "reference"})
        self.assert_valid()

    def test_optional_coverage_roots(self):
        del self.graph["coverage_roots"]
        self.write("notes/unmapped.md", VALID + "# Unmapped\n")
        self.assert_valid()

    def test_yaml_duplicate_keys_and_control_character(self):
        for bad in ("type: Concept\ntype: Reference", "type: Concept\nextra: {a: 1, a: 2}", "type: Concept\ntitle: bad\x01title"):
            with self.subTest(bad=bad):
                self.write("notes/a.md", "---\n" + bad + "\n---\n# Alpha\n")
                self.assert_error("yaml")

    def test_yaml_malformed_and_nonmapping(self):
        for bad in ("type: [", "- item", "null", "type: !!python/object:bad {}", "type: Concept\nextra: [a, b"):
            with self.subTest(bad=bad):
                self.write("notes/a.md", "---\n" + bad + "\n---\n# Alpha\n")
                self.assert_error("yaml")

    def test_unclosed_frontmatter(self):
        self.write("notes/a.md", "---\ntype: Concept\n# Alpha\n")
        self.assert_error("yaml", "unclosed")

    def test_yaml_type_string_fields_dates_maturity(self):
        bad_fields = ("type: 7", "type: ''", "type: Concept\ntitle: [bad]", "type: Concept\nmaturity: L9", "type: Concept\nmaturity: []", "type: Concept\nstatus: []", "type: Concept\nupdated: '2026-02-30'", "type: Concept\ntags: a,b")
        for bad in bad_fields:
            with self.subTest(bad=bad):
                self.write("notes/a.md", "---\n" + bad + "\n---\n# Alpha\n")
                self.assert_error("metadata")

    def test_yaml_date_objects_unknown_fields_preserved(self):
        text = VALID.replace("verified: []", "custom: {keep: [1, two]}\ndescription: |\n  Long description\n  continues here\nverified: []") + "# Alpha\n"
        data, body, offset = frontmatter(text)
        self.assertIsInstance(data["updated"], date)
        self.assertEqual(data["custom"], {"keep": [1, "two"]})
        self.assertEqual(list(metadata_errors("notes/a.md", data, text)), [])
        self.assertEqual(body, "# Alpha\n")
        self.assertGreater(offset, 0)

    def test_yaml_sources_and_verification_events(self):
        bad_fields = ("sources: [url]", "sources: {}", "sources: [{title: Missing}]", "sources: [{resource: 4}]", "verified: human-reviewed", "verified: [{by: human:a}]", "verified: [{by: a, at: '2026-10-03T01:00:00Z'}]", "verified: [{by: human:a, at: '2026-02-30T01:00:00Z'}]", "generated: []")
        for bad in bad_fields:
            with self.subTest(bad=bad):
                self.write("notes/a.md", "---\ntype: Concept\n" + bad + "\n---\n# Alpha\n")
                self.assert_error("metadata")

    def test_valid_sources_verified_generated(self):
        self.write("notes/a.md", "---\ntype: Concept\nsources: [{resource: 'https://example.invalid', title: Example}]\nverified: [{by: 'human:reviewer', at: 2026-10-03T01:00:00Z}]\ngenerated: {by: 'tool/1.0', at: '2026-10-03T01:00:00+00:00'}\n---\n# Alpha\n")
        self.assert_valid()

    def test_inline_reference_and_image_links(self):
        self.write("notes/a.md", VALID + "# Alpha\n[Inline](b.md#beta)\n[Reference][dest]\n![Image](../image.png)\n![Reference image][pic]\n[dest]: b.md#beta\n[pic]: ../image.png\n")
        (self.root / "image.png").write_bytes(b"fixture")
        self.assert_valid()

    def test_broken_reference_image_and_anchor(self):
        bodies = ("[Reference][dest]\n\n[dest]: missing.md", "![Image](missing.png)", "![Image][pic]\n\n[pic]: missing.png", "[Anchor](b.md#absent)")
        for body in bodies:
            with self.subTest(body=body):
                self.write("notes/a.md", VALID + "# Alpha\n" + body + "\n")
                self.assert_error("link")

    def test_encoded_hash_filename_and_fragment_decode_separately(self):
        self.write("notes/hash#name.md", VALID + "# 中文 标题\n")
        self.graph["documents"].append({"id": "doc-hash", "path": "notes/hash#name.md", "domain": "ai", "kind": "reference"})
        self.write("notes/a.md", VALID + "# Alpha\n[Encoded](hash%23name.md#%E4%B8%AD%E6%96%87-%E6%A0%87%E9%A2%98)\n")
        self.assert_valid()

    def test_escaped_parentheses_and_space_paths(self):
        self.write("notes/a (b).md", VALID + "# Escaped\n")
        self.graph["documents"].append({"id": "doc-escaped", "path": "notes/a (b).md", "domain": "systems", "kind": "reference"})
        self.write("notes/a.md", VALID + "# Alpha\n[Escaped](a%20\\(b\\).md#escaped)\n[Angle](<a (b).md#escaped>)\n")
        self.assert_valid()

    def test_duplicate_heading_slugs_and_inline_markup(self):
        self.write("notes/b.md", VALID + "# *Beta* `code`\n# Beta code\n# Beta code-1\n# Beta code\n")
        self.write("notes/a.md", VALID + "# Alpha\n[a](b.md#beta-code) [b](b.md#beta-code-1) [c](b.md#beta-code-1-1) [d](b.md#beta-code-2)\n")
        self.assert_valid()

    def test_setext_heading_and_explicit_html_anchor(self):
        self.write("notes/b.md", VALID + "Beta\n====\n<a name=\"Legacy\"></a>\n<span id='custom'></span>\n")
        self.write("notes/a.md", VALID + "# Alpha\n[b](b.md#beta) [legacy](b.md#Legacy) [custom](b.md#custom)\n")
        self.assert_valid()

    def test_fenced_indented_and_inline_code_ignored(self):
        self.write("notes/a.md", VALID + "# Alpha\n```markdown\n[bad](missing.md)\n# Fake\n```\n\n    [bad](missing.md)\n\n`[bad](missing.md)`\n")
        self.assert_valid()
        parsed = parse_markdown("```\n# Fake\n```\n# Real\n")
        self.assertNotIn("fake", parsed.anchors)
        self.assertIn("real", parsed.anchors)

    def test_local_link_escape(self):
        self.write("notes/a.md", VALID + "# Alpha\n[bad](../../outside.md)\n")
        self.assert_error("link", "escapes")

    def test_link_to_parent_directory(self):
        self.write("notes/a.md", VALID + "# Alpha\n[Home](../)\n")
        self.assert_valid()

    def test_external_links_not_fetched(self):
        self.write("notes/a.md", VALID + "# Alpha\n[web](https://example.invalid/missing#anchor) [mail](mailto:person@example.invalid)\n")
        self.assert_valid()

    def test_changed_mode_includes_worktree_and_untracked(self):
        self.git("add", "--", ".")
        self.git("commit", "--quiet", "-m", "Fixture baseline")
        self.write("notes/a.md", VALID + "# Alpha\n[Missing](missing.md)\n")
        self.write("new.md", "# No metadata\n")
        report = self.validate("HEAD")
        self.assertEqual(report["mode"], "changed")
        self.assertEqual(report["markdown_scanned"], 2)
        self.assert_error("link", report=report)
        self.assert_error("metadata", report=report)

    def test_changed_mode_still_checks_full_graph(self):
        self.git("add", "--", ".")
        self.git("commit", "--quiet", "-m", "Fixture baseline")
        self.graph["relations"][0]["to"] = "unknown"
        report = self.validate("HEAD")
        self.assertEqual(report["markdown_scanned"], 0)
        self.assert_error("graph", report=report)

    def test_exact_insertion_template_and_materialized_copy(self):
        template = '---\ntype: Concept\ntitle: "{{title}}"\nupdated: "{{date:YYYY-MM-DD}}"\n---\n# {{title}}\n'
        path = "references/templates/OKF-知识条目.md"
        self.write(path, template)
        self.assert_valid()
        self.write("ordinary.md", template)
        self.assert_error("metadata", "placeholder")
        self.write("ordinary.md", template.replace("{{title}}", "Actual title").replace("{{date:YYYY-MM-DD}}", "2026-10-03"))
        self.assert_valid()

    def test_changed_scope_does_not_certify_target_yaml(self):
        self.write("notes/b.md", "---\ntype: Concept\ntitle: bad\x01value\n---\n# Beta\n")
        self.git("add", "--", ".")
        self.git("commit", "--quiet", "-m", "Fixture baseline")
        self.write("notes/a.md", VALID + "# Alpha\n[Beta](b.md#beta)\n")
        self.assert_valid(self.validate("HEAD"))
        self.assert_error("yaml", report=self.validate())

    def test_primary_authority_kinds(self):
        for kind in ("concept", "source-analysis", "case-study"):
            with self.subTest(kind=kind):
                self.graph["documents"][0]["kind"] = kind
                self.assert_valid()
        for kind in ("journal", "reading-note", "experiment", "roadmap", "reference"):
            with self.subTest(kind=kind):
                self.graph["documents"][0]["kind"] = kind
                self.assert_error("graph", "primary must be")

    def test_ignored_local_link_target_is_invalid(self):
        self.write(".gitignore", "cache.md\nimage.png\n")
        self.write("cache.md", VALID + "# Cached\n")
        (self.root / "image.png").write_bytes(b"fixture")
        self.write("notes/a.md", VALID + "# Alpha\n[Ignored](../cache.md) ![Ignored image](../image.png)\n")
        self.assert_error("link", "tracked or nonignored")

    def test_tracked_image_and_pdf_links_valid(self):
        for filename in ("image.png", "paper.pdf"):
            (self.root / filename).write_bytes(b"fixture")
            self.git("add", "--", filename)
        self.write(".gitignore", "*.pdf\n*.png\n")
        self.write("notes/a.md", VALID + "# Alpha\n![Image](../image.png) [Paper](../paper.pdf)\n")
        self.assert_valid()

    def test_body_maturity_matches_frontmatter(self):
        for marker in ("知识成熟度：L2", "> 知识成熟度：L2。说明", "> **知识成熟度**：**L2**（说明）"):
            with self.subTest(marker=marker):
                self.write("notes/a.md", VALID + "# Alpha\n\n" + marker + "\n")
                self.assert_valid()
                self.write("notes/a.md", VALID.replace("maturity: L2", "maturity: L4") + "# Alpha\n\n" + marker + "\n")
                self.assert_error("metadata", "body maturity L2 disagrees")

    def test_maturity_examples_in_code_and_ranges_ignored(self):
        self.write("notes/a.md", VALID + "# Alpha\n\n```markdown\n> 知识成熟度：L4\n```\n\n    知识成熟度：L4\n\n`知识成熟度：L4`\n\n`multiline code\n知识成熟度：L4\nend code`\n\n> 知识成熟度：L0~L5 之一\n")
        self.assert_valid()

    def test_html_href_and_src_links(self):
        (self.root / "image.png").write_bytes(b"fixture")
        self.write("notes/a.md", VALID + '# Alpha\n<a href="b.md#beta">Beta</a> <img src="../image.png" />\n')
        self.assert_valid()
        for body in ('<a href="missing.md">Missing</a>', '<img src="missing.png">', '<a href="b.md#missing">Missing anchor</a>'):
            with self.subTest(body=body):
                self.write("notes/a.md", VALID + "# Alpha\n" + body + "\n")
                self.assert_error("link")

    def test_html_attribute_entities_and_code_ignored(self):
        self.write("notes/a&b.md", VALID + "# Other\n")
        self.graph["documents"].append({"id": "doc-html", "path": "notes/a&b.md", "domain": "systems", "kind": "reference"})
        self.write("notes/a.md", VALID + '# Alpha\n<a href="a&amp;b.md#other">Valid entity</a>\n\n```html\n<img src="missing.png">\n```\n\n`<a href="missing.md">code</a>`\n\n<!-- <img src="missing.png"> -->\n')
        self.assert_valid()

    def test_invalid_base_ref_fails_closed(self):
        self.assert_error("scope", report=self.validate("missing-ref"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
