---
type: Evidence
title: "Knowledge Base Audit Log"
description: "知识库历次结构、内容、证据和质量门禁审计记录。"
tags:
  - audit
  - knowledge-base
  - okf
status: stable
verified: []
maturity: L2
updated: 2026-09-14
---

# 知识库第四轮审计报告（2026-08-14，R4）

> 知识成熟度：L2（审计记录；基于本机文件实读、机械扫描与 4 个只读子代理并行语义验证）。
> 模式：audit（只读，主线程 + kb_analyzer/kb_architect/kb_curator 子代理语义验证）。第一至三轮报告见 git 历史；本文件为最新轮次。
> 基线：039a990（R3 整改落地），工作树干净；本轮未新增知识提交，属全量复核 + 深挖。
> 历史口径说明：本报告前半部分保留 2026-08-14 R4 基线；批次 1+3、批次 2 及 2026-08-17 收口复核的当前事实，以文末“维护后复核”章节为准。

## 结论

状态：**PASS_WITH_MINOR_WARNINGS**

基线健康（320 Markdown：正文 262 / README 58）：

- check_repo RESULT PASS / FAIL 0（12 WARN 全部为豁免类：.kb 控制面 / MOC / 路线图 / 笔记 / 工作日志 / SKILL）；
- manifest.yaml 与磁盘 320 文档完全同步（0 缺 0 多），抽查 8 条 bytes/lines 逐字节一致；
- 全库断链 0；%20 非代码链接 0（R3-STYLE-01 存活）；占位词 39 文件全部为合法语境（占位符概念 / 事实边界声明），无裸占位；重复 H2 0；多 H1 仅 4 个控制面文档（设计使然）；
- 成熟度分布与 R3 一致：L1=8、L2=230、L3=7、L4=6、L5=1，知识目录缺成熟度 0（仅豁免类）；
- 12 目录 01-48 编号连续、H1 模板统一（01-38 "UE 引擎源码分析 NN：主题"、39-48 "UE5.8 Lyra 源码解析 NN：主题"）；各域编号缺口（系统实战 1/2/5/6、服务端 06 2/6-10）均有 README 规划声明；
- R3 四项修复全部无回归（FACT-01 成熟度对齐、LINK-01 测试 06↔07、STYLE-01 %20 清理、STYLE-02 09 目录 H1）；
- 确定性、群体寻路、AI 评测、服务端 04（07↔08）、Lyra Travel（40↔44）互链双向存活；
- AOI、GAS/技能/Buff、确定性/回放、测试容量、DS 平台化主体等热点族的"分工声明 + 互链"收敛机制有效，无概念层重复；
- evidence 七实验目录 src/scripts/results 结构完整、无孤儿；git 工作树干净、diff --check 通过；
- KD-001~KD-010 决策记录齐全、六字段完整；MOC 链接全部有效。

存在两类新问题：互链缺口 3 处（其中 1 处 R3 健康项记录失真）与结构/控制面问题 5 项；另发现 1 组真语义重叠（背包族）进入 review-queue 待用户确认。

## P0 —— 正确性

无。

## P1 —— 互链与结构（建议直接修复）

| ID | 位置 | 问题 | 证据 | 置信度 |
| --- | --- | --- | --- | --- |
| R4-LINK-01 | 游戏知识/06-网络同步/05-ReplicationGraph兴趣管理.md 关联阅读（L491-505） | AOI 三角未闭合：该篇无任何指向 游戏算法/03-03-AOI与视野计算 或 游戏服务端/06-05-AOI与InterestManagement 的链接（grep "AOI/算法/服务端06/InterestManagement" 全无匹配），节点孤立；R3 健康项"AOI 三方互链均双向存活"与实不符 | 06-05 关联阅读无 AOI 链接；算法 03-03↔服务端 06-05 双向存活 | 0.9 |
| R4-LINK-02 | 游戏测试与质量/06-UE Dedicated Server联机验收与Gauntlet.md 关联阅读（L791-805） | 测试容量三角单侧缺口：06 未链 游戏服务端/05-平台化/05-DS自动扩缩容与容量规划（07↔06、07↔05、05→06 均双向，仅 06→05 缺）；R3 只闭合了 06→07，三角仍差一角 | 06 L791-805 无 05-DS 链接；05 篇 L242 有 →06 | 0.9 |
| R4-LINK-03 | 00-计算机与工程基础/04-C++并发与内存模型/02-Atomic、03-LockFree ↔ 游戏服务端/01-架构与网络/05-并发与高性能 | C++ 并发篇级互链未建立，且 R3 健康项记录失真：02/03 的关联阅读 L295 指向服务端 06-世界模拟 README（非 01-05）；01-05 L548 仅链 00/04 分类 README；R3 声称"01-05↔00-04 双向（L548 ↔ L127/L295）"与实际行号/目标不符 | 02 L295、03 L295 → 06-世界模拟；01-05 L548 → 00/04 README | 0.85 |
| R4-STRUCT-01 | 系统实战/03-技能释放完整链路.md L278-280、04-Buff系统完整链路.md L275-277 | 空节 + 内容归属错误："## 9. 术语速查"标题下无任何内容，术语表格实际位于"## 10. 关联阅读"标题之下（L279+/L282+ 的表格属于术语速查内容），两篇同病 | 03 L278 空节、L282 表格在 L280 关联阅读下；04 L275 空节、L279 表格在 L277 下 | 0.95 |
| R4-CTRL-03 | .kb/review-queue.md | 队列状态失真两处：(a) DEDUP-01 条目（L21）挂的注释是 KD-009 的 STYLE-01 执行结果，错位；DEDUP-01 真正决策（KD-004 维持现状）却记在 R2-DEDUP-01 条目下（L97）；(b) STYLE-01 条目 Status 仍 Pending（L175），而 KD-009 已声明执行完毕 | review-queue L7-21、L163-175 ↔ decisions.md KD-009/KD-004 | 0.95 |

## P2 —— 风格与控制面（可选）

| ID | 位置 | 问题 | 置信度 |
| --- | --- | --- | --- |
| R4-CTRL-01 | .kb/taxonomy.yaml | 全面过期：domain 键"计算机与工程基础"与目录"00-计算机与工程基础"不对齐；00_Index/Archive/Inbox/Knowledge/Projects/learning 六个顶层目录未登记；全部 subdomains 为空 {}（实际有 01-引擎基础 等子目录）；status 全 draft，与头部注释"首轮流程后修订为 active"及三轮审计现实矛盾 | 0.9 |
| R4-CTRL-02 | .kb/aliases.yaml | 覆盖不全：缺高频概念别名——行为树（Behaviortree/BehaviorTree/行为树，库内 497 处）、NavMesh（209 处）、RVO（105 处）、PCG（371 处）、"能力系统/GAS 能力系统"等 | 0.9 |
| R4-META-01 | .kb/README.md L10 | audit.md 描述过期：写"最新轮次：第二轮 2026-08-13"，实际 R3 已完成 | 0.95 |
| R4-STYLE-01 | 游戏知识/07-UI与性能优化/01-04 四篇 | 元数据缺"官方参考"行（06/07/08 有）；正文有官方链接（门禁 PASS），属规范 §3 元数据模板不完整，目录内 4/8 不一致 | 0.8 |
| R4-STYLE-02 | 游戏AI/01-决策与架构、游戏测试与质量 | H1 编号风格目录内不统一：AI/01 文件名有 NN- 前缀但 H1 无编号；测试 01-05 用 "NN ·" 前缀、06-10 H1 无编号 | 0.6 |
| R4-STYLE-03 | 00-全栈运行闭环（适用范围x2、兼容性边界x3）、08-09 构建烘焙（兼容性边界x4）、12-32 启动监听（适用范围x2）等 | 元数据"一键多值"用重复键表达（并列值风格），与"键：值"模板有出入；多数为摘要+元数据节双位置正常模式，仅上述几篇为连续列表内重复 | 0.6 |

## 健康项（本轮确认无需动作）

- R3 四项修复点（FACT-01/LINK-01/STYLE-01/STYLE-02）全部存活，无回归。
- 12 目录 01-48：编号连续、H1 双模板统一、成熟度齐备、01-18 "本篇对应知识库 X" 链接与 20-38 分工声明两种头部模式健康；39-48 Lyra 系列与 KD-004 附录去重决策一致。
- 服务端 04：00-08 编号连续；00 总览 FAQ/清单单一来源索引（L161/184/224）无回退；07↔08 互链双向。
- 服务端 06：01-14 映射完整（README L26）；13-Snapshot 已落地（README L58 登记）；编号缺口 02/06-10 均有规划声明。
- 热点族收敛：AOI（算法 03-03↔服务端 06-05 双向）、GAS/技能/Buff（服务端 08↔游戏知识 03-01↔12-05↔系统实战 03/04 分工互指充分）、确定性/回放（算法 05↔服务端 12↔13↔游戏知识 09）、测试容量（07↔05-05 显式分工）、DS 平台化主体（01 篇分工声明有效）——无概念层重复。
- 证据链：七个实验目录 src/scripts/results 与 README 声明一致；build 产物被 .gitignore 覆盖；无孤儿。
- 元数据：全库无旧 docs.unrealengine.com 链接；"待核对/待补充"均为有标注的事实边界声明（健康）；成熟度覆盖 0 缺失。
- git：工作树干净、diff --check 通过、无子模块、无 LFS。

