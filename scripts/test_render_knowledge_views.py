#!/usr/bin/env python3
"""Safety and deterministic rendering tests; all files are disposable fixtures."""
import copy
import tempfile
import unittest
from pathlib import Path
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

    def test_deterministic_and_no_write(self):
        before = sorted(str(p) for p in self.root.rglob('*'))
        a = render(self.root, self.graph)
        self.assertEqual(a, render(self.root, self.graph))
        self.assertEqual(before, sorted(str(p) for p in self.root.rglob('*')))
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
        before = {str(p): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        with self.assertRaises(ValueError):
            render(self.root, graph)
        self.assertEqual(before, {str(p): p.read_bytes() for p in self.root.rglob('*') if p.is_file()})

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
