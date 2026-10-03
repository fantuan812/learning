#!/usr/bin/env python3
"""Plan and explicitly apply byte-preserving Markdown moves and local-link edits.

Requires scripts/requirements-knowledge.txt. The default is a read-only dry run;
--apply is the only repository-writing mode. --report must name a file outside
this repository. This does not edit graph identities, regenerate navigation,
change Git state, or consolidate prose. Reviewed prose edits must happen first.
Every run snapshots the current bytes, including authorized working-tree edits.
Apply requires a quiet, single-writer workspace. It is not an OS-level multi-file
transaction. Actual original inodes are retained in a private, same-filesystem
backup beside the report, even on success; never delete this backup automatically.
POSIX backup mode is 0700; Windows ACL behavior is not verified by this helper.
"""
from __future__ import annotations

import argparse
import ctypes
import errno
from collections import Counter
from dataclasses import dataclass
import hashlib
import html
from html.parser import HTMLParser
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import stat
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from urllib.parse import quote, unquote, urlsplit

from markdown_it import MarkdownIt
from markdown_it.common.utils import unescapeAll
from markdown_it.parser_inline import ParserInline
from markdown_it.rules_block import reference
from markdown_it.rules_inline import html_inline, image, link

from validate_knowledge import Invalid, folded, git, parse_markdown, strict_json

PROTECTED_PREFIXES = ("读书笔记/", "工作日志/")
PROTECTED_FILES = {"learning/log.md", "learning/lessons.md", "log.md"}
MOVE_FORBIDDEN_PREFIXES = PROTECTED_PREFIXES + ("evidence/", "scripts/", ".git/", ".kb/", ".agents/", ".codex/")
BAD_COMPONENT = re.compile(r'[<>:"\\|?*\x00-\x1f\x7f-\x9f]|[. ]$')
DEVICE = re.compile(r"^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)", re.I)
ENTITY_OR_ESCAPE = re.compile(r"\\[!\"#$%&'()*+,\-./:;<=>?@\[\]\\^_`{|}~]|&(?:#[xX][0-9a-fA-F]+|#[0-9]+|[A-Za-z][A-Za-z0-9]+);")
ATTR = re.compile(r'''([^\s/>=]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]*)))?''')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def protected(path):
    key = folded(path)
    return key in PROTECTED_FILES or any(key.startswith(folded(p)) for p in PROTECTED_PREFIXES)


def path_parts(value):
    if not isinstance(value, str) or not value or value.startswith("/"):
        raise Invalid(f"unsafe repository-relative path: {value!r}")
    parts = value.split("/")
    if any(p in {"", ".", ".."} or BAD_COMPONENT.search(p) or DEVICE.match(p) for p in parts):
        raise Invalid(f"nonportable or unsafe repository-relative path: {value!r}")
    return parts


def checked_path(root, value, *, missing=False, file=True):
    cursor = root
    for part in path_parts(value):
        if cursor.exists():
            if not cursor.is_dir():
                raise Invalid(f"ancestor is not a directory: {value}")
            names = os.listdir(cursor)
            clashes = [name for name in names if folded(name) == folded(part) and name != part]
            if clashes:
                raise Invalid(f"case/Unicode spelling collision: {value} ({clashes[0]})")
        cursor = cursor / part
        try:
            info = cursor.lstat()
        except FileNotFoundError:
            if missing:
                continue
            raise Invalid(f"missing path: {value}") from None
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise Invalid(f"symlink/reparse point forbidden: {value}")
    if not missing and file and not cursor.is_file():
        raise Invalid(f"not a regular file: {value}")
    return cursor


def git_inventory(root):
    # Do not fall back to walking the disk: ignored files are not migration input.
    listed = set(git(root, "ls-files", "--cached", "--others", "--exclude-standard", "-z").split("\0")) - {""}
    return listed, {p for p in listed if os.path.lexists(root / p)}


def reject_collisions(paths):
    seen = {}
    for path in sorted(paths):
        pieces = path_parts(path)
        for index in range(1, len(pieces) + 1):
            prefix = "/".join(pieces[:index])
            key = folded(prefix)
            if key in seen and seen[key] != prefix:
                raise Invalid(f"case/Unicode path collision: {seen[key]} and {prefix}")
            seen[key] = prefix


def body_offset(text):
    """Skip frontmatter lexically, never parse, normalize, or rewrite its YAML."""
    lines = text.splitlines(keepends=True)
    if not lines or lines[0].strip() != "---":
        return 0
    for index, line in enumerate(lines[1:], 1):
        if line.strip() == "---":
            return sum(map(len, lines[:index + 1]))
    raise Invalid("unclosed frontmatter")


