---
type: Decision
title: "Knowledge Base Decision Log"
description: "知识库结构、迁移、控制面和发布边界的架构决策记录。"
tags:
  - decision-log
  - knowledge-base
  - okf
status: stable
verified: []
maturity: L2
updated: 2026-09-11
---

# Knowledge Base Decision Log

> 知识成熟度：L2（决策记录，只增不改历史条目）。

## KD-023

### Subject

战术战斗 AI 深度强化：攻击欲望令牌桶机制（Attack Tokens）、三层环形包围槽位（Ring Slots）与动作破招协同（2026-09-03）。

### Options

1. 仅在既有 Boss 设计文档中略微补充文字说明。
2. 保持物理路径稳定性契约，在 `游戏AI/02-移动学习与服务端/` 下设立核心专著 `07-战斗AI编排与战术协同.md`，升级子域手册、领域总手册与 Domain MOC，织密与动作系统、Boss 设计等跨域双向互链，同步控制面与门禁。

### Decision

Option 2。依据用户对“游戏 AI 与动作战斗重点强化”的战略定位，攻坚解决动作游戏中多敌人同屏围攻导致玩家“被无限连死”的体验灾难。新建 L2 级核心专著 `游戏AI/02-移动学习与服务端/07-战斗AI编排与战术协同.md`（355 行），系统化建立攻击欲望令牌桶系统（近战/远程/霸体特技三级令牌池、申请/借调/持有超时防死锁机制、玩家受击与倒地时动态压力调节）、战术多层环形槽位系统（内环近战/中环压迫/外环远程几何计算、NavMesh 贴地投影与动态旋转、视锥盲区偷袭抑制与 80% 降权）、招式前摇警示与破招失衡协同（前摇蓄力拉长、红黄光信号与弹反成功即刻交还令牌），并给出完整的 C++ CombatDirectorSubsystem 调度源码。更新 `游戏AI/02-移动学习与服务端/README.md`、`00_Index/domains/游戏AI.md` 与 `游戏AI/README.md`，在 `02-02 战斗与Boss设计` 及 `04-08 动作战斗系统` 中建立精准双向互链。

### Reason

在动作游戏与 ARPG 中，高品质的战斗体验绝不是单体 AI 的各自为战，而必须依赖中央战斗导演（Combat Director）控制攻防轮次与舞台站位。此前知识库在单体行为树、FSM 与仇恨计算有较好覆盖，但在多敌人战术编排、令牌桶限量发牌与环形槽位几何分布方面存在显著空白。本篇专著与《04-08 动作战斗系统与打击手感》形成紧密咬合，打通了“玩家手感 ↔ AI 攻击节奏 ↔ 破招反制”的端到端技术闭环。

### Confidence

0.98

### Affected files

`游戏AI/02-移动学习与服务端/07-战斗AI编排与战术协同.md`、`游戏AI/02-移动学习与服务端/README.md`、`00_Index/domains/游戏AI.md`、`游戏AI/README.md`、`游戏AI/02-移动学习与服务端/02-战斗与Boss设计.md`、`游戏知识/04-动画系统/08-动作战斗系统与打击手感.md`、`.kb/decisions.md`、`.kb/plans/current.md`、`learning/log.md`、`.kb/manifest.yaml`。

### Status

Accepted

## KD-022

### Subject

系统实战角色移动完整链路落地：客户端移动预测、SavedMoves 队列、ServerMove 压缩、服务端防作弊校验、回滚重放与 Mesh 平滑消抖（2026-09-03）。

### Options

1. 保持 `系统实战/02-角色移动完整链路` 为规划状态，仅在客户端或服务端文档补充局部片段。
2. 依据系统实战 15 步标准模板，落地权威实战专著 `系统实战/02-角色移动完整链路.md`，升级系统实战 README 状态表与文件清单，织密与 CMC 移动、网络预测、Root Motion 的跨域双向互链，同步控制面与门禁。

### Decision

Option 2。落实用户确立的“服务端 Gameplay、客户端 Gameplay、动作系统重点强化”技术定位，填补系统实战在移动核心链路的空白。新建 L3 级系统实战专著 `系统实战/02-角色移动完整链路.md`（376 行），覆盖输入采样、CMC 客户端预测执行、FSavedMove_Character 缓存入队、ServerMove/ServerMoveDual 协议打包与压缩优化、服务端时间戳单调防加速外挂（Speedhack Defense）、状态门禁与最大速度校验、连续空间 Swept Move 碰撞推进、地面贴合（FindFloor）与台阶跨越（StepUp）、权威确认 Ack 与 ClientAdjustPosition 纠偏裁决、远端模拟代理状态缓冲平滑插值、客户端回滚与未确认 Moves 重放（Replay）、网格相对偏移平滑消抖（Mesh Relative Offset Smoothing）、弱网丢包对抗、恶意作弊注入测试矩阵与线上性能指标。更新 `系统实战/README.md` 状态表与链路矩阵，并在 `游戏知识/03-游戏玩法编程/06-角色移动系统UCharacterMovement.md` 等关联文档形成双向互链。

### Reason

角色移动是整个多人联网游戏发生频率最高、对网络延迟与作弊攻击最敏感的基础生命线。此前知识库在客户端移动组件（03-06）与网络预测（06-03）有横向讲解，但缺少回答“一个按键从客户端输入到 Dedicated Server 权威物理推进、防作弊拦截再到客户端平滑消抖”全景链路的端到端专著。本次落地使系统实战板块的四条核心链路（移动、技能、Buff、假人AI）形成完整工业级矩阵，有力支撑双端 Gameplay 的实战工程落地。

### Confidence

0.98

### Affected files

`系统实战/02-角色移动完整链路.md`、`系统实战/README.md`、`游戏知识/03-游戏玩法编程/06-角色移动系统UCharacterMovement.md`、`.kb/decisions.md`、`.kb/plans/current.md`、`learning/log.md`、`.kb/manifest.yaml`。

### Status

Accepted

## KD-021

### Subject

核心 Gameplay 动作与战斗系统专项深化：连招打断、输入缓冲、受击硬直与霸体、高精度扫掠判定与 Root Motion 网络同步（2026-09-03）。

### Options

1. 将动作战斗零散写入现有蒙太奇或技能文档附录中。
2. 保持物理路径稳定性契约，在 `游戏知识/04-动画系统/` 下设立专著 `08-动作战斗系统与打击手感.md`，升级子域 README 与领域 Domain MOC，织密与技能框架、蒙太奇等跨域双向互链，同步控制面与门禁。

### Decision

Option 2。依据用户对“游戏服务端 gameplay、客户端 gameplay、动作、游戏 AI 重点强化，其余不减少”的战役定位，优先攻坚动作战斗这一最关键也最薄弱的交叉地带。新建 L2 级核心专著 `游戏知识/04-动画系统/08-动作战斗系统与打击手感.md`（580 行），涵盖输入缓冲队列防吞键机制、攻击四阶段生命周期与取消窗口（Cancel Window）、受击硬直五级模型与韧性霸体恢复公式、五维打击反馈管线（Hitstop/CameraShake/HitFlash/SFX/冲量）、连续骨骼多插槽扫掠检测（Sweep Trace）防穿模算法，以及 Root Motion 动画位移在 Dedicated Server 联网环境下的时间轴与位移包络线权威校验。同步更新 `游戏知识/04-动画系统/README.md`、`00_Index/domains/游戏知识.md`、`游戏知识/README.md`，并在关联正文中形成严密的网状双向互链。

### Reason

