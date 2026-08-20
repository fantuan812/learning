---
type: Policy
title: "Agent 协作与发布规则"
description: "知识库多 Agent 分工、共享文件串行写入、Git 发布门禁与 Obsidian 外部写者边界。"
tags:
  - agents
  - collaboration
  - publishing
  - obsidian
status: stable
verified: []
maturity: L2
updated: 2026-08-20
---

# Agent 协作与发布规则

> [AGENTS.md](../AGENTS.md) 是最高层规则；本页提供知识库维护时的可执行速查，不覆盖或缩减其中的约束。

## 角色边界

- 主线程负责范围、最终判断、共享控制面写入、Git index、提交与推送。
- 子 Agent 默认只读，适合并行盘点目录、链接、来源、重复主题和风险；结果必须给出可复核路径或命令。
- `.kb/*`、全局 MOC、manifest、taxonomy 和同一篇正文由主线程串行修改，避免并发覆盖。
- 结构动作遵循 Inspect → Analyze → Plan → Review → Execute → Audit；不得从“看起来应归类”直接跳到搬文件。

## 文件操作门槛

| 操作 | 最低要求 |
| --- | --- |
| CreateLink / CreateMOC | 目标存在、职责不重复、所有相对链接可解析 |
| UpdateMetadata | 不改变正文语义，不自动提升 `maturity` 或生成 `verified` 事件 |
| Move / Rename | 明确旧→新映射、全部入链、README/MOC/manifest 更新与回滚清单 |
| Split / Merge | 用户批准；逐段或逐字完整性证据；旧入口保留兼容导航 |
| Delete | 只有用户明确要求才允许 |

置信度低于 0.75 的结构动作写入 [.kb/review-queue.md](../.kb/review-queue.md)，不直接执行。

## 发布门禁

1. 用户明确授权 commit；push 可以在同一请求中一并授权。
2. 发布者只用路径 allowlist 暂存，禁止 `git add .` 或 `git add -A`。
3. 暂存后冻结任务写入和 Git index；独立只读审核必须核对 HEAD、分支、allowlist、cached diff、路径/blob manifest 与未授权交集。
4. 审核 PASS 后，发布者在 commit 前复核 HEAD 和暂存指纹。
5. commit 后复核父提交、路径集合、mode/blob 与整体 diff 指纹；不一致就停止，禁止自动 amend/reset/recommit。
6. push 成功后 fetch 远端，确认本地与 `origin/<branch>` SHA 相同且 ahead/behind 为 `0/0`。
7. push 失败即停止；不得自行 pull、rebase、reset 或 force-push 恢复。

## Obsidian 外部写者

- 仓库根是 vault；Markdown、YAML Properties 和标准相对链接是共享事实源。
- Obsidian 可能在打开文件时规范化 `.base` YAML，或持续更新 `.obsidian/workspace*.json`。发布门禁中必须区分工作区变化与已冻结的 Git index。
- `.obsidian` 的设备布局、个人偏好和插件状态不得自动接管；共享范围按 [Obsidian 协作指南](Obsidian协作指南.md) 单独批准。
- 批量重命名、Git 同步与 Obsidian Sync 不得同时写同一文件。

## 每批交付证据

- `scripts/check_okf.ps1 -Mode Changed` 或 `-Mode Strict`；
- `scripts/check_repo.ps1`；
- 本地链接、UTF-8/BOM、代码围栏与 `git diff --check`；
- manifest 路径、bytes、lines、maturity 与磁盘一致；
- 清楚区分“已修改”“已提交”“已推送”和“仅计划”。

> 知识成熟度：L2（规则已用于 OKF 全库迁移发布；后续发布仍须逐次重跑门禁）。
