#!/usr/bin/env python3
"""Read-only knowledge graph, real YAML, and CommonMark link validation.

Install scripts/requirements-knowledge.txt. By default every in-scope Markdown
file is checked. --base-ref restricts YAML/link *sources* to changed and new
Markdown, including working-tree changes; graph, coverage, and path checks stay
global. A changed-scope pass is never a claim that the entire library passes.
Local CommonMark links/images and raw HTML href/src attributes are checked.
External URLs, HTML srcset, CSS URLs, and script-generated URLs are not fetched
or validated. Unknown YAML fields are retained, never rewritten.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict, deque
from dataclasses import asdict, dataclass
from datetime import date, datetime
from html.parser import HTMLParser
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
import unicodedata
from urllib.parse import unquote, urlsplit

import yaml
from markdown_it import MarkdownIt
from yaml.constructor import ConstructorError

DOMAINS = frozenset({"systems", "algorithms", "engine", "simulation", "gameplay", "ai", "online", "engineering"})
KINDS = frozenset({"concept", "source-analysis", "case-study", "reference", "reading-note", "journal", "experiment", "roadmap"})
RELATIONS = frozenset({"prerequisite", "implemented_by", "applied_in", "verified_by", "related", "reads"})
COVERAGE_RESERVED = frozenset({"readme.md", "index.md", "log.md", "skill.md"})
EXCLUDED_PARTS = frozenset({".img-work", ".workbuddy"})
STRING_FIELDS = ("type", "title", "description", "resource", "status", "maturity", "domain", "subdomain", "topic", "canonical", "scope")
ACTOR = re.compile(r"^(?:human:.+|process:.+|[^\s/]+/[^\s/]+)$")
DATETIME = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$")
CONTROL = re.compile(r"[\x00-\x1f\x7f-\x9f]")
INSERTION_TEMPLATE = "references/templates/OKF-知识条目.md"


@dataclass(frozen=True)
class Issue:
    code: str
    path: str
    message: str
    line: int | None = None


class Invalid(ValueError):
    pass


class UniqueLoader(yaml.SafeLoader):
    """SafeLoader with duplicate-key rejection, including nested mappings."""

    def construct_mapping(self, node, deep=False):
        if not isinstance(node, yaml.MappingNode):
            raise ConstructorError(None, None, "expected a mapping", node.start_mark)
        self.flatten_mapping(node)
        result = {}
        for key_node, value_node in node.value:
            key = self.construct_object(key_node, deep=deep)
            try:
                duplicate = key in result
            except TypeError as exc:
                raise ConstructorError(None, None, "unhashable mapping key", key_node.start_mark) from exc
            if duplicate:
                raise ConstructorError(None, None, f"duplicate key: {key!r}", key_node.start_mark)
            result[key] = self.construct_object(value_node, deep=deep)
        return result


def json_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise Invalid(f"duplicate JSON key: {key!r}")
        result[key] = value
    return result


def strict_json(text):
    def bad_constant(value):
        raise Invalid(f"non-JSON constant: {value}")
    return json.loads(text, object_pairs_hook=json_pairs, parse_constant=bad_constant)


def folded(value):
    return unicodedata.normalize("NFC", value).casefold()


def nonempty(value):
    return isinstance(value, str) and bool(value.strip())


def git(root, *args):
    process = subprocess.run(["git", "-c", f"safe.directory={root}", "-C", str(root), *args], capture_output=True)
    if process.returncode:
        raise Invalid(f"git {' '.join(args)} failed: {process.stderr.decode('utf-8', 'replace').strip()}")
    return process.stdout.decode("utf-8", "strict")


def excluded(path):
    parts = PurePosixPath(path).parts
    return any(folded(p) in EXCLUDED_PARTS for p in parts) or any(
        folded(parts[i]) == "references" and folded(parts[i + 1]) == "unrealengine-5.8-docs"
        for i in range(len(parts) - 1)
    )


def operational(path):
    parts = PurePosixPath(path).parts
    return (len(parts) >= 4 and parts[:2] == (".agents", "skills") and folded(parts[-1]) == "skill.md") or folded(path) == "learning/log.md"


def safe_path(root, value, *, directory=False, allow_root=False, exact_case=True):
    """Validate portable relative paths and every ancestor without following links."""
    if not nonempty(value) or CONTROL.search(value) or "\\" in value or ":" in value:
        raise Invalid(f"unsafe repository-relative path: {value!r}")
    if allow_root and value == ".":
        return root
    parts = value.split("/")
    if any(p in ("", ".", "..") for p in parts) or PurePosixPath(value).is_absolute():
        raise Invalid(f"unsafe repository-relative path: {value!r}")
    cursor = root
    for part in parts:
        if exact_case and cursor.is_dir():
            with os.scandir(cursor) as entries:
                names = {entry.name for entry in entries}
            if part not in names and any(folded(name) == folded(part) for name in names):
                raise Invalid(f"path spelling/case/Unicode differs from disk: {value}")
        cursor = cursor / part
        try:
            metadata = cursor.lstat()
        except FileNotFoundError:
            raise Invalid(f"missing {'directory' if directory else 'file'}: {value}") from None
        if stat.S_ISLNK(metadata.st_mode) or getattr(metadata, "st_file_attributes", 0) & 0x400:
            raise Invalid(f"symlink/reparse point is not allowed: {value}")
    if directory and not cursor.is_dir():
        raise Invalid(f"not a directory: {value}")
    if not directory and not cursor.is_file():
        raise Invalid(f"not a file: {value}")
    return cursor


def validate_legacy_path(root, value):
    """A historical Markdown identity need not exist; existing ancestors stay safe."""
    if not nonempty(value) or CONTROL.search(value) or "\\" in value or ":" in value:
        raise Invalid(f"unsafe legacy path: {value!r}")
    parts = value.split("/")
    if any(part in ("", ".", "..") for part in parts) or PurePosixPath(value).is_absolute():
        raise Invalid(f"unsafe legacy path: {value!r}")
    if PurePosixPath(value).suffix.casefold() != ".md":
        raise Invalid(f"legacy path must identify Markdown: {value!r}")
    cursor = root
    for part in parts:
        cursor = cursor / part
        try:
            metadata = cursor.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(metadata.st_mode) or getattr(metadata, "st_file_attributes", 0) & 0x400:
            raise Invalid(f"symlink/reparse point in legacy path: {value!r}")
    return value


def inventory(root):
    paths = set(git(root, "ls-files", "--cached", "--others", "--exclude-standard", "-z").split("\0")) - {""}
    # A deleted tracked file is not an extant knowledge document. A graph/link to
    # that path still fails. Do not discover ignored caches by recursive walking.
    return {p for p in paths if not excluded(p) and os.path.lexists(root / p)}


def changed_markdown(root, base_ref, all_markdown):
    base = git(root, "rev-parse", "--verify", "--end-of-options", f"{base_ref}^{{commit}}").strip()
    changed = set(git(root, "diff", "--name-only", "-z", "--diff-filter=ACMRTUXB", base, "--").split("\0"))
    changed.update(git(root, "ls-files", "--others", "--exclude-standard", "-z").split("\0"))
    return all_markdown & changed


def frontmatter(text):
    """Return (mapping or None, Markdown body, body line offset)."""
    lines = text.splitlines(keepends=True)
    if not lines or lines[0].strip() != "---":
        return None, text, 0
    end = next((i for i, line in enumerate(lines[1:], 1) if line.strip() == "---"), None)
    if end is None:
        raise Invalid("unclosed YAML frontmatter")
    loaded = yaml.load("".join(lines[1:end]), Loader=UniqueLoader)
    if not isinstance(loaded, dict):
        raise Invalid("frontmatter must be a YAML mapping")
    if not all(isinstance(k, str) for k in loaded):
        raise Invalid("frontmatter field names must be strings")
    return loaded, "".join(lines[end + 1:]), end + 1


def is_date(value):
    if type(value) is date:
        return True
    if not isinstance(value, str) or not re.fullmatch(r"\d{4}-\d{2}-\d{2}", value):
        return False
    try:
        date.fromisoformat(value)
        return True
    except ValueError:
        return False


def is_datetime(value):
    if isinstance(value, datetime):
        return value.tzinfo is not None
    if not isinstance(value, str) or not DATETIME.fullmatch(value):
        return False
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00")).tzinfo is not None
    except ValueError:
        return False


def valid_actor(value):
    return nonempty(value) and ACTOR.fullmatch(value) is not None


def body_maturity_markers(text):
    """Explicit standalone/blockquote declarations, excluding Markdown code."""
    lines = text.splitlines()
    offset = 0
    if lines and lines[0].strip() == "---":
        end = next((i for i, line in enumerate(lines[1:], 1) if line.strip() == "---"), None)
        if end is not None:
            offset = end + 1
            lines = lines[offset:]
    declaration = re.compile(
        r"^ {0,3}(?:>\s*)*(?:\*{1,2})?知识成熟度(?:\*{1,2})?\s*[:：]\s*"
        r"(?:\*{1,2})?(L[0-9]+)(?:\*{1,2})?(?=$|[\s。.,，；;（(])"
    )
    for token in MarkdownIt("commonmark").parse("\n".join(lines)):
        if token.type != "inline" or token.map is None:
            continue
        # A CommonMark inline code span can cross physical lines. Checking raw
        # lines alone would mistake a declaration inside that span for metadata.
        visible = "".join(
            child.content if child.type == "text" else "\n" if child.type in {"softbreak", "hardbreak"}
            else "\ufffc" if child.type == "code_inline" else ""
            for child in token.children or []
        )
        visible_levels = {match.group(1) for line in visible.splitlines() if (match := declaration.match(line))}
        for index in range(token.map[0], token.map[1]):
            match = declaration.match(lines[index])
            if match and match.group(1) in visible_levels:
                yield match.group(1), index + offset + 1


def metadata_errors(path, data, text):
    name = folded(PurePosixPath(path).name)
    reserved = name in {"index.md", "log.md"}
    if data is None:
        if not reserved:
            yield "missing frontmatter"
        elif name == "log.md" and not re.search(r"(?m)^##\s+\d{4}-\d{2}-\d{2}(?:\s|$)", text):
            yield "log.md requires a dated ## YYYY-MM-DD heading"
        return
    if reserved:
        if path == "index.md":
            if set(data) - {"okf_version"} or str(data.get("okf_version", "0.2")) != "0.2":
                yield "root index frontmatter may contain only okf_version: 0.2"
        else:
            yield f"{name} must not have frontmatter"
        return
    if not nonempty(data.get("type")):
        yield "type must be a nonempty string"
    for key in STRING_FIELDS:
        if key in data and not nonempty(data[key]):
            yield f"{key} must be a nonempty string"
        if isinstance(data.get(key), str) and re.search(r"\{\{.*?\}\}", data[key]):
            if not (path == INSERTION_TEMPLATE and key == "title" and data[key] == "{{title}}"):
                yield f"{key} contains an unmaterialized template placeholder"
    if "maturity" in data and (not isinstance(data["maturity"], str) or data["maturity"] not in {f"L{i}" for i in range(6)}):
        yield "maturity must be L0, L1, L2, L3, L4, or L5"
    if isinstance(data.get("maturity"), str):
        for marker, line in body_maturity_markers(text):
            if marker != data["maturity"]:
                yield f"body maturity {marker} disagrees with frontmatter maturity {data['maturity']} (line {line})"
    if "status" in data and (not isinstance(data["status"], str) or data["status"] not in {"draft", "stable", "deprecated"}):
        yield "status must be draft, stable, or deprecated"
    for key in ("updated", "stale_after"):
        if key in data and not is_date(data[key]):
            # This exact, documented Obsidian insertion template is not a dated
            # knowledge article. Materialized copies must contain a real date.
            if not (path == INSERTION_TEMPLATE and key == "updated" and data[key] == "{{date:YYYY-MM-DD}}"):
                yield f"{key} must be a real ISO date (YYYY-MM-DD)"
    if "tags" in data and (not isinstance(data["tags"], list) or any(not nonempty(tag) for tag in data["tags"])):
        yield "tags must be a list of nonempty strings"
    if "sources" in data:
        sources = data["sources"]
        if not isinstance(sources, list):
            yield "sources must be a list of mappings"
        else:
            for i, source in enumerate(sources):
                if not isinstance(source, dict) or not nonempty(source.get("resource")):
                    yield f"sources[{i}] requires a nonempty string resource"
                if isinstance(source, dict):
                    for key in ("id", "title", "author"):
                        if key in source and not nonempty(source[key]):
                            yield f"sources[{i}].{key} must be a nonempty string"
    for field in ("verified", "generated"):
        if field not in data:
            continue
        value = data[field]
        if field == "verified" and isinstance(value, list):
            events = value
        elif isinstance(value, dict):
            events = [value]
        else:
            yield f"{field} must be {'a mapping or list of mappings' if field == 'verified' else 'a mapping'}"
            continue
        for i, event in enumerate(events):
            label = f"{field}[{i}]" if isinstance(value, list) else field
            if not isinstance(event, dict):
                yield f"{label} must be a mapping"
                continue
            if not valid_actor(event.get("by")):
                yield f"{label}.by must identify human:, process:, or tool/version"
            if (field == "verified" or "at" in event) and not is_datetime(event.get("at")):
                yield f"{label}.at must be an ISO datetime with timezone"


class AnchorHTML(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.anchors = set()
        self.links = []

    def handle_starttag(self, tag, attrs):
        for name, value in attrs:
            if value is not None and (name == "id" or (tag == "a" and name == "name")):
                self.anchors.add(value)
            if value is not None and name in {"href", "src"}:
                self.links.append(value)

    handle_startendtag = handle_starttag


def inline_text(children):
    result = []
    for token in children or []:
        if token.type in {"text", "code_inline"}:
            result.append(token.content)
        elif token.type in {"softbreak", "hardbreak"}:
            result.append(" ")
        elif token.type == "image":
            result.append(inline_text(token.children) if token.children else token.content)
    return "".join(result)


def heading_slug(value):
    # GitHub-style automatic heading IDs: lower case, punctuation/symbols
    # removed, spaces replaced by hyphens; Unicode letters/marks are retained.
    return "".join(c for c in value.lower() if c in "-_ " or unicodedata.category(c)[0] not in "PSC").replace(" ", "-")


def walk_tokens(tokens):
    for token in tokens:
        yield token
        if token.children:
            yield from walk_tokens(token.children)


@dataclass
class ParsedMarkdown:
    anchors: set[str]
    links: list[tuple[str, int | None]]


def parse_markdown(body, offset=0):
    tokens = MarkdownIt("commonmark", {"html": True}).parse(body)
    anchors = set()
    used_slugs = set()
    links = []
    html = AnchorHTML()
    for i, token in enumerate(tokens):
        if token.type == "heading_open" and i + 1 < len(tokens):
            base = heading_slug(inline_text(tokens[i + 1].children))
            slug, suffix = base, 0
            while slug in used_slugs:
                suffix += 1
                slug = f"{base}-{suffix}"
            used_slugs.add(slug)
            anchors.add(slug)
        line = token.map[0] + offset + 1 if token.map else None
        for child in walk_tokens([token]):
            if child.type == "link_open":
                links.append((child.attrGet("href") or "", line))
            elif child.type == "image":
                links.append((child.attrGet("src") or "", line))
            elif child.type in {"html_inline", "html_block"}:
                previous_links = len(html.links)
                html.feed(child.content)
                links.extend((destination, line) for destination in html.links[previous_links:])
    anchors.update(html.anchors)
    return ParsedMarkdown(anchors, links)


class Validator:
    def __init__(self, root, graph=".kb/knowledge-map.json", base_ref=None):
        self.root = Path(root).resolve()
        self.graph = graph
        self.base_ref = base_ref
        self.issues = []
        self.files = set()
        self.markdown = set()
        self.parsed = {}
        self.scanned = 0
        self.graph_counts = {}

    def add(self, code, path, message, line=None):
        self.issues.append(Issue(code, path, message, line))

    def document(self, path, *, validate_yaml=False):
        if path in self.parsed:
            return self.parsed[path]
        try:
            text = safe_path(self.root, path).read_text(encoding="utf-8-sig", errors="strict")
        except (Invalid, OSError, UnicodeError) as exc:
            self.add("read", path, str(exc))
            return None
        try:
            data, body, offset = frontmatter(text)
        except (Invalid, yaml.YAMLError, ValueError) as exc:
            # Do not turn malformed YAML into success; continue body-link checking
            # only after a recognizable closing delimiter, for useful diagnostics.
            if validate_yaml:
                self.add("yaml", path, str(exc))
            lines = text.splitlines(keepends=True)
            end = next((i for i, line in enumerate(lines[1:], 1) if line.strip() == "---"), None)
            offset = end + 1 if end is not None else 0
            body = "".join(lines[offset:])
        else:
            if validate_yaml:
                for error in metadata_errors(path, data, text):
                    self.add("metadata", path, error)
        parsed = parse_markdown(body, offset)
        self.parsed[path] = parsed
        return parsed

    def validate_graph(self):
        try:
            graph_file = safe_path(self.root, self.graph)
            data = strict_json(graph_file.read_text(encoding="utf-8-sig"))
        except (Invalid, OSError, UnicodeError, ValueError) as exc:
            self.add("graph", self.graph, str(exc))
            return
        if not isinstance(data, dict):
            self.add("graph", self.graph, "graph must be an object")
            return
        required = {"version", "domains", "documents", "concepts", "relations"}
        if set(data) - required - {"coverage_roots"} or required - set(data):
            self.add("graph", self.graph, "expected version, domains, documents, concepts, relations, and optional coverage_roots")
        if type(data.get("version")) is not int or data["version"] != 1:
            self.add("graph", self.graph, "version must be integer 1")
        sections = {}
        for key in ("domains", "documents", "concepts", "relations"):
            if not isinstance(data.get(key), list):
                self.add("graph", self.graph, f"{key} must be an array")
                sections[key] = []
            else:
                sections[key] = data[key]
        self.graph_counts = {key: len(value) for key, value in sections.items()}

        def row_check(row, fields, label, optional=()):
            if not isinstance(row, dict) or set(fields) - set(row) or set(row) - set(fields) - set(optional):
                suffix = f"; optional: {', '.join(optional)}" if optional else ""
                self.add("graph", self.graph, f"{label} requires only {', '.join(fields)}{suffix}")
                return False
            return True

        def unique(value, seen, label):
            if not nonempty(value) or CONTROL.search(value):
                self.add("graph", self.graph, f"{label} must be a nonempty string without control characters")
                return False
            key = folded(value)
            if key in seen:
                self.add("graph", self.graph, f"duplicate/case/Unicode-conflicting {label}: {value}")
                return False
            seen.add(key)
            return True

        domain_ids, domain_titles = set(), set()
        found_domains = set()
        domain_roots = {}
        for i, row in enumerate(sections["domains"]):
            label = f"domains[{i}]"
            if not row_check(row, ("id", "title", "entrypoint"), label):
                continue
            unique(row["id"], domain_ids, f"{label}.id")
            unique(row["title"], domain_titles, f"{label}.title")
            if isinstance(row["id"], str):
                found_domains.add(row["id"])
            try:
                if not isinstance(row["entrypoint"], str) or not re.fullmatch(r"知识/[^/]+/README\.md", row["entrypoint"]):
                    raise Invalid("domain entrypoint must be 知识/<domain-folder>/README.md")
                if isinstance(row["id"], str):
                    domain_roots[row["id"]] = str(PurePosixPath(row["entrypoint"]).parent)
                safe_path(self.root, row["entrypoint"])
                if row["entrypoint"] not in self.markdown:
                    raise Invalid("domain entrypoint must be tracked or nonignored Markdown")
            except (Invalid, OSError) as exc:
                self.add("graph", self.graph, f"{label}: {exc}")
        if found_domains != DOMAINS:
            self.add("graph", self.graph, f"domains must be exactly {', '.join(sorted(DOMAINS))}")

        ids, paths, docs = set(), set(), {}
        for i, row in enumerate(sections["documents"]):
            label = f"documents[{i}]"
            if not row_check(row, ("id", "path", "domain", "kind"), label, ("technologies", "legacy_paths")):
                continue
            if "technologies" in row:
                technologies = row["technologies"]
                if not isinstance(technologies, list) or not technologies:
                    self.add("graph", self.graph, f"{label}.technologies must be a nonempty array when present")
                else:
                    seen_technologies = set()
                    for technology in technologies:
                        if not isinstance(technology, str) or not re.fullmatch(r"[a-z][a-z0-9-]*", technology):
                            self.add("graph", self.graph, f"{label}.technologies has invalid slug {technology!r}")
                        elif technology in seen_technologies:
                            self.add("graph", self.graph, f"{label}.technologies has duplicate slug {technology!r}")
                        else:
                            seen_technologies.add(technology)
            valid_id = unique(row["id"], ids, f"{label}.id")
            unique(row["path"], paths, f"{label}.path")
            if not isinstance(row["domain"], str) or row["domain"] not in DOMAINS:
                self.add("graph", self.graph, f"{label}: invalid domain {row['domain']!r}")
            if isinstance(row["path"], str) and row["path"].startswith("知识/"):
                expected_root = domain_roots.get(row["domain"]) if isinstance(row["domain"], str) else None
                if expected_root is None or not row["path"].startswith(expected_root + "/"):
                    self.add("graph", self.graph, f"{label}: canonical path is outside its declared domain entrypoint directory")
                if folded(PurePosixPath(row["path"]).name) in {"readme.md", "index.md"}:
                    self.add("graph", self.graph, f"{label}: knowledge navigation README/index cannot be a canonical document")
            if not isinstance(row["kind"], str) or row["kind"] not in KINDS:
                self.add("graph", self.graph, f"{label}: invalid kind {row['kind']!r}")
            try:
                safe_path(self.root, row["path"])
                if row["path"] not in self.markdown:
                    raise Invalid("document must be tracked or nonignored Markdown")
            except (Invalid, OSError) as exc:
                self.add("graph", self.graph, f"{label}: {exc}")
            if valid_id:
                docs[row["id"]] = row

        alias_owners = {}
        for i, row in enumerate(sections["documents"]):
            if not isinstance(row, dict) or "legacy_paths" not in row:
                continue
            aliases = row["legacy_paths"]
            if not isinstance(aliases, list) or not aliases:
                self.add("graph", self.graph, f"documents[{i}].legacy_paths must be a nonempty array when present")
                continue
            for alias in aliases:
                try:
                    validate_legacy_path(self.root, alias)
                except (Invalid, OSError) as exc:
                    self.add("graph", self.graph, f"documents[{i}].legacy_paths: {exc}")
                    continue
                key = folded(alias)
                if key in paths:
                    self.add("graph", self.graph, f"legacy path conflicts with an active canonical path: {alias}")
                if key in alias_owners:
                    self.add("graph", self.graph, f"duplicate/case/Unicode-conflicting legacy path: {alias} (already in documents[{alias_owners[key]}])")
                else:
                    alias_owners[key] = i

        concept_ids, titles = set(), set()
        for i, row in enumerate(sections["concepts"]):
            label = f"concepts[{i}]"
            if not row_check(row, ("id", "title", "primary", "related"), label):
                continue
            unique(row["id"], concept_ids, f"{label}.id")
            unique(row["title"], titles, f"{label}.title")
            primary = row["primary"]
            if not isinstance(primary, str) or primary not in docs:
                self.add("graph", self.graph, f"{label}: unknown primary document {primary!r}")
            else:
                primary_kind = docs[primary].get("kind")
                if not isinstance(primary_kind, str) or primary_kind not in {"concept", "source-analysis", "case-study"}:
                    self.add("graph", self.graph, f"{label}: primary must be concept, source-analysis, or case-study; found {primary_kind!r}")
            related = row["related"]
            if not isinstance(related, list):
                self.add("graph", self.graph, f"{label}.related must be an array")
                continue
            related_seen = set()
            for value in related:
                unique(value, related_seen, f"{label}.related")
                if not isinstance(value, str) or value not in docs:
                    self.add("graph", self.graph, f"{label}: unknown related document {value!r}")
                elif value == primary:
                    self.add("graph", self.graph, f"{label}: primary cannot also be related")
        overlap = ids & concept_ids
        if overlap:
            self.add("graph", self.graph, "document and concept IDs must be globally distinct: " + ", ".join(sorted(overlap)))

        edges, degrees, seen_edges = defaultdict(set), {key: 0 for key in docs}, set()
        for i, row in enumerate(sections["relations"]):
            label = f"relations[{i}]"
            if not row_check(row, ("from", "to", "type"), label):
                continue
            source, target, kind = row["from"], row["to"], row["type"]
            if not all(isinstance(value, str) for value in (source, target, kind)):
                self.add("graph", self.graph, f"{label}: endpoints and type must be strings")
                continue
            if source not in docs or target not in docs:
                self.add("graph", self.graph, f"{label}: endpoints must reference document IDs")
                continue
            if kind not in RELATIONS:
                self.add("graph", self.graph, f"{label}: invalid relation type {kind!r}")
                continue
            edge = (source, target, kind)
            if edge in seen_edges:
                self.add("graph", self.graph, f"{label}: duplicate relation {edge!r}")
            seen_edges.add(edge)
            if kind == "prerequisite" and target not in edges[source]:
                edges[source].add(target)
                degrees[target] += 1
        ready = deque(key for key, degree in degrees.items() if degree == 0)
        count = 0
        while ready:
            key = ready.popleft()
            count += 1
            for target in edges[key]:
                degrees[target] -= 1
                if degrees[target] == 0:
                    ready.append(target)
        if count != len(degrees):
            self.add("graph", self.graph, "prerequisite cycle involving: " + ", ".join(sorted(key for key, value in degrees.items() if value)))

        if "coverage_roots" in data:
            roots = data["coverage_roots"]
            if not isinstance(roots, list):
                self.add("coverage", self.graph, "coverage_roots must be an array")
            else:
                root_seen, required_paths = set(), set()
                for value in roots:
                    unique(value, root_seen, "coverage root")
                    try:
                        safe_path(self.root, value, directory=True, allow_root=True)
                    except (Invalid, OSError) as exc:
                        self.add("coverage", self.graph, str(exc))
                        continue
                    required_paths.update(path for path in self.markdown if (value == "." or path.startswith(value + "/")) and folded(PurePosixPath(path).name) not in COVERAGE_RESERVED)
                mapped = Counter(row["path"] for row in docs.values() if isinstance(row.get("path"), str))
                for path in sorted(required_paths):
                    if mapped[path] != 1:
                        self.add("coverage", path, f"knowledge document must occur exactly once in graph (found {mapped[path]})")

    def validate_link(self, source, destination, line):
        try:
            parts = urlsplit(destination)
            if parts.scheme or parts.netloc:
                return  # External destinations are outside this offline checker.
            relative = unquote(parts.path, errors="strict")
            fragment = unquote(parts.fragment, errors="strict")
            if not relative:
                target = source
            else:
                if "\\" in relative or relative.startswith("/") or ":" in relative or CONTROL.search(relative):
                    raise Invalid("unsafe local link path")
                joined = PurePosixPath(source).parent.joinpath(relative)
                normalized = []
                for part in joined.parts:
                    if part == "..":
                        if not normalized:
                            raise Invalid("local link escapes repository")
                        normalized.pop()
                    elif part != ".":
                        normalized.append(part)
                target = "/".join(normalized)
                full = self.root / target
                # Directories are legitimate navigation links, but every ancestor
                # is still checked for a symlink, exact spelling, and existence.
                is_directory = full.is_dir()
                safe_path(self.root, target or ".", directory=is_directory, allow_root=is_directory)
                if not is_directory and target not in self.files:
                    raise Invalid("local link target must be tracked or nonignored; disk existence alone is insufficient")
            if fragment and PurePosixPath(target).suffix.casefold() == ".md":
                parsed = self.document(target)
                if parsed is not None and fragment not in parsed.anchors:
                    raise Invalid(f"missing Markdown anchor #{fragment} in {target}")
        except (Invalid, OSError, UnicodeError, ValueError) as exc:
            self.add("link", source, f"{destination!r}: {exc}", line)

    def run(self):
        try:
            self.files = inventory(self.root)
            self.markdown = {p for p in self.files if PurePosixPath(p).suffix.casefold() == ".md"}
            seen = {}
            conflicts = set()
            for path in sorted(self.markdown):
                # Directory components matter too: Foo/a.md and foo/b.md
                # collide on common case-insensitive filesystems.
                parts = PurePosixPath(path).parts
                for end in range(1, len(parts) + 1):
                    spelling = "/".join(parts[:end])
                    key = folded(spelling)
                    if key in seen and seen[key] != spelling:
                        pair = (seen[key], spelling)
                        if pair not in conflicts:
                            self.add("path", path, f"case/Unicode path conflict: {spelling} with {seen[key]}")
                            conflicts.add(pair)
                    else:
                        seen[key] = spelling
                try:
                    safe_path(self.root, path)
                except (Invalid, OSError) as exc:
                    self.add("path", path, str(exc))
            scope = changed_markdown(self.root, self.base_ref, self.markdown) if self.base_ref else self.markdown
        except (Invalid, OSError, UnicodeError) as exc:
            self.add("scope", str(self.root), str(exc))
            return self.report()
        self.validate_graph()
        scope = sorted(path for path in scope if not operational(path))
        self.scanned = len(scope)
        # Validate all source metadata before following links so cached targets
        # cannot inadvertently bypass later YAML validation of a source.
        for path in scope:
            self.document(path, validate_yaml=True)
        for path in scope:
            parsed = self.parsed.get(path)
            if parsed is not None:
                for destination, line in parsed.links:
                    self.validate_link(path, destination, line)
        return self.report()

    def report(self):
        return {
            "ok": not self.issues,
            "mode": "changed" if self.base_ref else "full",
            "base_ref": self.base_ref,
            "markdown_scanned": self.scanned,
            "markdown_inventory": len(self.markdown),
            "graph_scope": "full",
            "graph_counts": self.graph_counts,
            "error_count": len(self.issues),
            "errors": [asdict(issue) for issue in self.issues],
        }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--graph", default=".kb/knowledge-map.json", help="repository-relative graph path")
    parser.add_argument("--base-ref", help="check changed/new Markdown against this Git commit; graph and coverage remain full")
    parser.add_argument("--json-report", type=Path, help="write complete machine-readable diagnostics; no source files are rewritten")
    parser.add_argument("--max-errors", type=int, default=30, help="bound console diagnostics; JSON always contains every error")
    args = parser.parse_args(argv)
    if args.max_errors < 0:
        parser.error("--max-errors must be nonnegative")
    if args.json_report and args.json_report.resolve().is_relative_to(args.root.resolve()):
        parser.error("--json-report must be outside the repository (for example in the OS temporary directory)")
    report = Validator(args.root, args.graph, args.base_ref).run()
    for issue in report["errors"][:args.max_errors]:
        location = issue["path"] + (f":{issue['line']}" if issue["line"] else "")
        print(f"[{issue['code']}] {location}: {issue['message']}")
    if report["error_count"] > args.max_errors:
        print(f"... {report['error_count'] - args.max_errors} more errors (use --json-report for the complete list)")
    scope = f"changed Markdown against {args.base_ref}" if args.base_ref else "full Markdown"
    print(f"{'PASS' if report['ok'] else 'FAIL'}: {scope}; {report['markdown_scanned']}/{report['markdown_inventory']} Markdown files; full graph; {report['error_count']} errors")
    if args.base_ref:
        print("Changed-scope result does not certify unchanged Markdown/YAML/links.")
    if args.json_report:
        args.json_report.parent.mkdir(parents=True, exist_ok=True)
        args.json_report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