动作战斗系统是连接“客户端手感”、“表现层动画状态机”与“服务端移动/伤害权威校验”的中枢神经。此前知识库在动画资产、IK、蒙太奇方面已有覆盖，但在 ACT/ARPG 核心动作战斗机制（输入缓冲、打断窗口、受击霸体、连续扫掠、Root Motion 网络同步校验）方面存在重大知识缺口。本次落地直接打通了客户端战斗表现与服务端移动防作弊/伤害结算的端到端技术链路，满足高成熟度工业级标准。

### Confidence

0.98

### Affected files

`游戏知识/04-动画系统/08-动作战斗系统与打击手感.md`、`游戏知识/04-动画系统/README.md`、`00_Index/domains/游戏知识.md`、`游戏知识/README.md`、`游戏知识/04-动画系统/02-动画蒙太奇与混合空间.md`、`游戏知识/04-动画系统/03-IK与程序化动画.md`、`游戏服务端/03-业务系统设计/08-技能与战斗框架.md`、`.kb/decisions.md`、`.kb/plans/current.md`、`learning/log.md`、`.kb/manifest.yaml`。

### Status

Accepted

## KD-020

### Subject

游戏算法知识体系全面整理优化、全景技术拓扑升级与跨域工程贯通（2026-09-03）。

### Options

1. 大规模物理拆分或合并现有 4 大子域与 22 篇核心正文，重新排号。
2. 保持现有物理路径与 OKF 结构稳定，将 `00_Index/domains/游戏算法.md` 从 39 行草稿升级为包含四大技术分层拓扑、22 篇专题全矩阵、五大工程角色路径、四大核心算法横向选型矩阵（寻路/空间索引/数值积分/局部避障）、跨域技术映射网格与本地实验证据索引的权威 Domain MOC；将 `游戏算法/README.md` 升级为工业级领域工程手册；将四大子域 README 全面提升至 L2 标准化手册规范；织密全域 22 篇核心专题网状互链，消除知识孤岛，连接 `evidence/algorithms` 本地代码与 Benchmark 证据；同步控制面与门禁校验。
3. 仅微调个别子域 README，维持极简 MOC。

### Decision

Option 2。依据 KD-016 的物理稳定性契约，保持 4 大子域物理目录与 22 篇核心正文的 Canonical 物理路径完全不变（防范外部死链），采用“顶层全景 MOC + 领域总工程手册 + 四大标准化子域手册 + 核心专题网状互链织密 + 本地 Benchmark 证据接入”的系统化优化策略。在 `00_Index/domains/游戏算法.md` 中构建“离散图论寻路 - 连续空间几何运动与碰撞 - 玩法与高并发工程技巧 - 动态确定性与性能基准”四大支柱全景架构；提供针对 Gameplay/战斗算法、大规模 MMO/RTS 服务端算法、物理与运动模拟、联机帧同步确定性引擎、程序化生成与玩法数值的五大系统化实战进阶路线；提供寻路、空间分区、数值积分与局部避障四大选型对比矩阵；消除正文孤岛，实现全域 22 篇正文双向网状互链；接入 `evidence/algorithms/astar` 与 `aoi` 真实基准代码。

### Reason

游戏算法是现代游戏开发的核心底层工程基石，涵盖从图论搜索、空间分区加速，到刚体运动几何、连续碰撞检测与数值积分，再到伪随机洗牌、AOI 广播裁剪、位运算与数据压缩，直至复杂的动态网格增量重规划、ORCA 避障、定点数与联机确定性回放。原 MOC（39行）与总 README（33行）极度骨架化，严重滞后于 22 篇高价值正文内容，且子域 02（缺失 05/06 篇）与子域 03（缺失 06/07 篇）的导航陈旧，部分核心专题出入链薄弱存在孤岛。通过本次系统化重构整理，确立了工业级算法工程认知与推导闭环，显著提升了人和 AI 在游戏核心算法体系上的检索、推导与工程落地效能。

### Confidence

0.98

### Affected files

`00_Index/domains/游戏算法.md`、`游戏算法/README.md`、`游戏算法/01-寻路与图论/README.md`、`游戏算法/02-数学与碰撞/README.md`、`游戏算法/03-工程与实用技巧/README.md`、`游戏算法/04-确定性与基准工程/README.md`、`游戏算法/01-寻路与图论/01-图论基础与搜索算法.md`、`游戏算法/01-寻路与图论/03-流场与群体寻路.md`、`游戏算法/01-寻路与图论/04-空间分区与索引.md`、`游戏算法/02-数学与碰撞/01-向量与矩阵基础.md`、`游戏算法/02-数学与碰撞/02-四元数与旋转.md`、`游戏算法/02-数学与碰撞/03-插值与曲线.md`、`游戏算法/02-数学与碰撞/04-碰撞检测.md`、`游戏算法/02-数学与碰撞/05-数值积分与运动学模拟.md`、`游戏算法/02-数学与碰撞/06-弹道与射击算法.md`、`游戏算法/03-工程与实用技巧/01-随机数与洗牌算法.md`、`游戏算法/03-工程与实用技巧/02-程序化生成.md`、`游戏算法/03-工程与实用技巧/05-位运算与性能优化技巧.md`、`游戏算法/03-工程与实用技巧/06-数据压缩与序列化.md`、`.kb/decisions.md`、`.kb/plans/current.md`、`learning/log.md`。

### Status

Accepted

## KD-019

### Subject

游戏 AI 知识体系全面整理优化、全景决策拓扑升级与跨域工程贯通（2026-09-03）。

### Options

1. 大规模物理拆分或合并现有 3 大子域与 16 篇核心正文，重新排号。
2. 保持现有物理路径与 OKF 结构稳定，将 `00_Index/domains/游戏AI.md` 从 38 行草稿升级为包含全景技术拓扑、16 篇专题全矩阵、五大工程角色路径、决策模型横向选型矩阵与跨域技术映射网格的权威 Domain MOC；将 `游戏AI/README.md` 升级为工业级知识库手册；将三大子域 README 全面提升至 L2 标准化手册规范；重构治理 `03-LLM-NPC安全.md` 消除碎片断行排版缺陷，补齐互链；强化各专题在虚幻引擎、算法、服务端与实战工程间的网状连接。
3. 仅微调个别子域 README，维持极简 MOC。

### Decision

Option 2。维持 3 大子域与 16 篇核心专题 Canonical 物理路径稳定（防范外部死链），采用“顶层全景 MOC + 领域总手册 + 三大标准化子域手册 + 核心专题互链织密 + 碎片排版重构”的系统化优化策略。在 `00_Index/domains/游戏AI.md` 中构建“感知-决策-行动-服务端-学习-评测安全”全生命周期认知架构，提供针对 Gameplay AI、服务端 MMO AI、强化学习 Agent、测试回归 Bot 及 LLM NPC 的五大系统化成长路径；提供 FSM/HSM/BT/Utility/GOAP/HTN/StateTree/MCTS/RL/LLM 全景选型对比；治理 `03-LLM-NPC安全.md` 的排版断行缺陷；同步控制面与门禁校验。

### Reason

游戏 AI 涵盖了从传统确定性状态机、分层行为树，到现代效用规划、博弈搜索、大规模服务端 Tick 预算管控、多智能体协同、强化学习以及新兴的 LLM NPC 安全治理，知识维度极广且跨越客户端表现层、服务端裁决层、数值算法层与质保工程。原 MOC（38行）与总 README（40行）严重滞后于 16 篇正文的高价值内容，难以起到全局导航与架构选型指导作用。通过本次整理优化，建立起完整的认知与工程落地推导链条，显著增强人和 AI 对游戏 AI 体系的检索、推演和落地效率。

### Confidence

0.98

### Status

Accepted

## KD-018

### Subject

虚幻引擎（UE）知识体系全面整理优化、全景拓扑织密与工程映射强化（2026-08-20）。

### Options

