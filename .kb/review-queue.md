---
type: Decision
title: "Needs Review"
description: "置信度不足或需要用户授权的知识库结构与迁移候选。"
tags:
  - review-queue
  - knowledge-base
  - okf
status: stable
verified: []
maturity: L2
updated: 2026-09-14
---

# Needs Review

> 知识成熟度：L2（低置信度操作队列，人工确认后执行）。

低于 0.75 confidence 的结构性操作登记在此，人工确认后才执行。

> 条目中的 Current/Suggested 保留问题发现时的历史快照；只有 Status 为 Pending 的有效条目才可触发后续操作。已执行/已决策条目不可按旧快照重复执行。

## W4-ENTRY-01（角色进入游戏链路与证据）

- **状态**：已完成（2026-09-11），本条目保留用于复核与后续集成测试排期。
- **产出**：`系统实战/01-角色进入游戏完整链路.md`（L4，15 步闭环）、`evidence/tests/entry-core/`（4 程序 83 断言全通过）。
- **必读结论**：① 验签比较必须常量时间；② 幂等需"请求级 + 玩家级"两层，否则客户端换 requestId 重试会多占名额；③ 失败必须立即终止链路（本轮真实缺陷：分配失败后仍走完 Travel/Load/Spawn，产出"Ready 但未连上"的会话）；④ 名额在分配成功时即占用，租约 + 宽限期 + 栅栏令牌三件套缺一不可；⑤ JIP 乱序包必须重排缓冲，丢弃会造成静默不一致；⑥ 增量追赶 = 快照拷贝 + 重放，CPU 上永不比全量便宜，只在"世界大、缺口小"时省带宽。
- **待办（未完成，需环境）**：网关 + DS 双进程集成测试、弱网/丢包/乱序下的进入成功率、同玩家并发进入、千级玩家批量登录压测、与 `08-跨Zone与跨服迁移` 打通后的重入路径。
- **不可外推边界**：密码学实现未经侧信道审计（生产须用成熟库 + KMS）；状态机与分配器为单线程模型；带宽为 32B/条估算而非线上包体实测。

## W4-PROFILING-01（性能问题定位链路与插桩开销证据）

Current: 性能定位只有方法速查（`笔记/插桩测试`、`笔记/perf性能分析`）与工具介绍（游戏知识 07-03、00-07-03），缺少"症状 → 基线 → 归因 → 补丁 → 复测"的纵向链路；"热路径别打日志"等经验没有量级依据。

Suggested: 建纵向链路 + 量化插桩开销：固定插桩样式开销、帧预算换算、卡顿检测规则与预算降级状态机，并用本机证据固化。

Confidence: 0.95

Reason: 没有量级的性能建议无法参与预算决策；没有固定规则的卡顿检测会出现漏检与误报，报警数量与真实问题数脱钩。

Suggested action: 建证据 + 新建链路正文

Status: 已执行（2026-09-11：`evidence/labs/profiling` 17 条断言全通过；`系统实战/10-性能问题定位完整链路` 落地为 L4；决策见 KD-029）

---

## W4-DAMAGE-01（伤害与属性结算链路与证据）

Current: 伤害结算只作为 `03-技能释放完整链路` 的"步骤 7 Damage Pipeline"一节存在；乘区顺序、护盾/过量/上限/DOT 取整等边界没有独立正文，也没有可运行断言。

Suggested: 单独成篇并配可运行证据：固定乘区顺序与边界语义，给出结算基准与确定性重放证据，03 反向链接。

Confidence: 0.94

Reason: 伤害结算是策划表/客户端/服务端/战斗日志四方对数字的唯一交汇点，事故多来自顺序约定；没有独立正文与断言，就只能在事故后靠日志猜。

Suggested action: 建证据 + 新建链路正文

Status: 已执行（2026-09-11：`evidence/tests/damage-core` 15 条断言全通过；`系统实战/11-伤害与属性结算完整链路` 落地为 L4；决策见 KD-028）

---

## SCOPE-TOOL-DIR-01（工具目录污染知识扫描范围）

Current: `.workbuddy/memory/*.md`（Agent 工作记忆）未被 Git 忽略，被 `get_kb_markdown.ps1` 当成知识正文纳入扫描：目录缺 README、正文缺成熟度、缺 frontmatter `type` 三项同时 FAIL，并写入 `.kb/manifest.yaml`。

Suggested: 把 `.workbuddy/` 加入 `.gitignore`、`architecture.json` 的 `excluded_roots` 与 `get_kb_markdown.ps1` 的排除规则；工具状态一律不进入知识范围。

Confidence: 0.95

