#!/usr/bin/env python3
"""Render deterministic domain/concept navigation from the sole identity graph."""
import argparse
import json
import os
import re
import stat
import unicodedata
from pathlib import Path, PurePosixPath

LABELS = {
    'concept': '概念与机制', 'source-analysis': '源码解析', 'case-study': '端到端案例',
    'reference': '速查参考', 'reading-note': '书籍阅读关联', 'journal': '工作日志关联',
    'experiment': '可复现实验', 'roadmap': '主题路线',
}


FIXED_VIEWS = {'00_Index/跨域关系.md', '00_Index/UE专题.md'}
# Match the migration tool's portable component rules without importing its dependencies.
BAD_COMPONENT = re.compile(r'[<>:"\\|?*\x00-\x1f\x7f-\x9f]|[. ]$')
DEVICE = re.compile(r'^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)', re.I)


def folded(value):
    return unicodedata.normalize('NFC', value).casefold()


def safe_path(root, value, output=False):
    """Reject all escapes/reparse parents before reading or writing."""
    if not isinstance(value, str) or not value or '\\' in value:
        raise ValueError('path must be a nonempty POSIX relative string')
    parts = PurePosixPath(value).parts
    if value.startswith('/') or any(part in ('', '..', '.') for part in value.split('/')) or ':' in value:
        raise ValueError('path outside repository')
    if any(BAD_COMPONENT.search(part) or DEVICE.match(part) for part in parts):
        raise ValueError('nonportable knowledge view path component')
    if output and value not in FIXED_VIEWS:
        if not 3 <= len(parts) <= 6 or parts[0] != '知识' or parts[-1] != 'README.md':
            raise ValueError('output is not an approved knowledge view')
    target = root / value
    cursor = root
    for part in parts:
        if cursor.is_dir():
            with os.scandir(cursor) as entries:
                if any(entry.name != part and folded(entry.name) == folded(part) for entry in entries):
                    raise ValueError('knowledge view path collides with disk spelling by case or Unicode normalization')
        cursor = cursor / part
        try:
            info = cursor.lstat()
        except FileNotFoundError:
            break
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('symlink/reparse point in knowledge view path')
        if cursor != target and not stat.S_ISDIR(info.st_mode):
            raise ValueError('knowledge view parent must be a directory')
        if cursor == target and output and not stat.S_ISREG(info.st_mode):
            raise ValueError('knowledge view output must be a regular file')
    if not target.resolve().is_relative_to(root.resolve()):
        raise ValueError('path outside repository')
    if not output and (not target.is_file() or target.suffix != '.md'):
        raise ValueError('source must be an existing Markdown file')
    return target


def nested_documents(domain, nodes):
    domain_root = str(PurePosixPath(domain['entrypoint']).parent)
    nested = {}
    for node in nodes:
        if node['domain'] != domain['id'] or not node['path'].startswith(domain_root + '/'):
            continue
        parent = PurePosixPath(node['path']).parent
        while str(parent) != domain_root:
            nested.setdefault(str(parent), [])
            if str(parent) == str(PurePosixPath(node['path']).parent):
                nested[str(parent)].append(node)
            parent = parent.parent
    return nested


def path_keys(root, value, target):
    """Include hardlinks and missing leaves below filesystem-aliased parents."""
    keys = {('path', folded(value))}
    suffix = []
    cursor = target
    while cursor != root:
        try:
            info = cursor.stat()
        except FileNotFoundError:
            pass
        else:
            keys.add(('inode', info.st_dev, info.st_ino, folded('/'.join(suffix))))
        suffix.insert(0, cursor.name)
        cursor = cursor.parent
    return keys


def preflight(root, graph):
    """Reject the complete plan before any writes, including predictable FS errors.

    This is not a multi-file transaction: concurrent filesystem changes or I/O
    failures after preflight can still interrupt the subsequent write loop.
    """
    paths = []
    for domain in graph['domains']:
        path = domain['entrypoint']
        safe_path(root, path, output=True)
        if len(PurePosixPath(path).parts) != 3:
            raise ValueError('domain entrypoint must be a direct knowledge-domain README')
        if path in FIXED_VIEWS:
            raise ValueError('domain output cannot replace another view')
        paths.append(path)
    source_keys = set()
    for node in graph['documents']:
        path = node['path']
        source_keys.update(path_keys(root, path, safe_path(root, path)))
    nested_views = [nested_documents(domain, graph['documents']) for domain in graph['domains']]
    paths.extend(directory + '/README.md' for nested in nested_views for directory in sorted(nested))
    paths.extend(sorted(FIXED_VIEWS))
    output_keys = set()
    linked_outputs = []
    for path in paths:
        target = safe_path(root, path, output=True)
        keys = path_keys(root, path, target)
        if keys & source_keys:
            raise ValueError(f'generated view cannot overwrite a knowledge document: {path}')
        if keys & output_keys:
            raise ValueError(f'generated output paths collide by case, Unicode normalization or filesystem alias: {path}')
        output_keys.update(keys)
        if target.exists() and target.stat().st_nlink > 1:
            linked_outputs.append(path)
    # An unregistered (or out-of-root) alias must not lose authored bytes either.
    if linked_outputs:
        raise ValueError(f'generated output has hardlink aliases outside the output plan: {linked_outputs[0]}')
    return nested_views


def title(root, path):
    text = safe_path(root, path).read_text(encoding='utf-8')
    match = re.search(r'^title:\s*["\']?(.*?)["\']?\s*$', text, re.M)
    label = match[1] if match else Path(path).stem
    return ''.join(c for c in label if not ('\x7f' <= c <= '\x9f')).replace('|', '\\|').replace('[', '\\[').replace(']', '\\]')