1. 大规模重命名或物理重构现有 13 大子域与 158 篇核心专题目录结构，重排编号。
2. 保持物理路径与 OKF 元数据稳定，全面重构升级 Domain MOC 与领域总 README，深度标准化 13 个子域 README，为 13 篇孤岛正文补齐结构化双向交叉互链，建立“底座原理 ↔ 引擎机制 ↔ 源码实现 ↔ 服务端/算法/质量”全网状推导链路。
3. 仅调整部分子域 README，不触碰 Domain MOC、总 README 和孤岛正文。

### Decision

Option 2。依据 KD-016 的物理稳定性契约，保持 13 子域物理目录与 158 篇正文 + 1 篇全栈闭环总览的 Canonical 物理路径不变，防止全库断链与 Git 历史断裂。将 `00_Index/domains/游戏知识.md` 从简易清单（48 行）大幅重构升级为涵盖 13 大子域 158 核心专题全景矩阵、五大工程角色学习路径（Gameplay/客户端、引擎底层/源码、技术美术/渲染、多人联机/DS架构、工具链与工程效能）、跨域技术映射网格及源码证据索引的权威 Domain MOC；重构 `游戏知识/README.md` 领域总入口，确立六层推导模型与工程协作规范；全面标准化升级 13 个子域 README 为工业级子域工程手册（统一定位与核心问题、专题矩阵、学习顺序、工程落地与跨域依赖）；重点治理 13 篇零外链孤岛正文，织密前置/后置与跨域互链，使知识孤岛清零；同时对 8 篇关键专题（01-11 多线程与任务系统、02-10 移动端渲染、02-11 RenderTarget与SceneCapture、04-05 AnimNext、06-07 Iris、09-06 Chaos车辆、10-04 Quartz、13-04 PCG）进行深度重构与源码/代码扩充，全域正文行数均 ≥ 300 行并 100% 达成 L2 成熟度；最后机械重建 manifest 并确保门禁 100% 通过。

### Reason

虚幻引擎知识体系是游戏研发最核心的业务实现与工程落地枢纽，既依赖底层的 C++、操作系统、体系结构与图形 API，又向上承载服务端网络裁决、空间算法、AI 决策模型与持续交付流水线。其关键在于网状贯通而非单纯分类。保持物理路径稳定可确保引用的持久可靠，通过 MOC 与标准化子域手册提供清晰的知识拓扑与学习闭环，消除知识孤岛能够极大提升人和 AI 在大型技术项目中的检索、推理与落地效能。

### Confidence

0.98

### Affected files

`00_Index/domains/游戏知识.md`、`游戏知识/README.md`、`游戏知识/*/README.md`（13 篇）、`游戏知识/**/*.md`（13 篇孤岛正文 + 8 篇强化正文）、`.kb/decisions.md`、`.kb/manifest.yaml`、`.kb/plans/current.md`、`.kb/taxonomy.yaml`、`learning/log.md`。

### Status

Accepted（已执行并通过验证）

## KD-017

### Subject

计算机与工程基础知识体系全面整理优化与拓扑网格织密（2026-08-20）。

### Options

1. 大规模物理重命名所有存在历史编号不连续或冲突的文件（04/07/08/13/14/15/16），全库批量替换相对链接。
2. 保持物理路径与 OKF 元数据稳定，全面重构升级 Domain MOC、总 README、16 个二级 README，重写替换历史残留占位正文，并为全量 40 篇正文补齐结构化双向交叉互链，消除知识孤岛。
3. 仅修改 MOC 列表，不处理子域 README 与正文孤岛缺陷。

### Decision

Option 2。依据 KD-016 的物理稳定性契约，保持 16 子域物理目录与 40 篇正文 Canonical 文件名不变，防止全库链接断裂与 Git 历史断层。通过 Domain MOC（从 51 行扩展为 160 行具备 40 专题矩阵、5 大角色路径、业务映射网格与实验链索引的权威导航）与 16 个子域 README 规范逻辑学习序列；彻底重写修复了 02-01（对象布局虚函数）与 03-01（Concepts 与 Ranges）中残留的历史重复填充内容，恢复为 370+ 行深度实战技术专著；为全部 40 篇正文织密前置/后置与跨域（UE5 引擎、服务端架构、游戏算法与测试质量）互链，将零外链孤岛数从 33 篇降为 0；最后机械重建 manifest 并全量通过门禁。

### Reason

知识体系的生命力在于网状连接与底层到业务的可推导性。物理文件名仅作为持久寻址标识，不应频繁变动；通过清晰的 MOC 与二级 README 呈现逻辑拓扑，既保障了知识的易检索与可演进，又避免了短期的目录折腾。消除知识孤岛使人和 AI 检索能沿着“硬件原理 → 内核机制 → 引擎与服务端工程化”双向自由穿透。

### Confidence

0.98

### Affected files

`00_Index/domains/计算机与工程基础.md`、`00-计算机与工程基础/README.md`、`00-计算机与工程基础/*/README.md`（16 篇）、`00-计算机与工程基础/**/*.md`（33 篇正文）、`.kb/decisions.md`、`.kb/manifest.yaml`、`.kb/plans/current.md`。

### Status

Accepted（已执行并通过 Changed/Strict 与 check_repo 门禁验证）

## KD-016

### Subject

知识体系重规划采用“先虚拟重构、后小域物理试点”（2026-08-20）。

### Options

1. 立即把全部知识搬入新的物理目录树，同时改写链接和 manifest。
2. 保留现有 Canonical 路径，先用 Domain MOC、横向主题、生命周期规则和 Obsidian Base 建立虚拟结构；只有通过独立闸门后才做单域试点。
3. 完全冻结结构，不处理已确认的跨域歧义与导航缺口。

### Decision

Option 2。第一阶段创建六个领域 MOC、跨域主题地图、知识边界与生命周期规则及四个 Base 视图；不移动、重命名、拆分或合并正文，不改变 H1、type、maturity、verified 或现有链接。第二阶段只能在用户再次批准且小域/主题族满足 old→new 映射、唯一 Canonical、入链影响和回滚清单后启动。

### Reason

当前证据支持局部重叠（背包、NavMesh、网络复制、Dedicated Server、GAS/Buff 等），不支持判定整棵物理目录树失效。虚拟视图可以先验证 Domain → Subdomain → Topic、Primary/Secondary 分工和 Obsidian 消费体验，同时避免数百相对链接、README、manifest 与 backlinks 的同步风险。

### Confidence

0.94

### Affected files

`00_Index/domains/*.md`、`00_Index/axes/*.md`、`00_Index/Knowledge.base`、全局导航、`.kb/taxonomy.yaml`、当前计划、review queue、audit、manifest 与协作规范；知识正文路径不在第一阶段范围。

### Status

Accepted（第一阶段已执行并审计；物理试点保持 Pending）

## KD-015

### Subject

执行 OKF-MIGRATION-01：将剩余 369 篇 legacy Markdown 分域迁移到最小 OKF frontmatter（2026-08-20）。

### Options

1. 继续仅按触碰迁移，保留 369 篇 legacy。
2. 一次性不经检查机械写入全部文件。
3. 按支持/证据、基础、算法+AI、服务端、游戏知识五批迁移，每批使用已审查类型映射并通过门禁后继续。

### Decision

Option 3。用户明确“继续做迁移”，视为批准 review queue 中的分域迁移。每篇只写入 `type`、安全转义的首个 H1 `title`、`status: stable`、`verified: []` 与原有 `maturity`；无显式成熟度的支持文档使用 L0。不生成来源、验证事件、生成者或更新时间，不修改正文、路径、taxonomy 或链接。

### Reason