Reason: `.workbuddy/` 是 Agent 项目数据与记忆目录，不是知识内容。若改成在成熟度门禁里豁免该路径，只会让同类工具产物继续污染清单与统计，问题被掩盖而非解决。

Suggested action: 从扫描范围排除根目录（而非豁免文件类型）

Status: 已执行（2026-09-11：三处排除已落地，manifest 重建后 589 篇、漂移 0；决策见 KD-027）

---

## W4-GAMEPLAY-01（Gameplay 可运行证据与背包链路）

Current: `系统实战/` 缺背包道具链路；`evidence/tests/` 为空（执行方案 6.1 曾规划 `tests/{skill,buff,reconnect}`）；技能与 Buff 链路因缺少可运行证据停在 L3。

Suggested: 新建 `evidence/tests/gameplay-core/`（背包事务 / Buff 冲突 / 技能管线 / 属性聚合基准），据此落地 `系统实战/05-背包道具完整链路`（L4），并给 03/04 补本机证据小节。

Confidence: 0.9

Reason: Gameplay 工程师最依赖的四类机制（物品写入、Buff 交互、技能请求门禁、属性聚合）此前只有结论没有证据；本机已有可用 MinGW 工具链，可产出真实测试与基准。

Suggested action: 建证据 + 新建链路正文

Status: 已执行（2026-09-11：4 程序 29 条断言全通过并归档原始输出；05 链路落地为 L4；03/04 补证据小节；决策见 KD-027）

---

## SOURCES-GATE-01（来源层成熟度门禁豁免）

Current: `读书笔记/` 173 篇是外部导入的书籍/专栏精读材料，`architecture.json` 已把它们登记在 sources 责任层，但 `check_repo.ps1` 的知识成熟度门禁只豁免 `工作日志/`、`笔记/`、`方案/`，导致 164 篇来源材料缺成熟度行成为 FAIL。

Suggested: 按既有分层在门禁中增加 `读书笔记\*` 豁免；来源材料的内容质量按来源完整性与可追溯性衡量，不按 L0~L5 证据深度标注。结构门禁（围栏闭合、相对链接、README 清单）对来源层继续生效。

Confidence: 0.95

Reason: 架构说明已声明“按来源保留上下文，不建立第二份主题权威正文”“书籍笔记不成为第七个主题域”；对来源摘录补标证据深度既缺证据支持，也会产生虚假质量信号。

Suggested action: 修改门禁豁免范围 + 登记决策

Status: 已执行（2026-09-10：`check_repo.ps1` 增加豁免，`.kb/taxonomy.yaml` 与 `references/知识库架构.md` 同步登记；决策见 KD-026）

---

## STRUCT-01（来源材料未闭合围栏与清单缺链）

Current: 42 篇来源材料存在未闭合代码围栏（集中在 GameAIPro 与游戏引擎架构精读），2 篇来源 README 缺章节链接，2 个来源文件名含乱码 `â`。

Suggested: 按语义修复围栏（误置开栏删除、代码/图表块补闭合），把缺链章节改为本地链接，规范化乱码文件名。

Confidence: 0.9

Reason: 未闭合围栏会使后续整篇渲染为代码块；清单缺链使章节无法从 README 进入；乱码文件名影响检索与引用。

Suggested action: 逐文件修复

Status: 已执行（2026-09-10：围栏 42/42、缺链 2/2、乱码命名 2/2 修复；未改动正文文字，仅 2 个文件重命名）

---

## OKF-MIGRATION-01（legacy 全量 frontmatter 迁移）

Current: 全库采用 hybrid 模式；新建或本次修改文档受 Changed 门禁约束，未触碰 legacy 文档可读但尚未全部包含 OKF `type`。

Suggested: 在独立批次对 legacy 文档进行语义分类、type 复核、来源/验证事件补录与 profile-scope Strict 验收；禁止只按目录机械推断全部 type，也不把轻量 lint 冒充通用 OKF parser。

Confidence: 0.65

Reason: 全量迁移会改写数百篇文件；type、verified、sources 需要语义证据，机械补字段会制造虚假质量信号和大面积 diff。

Suggested action: 分域迁移 + 独立审计（需用户确认批次）

Status: 已执行（2026-08-20：用户明确“继续做迁移”；369 篇 legacy 按 5 批完成，profile 为 Scanned 390 / Conformant 390 / Legacy 0 / Excluded 2，Strict PASS）

Outcome: 每篇仅新增 7 行最小 frontmatter；330 篇正文去前缀 SHA-256 与迁移基线一致，其余 39 篇以 `+7/-0` 和前缀门禁证明正文未改。未补造 `sources`、`generated` 或验证事件。

