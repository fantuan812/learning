---
type: Index
title: "Learning"
description: "游戏全栈工程知识体系的人类入口与全局导航。"
tags:
  - game-development
  - knowledge-base
  - okf
status: stable
verified: []
maturity: L2
updated: 2026-08-20
---

# Learning

仓库：[fantuan812/learning](https://github.com/fantuan812/learning)。本地仓库根同时作为 OKF bundle 与 Obsidian vault，不依赖固定绝对路径。

## 四层知识视图：知识树 · 技能树 · 项目树 · Evidence

仓库按「知识树 + 技能树 + 项目树 + Evidence」四层组织：知识树回答"有什么主题"，技能树回答"掌握到什么程度"，项目树回答"能不能把一条业务链路完整跑通"，Evidence 回答"你凭什么证明它是对的"。

```text
                游戏全栈工程知识体系
                        │
      ┌─────────────────┴─────────────────┐
      │                                   │
  00 计算机与工程基础                  游戏工程（知识树）
      │                                   │
  C++ / OS / Linux / 网络          ┌───────┼───────┐
  并发 / CPU / 数据结构            │       │       │
                                  │   游戏知识   游戏服务端
                                  │   (UE客户端)   │
                                  │       │    06 世界模拟与运行时
                                  │       │       │
                                  │   游戏算法 ────┘
                                  │       │
                                  │    游戏AI
                                  │       │
                                  │   游戏测试与质量
                                  │       │
                                  └───┬───┘
                                      │
                                  系统实战（项目树）
                                      │
                      技能 / 移动 / 背包 / 匹配 / AI / AOI / DS / 性能
                                      │
                                  测试 · Benchmark · 工作复盘
```

### 知识树 —— 主题广度

- [00-计算机与工程基础/](00-计算机与工程基础/README.md) —— 全栈公共底座：C++、OS/Linux、体系结构、网络、数据库/分布式、安全、软件工程、容器与可观测性
- [游戏知识/](游戏知识/README.md) —— UE 客户端开发系统化知识库（13 大分类，含引擎源码分析纵深层）
- [游戏服务端/](游戏服务端/README.md) —— 游戏服务端知识库（架构网络 / 数据业务 / DS 平台化 / 世界模拟与运行时）
- [游戏算法/](游戏算法/README.md) —— 游戏算法知识库（寻路图论 / 数学碰撞 / 概率采样 / 工程技巧 / 确定性基准）
- [游戏AI/](游戏AI/README.md) —— 游戏 AI 知识库（决策架构 / 移动学习与服务端 / 评测与安全）
- [游戏测试与质量/](游戏测试与质量/README.md) —— 游戏测试与质量保障知识库（测试策略 / 自动化 / 压测 / CI 门禁）

### 多智能体知识库控制面

仓库根配置了 Codex 多智能体知识库控制面：主线程作为 Orchestrator 决策，kb_scanner / kb_analyzer / kb_architect / kb_curator / kb_auditor 默认只读取证；具体模型、推理档位与并发以当前运行时实际能力和用户选择为准，项目配置中的默认值不能作为当前可用性的证据。入口与状态：

- [AGENTS.md](AGENTS.md) —— 最高层行为规则；
- [.kb/](.kb/README.md) —— 控制平面状态（manifest / taxonomy / aliases / decisions / review-queue / plans）；
- [.agents/](.agents/README.md) —— 项目级 Skill（knowledge-base-organizer）；
- [00_Index/](00_Index/README.md) —— 全局导航索引（含 [MOC.md](00_Index/MOC.md)）；
- [Inbox/](Inbox/README.md) / [Archive/](Archive/README.md) —— 未处理材料入口 / 历史原材料；
- [Knowledge/](Knowledge/README.md) / [Projects/](Projects/README.md) —— 通用与项目知识的规范位置。

### OKF 与 Obsidian 互操作层

仓库采用 GoogleCloudPlatform/knowledge-catalog 发布的 Open Knowledge Format（OKF）v0.2 互操作层。2026-08-20 已完成本库 profile 范围迁移：390/390 篇受检文档通过 Strict，2 篇操作性文档按 profile 排除；这表示本库轻量门禁通过，不等同于官方通用 OKF parser 认证。未来导入 legacy 内容仍按 hybrid 流程分批审查；L0~L5、Canonical Location、taxonomy 与 manifest 继续承担原职责，不另建一套知识分类。

- [index.md](index.md) —— OKF/Obsidian 根入口；GitHub 入口仍是本 README；
- [OKF 兼容规范](references/OKF-兼容规范.md) —— `type`、来源、验证状态、时效与渐进迁移规则；
- [Obsidian 协作指南](references/Obsidian协作指南.md) —— 将仓库根打开为 vault、模板/Bases 与 Git/Sync 单写者边界；
- [Knowledge.base](00_Index/Knowledge.base) —— 通过 Obsidian Bases 查看已迁移条目的 Properties；
- [OKF 知识条目模板](references/templates/OKF-知识条目.md) —— 新条目默认 `status: draft / verified: [] / maturity: L0`；
- [OKF profile](.kb/okf-profile.yaml) / [Bundle log](log.md) —— 机器规则与只增不改的格式变更记录。

### 技能树 —— 掌握深度（成熟度 L0~L5）

每篇正文顶部标注 `知识成熟度`，区分"看过"与"真正做过"；标定规则见 [写作规范](references/写作规范.md)。

| 等级 | 含义 | 证据要求 |
| --- | --- | --- |
| L0 | 收集到主题 | 仅有主题占位或来源收集 |
| L1 | 能解释概念 | 正文可完整解释原理与边界 |
| L2 | 有源码/官方资料验证 | 带本机源码路径或官方链接，且已核对 |
| L3 | 有可运行 Demo | 可运行的最小实现或实验脚本 |
| L4 | 有 Benchmark / Test | 有测试用例或性能数据（P50/P95/P99、CPU、内存） |
| L5 | 有真实项目实践与复盘 | 可指向工作日志 / 线上问题复盘 |

### 项目树 —— 纵向链路

- [系统实战/](系统实战/README.md) —— 从客户端输入到服务器、数据库、测试与性能的完整链路（技能 / Buff / 假人AI 三条链路已落地）
  - 首批优先：技能系统、假人 AI + A*、Dedicated Server 上线

## 目录结构

- [方案/](方案/README.md) —— 仓库级建设方案、阶段目标与执行状态（以 [知识体系完善执行方案](方案/知识体系完善执行方案.md) 为基线）
- [工作日志/](工作日志/README.md) —— 按日期记录的工作日志
- [笔记/](笔记/README.md) —— 工作相关的知识点整理
  - `A星算法优化.md` —— A* 寻路算法优化（服务器 / 假人 AI）
  - `插桩测试.md` —— 耗时点测试方法一：插桩（Instrumentation）
  - `perf性能分析.md` —— 耗时点测试方法二：perf 采样分析
- [游戏知识/](游戏知识/README.md) —— UE 客户端开发系统化知识库（含 13 大分类）
- [游戏服务端/](游戏服务端/README.md) —— 游戏服务端知识库（架构网络 / 数据业务 / DS 平台化 / 世界模拟与运行时）
- [游戏算法/](游戏算法/README.md) —— 游戏算法知识库（寻路图论 / 数学碰撞 / 概率采样 / 工程技巧 / 确定性基准）
- [游戏AI/](游戏AI/README.md) —— 游戏 AI 知识库（决策架构 / 移动学习与服务端 / 评测与安全）
- [游戏测试与质量/](游戏测试与质量/README.md) —— 游戏测试与质量保障知识库（测试策略 / 自动化 / 压测 / CI 门禁）
- [evidence/](evidence/README.md) —— 实验与基准证据库（Demo / Test / Benchmark / 原始数据，供 L3~L5 文档引用）

> 六大知识域 + 系统实战共同构成**游戏全栈**知识体系：计算机基础（00-计算机与工程基础）+ 客户端（游戏知识）+ 服务端（游戏服务端）+ 通用算法（游戏算法）+ AI（游戏AI）+ 质量保障（游戏测试与质量）；纵向由 系统实战 贯通，横向由 evidence 提供证据支撑。