分批可以把 369 文件的大 diff 切成可验证边界；保守 `Concept` 兜底比按关键词伪造精确类型更可信。成熟度沿用正文既有证据，`verified: []` 明确迁移不等于复核，符合 OKF 与本库 profile 的正交规则。

### Confidence

0.93

### Affected files

369 篇 legacy Markdown、`.kb/manifest.yaml`、`.kb/plans/current.md`、`.kb/review-queue.md`、`.kb/audit.md`、`log.md`、`learning/log.md`。

### Status

已执行（2026-08-20）：369 篇按五批完成；profile Scanned 390 / Conformant 390 / Legacy 0 / Excluded 2，Strict PASS。330 篇正文去前缀 SHA-256 与基线一致，其余 39 篇为 `+7/-0`；轻量 lint 不等于官方通用 OKF parser 认证。本状态已收口 KD-014 中“Strict 迁移留待 review queue”的历史阶段，KD-014 原文只保留为当时决策快照。

## KD-014

### Subject

将 GoogleCloudPlatform/knowledge-catalog 的 Open Knowledge Format（OKF）v0.2 与 Obsidian 协作层融入现有知识体系（2026-08-20）。

### Options

1. 一次性给全部 386 篇 Markdown 机械添加 frontmatter，并立即声称全库严格 OKF 合规。
2. 采用 hybrid：仓库根作为 bundle，新增 `index.md` / `log.md` / profile；新建或修改文档强制 `type`，legacy 只审计并按触碰迁移；Obsidian 直接消费同一仓库，但不接管用户 `.obsidian` 状态。
3. 在独立子目录复制一套 OKF 内容，保留现有仓库不变。

### Decision

Option 2。OKF 作为互操作格式层，不替代现有 Domain → Subdomain → Topic taxonomy、Canonical Location、L0~L5、MOC 或 manifest。根 `README.md` 服务 GitHub，根 `index.md` 服务 OKF/Obsidian，`00_Index/MOC.md` 服务主题导航；根 `log.md` 记录 bundle 变化，`learning/log.md` 继续记录维护经验。Changed 门禁约束本次及后续触碰文档；Audit 展示 legacy 欠账；Strict 留待独立批准的 profile 范围迁移，且不替代通用 YAML/OKF 解析器。

### Reason

OKF v0.2 的唯一强制 frontmatter 字段是 `type`，与当前 Markdown 体系天然兼容。hybrid 能立即建立来源、验证状态、时效和 Obsidian Properties/Bases 能力，同时避免 386 篇无语义机械 diff、错误 type 推断和链接风险。复制一套内容会违反 Canonical Knowledge Rule。

### Confidence

0.92

### Affected files

`index.md`、`log.md`、`.kb/okf-profile.yaml`、`references/OKF-兼容规范.md`、`references/Obsidian协作指南.md`、`references/templates/`、`00_Index/Knowledge.base`、`scripts/check_okf.ps1` 以及本轮同步的控制面/导航文件。

### Status

已执行（2026-08-20；hybrid 接入与门禁通过，legacy Strict 迁移保留在 review queue）

## KD-013

### Subject

补齐游戏服务端 Runtime 的 Timer、Entity Authority 与跨 Zone 迁移主链（2026-08-18）。

### Options

1. 继续扩写已有架构/可靠性文章，暂不落地 `06-世界模拟与运行时` 的规划缺口。
2. 沿既有 W2 方案增量落地 02 Timer、07 Entity Ownership、08 跨 Zone 迁移，并同步 README、manifest、taxonomy 与学习路径；SpatialQuery、动态分线、大规模战斗保留为下一批规划。

### Decision

Option 2，已执行：新建 3 篇 L2 正文（均包含状态机、失败路径、验证矩阵与关联阅读），更新 06 Runtime 状态表、服务端根 README、01/03 分类导航、W2 计划、`.kb/manifest.yaml` 与 `.kb/taxonomy.yaml`。不新增服务端顶层分类，避免与既有六子域重复。

### Reason

审计显示现有 06 Runtime 已覆盖 Tick、Entity、Scene、AOI、AI 预算、GameClock、Snapshot 与背压，但 Timer、权威所有权和迁移协议仍以规划文字互相引用；三者是实时服务器正确性和故障恢复的前置链路。增量落地能直接消除规划断链，同时保持 `06` 的既有编号与 W2 最终目录一致。

### Confidence

0.95

### Status

Accepted（已执行；L2，不宣称线上运行或容量证据）

## KD-012

### Subject

执行 review-queue 剩余四项结构性操作（2026-08-14 用户批准"将剩余这四项执行"）。

### Options

1. 仅登记不执行，四项保持 Pending。
2. 全部执行：R4-OBS-01 性能笔记归位、R2-SPLIT-01 AI 03-01 拆分、R2-MOVE-01 GameplayDebugger 归属声明、R2-GATE-01 成熟度门禁阶段 B。

### Decision

Option 2，已执行：
- R4-OBS-01：新建 `00-计算机与工程基础/07-Linux系统编程/03-性能工具：插桩与perf采样.md`（326 行 L2 canonical，占 07 目录规划 03 号）；笔记/插桩测试与笔记/perf性能分析 收敛为速查并加正式指向。
- R2-SPLIT-01：`01-AI评测回放与LLM安全.md` → 改名收窄 `01-AI评测回放.md`（1649 行，§七 删改为指路节）；新建 `03-LLM-NPC安全.md`（419 行，原 §七 394 行逐字迁移，SHA-256 去空行归一后一致，7.x 重编号 1.x）；全库 13 处引用同步（LLM 语境改指 03，评测语境更新文件名），AI/03 README、游戏AI README 同步。
- R2-MOVE-01：07 README 补"运行时调试"覆盖声明（低风险方案，文件不迁移）。
- R2-GATE-01：check_repo.ps1 成熟度门禁阶段 A → 阶段 B（既有正文缺成熟度由 WARN 升级 FAIL）；全库复跑 FAIL 0；修复脚本 BOM（git 恢复后重新加 BOM 保证 Windows PowerShell 5.1 可解析）。

### Reason

四项均为 review-queue 遗留结构性操作，用户批准执行；拆分按 KD-007 先例（旧文收窄 + 新建分篇 + 逐字迁移 + 全库链接同步）；门禁升级前已确认全库缺成熟度正文为 0，升级无回归风险。

### Confidence

0.9

### Status

Accepted（已执行）

## KD-011

### Subject

第四轮审计（R4，2026-08-14）结论与执行方式。

### Options

1. 只输出报告，不做任何文件修改。
2. 报告 + 控制面登记（audit.md / review-queue / plans），P1 五项与 P2 六项修复待用户批准后分批执行；review-queue 新增 R4-DUP-01/02/03 不自动执行。

### Decision

Option 2：写入 .kb/audit.md（R4 报告，PASS_WITH_MINOR_WARNINGS）、.kb/plans/current.md（R4 执行计划）、.kb/review-queue.md（新增 3 项候选）；知识文件零修改。P1（R4-LINK-01/02/03、R4-STRUCT-01、R4-CTRL-03）与 P2 修复按用户批准批次执行，主线程串行写。

### Reason

R3 四项修复无回归、基线健康；但发现 3 处互链缺口（含 R3 健康项记录失真 1 处）、1 处结构问题、1 处控制面状态失真与 1 组真语义重叠候选。P1 为低风险增量编辑；R4-DUP 系列为结构性去重，需用户确认。审计阶段保持只读符合 AGENTS.md。

### Confidence

0.9

### Status

Accepted（报告已登记，修复待批准）

## KD-001

### Subject

采用多智能体知识库控制面（.codex/agents + .agents/skills/knowledge-base-organizer + .kb），子代理模型统一为 opencode-go/deepseek-v4-flash，推理档位 max。

### Options