Historical note: 本条的 Current/Suggested 是执行前快照；当前事实只以本条 Status/Outcome、KD-015 与最新 audit 为准，不得据旧措辞重新触发迁移。

---

## OBSIDIAN-CONFIG-01（共享 `.obsidian` 配置范围）

Current: `.obsidian/app.json`、`appearance.json`、`core-plugins.json`、`graph.json` 已归档纳入版本控制（commit d4ca7c3），`.gitignore` 精确忽略 `workspace.json` / `workspaces.json` 设备状态。

Suggested: 保持当前轻量共享策略；若后续引入社区插件或新核心插件，逐项审计后再提交。

Confidence: 0.90

Reason: core plugin 清单（bases/properties/sync/graph 等）已稳定生效，设备工作区状态已隔离，无冲突风险。

Suggested action: 维持现状，按需增量维护

Status: 已执行（2026-09-02：归档基础配置并固化 .gitignore 忽略规则）

---

## TAXONOMY-PILOT-01（物理目录重构试点）

Current: 第一阶段已选择 virtual-first，现有六大领域继续作为 Canonical；领域 MOC、跨域主题与 Base 承担学习顺序和横向关系，正文路径未移动。

Suggested: 第一阶段稳定后，只选择一个小域（优先评估 `游戏测试与质量`）或一个单一语义族做物理试点；试点前必须生成 old→new 映射、唯一 Canonical、全部入链/出链、README/MOC/manifest 影响和逐批回滚清单。

Confidence: 0.65

Reason: 已确认少数跨域歧义，但尚无证据证明物理搬迁收益高于链接与历史风险；需要用小批试点数据验证。

Suggested action: 用户批准后独立试点；不得与 Split/Merge 或正文重写同批执行

Status: Pending

---

## LYRA-COV-01（LyraGame/UI 专项缺口）

Current: LyraGame/UI（79 文件）仅 3 覆盖（4%）。43 篇的"UI"实为插件注入机制；Lyra 自有控件（HUD 布局/Foundation 控件族/IndicatorSystem 头顶指示器/Weapons UI 准星与命中标记/LyraUIManagerSubsystem/LyraSettingScreen）完全未分析。

Suggested: 补 1 篇专项（量级与 43 篇相当，编号 49 或并入 43 扩展）。

Confidence: 0.85

Reason: R4-LYRA-COVERAGE 审查；UI 是游戏表现层实际内容，不属间接覆盖。

Suggested action: 新建专项篇

Status: 已执行（2026-08-14：新建 49-Lyra-UI控件与表现源码.md，558 行正文 + 11 文件附录逐字一致，12 README/19 路线图/39 总览同步）

---

## LYRA-COV-02（Plugins/GameSettings 专项缺口）

Current: GameSettings 插件（59 文件）0 覆盖。它是 Lyra 设置系统唯一实现（GameSetting/Registry/Value*/Action/Collection 抽象），LyraSettingScreen 直接搭载；LyraSettingScreen/GameSettingRegistry/LyraSettingsLocal 全库零提及。

Suggested: 补 1 篇专项（或并入 UI 专项篇的设置章节）。

Confidence: 0.85

Reason: R4-LYRA-COVERAGE 审查；设置系统是 Lyra 区别于裸引擎的完整子系统。

Suggested action: 新建专项篇

Status: 已执行（2026-08-14：新建 50-Lyra-设置系统与GameSettings源码.md，342 行正文 + 10 文件附录行数全 OK，12 README/19 路线图/39 总览同步）

---

## LYRA-COV-03（空心覆盖标注 + 39 插件地图补全）

Current: 44-CommonUserSubsystem.cpp（2684 行）/42-RangedWeaponInstance/41-ALyraCharacter 等附录收录但正文分析不足（空心）；39 篇插件地图缺失 GreenRoom/RedRoom/LyraExampleContent/LyraExtTool。

Suggested: 附录清单注明"仅收录未深析"；39 插件地图补 4 个缺失插件名；12 README/19 路线图新增覆盖边界声明列。

Confidence: 0.75

Reason: R4-LYRA-COVERAGE 审查；空心标注与地图补全是低风险增量编辑。

Suggested action: 增量编辑

Status: 已执行（2026-08-14：39 插件地图补全 16 插件；41/42/44 附录补覆盖边界声明（空心标注）；12 README 覆盖口径 47→49 篇）

---

## LYRA-BATCH-01（批次 1+3：GAS 扩展新篇 + 三篇补深挖 + 边界声明）