def normalize_source(text, offset):
    chars, positions = [], []
    index = offset
    while index < len(text):
        char = text[index]
        if char == "\x00":
            raise Invalid("NUL is not supported in Markdown input")
        chars.append("\n" if char == "\r" else char)
        positions.append(index)
        index += 2 if text[index:index + 2] == "\r\n" else 1
    return "".join(chars), positions


@dataclass(frozen=True)
class Destination:
    start: int
    end: int
    url: str
    syntax: str
    raw: str


class HTMLDestinations(HTMLParser):
    """HTMLParser confirms real start tags; the lexer supplies attribute spans."""
    def __init__(self, source, emit):
        super().__init__(convert_charrefs=True)
        self.source = source
        self.emit = emit
        self.line_starts = [0] + [m.end() for m in re.finditer("\n", source)]

    def handle_starttag(self, tag, attrs):
        raw = self.get_starttag_text()
        row, column = self.getpos()
        base = self.line_starts[row - 1] + column
        opening = re.match(r"<[^\s/>]+", raw)
        if opening is None:
            return
        lexical = []
        for match in ATTR.finditer(raw, opening.end()):
            name = match.group(1).lower()
            group = next((i for i in (2, 3, 4) if match.group(i) is not None), None)
            if group is not None and name in {"href", "src"}:
                value = match.group(group)
                lexical.append((name, html.unescape(value)))
                self.emit(base + match.start(group), base + match.end(group), html.unescape(value), "html")
        expected = [(name, value) for name, value in attrs if name in {"href", "src"} and value is not None]
        if lexical != expected:
            raise Invalid("unsupported HTML attribute spelling; refusing an incomplete source-span map")

    handle_startendtag = handle_starttag


class SpanInline(ParserInline):
    def parse(self, src, md, env, tokens):
        previous = env.get("_origin")
        env["_origin"] = env.pop("_next_origin", None)
        try:
            return super().parse(src, md, env, tokens)
        finally:
            env["_origin"] = previous


