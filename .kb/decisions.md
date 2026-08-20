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
updated: 2026-08-20
---

# Knowledge Base Decision Log

> 知识成熟度：L2（决策记录，只增不改历史条目）。

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