Current: R4-LYRA-COVERAGE 建议中优先项（AbilitySystem 未覆盖子集/Feedback NumberPop/Messages 协议/Weapons 实例）与 12 README/19 路线图边界声明（批次 3）。

Suggested: 新建 51 篇（AbilityCost/AttributeSet/CombatSet/HealExecution/TagRelationshipMapping/GlobalAbilitySystem/GameplayCueManager/Jump/Reset）；42/43/49 分别追加 Weapons 实例、VerbMessage 消息协议、NumberPop/ContextEffects 章节；边界声明三分类 + 批次 2 登记。

Confidence: 0.9

Reason: 2026-08-14 用户批准执行批次 1 + 批次 3；全部为可复用范式价值项，不追求穷举覆盖。

Suggested action: 新建 + 追加 + 声明

Status: 已执行（2026-08-14：51 篇 551 行正文 + 附录 A 22 文件逐字一致；42 篇 +5 附录、43 篇 +5 附录、49 篇 +6 附录逐字一致；12 README/19 路线图/39 总览 50 篇口径 + 51 登记；manifest 326 条目同步；覆盖率 35.4%→40.5%；check_repo PASS）

---

## LYRA-BATCH-02（批次 2：CommonGame UIManager / GameFeatureAction 家族 / Interaction / Animation）

Current: 边界声明登记的四项待补价值项；Interaction 全系列 0 覆盖（17 文件），GameFeatureAction 家族与 CommonGame UI 管理层仅路过提及，Animation 仅 2 文件（111 行）。

Suggested: Interaction 新建 52 篇；GameFeatureAction 家族追加 40 篇；CommonGame UI 管理层追加 49 篇；Animation（体量小）追加 41 篇。

Confidence: 0.9

Reason: 2026-08-14 用户指示"现在去处理批次2"；四项均按体量选择落点（成篇/追加），不强行凑篇。

Suggested action: 新建 + 三篇追加

Status: 已执行（2026-08-14：新建 52 篇 600 行正文 + 17 附录逐字一致；40 篇 +529 行正文 + 6 附录、49 篇 +390 行正文 + 6 附录、41 篇 +149 行正文 + 2 附录逐字一致；12 README/19 路线图/39 总览 51 篇口径 + 52 登记、边界声明批次 2 完成；manifest 327 条目同步；覆盖率 40.5%→42.9%；check_repo PASS）

---

## LYRA-DUP-01（46/47 BotCheats 重复）

Current: ULyraBotCheats 与 ULyraDeveloperSettings（机器人数目字段）在 46 §八.1-8.2（正文级）与 47 §八/§7.2（正文级详解）重复覆盖，互不转发；BotCheats.h/.cpp 附录仅收 46。

Suggested: 46 §八 收敛为"机器人组件如何被 Cheat 驱动"的机器人视角并显式转发 47（调试域详解），附录保留（KD-004 完整收录）。

Confidence: 0.85

Reason: KD-005 调试主题归 47；双篇同组件详讲使读者无法判定权威章节（R4-LYRA 专项审查）。

Suggested action: 收敛 + 转发（正文编辑，附录不动）

Status: 已执行（2026-08-14：46 §八 补分工声明与 47 转发，正文保留机器人视角，附录未动）

---

## LYRA-LINK-01/02/03/LOG-01（Lyra 导航与口径）

Current: 19 路线图 L189"39-44"过时；40-43 未链 47/48；44 重复链 47 缺 45；40-42 更新日志未登记 48、47 日志"58 文件"陈旧。

Suggested: 19 路线图改 39-48；40/41/42/43 关联阅读补链 47/48；44 去重 47 补 45；40-42 更新日志补登记 48、47 日志改"33 + 25 迁出"。

Confidence: 0.9

Reason: R4-LYRA 专项审查；均为低风险增量编辑，无内容删改。

Suggested action: 批量增量编辑

Status: 已执行（2026-08-14：19 路线图 L189 改 39-48；40-43 补链 47/48；44 去重 47 补 45；40-42 更新日志补登记 48、47 日志口径修正；45/46 补链 48；47 术语表/失败模式表插件残留清理）

---

## R4-OBS-01（性能分析族笔记无正式家）

Current: 笔记/插桩测试.md、笔记/perf性能分析.md 为通用服务端/Linux C++ 插桩与 perf 采样方法（与 游戏知识/07-03 性能分析 无内容重叠，后者为 UE stat/Insights/ProfileGPU 专用），自述应归六大知识域但无 canonical 正文吸收。

Suggested: 将插桩/perf 方法论 Extend 进服务端侧性能 canonical 文档（如 服务端AI与性能 或新建服务端 Profiling 篇），或 Archive 笔记并依赖既有引用。

