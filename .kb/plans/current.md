---
type: Plan
title: "知识库门禁修复与来源层收敛"
description: "修复全库结构门禁失败，将读书笔记登记为来源层，重建清单并同步控制面。"
tags:
  - plan
  - knowledge-base
  - okf
status: stable
verified: []
maturity: L2
updated: 2026-09-11
---

# 当前执行计划

> 知识成熟度：L2（依据仓库文件审计与等价复现的门禁规则；宿主运行验收单独记录）。

## Goal

在保持正文路径与知识内容不变的前提下，把全库结构门禁收敛到健康状态：修复可客观判定的缺陷（清单漂移、未闭合围栏、README 缺链、元数据/证据边界缺口），并把 `读书笔记/` 按既有架构登记为 sources 层，使其不再触发知识成熟度门禁。

## Scope

用户授权的仓库维护（“完善整个知识体系”）。无提交、推送、永久删除授权。物理改动限于：2 个含乱码的来源文件名规范化、42 处围栏修复、3 篇核心正文的元数据/证据边界补充、1 篇正文的成熟度纠正（L3→L2）、门禁豁免与 taxonomy 登记。

## Current State

- 仓库：Git 根；分支 main；HEAD `a1e3c29`。
- 本轮开始时工作树干净；上轮计划原文见 [2026-09-08-architecture-refactor.md](2026-09-08-architecture-refactor.md)。
- 宿主限制：本机 PowerShell 宿主无法调用 `git`，仓库自带 PS 门禁脚本无法直接执行；本轮用等价只读复现逐条核对，结论与 `.kb/plans/current.md`（2026-09-08）记录的快照一致（219 FAIL / 28 WARN）。

## Findings

1. 结构门禁：42 处未闭合代码围栏、2 处来源 README 缺链、2 个含乱码（`â`）的来源文件名。
2. 成熟度门禁：164 篇来源材料缺成熟度行；架构已把 `读书笔记/` 归入 sources 层，但 `check_repo.ps1` 未按该分层豁免。
3. 核心正文 11 项：游戏AI `07-战斗AI编排与战术协同`（缺最后更新/外部来源/验证入口）、`03-LLM-NPC安全`（缺知识基线/最后更新/外部来源）、系统实战 `02-角色移动完整链路`（缺最后更新与证据边界）、服务端 `06-03`/`06-04`（标 L3 但缺证据表述；`06-04` 示例为伪代码）、游戏知识 `04-动画系统/08`（缺版本基准/最后更新）。
4. 控制面：commit `a1e3c29` 新增工作日志后未重建 manifest，`工作日志/README.md` 指标过期且新文件未入索引（`check_architecture.ps1` 唯一 FAIL）。

## File Operations

| Action | Exact scope | Owner | Confidence |
| --- | --- | --- | --- |
| Fix | 42 篇来源材料未闭合围栏（27 处删除误置开栏、15 处按代码/图表语义补闭合） | 主线程 | 0.9 |
| Rename | 2 个乱码来源文件名 + 正文标题/描述中的 `â` 归一 | 主线程 | 0.95 |
| Fix | `读书笔记/GameAIPro/卷1-GameAIPro1/README.md`、`卷4-OnlineEdition2021/README.md` 第 05 章行改为本地链接 | 主线程 | 0.95 |
| Edit | 游戏AI 02-07、03-03；系统实战 02；游戏知识 04-08；服务端 06-03、06-04 与 06 README | 主线程 | 0.9 |
| Refactor | `scripts/check_repo.ps1` 成熟度门禁增加 `读书笔记\*` 豁免 | 主线程 | 0.95 |
| Update | `.kb/taxonomy.yaml`、`references/知识库架构.md` 登记来源层边界 | 主线程 | 0.95 |
| Rebuild | `.kb/manifest.yaml`（完成后机械重建） | 主线程 | 1.0 |
| Update | `.kb/decisions.md`、`.kb/review-queue.md`、`.kb/audit.md`、`.kb/plans/*`、`learning/log.md`、`log.md` | 主线程 | 0.95 |