class SpanParser:
    """Instrument the actual CommonMark rules, retaining lexical destinations.

    Link rules decide what is a link. No full-text path replacement is used.
    Parser source maps are proved against the original substring before edits.
    """
    def __init__(self, text):
        self.text = text
        self.body_start = body_offset(text)
        self.source, self.original_positions = normalize_source(text, self.body_start)
        self.lines = self.source.splitlines(keepends=True)
        self.line_starts = [0]
        for line in self.lines:
            self.line_starts.append(self.line_starts[-1] + len(line))
        self.destinations = []
        self.capture_stack = []
        self.md = MarkdownIt("commonmark", {"html": True, "inline_definitions": True})
        self.md.inline = SpanInline()
        helpers = dict(vars(self.md.helpers))
        original_destination = self.md.helpers.parseLinkDestination

        def capture(src, pos, maximum):
            result = original_destination(src, pos, maximum)
            if result.ok and self.capture_stack:
                self.capture_stack[-1].append((src, pos, result.pos, result.str))
            return result

        helpers["parseLinkDestination"] = capture
        self.md.helpers = SimpleNamespace(**helpers)
        self.md.inline.ruler.at("link", self.inline_rule(link, "link"))
        self.md.inline.ruler.at("image", self.inline_rule(image, "image"))
        self.md.inline.ruler.at("html_inline", self.html_rule)
        self.md.block.ruler.at("reference", self.reference_rule)
        self.md.core.ruler.at("inline", self.core_inline)

    def emit(self, mapping, start, end, url, syntax):
        if mapping is None or start >= end:
            # Empty destinations need no relocation: they refer to their owner.
            if start == end:
                return
            raise Invalid("missing parser source map")
        positions = mapping[start:end]
        if len(positions) != end - start or any(b != a + 1 for a, b in zip(positions, positions[1:])):
            raise Invalid("destination crosses a transformed source boundary")
        first, last = positions[0], positions[-1] + 1
        self.destinations.append(Destination(first, last, url, syntax, self.text[first:last]))

    def token_mapping(self, token):
        if token.map is None:
            raise Invalid("CommonMark block is missing source line information")
        start_line, end_line = token.map
        content_lines = token.content.splitlines(keepends=True)
        mapping = []
        cursor = start_line
        for content in content_lines:
            value = content.rstrip("\n")
            found = False
            while cursor < end_line:
                raw = self.lines[cursor]
                candidate = raw.rstrip("\n")
                at = candidate.find(value)
                if at >= 0:
                    if value and candidate.find(value, at + 1) >= 0:
                        raise Invalid("ambiguous parser-to-source line mapping")
                    absolute = self.line_starts[cursor] + at
                    mapping.extend(self.original_positions[absolute:absolute + len(value)])
                    if content.endswith("\n"):
                        mapping.append(self.original_positions[self.line_starts[cursor] + len(candidate)])
                    cursor += 1
                    found = True
                    break
                cursor += 1
            if not found:
                raise Invalid("unsupported parser indentation/source transformation")
        return mapping

    def core_inline(self, state):
        for token in state.tokens:
            if token.type == "inline":
                token.children = []
                # A missing map is fatal only if a link rule actually needs it.
                try:
                    origin = self.token_mapping(token)
                except Invalid:
                    origin = None
                state.env["_next_origin"] = origin
                state.md.inline.parse(token.content, state.md, state.env, token.children)
            elif token.type == "html_block":
                mapping = self.token_mapping(token)
                HTMLDestinations(token.content, lambda a, b, u, s: self.emit(mapping, a, b, u, s)).feed(token.content)

    def inline_rule(self, original, kind):
        def rule(state, silent):
            old_pos, old_count = state.pos, len(state.tokens)
            origin = state.env.get("_origin")
            captures = []
            self.capture_stack.append(captures)
            pending = None
            try:
                if kind == "image" and not silent and state.src[old_pos:old_pos + 2] == "![":
                    end = state.md.helpers.parseLinkLabel(state, old_pos + 1, False)
                    if end >= 0:
                        pending = origin[old_pos + 2:end] if origin is not None else None
                        state.env["_next_origin"] = pending
                result = original(state, silent)
            finally:
                self.capture_stack.pop()
                if kind == "image":
                    state.env.pop("_next_origin", None)
            if result and not silent and captures:
                src, start, end, decoded = captures[0]
                emitted = state.tokens[old_count:]
                urls = [t.attrGet("href" if kind == "link" else "src") for t in emitted if t.type == ("link_open" if kind == "link" else "image")]
                normalized = state.md.normalizeLink(decoded)
                # Failed inline syntax may fall back to a shortcut reference.
                if normalized in urls and state.pos > end and state.src[state.pos - 1:state.pos] == ")":
                    if src[start:start + 1] == "<":
                        start, end = start + 1, end - 1
                    self.emit(origin, start, end, normalized, "markdown")
            return result
        return rule

    def html_rule(self, state, silent):
        start = state.pos
        result = html_inline(state, silent)
        if result and not silent:
            content = state.src[start:state.pos]
            mapping = state.env.get("_origin")
            HTMLDestinations(content, lambda a, b, u, s: self.emit(mapping, start + a, start + b, u, s)).feed(content)
        return result

    def reference_rule(self, state, start_line, end_line, silent):
        captures = []
        self.capture_stack.append(captures)
        try:
            result = reference(state, start_line, end_line, silent)
        finally:
            self.capture_stack.pop()
        if result and not silent and captures:
            src, start, end, decoded = captures[0]
            lexical, mapping = "", []
            for line in range(start_line, state.line):
                first = state.bMarks[line] + state.tShift[line]
                last = state.eMarks[line] + 1
                lexical += state.src[first:last]
                mapping.extend(self.original_positions[first:last])
            if not lexical.startswith(src):
                raise Invalid("unsupported reference-definition source transformation")
            if src[start:start + 1] == "<":
                start, end = start + 1, end - 1
            self.emit(mapping, start, end, state.md.normalizeLink(decoded), "markdown")
        return result

    def parse(self):
        self.md.parse(self.source, {})
        unique = {}
        for destination in self.destinations:
            key = (destination.start, destination.end)
            if key in unique and unique[key] != destination:
                raise Invalid("inconsistent parser destination span")
            unique[key] = destination
        result = sorted(unique.values(), key=lambda d: (d.start, d.end))
        if any(a.end > b.start for a, b in zip(result, result[1:])):
            raise Invalid("overlapping destination spans")
        return result


