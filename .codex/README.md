---
type: Reference
title: "项目 Agent 配置"
description: "项目自定义角色的配置层、继承规则与验证边界。"
status: stable
verified: []
maturity: L2
updated: 2026-09-07
---

# 项目 Agent 配置

> 知识成熟度：L2。角色职责以 [协作与发布规则](../references/agent协作与发布规则.md) 为准。

[config.toml](config.toml) 保留 Agent 启用与并发上限 5（不含主线程）。
agents/ 下保留五个只读分析角色，另有 kb_executor、kb_integrator 和只读 kb_verifier。
发布者是用户授权后的流程角色，不注册自动角色。

## 配置与继承

官方自定义角色文件使用 name、description、developer_instructions；sandbox_mode 设角色默认值。
项目和角色文件不固定 model 或 model_reasoning_effort，由用户当前宿主配置和显式调用选择决定。
全局用户配置未被本次重构修改。

当前官方规则中，模型和 effort 先按显式 spawn、agents 默认、parent 解析，
然后自定义角色文件中设置的值具有优先权。因此只移除项目默认模型仍不足以消除角色硬编码。
name 是角色身份的事实源，文件名采用既有连字符习惯；不需要再添加旧式 config_file 注册表。

配置文件存在、TOML 解析通过只证明静态配置有效，不能证明当前宿主已加载。
新角色是否可调用、最终模型、权限和并发以实际工具声明与运行结果为准。
sandbox_mode 是默认值；父回合实时权限 override 可能生效。
文档 allowlist 与单一写者是协作合同，不能宣称宿主据此实施了文件级强制隔离。

## 验证

检查 TOML 可解析、角色名唯一、必填字段非空、只读/写入默认符合职责，
并运行 scripts/check_architecture.ps1。Skill 另使用 skill-creator 的 quick_validate.py。
运行时验证需在支持项目角色的宿主中实际调用后记录；本轮不声称已完成加载或执行验证。

## 官方依据

2026-09-07 实际检索并打开 OpenAI Docs：
[Subagents](https://learn.chatgpt.com/docs/agent-configuration/subagents)、
[Configuration Reference](https://learn.chatgpt.com/docs/config-file/config-reference)。
前者说明项目 agents 目录、必填字段、模型优先级、并发与 sandbox；
后者定义 agents.enabled、max_concurrent_threads_per_session、default_subagent_model 等配置键。
这些键当前有官方支持；第三方模型标识是否可用仍依赖具体 provider，不能从配置文件推断。