未列出的路径不改动；正文的路径、H1、正文段落与链接关系保持不变。

## Merge Plan / Split Plan

无正文合并或拆分。仅收紧 `06-04` 的成熟度标注（与其自述“示例伪代码”一致），并在 3 篇正文补充证据边界声明。

## Needs Review

- 来源层豁免是否应同时覆盖围栏与清单检查：本轮结论为否（结构缺陷仍必须修复）。
- `06-04` 降为 L2 与本轮新增证据边界声明的措辞是否符合仓库口径。
- 宿主无法执行 PS 门禁，最终验收以等价复现结果记录，不宣称脚本已在宿主运行。

## Risks

- 来源材料为外部导入内容，围栏修复按“删除误置开栏 / 按代码语义补闭合”分类处理，未改动正文文字；如需回退可按 Git diff 逐文件还原。
- 本轮未提交、未推送；不接管用户其他未提交改动。

## Execution Progress

- [x] 只读审计与失败项分类
- [x] 来源层豁免登记与门禁调整
- [x] 结构缺陷修复（围栏、缺链、乱码命名）
- [x] 核心正文元数据与证据边界补正
- [x] manifest 重建与控制面同步
- [ ] 宿主内 PS 门禁实跑（宿主无法调用 git，未完成）

## Final Validation

2026-09-10 等价复现结果（`check_repo` / `check_okf` / `check_architecture` 规则）：

| 检查 | 结果 |
| --- | --- |
| 扫描范围 | 586 篇 Markdown（Git 可见且未被忽略） |
| 断链 / BOM / U+FFFD / 目录缺 README | 0 / 0 / 0 / 0 |
| 未闭合代码围栏 | 0（修复前 42） |
| README 清单缺链 | 0（修复前 2） |
| 成熟度门禁（豁免来源层后） | 缺成熟度 0；L3+/L4+/L5 证据缺口 0 |
| 领域质量门禁（AI/服务端/算法/00/质量/实战） | 0 |
| 游戏知识语义门禁 | 0 |
| manifest 漂移（路径/bytes/lines） | 0；585 条目与磁盘一致 |
| 成熟度分布（正文） | L2=304、L3=7、L4=6、L5=1 |

真实 PS 门禁脚本未能在宿主执行（无法调用 git）；上表为等价只读复现，未把该结果表述为脚本已运行。

---

# 追加阶段：Gameplay 主线强化（2026-09-11）

## Goal

按用户指令"重点强化完善 Gameplay 工程师需要的部分"：把 Gameplay 工程师最依赖但此前"只有结论没有证据"的四类机制做成**可编译运行**的测试与基准，并据此把缺失的背包道具链路补成有证据的纵向链路。

## 前置结论修正

上一阶段记录"本机无可用 C++ 工具链，无法产出 DoD 要求的可运行 Evidence"。本阶段复核后**撤销该结论**：

- `C:\msys64\mingw64\bin\g++.exe` 为 MinGW-w64 g++ 16.1.0，把该目录加入 PATH 后可正常编译运行；
- 先前失败的真实原因是 MinGW g++ 找不到 `cc1plus`/运行库，且宿主 PowerShell 不能派生原生进程；Bash 可以。

## Scope

| Action | Exact scope | Owner | Confidence |
| --- | --- | --- | --- |
| Create | `evidence/tests/gameplay-core/`（src×4、scripts×2、results×4、README） | 主线程 | 0.95 |
| Create | `系统实战/05-背包道具完整链路.md`（L4） | 主线程 | 0.9 |
| Edit | `系统实战/03`、`04` 增加本机证据小节；`系统实战/README`、根 `README`、`references/仓库结构.md` 同步 | 主线程 | 0.95 |
| Fix | `.workbuddy/` 排除出知识扫描范围（`.gitignore` + `architecture.json` + `get_kb_markdown.ps1`） | 主线程 | 0.95 |
| Rebuild | `.kb/manifest.yaml`（589 篇） | 主线程 | 1.0 |
| Update | `.kb/decisions.md`(KD-027)、`.kb/audit.md`、`.kb/review-queue.md`、`learning/log.md`、`log.md`、`方案/*` | 主线程 | 0.95 |

