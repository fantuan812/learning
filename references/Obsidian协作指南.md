---
type: BestPractice
title: Obsidian 协作指南
description: 在本知识库中安全使用 Obsidian、Bases、Templates 与 Git/Sync 的操作边界。
tags: [obsidian, collaboration, bases, templates]
status: stable
verified: []
maturity: L2
updated: 2026-08-20
sources:
  - id: obsidian-bases
    title: Bases syntax
    resource: https://obsidian.md/help/bases/syntax
  - id: obsidian-templates
    title: Templates
    resource: https://obsidian.md/help/plugins/templates
  - id: obsidian-storage
    title: Data storage
    resource: https://obsidian.md/help/data-storage
  - id: obsidian-sync-settings
    title: Sync settings
    resource: https://obsidian.md/help/sync/settings
  - id: obsidian-sync-notes
    title: Sync notes
    resource: https://obsidian.md/help/sync-notes
---

# Obsidian 协作指南

## 首次打开

在 Obsidian 中选择“Open folder as vault”，打开仓库根目录（包含 `00_Index`、`references` 等目录），不要选择其子目录。已有 `.obsidian` 属于工作区配置：协作时不得覆盖、删除或格式化它；如需个人设置，使用 Obsidian 的 workspace/个人配置并避免提交。

## Templates 与 Bases

在 Settings → Core plugins → Templates 启用 Templates，并将 **Templates folder location** 设置为 `references/templates`。插入 `OKF-知识条目` 模板后填写 YAML；模板使用官方变量 `{{title}}` 与 `{{date:YYYY-MM-DD}}`，插入后应检查标题和日期是否正确。

打开 `00_Index/Knowledge.base` 查看 Bases 数据库；Bases 读取 Markdown 属性，不是新的事实来源。Obsidian 数据以 vault 内 Markdown/附件和配置文件存储。修改条目属性后刷新 Base，发现字段不一致以条目 YAML 为准，并在 `log.md` 记录修复。

## 单写者与同步边界

同一时段只能有一个写者执行批量重命名、模板升级或 manifest 生成。Git 与 Obsidian Sync 不要同时对同一文件进行写入：团队约定一次只启用一种同步路径，提交/拉取前关闭另一方的自动写入。冲突必须人工合并 YAML 与正文，再运行 OKF 质量门禁；不要用“接受全部”覆盖他人修改。

## 日常协作

先拉取/同步，再编辑；提交前确认 `index.md`、`log.md` 和 manifest 的变更可解释。使用标准 Markdown 相对链接，避免仅依赖 `[[wikilink]]`。workspace 布局、插件缓存和个人视图不作为知识内容；不要把 workspace 状态写入条目。

## 恢复与安全

误操作时优先用 Git 历史或 Obsidian Sync 的版本恢复，保留恢复记录。任何自动化脚本必须限定在 vault 根目录，先 dry-run 再写入。

> 知识成熟度：L2。本指南已通过静态校验并具备可执行验证，具体插件行为应随官方文档更新复查。
