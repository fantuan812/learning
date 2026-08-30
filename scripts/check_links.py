#!/usr/bin/env python3
"""Cross-platform Markdown health check (check_repo.ps1-equivalent semantics).

Lint for WSL/Linux and CI where Windows PowerShell may not be available.
Checks:
  * BOM / UTF-8 encoding validity for every .md/.markdown file
  * unclosed code fences
  * empty files
  * files missing a top-level H1 heading
  * relative Markdown links pointing to non-existent files
    (code fences, indented code blocks and inline code are excluded;
     angle-bracket link targets like [x](<path with spaces>) are supported)

Usage:
  python3 scripts/check_links.py [ROOT]

Exit code is non-zero when any issue is found (intended for CI / pre-commit).
"""
import argparse
import json
import os
import re
import sys
import urllib.parse
from collections import defaultdict

SKIP_DIRS = {".git", ".github", "node_modules", ".obsidian", ".trash", "vendor"}
LINK_RE = re.compile(r"!?\[[^\]]*\]\((?:<([^>]+)>|([^)\s]+))")
FENCE_RE = re.compile(r"^\s*(```|~~~)")


def walk_md(root: str):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS and not d.startswith(".")]
        for name in filenames:
            if name.lower().endswith((".md", ".markdown")):
                yield os.path.join(dirpath, name)


def link_targets_per_line(lines):
    """Yield link targets per line, excluding fences / indented code / inline code."""
    in_fence = False
    fence_char = ""
    for line in lines:
        m = FENCE_RE.match(line)
        if m:
            char = m.group(1)[0]
            if not in_fence:
                in_fence, fence_char = True, char
            elif fence_char == char:
                in_fence, fence_char = False, ""
            continue
        if in_fence:
            continue
        if re.match(r"^\s{4,}", line):
            continue
        line = re.sub(r"`[^`]*`", "", line)
        for match in LINK_RE.finditer(line):
            target = match.group(1) if match.group(1) is not None else match.group(2)
            if target:
                yield target


def check_file(path: str, root: str, issues: dict):
    rel = os.path.relpath(path, root).replace(os.sep, "/")
    try:
        with open(path, "rb") as fh:
            raw = fh.read()
    except OSError as exc:
        issues["read_error"].append(f"{rel}: {exc}")
        return
    if raw[:3] == b"\xef\xbb\xbf":
        issues["bom"].append(rel)
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        issues["encoding"].append(rel)
        return
    if "\ufffd" in text:
        issues["replacement_char"].append(rel)
    if not text.strip():
        issues["empty"].append(rel)
        return
    if not re.search(r"^\s*#\s+\S", text, re.M):
        issues["no_h1"].append(rel)

    lines = text.splitlines()
    in_fence = False
    fence_char = ""
    fence_line = 0
    for num, line in enumerate(lines, 1):
        m = FENCE_RE.match(line)
        if m:
            char = m.group(1)[0]
            if not in_fence:
                in_fence, fence_char, fence_line = True, char, num
            elif fence_char == char:
                in_fence, fence_char = False, ""
    if in_fence:
        issues["unclosed_fence"].append(f"{rel} (line {fence_line})")

    base = os.path.dirname(path)
    for target in link_targets_per_line(lines):
        target = target.strip()
        if not target or re.match(r"^(?:[A-Za-z][A-Za-z0-9+.-]*:|//)", target) or target.startswith("#"):
            continue
        path_part = target.split("#", 1)[0].split("?", 1)[0]
        if not path_part:
            continue
        decoded = urllib.parse.unquote(path_part)
        full = os.path.normpath(os.path.join(base, decoded.replace("/", os.sep)))
        if not os.path.exists(full):
            issues["broken_link"].append(f"{rel} -> {target}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", default=".", help="repo root directory")
    parser.add_argument("--json", metavar="PATH", help="also write full report as JSON")
    args = parser.parse_args()

    issues = defaultdict(list)
    md_files = 0
    for path in walk_md(args.root):
        md_files += 1
        check_file(path, args.root, issues)

    report = {
        "root": os.path.abspath(args.root),
        "md_files": md_files,
        "issues": {k: sorted(v) for k, v in issues.items()},
        "issue_total": sum(len(v) for v in issues.values()),
    }
    text = json.dumps(report, ensure_ascii=False, indent=1)
    print(text)
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
    return 1 if report["issue_total"] else 0


if __name__ == "__main__":
    sys.exit(main())