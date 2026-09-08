---
okf_version: "0.2"
---

# Learning OKF Index

> 知识成熟度：L2（OKF bundle 根索引；导航结构已纳入静态门禁）。

本页是 OKF/Obsidian 的机器可读入口；[README](README.md) 继续作为 GitHub 人类入口，[Global MOC](00_Index/MOC.md) 继续承担主题导航。三者只分工，不复制知识正文。

> 格式依据：[Open Knowledge Format v0.2](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md)。根据 OKF §8/§12，bundle 根 `index.md` 的 frontmatter 只声明 `okf_version`。

## 主要入口

- [知识体系总览](README.md)
- [知识库架构与事实源](references/知识库架构.md)
- [Global MOC](00_Index/MOC.md)
- [领域 MOC](00_Index/domains/README.md)
- [跨域主题地图](00_Index/axes/跨域主题.md)
- [知识边界与生命周期](00_Index/axes/知识边界与生命周期.md)
- [Obsidian Knowledge Base](00_Index/Knowledge.base)
- [知识体系完善执行方案](方案/知识体系完善执行方案.md)
- [Inbox](Inbox/README.md)

## OKF 与协作规则

- [OKF 兼容规范](references/OKF-兼容规范.md)
- [Obsidian 协作指南](references/Obsidian协作指南.md)
- [Agent 协作与发布规则](references/agent协作与发布规则.md)
- [Agent 配置与角色](.codex/README.md)
- [写作与验收规范](references/写作规范.md)
- [OKF profile](.kb/okf-profile.yaml)
- [Bundle 更新记录](log.md)

## 兼容状态

2026-08-20 本库 profile 范围已迁移完成：`check_okf` Strict 为 Scanned 390、Conformant 390、Legacy 0、Excluded 2。新建或修改的非保留 Markdown 仍必须包含 OKF YAML frontmatter；未来导入 legacy 内容使用 hybrid 分批流程。该结果是本库受支持 YAML 子集的门禁快照，不代表官方通用 OKF parser 认证。
