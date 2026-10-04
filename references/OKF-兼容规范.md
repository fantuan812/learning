---
type: Reference
title: OKF 兼容规范
description: 将 GoogleCloudPlatform/knowledge-catalog 的 Open Knowledge Format v0.2 落地到本知识库。
tags: [okf, knowledge-management, interoperability]
status: stable
verified: []
maturity: L2
updated: 2026-10-04
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

OKF 强制字段只有 `type`；本库模板推荐同时提供 `title`、`description`、`tags`、`status`、`verified`、`maturity`、`updated`、`sources`。`verified` 是验证事件列表（或单一 mapping），每项至少 `{by, at}`；trust tier `unverified|machine-confirmed|human-reviewed` 由 actor 推导，与 `maturity` 正交。`maturity` 标定全文主要承诺所获支持的证据深度，不能直接取最高子项；局部实验应单列对象、输入、环境、结果和未覆盖项，详见[写作规范](写作规范.md)。

正文优先使用标准 Markdown 相对链接；稳定身份在 `.kb/knowledge-map.json` 登记。可选 YAML `canonical` 保留兼容用途，填写时必须与图中的文档 ID 或当前路径一致，不成为第二份身份源；不要把 Obsidian 专有语法作为唯一链接。

## 2.1 provenance、trust 与 lifecycle

`resource` 是条目的主要外部资源（适用时）；`sources` 非空时，每项至少提供 `resource`，可附 `id`、`title`、`author`。`generated` 仅在真实生成时填写，`by` 必填；可选 `at` 若存在必须是 ISO 8601 datetime。actor 使用 `human:`、`process:` 或 `producer/version`。`verified` 事件的 `by`/`at` 都必填，不得冒充人工复核。`status` 仅 `draft|stable|deprecated`（缺省 stable）；可选 `stale_after` 表达过期复核。上述字段按适用填写，本库只强制 `type`。

## 3. 身份、分类与文件快照

- `knowledge-map`：唯一维护文档稳定 ID、当前路径、主域、内容类型、技术栈与概念关系。稳定 ID 不随改名变化；当前路径随迁移更新，旧路径保存在 `legacy_paths`。
- `canonical`：可选的兼容声明，须与知识图一致；已有字段不因整理被机械覆盖。路径形式的值不能被当作永不变化的 ID。
- `taxonomy`：现行权威与历史路径的兼容指针，不维护第二套域清单。`aliases` 只规范术语，不分配文档身份；`tags` 是可多值的描述属性。
- 正文 frontmatter：标题、来源、状态、成熟度与验证事件的事实来源；知识图不复制正文标题、maturity 或 verified。
- `manifest`：机械文件快照（path/kind/maturity/bytes/lines），其中 maturity 是从正文取得的派生值，不能独立编辑。OKF 迁移状态由 `check_okf` Audit 报告；未来扩展 manifest 需另行决策。

不得把 manifest 行号或目录路径当作文档稳定身份。各事实源的分工见[知识库架构](知识库架构.md)。

## 4. 迁移与未来 legacy 导入流程（hybrid）

1. 盘点现有 Markdown，保留 `index.md`（导航）和 `log.md`（变更日志），建立路径到 canonical 的映射。
2. 为新建或修改条目补齐最小 YAML；未来导入的旧条目先分批审查再纳入 Strict，欠账由 `check_okf` Audit 报告。
3. 迁移时保持稳定 ID，更新图中的当前路径、legacy_paths 与标准 Markdown 入链。只保留经审计确有必要的兼容入口；不能复制第二份正文，也不能孤立受保护资料的旧引用。
4. 重建 manifest 并运行 Changed + `check_repo`；canonical/sources 语义由独立内容审阅核对，审阅者可为获分配的 Agent 或人类；记录事件时仍按实际 actor，不能把权限授权或 CI 通过当作人类审核。
5. 在 `log.md` 记录迁移批次、范围和校验结果。只有实际发生、actor 和时间可核实的验证才追加事件；机器/Agent 审核采用真实工具或流程 actor，不能自动写为 `human:<id>`。没有可记录事件时保留 `verified: []`；人类审核也不能由 CI 通过或用户授权维护推定。

## 5. 质量门禁

质量门禁与当前实现一致：OKF 只要求 `type`；本库 Changed 对已记录字段检查 frontmatter、type、status、日期、`sources[].resource`、`generated.by/at` 与 `verified[].by/at`；`check_repo` 校验链接和编码。`check_okf` 是本库所用 YAML 子集的轻量 lint，不是通用 YAML parser；当前不自动检查在线可访问性或 canonical 唯一性。事实变化时清空或移除不再覆盖现行正文的旧 verified 事件，实际复核后才追加新事件，不自动升降 maturity。若内容审查发现整篇误用局部证据，应明确修订标定、理由和仍成立的范围；这与添加验证事件是两项独立操作。机械 lint 不能判定教学正确性、来源是否支持结论或实验是否真实。

## 6. 版本与兼容

本规范针对 OKF v0.2；知识成熟度使用 L0-L5。`type` 采用本库 local vocabulary（如 Reference/BestPractice/Index/Concept），未知 type 仍合法。保留 `index.md` 与 `log.md`：index 不放 frontmatter，只有 bundle 根 index 可仅含 `okf_version`；log 按 `## YYYY-MM-DD` 分组。未知字段应保留；本库扩展字段登记在 profile。

> 知识成熟度：L2。本规范已通过静态校验并具备可执行验证，仍需随检查器演进更新。