> R4-CTRL-03 后续状态：review-queue 中错位的执行注记已归位，STYLE-01 已标记为已执行；本表保留该条作为历史发现，不再作为当前 P1 待办。

## review-queue 新增（不自动执行，待用户确认）

| ID | 候选 | 层级 | 置信度 | 建议 |
| --- | --- | --- | --- | --- |
| R4-DUP-01 | 游戏知识/03-13-背包与装备系统 ↔ 游戏服务端/03-01-背包与道具系统 | overlapping（族内唯一真重叠：双层数据模型/槽位/堆叠/同步主题重合，双向无引用无分工声明） | 0.85 | Split+Merge：核心建模/槽位/堆叠/同步收敛到单一 canonical，各自保留 UE 表现层与服务端事务层侧重；补双向互链 |
| R4-DUP-02 | 笔记/A星算法优化 ↔ 游戏算法/01-02-A星算法与优化 | semantic（笔记为豁免速查，但被系统实战 07 当依赖引用，被动承担正文角色） | 0.72 | Extend canonical 服务端工程角度后 Archive/收敛笔记为纯速查并正式指向 canonical |
| R4-DUP-03 | 游戏服务端/05-02-Linux DS部署 ↔ 游戏知识/08-09-构建烘焙 | overlapping 轻度漂移（05-02 重述构建机制，互链单向 08-09 未回链） | 0.6 | Keep+Refactor：05-02 收窄到部署视角，构建机制统一指向 08-09 |

既有遗留（不变）：R2-SPLIT-01（AI 03-01 拆分）、R2-MOVE-01（07-05 调试工具归属）、R2-GATE-01（成熟度门禁阶段 B）。

## 方法、边界与证据

- 机械扫描（主线程）：check_repo.ps1（PASS/FAIL 0）；manifest↔磁盘全量比对（0 缺 0 多）；非代码 %20 扫描（0）；占位词非代码扫描（39 文件全合法）；多 H1 非代码扫描（4 控制面文档）；重复 H2（0）；空节扫描（2 处真问题 = R4-STRUCT-01）；编号连续性全目录扫描（缺口均有声明）；重复元数据键扫描（含"摘要+元数据节"正常模式甄别）；成熟度分布全量扫描（与 R3 一致）。
- 语义验证（4 个只读子代理并行，模型 deepseek-v4-flash）：S1 回归与互链（R3 4/4 + R2 8 组：6 PASS、2 FAIL、1 记录失真）；S2 控制面（taxonomy/aliases/MOC/review-queue/decisions/K-P 边界/manifest）；S3 语义重复（8 热点族：1 组真重叠 + 2 组收敛候选）；S4 质量抽样（12 篇正文 + 5 目录 H1/编号扫描）。
- 子代理均按 AGENTS.md 只读约束执行，未写任何文件；本轮唯一写入为本控制面（.kb/audit.md、.kb/plans/current.md、.kb/decisions.md、.kb/review-queue.md）。
- 已知边界：互链判定以"篇级链接存在性"为准，分类 README 级链接不算双向互链（R4-LINK-03 据此判失真）。

---

# Lyra 专项审查（R4-LYRA，2026-08-14）

> 范围：游戏知识/12-引擎源码分析/39-48 十篇 + 12 README Lyra 段 + 19 路线图 Lyra 条目 + 全库 Lyra 引用。
> 模式：audit（只读）。主线程机械扫描（编号/H1/元数据/断链/附录围栏对/引用矩阵）+ 4 个只读子代理并行语义审查（A: 39-42、B: 43-45、C: 46-48、D: 跨篇一致性）。

## 结论

状态：**PASS_WITH_MINOR_WARNINGS**（十篇中 9 篇无 P0/P1；1 处 P1 组件重复 + 4 处 P2 导航/口径 + 若干 P3 观察）。

## 通过项（机械 + 子代理交叉验证）

- H1 模板 39-48 全部"UE5.8 Lyra 源码解析 NN：主题"统一；章节编号每篇连续无跳号。
- 元数据：版本基准 CL55116800 / UE5.8 / L2 / 最后更新 2026-08-13 / 官方参考全部齐全。
- 源码附录：10 篇共 243 个文件逐字收录（14/14/18/26/37/24/24/28/33/25），围栏对数=文件数全部命中；子代理 A/B 独立复核行数表 100% 吻合；均带"收录原则+版权提示+行数清单"。
- 事实边界纪律良好：各篇区分"静态核对事实 vs 待编辑器复核 vs 示意"；44 §十一 RepGraph/Iris 与 §十四 Gauntlet 口径与 34 篇、测试 06 篇一致，证据链闭合。
- KD-004 附录决策：47/48 拆分分摊明确（47=调试/编辑/测试 33 文件，48=插件 25 文件，双向注明 #1-#33 归属），48 的 CommonLoadingScreen 归类正确。
- KD-005 改名：全库无 `46-Lyra-AI队伍` 旧名残留（仅 .kb 决策档案历史描述）。
- 篇数口径："47 篇源码正文"正确（01-18 + 20-48 = 47）；"39-48 十篇"统一，无"九篇"旧口径。
- 关键互链双向存活：40↔44 Travel、42↔46 伤害过滤、45↔41、46↔47、47↔48。

## P1 —— 组件重复（建议修复）

| ID | 位置 | 问题 | 证据 | 置信度 |
| --- | --- | --- | --- | --- |
| LYRA-DUP-01 | 46 §八.1-8.2（L611-655）+ 附录 #5/#6 ↔ 47 §八（L433-483）/§7.2 | `ULyraBotCheats` 与 `ULyraDeveloperSettings`（机器人数目字段）双篇正文级重复详讲且互不转发；BotCheats.h/.cpp 附录仅收 46，调试域组件归属（KD-005 调试归 47）定位不一致 | 46 L613-632 命令/构造详解；47 L433-483 同类详解；46 §八 无"详见 47" | 0.85 |

## P2 —— 导航与口径（建议修复）

| ID | 位置 | 问题 | 置信度 |
| --- | --- | --- | --- |
| LYRA-LINK-01 | 19-高优先级源码覆盖路线图.md L189 | "39-44 阅读路线"过时，应改 39-48 | 0.95 |
| LYRA-LINK-02 | 40/41/42/43 关联阅读 | 未链拆分新篇 47/48（43 有 47 无 48；40/41/42 两者皆无）——旧篇查不到新篇，链单向 | 0.9 |
| LYRA-LINK-03 | 44 关联阅读 L1971/L1979 | 47 重复列出两次；缺 45 | 0.9 |
| LYRA-LOG-01 | 40/41/42 更新日志；47 更新日志 L4843 | 40-42 未登记 48 拆分（39 已登记）；47 写"共 58 个文件"陈旧（现 33 + 25 迁出） | 0.85 |

## P3 —— 观察（可不改）

- 45/46 未链 48；39 关联阅读仅列 45/48（正文有全表缓解）；44 缺 45（并入 LINK-03）。
- 成熟度行位置三风格并存（39-44 头部+元数据节双标、45/46 头部、47/48 元数据节）。
- 43 更新日志位于文中（§四十一）而非文末；45 元数据用引注块、43/44 用表格。
- 47 §27 术语表/§28 复盘残留 AsyncMixin/PocketWorlds 等已迁出插件术语。
- 39 证据分级 A/B 标签可在正文结论处更显式。

## 修复状态

- 审查只读完成；修复已执行（2026-08-14 用户批准）：
  - LYRA-DUP-01：46 §八 补分工声明（机器人视角）与 47 转发，附录未动。
  - LYRA-LINK-01：19 路线图 L189 "39-44"→"39-48"。
  - LYRA-LINK-02：40/41/42/43 关联阅读补链 47/48（各 +2 条）。
  - LYRA-LINK-03：44 关联阅读去重 47（2→1）、补 45。
  - LYRA-LOG-01：40-42 更新日志补登记 48（各 +1 条）；47 日志"58 文件"改"33 + 25 迁出"。
  - P3：45/46 补链 48；47 术语表删除 4 个已迁出插件词 + 指路注；47 失败模式表插件 2 行改指路注。
  - 复验：check_repo PASS / FAIL 0；修复点逐一确认。

---

# Lyra 源码覆盖率专项审查（R4-LYRA-COVERAGE，2026-08-14）

> 范围：39-48 十篇对 LyraStarterGame 5.8 项目源码（C:\Users\zhaozhiqi\Documents\Unreal Projects\LyraStarterGame）的覆盖完整性。
> 模式：audit（只读）。主线程机械比对（707 项目文件 vs 附录 228 收录 + 正文引用）+ 2 只读子代理（未覆盖模块重要度评估、正文分析深度核查）。

## 结论

状态：**PARTIAL_COVERAGE**（总覆盖率 32.4%，核心运行链深读达标，但存在 2 个高重要度模块缺口与若干空心覆盖）。