def local_target(owner, url):
    try:
        parts = urlsplit(url)
    except ValueError as exc:
        raise Invalid(f"invalid URL {url!r}: {exc}") from exc
    if parts.scheme or parts.netloc or url.startswith("//"):
        return None
    value = unquote(parts.path, encoding="utf-8", errors="strict")
    if not value:
        return owner
    if "\\" in value or "\x00" in value:
        raise Invalid(f"unsafe local URL: {url!r}")
    result = posixpath.normpath(value.lstrip("/") if value.startswith("/") else posixpath.join(posixpath.dirname(owner), value))
    if result == ".." or result.startswith("../"):
        raise Invalid(f"local URL escapes repository: {url!r}")
    return result


def suffix_of(url):
    match = re.search(r"[?#]", url)
    return url[match.start():] if match else ""


def raw_suffix(raw, syntax):
    """Keep the exact query/fragment spelling, including escapes and entities."""
    semantic, starts, cursor = "", [], 0
    for match in ENTITY_OR_ESCAPE.finditer(raw):
        semantic += raw[cursor:match.start()]
        starts.extend(range(cursor, match.start()))
        decoded = html.unescape(match.group()) if syntax == "html" else unescapeAll(match.group())
        semantic += decoded
        starts.extend([match.start()] * len(decoded))
        cursor = match.end()
    semantic += raw[cursor:]
    starts.extend(range(cursor, len(raw)))
    delimiter = re.search(r"[?#]", semantic)
    return raw[starts[delimiter.start()]:] if delimiter else ""


def rewritten_destination(owner, new_owner, destination, moves):
    target = local_target(owner, destination.url)
    if target is None:
        return destination.raw
    desired = moves.get(target, target)
    if local_target(new_owner, destination.url) == desired:
        return destination.raw
    path = urlsplit(destination.url).path
    # Empty-path links remain attached to their owner, even when it moves.
    if not path:
        return destination.raw
    relative = "/" + desired if path.startswith("/") else posixpath.relpath(desired, posixpath.dirname(new_owner) or ".")
    # Keep non-ASCII names readable; escape punctuation meaningful to Markdown/URLs.
    encoded_path = "".join(char if ord(char) >= 128 else quote(char, safe="/-._~") for char in relative)
    result = encoded_path + raw_suffix(destination.raw, destination.syntax)
    if destination.syntax == "html":
        # The suffix is already source-escaped, so escape the new path only.
        suffix = raw_suffix(destination.raw, destination.syntax)
        result = html.escape(encoded_path, quote=True) + suffix
    return result


def parsed_urls(text):
    return [url for url, _ in parse_markdown(text[body_offset(text):]).links]


def identity(owner, url, moves=None):
    target = local_target(owner, url)
    if target is None:
        return ("external", url)
    return ("local", (moves or {}).get(target, target), suffix_of(url))


def apply_edits(text, edits):
    output, cursor, delta = [], 0, 0
    for edit in edits:
        start, end = edit["start"], edit["end"]
        if text[start:end] != edit["old"] or start < cursor:
            raise Invalid("edit source span does not match the original")
        output.extend((text[cursor:start], edit["new"]))
        edit["output_start"] = start + delta
        edit["output_end"] = start + delta + len(edit["new"])
        edit["start_byte"] = len(text[:start].encode("utf-8"))
        edit["end_byte"] = len(text[:end].encode("utf-8"))
        delta += len(edit["new"]) - (end - start)
        cursor = end
    output.append(text[cursor:])
    result = "".join(output)
    inverse = result
    for edit in reversed(edits):
        first, last = edit["output_start"], edit["output_end"]
        if inverse[first:last] != edit["new"]:
            raise Invalid("inverse source span mismatch")
        inverse = inverse[:first] + edit["old"] + inverse[last:]
    if inverse != text:
        raise Invalid("inverse transformation did not reconstruct source bytes")
    return result, digest(inverse.encode("utf-8"))


def target_exists(root, target, visible):
    if target == ".":
        return True
    checked_path(root, target, file=False)
    return target in visible or any(path.startswith(target.rstrip("/") + "/") for path in visible)


