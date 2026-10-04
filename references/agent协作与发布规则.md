---
type: Policy
title: "Agent 协作与发布规则"
description: "知识库 Agent 角色、任务合同、并发边界、验收与发布的详细规范。"
status: stable
verified: []
maturity: L2
updated: 2026-10-04
---

# Agent 协作与发布规则

> 知识成熟度：L2。本页是角色与流程的详细事实源，受 [AGENTS.md](../AGENTS.md) 核心契约约束。
> [知识组织规则](知识组织规则.md) 管理内容边界；[配置说明](../.codex/README.md) 管理宿主配置。旧计划与历史 lessons 不覆盖当前规则。

## 角色与权限

| 角色 | 职责 | 写入权限 |
| --- | --- | --- |
| 主线程协调者 | 用户意图、dirty 基线、分解、分配、冲突、最终验收 | 只在明确兼任 executor 或 integrator 时写入其 allowlist |
| kb_scanner | 目录、文件、元数据和异常盘点 | 只读 |
| kb_analyzer | 语义、知识类型、复用边界与关系分析 | 只读 |
| kb_architect | 依据知识图提出分类与结构方案，查阅既有决策 | 只读 |
| kb_curator | 重复、合并拆分、知识单元边界建议 | 只读 |
| kb_auditor | 独立审核语义、结构、计划合理性与信息保存 | 只读 |
| kb_executor | 按批准的任务范围修改独立正文或指定文件 | 精确 allowlist，不接管共享文件 |
| kb_integrator | 维护知识图、共享导航、taxonomy 兼容指针、aliases、计划、决策、manifest | 单一指定写者，精确 allowlist，串行 |
| kb_verifier | 运行检查、比较基线、复核产物及发布证据 | 只读，不修复失败 |
| 单一发布者（流程角色） | 用户授权后暂存、提交与推送 | 仅发布阶段修改 Git index/history/remote，不编辑正文 |

分析/执行/验证应分离；执行者不得自称独立审查者。小修改可由主线程兼任执行者并运行检查，不要求为每个阶段强制创建 Agent。
publisher 不注册为自动启动角色。除明确授权发布阶段的单一发布者，任何角色都不得 add、commit、push 或以其他命令修改 Git index/history/remote。
模型、reasoning effort 和并发受当前宿主能力及用户当次选择约束，不在仓库硬编码模型。配置默认与自然语言规则不等于运行时强制隔离。

## 任务合同与交接

分派前明确 scope、authority、精确文件 allowlist、已知 dirty 路径、依赖、目标、验收命令和共享写者。
只读任务的 changes 应为空；执行者必须在写入前说明分配范围及权限。
结构任务的 allowlist 和旧→新操作映射写入 [.kb/plans/current.md](../.kb/plans/current.md)；小编辑可在任务消息记录，不强制全库计划。
用户已授权的范围直接执行，内部 plan/review 不新增用户确认；越界则先由协调者调整范围，不能自行扩大写集。

每个角色返回：

- scope：实际检查或修改范围；
- status：PASS、PASS_WITH_WARNINGS、FAIL 或 BLOCKED；
- evidence：可复核路径、命令与结果，观察/推断分开；
- changes：修改路径及操作；只读写为空；
- risks：既有失败、未验证条件、冲突或后续依赖。

失败、超时或缺失结果必须重试、重分配或报告，不能推断通过。交接列出下一写者需要的证据，避免重复扫描全库。

## 执行与并发

结构任务遵循 Inspect → Analyze → Plan → Review → Execute → Audit，不从搬文件开始。
独立目录扫描、文档分析、正文编辑可并行，但执行 allowlist 必须不相交。
共享知识图、README、MOC、taxonomy 兼容指针、aliases、manifest、决策和同一正文只由指定整合者串行写。
写入前由协调者在仓库外保存 status、HEAD、staged 基线和每个 dirty 文件的 diff/blob/hash；写者确认基线已存在。
若用户外部编辑进入分配范围，停下该路径并报告冲突，继续无冲突工作；不覆盖、不自动回退。

| 操作 | 最低证据 |
| --- | --- |
| CreateLink / CreateMOC | 目标存在、入口职责不重复、相对链接可解析 |
| UpdateMetadata | 正文语义不变，不自动提高 maturity 或产生 verified 事件 |
| Move / Rename | 旧→新映射、全部入链、导航与 manifest 更新、回滚映射 |
| Split / Merge | 在用户已授权范围内；逐段或逐字信息保存证据；旧入口保留兼容导航 |
| Archive | 明确来源、目标与保留信息，属于结构 allowlist |
| 永久删除 | 用户明确要求 |

置信度低于 0.75 的结构操作进入 [.kb/review-queue.md](../.kb/review-queue.md)，不自动执行。

## 验收

验证者检查实际变更、计划范围、dirty 保存证据、链接与导航、Canonical 和信息完整性。
运行根 AGENTS 的最低检查；架构改动补充架构门禁，Agent 配置补 TOML/角色契约检查，Skill 改动运行其 validator。
manifest 在内容及导航完成后由整合者机械重建，核对路径、bytes、lines、maturity。
发现缺陷交回原写者修复；验证者不自行修改文件。
报告区分本次引入、既有失败、仅计划与未运行项目，不为全绿削弱门禁或虚构成熟度。

## 发布门禁

1. 用户明确授权 commit；push 可在同一请求一并授权，但仍是独立执行门禁。
2. 发布前读取 index 基线；若存在 allowlist 外 staged 内容，停止并报告，不能 unstage、清空或混入。
3. 发布者精确暂存批准 allowlist，禁止 git add . / git add -A。可用路径参数或 pathspec 文件，确保编码与范围一致。
4. 暂存开始即冻结任务写入；完成暂存后冻结 index，仅发布者可接触且不得继续改变它。
5. 独立验证者核对 HEAD、分支不变，cached name-status 精确等于 allowlist，无未授权 dirty 交集，执行 cached check/stat/内容审核；记录 staged-diff hash 与每路径 mode/blob manifest。
6. 只有独立审核 PASS 才可 commit；发布者 commit 前再次核对 HEAD 与暂存指纹，任何变化停止。
7. commit 后立即验证 parent 是审核 HEAD，提交的路径/mode/blob/tree 与审核证据一致。异常停止，禁止自动 amend/reset/recommit，不能 push。
8. push 仅在已获用户授权且提交完整性通过后执行；失败停止，不自行 pull/rebase/reset/force-push。
9. 成功后 fetch 并确认本地与 origin/<branch> SHA 相同、ahead/behind 为 0/0；未验证远端时如实说明。

## Obsidian 外部写者

仓库根是 vault；Markdown、YAML Properties 与标准链接是共享事实源。
Obsidian 可能规范化 .base 或更新 .obsidian/workspace*.json；必须区分工作区变化与冻结的 index。
不接管设备布局、个人偏好及插件状态，共享范围遵循 [Obsidian 协作指南](Obsidian协作指南.md)。
批量重命名、Git 同步与 Obsidian Sync 不得同时写同一文件。