## Verification

- 断言：inventory 7/7、buff 12/12、skill 10/10、属性一致性 400 抽样 mismatch=0。
- 指标：背包 P50=100ns/P95=200ns/P99=200ns、≈9.76e6 ops/s；属性聚合 P50 加速 7.47x（全量 1756.0µs → 增量 234.9µs）。
- 门禁（等价复现，589 篇）：全部 0，成熟度分布 L2=304 / L3=7 / L4=7 / L5=1。
- 未验证边界：多线程竞争、持久化、网络复制、DS 容量、UE GAS 运行时集成。

## Remaining

按执行方案 W1/W2/W4 继续：W2 的 SpatialQuery / 动态分线 / 大规模战斗，W4 的 DS 上线完整链路，以及把 W1 基础层 8 篇按"正文 + `evidence/labs/*`"成对交付（工具链已可用，Evidence 不再受阻塞）。

---

# 追加阶段二：伤害与属性结算（2026-09-11）

## Goal

把伤害结算从"技能链路的一个步骤"提升为独立链路并配可运行证据，延续"正文 + 证据"成对交付。

## Scope

| Action | Exact scope | Owner | Confidence |
| --- | --- | --- | --- |
| Create | `evidence/tests/damage-core/`（src、scripts×2、results、README） | 主线程 | 0.94 |
| Create | `系统实战/11-伤害与属性结算完整链路.md`（L4，332 行） | 主线程 | 0.9 |
| Edit | `系统实战/03` 反向链接与分工声明；`系统实战/README`（10→11 条）、根 `README`、`references/仓库结构.md`、`evidence/README.md`、`方案/*` | 主线程 | 0.95 |
| Rebuild | `.kb/manifest.yaml`（591 篇） | 主线程 | 1.0 |
| Update | `.kb/decisions.md`(KD-028)、`.kb/audit.md`、`.kb/review-queue.md`、`learning/log.md`、`log.md` | 主线程 | 0.95 |

## Verification

- 断言：`damage_pipeline` pass=15 fail=0。
- 指标：单次结算 P50=14.2ns / P95=16.3ns / P99=16.8ns，吞吐 ≈7.72×10⁷ 次/秒；同种子重放哈希一致。
- 门禁（等价复现，591 篇）：全部 0，成熟度分布 L2=304 / L3=7 / **L4=8** / L5=1。
- 未验证边界：UE GAS 集成、并发结算、跨服重放、线上容量、项目策划公式标定。

## Remaining

同上一阶段的 Remaining：W2 三篇（SpatialQuery / 动态分线 / 大规模战斗）、W4 的 DS 上线完整链路、W1 基础层 8 篇；以及 `系统实战` 当时还剩的 01 进入游戏 / 06 匹配到对局 / 08 AOI 与大规模场景 / 10 性能问题定位 四条规划链路（其中 10 已在阶段三完成、01 已在阶段四完成）。

---

# 追加阶段三：性能问题定位（2026-09-11）

## Goal

把性能定位从方法速查提升为纵向链路，并量化插桩样式开销，使"热路径别打日志"这类经验变成可计算的预算决策。

## Scope