## 一、覆盖率数据（机械比对，归一化忽略大小写）

- 项目源码：Source 482 + Plugins 225 = **707** 个 .h/.cpp 文件。
- 知识库覆盖：附录逐字收录 **228** 唯一文件（0 个收录但项目不存在的伪路径）+ 正文引用，合计覆盖 **229/707 = 32.4%**。
- 附录结构：39-41/45-47 纯 Source（10/14/18/24/28/33）、42 全 Source 26、43 混合 28+9、44 混合 9+13（CommonUser/CommonGame/LoadingScreen/ShooterTests）、48 纯 Plugins 25。
- 模块 100% 覆盖：Audio、Camera、Inventory、Teams、Tests（LyraGame）+ CommonStartupLoadingScreen、PocketWorlds(91%)。

## 二、高重要度缺口（2 个，建议补深挖）

| 模块 | 覆盖 | 缺口内容 | 建议 |
| --- | --- | --- | --- |
| **LyraGame/UI** | 3/79（4%） | 43 篇的"UI"实为插件注入机制（UIExtension/GameplayMessageRouter）；Lyra 自有控件完全空白：HUD 布局（LyraHUD/LyraHUDLayout）、Foundation 控件族（按钮/确认屏/断线屏）、IndicatorSystem 头顶指示器（11 文件）、Weapons UI（Reticle 准星/HitMarker 命中标记，12 文件）、LyraUIManagerSubsystem/LyraSettingScreen | 补 1 篇专项（量级与 43 相当） |
| **Plugins/GameSettings** | 0/59（0%） | 47/48 均未收录；它是 Lyra 设置系统唯一实现（GameSetting/Registry/Value*/Action/Collection 抽象 + 响应式面板），LyraSettingScreen 直接搭载；`LyraSettingScreen`/`GameSettingRegistry`/`LyraSettingsLocal` 全库零提及 | 补 1 篇专项 |

## 三、中重要度部分缺口（建议选择性补充）

- **AbilitySystem 子集**（17/51）：未覆盖 `LyraAbilityCost_*`（三种 GAS 费用抽象）、`LyraHealExecution`、`LyraCombatSet`/`LyraAttributeSet`、`LyraAbilityTagRelationshipMapping`（Tag 关系映射）、`LyraGlobalAbilitySystem`、`LyraGameplayCueManager`、Jump/Reset 能力——AbilityCost 与 TagRelationshipMapping 是 Lyra 区别于裸 GAS 的复用设计点。
- **Feedback**（0/19）：NumberPop 伤害数字弹出（MeshText/Niagara 两套）是已验证链路，42 篇仅文字提及。
- **Messages**（0/9）：VerbMessage/VerbMessageHelpers 规范事件消息协议，是 43 篇的"上层协议"。
- **Weapons**（6/16）：`LyraWeaponInstance`/`LyraWeaponSpawner` 武器实例核心未覆盖。
- **Plugins/CommonGame**（2/29）：GameUIManagerSubsystem/GameUIPolicy/PrimaryGameLayout 是 CommonUI 落地承托层。
- **Plugins/GameFeatures**（3/53）：GameFeatureAction 家族动作（AddAbilities/AddInputBinding/AddGameplayCuePath）可复用。

## 四、空心覆盖（附录收录但正文分析不足，按严重度）

| 严重度 | 文件 | 附录行数 | 正文分析情况 |
| --- | --- | --- | --- |
| 严重 | 44-CommonUserSubsystem.cpp | 2684（全系列最大） | 正文仅 5 次提及（时序图/表格），OSSv1/v2 登录管线零深读 |
| 严重 | 44-LyraGameInstance.cpp | 339 | 主体网络加密/DTLS 代码，正文零次提及加密 |
| 严重 | 42-LyraRangedWeaponInstance.h/.cpp | 468 | Heat 热度→散布模型/衰减/生命周期全文未析，正文仅 2 次 |
| 严重 | 42-LyraPlayerController.h/.cpp | 792 | CheatManager/ServerCheat/相机管理大面积未分析 |
| 严重 | 41-ALyraCharacter.cpp/.h | 682+231 | 正文仅回调转发映射表，位移/FastSharedReplication/死亡主体零分析（仅 1 次） |
| 中 | 43-AsyncAction_ListenForGameplayMessage + UIExtensionPointWidget（4 文件） | ~431 | 仅附录索引列名，实现未析（43 篇约 22% 附录文件空心，按行数约 6%） |
| 中 | 41-GameFeatureAction_AddAbilities | 425 | 正文一句带过 |
| 中 | 47-EditorValidator_* 子类（6 文件） | — | 正文仅一行表格概括 |
| 中 | 48-CommonStartupLoadingScreen + SubtitleDisplay | — | 收录即空，自承"44 篇已深挖只交叉引用" |
| 中 | 40-LyraExperienceDefinition.cpp | — | "禁止二次继承"设计约束全文收录未讲解 |
| 中 | 46-LyraBotCheats.cpp | 59 | 正文委托 47 篇，实质逻辑未析（与 LYRA-DUP-01 修复相关） |

## 五、39 篇插件地图缺口

- 16 插件覆盖 12：正文目录树仅画 6 个，其余靠启用列表/附录补全。
- **完全缺失 4 个**：GreenRoom、RedRoom、LyraExampleContent、LyraExtTool（正文+附录零命中）；39/48 以"8 个扩展插件"概述但未点名这 4 个。

## 六、已覆盖部分的名实判定

- **核心链路类深读达标**：ExperienceManagerComponent、PawnExtension+Hero、CameraModeStack、TeamSubsystem、GAS-ASC、InventoryManager 均有调用链+生命周期+数据流真正深读（核心类正文出现 40-83 次）。
- 附录逐字收录声明成立（228 文件全部存在、行数表 100% 命中、0 伪路径）。
- 5 个"题眼类"在 5.8 源码中不存在（非缺陷）；2 个主题相关但未收录（ULyraWeaponStateComponent 正文自认未追踪、ULyraGameSession 全委任 CommonSession）。

## 七、建议（待用户批准）

1. **高优先**：补 LyraGame/UI 专项篇 + Plugins/GameSettings 专项篇（预计覆盖率 32.4% → ~40%）。
2. **中优先**：AbilitySystem 未覆盖子集（AbilityCost/TagRelationshipMapping 等）、Feedback NumberPop、Messages 协议、Weapons 实例、CommonGame UIManager、GameFeatureAction 家族选择性补充。
3. **低优先**：空心覆盖标注——在附录清单注明"仅收录未深析"；39 插件地图补 4 个缺失插件名。
4. 建议在 12 README/19 路线图新增"覆盖边界声明"列，区分未覆盖是"已隐式覆盖/薄壳/真遗漏"。

## 补篇执行状态（2026-08-14 用户批准，已执行）

- LYRA-COV-01 已执行：新建 `49-Lyra-UI控件与表现源码.md`（558 行正文 + 11 文件附录逐字一致；HUD/Layout/Foundation/IndicatorSystem/Weapons UI/性能统计）。
- LYRA-COV-02 已执行：新建 `50-Lyra-设置系统与GameSettings源码.md`（342 行正文 + 10 文件附录行数全 OK；GameSettings 插件抽象 + Lyra 注册表/载体/设置屏）。
- LYRA-COV-03 已执行：39 插件地图补全 16 插件；41/42/44 附录补覆盖边界声明（空心标注）；12 README/19 路线图/39 总览篇数口径 47→49 更新。
- 导航同步：12 README（映射表/文件列表/学习顺序/篇数）、19 路线图（L12/L124/L136 + 49/50 登记行）、39 总览（系列表 + 阅读顺序）、manifest（325 条目与磁盘一致）。
- 覆盖率提升：新增 49（11 文件）+ 50（10 文件）= 21 个新覆盖文件 → 250/707 = **35.4%**（补充后）。
- 复验：check_repo PASS / FAIL 0；49 附录逐字比对一致；git diff --check 通过。

## 批次 1 + 批次 3 执行状态（2026-08-14 用户批准，已执行）

- LYRA 批次 1：新建 `51-Lyra-GAS扩展与能力费用源码.md`（551 行正文 + 附录 A 12 逻辑单元/22 文件逐字一致；AbilityCost 接口与三实现、LyraAttributeSet/CombatSet、LyraHealExecution、AbilityTag 关系映射、全局能力系统、GameplayCue 管理器、Jump/Reset 能力）；42 篇追加"武器实例与生成器"章节（5 附录文件逐字一致）；43 篇追加"游戏语义消息协议（VerbMessage）"章节（5 附录文件逐字一致，含 5.8 版本口径校正：Helpers 仅四函数，FindInstigator/GetVerbMessageContext 不存在）；49 篇追加"伤害数字弹出（NumberPop）与上下文特效（ContextEffects）"章节（6 附录文件逐字一致，含 MeshText.cpp 尾随空行修复）。
- LYRA 批次 3：12 README/19 路线图"Lyra 系列覆盖边界声明"（已隐式覆盖/薄壳/示例玩法专属/待补批次 2）。
- 覆盖率提升：批次 1 新增 42(+5) + 43(+5) + 49(+6) + 51(+22) = 38 个新覆盖文件（去重叠后净 +36）→ 286/707 = **40.5%**（基线 250/35.4%）。AbilitySystem 模块 39 文件覆盖、Weapons 10、Messages 7、Feedback 6。
- 复验：check_repo PASS / FAIL 0；43/49/51 新增附录 16 文件程序化逐字比对一致（51 的 22 文件经围栏级比对 + 尾随空行修复）；manifest 326 条目与磁盘一致（顺带清理 R3 遗留 24 行垃圾前缀并补录 `03-LLM-NPC安全.md` 缺失条目）；12 README/19 路线图/39 总览篇数口径 49→50 篇。

