---
type: Index
title: "Inbox —— 未处理材料"
description: "尚未完成语义分析、去重、归类和 OKF 元数据复核的材料入口。"
tags:
  - inbox
  - workflow
  - okf
status: stable
verified: []
maturity: L2
updated: 2026-08-20
---

# Inbox —— 未处理材料

新收集、尚未整理的材料入口：

- 剪藏、PDF、随手笔记；
- 原始 AI 对话（整理后原始记录移入 [Archive/AI-Conversations](../Archive/README.md)）；
- 待分类的临时文档。

处理流程（Incremental Mode）：

```text
Inbox
  ↓ 语义分析（kb_analyzer）
  ↓ 搜索已有 Canonical Knowledge
  ↓ 重复检查（kb_curator）
  ↓ Extend / Merge / Create
  ↓ 补 OKF type / sources / verified / maturity
  ↓ 更新 .kb/manifest.yaml、aliases.yaml、相关 MOC
  ↓ check_okf Changed + check_repo
```

不要因为新增几篇文档就重新设计整个 taxonomy。

新材料默认 `status: draft`、`verified: []`、`maturity: L0`；验证后向 `verified` 追加带 `by/at` 的事件，信任层级由 actor 推导，成熟度按证据独立提升。模板见 [OKF 知识条目](../references/templates/OKF-知识条目.md)。
