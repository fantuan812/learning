---
name: knowledge-base-organizer
description: Organize, classify, deduplicate, refactor, audit, or restructure a Markdown or technical knowledge base. Use for Inbox processing, knowledge taxonomy work, document merging/splitting, MOC construction, OKF metadata migration, Obsidian-compatible navigation, AI conversation extraction, and knowledge-base maintenance.
---

> 知识成熟度：L2（工作流 Skill，随使用修订）。

# Knowledge Base Organizer

## Determine operation mode

Classify the task as:

- single_document
- inbox_cleanup
- incremental_update
- topic_cleanup
- audit
- full_rebuild

Prefer the smallest sufficient mode.

Never run full_rebuild for ordinary new documents.

---

# OKF compatibility

Read `.kb/okf-profile.yaml` and `references/OKF-兼容规范.md` before creating or
modifying Markdown. The repository profile migration is complete; use hybrid
staging only for future imported legacy content:

- add a non-empty OKF `type` to every new or modified non-reserved Markdown file;
- keep imported legacy documents readable and in place while their batch is
  reviewed, then bring the batch into the profile before closeout;
- preserve unknown frontmatter fields;
- keep `maturity` and `verified` independent;
- prefer standard relative Markdown links so Obsidian is a consumer, not a
  second source of truth;
- never overwrite `.obsidian` workspace state.

Run `scripts/check_okf.ps1 -Mode Changed` before the existing repository gate.
Use `Audit` to require a zero migration backlog and `Strict` as the final
profile-scope gate. This lightweight lint is not an official general OKF parser.

---

# Single Document

Use:

kb_analyzer

Then search existing related knowledge.

Decide:

- Extend
- Merge
- Create
- Archive

Ask kb_curator when semantic overlap exists.

---

# Inbox Cleanup

1. Inventory Inbox.
2. Spawn kb_analyzer workers for independent documents or batches.
3. Wait for analysis.
4. Cluster documents by topic.
5. Search existing canonical knowledge.
6. Ask kb_curator to review duplicates.
7. Ask kb_architect only for unresolved structural questions.
8. Produce migration plan.
9. Ask kb_auditor to review.
10. Execute approved changes serially.

---

# Topic Cleanup

1. Inspect the target topic.
2. Identify all related documents.
3. Analyze knowledge boundaries.
4. Detect duplicates and overlaps.
5. Propose canonical documents.
6. Propose Merge/Split/Rename.
7. Audit.
8. Execute.

---

# Full Rebuild

Full rebuild is expensive.

Use only when explicitly required or structurally necessary.

Workflow:

kb_scanner
    ↓

parallel kb_analyzer workers
    ↓

topic clustering
    ↓

kb_architect
    ↓

kb_curator
    ↓

migration plan
    ↓

kb_auditor
    ↓

single integrator execution
    ↓

kb_auditor final validation

Do not allow subagents to modify files during analysis. The kb_scanner,
kb_analyzer, kb_architect, kb_curator, and kb_auditor roles are read-only
analysis/review roles. Content writes are performed only by an explicitly
allowlisted content executor; shared MOCs, taxonomy, and plans are updated
serially by the single integrator. The verifier/auditor remains read-only.

---

# Parallelization

Good parallel tasks:

- scanning independent directories
- analyzing independent document batches
- duplicate candidate analysis
- topic coverage analysis
- relationship discovery

Bad parallel tasks:

- moving overlapping directories
- editing the same MOC
- merging the same documents
- changing taxonomy files
- renaming shared structures

Write-heavy work should normally be serial. Do not grant analysis roles
content-write or publication authority; publication follows the repository's
authoritative AGENTS rules.

---

# Required Planning

Before structural changes create or update:

.kb/plans/current.md

Include:

# Goal

# Scope

# Current State

# Findings

# Proposed Taxonomy Changes

# File Operations

# Merge Plan

# Split Plan

# Needs Review

# Risks

# Audit Result

# Execution Progress

# Final Validation

Keep it updated while executing.

---

# Migration Plan

Before executing produce:

| ID | Current | Target | Action | Confidence | Reason |
|---|---|---|---|---|---|

Allowed actions:

- Keep
- Move
- Rename
- Merge
- Split
- Archive
- Create
- Extend

Actions below 0.75 confidence go to review queue.

---

# Final Validation

After execution:

1. check moved files exist
2. check source duplicates
3. check links
4. check MOCs
5. check taxonomy
6. check aliases
7. check decision records
8. run kb_auditor

Only report completion after validation.