## 批次 2 执行状态（2026-08-14 用户批准，已执行）

- 新建 `52-Lyra-交互系统源码.md`（600 行正文 + 17 文件附录逐字一致；IInteractableTarget/IInteractionInstigator 接口、InteractionQuery/Option 数据层、InteractionStatics、GAS 交互能力与任务族、持续时间交互消息；事实校正：5.8 接口仅 GatherInteractionOptions/CustomizeInteractionEventData、真实现者是 ShooterCore `ALyraWorldCollectable` 而非 WeaponSpawner、持续时间消息无 C++ 生产端）。
- 40 篇追加"GameFeatureAction 家族"章节（529 行正文 + 6 附录文件逐字一致；WorldActionBase 四阶段钩子、AddAbilities/AddInputBinding/AddInputContextMapping/AddWidget/AddGameplayCuePath/SplitscreenConfig、Policy 补深；版本口径：AddAbilities 无 bAllowGrantingToNonInstigatedActors、激活锁定在引擎 GameFeaturesSubsystem）。
- 49 篇追加"CommonGame UI 管理层"章节（约 390 行正文 + 6 附录文件逐字一致；GameUIManagerSubsystem/GameUIPolicy/PrimaryGameLayout/CommonUIExtensions/AsyncAction/Messaging；版本口径：无 UCommonGameUIPolicy 子类、PushContentToLayer 在 Extensions/Layout 而非 Policy）。
- 41 篇追加"动画实例基类与 Tag 属性映射"章节（149 行正文 + 2 附录文件逐字一致；GameplayTagPropertyMap 桥、ASC→AnimInstance 调用链、GroundDistance、编辑器校验）。
- 覆盖率提升：批次 2 净 +17 → 303/707 = **42.9%**（基线 286/40.5%）。Interaction 17 文件全覆盖、CommonGame 8、GameFeatures 10、Animation 2。
- 复验：check_repo PASS / FAIL 0（52 篇"预留"占位词 5 处改为"保留"后归零）；四篇新增附录 31 文件程序化逐字比对一致；52 附录路径补全 `Source/LyraGame/` 前缀与系列口径统一；manifest 327 条目与磁盘一致；12 README/19 路线图/39 总览篇数口径 50→51 篇、边界声明"批次 2 已执行、暂无已登记待补项"。

## 方法、边界与证据

- 机械比对：PowerShell 脚本提取附录表格行（`| N | \`路径\` | 行数 |`）+ 正文路径正则（Source/Plugins 前缀 .h/.cpp），与项目文件全量枚举归一化比对；-Include 与 -Recurse 组合统计陷阱已修正（目录文件数以 Where-Object Extension 为准）。
- 语义验证：子代理 1 抽样实读 22 个未覆盖模块（每目录 2-3 头文件）；子代理 2 逐篇核查附录-正文对应 + 内部 2 个深度子代理（40/41 篇）交叉验证，结论一致。
- 已知边界：覆盖判定为"文件级"（收录或正文引用即计覆盖）；"深读 vs 仅提及"以子代理抽样判定；正文引用正则可能遗漏少数非常规格式路径（如换行断开的路径）。

## 维护后复核（2026-08-17）

> 范围：以 `aca16b8` 为基线，复核本轮工作树对工作日志、Lyra 39-52 导航、路线图完成清单、控制面状态和 manifest 的修复；不改写前述 R4 历史基线。

- 当前仓库统计：327 个 Markdown（正文 269、README 58），`check_repo.ps1` 结果 PASS / FAIL 0；短工作日志、路线图和维护文档的 11 条 WARN 属于既有豁免口径。
- Lyra 当前口径统一为 39-52：根 README 为 51 篇源码正文 + 1 篇路线图，12 README/19 路线图/39 总览及 43/44/45/49/51 交叉阅读均已同步；路线图完成清单已补 49-52。
- 时装工作日志的配置完备性改为：低级/中级必须分别配置出边进阶道具，三个品质均必须配置升级道具组，高级不要求进阶道具。
- `.kb/manifest.yaml` 重新计算 327 条目 bytes/lines，路径集合、文件大小和行数与磁盘一致。
- review queue 的历史 Current/Suggested 快照保留；DEDUP-01/R2-DEDUP-01 已标为已决策，R2-MANIFEST-01/RENAME-01 已标为已执行，无有效 Pending 条目。
- Lyra 源码附录完整保留代码字符、注释、条件编译和文件尾换行；仅统一代码围栏内的行尾及缩进空白并在附录标注格式归一，普通 Markdown 继续执行 Git whitespace 检查。

## 核心源码覆盖复核（2026-08-18）

> 范围：以 2026-08-17 收口状态为基线，复核 Lyra 39-56 导航、核心玩法源码证据、引擎模块化桥接和控制面；不把静态代码证据误报为 PIE/DS/设备矩阵运行验证。

- 新增 53-56 四篇正式源码文章：53 收录 GameState、PlayerController、PlayerSpawningManager、PlayerStart、Pawn、CharacterMovement 和 CharacterWithAbilities；54 收录 Lyra ReplicationGraph、ModularGameplayActors、UE 5.8 GameFrameworkComponentManager/GameFeatures 状态真实节选；55 收录 Lyra 输入修正器、用户设置/配置档、Latency Marker 和 ShooterCore AimAssist；56 收录 ShooterCore TDM 选点、Assist/ElimChain/ElimStreak、Accolade，以及 GAS EffectContext/TargetData/AbilitySource。
- 前置 UE 专题 20-31 同步补入本机实际函数：Iris ReplicationSystem、Mass Signal/StateTree、WorldPartition/WorldStreaming Insights、Landscape/Foliage、Sequencer/MoviePipeline、Enhanced Input/Gameplay Tags、CommonUI/CommonInput、MVVMView、GameplayTasks、Trace/Insights、Lumen/MegaLights、ProceduralVegetationEditor；原有流程图和伪代码只保留为概念说明，不再作为唯一源码证据。
- 四篇文章均以本机 LyraStarterGame/UE 5.8 文件生成真实 C++ 围栏；53/54/55/56 分别为 25/35/25/30 个 cpp 围栏，围栏成对、源码占位标记为 0；项目核心文件按全文附录收录，引擎文件只收录可核对的真实函数节选并标注版本边界。
- 导航已统一到 39-56：12 README、19 路线图、核心覆盖矩阵和阅读顺序均登记 53-56；ShooterCore 的 TDM/AimAssist/淘汰消息/Accolade 核心不再列为整体范围外，TopDownArena 与剩余资产/模式仍保持示例边界。
- 覆盖矩阵采用 L0/L1/L2/L3：L0 仅路径/类名，L1 真实函数片段，L2 真实片段加全文附录，L3 运行态验证。53/55/56 的项目核心条目达到 L2，54 的引擎桥为 L1、项目文件为 L2；PIE、Dedicated Server、Iris/ReplicationGraph 配置切换、手柄设备矩阵和资产接线仍未宣称完成。
- 当前仓库统计：331 个 Markdown（正文 273、README 58）；check_repo.ps1 -Root <repo> PASS / FAIL 0，既有 11 条短文 WARN 保持豁免；全库 UTF-8/BOM/围栏检查通过，git diff --check 通过。
- .kb/manifest.yaml 已扩展到 331 条目，新增 53-56 并重新计算修改后的 README/路线图/审计/计划 bytes/lines；路径集合和磁盘 Markdown 集合应保持一致。review-queue 无有效 Pending 条目，历史 Current/Suggested 快照继续保留为历史记录。

### 当前仍明确的下一步

核心玩法和引擎桥已达到可复核的静态源码覆盖，但不能据此声称 Lyra 全部 756 个 Source/Plugins 文件都已深读。下一轮应优先把 Settings/UI/Feedback/Performance/Replays/Hotfix/Cosmetics 的 L0/L1 条目提升为真实函数或全文附录，并为 53-56 补 PIE、Dedicated Server、网络切换和设备矩阵结果。

## 计算机与工程基础全面审查收口（2026-08-20）

> 范围：复核 `00-计算机与工程基础/` 的 16 分类、40 篇正文及其导航、成熟度、控制面和当前未提交工作树；历史 Lyra 审计保留为历史快照。

