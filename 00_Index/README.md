---
type: Index
title: "00_Index —— 全局索引与 MOC"
description: "全局 Map of Content 与 Obsidian Bases 导航入口。"
tags:
  - index
  - moc
  - obsidian
status: stable
verified: []
maturity: L2
updated: 2026-08-20
---

# 00_Index —— 全局索引与 MOC

本目录存放导航型索引，不存放知识正文：

总体职责与维护入口见 [知识库架构](../references/知识库架构.md)。主题导航以 Global MOC 为主，目录 README 维护本地文件清单；同一层不再复制完整正文。

| 文件 | 用途 |
| --- | --- |
| [MOC.md](MOC.md) | 全局 Map of Content：指向各领域入口 |
| [domains/](domains/README.md) | 六个领域 MOC：学习路径、领域边界和物理 README 入口 |
| [axes/](axes/README.md) | 跨域主题、Primary Canonical 与知识生命周期路由 |
| [Knowledge.base](Knowledge.base) | Obsidian Bases：All / Review / Evidence / Project 四个属性视图 |

规则：MOC 只做导航；Base 只投影 Markdown Properties；领域 MOC 集中放在 `00_Index/domains/`，跨域视图放在 `00_Index/axes/`，不为单篇文档单独建 MOC。正文和目录 README 仍是 Canonical，不在索引中复制。OKF bundle 根入口为 [../index.md](../index.md)。