Confidence: 0.6

Reason: R4 语义重复审计发现；两笔记内容独立、无重复，但缺少正式归属，属结构性增补/归档决策。

Suggested action: Extend 或 Archive（需用户确认方向）

Status: 已执行（2026-08-14：新建 canonical 00-07/03-性能工具：插桩与perf采样.md，笔记收敛为速查并加正式指向）

---

## R4-DUP-01（背包族语义重叠）

Current: 游戏知识/03-游戏玩法编程/13-背包与装备系统.md（客户端/Lyra/SaveGame/UI 视角）↔ 游戏服务端/03-业务系统设计/01-背包与道具系统.md（事务/锁/幂等/SQL 视角）。

Suggested: 核心建模/槽位/堆叠/同步收敛到单一 canonical 点，各自保留 UE 表现层与服务端事务层独有侧重；补双向互链与分工声明。

Confidence: 0.85

Reason: 两篇同为"背包系统权威参考"，双层数据模型/容器槽位/堆叠/网络同步主题重合，但双向无任何引用或分工声明（R4 语义重复审计）。

Suggested action: Split+Merge + 补互链

Status: 已执行（2026-08-14：分工声明+双向互链收敛，内容未删减）

---

## R4-DUP-02（A* 笔记收敛）

Current: 笔记/A星算法优化.md（豁免速查）被系统实战/07 §2.1 当作"A* 请求"依赖引用，内容（二叉堆/四叉堆/JPS/HPA*/funnel/Tick 预算）与 canonical 篇 游戏算法/01-寻路与图论/02-A星算法与优化.md §6/7/8/10/12 高度重合。

Suggested: 服务端工程特有角度回填 canonical 篇后，笔记保持"纯速查 + 正式指向 canonical"或归档。

Confidence: 0.72

Reason: 笔记属豁免类（不强制），但被动承担正文角色形成语义重复。

Suggested action: Extend + Archive 收敛

Status: 已执行（2026-08-14：canonical 补服务端工程指路，笔记补正式指向）

---

## R4-DUP-03（DS 部署篇轻度漂移）

Current: 游戏服务端/05-UE Dedicated Server平台化/02-Linux DS部署与容器实战.md §2/§3 重述 Linux 交叉构建/无头运行，与 游戏知识/08-工具链与打包发布/09-UE Dedicated Server构建烘焙与运行.md 重叠；互链单向（08-09 未回链 05-02）。

Suggested: 05-02 收窄到部署/容器/信号/资源平台视角，构建机制统一指向 08-09。

Confidence: 0.6

Reason: 属 README"以工具链为边界"声明的轻度漂移，非紧急。

Suggested action: Keep+Refactor

Status: 已执行（2026-08-14：05-02 补分工声明，08-09 补回链）

---

## DEDUP-01（39-47 附录去重）

Current: 39-47 九篇附录跨篇重复粘贴 16 个源码文件（7 对文章；LyraGameInstance、LyraAbilitySet 各出现 3 次）。

Suggested: 附录改为"单篇收录 + 他篇链接"，或抽取共享附录文件。

Confidence: 0.9（重复事实）/ 0.5（是否执行）

Reason: 用户此前明确要求"每篇都出现完整源码，而不是指向源码在哪"；去重方案与该要求冲突，需用户确认后才可执行。

Suggested action: 保持现状或按用户新决定执行

Status: 已决策（维持现状）

> 2026-08-14 决策：**维持现状**（KD-004）。用户此前明确要求"每篇都出现完整源码，而不是指向源码在哪"；去重方案与该要求冲突，不执行。跨篇重复作为已知成本记录，后续若用户改变要求再执行。

---

## R2-SPLIT-01（AI 03-01 拆分）

Current: 游戏AI/03-评测与安全/01-AI评测回放与LLM安全.md（2036 行双主题）。

Suggested: 拆为《AI 评测与回放》《LLM NPC 安全》两篇；§四 回放与确定性压缩为指路节并与算法 04-05 双向互链。

Confidence: 0.75

Reason: §七（LLM NPC 安全，约 395 行）可独立成篇；§四 与算法 04-05 实质重叠仅单向链。

Suggested action: 拆分（需迁移计划）

Status: 已执行（2026-08-14：01 收窄改名《AI评测回放》1649 行 + 新建 03-LLM-NPC安全 419 行，§七 逐字迁移 SHA-256 一致，全库 13 处引用同步）

## R2-MOVE-01（07-05 调试工具归属）

Current: 游戏知识/07-UI与性能优化/05-GameplayDebugger与运行时调试.md。