- 格式：修复 5 篇正文中的字面 `` `r`n `` 元数据，复查基础域字面换行残留为 0；Markdown 保持 UTF-8 无 BOM，代码围栏闭合。
- 技术表述：修正 no-throw guarantee、析构异常规范、major/minor page fault、Paxos prepare/promise、Quorum 条件、TLS 客户端认证、QUIC 共享拥塞控制与 `IORING_SETUP_COOP_TASKRUN` 边界。
- 证据：7 篇仅有命令计划、没有归档脚本/原始结果的 L3 文档降为 L2，并写明证据状态；实际运行证据落盘后再升级。
- 深度：原 13 篇低于 300 行的基础专题已扩写至 300 行以上，补充最小程序、环境与配置、预期输出、Benchmark、故障注入、回滚和验收模板；新增 301 行虚拟化专题，覆盖硬件辅助隔离与开销验证。
- 导航：基础层状态表按实际 40 篇重算；taxonomy 扩展为 16 个子域；`.kb/README.md` 的 taxonomy 状态与 audit 日期同步。
- 索引：manifest 按磁盘 386 个 Markdown 机械重建，路径集合完全一致，逐条 `bytes`、`lines`、`maturity` 不一致项为 0。
- 门禁：全库正文 315、README 71，基础域正文 40；`check_repo.ps1` PASS、FAIL 0，12 条 WARN 均为既有控制面、日志、笔记、路线图或维护 Skill 短文；UTF-8 解码、BOM、替换字符和基础域代码围栏检查均无异常，`git diff --check` 退出码为 0。
- 发布边界：本轮未获 commit/push 授权；全部变更保留在工作树，不将静态示例或计划误报为已运行结果。

## OKF v0.2 与 Obsidian hybrid 接入审计（2026-08-20）

> 范围：在不移动 386 篇任务前 legacy Markdown、不覆盖用户 `.obsidian` 的前提下，将 GoogleCloudPlatform/knowledge-catalog 的 Open Knowledge Format v0.2 作为互操作层接入现有 taxonomy/canonical/manifest/L0~L5 体系。

- 架构：仓库根为 bundle/vault；`README.md` 是 GitHub 人类入口，根 `index.md` 是 OKF/Obsidian 渐进入口，`00_Index/MOC.md` 是主题导航；根 `log.md` 与历史 `learning/log.md` 职责分离。
- 语义：OKF 只强制概念文档具有非空 `type`；`status` 限于 `draft|stable|deprecated`；`verified` 保存带 `by/at` 的事件，trust tier 由 actor 推导；本库 type 词汇、maturity、canonical 与 scope 都明确为本地约定。
- 迁移：采用 hybrid。Changed 约束新建/触碰文档；Audit 报告 legacy 欠账；Strict 仅作为 profile 范围迁移门禁。检查器是受支持 YAML 子集的 lint，不冒充通用 YAML/OKF parser。
- Obsidian：新增 `00_Index/Knowledge.base`、知识条目模板与协作指南；标准 Markdown/YAML 是唯一共享事实，`.obsidian/workspace*.json` 作为本机动态状态忽略。
- 校验：Windows PowerShell 5.1 与 `pwsh` 的 Changed 均通过；正反例夹具同时验证合法 block 事件/生成映射可接受，7 种非法标量、缺字段、非法日期和保留文件结构会失败。最终精确计数见当前计划和本节后续门禁输出。
- 最终门禁：Changed 21/21 PASS；Audit 为 Conformant 21 / Legacy 369 / Excluded 2 / FAIL 0；Strict 对 369 个 legacy 返回预期 FAIL；`check_repo` 对 392 个 Markdown 返回 PASS / FAIL 0 / WARN 13；manifest 392/392 且 bytes/lines/maturity 差异 0；`git diff --check` 无 whitespace error。
- 用户状态：任务前稳定配置 `app.json`、`appearance.json`、`core-plugins.json` 的 SHA-256 保持不变；`workspace.json` 在执行期间被外部 Obsidian 从 4849 bytes 更新为 5135 bytes，已原样保留且进入忽略规则。
- 发布边界：未获 commit/push 授权；本轮不提交、不推送，也不把 hybrid 接入描述成全库官方严格合规。

## OKF profile 范围全量迁移收口（2026-08-20）

> 范围：执行用户批准的 `OKF-MIGRATION-01`，将上一轮审计确认的 369 篇 legacy Markdown 迁入本库最小 frontmatter 合同；不移动路径、taxonomy、canonical 或链接，不覆盖用户 `.obsidian`。

- 批次：支持/证据 39、计算机基础 57、游戏算法+游戏 AI 47、游戏服务端 54、游戏知识 172，共 369 篇；批次互不重叠，每批完成后运行 Changed、正文完整性和仓库门禁再继续。
- 合同：每篇只前置 7 行 `type/title/status/verified/maturity` 元数据；`status: stable` 不表示已经验证，`verified: []` 不产生验证事件；未补造 `sources`、`generated`、`description`、`tags` 或更新时间。
- 类型：Architecture 23、BestPractice 32、Comparison 5、Concept 116、Evidence 7、Experience 4、Implementation 5、Index 60、Mechanism 106、Plan 1、Project 3、Reference 4、Research 1、Tutorial 2；含糊正文保守使用 `Concept`，未据关键词做结构移动。
- 成熟度：迁移元数据为 L0 73、L1 8、L2 274、L3 7、L4 6、L5 1；已有正文成熟度原样保留，无显式成熟度的支持/索引文档使用 L0，`maturity` 与 `verified` 保持正交。
- 正文完整性：所有 369 篇的 Git numstat 均为 `+7/-0`；后四批 330 篇去掉 7 行前缀后的 SHA-256 与仓库外迁移基线逐篇一致，先执行的 39 篇由严格前缀结构、`+7/-0` 与门禁共同确认无正文删改。
- OKF 门禁：Windows PowerShell 5.1 与 `pwsh` 的 Changed/Strict 均为 Scanned 390、Conformant 390、Legacy 0、Excluded 2、WARN 0、FAIL 0、PASS；Audit 同样 PASS。2 篇排除项是 `.agents/skills/**/SKILL.md` 与 `learning/log.md` 的操作性格式合同。
- 仓库门禁：`check_repo.ps1` 对 392 个 Markdown 返回 PASS / FAIL 0 / WARN 13；WARN 仍是短索引、计划、Skill、工作日志、路线图与笔记的既有用途豁免。标准 `git diff --check` 退出码为 0。
- Manifest：按磁盘 392 篇机械重建；路径集合、bytes、lines 与可检测 maturity 均为 0 差异。profile/manifest 标记的是本库 profile-scope Strict，不声称通过官方通用 YAML 或 OKF parser。
- Obsidian：Markdown Properties 与 `00_Index/Knowledge.base` 现在可覆盖全部受检条目；稳定 `.obsidian` 配置未由迁移任务修改，动态 workspace 状态继续忽略并原样保留。
- 独立验收：复跑 Strict、manifest 路径、`git diff --check` 与历史快照语义检查；P0–P3 均无遗留问题。
- 发布边界：本轮没有 commit/push 授权；改动只保留在工作树，未提交、未推送。

## 知识体系 virtual-first 导航重构 Phase A（2026-08-20）

> 范围：在已发布的 OKF 迁移提交 `9beb653` 上整理知识体系的导航与关系层；不移动、重命名、拆分、合并或改写知识正文，不修改 maturity/verified，不接管用户 `.obsidian`。

- 发布检查点：迁移提交父节点、399 个路径、mode/blob manifest 与 staged diff 指纹逐项一致；推送后本地 `main` 与 `origin/main` 均为 `9beb653aa267a4a0dbcf44a32fa21ce205cc3ce5`，ahead/behind `0/0`。
- 架构决策：KD-016 选择 virtual-first。现有六大领域继续作为物理 Canonical；`Knowledge/Projects` 保持有条件启用的骨架，evidence、系统实战、工作日志、笔记、Inbox、Archive 和方案保持各自边界。
- 导航：新增六个 Domain MOC 和两个 axes README/视图；七个跨域主题均指定一个 Primary Canonical，并把其他页面限定为 UE 实现、算法、源码证据、服务端生产约束、测试或实战视角。
- Obsidian：`Knowledge.base` 增加 All / Review / Evidence / Project 四视图；全局与 view filters、`file.inFolder`、`groupBy`、`order` 依据官方 Bases 语法。Base 只读 YAML Properties，不替代 Markdown 或 taxonomy。
- 协作：补 `references/agent协作与发布规则.md`，固化只读并行审计、共享文件串行写入、cached review、commit 后完整性和远端 SHA 门禁；`.obsidian` 始终不在任务写入范围。
- Manifest：新增 `scripts/rebuild_manifest.ps1`；UTF-8 严格解码、PS5 ASCII-safe Unicode 正则、Ordinal 排序；Windows PowerShell 5.1 与 pwsh 对同一输入生成相同 SHA-256。
- 独立审核：语义审计 P0/P1/P2/P3 均为 0；新增导航链接目标存在，知识正文路径/内容、maturity 与 verified 未改变。新增短 MOC 的不足 300 行 WARN 是导航用途预期，不是正文缺陷。
- 最终门禁：Changed 22/22、Strict 401/401（Excluded 2）均 PASS / FAIL 0；`check_repo` 对 403 个 Markdown PASS / FAIL 0 / WARN 21；manifest 403/403 且 path/kind/bytes/lines/maturity 差异 0；`git diff --check` 为 0。
- 物理重构：登记 `TAXONOMY-PILOT-01` 为 Pending；只有用户再次批准并具备 old→new、唯一 Canonical、链接影响、README/MOC/manifest 更新和回滚清单时，才允许小域试点。
- 发布边界：本 Phase A 改动尚未提交、尚未推送；与已发布的 OKF 迁移检查点分开审计。

