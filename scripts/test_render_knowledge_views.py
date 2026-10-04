#!/usr/bin/env python3
"""Safety and deterministic rendering tests; all files are disposable fixtures."""
import copy
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
import render_knowledge_views as renderer
from render_knowledge_views import render, safe_path


class RendererTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        for name in ['topic.md', '知识/README.md', '00_Index/跨域关系.md']:
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('---\ntype: Concept\ntitle: "Title"\n---\n# Topic\n', encoding='utf-8')
        self.graph = {
            'domains': [{'id': 'systems', 'title': '编程', 'entrypoint': '知识/01-编程/README.md'}],
            'documents': [{'id': 'one', 'path': 'topic.md', 'domain': 'systems', 'kind': 'concept'}],
            'concepts': [], 'relations': []}

    def write(self, name, content=b'USER CONTENT MUST SURVIVE\r\n\x00\xff'):
        target = self.root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
        return target

    def snapshot(self):
        """Compare bytes AND added/deleted entries, including empty directories."""
        result = {}
        for path in self.root.rglob('*'):
            name = path.relative_to(self.root).as_posix()
            if path.is_symlink():
                result[name] = ('symlink', str(path.readlink()))
            elif path.is_file():
                result[name] = ('file', path.read_bytes())
            else:
                result[name] = ('directory', None)
        return result

    def save_graph(self, graph):
        self.write('.kb/knowledge-map.json', json.dumps(graph, ensure_ascii=False).encode('utf-8'))

    def cli(self, *args):
        # Invoke the standalone renderer, never the separate graph validator.
        return subprocess.run([sys.executable, '-B', str(Path(renderer.__file__).resolve()),
                               '--root', str(self.root), *args], capture_output=True,
                              text=True, encoding='utf-8', timeout=30)

    def failure_graph(self):
        graph = copy.deepcopy(self.graph)
        self.write('知识/01-编程/README.md', b'EXISTING NAVIGATION MUST SURVIVE\n')
        self.write('unrelated-user-content.bin')
        path = '知识/01-编程/New/Child/topic.md'
        self.write(path, b'# Pending nested navigation\n')
        graph['documents'].append({'id': 'nested', 'path': path, 'domain': 'systems', 'kind': 'concept'})
        return graph

    def assert_cli_rejected_without_write(self, graph, message):
        self.save_graph(graph)
        before = self.snapshot()
        for args in [(), ('--check',)]:
            with self.subTest(args=args):
                result = self.cli(*args)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertRegex(result.stderr, message)
                self.assertNotIn('PASS:', result.stdout)
                self.assertEqual(before, self.snapshot())

    def hardlink(self, source, target):
        target = self.root / target
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            target.unlink()
        try:
            os.link(self.root / source, target)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f'Platform does not permit creating hardlink fixtures: {exc}')

    def test_deterministic_and_no_write(self):
        before = self.snapshot()
        a = render(self.root, self.graph)
        self.assertEqual(a, render(self.root, self.graph))
        self.assertEqual(before, self.snapshot())
        self.assertIn('知识/01-编程/README.md', a)

    def test_generated_metadata_does_not_invent_content_update_date(self):
        outputs = render(self.root, self.graph)
        for text in outputs.values():
            frontmatter = text.split('---', 2)[1]
            self.assertNotIn('updated:', frontmatter)
            self.assertIn('maturity: L0', frontmatter)
        self.assertEqual(outputs, render(self.root, self.graph))

    def test_protected_book_log_and_source_outputs_rejected(self):
        for path in ['工作日志/README.md', '读书笔记/README.md', 'topic.md', '00_Index/UE专题.md', '../escape.md', '/tmp/escape.md']:
            with self.subTest(path=path):
                graph = copy.deepcopy(self.graph)
                graph['domains'][0]['entrypoint'] = path
                with self.assertRaises(ValueError):
                    render(self.root, graph)

    def test_source_escape_rejected(self):
        for path in ['../outside.md', '/tmp/outside.md', 'C:/outside.md']:
            graph = copy.deepcopy(self.graph)
            graph['documents'][0]['path'] = path
            with self.assertRaises(ValueError):
                render(self.root, graph)

    def test_duplicate_outputs_rejected(self):
        graph = copy.deepcopy(self.graph)
        graph['domains'].append(dict(graph['domains'][0]))
        with self.assertRaises(ValueError):
            render(self.root, graph)

    def test_case_and_unicode_output_collisions_rejected(self):
        for a, b in [('知识/Case/README.md', '知识/case/README.md'), ('知识/é/README.md', '知识/e\u0301/README.md')]:
            graph = copy.deepcopy(self.graph)
            graph['domains'][0]['entrypoint'] = a
            graph['domains'].append({'id': 'other', 'title': 'Other', 'entrypoint': b})
            with self.assertRaises(ValueError):
                render(self.root, graph)

    def test_nested_semantic_index_is_generated(self):
        target = self.root / '知识/01-编程/语言与内存/topic.md'
        target.parent.mkdir(parents=True)
        target.write_text('---\ntype: Concept\ntitle: "Topic"\n---\n# Topic\n', encoding='utf-8')
        graph = copy.deepcopy(self.graph)
        graph['documents'][0]['path'] = '知识/01-编程/语言与内存/topic.md'
        outputs = render(self.root, graph)
        self.assertIn('知识/01-编程/语言与内存/README.md', outputs)
        self.assertIn('语言与内存/README.md', outputs['知识/01-编程/README.md'])
        self.assertIn('topic.md', outputs['知识/01-编程/语言与内存/README.md'])

    def test_generated_index_cannot_replace_document(self):
        target = self.root / '知识/01-编程/子类/README.md'
        target.parent.mkdir(parents=True)
        target.write_text('# Authored knowledge\n', encoding='utf-8')
        graph = copy.deepcopy(self.graph)
        graph['documents'][0]['path'] = '知识/01-编程/子类/README.md'
        with self.assertRaises(ValueError):
            render(self.root, graph)

    def test_nested_output_collision_fails_before_write(self):
        graph = copy.deepcopy(self.graph)
        graph['documents'] = []
        for i, dirname in enumerate(['Case', 'case']):
            relative = f'知识/01-编程/{dirname}/topic{i}.md'
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('# Topic\n', encoding='utf-8')
            graph['documents'].append({'id': str(i), 'path': relative, 'domain': 'systems', 'kind': 'concept'})
        before = self.snapshot()
        with self.assertRaises(ValueError):
            render(self.root, graph)
        self.assertEqual(before, self.snapshot())

    def test_all_source_output_collisions_fail_globally_before_cli_writes(self):
        for path in ['知识/01-编程/README.md', '00_Index/跨域关系.md',
                     '00_Index/UE专题.md', '知识/01-编程/子类/README.md']:
            with self.subTest(path=path):
                graph = self.failure_graph()
                self.write(path, b'# AUTHORED DOCUMENT MUST SURVIVE\n')
                graph['documents'][0]['path'] = path
                self.assert_cli_rejected_without_write(graph, 'cannot overwrite a knowledge document')
                self.assertFalse((self.root / '知识/01-编程/New/README.md').exists())
                self.assertFalse((self.root / '知识/01-编程/New/Child/README.md').exists())

    def test_case_source_output_collision_fails_before_cli_writes(self):
        graph = self.failure_graph()
        graph['documents'][0]['path'] = '00_Index/ue专题.md'
        self.write(graph['documents'][0]['path'], b'# Authored UE document\n')
        self.assert_cli_rejected_without_write(graph, 'collide|cannot overwrite')

    def test_unicode_source_output_collision_fails_before_cli_writes(self):
        graph = self.failure_graph()
        graph['domains'].append({'id': 'other', 'title': 'Other', 'entrypoint': '知识/é/README.md'})
        graph['documents'][0]['path'] = '知识/e\u0301/README.md'
        graph['documents'][0]['domain'] = 'other'
        self.write(graph['documents'][0]['path'], b'# Authored Unicode document\n')
        self.assert_cli_rejected_without_write(graph, 'collide|cannot overwrite')

    def test_missing_outputs_collide_by_case_before_cli_writes(self):
        graph = self.failure_graph()
        for i, directory in enumerate(['Case', 'case']):
            graph['domains'].append({'id': str(i), 'title': directory,
                                     'entrypoint': f'知识/{directory}/README.md'})
        self.assert_cli_rejected_without_write(graph, 'output paths collide')
        self.assertFalse((self.root / '知识/Case').exists())
        self.assertFalse((self.root / '知识/case').exists())

    def test_missing_outputs_collide_by_unicode_before_cli_writes(self):
        graph = self.failure_graph()
        for i, directory in enumerate(['é', 'e\u0301']):
            graph['domains'].append({'id': str(i), 'title': directory,
                                     'entrypoint': f'知识/{directory}/README.md'})
        self.assert_cli_rejected_without_write(graph, 'output paths collide')

    def test_nonportable_new_output_components_fail_before_cli_writes(self):
        # These names need not exist: reject Windows failures on every host.
        for component in ['CON', 'con.notes', 'PRN', 'aux', 'NUL', 'COM1', 'lpt9.notes',
                          'trailing.', 'trailing ', 'bad<name', 'bad>name', 'bad"name',
                          'bad|name', 'bad?name', 'bad*name', 'bad\x00name', 'bad\x1fname', 'bad\x7fname']:
            with self.subTest(component=component):
                graph = self.failure_graph()
                graph['domains'].append({'id': 'other', 'title': 'Other',
                                         'entrypoint': f'知识/{component}/README.md'})
                self.assert_cli_rejected_without_write(graph, 'nonportable')

    def test_missing_output_conflicts_with_disk_case_spelling(self):
        graph = self.failure_graph()
        graph['domains'].append({'id': 'other', 'title': 'Other', 'entrypoint': '知识/Case/README.md'})
        self.write('知识/case/user.md')
        self.assert_cli_rejected_without_write(graph, 'disk spelling')

    def test_missing_output_conflicts_with_disk_unicode_spelling(self):
        graph = self.failure_graph()
        graph['domains'].append({'id': 'other', 'title': 'Other', 'entrypoint': '知识/é/README.md'})
        self.write('知识/e\u0301/user.md')
        self.assert_cli_rejected_without_write(graph, 'disk spelling')

    def test_missing_output_conflicts_with_disk_leaf_spelling(self):
        graph = self.failure_graph()
        self.write('00_Index/ue专题.md')
        self.assert_cli_rejected_without_write(graph, 'disk spelling')

    def test_existing_output_and_case_alias_on_disk_fail_before_cli_writes(self):
        graph = self.failure_graph()
        self.write('00_Index/UE专题.md', b'EXISTING VIEW\n')
        self.write('00_Index/ue专题.md')
        names = {path.name for path in (self.root / '00_Index').iterdir()}
        if not {'UE专题.md', 'ue专题.md'} <= names:
            self.skipTest('Filesystem cannot retain case-distinct sibling fixtures')
        self.assert_cli_rejected_without_write(graph, 'disk spelling')

    def test_existing_output_and_unicode_alias_on_disk_fail_before_cli_writes(self):
        graph = self.failure_graph()
        graph['domains'].append({'id': 'other', 'title': 'Other', 'entrypoint': '知识/é/README.md'})
        self.write('知识/é/README.md', b'EXISTING VIEW\n')
        self.write('知识/e\u0301/user.md')
        names = {path.name for path in (self.root / '知识').iterdir()}
        if not {'é', 'e\u0301'} <= names:
            self.skipTest('Filesystem cannot retain Unicode-distinct sibling fixtures')
        self.assert_cli_rejected_without_write(graph, 'disk spelling')

    def test_domain_output_hardlink_to_source_fails_before_cli_writes(self):
        graph = self.failure_graph()
        self.hardlink('topic.md', '知识/01-编程/README.md')
        self.assert_cli_rejected_without_write(graph, 'cannot overwrite a knowledge document')

    def test_fixed_output_hardlink_to_source_fails_before_cli_writes(self):
        graph = self.failure_graph()
        self.hardlink('topic.md', '00_Index/UE专题.md')
        self.assert_cli_rejected_without_write(graph, 'cannot overwrite a knowledge document')

    def test_nested_output_hardlink_to_source_fails_before_cli_writes(self):
        graph = self.failure_graph()
        self.hardlink('topic.md', '知识/01-编程/New/README.md')
        self.assert_cli_rejected_without_write(graph, 'cannot overwrite a knowledge document')

    def test_outputs_hardlinked_to_each_other_fail_before_cli_writes(self):
        graph = self.failure_graph()
        self.hardlink('知识/01-编程/README.md', '00_Index/UE专题.md')
        self.assert_cli_rejected_without_write(graph, 'output paths collide')

    def test_output_hardlink_to_unregistered_user_file_fails_before_cli_writes(self):
        graph = self.failure_graph()
        self.hardlink('unrelated-user-content.bin', '00_Index/UE专题.md')
        self.assert_cli_rejected_without_write(graph, 'hardlink aliases outside the output plan')

    def test_directory_at_late_output_fails_before_cli_writes(self):
        graph = self.failure_graph()
        (self.root / '00_Index/UE专题.md').mkdir()
        self.write('00_Index/UE专题.md/retained-user-content.bin')
        self.assert_cli_rejected_without_write(graph, 'output must be a regular file')

    def test_nondirectory_parent_of_late_output_fails_before_cli_writes(self):
        graph = self.failure_graph()
        (self.root / '00_Index/跨域关系.md').unlink()
        (self.root / '00_Index').rmdir()
        self.write('00_Index')
        self.assert_cli_rejected_without_write(graph, 'parent must be a directory')

    def test_late_symlink_output_fails_before_cli_writes(self):
        graph = self.failure_graph()
        try:
            (self.root / '00_Index/UE专题.md').symlink_to(self.root / 'topic.md')
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f'Platform does not permit creating symlink fixtures: {exc}')
        self.assert_cli_rejected_without_write(graph, 'symlink/reparse')

    def test_normal_cli_generation_is_byte_exact_and_check_never_writes(self):
        graph = copy.deepcopy(self.graph)
        path = '知识/01-编程/语言与内存/topic.md'
        self.write(path, b'---\ntype: Concept\ntitle: "Topic"\n---\n# Topic\n')
        graph['documents'][0].update(path=path, technologies=['unreal-engine'])
        graph['concepts'] = [{'id': 'concept', 'title': '概念', 'primary': 'one', 'related': []}]
        self.save_graph(graph)
        self.write('unrelated-user-content.bin')
        # Frozen from the pre-fix renderer; check content, metadata, links, and LF bytes.
        expected_hashes = {
            '知识/01-编程/语言与内存/README.md': 'e4b7125863546f95538b29a2345af04498dbe51d89bc6b2a661aca3f164ce5a1',
            '知识/01-编程/README.md': 'bf3b2c260dcab09df895b31ce695b57e14420446eaceb8d5c7e41f9472b0199b',
            '00_Index/跨域关系.md': 'fd4d9e8b8ea65b13d40a9f1b2f4127ef4b100e13add8512cb9ec61ce1a326df4',
            '00_Index/UE专题.md': 'af08889deb685b0aec2ee28c5a5280318acb664bf0eaf77e61201b454f7db36a',
        }
        before = self.snapshot()
        outputs = render(self.root, graph)
        self.assertEqual(expected_hashes, {name: hashlib.sha256(text.encode('utf-8')).hexdigest()
                                           for name, text in outputs.items()})
        self.assertEqual(before, self.snapshot())
        result = self.cli('--check')
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('FAIL: generated navigation differs', result.stdout)
        self.assertEqual(before, self.snapshot())
        result = self.cli()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS: 4 generated knowledge views written', result.stdout)
        after = self.snapshot()
        expected = dict(before)
        expected.update({name: ('file', text.encode('utf-8')) for name, text in outputs.items()})
        self.assertEqual(expected, after)
        result = self.cli('--check')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(after, self.snapshot())
        self.write('00_Index/UE专题.md', b'USER EDIT\r\n')
        drift = self.snapshot()
        result = self.cli('--check')
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(drift, self.snapshot())

    def test_path_invariants(self):
        for path in ['知识/../工作日志/README.md', '知识/./README.md', '知识\\x\\README.md', '知识//x/README.md']:
            with self.assertRaises(ValueError):
                safe_path(self.root, path, output=True)

    def test_symlink_source_and_output_rejected(self):
        source = self.root / 'linked.md'
        try:
            source.symlink_to(self.root / 'topic.md')
        except (OSError, NotImplementedError):
            self.skipTest('Platform does not permit creating symlink fixtures')
        with self.assertRaises(ValueError):
            safe_path(self.root, 'linked.md')
        (self.root / '知识' / 'linked').symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(ValueError):
            safe_path(self.root, '知识/linked/README.md', output=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