Suggested: 迁至引擎基础/独立调试分类，或 README 补覆盖声明（33KB 通用运行时调试内容，README 覆盖范围未提调试主题）。

Confidence: 0.7

Reason: 分类名与内容错位；迁移涉及跨分类导航与链接更新。

Suggested action: 迁移 / 声明

Status: 已执行（2026-08-14：采用低风险方案——07 README 补"运行时调试"覆盖声明，文件不迁移）

## R2-GATE-01（成熟度字段入门禁 + 批量补标）

Current: 全库约 129 篇正文缺"知识成熟度"行；check_repo.ps1 不检查该字段，只查知识基线/最后更新/官方链接。

Suggested: check_repo 增加"本次新增/修改正文必须带成熟度行"（已有逻辑）扩展为"既有正文缺成熟度按目录分批 FAIL 阈值"；同时按 L0-L5 口径批量补标约 129 篇。

Confidence: 0.9（门禁缺口事实）/ 0.7（是否本轮执行）

Reason: 写作规范 §2 要求每篇标注；门禁与规范脱节导致欠账长期不可见。

Suggested action: 先补标后加固门禁（或反向，需用户定序）

Status: 已执行（2026-08-14：check_repo.ps1 阶段 A → 阶段 B，既有正文缺成熟度由 WARN 升级 FAIL；全库复跑 FAIL 0）

## R2-MANIFEST-01（manifest 索引填充）

Current: .kb/manifest.yaml 为空骨架（documents: {}）。

Suggested: 由 kb_scanner 回填文档索引（路径/域/成熟度/大小），作为 RAG 与后续审计基线。

Confidence: 0.8

Reason: Phase 1 要求维护 manifest；空索引使控制面无法回答"仓库当前有哪些文档"。

Suggested action: 填充（机械生成，主线程执行）

Status: 已执行

> 2026-08-14 决策：**已执行**（R3）。机械回填 320 个文档（kind/maturity/bytes/lines），版本升至 2；check_repo 复跑 PASS。

## R2-DEDUP-01（概念层双写收敛）

Current: 01-07/08/09 与 01-04、02-03/04/08/10、04-01/02、04-00 FAQ/清单双源、08-08 与服务端 04 系列等多组概念层重复详述。

Suggested: 主责篇保留详述，他篇收敛为摘要+链接；04-00 问答/清单改为单一来源（分篇）并注明同步规则。

Confidence: 0.7-0.95（按组）

Reason: 收敛涉及正文删改与互链，属于结构性去重；与"每篇完整可读"的既有要求需平衡。

Suggested action: 分批去重（先 exact 级：01-08/09 对比表、04-00 FAQ）

Status: 已决策（维持现状）

> 2026-08-13 决策：**维持现状**（KD-004）。用户此前明确要求"每篇都出现完整源码"；去重方案与该要求冲突，不执行。跨篇重复作为已知成本记录，后续若用户改变要求再执行。

## SPLIT-01（47 篇拆分）

Current: 47-Lyra-调试工具与扩展源码.md（正文 1522 行，20+ 组件）。

Suggested: 拆分"Lyra 扩展插件"为 48 号独立篇，或声明为目录级速查篇。

Confidence: 0.6

Reason: 粒度远粗于 40-43 单一链路文章；拆分影响系列编号与导航。

Suggested action: 拆分 / 维持

Status: 已执行（KD-007，2026-08-13：47 拆出 48 插件篇；本条目与下方合并条目重复，保留为历史记录）

## SPLIT-02（算法 04-01 拆分）

Current: 游戏算法/04-确定性与基准工程/01-动态寻路确定性与基准测试.md（1832 行）。

Suggested: 按 §一~§六 拆为 3-4 篇（动态寻路 / NavMesh 工程 / 避障与数值鲁棒 / 确定性基准）。

Confidence: 0.65

Reason: 单篇承担 4-6 个独立篇目的工作量，分类名盖不住内容。

Suggested action: 拆分 / 改名"寻路工程与确定性"

Status: 已执行（KD-007，2026-08-13：算法 04 拆为 01-总览 + 02-05 四篇；本条目与下方合并条目重复，保留为历史记录）

## SPLIT-03（服务端 04-01 拆分）

Current: 游戏服务端/04-平台与可靠性/01-鉴权限流幂等灾备与可观测性.md（1134 行八主题）。

Suggested: 按方案 W7-10.1 拆分，主题归还 02-数据与业务 系列。

Confidence: 0.6

Reason: 八主题合辑与 02 系列单主题篇粒度不一致；已有规划 TODO（P1 未动）。

