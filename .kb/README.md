---
type: Index
title: ".kb —— Knowledge Base Control Plane"
description: "知识库 taxonomy、manifest、OKF profile、决策、审计与执行计划入口。"
tags:
  - knowledge-base
  - control-plane
  - okf
status: stable
verified: []
maturity: L2
updated: 2026-10-04
---

# .kb —— Knowledge Base Control Plane

机器清单与维护契约，不是知识正文。八域分类、稳定身份与关系以 knowledge-map 为准，旧路径只用于历史定位、必要兼容和质量规则。

当前整体设计见 [知识库架构](../references/知识库架构.md)；顶层目录责任由 architecture.json 管理，主题归属与主责关系由 knowledge-map 管理；taxonomy 只提供现行权威与旧路径兼容规则的指针。

| 文件 | 用途 |
| --- | --- |
| [knowledge-map.json](knowledge-map.json) | 八域、稳定文档身份、唯一概念主责与关系 |
| [architecture.json](architecture.json) | 顶层责任层、入口与契约文件的机器清单 |
| [manifest.yaml](manifest.yaml) | 文档索引（不含正文） |
| [taxonomy.yaml](taxonomy.yaml) | 分类权威、当前导航与旧路径兼容规则的指针，不复制域清单 |
| [aliases.yaml](aliases.yaml) | 同义分类映射 |
| [okf-profile.yaml](okf-profile.yaml) | OKF v0.2 hybrid 兼容级别、字段词汇与验证命令 |
| [audit.md](audit.md) | 历史审计记录；本轮执行与验收见 current plan |
| [decisions.md](decisions.md) | 决策记录（KD-xxx） |
| [review-queue.md](review-queue.md) | 低置信度操作队列 |
| [plans/](plans/README.md) | 执行计划 |

架构验收：[check_architecture.ps1](../scripts/check_architecture.ps1)；负向回归：[test_architecture.ps1](../scripts/test_architecture.ps1)。manifest 重建后运行，不替代内容质量与 OKF 门禁。

统一扫描源：[get_kb_markdown.ps1](../scripts/get_kb_markdown.ps1)；范围回归：[test_kb_scope.ps1](../scripts/test_kb_scope.ps1)。Git 忽略的未跟踪缓存不属于知识清单，已跟踪正文仍受检。
