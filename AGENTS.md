---
type: Policy
title: "Knowledge Base Agent Instructions"
description: "Repository-wide governance for knowledge organization, OKF interoperability, agent collaboration, and publication."
tags:
  - knowledge-base
  - governance
  - okf
status: stable
verified: []
maturity: L2
updated: 2026-08-20
sources:
  - id: google-okf-v0.2
    title: "Open Knowledge Format specification"
    author: "Google Cloud"
    resource: "https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md"
---

# Knowledge Base Agent Instructions

> 知识成熟度：L2（控制面行为规则，随维护修订）。

## Mission

This repository is a long-term technical knowledge base.

The goal is not to make files look tidy.

The goal is to maintain a knowledge system that is:

- stable
- searchable
- low-duplication
- easy to extend
- suitable for humans
- suitable for AI retrieval
- suitable for RAG and agent workflows

Always optimize for long-term knowledge structure rather than short-term folder neatness.

---

# OKF Interoperability and Obsidian

This repository uses Open Knowledge Format (OKF) v0.2 from the
GoogleCloudPlatform/knowledge-catalog project through the repository profile in
`.kb/okf-profile.yaml`. The profile-scope migration completed on 2026-08-20:
390/390 scanned documents conform, with 2 documented operational exclusions.
Every newly created or modified non-reserved Markdown document must start with
YAML frontmatter containing a non-empty `type`. Future imported legacy content
uses the hybrid batch-review workflow. `index.md` and `log.md` are OKF reserved
names and may omit concept metadata.

Use `references/OKF-兼容规范.md` as the repository mapping. The existing control
plane remains authoritative:

- taxonomy classifies Domain → Subdomain → Topic;
- canonical rules choose one authoritative location;
- `.kb/manifest.yaml` inventories the current files;
- OKF frontmatter makes each profile-scoped document portable to other consumers.

`maturity` (L0–L5 evidence depth) and `verified` events are independent.
`verified` is an optional `{ by, at }` mapping or list; consumers derive the
unverified / machine-confirmed / human-reviewed trust tier from its actors.
Never store that derived tier as the `verified` value, and never infer maturity
from it. Preserve unknown frontmatter fields. Prefer standard Markdown links;
Obsidian wikilinks may be an additional convenience but never the only portable
relationship.

Treat the repository root as the Obsidian vault. Do not overwrite a user's
existing `.obsidian` state. `workspace*.json` is per-device UI state, not shared
knowledge. Git remains the source of truth for Markdown, MOCs, templates, and
the OKF profile; do not let Git and Obsidian Sync write the same files at the
same time.

Before final validation run both:

```powershell
& (Join-Path $RepoRoot 'scripts/check_okf.ps1') -Root $RepoRoot -Mode Changed
& (Join-Path $RepoRoot 'scripts/check_repo.ps1') -Root $RepoRoot
```

`Audit` mode must normally report a zero legacy backlog. `Strict` is the
repository profile's final gate for non-excluded documents; because this
checker lints a documented YAML subset and retains operational exclusions, it
is not by itself a claim of complete official OKF parser conformance.

---

# Core Architecture

The primary Codex thread is the coordinator. It owns user intent, the clean/dirty
baseline, task decomposition, conflict resolution, and final review. It may remain
read-only when the user requests an audit or plan.

| Role | Default authority |
| --- | --- |
| `kb_scanner`, `kb_analyzer`, `kb_architect`, `kb_curator`, `kb_auditor` | Read-only analysis; return evidence and recommendations. |
| Content executor | Write only an explicitly assigned allowlist; never commit or push. |
| Single integrator | Serially edits shared `README`, MOC, roadmap, manifest, or decision records; no parallel writers for those files. |
| Verifier | Read-only validation; reports failures and never repairs them. |
| Single publisher | After explicit commit authorization, precisely stages only the approved allowlist, then pauses for an independent verifier's cached review; commits only after that review passes. Push is an independent gate: the user may explicitly authorize both commit and push in one request, but execution remains stage → review → commit → push; does not edit content. |

Parallelize independent inspection and analysis, but serialize conflicting edits.
Every agent must state its assigned scope and authority before acting.

Permissions are separate: `review` (read/assess), `edit` (allowlisted files),
`commit`, and `push`. All agents except the single publisher are forbidden from
`commit` and `push`.
Except for the publisher's explicitly authorized phase, all agents are forbidden
from mutating the Git index, history, or remote, including `add`, `commit`, and
`push`.

Model, reasoning effort, and concurrency are runtime capabilities. Honor the
user's explicit selection when available, subject to tool and platform limits;
do not hard-code a model or assume an unavailable setting.

Missing, timed-out, or failed agent results are not approval. Wait, reassign, or
report the failure; never infer that an unreturned check passed.