Suggested action: 拆分

Status: 已执行（KD-007，2026-08-13：服务端 04 按 W7-10.1 拆为 00-总览 + 01-08 八篇；本条目与下方合并条目重复，保留为历史记录）

## RENAME-01（46 文件名）

Current: 46-Lyra-AI队伍与调试源码.md，H1 为"AI 机器人与队伍系统"（调试主题实际在 47）。

Suggested: 文件名改为 46-Lyra-AI机器人与队伍源码.md，或标题补充调试范围。

Confidence: 0.55

Reason: 文件名与标题/内容范围不一致；改文件名需同步 39/44/README/19 路线图引用。

Suggested action: 改名 / 维持

Status: 已执行

> 2026-08-13 决策：**已执行**（KD-005）。`46-Lyra-AI队伍与调试源码.md` → `46-Lyra-AI机器人与队伍源码.md`，13 处引用（39/40/41/42/43/44/45/47、12 README、19 路线图、03 README、46 自查命令、.kb/audit.md）全部同步。

## SPLIT-01/02/03 与 STYLE-01（拆分与标题模板）

> 2026-08-13 决策：**延后**（KD-006）。三项拆分与 H1 模板统一属于大规模结构变更，需独立计划（内容迁移清单、链接影响面、导航同步）后分批执行，避免仓促拆分破坏正文；其中 SPLIT-03 已挂靠方案 W7-10.1 的既有 TODO。恢复执行时从本队列取回。

> 2026-08-13 更新：SPLIT-01/02/03 已按 .kb/plans/current.md 迁移计划执行完毕（KD-007）：47 拆出 48 插件篇；算法 04 拆为 01-总览 + 02-05 四篇；服务端 04 按 W7-10.1 拆为 00-总览 + 01-08 八篇；正文逐字迁移并完成全库链接/导航同步。STYLE-01（H1 模板统一）仍延后。

## STYLE-01（12 目录标题模板）

Current: 01-38 的 H1 存在 4+ 套命名模板（"NN · 主题" / "UE 引擎源码分析 NN：主题" / 裸标题 / 编号内嵌）。

Suggested: 统一为"UE 引擎源码分析 NN：主题"（20-35 补编号）。

Confidence: 0.6

Reason: 影响系列归属辨识与检索；只改标题不改文件名，但涉及 20+ 文件。

Suggested action: 批量统一

Status: 已执行（KD-009）

> 2026-08-13 决策：**已执行**（KD-009）。用户"继续处理剩余事项"确认后，01-38 H1 统一为"UE 引擎源码分析 NN：主题"（33 个文件；05/06/07/38 原已达标），39-48 Lyra 系列保持"UE5.8 Lyra 源码解析 NN：主题"；全库 grep 无旧标题残留。（2026-08-14 R4 修正：该注释原错位于 DEDUP-01 条目下，已归位至 STYLE-01。）

## R5-LYRA-CORE-01（核心玩法与引擎桥接源码覆盖）

Current（历史基线，已由本条执行收口）: 39-52 已覆盖 Experience、Pawn 初始化、输入/GAS、背包/装备、UI/设置等主链，但对局生成/移动状态、项目 ReplicationGraph、模块化引擎桥、输入重映射/AimAssist、ShooterCore TDM/淘汰消息仍缺少可逐字复核的核心源码证据。

Suggested: 新建 53-56 四篇专题，按 L0/L1/L2/L3 矩阵登记真实函数片段、项目文件全文附录和运行态验证边界。

Confidence: 0.98

Reason: 核心玩法入口不能只以路径表或“请自行阅读源码”计覆盖；这些文件决定生成、移动、复制、输入辅助和淘汰反馈的真实调用链。

Suggested action: 新建 + 导航/manifest/控制面同步

Status: 已执行（2026-08-18：53 核心生成移动状态、54 网络复制与模块化引擎、55 输入重映射与辅助瞄准、56 ShooterCore 核心玩法与淘汰消息已落地；12 README/19 路线图/39 总览、manifest 和审计记录同步；运行态验证保留为 L3）

## R5-UE-VERIFY-01（12-引擎源码分析核验时发现的既有门禁失败，范围外未修）

Current（2026-09-14 核验记录）: `scripts/check_repo.ps1` 报 3 项 FAIL，经逐条核实**均为本轮之前既存**、且不在本轮 allowlist（`游戏知识/12-引擎源码分析`）：①`evidence/tests/aoi-scale/README.md` 与 ②`evidence/tests/match-core/README.md` 的相对链接断链（两文件 2026-09-11 创建、`git` 未跟踪）；③`游戏知识/06-网络同步/07-Iris复制使用与迁移.md` 正文仍引用旧路径 `ActorChannel.cpp`（该文件在 HEAD 即为当前内容，非本轮改动）。