def validate_plan(root, plan, graph, listed, visible):
    if not isinstance(plan, dict) or type(plan.get("version")) is not int or plan["version"] != 1:
        raise Invalid("plan version must be integer 1")
    base = plan.get("base_commit")
    if not isinstance(base, str) or not re.fullmatch(r"[0-9a-fA-F]{40,64}", base):
        raise Invalid("plan base_commit must be a full commit SHA")
    if git(root, "rev-parse", "HEAD").strip() != base:
        raise Invalid("plan base_commit does not match current HEAD")
    if not isinstance(graph, dict) or graph.get("version") != 1:
        raise Invalid("invalid knowledge graph version")
    by_id, by_path, domains = {}, {}, {}
    for row in graph.get("documents", []):
        if not isinstance(row, dict) or not isinstance(row.get("id"), str) or not row["id"]:
            raise Invalid("graph document has no stable id")
        if row["id"] in by_id or row.get("path") in by_path:
            raise Invalid("duplicate graph source id/path")
        by_id[row["id"]] = row
        by_path[row.get("path")] = row
    for row in graph.get("domains", []):
        if row.get("id") in domains:
            raise Invalid("duplicate graph domain")
        domains[row.get("id")] = row
    operations = plan.get("operations")
    if not isinstance(operations, list) or not operations:
        raise Invalid("plan requires a nonempty operations list")
    moves, targets, ids = {}, set(), set()
    for operation in operations:
        if not isinstance(operation, dict):
            raise Invalid("operation must be an object")
        source, target, item_id = (operation.get(k) for k in ("source", "target", "id"))
        path_parts(source)
        path_parts(target)
        if source in moves or target in targets or item_id in ids:
            raise Invalid(f"duplicate migration source/target/id: {source}")
        row = by_id.get(item_id)
        if row is None or row.get("path") != source or by_path.get(source) != row:
            raise Invalid(f"graph source/id disagreement: {source}")
        if operation.get("source_domain", row.get("domain")) != row.get("domain"):
            raise Invalid(f"graph source_domain disagreement: {source}")
        if operation.get("kind") != row.get("kind"):
            raise Invalid(f"graph kind disagreement: {source}")
        if operation.get("action") not in {"move", "consolidate_reference_then_move"}:
            raise Invalid(f"unsupported migration action: {source}")
        if operation.get("preserve_text_except_local_links") is not True:
            raise Invalid(f"operation must explicitly preserve prose: {source}")
        if operation["action"] == "consolidate_reference_then_move" and operation.get("kind") != "reference":
            raise Invalid(f"consolidation move must identify a reference: {source}")
        if source not in visible or PurePosixPath(source).suffix.lower() != ".md" or PurePosixPath(target).suffix.lower() != ".md":
            raise Invalid(f"source/target must identify Git-visible Markdown: {source}")
        if protected(source) or any(folded(source).startswith(folded(p)) for p in MOVE_FORBIDDEN_PREFIXES):
            raise Invalid(f"protected source cannot move: {source}")
        if protected(target) or any(folded(target).startswith(folded(p)) for p in MOVE_FORBIDDEN_PREFIXES):
            raise Invalid(f"protected destination: {target}")
        checked_path(root, source)
        checked_path(root, target, missing=True)
        if target in listed or os.path.lexists(root / target):
            raise Invalid(f"refusing to overwrite existing target: {target}")
        domain = domains.get(operation.get("domain"))
        if domain is None or not isinstance(domain.get("entrypoint"), str):
            raise Invalid(f"unknown target domain: {source}")
        domain_parent = PurePosixPath(domain["entrypoint"]).parent
        target_path = PurePosixPath(target)
        if not target_path.is_relative_to(domain_parent) or len(target_path.parts) < len(domain_parent.parts) + 2:
            raise Invalid(f"target requires its graph domain and semantic subdirectory: {target}")
        if target_path.name != PurePosixPath(source).name:
            raise Invalid(f"move must preserve original filename: {target}")
        ignored = subprocess.run(["git", "-c", f"safe.directory={root}", "-C", str(root), "check-ignore", "--no-index", "--quiet", "--", target], capture_output=True)
        if ignored.returncode != 1:
            raise Invalid(f"target is ignored or ignore check failed: {target}")
        moves[source] = target
        targets.add(target)
        ids.add(item_id)
    reject_collisions(listed | targets)
    return moves


