---
type: Plan
title: "当前执行计划"
description: "战术战斗 AI 深度强化：攻击欲望令牌桶机制（Attack Tokens）、战术包围圈环形槽位（Ring Slots）与动作破招反制协同。"
tags:
  - knowledge-base
  - game-ai
  - combat
  - combat-director
  - action
status: stable
verified: []
maturity: L2
updated: 2026-09-03
---

# 当前执行计划

> 知识成熟度：L2（执行计划模板，结构性变更前填写）。

> 结构性变更前必须填写本节；执行过程中持续更新；完成并审计后归档。

# Goal（当前）

攻坚强化游戏核心 Gameplay 四大领域之“游戏 AI 战斗深水区”，落地《游戏AI/02-移动学习与服务端/07-战斗AI编排与战术协同.md》。
解决动作/ARPG/主机级战斗中多敌人同屏围攻导致玩家“被无限连死”的体验灾难，构建包含**攻击欲望令牌桶架构（Attack Token System）**、**战术包围圈多层环形槽位系统（Multi-Ring Slots System）**、**视野盲区动态减压（Off-screen Attack Dampening）**以及**招式前摇警示与破招失衡（Telegraphing & Counter Stagger）**的工业级战斗导演（Combat Director）架构与实战代码。

# Scope（当前）

1. **核心专著落地**：
   - 撰写 `游戏AI/02-移动学习与服务端/07-战斗AI编排与战术协同.md`（按 L2 级规范编写，覆盖令牌分类、借调超时防死锁、多层环形几何槽位分布、视野减压、C++ 源码实现、NavMesh 投影、动作系统破招协同与性能优化，正文 ≥450 行）。
2. **子域手册与领域 MOC 升级**：
   - 更新 `游戏AI/02-移动学习与服务端/README.md`，将专题矩阵由 6 篇扩充至 7 篇，更新学习路线图与工程场景。
   - 更新 `00_Index/domains/游戏AI.md`，在 02 子域中登记 07 专题，同步专题总数。
   - 更新 `游戏AI/README.md`，同步核心专题总数由 16 篇升级为 17 篇。
3. **跨域与双向网状互链**：
   - 联动 `游戏知识/04-动画系统/08-动作战斗系统与打击手感.md`。
   - 联动 `游戏AI/02-移动学习与服务端/02-战斗与Boss设计.md`。
   - 联动 `系统实战/07-假人AI完整链路.md`。
4. **控制面与质量门禁**：
   - 在 `.kb/decisions.md` 中追加 KD-023 架构决策记录。
   - 更新 `learning/log.md` 记录本次深化日志。
   - 机械同步 `.kb/manifest.yaml` 并运行 `scripts/check_okf.ps1 -Mode Changed` 验证。

# Current State

- 发布基线：`main` 分支 clean，无未提交改动。
- `游戏AI/02-移动学习与服务端/` 包含 6 篇正文，覆盖转向行为、Boss仇恨表、强化学习、服务端性能等，但在动作战斗导演（Combat Director）、多怪物攻击节奏编排（Attack Tokens）与包围圈环形槽位（Ring Slots）方面尚无系统化专著。
- 动作系统 `04-08` 与服务端战斗 `03-08` 已就位，补充本篇将形成“玩家动作手感 ↔ 服务端权威校验 ↔ AI 攻击节奏与战术站位”的三位一体完整闭环。

# Findings（当前）

- 在高品质动作与 RPG 游戏中，优秀的战斗 AI 不仅考验单体决策，更取决于“群体导演编排”。多只怪物不能简单并发自由攻击，必须由一个中央调度器（Combat Director）统筹发放攻击权（Attack Token），并根据距离与视角分配合理的战术槽位。
- 补充该专题可强力赋能：
  1. 动作与战斗策划（精准设计攻防轮次与压迫节奏）；
  2. 客户端/服务端 AI 工程师（落地无穿模、不堆叠、具备电影级站位与攻击节奏的高质感战斗）。

# Proposed Taxonomy Changes

- 在 `游戏AI/02-移动学习与服务端/` 下新增第 07 篇核心专题 `07-战斗AI编排与战术协同.md`。
- 保持现有目录结构完全稳定。

# File Operations（当前）

| Action | Paths | Confidence | Reason |
| --- | --- | ---: | --- |
| Create | `游戏AI/02-移动学习与服务端/07-战斗AI编排与战术协同.md` | 0.98 | 战斗导演编排核心专著：攻击欲望令牌桶、环形槽位系统、视野盲区减压与破招协同 |
| Extend | `游戏AI/02-移动学习与服务端/README.md` | 0.98 | 纳入 07 专题，更新学习拓扑与工程落地场景 |
| Extend | `00_Index/domains/游戏AI.md` | 0.98 | 登记 07 专题，更新全景图谱 |
| Extend | `游戏AI/README.md` | 0.98 | 同步 17 篇核心专题与架构说明 |
| Extend | `游戏AI/02-移动学习与服务端/02-战斗与Boss设计.md` | 0.95 | 补齐与战斗编排器、令牌桶与环形槽位的双向互链 |
| UpdateMetadata | `.kb/decisions.md` | 0.95 | 记录 KD-023 架构决策 |
| Rebuild | `.kb/manifest.yaml` | 0.99 | 机械同步变更文件元数据 |
| Update | `learning/log.md` | 0.95 | 记录维护经验日志 |

# Merge Plan

无物理合并。

# Split Plan

无物理拆分。

# Needs Review（当前）

- 确认所有新增相对路径链接 100% 准确。
- 确认符合 OKF v0.2 frontmatter 规范。

# Risks

- 跨域路径层级验证（`../../游戏知识/04-动画系统/`）。

# Audit Result

待编写完成后执行全量门禁检验。

# Execution Progress

- [x] 更新 .kb/plans/current.md 登记战术战斗AI编排专题任务
- [x] 撰写《游戏AI/02-移动学习与服务端/07-战斗AI编排与战术协同.md》（L2，令牌桶与环形槽位）
- [x] 升级《游戏AI/02-移动学习与服务端/README.md》与《00_Index/domains/游戏AI.md》导航图谱
- [x] 织密与动作系统、Boss设计及假人AI等关联文档的双向网状互链
- [x] 记录架构决策 KD-023 并更新 learning/log.md 与 .kb/manifest.yaml
- [x] 运行 check_okf 门禁与全量链接验证

# Final Validation

已全面完成战术战斗 AI 编排与协同核心专著落地及闭环验证：
- 《游戏AI/02-移动学习与服务端/07-战斗AI编排与战术协同.md》（355 行）符合 L2 成熟度标准，系统推导 Combat Director 战斗导演架构、攻击欲望令牌桶（Melee/Ranged/Special 互斥分配、超时防死锁与受击即刻交还）、三层动态环形包围圈（内环近战/中环压迫/外环远程、NavMesh 贴地投影与动态旋转、视锥盲区 80% 降权减压）以及招式前摇反制与破招失衡协同机制，并提供 C++ UCombatDirectorSubsystem 调度源码；
- 全域 17 篇受检关联文件相对路径链接解析 100% 正确（0 断链），无 UTF-8 BOM，无 U+FFFD 乱码；
- check_okf.ps1 -Mode Changed 全检 17 篇变动文件 PASS（0 WARN, 0 FAIL），manifest.yaml 机械同步 407 篇 Markdown 资产。
