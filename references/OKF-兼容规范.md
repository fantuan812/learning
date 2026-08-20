---
type: Reference
title: OKF 兼容规范
description: 将 GoogleCloudPlatform/knowledge-catalog 的 Open Knowledge Format v0.2 落地到本知识库。
tags: [okf, knowledge-management, interoperability]
status: stable
verified: []
maturity: L2
updated: 2026-08-20
sources:
  - id: okf-spec
    title: Open Knowledge Format Specification
    resource: https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md
  - id: okf-readme
    title: Open Knowledge Format README
    resource: https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/README.md
---

# OKF 兼容规范

## 1. 目标与边界

本仓库以 **hybrid（混合）迁移** 接入 OKF：保留已有目录、`index.md` 与 `log.md`，同时为条目增加 OKF 可识别的元数据和关系。2026-08-20 已完成当前 profile 范围迁移（390/390 受检文档、2 篇操作性排除）；未来导入 legacy 内容仍使用同一分批流程。迁移必须可逆、可审计；不以一次重命名破坏旧链接或 Git 历史。

## 2. 条目最小合同

OKF 强制字段只有 `type`；本库模板推荐同时提供 `title`、`description`、`tags`、`status`、`verified`、`maturity`、`updated`、`sources`。`verified` 是验证事件列表（或单一 mapping），每项至少 `{by, at}`；trust tier `unverified|machine-confirmed|human-reviewed` 由 actor 推导，与 `maturity` 正交。

正文优先使用标准 Markdown 相对链接（例如 `[概念](../concepts/概念.md)`）；需要稳定标识时，在 YAML 增加 `canonical`（本库唯一权威相对路径或显式 ID），不要把 Obsidian 专有语法作为唯一链接。

## 2.1 provenance、trust 与 lifecycle

`resource` 是条目的主要外部资源（适用时）；`sources` 非空时，每项至少提供 `resource`，可附 `id`、`title`、`author`。`generated` 仅在真实生成时填写，`by` 必填；可选 `at` 若存在必须是 ISO 8601 datetime。actor 使用 `human:`、`process:` 或 `producer/version`。`verified` 事件的 `by`/`at` 都必填，不得冒充人工复核。`status` 仅 `draft|stable|deprecated`（缺省 stable）；可选 `stale_after` 表达过期复核。上述字段按适用填写，本库只强制 `type`。

## 3. canonical、taxonomy、manifest

- `canonical`：本库定义的唯一权威路径/身份锚点；迁移或改名时保持不变，不强制为 URI。
- `taxonomy`：主题分类/标签体系；`tags` 可多值，分类调整不应改变 canonical。
- `manifest`：当前只做文件快照（path/kind/maturity/bytes/lines），不声称记录 canonical/type/status/updated。OKF 迁移状态由 `check_okf` Audit 报告；未来扩展 manifest 需另行决策。

三者关系是“身份—分类—清单”：canonical 由条目、aliases 与 decision 维护，taxonomy 解释分类，manifest 仅盘点文件快照；不得把 manifest 行号或目录路径当作 canonical。

## 4. 迁移与未来 legacy 导入流程（hybrid）

1. 盘点现有 Markdown，保留 `index.md`（导航）和 `log.md`（变更日志），建立路径到 canonical 的映射。
2. 为新建或修改条目补齐最小 YAML；未来导入的旧条目先分批审查再纳入 Strict，欠账由 `check_okf` Audit 报告。
3. 新旧链接均保留：改名时先加重定向条目或兼容链接，再移动文件；禁止孤立旧入口。
4. 重建 manifest 并运行 Changed + `check_repo`；canonical/sources 语义由人工 review。
5. 在 `log.md` 记录迁移批次、范围和校验结果；审核通过后追加 `{by: human:<id>, at: <ISO8601>}` 验证事件。

## 5. 质量门禁

质量门禁与当前实现一致：OKF 只要求 `type`；本库 Changed 对已记录字段检查 frontmatter、type、status、日期、`sources[].resource`、`generated.by/at` 与 `verified[].by/at`；`check_repo` 校验链接和编码。`check_okf` 是本库所用 YAML 子集的轻量 lint，不是通用 YAML parser；当前不自动检查在线可访问性或 canonical 唯一性。事实变化时清空或移除旧 verified 事件，复核后再追加新事件，不自动降低 maturity。

## 6. 版本与兼容

本规范针对 OKF v0.2；知识成熟度使用 L0-L5。`type` 采用本库 local vocabulary（如 Reference/BestPractice/Index/Concept），未知 type 仍合法。保留 `index.md` 与 `log.md`：index 不放 frontmatter，只有 bundle 根 index 可仅含 `okf_version`；log 按 `## YYYY-MM-DD` 分组。未知字段应保留；本库扩展字段登记在 profile。

> 知识成熟度：L2。本规范已通过静态校验并具备可执行验证，仍需随检查器演进更新。
