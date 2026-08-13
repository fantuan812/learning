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

# Core Architecture

The primary Codex thread acts as the Knowledge Base Orchestrator.

Delegate independent read-heavy analysis to specialized subagents.

Preferred agents:

- kb_scanner
- kb_analyzer
- kb_architect
- kb_curator
- kb_auditor

Parallelize:

- inventory
- semantic analysis
- duplicate discovery
- knowledge relationship discovery
- gap analysis

Do NOT parallelize destructive or conflicting file modifications.

All final writes, moves, renames and merges must be coordinated by the primary thread.

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

Subagents should generally operate read-only.

Only the primary orchestration thread should coordinate repository-wide structural writes.

Allowed final actions:

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

When a task is large:

Use subagents.

Give each subagent a bounded scope.

Wait for all relevant analysis agents before making structural decisions.

Ask subagents to return concise findings rather than raw exploration logs.

Keep the primary thread focused on:

- user intent
- decisions
- conflicts
- plans
- final execution