def make_plan(root, plan_path, graph_path):
    root = Path(root).resolve()
    listed, visible = git_inventory(root)
    plan_bytes = Path(plan_path).read_bytes()
    graph_file = checked_path(root, graph_path)
    graph_bytes = graph_file.read_bytes()
    plan = strict_json(plan_bytes.decode("utf-8"))
    graph = strict_json(graph_bytes.decode("utf-8"))
    report = {"version": 1, "mode": "dry-run", "ok": False, "base_commit": plan.get("base_commit"), "plan_sha256": digest(plan_bytes), "graph_sha256": digest(graph_bytes), "markdown_scanned": 0, "operations": [], "affected_files": [], "files": [], "errors": [], "applied": False}
    moves = validate_plan(root, plan, graph, listed, visible)
    markdown = sorted(p for p in visible if PurePosixPath(p).suffix.lower() == ".md")
    snapshots, outputs = {}, {}
    for owner in markdown:
        try:
            path = checked_path(root, owner)
            original = path.read_bytes()
            snapshots[owner] = original
            if original.startswith(b"\xef\xbb\xbf"):
                raise Invalid("UTF-8 BOM is unsupported; input must be plain UTF-8")
            text = original.decode("utf-8", "strict")
            before_urls = parsed_urls(text)
            destinations = SpanParser(text).parse()
            new_owner = moves.get(owner, owner)
            edits = []
            for destination in destinations:
                replacement = rewritten_destination(owner, new_owner, destination, moves)
                if replacement != destination.raw:
                    edits.append({"start": destination.start, "end": destination.end, "old": destination.raw, "new": replacement, "syntax": destination.syntax})
            if edits and protected(owner):
                raise Invalid("protected book/log source requires link edits; explicit review is required")
            after, inverse_hash = apply_edits(text, edits)
            before = Counter(identity(owner, url, moves) for url in before_urls)
            actual = Counter(identity(new_owner, url) for url in parsed_urls(after))
            if before != actual:
                raise Invalid(f"parsed link identity multiset mismatch; missing={list((before - actual).elements())[:4]}, unexpected={list((actual - before).elements())[:4]}")
            # Definitions can be unused. Validate and migrate them too, while
            # parsed links independently prove that rendered references survive.
            all_urls = set(before_urls) | {d.url for d in destinations}
            for url in sorted(all_urls):
                target = local_target(owner, url)
                if target is not None and not target_exists(root, target, visible):
                    raise Invalid(f"local target is not Git-visible: {url!r} -> {target}")
            # Wikilinks are not standard Markdown. Relevant occurrences must be
            # resolved manually instead of being silently made stale.
            body = text[body_offset(text):]
            tokens = MarkdownIt("commonmark", {"html": True}).parse(body)
            visible_text = "\n".join(child.content for token in tokens if token.type == "inline" for child in token.children or [] if child.type == "text")
            for wiki in re.findall(r"!?\[\[([^\]\n]+)\]\]", visible_text):
                raw_target = wiki.split("|", 1)[0].split("#", 1)[0]
                candidates = {raw_target, raw_target + ".md"}
                if owner in moves or any(p in candidates or PurePosixPath(p).name in candidates or PurePosixPath(p).stem in candidates for p in moves):
                    raise Invalid("relevant wikilink is unsupported; migrate it explicitly first")
            output = after.encode("utf-8")
            if output != original or new_owner != owner:
                outputs[owner] = (new_owner, output)
                report["files"].append({"source": owner, "target": new_owner, "source_sha256": digest(original), "target_sha256": digest(output), "inverse_proof_sha256": inverse_hash, "source_bytes": len(original), "target_bytes": len(output), "link_edits": edits})
        except (Invalid, OSError, UnicodeError, ValueError) as exc:
            report["errors"].append({"path": owner, "message": str(exc)})
        report["markdown_scanned"] += 1
    for operation in plan["operations"]:
        source = operation["source"]
        report["operations"].append({"id": operation["id"], "source": source, "target": operation["target"], "source_sha256": digest(snapshots[source]) if source in snapshots else None, "target_sha256": digest(outputs[source][1]) if source in outputs else None})
    report["affected_files"] = sorted({p for source, (target, _) in outputs.items() for p in (source, target)})
    proof = [(f["source"], f["target"], f["source_sha256"], f["target_sha256"], f["inverse_proof_sha256"]) for f in report["files"]]
    report["inverse_transform_proof_sha256"] = digest(json.dumps(proof, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))
    report["ok"] = not report["errors"]
    return report, snapshots, outputs, (listed, visible, graph_bytes, plan_bytes)