def link(root, source, target, label=None):
    relative = os.path.relpath(target, os.path.dirname(source) or '.').replace(os.sep, '/')
    return f'[{label or title(root, target)}](<{relative}>)'


def page(title_, body):
    return (f'---\ntype: Index\ntitle: "{title_}"\nstatus: stable\nverified: []\n'
            'maturity: L0\n---\n\n'
            f'# {title_}\n\n> 知识成熟度：L0（导航条目，不代表主题内容已实测）\n\n{body.rstrip()}\n')


def render(root, graph):
    nested_views = preflight(root, graph)
    result = {}
    nodes = graph['documents']
    by_id = {node['id']: node for node in nodes}
    for domain, nested in zip(graph['domains'], nested_views):
        path = domain['entrypoint']
        domain_root = str(PurePosixPath(path).parent)
        for directory, local_nodes in sorted(nested.items()):
            index = directory + '/README.md'
            section = '本分类只提供标准链接，正文、来源与证据仍各自维护。\n\n'
            children = sorted(key for key in nested if str(PurePosixPath(key).parent) == directory)
            if children:
                section += '## 子分类\n\n'
                section += ''.join('- ' + link(root, index, child + '/README.md', PurePosixPath(child).name) + '\n' for child in children) + '\n'
            section += '## 条目\n\n'
            section += ''.join('- ' + link(root, index, node['path']) + '（' + LABELS[node['kind']] + '）\n' for node in sorted(local_nodes, key=lambda n: n['path']))
            section += '\n' + link(root, index, path, domain['title']) + ' · ' + link(root, index, '知识/README.md', '八域总览') + '\n'
            result[index] = page(PurePosixPath(directory).name, section)
        body = ('本页按知识职责导航，每份正文只登记一个主域。概念、实现、案例和来源分别列出；'
                '阅读材料的关联不等于已验证其全部结论。\n\n'
                '正文迁移沿用稳定身份，不复制另一份权威正文。书籍与工作日志原文、日期、附件完整保留。\n\n')
        children = sorted(directory for directory in nested if str(PurePosixPath(directory).parent) == domain_root)
        if children:
            body += '## 分类目录\n\n'
            body += ''.join('- ' + link(root, path, child + '/README.md', PurePosixPath(child).name) + '\n' for child in children) + '\n'
        for kind, label in LABELS.items():
            selected = sorted((n for n in nodes if n['domain'] == domain['id'] and n['kind'] == kind), key=lambda n: n['path'])
            if not selected:
                continue
            body += f'## {label}\n\n'
            body += ''.join('- ' + link(root, path, n['path']) + '\n' for n in selected) + '\n'
        body += ('## 边界与扩展\n\n一个主题按主要问题确定主责，其他领域引用它。运行在服务端的玩法规则仍属于 Gameplay；'
                 'UE 源码分析按具体机制归类。新概念先找已有主文，再决定扩充或新建。\n\n'
                 + link(root, path, '知识/README.md', '八域总览') + ' · '
                 + link(root, path, '00_Index/跨域关系.md', '主责与关系') + '\n')
        result[path] = page(domain['title'], body)
    path = '00_Index/跨域关系.md'
    body = '每个概念登记唯一主责正文，补充文章通过实现、案例、证据或相关主题关联。以下入口由身份图统一登记；页面不复制正文。\n\n'
    for concept in graph['concepts']:
        body += '## ' + concept['title'] + '\n\n- 主责：' + link(root, path, by_id[concept['primary']]['path'])
        body += '\n- 关联：' + '、'.join(link(root, path, by_id[i]['path']) for i in concept['related']) + '\n\n'
    body += '[八域总览](../知识/README.md) · [原跨域详解](axes/跨域主题.md)\n'
    result[path] = page('跨域概念的主责与关系', body)
    path = '00_Index/UE专题.md'
    body = ('UE是技术栈，源码解析是内容类型，官方文档是来源。本视图按知识职责分组，'
            '由身份图的技术栈标签生成；物理搬迁不改变条目身份。\n\n')
    for domain in graph['domains']:
        selected = sorted((n for n in nodes if n['domain'] == domain['id'] and 'unreal-engine' in n.get('technologies', [])), key=lambda n: n['path'])
        if not selected:
            continue
        body += '## ' + domain['title'] + '\n\n'
        body += ''.join('- ' + link(root, path, n['path']) + ('（源码解析）' if n['kind'] == 'source-analysis' else '') + '\n' for n in selected) + '\n'
    body += ('## 官方文档与版本\n\n'
             '[UE官方文档](https://dev.epicgames.com/documentation/en-us/unreal-engine)用于查最新API和版本差异；'
             'Markdown整理保留准确来源与版本，原创解释、引用和示例应可区分。历史版本声明不等于本次运行过对应引擎。\n\n'
             '[八域总览](../知识/README.md) · [源码原始总索引](../游戏知识/12-引擎源码分析/README.md)\n')
    result[path] = page('UE专题、官方来源与源码总览', body)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--check', action='store_true', help='Do not write; fail if any generated view drifts')
    args = parser.parse_args()
    root = args.root.resolve()
    graph = json.loads((root / '.kb/knowledge-map.json').read_text(encoding='utf-8'))
    outputs = render(root, graph)
    for name in outputs:
        safe_path(root, name, output=True)
    drift = []
    for name, content in outputs.items():
        target = safe_path(root, name, output=True)
        if args.check:
            if not target.is_file() or target.read_bytes() != content.encode('utf-8'):
                drift.append(name)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding='utf-8', newline='\n')
    if drift:
        print('FAIL: generated navigation differs from knowledge map:')
        print('\n'.join(drift))
        return 1
    print(f'PASS: {len(outputs)} generated knowledge views' + (' match graph' if args.check else ' written'))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