## 知识体系门禁修复与来源层收敛（2026-09-10）

> 范围：在保持正文路径、H1、正文段落与链接关系不变的前提下，修复可客观判定的门禁缺陷，并把 `读书笔记/` 按 `architecture.json` 的 sources 分层从知识成熟度门禁中豁免。
> 模式：只读审计 + 用户授权的维护写入。宿主 PowerShell 无法调用 `git`，仓库自带的 PS 门禁未在本机实跑，改用等价只读复现核对规则。

- 修复前基线（等价复现）：585 篇 Markdown；未闭合代码围栏 42、来源 README 缺链 2、来源材料缺成熟度 164、核心正文缺陷 11、manifest 漂移 2。
- 围栏：42/42 修复——27 处为误置开栏（其后为 `---`、标题或正文），按语义删除；15 处为代码块/ASCII 图表未闭合（其后为 `#include`/`#pragma`/`//`/`+---`/树形图），按原文语义补闭合。未改动正文文字。
- 命名与清单：2 个含乱码 `â` 的来源文件重命名为标准名称（`—`/`–` 语义），标题/描述同步归一；`卷1-GameAIPro1`、`卷4-OnlineEdition2021` 的第 05 章行改为本地链接，待处理项清零。
- manifest：机械重建 585 篇，路径集合、bytes、lines 漂移均为 0。
- 核心正文：游戏AI `02-07`（新增 §6.3 验证与基准、最后更新、UE 官方来源）、游戏AI `03-03`（新增知识基线、来源、最后更新）、系统实战 `02`（新增最后更新与证据边界声明）、游戏知识 `04-08`（新增版本基准与最后更新）；服务端 `06-03` 补证据状态说明（保留 L3，证据为文档内可运行示例）；服务端 `06-04` 因示例为伪代码、无可运行 Demo，按 DoD 下调 L2，并同步 `游戏服务端/06-世界模拟与运行时/README.md`。
- 门禁与分层：`check_repo.ps1` 的知识成熟度门禁在 `工作日志/`、`笔记/`、`方案/` 之外增加 `读书笔记/` 豁免；`.kb/taxonomy.yaml` 与 `references/知识库架构.md` 同步登记来源层边界（决策 KD-026）。
- 复现结果：断链 0、BOM 0、替换字符 0、目录缺 README 0、未闭合围栏 0、README 清单缺链 0、成熟度缺失 0、领域质量门禁 0、游戏知识语义门禁 0、manifest 漂移 0；成熟度分布 L2=304、L3=7、L4=6、L5=1。
- 边界：以上为等价只读复现结果，不是仓库 PS 脚本在宿主的运行输出；本轮未提交、未推送，也未接管用户其他未提交改动。

## Gameplay 主线强化与本机证据落地（2026-09-11）

> 范围：新建 Gameplay 核心机制可运行证据与背包道具完整链路；修复"工具目录被当作知识正文"的扫描范围回归。宿主 PowerShell 仍无法调用原生进程，但 Bash 可调用 MinGW g++，上一轮的"无工具链"结论据此撤销。

- 环境事实修正：`C:\msys64\mingw64\bin\g++.exe`（MinGW-w64 g++ 16.1.0）在把自身目录加入 PATH 后可正常编译与运行；失败原因是 MinGW g++ 找不到 `cc1plus`/运行库，且宿主 PowerShell 不能派生原生进程，而非缺少工具链。
- 新增证据：`evidence/tests/gameplay-core/`（4 个 C++17 程序 + bash/PS 两套构建脚本 + 未修改的原始输出 + 统一格式 README）。断言结果：`inventory_txn` 7/7、`buff_conflict` 12/12、`skill_pipeline` 10/10、`attr_modifier_bench` 400 抽样 mismatch=0。
- 关键指标（单次运行，原始值见 `results/`）：背包事务 40 槽 400 000 次增删 **P50=100ns / P95=200ns / P99=200ns**，吞吐≈9.76×10⁶ ops/s；属性聚合 20 000 实体 × 16 修正器，全量重算 P50=1756.0µs / P99=2238.3µs，脏标记增量 P50=234.9µs / P99=422.7µs，**P50 加速 7.47x**；技能管线确定性重放两次运行哈希一致。
- 新增正文：`系统实战/05-背包道具完整链路.md`（**L4**：15 步 Gameplay Transaction 链路、权威状态与数据所有权、失败矩阵、验证矩阵、本地证据与未验证边界）；`系统实战/03`、`04` 增加本机证据小节（覆盖逻辑层，维持 L3 并明确边界）。
- 扫描范围修复：`.workbuddy/`（Agent 记忆与工具状态）加入 `.gitignore`、`architecture.json` 的 `excluded_roots` 与 `get_kb_markdown.ps1` 排除规则；修复前它为扫描结果引入"目录缺 README / 缺成熟度 / 缺 type"三项 FAIL 并进入 manifest。
- 门禁复核（等价复现，589 篇）：断链 0、BOM 0、替换字符 0、目录缺 README 0、未闭合围栏 0、README 清单缺链 0、成熟度缺项 0、领域质量门禁 0、游戏知识语义门禁 0、manifest 漂移 0；成熟度分布 L2=304、L3=7、**L4=7**、L5=1。
- 边界：全部数字来自单机单线程最小实现，**不代表线上容量**；基准存在运行间波动（属性 p50 1750–1760µs、加速比 7.4–7.8x）；未接入 UE GAS 运行时，未覆盖多线程竞争、跨服迁移与持久化链路。未提交、未推送。

## 伤害与属性结算链路落地（2026-09-11）

> 范围：把伤害结算从"技能链路的一个步骤"提升为独立链路并配可运行证据；延续"正文 + `evidence/*` 成对交付"的模式。

- 新增证据：`evidence/tests/damage-core/`（`damage_pipeline`）。**15 条断言全部通过**：A1 聚合顺序（Override→Add→Mul）、A2 顺序无关、D1 暴击在护甲前、D2 护甲曲线渐近（护甲 10⁹ 仍留 ≈1e-7 剩余系数）、D3 真伤跳护甲、D4 抗性与护甲乘算、D5 受伤加成位置、D6 护盾先于血量、D7 护盾余额保留、D8 过量单列与血量钳制、D9 死亡/免疫整段拦截不消耗护盾、D10 单次上限位于减伤后护盾前、D11 零负伤害拦截、D12 DOT 跳数精确取整、D13 同种子重放哈希一致。
- 关键指标：500 轮 × 1 000 次完整结算 = 500 000 次，单次 **P50=14.2ns / P95=16.3ns / P99=16.8ns**、吞吐 ≈7.72×10⁷ 次/秒；换算 20 Hz Tick 下 1 万次结算约 0.14 ms，表明**瓶颈不在公式**而在快照、范围查询与日志广播。
- 新增正文：`系统实战/11-伤害与属性结算完整链路.md`（**L4**，332 行）：15 步结算闭环、权威状态与快照规则、五乘区、护盾/过量/上限/拦截、分步演算表、与策划表对账方法、反模式、验证矩阵、术语速查；`03-技能释放` 补反向链接并明确 03/11 分工。
- 导航同步：`系统实战/README`（规划链路 10→11 条、状态表与文件列表）、根 `README`、`references/仓库结构.md`、`evidence/README.md`、`方案/知识体系完善执行方案.md` W4 状态。
- 门禁复核（等价复现，591 篇）：断链 0、BOM 0、替换字符 0、目录缺 README 0、未闭合围栏 0、README 清单缺链 0、成熟度缺项 0、领域质量门禁 0、游戏知识语义门禁 0、manifest 漂移 0；成熟度分布 L2=304、L3=7、**L4=8**、L5=1。
- 边界：公式为可辩护的参考实现，需按项目策划表重新标定；未接入 UE GAS，未覆盖并发结算、跨服重放与线上容量。未提交、未推送。

## 性能问题定位链路落地（2026-09-11）

> 范围：把性能定位从"方法速查"提升为纵向链路，并量化常见插桩样式的开销；延续"正文 + `evidence/*` 成对交付"的模式。