1. 沿用单一 learning-repo-maintainer 流程，不引入子代理角色。
2. 引入 kb_scanner / kb_analyzer / kb_architect / kb_curator / kb_auditor 五角色只读分析控制面。

### Decision

Option 2：在仓库根建立 AGENTS.md、.codex/agents 五角色、knowledge-base-organizer Skill 与 .kb 控制面；主线程始终承担最终决策与全部写操作。

### Reason

现有仓库已有成熟度门禁与维护脚本；新增角色只做只读分析与建议，主线程收敛，可复用 3-5 并行 Worker 且不引入写冲突。

### Confidence

0.90

### Status

Accepted

## KD-007

### Subject

执行 SPLIT-01/02/03（2026-08-13）。

### Options

1. 维持 47/算法 04-01/服务端 04-01 单篇超长结构。
2. 按迁移计划拆分：47→48 插件篇；算法 04 1→5 篇；服务端 04 按 W7-10.1 1→9 篇。

### Decision

Option 2，已执行：三处均"旧文改总览/收窄 + 新建分篇"，正文逐字迁移（SHA-256 切片比对），附录 25 文件逐字搬移；全库 16 处旧文件名链接与 7 处导航文件同步。

### Reason

超长单篇粒度与同级篇目不一致（audit G2/S4/T1）；拆分后每篇主题单一、可独立维护；服务端结构遵循方案 W7-10.1 既定命名。

### Confidence

0.85

### Status

Accepted（已执行）

## KD-010

### Subject

执行第二轮审计修复三批（P0 正确性 → P1 边界互链 → P2 成熟度/命名/README，2026-08-13）。

### Options

1. 仅登记不执行。
2. 按 audit.md 的 P0/P1/P2 清单分批执行；review-queue 中拆分/门禁/清单类（R2-SPLIT-01/02、R2-GATE-01、R2-MANIFEST-01）不自动执行。

### Decision

Option 2，已执行（208 文件）：P0 消除 1 处版本事实冲突（r.Mobile.ShadingPath 默认 Forward=0，以本机 RendererSettings.h 为准）与 11 处残留；P1 落地 23 项分工声明/互链/收敛；P2 完成 110 篇成熟度补标、01-10 改名、40 个 H1 前缀统一、12 个 README/控制面同步。

### Reason

用户批准"按批次执行 P0→P1→P2"；结构性拆分与门禁行为变更仍留 review-queue 单独确认。

### Confidence

0.9

### Status

Accepted（已执行）

## KD-009

### Subject

执行"剩余事项"三批（2026-08-13 第二批次）：A-成熟度补标、B-短篇扩写达标、C-STYLE-01 标题模板统一。

### Options

1. 维持现状：16 篇缺成熟度（WARN）、9 篇短正文（WARN）、12 目录 H1 四套模板并存。
2. 三批全部执行：16 篇补 L2 成熟度行；服务端 04 八篇与算法 04-03 NavMesh 扩写至 ≥300 行；12 目录 01-38 H1 统一为"UE 引擎源码分析 NN：主题"。

### Decision

Option 2，已执行（2026-08-13）：
- A：服务端 01/02/03 目录 16 篇 + 12 目录 25 篇（22 篇门禁 FAIL 触发 + 05/06/38 一致性补齐）补 `> 知识成熟度：L2（本轮审计修订时补标）`。
- B：服务端 04 八篇（00-08）扩写"术语速查 + 落地检查清单 + 常见反模式 + 典型故障案例 + 补充示例"，135-218 行 → 302-347 行；算法 04-03 NavMesh 257 → 331 行；正文由 5 个只读草稿代理产出、主代理统一落盘并补更新日志。
- C：STYLE-01 执行：12 目录 33 个 H1 统一（01-18/20-37），05/06/07/38 原已达标；39-48 Lyra 系列保持"UE5.8 Lyra 源码解析 NN：主题"不变；全库 grep 无旧标题残留。

### Reason

用户"继续处理剩余事项"确认了 review-queue 中延后的 STYLE-01 与短篇扩写；扩写遵循既有"落地检查清单+术语速查+常见反模式"模式，不虚构实验证据、不改变 L2 成熟度口径；门禁要求本次修改正文必须带成熟度行，故 12 目录被改文件同步补标。

### Confidence

0.9

### Status

Accepted（已执行）

## KD-008

### Subject

执行 P2 批（NAME-02 / NAV-03，2026-08-13）。

### Options

1. 延后全部 P2 项，仅保留 STYLE-01 待用户确认。
2. 执行不涉及 H1 模板统一与用户确认的 P2 项：NAME-02（03 目录 07/08 H1 补编号、06 README 文件列表排序）与 NAV-03（07/02/11 README 性能主题互链）。

### Decision

Option 2，已执行：03-业务系统设计 07/08 两篇 H1 补编号前缀；06-世界模拟与运行时 README 文件列表按编号排序；07/02/11 三个 README 增加"性能主题跨分类导航"互链。

### Reason

NAME-02 属编号/排序一致性修复，无内容风险；NAV-03 为跨分类导航增量（11 原本已互链 02/07，本次补齐 07/02 两个方向）。TEMPLATE-01 与 STYLE-01 同属 12 目录 H1 模板统一，涉及 20+ 文件且需用户确认，保持延后。

### Confidence

0.9

### Status

Accepted（已执行）

## KD-004

### Subject

DEDUP-01：39-47 附录跨篇重复是否去重。

### Options

1. 改为"单篇收录 + 他篇链接"，消除 16 个文件的跨篇重复粘贴。
2. 维持每篇完整源码附录，接受重复成本。

### Decision

Option 2（维持现状）。

### Reason

用户 2026-08-13 明确要求"每篇都出现完整源码，而不是简单的指向源码在哪"；去重方案与既有要求直接冲突，未获用户新指令前不改变该决定。跨篇重复作为已知成本在 audit.md G1 记录。

### Confidence

0.9

### Status

Accepted

## KD-005

### Subject

RENAME-01：46 文件名与 H1 不一致。

### Options

1. 改文件名（同步全部引用）。
2. 改 H1 标题迁就文件名。
3. 维持。

### Decision

Option 1：`46-Lyra-AI队伍与调试源码.md` → `46-Lyra-AI机器人与队伍源码.md`，与 H1"AI 机器人与队伍系统"及系列命名风格对齐；13 处引用已同步。

### Reason

文件名是长期锚点，标题应服从文件名；"调试"主题实际在 47，文件名含"调试"会造成检索误导。

### Confidence

0.85

### Status

Accepted（已执行）

## KD-006

### Subject

SPLIT-01/02/03 与 STYLE-01 是否本轮执行。

### Options

1. 本轮直接拆分 47、算法 04-01、服务端 04-01，并统一 12 目录 H1 模板。
2. 延后：先记录理由，纳入独立计划分批执行。

### Decision

Option 2（延后）。

### Reason

三项拆分涉及 2 千行级正文的内容迁移与全库链接影响面，无独立迁移计划时执行会破坏正文与导航；H1 模板统一（置信度 0.6）属低优先风格项。SPLIT-03 挂靠方案 W7-10.1 既有 TODO，恢复时从 review-queue 取回。

### Confidence

0.8

### Status

Accepted

## KD-003

### Subject

首轮审计 P0/P1 修复执行（2026-08-13）。

### Options

1. 只完成 P0 导航/元数据修复，P1 互链另行安排。
2. P0 与 P1（15 组互链）+ CLEAN-01 一并执行。

### Decision

Option 2：主线程串行完成 P0 四组、P1 LINK-01~15、CLEAN-01；需要用户决策的项（附录去重、拆分、改名、标题模板）不执行，保留在 review-queue。

### Reason