Suggested: 修复两个 evidence README 的断链目标（或删除失效引用）；将 06-网络同步/Iris 文内的 `ActorChannel.cpp` 改为 5.8 的真实落点（`DataChannel.cpp`）或显式标注为历史名称。

Confidence: 0.9

Reason: 三项会让全库门禁长期 FAIL，掩盖后续真实回归；`ActorChannel.cpp` 旧名与本库 09 篇"旧名已移除"的口径直接冲突，同仓库内不一致会误导读者。

Suggested action: 修复（涉及 `游戏知识/06-网络同步/` 与 `evidence/`，超出本轮 12-引擎源码分析范围，需用户确认后再动）

Status: 已执行（2026-09-24：用户"先解决缺陷"授权下修复——①②两个 evidence README 断链改为指向 `系统实战/README.md` 规划表的纯文本 + 有效链接；③ 07-Iris 文内 `ActorChannel.cpp` 改为 `DataChannel.cpp` 中的 `UActorChannel`。同轮修复工作区回归 4 项（16 frontmatter、15 未闭合围栏与合并围栏行、13/14/15 版本基准）。验收：check_repo FAIL 0、check_okf Changed PASS 136/136、DS 门禁 ActorChannel.cpp 残留 0）

<!-- 模板（有内容时取消注释并填写）：
## KB000XXX

Current:

    Inbox/xxx.md

Suggested:

    Knowledge/<Domain>/<Subdomain>/<Topic>/

Confidence:

    0.68

Reason:

The document contains both general knowledge and project-specific configuration.

Suggested action:

Split

Status:

Pending
-->

## R6-UE-ENRICH-01（引擎源码分析：剩余篇章的代码块保真度与补深候选）

- **状态**：Pending（需用户授权；本轮只完成了 9 篇，其余篇章未动）。
- **背景**：本轮引入"代码块保真度审计"（抽出代码块 → 按「摘自」声明归因 → 与 checkout 逐行比对 → 未命中行全树兜底）。全库 56 篇口径：1234 个代码块中 150 个自称有出处，3403/3504 行命中（97.1%）；本轮 9 篇的 120 行未命中**全部**能在引擎其它文件中找到（自造代码 0 行），其中 8 处属"引用错文件"，已逐个改正。
- **本轮已完成**：6 篇补深（01/02/03/04/09/10）+ 3 篇伪源码修正（33/34/38）+ README/19 记录。详见 `.kb/audit.md` 的 R6-UE-ENRICH 段与 19 §2.4。
- **待授权候选（按证据排序）**：
  1. **散文体薄篇补深**：`28-UnrealInsights与Trace源码`、`29-GameplayTasks源码`、`31-ProceduralVegetationEditor源码` 等仍以"概述/核心概念/原理 + 附录源码"为主，正文缺少逐段解构（与 01/02/03/04/09/10 补深前同形）。
  2. **大系统再下钻**：`35-Nanite`、`36-AnimNext与UAF`、`37-Chaos破坏与Field` 的真实源码覆盖可以继续加深（当前以概念 + 关键入口为主）。
  3. **示意块清点**：本库仍有篇章保留"无出处声明"的示意/用户示例代码块（审计脚本把这些块单列，不判为缺陷）。若用户希望"所有代码块都必须可回溯到真实源码"，需逐篇替换——这是一次范围较大的写作决策，需明确授权。
  4. **`19` §五/§三 路线图缺口**：5.8 新增/变动系统（如 Iris 深水区、Verse/其他）尚无独立文章，属新增主题而非补深，需按组织规则先确认 Canonical 落点。
- **不可外推边界**：静态源码阅读，不验证运行态；行号以 5.8 源码 checkout 为准。

## R5-UE-VERIFY-01（承接：门禁残留与两处引用待修）

- **状态**：已执行（2026-09-24：与上方同名条目一并修复；两处断链与 `ActorChannel.cpp` 旧路径均已解决，`check_repo` 全量 FAIL 0）。
- **内容**：① `evidence/tests/aoi-scale/README.md` 与 `evidence/tests/match-core/README.md` 的断链（前者为未跟踪文件）；② `游戏知识/06-网络同步/07-Iris复制使用与迁移.md` 里 `ActorChannel.cpp` 旧路径。三项在 R5/R6 两轮 `check_repo.ps1` 中均报 FAIL，经核实为**既存**问题，不在两轮 allowlist 内，未修改。