- 新增证据：`evidence/labs/profiling/`（`profiling_overhead` 7 条 + `hitch_and_budget` 10 条，**17 条断言全部通过**）。
- 每调用开销（p50，本机单线程）：裸计数 **0.33 ns**、原子计数 **4.01 ns**、作用域计时 **50.03 ns**、字符串查找 **5.68 ns**、格式化日志 **274.25 ns**、格式化+缓冲写 **309.90 ns**、1/100 采样计时 **0.96 ns**。极值比约 **940 倍**。
- 帧预算换算（热路径 100 000 次/帧，16.6 ms 预算）：作用域计时 **5.003 ms（30.14%）**、格式化日志 **27.467 ms（165.46%）**、格式化+写 **30.846 ms（185.82%）**、采样 **0.096 ms（0.58%）**、裸计数 0.032 ms（0.19%）。
- 卡顿检测：规则固定为 `max(2.5 × 中位数, 1.5 帧)` + 迟滞（连续 2 帧低于阈值 60% 结束）；实测 12 次注入（含 3 次级联）→ 12 个事件、18/18 帧召回、纯抖动 **0 误报**。**明确弃用 p99 基准**——p99 被卡顿污染后会自抬阈值导致漏检，本轮第一版实现正是因此漏检 2/18 帧，修正后重跑通过。
- 预算降级：连续 3 次超预算逐级 +1（上限 3）、连续 30 次达标逐级 −1；交替"超/不超"输入下级别变化 **0 次**；记账开销 **1.7 ns/Tick、每系统 0.4 ns**。
- 新增正文：`系统实战/10-性能问题定位完整链路.md`（**L4**，329 行）：四类性能问题分型、15 步定位闭环、一次定位推演模板（含失败方案记录）、预算与告警配置模板、十类反模式、验证矩阵。
- 导航同步：`系统实战/README`（10 号链路由规划转为已完成，共 7 条落地）、根 `README`、`references/仓库结构.md`、`evidence/README.md`、`方案/知识体系完善执行方案.md` W4 状态；`05`/`11` 增加指向本篇的度量口径链接。
- 门禁复核（等价复现，593 篇）：断链 0、BOM 0、替换字符 0、目录缺 README 0、未闭合围栏 0、README 清单缺链 0、成熟度缺项 0、领域质量门禁 0、游戏知识语义门禁 0、manifest 漂移 0；成熟度分布 L2=304、L3=7、**L4=9**、L5=1。
- 边界：开销为单机 `-O2` x86_64 的 p50，随编译器与架构变化，应看比例与量级；未接入 UE Insights/Trace、Linux perf 与线上流量，GPU 侧耗时未覆盖。未提交、未推送。

## 角色进入游戏链路落地（2026-09-11）

- 范围：新建 `系统实战/01-角色进入游戏完整链路.md`（L4）与 `evidence/tests/entry-core/`；同步导航、状态矩阵与控制面。
- 证据：4 个程序 **83 条断言全部通过**（entry_ticket 24 / entry_session 27 / ds_allocator 20 / jip_resync 12）。
- 密码学自校验：SHA-256 对 FIPS 180-4 向量（空串、`abc`、56 字节填充边界）、HMAC-SHA256 对 RFC 4231 TC1/TC2/TC3/TC6（含 >64 字节密钥），全部逐字匹配。
- 票据语义：篡改 payload / 换密钥 / 过期 / 时钟漂移（3s 容差内、30s 超容差）/ 跨服 / 重放，六类各由固定步骤拦下；比较为常量时间（首字节与末字节差异耗时一致）。
- 状态机：双层幂等（同 requestId 命中幂等表；换 requestId 时按玩家维度复用同一 ds-1，负载保持 1）；超时重试、拒绝不重试、Spawn 期失败仍回滚、`no_capacity` 不超卖、Ready 后取消为 no-op。
- **发现并修复一处真实缺陷**：分配失败仅置状态未中断步骤循环，链路带着空 DS 走完 Travel/Load/Spawn，产出"Ready 但从未连上服务器"的会话（断言 E18/E19 抓出）；修复为失败立即终止。
- **另一处纠正**：JIP 乱序包若按"重复即丢弃"处理会永久丢失该次写入（静默不一致），改为重排缓冲（缺口未补齐不推进版本）。同时修正基准实现——对本就有序的追加日志多做一次排序与指针数组分配，实测把追赶成本放大近 10 倍（66.6µs → 6.5µs @2000 实体）。
- 基准：JIP 追赶 p50 6.510 / 117.960 / 2104.845 µs（2 000 / 20 000 / 200 000 实体）；全量快照 p50 1.300 / 25.200 / 1941.100 µs。结论：**追赶 = 快照拷贝 + 重放，CPU 上永不比全量便宜**；200 000 实体 / 20 000 增量时带宽省 10 倍、CPU 多付 163.7 µs（约 +8%）。
- 门禁（等价复现，596 篇）：断链 / BOM / 编码 / 目录 README / 围栏 / 清单 / 成熟度 / 领域门禁 / 语义门禁 / manifest 漂移全部 0；成熟度分布 L2=304 / L3=7 / **L4=10** / L5=1。
- 边界：密码学实现未经侧信道审计、未接真实网关/DS 平台/DB；状态机与分配器为单线程模型；带宽按 `Entity 32B / Delta 32B` 估算而非线上实测包体。未提交、未推送。

# UE 源码分析核验轮次（R5-UE-VERIFY，2026-09-14）

- 范围：游戏知识/12-引擎源码分析 既有 55 篇（01-18、20-56）的路径、符号、Lyra 附录与行号断言核验；只读复核 + 定点修正，不新增主题、不改目录结构、不提升 maturity。
- 证据源：安装树 `C:\Program Files\Epic Games\UE_5.8\Engine`（5.8.0 / CL 55116800）、5.8 源码 checkout `C:\Users\zhaozhiqi\Documents\GitHub\UnrealEngine`（5.8.2 / `CompatibleChangelist` 55116800 / 分支 `UE5`，含 `Samples/Games/Lyra`）、本机 Lyra 项目 `LyraStarterGame`（`EngineAssociation` 5.8）。
- 方法：4 个只读审计脚本（路径断言解析、限定符号回查含全树兜底与控制符号自检、附录行数核对、附录正文逐字比对、行号断言按上下文绑定核验）；脚本与报告留在 git-ignored 的 `.kb_work/cache/`。
- 通过项：附录行数 311/311 一致；40-56 整卷收录代码块与磁盘逐字一致（0 处不符）；路径断言除修正项外全部命中；2315 个限定符号的未命中项全部由"类内内联实现 / 纯虚声明 / 文章自定义示例类"解释。
- 修正 12 处：30 篇 `LumenSceneData.cpp`（不存在）→`LumenSurfaceCache.cpp` + 逐字节选；30 篇伪函数名标注；05 篇 `AttributeSet.cpp` 归因→`AttributeSet.h` 类内内联空虚函数；02/04×2/09/33 行号；20 路径补全；41 文件名；35 时序图命名空间 + 行号口径；19 自身计数 54→55。
- 门禁：`check_okf.ps1 -Mode Changed` PASS（91/91）；`git diff --check` exit 0；`check_repo.ps1` FAIL 3 项，逐条核实为**既存**问题（见 review-queue `R5-UE-VERIFY-01`），不在本轮 allowlist，未修改。
- Manifest：仅重算本目录 11 篇的 bytes/lines，其余保持 2026-09-10 值，头部注明部分刷新。
- 边界：只做静态核对，未验证运行态（Editor / Listen Server / DS 时序、网络后端、资产接线）；行号仅对 5.8.0 / CL 55116800 与本轮 checkout 有效。未提交、未推送。

# UE 源码补深轮次（R6-UE-ENRICH，2026-09-14）