| Action | Exact scope | Owner | Confidence |
| --- | --- | --- | --- |
| Create | `evidence/labs/profiling/`（src×2、scripts×2、results×2、README） | 主线程 | 0.95 |
| Create | `系统实战/10-性能问题定位完整链路.md`（L4，329 行） | 主线程 | 0.9 |
| Edit | `系统实战/05`、`11` 增加度量口径链接；`系统实战/README`（10 号转已完成）、根 `README`、`references/仓库结构.md`、`evidence/README.md`、`方案/*` | 主线程 | 0.95 |
| Rebuild | `.kb/manifest.yaml`（593 篇） | 主线程 | 1.0 |
| Update | `.kb/decisions.md`(KD-029)、`.kb/audit.md`、`.kb/review-queue.md`、`learning/log.md`、`log.md` | 主线程 | 0.95 |

## Verification

- 断言：`profiling_overhead` 7/7、`hitch_and_budget` 10/10。
- 指标：0.33 ns 计数 ↔ 309.9 ns 日志（≈940 倍）；热路径 100 000 次/帧下日志占预算 185.82%、计时 30.14%、采样 0.58%；卡顿 12 事件 / 0 误报；降级无抖动；记账 1.7 ns/Tick。
- 门禁（等价复现，593 篇）：全部 0，成熟度分布 L2=304 / L3=7 / **L4=9** / L5=1。
- 未验证边界：UE Insights/Trace、Linux perf、GPU 侧、真机与线上分档。

## Remaining

`系统实战` 还剩 06 匹配到对局 / 08 AOI 与大规模场景 两条规划链路；W2 三篇、W4 DS 上线链路、W1 基础层 8 篇待推进。

---

# 追加阶段四：角色进入游戏链路（2026-09-11）

## Goal

把"启动 → 票据 → 幂等 → 分配 → 旅行 → 二次鉴权 → Spawn → 追赶 → 就绪 → 回滚"串成纵向闭环，并用本机可运行证据固化四类机制：票据密码学、状态机幂等与回滚、DS 租约与栅栏令牌、JIP 状态追赶。

## Scope

| Action | Exact scope | Owner | Confidence |
| --- | --- | --- | --- |
| Create | `evidence/tests/entry-core/`（src×4、scripts×2、results×4、README） | 主线程 | 0.95 |
| Create | `系统实战/01-角色进入游戏完整链路.md`（L4） | 主线程 | 0.9 |
| Edit | `系统实战/README`（01 转已完成，共 8 条落地）、根 `README`、`references/仓库结构.md`、`evidence/README.md`、`方案/知识体系完善执行方案.md`（W3/W4） | 主线程 | 0.95 |
| Rebuild | `.kb/manifest.yaml` | 主线程 | 1.0 |
| Update | `.kb/decisions.md`(KD-030)、`.kb/audit.md`、`.kb/review-queue.md`(W4-ENTRY-01)、`learning/log.md`、`log.md` | 主线程 | 0.95 |

## Verification

- 断言：`entry_ticket` 24/24、`entry_session` 27/27、`ds_allocator` 20/20、`jip_resync` 12/12（合计 83）。
- 密码学：SHA-256 对 FIPS 180-4（空串 / `abc` / 56 字节填充边界）、HMAC-SHA256 对 RFC 4231 TC1/TC2/TC3/TC6 逐字匹配。
- 基准：JIP 追赶 p50 6.510 / 117.960 / 2104.845 µs（2 000 / 20 000 / 200 000 实体），全量快照 p50 1.300 / 25.200 / 1941.100 µs；带宽节省 1.00× / 1.00× / 10.00×。
- 门禁（等价复现，596 篇）：全部 0，成熟度分布 L2=304 / L3=7 / **L4=10** / L5=1。
- 本轮修正：① 状态机失败未中断链路（真实缺陷）；② JIP 乱序包丢弃语义；③ 基准对有序日志多余排序（66.6µs → 6.5µs）。
- 未验证边界：真实网关/DB/DS 平台、并发与弱网、密码学侧信道。

## Remaining

`系统实战` 还剩 06 匹配到对局 / 08 AOI 与大规模场景 两条规划链路；W2 三篇、W4 DS 上线链路、W1 基础层 8 篇待推进。