互链与清理为只读性增量编辑、无结构性风险；去重/拆分/改名与既有用户要求或系列导航冲突，需逐项确认。

### Confidence

0.9

### Status

Accepted

## KD-002

### Subject

首轮知识库审计（2026-08-13）结论与执行方式。

### Options

1. 只输出报告，不做任何文件修改。
2. 报告 + 控制面登记（audit.md / review-queue / plans），P0/P1 修复待用户逐批批准后执行。

### Decision

Option 2：写入 .kb/audit.md 与 plans/current.md；低置信度或与既有要求冲突的项（附录去重、拆分、改名、标题模板）进入 review-queue；P0 导航/元数据修复与 P1 互链按用户批准批次执行，主线程串行写。

### Reason

AGENTS.md 要求"Never begin by moving files"；审计阶段只读；结构性操作须 auditor 通过并获用户批准。

### Confidence

0.9

### Status

Accepted

## KD-024

### Subject

知识库与 Agent 整体架构重构：责任分层、规则单一事实源和可执行验收（2026-09-07）。

### Options

1. 将现有中文领域整体搬入 Knowledge，重新建立目录与导航。
2. 保留正文路径，重构控制面与 Agent 契约，统一新材料落点，增加架构一致性检查。
3. 只写设计建议，保持现有角色冲突与别名环不变。

### Decision

采用 Option 2。用户已明确授权本轮重构，内部计划和评审不构成新的用户批准请求。

- architecture.json 独占顶层路径的责任层登记；taxonomy 保留兼容 domains 注册并负责领域语义。
- 现有六个领域是通用知识的主要 Canonical；Knowledge 仅承接经决策确认的领域外扩展。来源集合允许保留书籍章节与日期组织。
- 根 AGENTS 保留最小契约并按任务加载细则；知识组织、协作发布分别只有一份详细规则。项目 Skill 负责路由，角色配置负责单一职责和默认权限。
- 保留五个分析角色，补执行者、整合者和验证者；共享文件单写者，不相交执行范围可以并行。模型和推理强度继承当次运行时选择。
- 修复 Inventory/背包系统与 Determinism/确定性的循环，并把行为树和 PCG 的别名收敛为单跳标准词。
- 增加目录覆盖、入口、别名与 manifest 检查及负向自测；修复 manifest 生成器的 safe.directory 使用与跨宿主确定性排序。

### Reason

审计发现 AGENTS 的 universal 落点与现行生命周期规则不一致；协作参考把写入和发布全归主线程，与角色矩阵冲突；角色文件固定模型会覆盖任务选择；别名环会阻止递归归一化终止。这些问题可以直接通过结构契约与校验解决，无须迁移数百篇知识正文。

### Confidence

0.95。路径、权限和别名问题有实际文件证据；配置静态检查不代表当前宿主已经加载新角色。

### Affected files

精确改动与职责见 [.kb/plans/current.md](plans/current.md) 的 File Operations；整体责任与验收见 [知识库架构](../references/知识库架构.md)。

### Consequences

本决策替代根 AGENTS 和旧参考中的“通用知识一律进入 Knowledge”“所有写入必须由主线程完成”等冲突表述；历史记录不回写。原有 dirty README、references/仓库结构.md 和读书笔记保持不变，现行合同通过 index、Global MOC、.kb 和 AGENTS 接入。

本轮重构不自动修复既有 29 项内容质量失败、不提升成熟度、不修改个人 Obsidian 状态；各门禁的本轮结果以 current plan 为准。没有提交或推送。

### Status

Accepted（用户授权的架构方案；实施与验收状态见当前计划）。

## KD-025

### Subject

统一知识扫描范围，隔离 Git 忽略的未跟踪工具缓存（2026-09-08）。

### Options

1. 保持检查器递归扫描磁盘、manifest 扫描 Git 的不同口径。
2. 让检查与清单共同使用 Git 可见且实际存在的 Markdown 集合。

### Decision

采用 Option 2。共享脚本 [get_kb_markdown.ps1](../scripts/get_kb_markdown.ps1) 返回已跟踪文件及未忽略的新文件；删除但未提交的文件跳过，Git 错误终止，原有两个路径排除保留。check_repo 的目录导航与 OKF 的检查范围使用同一集合；OKF 操作性排除保持原义。

### Reason

并行阅读任务新增了翻译缓存与本地依赖，已在用户 .gitignore 中排除；旧检查器仍扫描它们，造成工具文件被误当知识正文，且与 manifest 范围不一致。统一选择源解决职责错误，不修改来源内容和质量判断规则。

### Confidence

0.99。测试明确证明未忽略的新 Markdown 受检、已跟踪且匹配 ignore 的 Markdown 仍受检、忽略缓存排除、Git 失效报错。

### Affected files

scripts/get_kb_markdown.ps1、scripts/test_kb_scope.ps1、scripts/check_repo.ps1、scripts/check_okf.ps1、scripts/rebuild_manifest.ps1、scripts/check_architecture.ps1，以及架构说明、导航、manifest 和当前计划。

### Status

Accepted。范围回归双环境通过，内容规则保持原样；遗留正文与外部导入问题按当前计划分别报告。

## KD-026

### Subject

知识库结构门禁修复与来源层（sources）成熟度豁免：读书笔记不再计入知识成熟度门禁，同时修复围栏、清单与核心正文元数据缺陷（2026-09-10）。

### Options

1. 为 173 篇来源材料逐篇补标 `知识成熟度` 行，使其与 Canonical 正文同口径。
2. 按 architecture.json 既有的 sources 分层，在 `check_repo.ps1` 中豁免 `读书笔记\*`；同时只修复客观缺陷（未闭合围栏、README 缺链、乱码命名、清单漂移、核心正文元数据/证据缺口）。

### Decision

Option 2，已执行：

- `scripts/check_repo.ps1` 的知识成熟度门禁在既有豁免（`工作日志/`、`笔记/`、`方案/`）之外增加 `读书笔记/`；`.kb/taxonomy.yaml` 与 `references/知识库架构.md` 同步登记该边界。
- 修复 42 处未闭合代码围栏（27 处为误置开栏，按语义删除；15 处为代码块/ASCII 图表未闭合，按原文语义补闭合），未改动正文文字。
- 修复 `读书笔记/GameAIPro/卷1`、`卷4` 的第 05 章 README 行（改为本地链接），并把 2 个含乱码 `â` 的来源文件名与对应标题归一为标准名称（`—`/`–`）。
- 核心正文 11 项：为 `游戏AI/02-07`、`游戏AI/03-03`、`系统实战/02`、`游戏知识/04-08` 补齐知识基线/版本基准/最后更新/来源链接/验证入口；`服务端/06-03` 补证据状态说明（保留 L3，证据为文档内可运行示例）；`服务端/06-04` 因示例为伪代码而下调 L2，并同步 `游戏服务端/06-世界模拟与运行时/README.md`。
- 机械重建 `.kb/manifest.yaml`（585 篇），并把上一轮计划快照归档为 `.kb/plans/2026-09-08-architecture-refactor.md`。

### Reason

来源材料不是 Canonical 正文，其质量应按来源完整性与可追溯性衡量；对书籍精读逐篇补证据深度标注既缺证据支持，也会制造虚假质量信号。围栏与清单缺陷是客观渲染/维护缺陷，必须修复而不能靠豁免掩盖。核心正文的元数据与证据缺口属于既有欠账，其中 `06-04` 的自述（“示例伪代码”）与 L3 标注自相矛盾，按 DoD 下调 L2 比补一段文字维持 L3 更诚实。

### Confidence

0.93。缺陷判定有文件证据；来源层豁免与既有 architecture.json 分层一致。宿主无法调用 git，PS 门禁未能在本机实跑，验收以等价只读复现记录，未宣称脚本已执行。