def atomic_write(path, data, *, exclusive=False, mode=0o644):
    fd, temporary = tempfile.mkstemp(prefix=".kb-migration-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        if exclusive:
            # link() is an atomic no-clobber install, unlike replace().
            os.link(temporary, path)
            os.unlink(temporary)
        else:
            os.replace(temporary, path)
    finally:
        if os.path.lexists(temporary):
            os.unlink(temporary)


def rename_noreplace(source, target):
    """Atomic same-filesystem capture; never overwrite a recovery file.

    Linux renameat2 is required here rather than an unsafe link/unlink emulation.
    Windows os.rename also refuses to replace an existing destination. Other
    platforms fail closed before changing repository files.
    """
    if os.name == "nt":
        os.rename(source, target)
        return
    if not sys.platform.startswith("linux"):
        raise Invalid("atomic no-clobber capture is unsupported on this platform")
    library = ctypes.CDLL(None, use_errno=True)
    rename = getattr(library, "renameat2", None)
    if rename is None:
        raise Invalid("libc renameat2 is required for safe original-file capture")
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(-100, os.fsencode(source), -100, os.fsencode(target), 1) != 0:
        error = ctypes.get_errno()
        if error in {errno.ENOSYS, errno.EINVAL, errno.EXDEV}:
            raise Invalid(f"atomic same-filesystem capture unavailable: {os.strerror(error)}")
        raise OSError(error, os.strerror(error), str(source), None, str(target))


def apply_plan(root, plan_path, graph_path, report, snapshots, outputs, baseline, *, report_path):
    """Apply in a quiet workspace, retaining originals and partial-result evidence.

    Capturing the actual source inode closes check-to-unlink/replace data loss.
    A noncooperating writer may still hold an open descriptor or recreate a path;
    retained files preserve its bytes, but this is not a global filesystem lock.
    """
    if not report["ok"]:
        raise Invalid("validation failed; no repository writes are permitted")
    listed, visible, graph_bytes, plan_bytes = baseline
    if git_inventory(root) != (listed, visible) or checked_path(root, graph_path).read_bytes() != graph_bytes or Path(plan_path).read_bytes() != plan_bytes:
        raise Invalid("inventory, graph, or plan changed during validation; rerun")
    if git(root, "rev-parse", "HEAD").strip() != report["base_commit"]:
        raise Invalid("HEAD changed during validation; rerun")
    for source, original in snapshots.items():
        if checked_path(root, source).read_bytes() != original:
            raise Invalid(f"source changed during validation: {source}")
    report_parent = Path(report_path).resolve().parent
    if report_parent.is_relative_to(root):
        raise Invalid("backup/report directory must be outside the repository")
    report_parent.mkdir(parents=True, exist_ok=True)
    device = root.stat().st_dev
    if report_parent.stat().st_dev != device:
        raise Invalid("backup/report directory must be on the repository filesystem")
    modes = {}
    for source, (target, _) in outputs.items():
        original_path = checked_path(root, source)
        if original_path.stat().st_dev != device:
            raise Invalid(f"source is on a different filesystem: {source}")
        modes[source] = stat.S_IMODE(original_path.stat().st_mode)
        ancestor = checked_path(root, target, missing=True).parent
        while not ancestor.exists():
            ancestor = ancestor.parent
        if ancestor.stat().st_dev != device:
            raise Invalid(f"target is on a different filesystem: {target}")

    backup = Path(tempfile.mkdtemp(prefix="knowledge-migration-backup-", dir=report_parent))
    # Probe the actual filesystem's no-clobber rename support before repo writes.
    probe = backup / ".capture-probe"
    probe.write_bytes(b"")
    rename_noreplace(probe, backup / ".capture-probe-retained")
    recovery = {"backup_directory": str(backup), "journal": str(backup / "recovery.json"),
                "workspace_requirement": "quiet single-writer workspace; not an OS-level multi-file transaction",
                "retention": "Original inodes and rollback captures are retained on success and failure; do not purge automatically.",
                "files": [{"source": source, "target": target, "backup": str(backup / "originals" / source),
                           "expected_sha256": digest(snapshots[source]), "captured_sha256": None, "state": "pending"}
                          for source, (target, _) in outputs.items()], "rollback_captures": []}
    report["recovery"] = recovery
    records = {record["source"]: record for record in recovery["files"]}
    installed, captured = [], []

    def persist():
        atomic_write(backup / "recovery.json", (json.dumps(recovery, ensure_ascii=False, indent=2) + "\n").encode("utf-8"), mode=0o600)

    def capture(source):
        record = records[source]
        retained = Path(record["backup"])
        retained.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        original = checked_path(root, source)
        # This check is diagnostic; the authoritative check is of the inode
        # actually captured by the atomic rename immediately below.
        if original.read_bytes() != snapshots[source]:
            raise Invalid(f"source changed immediately before capture: {source}")
        rename_noreplace(original, retained)
        captured.append(source)
        record["state"] = "captured"
        actual = retained.read_bytes()
        record["captured_sha256"] = digest(actual)
        persist()
        if actual != snapshots[source]:
            raise Invalid(f"source changed during capture; edited bytes retained at {retained}")

    persist()  # All recovery paths exist in a journal before any repo write.
    try:
        # Install moved targets first. Never replace an independently created file.
        for source, (target, content) in outputs.items():
            if source == target:
                continue
            destination = checked_path(root, target, missing=True)
            destination.parent.mkdir(parents=True, exist_ok=True)
            checked_path(root, target, missing=True)
            atomic_write(destination, content, exclusive=True, mode=modes[source])
            installed.append((source, target))
        # In-place rewrites also preserve the actual original inode first.
        for source, (target, content) in outputs.items():
            if source != target:
                continue
            capture(source)
            atomic_write(checked_path(root, target, missing=True), content, exclusive=True, mode=modes[source])
            installed.append((source, target))
        # No source unlink exists: each original becomes a retained backup.
        for source, (target, content) in outputs.items():
            if source == target:
                continue
            if checked_path(root, target).read_bytes() != content:
                raise Invalid(f"target changed before source capture: {target}")
            capture(source)
        for source, (target, content) in outputs.items():
            if checked_path(root, target).read_bytes() != content:
                raise Invalid(f"post-write hash mismatch: {target}")
            if source != target and os.path.lexists(root / source):
                raise Invalid(f"concurrent writer recreated moved source: {source}")
            if Path(records[source]["backup"]).read_bytes() != snapshots[source]:
                raise Invalid(f"retained original changed through an open descriptor: {source}")
        for record in recovery["files"]:
            record["state"] = "retained_after_success"
        persist()
        report["applied"] = True
    except Exception:
        rollback_errors = []
        # Capture partial outputs instead of deleting them: a concurrent edit of
        # an installed target is recoverable too, including during rollback.
        for source, target in reversed(installed):
            path = root / target
            retained = backup / "rollback-targets" / target
            try:
                if os.path.lexists(path):
                    retained.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
                    checked_path(root, target)
                    rename_noreplace(path, retained)
                    recovery["rollback_captures"].append({"source": target, "backup": str(retained), "captured_sha256": digest(retained.read_bytes())})
            except Exception as exc:
                rollback_errors.append(f"retain partial output {target}: {exc}")
        for source in reversed(captured):
            record = records[source]
            try:
                # A hard link restores the captured original inode without
                # overwriting a concurrently recreated source or dropping edits
                # made through an already-open descriptor. The backup remains.
                checked_path(root, source, missing=True)
                os.link(record["backup"], root / source)
                record["state"] = "restored_with_backup_retained"
            except Exception as exc:
                record["state"] = "backup_retained_restore_blocked"
                rollback_errors.append(f"restore {source} from {record['backup']}: {exc}")
        report["rollback_errors"] = rollback_errors
        recovery["rollback_errors"] = rollback_errors
        try:
            persist()
        except Exception as exc:
            report["rollback_errors"].append(f"recovery journal update failed; retained directory {backup}: {exc}")
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--graph", default=".kb/knowledge-map.json")
    parser.add_argument("--report", type=Path, required=True, help="explicit output JSON path outside the repository")
    parser.add_argument("--apply", action="store_true", help="perform the completely validated moves and lexical link edits")
    args = parser.parse_args(argv)
    root, report_path = args.root.resolve(), args.report.absolute()
    if report_path.resolve().is_relative_to(root):
        parser.error("--report must be outside the repository")
    if report_path.is_symlink():
        parser.error("--report may not be a symlink")
    report = {"version": 1, "ok": False, "applied": False, "errors": [], "affected_files": [], "files": []}
    try:
        report, snapshots, outputs, baseline = make_plan(root, args.plan, args.graph)
        if args.apply and report["ok"]:
            apply_plan(root, args.plan, args.graph, report, snapshots, outputs, baseline, report_path=report_path)
    except (Invalid, OSError, UnicodeError, ValueError, TypeError, KeyError) as exc:
        report["ok"] = False
        report["errors"].append({"path": "", "message": str(exc)})
    report["mode"] = "apply" if args.apply else "dry-run"
    report["error_count"] = len(report["errors"])
    report_path.parent.mkdir(parents=True, exist_ok=True)
    atomic_write(report_path, (json.dumps(report, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    print(f"{'PASS' if report['ok'] else 'FAIL'} {report['mode']}: {len(report.get('operations', []))} moves; {len(report['files'])} affected Markdown files; {report['error_count']} errors")
    for issue in report["errors"][:10]:
        print(f"{issue['path']}: {issue['message']}")
    print(f"Report: {report_path}")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
