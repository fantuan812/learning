# OKF Bundle Log

> 知识成熟度：L2（OKF bundle 变更记录，只增不改）。

本文件只记录 OKF bundle 格式、入口和兼容级别变化。仓库维护经验仍追加到 [learning/log.md](learning/log.md)，两类日志不互相复制。

## 2026-08-20

- **Initialization**：建立 OKF v0.2 hybrid 兼容层；新增根 `index.md`、profile、Changed/Audit/Strict 校验、Obsidian Base 与知识条目模板。legacy 正文未批量改写。
- **Validation**：Changed 21/21 PASS；Audit 记录 369 个 legacy WARN；Strict 对这些 legacy 返回预期 FAIL；现有仓库门禁 PASS，未宣称全库官方严格合规。
- **Migration complete**：按支持/证据、计算机基础、游戏算法+AI、游戏服务端、游戏知识五批为 369 篇 legacy 文档补最小 frontmatter；每篇仅新增 7 行，正文、路径与链接不变。
- **Profile validation**：Changed/Audit/Strict 均为 Scanned 390、Conformant 390、Legacy 0、Excluded 2、FAIL 0；双 PowerShell 通过，manifest 与磁盘 392/392。该结果仅代表本库 profile 的轻量 lint，不是官方通用 OKF parser 认证。