### Affected files

`scripts/check_repo.ps1`、`.kb/taxonomy.yaml`、`references/知识库架构.md`、`.kb/manifest.yaml`、`.kb/plans/current.md`、`.kb/plans/2026-09-08-architecture-refactor.md`、`.kb/review-queue.md`、`.kb/audit.md`、`learning/log.md`、`log.md`、42 篇来源材料、2 个来源文件名、`读书笔记/GameAIPro/卷1-GameAIPro1/README.md`、`读书笔记/GameAIPro/卷4-OnlineEdition2021/README.md`、`游戏AI/02-移动学习与服务端/07-战斗AI编排与战术协同.md`、`游戏AI/03-评测与安全/03-LLM-NPC安全.md`、`系统实战/02-角色移动完整链路.md`、`游戏知识/04-动画系统/08-动作战斗系统与打击手感.md`、`游戏服务端/06-世界模拟与运行时/03-Entity生命周期与组件模型.md`、`游戏服务端/06-世界模拟与运行时/04-Scene-Map-Zone与实例管理.md`、`游戏服务端/06-世界模拟与运行时/README.md`。

### Status

Accepted（2026-09-10；已执行并通过等价复现校验；未提交、未推送）。

## KD-027

### Subject

Gameplay 工程师主线强化：新增背包道具完整链路与本机可运行证据，并把 `.workbuddy/` 从知识扫描范围排除（2026-09-11）。

### Options

1. 继续只补 Gameplay 横向知识点，不产出可运行证据，链路停在 L3。
2. 用本机可用的 MinGW g++ 产出**真实可编译运行**的 Gameplay 核心机制证据（背包事务 / Buff 冲突 / 技能管线 / 属性聚合），据此落地 `系统实战/05-背包道具完整链路` 并把它做到 L4；同时修复工具目录污染扫描范围的问题。

### Decision

Option 2，已执行：

- 新建 `evidence/tests/gameplay-core/`（`src/` 四个 C++17 程序 + `scripts/run_all.sh` 与 `scripts/build_run.ps1` + 未修改的 `results/*.txt` + 统一格式 `README.md`）；4 个程序共 29 条断言全部通过（inventory 7、buff 12、skill 10、属性一致性 400 抽样 mismatch=0），并产出 P50/P95/P99 与吞吐原始数据。
- 新建 `系统实战/05-背包道具完整链路.md`（L4）：15 步 Gameplay Transaction 链路 + 权威状态与数据所有权 + 失败矩阵 + 验证矩阵 + 本地证据与未验证边界；同步 `系统实战/README`、根 `README`、`references/仓库结构.md`。
- `系统实战/03-技能释放`、`04-Buff系统` 新增本机证据小节（覆盖逻辑层，维持 L3，明确未验证边界）。
- 排除工具目录：`.workbuddy/` 加入 `.gitignore`、`architecture.json` 的 `excluded_roots` 与 `get_kb_markdown.ps1` 的排除规则；原因是上一轮写入的 Agent 记忆文件未被忽略，被当成知识正文（缺 README / 成熟度 / type），会污染 manifest 与门禁。
- 机械重建 `.kb/manifest.yaml`（589 篇）。

### Reason

上一轮的结论是"无可用 C++ 工具链，无法产出 DoD 要求的可运行 Evidence"。本轮复核发现该结论**不成立**：`C:\msys64\mingw64\bin\g++.exe`（MinGW-w64 g++ 16.1.0）在把自身目录加入 PATH 后可正常编译运行；宿主 PowerShell 无法调用原生进程，但 Bash 可以。因此"不可产出证据"的前提被推翻，不应继续以 L2 交付 Gameplay 内容。工具目录污染则是上一轮引入的真实回归，必须从扫描范围根除而不是靠豁免文件类型掩盖。

### Confidence

0.95。断言与基准输出可复现（`scripts/run_all.sh`）；工具链事实已实测。属性基准存在运行间波动（p50 1750–1760 µs、加速比 7.4–7.8x），已在证据 README 与正文中如实标注。

### Affected files

`evidence/tests/gameplay-core/**`、`evidence/README.md`、`系统实战/05-背包道具完整链路.md`、`系统实战/03-技能释放完整链路.md`、`系统实战/04-Buff系统完整链路.md`、`系统实战/README.md`、`README.md`、`references/仓库结构.md`、`.gitignore`、`.kb/architecture.json`、`scripts/get_kb_markdown.ps1`、`.kb/manifest.yaml`、`.kb/plans/current.md`、`.kb/audit.md`、`.kb/review-queue.md`、`learning/log.md`、`log.md`、`方案/知识体系完善执行方案.md`、`方案/知识体系门禁收敛报告-2026-09-10.md`。

### Status

Accepted（2026-09-11；已执行并通过等价复现校验；未提交、未推送）。

## KD-028

### Subject

伤害与属性结算链路落地与证据升级：新增 `系统实战/11-伤害与属性结算完整链路`（L4）与 `evidence/tests/damage-core`（2026-09-11）。

### Options

1. 把伤害结算继续留在 `03-技能释放完整链路` 的"步骤 7 Damage Pipeline"一节里，不单独成篇。
2. 单独成篇做深：固定乘区顺序与边界语义，配本机可运行的公式/边界断言与结算基准，并从 03 反向链接。

### Decision

Option 2，已执行：

- 新建 `evidence/tests/damage-core/`（`src/damage_pipeline.cpp` + `scripts/run_all.sh` + `scripts/build_run.ps1` + `results/damage_pipeline.txt` + 统一格式 README）；**15 条断言全部通过**（A1–A2 聚合语义、D1–D13 结算语义与边界），并给出单次结算 P50=14.2ns / P95=16.3ns / P99=16.8ns、吞吐 ≈7.72×10⁷ 次/秒，以及同种子重放哈希一致的确定性证据。
- 新建 `系统实战/11-伤害与属性结算完整链路.md`（L4，332 行）：15 步结算闭环、权威状态与快照规则、五乘区公式、护盾/过量/上限/拦截语义、分步演算表、与策划表的对账方法、反模式、验证矩阵、术语速查。
- 边界分工写死：03 负责"技能能否触发与执行"，11 负责"结算出什么数字"；03 的关联阅读与 06.4 证据小节互相指向。
- 同步 `系统实战/README`（规划链路 10→11 条、状态表、文件列表）、根 `README`、`references/仓库结构.md`、`evidence/README.md`、`方案/` W4 状态。

### Reason

伤害结算是四方对数字（策划表 / 客户端表现 / 服务端结果 / 战斗日志）的唯一交汇点，其事故多来自顺序约定而非逻辑错误；这类知识必须在代码里固定顺序、在文档里固定语义、在证据里固定断言。实测结算本身仅约 14 ns/次，说明优化重点在快照、范围查询与日志广播——这一结论只有在有基准的前提下才能给出。

### Confidence

0.94。断言与基准可复现；护甲曲线与乘区顺序属可辩护的工程选择，已声明需按项目策划表重新标定。

### Affected files

`evidence/tests/damage-core/**`、`evidence/README.md`、`系统实战/11-伤害与属性结算完整链路.md`、`系统实战/03-技能释放完整链路.md`、`系统实战/README.md`、`README.md`、`references/仓库结构.md`、`.kb/manifest.yaml`、`.kb/plans/current.md`、`.kb/audit.md`、`.kb/review-queue.md`、`learning/log.md`、`log.md`、`方案/知识体系完善执行方案.md`。

### Status

Accepted（2026-09-11；已执行并通过等价复现校验；未提交、未推送）。

## KD-029

### Subject

性能问题定位链路落地与插桩证据升级：新增 `系统实战/10-性能问题定位完整链路`（L4）与 `evidence/labs/profiling`（2026-09-11）。