Before any write, save `status`, `HEAD`, and each dirty path's diff or blob/hash
outside the repository. Final `status` alone cannot prove that existing changes
were not overwritten.

Before publishing, inspect the Git index baseline. If staged content already
exists outside the explicitly approved allowlist, stop and report it; never
unstage, clear, or mix it into the task.

---

# Mandatory Workflow

For non-trivial knowledge-base organization tasks, follow:

1. Inspect
2. Analyze
3. Plan
4. Review
5. Execute
6. Audit

Never begin by moving files.

---

# Phase 1 — Inspect

Use kb_scanner when repository-wide inspection is needed.

Determine:

- current directory structure
- document count
- file formats
- obvious temporary files
- duplicate filenames
- unusually large files
- unclassified documents

Update or propose updates to:

    .kb/manifest.yaml

Do not modify knowledge files during this phase.

---

# Phase 2 — Semantic Analysis

Use kb_analyzer.

Determine for each relevant document:

- domain
- subdomain
- topic
- knowledge type
- summary
- key concepts
- project specificity
- related concepts

Knowledge types include:

- Concept
- Principle
- Architecture
- Mechanism
- Implementation
- Tutorial
- Troubleshooting
- BestPractice
- Comparison
- CaseStudy
- Reference
- Research
- Project
- Decision
- Experience
- Interview

Separate:

    Observed Fact
    Inference
    Recommendation

Never present an inference as a fact.

---

# Phase 3 — Knowledge vs Project Boundary

Every document must be classified as one of:

    universal
    project
    mixed
    temporary

Universal knowledge belongs under:

    Knowledge/

Project-specific knowledge belongs under:

    Projects/

Mixed documents should usually be considered for Split.

Example:

    Generic Buff conflict rules
        → Knowledge/GameDevelopment/...

    Current MMO project's Buff implementation
        → Projects/<project>/...

Use links instead of duplicate copies.

---

# Phase 4 — Taxonomy

Before creating a new folder or topic, read:

    .kb/taxonomy.yaml
    .kb/aliases.yaml
    .kb/decisions.md

Use kb_architect for significant taxonomy decisions.

Default taxonomy model:

    Domain
      → Subdomain
        → Topic

Prefer 2–4 levels.

Do not create a directory merely because one document exists.

Create a directory when at least one condition holds:

1. There are at least 3 related knowledge units.
2. The topic is expected to grow.
3. The topic represents a clearly independent domain.

Otherwise prefer a Markdown document.

---

# Taxonomy Stability Rule

Do not redesign the taxonomy for small incremental updates.

A taxonomy redesign is justified only when:

- a domain has grown substantially
- many documents cannot be classified cleanly
- duplicated categories have appeared
- the current hierarchy consistently creates ambiguity

Prefer incremental evolution.

---

# Canonical Knowledge Rule

Each knowledge concept has exactly one Canonical Location.

Other locations may contain:

- links
- aliases
- MOC references

Never maintain two authoritative copies of the same knowledge.

---

# Source Is Not Taxonomy

Never use information source as the primary knowledge taxonomy.

Avoid structures such as:

    ChatGPT/
    Claude/
    PDFs/
    Websites/
    2026/
    LearningNotes/

Instead classify by knowledge domain.

Source information belongs in metadata.

---

# Duplicate Handling

Before creating new knowledge:

1. Search existing topics.
2. Search aliases.
3. Search semantically related documents.
4. Decide whether the new information should:

    Extend
    Merge
    Create
    Archive

Duplicate levels:

    exact
    semantic
    overlapping

Exact duplicates:

    DeleteCandidate

Semantic duplicates:

    Merge

Overlapping documents:

    Keep + Refactor
or
    Split + Merge

Never permanently delete automatically.

Move deletion candidates to:

    Archive/DeleteCandidates/

---

# Naming

Prefer concise conceptual names:

    AOI.md
    ECS.md
    Skill-System.md
    Buff-Conflict-Resolution.md
    A-Star.md

Avoid:

    notes1.md
    new-document.md
    today-learning.md
    chatgpt-summary.md

---

# Knowledge Unit Granularity

Avoid giant documents covering unrelated topics.

Also avoid extreme atomic-note fragmentation.

Bad:

    Buff-ID.md
    Buff-Time.md
    Buff-Start.md
    Buff-End.md

Better:

    Buff-System.md
    Buff-Lifecycle.md
    Buff-Stacking-and-Conflict.md

A knowledge unit should be independently understandable and semantically complete.

---

# AI Conversation Processing

Raw AI conversations are not canonical knowledge.

Process:

    Raw Conversation
        ↓
    extract problems
        ↓
    extract conclusions
        ↓
    extract principles
        ↓
    extract trade-offs
        ↓
    extract implementation knowledge
        ↓
    merge into existing knowledge

Archive raw conversations under:

    Archive/AI-Conversations/

Do not build taxonomy around AI provider names.

---

