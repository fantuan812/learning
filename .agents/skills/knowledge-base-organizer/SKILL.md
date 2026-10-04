---
name: knowledge-base-organizer
description: Maintain this knowledge base through scoped edits, topic organization, raw-material processing, metadata maintenance, or architecture audits and refactoring.
---

# Knowledge Base Organizer

> 知识成熟度：L2（仓库工作流规则，不代表内容或运行时已验证）。

选择能完成用户目标的最小模式。纯文本指令路径以仓库根为基准，Markdown 链接相对本文件；先遵循 [AGENTS.md](../../../AGENTS.md)。

| 模式 | 工作与按需加载 |
| --- | --- |
| 单文档或小编辑 | 只读目标与相关 Canonical；按目标修改，不启动全库扫描或结构计划 |
| 元数据维护 | 读取 references/OKF-兼容规范.md 与 .kb/okf-profile.yaml |
| 来源材料 / 主题整理 | 读取 references/知识组织规则.md；盘点目标、找既有主题、分析重复与边界 |
| 审计 | 保持只读，按范围检查并报告 evidence，不因发现问题自动修复 |
| 全库重构 | 仅在用户要求或有结构失配证据时使用；加载 references/知识库架构.md 与知识组织规则 |

需要 Agent 协作、执行交接或发布时，读取 references/agent协作与发布规则.md。
该文档是角色与流程详细事实源；本 Skill 不重定义角色或权限。

编辑前确定精确文件 allowlist、写者与现有 dirty 基线。
结构变更先维护 .kb/plans/current.md 的目标、映射、范围、风险及验收；非结构小编辑在任务消息记录即可。
内部分析与审核不增加用户确认门槛，用户已授权范围内直接完成。
独立正文可按不相交 allowlist 并行；共享导航与控制面由指定整合者串行更新。

主题或来源材料工作先搜索既有 Canonical，再决定 Extend / Merge / Create / Archive；
读取 .kb/knowledge-map.json 的当前身份、主域与关系，遇到实际职责冲突才进行结构设计。低置信度操作进入 .kb/review-queue.md。
具体分类、信息保存和迁移规则按知识组织规则执行；taxonomy 仅作兼容指针，不自动重建已移除的通用知识、项目、接收或归档骨架。
本轮规则重建核对日期：2026-10-04。

完成时运行根 AGENTS 适用检查。内容与导航收敛后由整合者更新 manifest；
对照基线报告 changes、evidence、risks，明确已执行、未验证、既有失败。
提交与推送必须按协作规则的显式授权门禁另行处理。