### Options

1. 把性能定位留在"笔记/插桩测试"与"perf 性能分析"速查层面，不建纵向链路。
2. 建纵向链路：量化常见插桩样式开销、给出帧预算换算、固定卡顿检测与降级状态机规则，并用本机证据固化。

### Decision

Option 2，已执行：

- 新建 `evidence/labs/profiling/`（`profiling_overhead` + `hitch_and_budget` + bash/PS 构建脚本 + 原始输出 + 统一格式 README）；**17 条断言全部通过**。
- 关键数据：裸计数 0.33 ns、原子计数 4.01 ns、作用域计时 50.03 ns、格式化日志 274.25 ns、格式化+缓冲写 309.90 ns、1/100 采样计 0.96 ns；换算到 100 000 次/帧热路径：计时 5.003 ms（30.14% 预算）、格式化日志 27.467 ms（165.46%）、采样 0.096 ms（0.58%）。
- 卡顿检测规则固定为 `max(2.5 × 中位数, 1.5 帧)` + 迟滞（连续 2 帧低于阈值 60% 才结束），实测 12 次注入卡顿（含级联）判定为 12 个事件、18/18 帧召回、纯抖动 0 误报；**弃用 p99 基准**，因为 p99 会被卡顿污染导致阈值自抬漏检。
- 降级状态机：连续 3 次超预算逐级 +1（上限 3），连续 30 次达标逐级 −1；交替"超/不超"输入下级别变化 0 次（不抖动）；预算记账开销 1.7 ns/Tick、每系统 0.4 ns。
- 新建 `系统实战/10-性能问题定位完整链路.md`（**L4**，329 行）：15 步定位闭环、四类性能问题分型、调度权归属、一次定位推演模板、预算与告警配置模板、反模式与验证矩阵。
- 同步 `系统实战/README`（10 号链路规划→已完成，共 7 条落地）、根 `README`、`references/仓库结构.md`、`evidence/README.md`、`方案/` W4 状态；`05`/`11` 增加指向本篇的度量口径链接。

### Reason

性能定位是 Gameplay 工程师的日常动作，此前仓库只有方法速查（笔记）与工具介绍（横向文档），缺少"症状到结论"的纵向链路；而"热路径别打日志""每实体加计时器"这类经验长期停留在口号层面，缺少量级依据。本机实测把这个量级固定下来（计数 0.33 ns ↔ 日志 309.9 ns，相差约 940 倍），才能让预算讨论变成可计算的决策。

### Confidence

0.95。断言的因果与状态机行为可复现；绝对耗时随编译器与架构变化，已在证据与正文中要求以**比例与量级**而非绝对值外推。

### Affected files

`evidence/labs/profiling/**`、`evidence/README.md`、`系统实战/10-性能问题定位完整链路.md`、`系统实战/05-背包道具完整链路.md`、`系统实战/11-伤害与属性结算完整链路.md`、`系统实战/README.md`、`README.md`、`references/仓库结构.md`、`.kb/manifest.yaml`、`.kb/plans/current.md`、`.kb/audit.md`、`.kb/review-queue.md`、`learning/log.md`、`log.md`、`方案/知识体系完善执行方案.md`。

### Status

Accepted（2026-09-11；已执行并通过等价复现校验；未提交、未推送）。

---

## KD-030

### Subject

角色进入游戏链路落地与可运行证据：新增 `系统实战/01-角色进入游戏完整链路`（L4）与 `evidence/tests/entry-core`（2026-09-11）。

### Options

1. 把进入游戏留给横向文档（鉴权 / DS 会话注册 / Ownership 各自成篇），不建纵向链路。
2. 建纵向链路：把"启动→票据→幂等→分配→旅行→二次鉴权→Spawn→追赶→就绪→回滚"串成闭环，并把票据密码学、状态机幂等回滚、租约栅栏、JIP 追赶四类机制用本机可运行证据固化。

### Decision

Option 2，已执行：

- 新建 `evidence/tests/entry-core/`（`entry_ticket` / `entry_session` / `ds_allocator` / `jip_resync` + bash/PS 构建脚本 + 原始输出 + 统一格式 README）；**83 条断言全部通过**（24 + 27 + 20 + 12）。
- `entry_ticket`：自实现 SHA-256 与 HMAC-SHA256，对 **FIPS 180-4 与 RFC 4231 标准向量**逐条校验（空串、`abc`、56 字节填充边界、分段流式输入、>64 字节密钥先哈希、TC1/TC2/TC3/TC6）；票据五步验签（格式→常量时间验签→时间窗→归属→一次性消费）覆盖篡改/换钥/过期/时钟漂移/跨服/重放。
- `entry_session`：进入状态机（Auth→Allocate→Travel→LoadCharacter→Spawn→Ready）；**双层幂等**（同 requestId 命中幂等表；换 requestId 时按玩家维度复用同一 `ds-1`，负载保持 1）；超时重试、拒绝不重试、Spawn 期失败仍回滚、`no_capacity` 不超卖、Ready 后取消为 no-op。
- `ds_allocator`：租约 + 宽限期 + **栅栏令牌**；令牌严格递增永不复用、旧令牌提交/续约被拒、玩家回归后旧令牌永久失效、令牌按 DS 作用域（ds-1 令牌不授权 ds-2）；不变量"总负载 == 活跃租约数"在混合序列后仍成立。
- `jip_resync`：**乱序包必须重排缓冲而非丢弃**（缺口未补齐前不推进版本，补齐后连续落地并收敛到与权威一致的指纹）；日志窗口不足回退全量快照；基准给出增量 vs 全量的取舍——**追赶 = 快照拷贝 + 重放，CPU 上永不比全量便宜**；200 000 实体 / 20 000 增量时带宽省 10 倍、CPU 多付 163.7 µs（约 +8%）。
- 新建 `系统实战/01-角色进入游戏完整链路.md`（**L4**，15 步闭环 + 失败矩阵 + 反模式 + 检查清单 + 术语速查）。
- **发现并修复一处真实缺陷**：分配失败时只置 `state = Failed` 而未中断步骤循环，链路带着空 DS 继续走完 Travel/Load/Spawn，产出"状态 Ready 但从未连上服务器"的会话；断言 E18/E19 抓出，修复为失败后立即终止。另一处同类问题是 JIP 乱序包直接丢弃会导致静默状态不一致，改为重排缓冲。

### Reason

进入游戏是唯一一条"玩家还没开始玩、状态已经写进线上系统"的链路，它会同时踩到密码学、幂等、分布式租约与状态同步四类问题，且失败表现极具误导性（卡加载 / 反复连接中 / 世界为空 / 莫名被踢 / 名额耗尽）。此前仓库中这些机制分散在四篇横向文档里，缺少一条把它们按真实时序串起来的链路，也缺少可判定的证据；"验签要常量时间""失败要回滚""乱序要缓冲"这些要求长期停留在口号层面。本机证据把它们变成了可复现的断言与数字。

### Confidence

0.9。断言与不变量可复现；扣分项是本机模型为单线程、密码学实现未经侧信道审计、分配器未接真实平台，均已在证据 README 与正文"局限 / 事实边界"中显式声明。

### Affected files

`evidence/tests/entry-core/**`、`evidence/README.md`、`系统实战/01-角色进入游戏完整链路.md`、`系统实战/README.md`、`README.md`、`references/仓库结构.md`、`.kb/manifest.yaml`、`.kb/plans/current.md`、`.kb/audit.md`、`.kb/review-queue.md`、`learning/log.md`、`log.md`、`方案/知识体系完善执行方案.md`。

### Status

Accepted（2026-09-11；已执行并通过等价复现校验；未提交、未推送）。