# Troubleshooting Knowledge

Convert debugging history into reusable engineering knowledge.

Preferred structure:

    # Problem

    ## Symptoms

    ## Environment

    ## Root Cause

    ## Diagnosis

    ## Solution

    ## Why It Works

    ## Prevention

    ## Related Topics

---

# Research Knowledge

Do not organize research only by PDF files.

Prefer:

    Research/
      Topic/
        Overview.md
        Methods/
        Papers/
        Experiments/
        Ideas/

Important methods from papers should be converted into reusable Method Knowledge where appropriate.

---

# Architecture Decisions

Important engineering decisions should use ADR-style documents:

    # ADR: Decision

    ## Context
    ## Problem
    ## Options
    ## Decision
    ## Reasons
    ## Trade-offs
    ## Consequences
    ## Related Systems

---

# Knowledge Relationships

Use relationships such as:

    is_a
    part_of
    depends_on
    uses
    implemented_by
    related_to
    alternative_to
    contrasts_with
    extends
    example_of
    applied_in

Use links instead of content duplication.

---

# MOC

MOC means Map of Content.

Use MOCs for navigation, not large bodies of knowledge.

Possible hierarchy:

    Global MOC
    Domain MOC
    Topic MOC

Do not create an MOC for every document.

---

# Confidence

Use confidence for uncertain classification:

    >= 0.90 strong
    0.75–0.89 likely
    0.50–0.74 needs_review
    < 0.50 uncertain

Any structural action with confidence below 0.75 must enter:

    .kb/review-queue.md

Do not automatically perform it.

---

# Decision Records

Significant structural decisions must be appended to:

    .kb/decisions.md

Each decision should contain:

    Decision ID
    Subject
    Options
    Decision
    Reason
    Confidence
    Affected files

Before revisiting an existing structural decision, check this file.

Do not repeatedly redesign previously settled taxonomy without new evidence.

---

# Write Safety

Edits require an explicit allowlist and an assigned content executor or the
single integrator. Repository-wide structural writes are coordinated by the
primary thread; shared navigation files are edited serially by the integrator.
Verification remains read-only.

Allowed edit actions:

    Keep
    Move
    Rename
    Merge
    Split
    Archive
    CreateMOC
    CreateLink
    UpdateMetadata

Permanent deletion is forbidden unless explicitly requested by the user.

No agent other than the single publisher may mutate the Git index, history, or
remote. Publishing has two gates: after explicit commit authorization, the
publisher precisely stages the allowlist and stops for an independent verifier's
cached review; only a passing review permits commit. Push requires separate
explicit authorization: the user may authorize commit and push in the same
request, but the gates still run in order: stage, review, commit, push. Use
path-specific `add -- <allowlist>` only; never use `add -A` or `add .`. Once the
publisher begins staging, task writes and the Git index are frozen; only the
publisher may touch the index, and it must not change after staging.

The independent verifier must confirm unchanged `HEAD` and branch; cached
name-status exactly equals the approved allowlist; no unauthorized intersection
with task-start dirty user paths; cached check, stat, and content; and a recorded
staged-diff hash plus an immutable staged path/blob manifest (or equivalent
content hash) for every approved path. Before commit, the publisher rechecks
`HEAD` and the staged hash; any mismatch stops the gate. Immediately after
commit, verify the commit parent is the reviewed `HEAD` and its path/blob/tree
manifest exactly matches the reviewed staged evidence. Any mismatch stops and
forbids push; do not amend, reset, or recommit automatically. The push gate opens
only after this post-commit integrity check passes. A push failure stops the
publishing phase; do not pull, rebase, reset, or force-push to recover.

---

# Incremental Mode

For newly added documents:

    Inbox
      ↓
    semantic analysis
      ↓
    search existing knowledge
      ↓
    duplicate check
      ↓
    Extend / Merge / Create
      ↓
    add relationships
      ↓
    update MOC if necessary

Do not perform full taxonomy redesign.

---

# Full Rebuild Mode

Use only when explicitly requested or when the repository is clearly structurally inconsistent.

Workflow:

    Inventory
        ↓
    Semantic Analysis
        ↓
    Domain Analysis
        ↓
    Taxonomy Proposal
        ↓
    Duplicate Analysis
        ↓
    Migration Plan
        ↓
    Audit
        ↓
    Execution
        ↓
    Final Audit

---

# Important

When a task is large, use bounded-scope subagents and wait for all relevant
results before making structural decisions. Ask for concise findings with
evidence, status, and unresolved risks. Missing or failed results must be
reported or retried, not treated as success.

Keep the primary thread focused on:

- user intent
- decisions
- conflicts
- plans
- final review and authorization gates

The primary thread may choose a read-only outcome. Do not imply that all writes
belong to the main thread or that all subagents are read-only: the role matrix
above is authoritative. Permanent deletion still requires explicit user request.