- 范围：`游戏知识/12-引擎源码分析` 9 篇——01/02/03/04/09/10 补深（骨架式剖析 → 以真实源码为主体 + 逐段解构 + 事实边界），33/34/38 定点修正"自称摘自源码、实则与 5.8 不匹配"的代码块与事实性错误；另同步 README 与 19 的记录。
- 方法（本轮新增能力）：**代码块保真度审计**——抽出各篇 `cpp` 代码块，按块前是否出现「摘自 / 完整真实源码」判定该块**是否自称有出处**；有出处的块逐行归一化后与被引文件比对；未命中行再做**全树兜底检索**，区分"引用错文件 / 版本漂移"与"引擎中不存在（自造代码）"。脚本 `audit-verbatim-coverage.ps1` 与报告留在 git-ignored 的 `.kb_work/cache/`。
- 通过项：本轮 9 篇共 293 个代码块，其中 142 个自称有出处；受检 3710 行引擎代码在**被引文件中逐行命中 3681 行（99.2%）**，未命中 29 行全部属于 2 个"多文件引用 / 引用句用'同文件'指代"的边界块，已逐行确认代码真实；**引擎里根本不存在的行（自造代码）0 行**。全库 57 篇同口径审计（含 Lyra 附录）：1230 块 / 434 个带出处声明的块 / 28476 行受检 / 28445 行命中（99.89%）。
- 修正缺陷（逐条证据见 19 §2.4）：① 伪源码块替换——33（5 块，含整块伪造的 `NMT_ServerTravel` 路径）、34（2 块）、38（5 处，含两个完全重复小节合并）；② "引擎中不存在"的 API/命名——09 `MaxReplicationDistanceSquared`、`net.MaxActorsPerFrame`、`FRepState::DynamicBuffer`、`SetIsReplicatedDormant`；02 `FGCReferenceTokenStream`、`FGCFrameData`、`PurgeObjectsAndRecordsInSlot`、`MarkPendingKill`、`FUObjectItem::SetUnreachable`；03 `UWorld::SpawnActor_Internal`、`TickFunction.h`；04 `APlayerController::Possess`、`AGameModeBase::SpawnPlayActor`；33 `DelayIncomingPacket`、`ShouldSimulatePacketDelay`；34 `NetConnection->IsSaturated()`；38 `bAllowAsyncExecution` 与 4 条控制台命令；10 `FRHICommandListExecutor::Execute`；③ 语义/默认值错误——34 优先级"降序"实为**升序**、`ReplicateSingleActor` 参数个数；33 连接超时 15s → `BaseEngine.ini` 60s 且真实链路是 `GetTimeoutValue()`→`Tick`→`HandleConnectionTimeout()`；38 种子链实为 `GetSeed`+`ComputeSeed`。
- 写作纪律：所有新增代码块由脚本按行区间**从 checkout 机械抽取**（零转录），替换处标注「（2026-09-14：原示意块已替换为 5.8 源码逐字版）」；代码围栏内行尾空白统一剥除（沿用 Lyra 附录既有约定）。
- 门禁：`check_okf.ps1 -Mode Changed` PASS；`git diff --check` exit 0；`check_repo.ps1` 的 FAIL 项与 R5 相同（既有问题，见 review-queue `R5-UE-VERIFY-01`）。
- Manifest：仅重算本轮改动文件（9 篇 + README + 19）的 bytes/lines。
- 边界：静态源码阅读，未验证运行态；行号以 5.8 源码 checkout 为准，安装版 5.8.0 可能相差数行；未提交、未推送。
- 教训：① **子代理报告不等于验收**——本轮复核发现某篇子代理报告中的"`RefLink` 在 5.8 零命中"结论有误（`RefLink` 实际存在于 `CoreUObject\Public\UObject\Class.h:551`），该错误未写入正文，但说明"不存在"类断言必须给出检索范围与命令，且必须由整合者复核。② **审计脚本的"归因"列不是缺陷证据**——第一版脚本取"块前 4 行里第一个真实存在的路径"，会被正文提到的其它文件（被反驳的旧路径/调用点/.h 声明）劫持，本轮因此产出 8 处"引用错文件"假阳性；逐处回查确认**文章引用句本身都正确、无需修改**，脚本已改为"只接受带目录分隔符的路径 + 取窗口内最后一个非否定语境的路径"，低命中块从 8 降到 2（且属真实边界：多文件引用、引用句用"同文件"指代）。低命中块必须人工读引用句原文再定性。③ `core.autocrlf=true` 下工作区 CRLF 与索引 LF 属正常，`git diff` 不显示 EOL churn——不要据此"修正"换行。

# 结构缺陷修复轮次（DEFECT-FIX-2，2026-09-24）

- 范围：12 篇定点修复（写前基线 `.kb_work/defect-fix2-baseline-20260924-155816/`）。
- 空节（7）：00-07/01 与 服务端06/12 术语表移回 `术语速查`；测试08 服务端持久化条目、测试09 安全回归条目、测试10 发布验收流程图归位到各自空标题下；14-UMG `## 七、关联阅读` 补文末 7 条链接；AI/04 `## 3.x`→`### 3.x`、`### 3.x.y`→`#### 3.x.y`（3+12 处）。
- 重复标题：15-物理 Q7 归位 FAQ 并去重 `十二、关联阅读`；16-音频删除 43 行重复块（保留首段架构表与 `OnProcessAudioStream` 独有正文，围栏 49/49 平衡）；00-可观测性第二处 `告警治理` 改名 `交付检查：告警治理`。
- AOI 三角回链（3）：算法03-03→RepGraph兴趣管理、服务端06-05→RepGraph兴趣管理、服务端06-05→算法03-03；复扫 6 向均 ≥2（含分工声明行）。
- 复扫结果：知识域空节 0；重复 H2 0；AOI 全双向。
- 门禁：`check_okf` Changed 145/145 PASS、Strict 623/623 PASS；`check_repo` FAIL 0 PASS（WARN 36 均为豁免类短正文）；`check_architecture` PASS（manifest 625 精确路径）；`git diff --check` exit=2，仅 15 篇 L143/L146 两处 space-before-tab（与 UE 5.8 World.cpp 逐字一致的既有项，不修）。
- 未执行（待用户）：`TAXONOMY-PILOT-01`、`R6-UE-ENRICH-01` 仍为 Pending。未提交、未推送。


# 进入链路证据纠正与JIP合同复验（2026-10-04）

- 上文2026-09-11记录按原字节保留，但83个选定用例不支持常量时间、跨DS隔离、完整JIP正确性或端到端L4的原结论。当前[进入链路](../知识/07-网络与游戏服务端/会话身份与在线服务/01-角色进入游戏完整链路.md)保持L2；票据/Session/DS的定点修订见[PR23](https://github.com/fantuan812/learning/pull/23)及[证据范围](../evidence/tests/entry-core/README.md)。
- JIP修订以固定复制scope和R→T为合同：零增量也安装、缺头/中洞/缺尾不假成功、完整同步身份/实体generation、有界pending/span/等待、当前identity+T的ACK。每次Install/Deliver的正常错误不提交部分连续段；批CatchUp保留先前成功前缀，不宣称全批事务或内存异常强保证。
- 最终JIP strict与UBSan各143项通过，runner各42项；3个既有安全模型负控与3个JIP语义负控均真实编译并产生预期FAIL/非零。原始结果见[strict manifest](../evidence/tests/entry-core/results/2026-10-04-jip-contract/strict/run-manifest.json)与[UBSan manifest](../evidence/tests/entry-core/results/2026-10-04-jip-contract/ubsan/run-manifest.json)，不从测试数推出所有输入正确。
- 撤回上文旧CPU必然更贵、泛化10倍带宽和指纹等同byte-identical的结论：旧对照终点/析构/采样口径不一致，32字节乘数量不是实际网络编码。新模型比较完整字段，不再运行旧计时；fresh JIP与warm resume分开讨论，本次没有新CPU/网络性能结论。
- 原4份历史raw、先前安全模型raw和用户书籍/日志/附件保留。尚未验证真实网络、UE、并发快照捕获、持久化、跨进程身份唯一性或客户端ACK诚实性；仓库检查仍单列26项已知保护来源缺陷，不能写成零缺陷或全质量通过。


# 伤害结算字段、数值与DOT合同纠正（2026-10-04）

- 保留KD-028及上文历史记录原文，但旧15条局部断言未覆盖同实例DOT小步丢相位、toHp虚报请求伤害、raw/mitigated阶段错位、非有限/中间溢出与极大护甲问题；已原位修订[模型与独立测试](../evidence/tests/damage-core/README.md)。主文以L2表达设计范围，局部实验不代替完整技能/引擎链路。
- 当前模型区分名义命中/盾分流、实际存储盾损和HP差、过量及有符号舍入残差；护甲直接稳定计算剩余系数。完整输入/状态和中间范围校验后才提交状态与临时RNG。保留真伤只跳护甲、减伤后盾前cap、lastOverride胜、最终半跳弃掉等原玩法选择，不把纠错当重平衡。
- DOT采用微秒uint64相位/计数，避免phase+active和总跳数溢出，整批计划后提交；double秒适配以精确binary64值量化，双limb整数积修复二次舍入。相同整数时间轴的分块不变性不外推到每次独立量化的任意double，也不把DOT原额汇总说成逐跳伤害管线。
- 本轮实际严格O0+NDEBUG、O2、UBSan各185项通过，原28项反例期望经薄适配仍28/0；Python优化模式及Bash/Linux pwsh等运行器故障/拒覆盖合同106项通过，四种实际源码变异被非零捕获。原始阶段流与必要复现夹具按字节范围及SHA保存在[本次manifest](../evidence/tests/damage-core/results/2026-10-04-damage-contract/run-manifest.json)，测试数只表示这套有界输入，不是全域或形式证明。
- 新增Linux CI步骤，要求执行三构建与运行器负控，必需g++-14、失败向上返回并留独立诊断/时限。本地同一PowerShell命令块已重跑；编译器合成return23使整步非零且后续命令不执行。Linux pwsh不代表Windows C++通过。
- 历史[damage_pipeline.txt](../evidence/tests/damage-core/results/damage_pipeline.txt)保持原字节。旧500×1000计时的分位数属于批均摊成本，撤回单次P99、线上一万次0.14ms与公式通常不是瓶颈的推断；本轮不新增CPU数字。20Hz对应50ms，实际子预算仍需完整工作负载测量。
- 所有既存实验raw和282份书籍/日志/附件保留；没有访问私有UE源码或真实服务。尚未验证UE/GAS/PIE、真实网络/持久化/归属、并发、Windows、跨平台确定性或生产容量；仓库机械检查仍单列26项已知保护来源缺陷，不是零缺陷或全质量通过。
