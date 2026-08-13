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
  ↓ 更新 .kb/manifest.yaml、aliases.yaml、相关 MOC
```

不要因为新增几篇文档就重新设计整个 taxonomy。
