---
type: Concept
title: "02 感知系统与 EQS"
description: "UE AI 感知的来源、刺激与遗忘语义，以及 EQS 的过滤评分、上下文、结果生命周期和黑板接入。"
status: stable
verified: []
maturity: L2
updated: 2026-10-09
sources:
  - id: epic-ai-perception
    title: "AI Perception"
    resource: "https://dev.epicgames.com/documentation/unreal-engine/ai-perception-in-unreal-engine"
  - id: epic-eqs-tests
    title: "EQS Node Reference: Tests"
    resource: "https://dev.epicgames.com/documentation/unreal-engine/eqs-node-reference-tests-in-unreal-engine?lang=en-US"
  - id: epic-eqs-request
    title: "FEnvQueryRequest"
    resource: "https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/FEnvQueryRequest?lang=en-US"
  - id: epic-eqs-result
    title: "FEnvQueryResult"
    resource: "https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/FEnvQueryResult"
---
# 02 感知系统与 EQS

> 知识成熟度：L2（Epic 公开文档和 API 的静态核对；示例未编译、未运行，不代表玩法或性能已验证）。
> 版本基准：2026-10-09 可访问、页面标题标为 Unreal Engine 5.8 的 Epic 文档/API；公开页面不是本地引擎 checkout 或固定源码 revision。
> 知识基线：以文末逐项来源支持的公开合同为限；AI 感知、EQS 查询和项目自己的目标选择/记忆策略分开负责。
> 历史版本声明：旧文记载 UE 5.8.0、CL 55116800、`++UE5+Release-5.8` 及本机 `Engine/Build/Build.version`。本轮未访问该环境，不能用公开 5.8 页面认证这个 CL、分支或任何本机核对结果。
> 适用范围：AIController 管理感知、黑板与项目决策，当前受控 Pawn 作为本文 EQS 请求 Owner/Querier。导航、碰撞、队伍关系和权威执行位置由项目配置。其他 Querier 类型须另定上下文解析合同；不承诺 UE4.27/早期 UE5 无修改兼容。
> 最后更新：2026-10-09（纠正来源注册、丢失/遗忘、过滤评分、查询结果接收与退出责任）。
> 验证边界：仅查阅资料并静态检查文档。没有 UE/UHT/C++ 编译、蓝图/PIE、AI/网络/设备/设置实验、纸例执行或压力测试；文中的验证步骤均为待执行计划。

## 1. 两个系统各自回答什么问题

AI Perception 回答“这个 AI 收到了谁的哪一种感官信息”；EQS 回答“给定候选和上下文，哪些选项满足约束，剩下的谁更合适”。目标是否活着、是否可攻击、是否仍属于本次任务，以及该不该搜索最后已知位置，仍是玩法决策。

例如守卫发现玩家后需要移动到射击位：感知提供目标信息，项目筛选有效敌人并写入黑板，EQS 生成位置并按可达性/视线/距离过滤评分，行为树才决定移动。EQS 给出的点也不是移动成功、掩体可用或攻击命中的保证。逃离查询可先过滤不可达位置，再把远离威胁作为偏好；巡逻查询可从项目允许的巡逻点中筛选下一个位置。它们同样需要有效上下文、结果接受和后续移动责任。

通用“感知→决策→行动”、记忆可信度与多层预算见[AI 总体架构与感知](01-AI总体架构与感知.md)。本篇专门说明 UE 的数据和接入边界。

| 对象 | 职责 | 不能混同的东西 |
| --- | --- | --- |
| `UAIPerceptionComponent` | 注册监听者，保存/更新目标的感知数据 | 不是所有感官判定算法的实现者 |
| `UAISenseConfig` | 配置特定感官，如范围、Max Age | 不是刺激来源 Actor |
| `UAISense` | 特定感官实现，如 Sight、Hearing | 不是黑板目标选择策略 |
| `UAIPerceptionStimuliSourceComponent` | 帮所属 Actor 注册为指定感官的来源 | 注册 Hearing 不等于已经发生噪声 |
| `UAISenseEvent` / 感官事件结构 | 向特定感官报告事件 | 不是 `FAIStimulus` 的同义词 |
| `FAIStimulus` | 某感官处理后给监听者的记录，含类型、位置、年龄、成功状态等 | `Strength` 不是通用置信度衰减公式 |
| `UAIPerceptionSystem` | 在监听者、来源与感官之间组织处理 | 继承 `UAISubsystem`，不是跨 World 的进程单例 |
| `UEnvQuery` / Generator / Context / Test | 查询资产、候选产生、参照数据、过滤和评分 | Context 不自动等于黑板目标 |
| `FEnvQueryRequest` / `FEnvQueryInstance` / `FEnvQueryResult` | 请求、执行中数据、完成结果 | 完成结果不是长期有效的世界快照 |
| Blackboard / BT | 存储项目知识、控制行为执行和中断 | 引擎感知不会替项目自动维护所有黑板键 |

来源：[S01](#来源与核对范围)、[S02](#来源与核对范围)、[S03](#来源与核对范围)、[S04](#来源与核对范围)、[S17](#来源与核对范围)。

## 2. 感知：先接通来源，再解释刺激

### 2.1 视觉来源与事件来源是两条入口

视觉需要监听者配置 Sight，也需要目标进入该感官的来源管理。对任意拾取物、诱饵或非 Pawn Actor，最明确的教学方式是添加 AI Perception Stimuli Source，启用 Auto Register as Source，并将 Sight 加入 Register as Source for Senses。也可用 `RegisterForSense` / `RegisterWithPerceptionSystem` 手动注册，退出时配对注销。不要用“给目标加碰撞”替代来源注册。[S03]

Pawn 的自动注册行为涉及当前感官实现及配置；本篇不把“所有 Actor 默认可见”或“每个 Pawn 必须手动注册”写成普遍前提。排查时观察实际来源，而不是依赖教程项目的默认状态。

听觉走事件报告：播放音频与报告 AI 噪声是两个用途。下面是调用片段，`NoiseMaker` 必须有效，世界、事件位置和声音归属由调用点提供；参数只是教学示例，未运行。

```cpp
#include "Perception/AISense_Hearing.h"
#include "GameFramework/Actor.h"

void ReportFootstepForAI(AActor* NoiseMaker, const FVector& NoiseLocation)
{
    if (!IsValid(NoiseMaker))
    {
        return;
    }
    UAISense_Hearing::ReportNoiseEvent(
        NoiseMaker, NoiseLocation, 1.0f, NoiseMaker, 0.0f, FName(TEXT("Footstep")));
}
```

接口顺序是 WorldContextObject、NoiseLocation、Loudness、Instigator、MaxRange、Tag；Tag 的类型是 `FName`。不要把 `MakeShared<FAINoiseEvent>` 传给一个假定存在的 `ReportEvent(World, SharedPtr)` 重载，也不要把噪声的距离限制或响度公式外推为所有感官共有的强度衰减。[S07]

Touch、Damage 可用于接触警觉和受击反应，但要说明事件桥接：项目的碰撞/伤害入口按需要调用 `ReportTouchEvent` / `ReportDamageEvent`；仅添加 Sense Config 不是“任何物理接触/伤害都已经报告”的证明。Prediction 是请求预测位置，Team 是另一种信息来源；它们都不代替项目的敌友规则。自定义气味等感官仍需配置、事件输入和感官实现配套，不能只继承一个事件类便宣称接通。[S01、S08、S09]

### 2.2 从判定到保存的因果链

1. 监听者和感官先建立配置；来源注册或事件报告提供输入。
2. 具体感官按自身算法和调度处理输入，如 Sight 检查感知几何与可见性，Hearing 处理噪声事件。
3. 感官形成刺激，监听组件处理批量到达的记录；同一目标可保留多个感官的信息。
4. 感知事件通知项目，项目重新检查有效性、队伍/玩法资格，再更新黑板或自己的目标集合。
5. 感官停止成功、刺激随时间老化、目标数据被遗忘，是三个不同层次；事件消费者不能用一个 `else` 覆盖所有情况。

`UAIPerceptionSystem` 的 Tick、AgeStimuli 与延迟刺激处理是不同入口；公开合同不支持“统一的 PerceptionTickInterval 默认 0.25 秒”。Sight 也有查询队列及同步/异步可见性相关接口，不能把“全部迹检测必定异步”当作性能保证。[S02、S04、S06]

### 2.3 Sight 与 Hearing 参数怎么读

| 配置 | 可依赖的含义 | 接入误区 |
| --- | --- | --- |
| Sight Radius | 开始发现目标的最大视距 | 不等于所有已发现目标立即失效的边界 |
| Lose Sight Radius | 对已发现目标使用的视距边界 | 通常用较大值提供距离滞回，但不免除其他判定 |
| Peripheral Vision Angle Degrees | 相对正前方的半角 | 不是整个视锥张角 |
| Auto Success Range From Last Seen Location | 非禁用时，针对已见目标相对最后见到位置的自动成功范围 | 不是围绕监听者的“近距离无视视野探测圈”；旧文 `AutoSightRange` 不采用 |
| Point Of View Backward Offset / Near Clipping Radius | 影响视锥几何的参数 | 不凭空加入未核对的 `Affects FOV` 属性 |
| Detection by Affiliation | 感官按敌人/友军/中立关系过滤 | 勾选 Detect Enemies 不会自动把玩家定义为敌人 |
| Hearing Range | 听觉配置的距离范围 | 没有据此证实通用 `Lose Hearing Range`；LoS Hearing Range 不当作遗忘时间 |
| Max Age | 该感官刺激的最大年龄配置；0 表示不因年龄遗忘 | 不是目标追踪 TTL、死亡通知或每帧衰减强度的系数 |
| Dominant Sense | 多感官信息中确定目标位置时的优先感官 | 不是只保留此感官，也不是攻击资格过滤 |

Sight 字段以 [S05] 为准；Hearing、Max Age、Dominant Sense 的使用说明见 [S01]。视线通道、目标观察点、是否采用自定义 `IAISightTargetInterface`、异步路径和预算仍需在实际引擎/项目核对，不能用一段“源码简化”冒充已读源码调用链。

### 2.4 队伍、目标生命周期与黑板所有权

纯蓝图起步可先在明确的测试场景允许中立目标进入感知，再用项目标签/接口筛选，避免“玩家没被定义为敌人”导致完全看不到。正式项目应明确监听者和目标的队伍标识如何提供、团队态度如何计算，以及运行中换队如何刷新已有感知/目标状态；`IGenericTeamAgentInterface` 是核对入口。[S01、S10]

这里建议项目约定一个目标选择者独占 `TargetActor`，其他回调只更新观察记录并请求重新选目标。目标销毁、死亡/不可攻击、换队、UnPossess 和 World 退出都要让选择者失效相应记录；`IsValid` 只检查 UObject 有效性，不表示“仍存活且可攻击”。多人项目还要指定在哪一端做决策及同步哪些结果，本次未验证网络行为。

推荐的黑板合同不是固定模板，而是一组分开的事实：

| 键 | 数据含义 | 更新/清理责任 |
| --- | --- | --- |
| `TargetActor`（Object，Actor 基类） | 当前选中的、仍有玩法资格的目标 | 目标选择者；失去资格时清空或重选 |
| `HasLineOfSight`（Bool） | 对当前目标的 Sight 结论 | 仅 Sight 状态更新者，不能被 Hearing 成功覆盖 |
| `LastKnownLocation`（Vector） | 项目认可的最近感知位置 | 用刺激位置及自己的时效策略；失去视线后不要读实时目标位置来冒充记忆 |
| `CoverLocation` / `FiringLocation`（Vector） | 当前有效查询生成的候选位置 | 本次查询拥有者；失败/退出时按明确策略清理 |
| 目标/决策 generation | 拒绝旧结果的项目代次，可存在 Controller 状态中 | 切换目标、重启任务、重新 Possess 时更新；不是 UE 内置语义 |

“发现一个 Actor 就覆盖 TargetActor”会造成目标抖动；“任何感官失败就清空 TargetActor”则会丢掉仍被其他感官感知的目标。回调负责更新事实，目标选择者负责聚合事实和玩法规则。

### 2.5 丢失、老化与遗忘的边界

| 情形 | 能说明什么 | 不能据此推出什么 |
| --- | --- | --- |
| 某刺激 `WasSuccessfullySensed()` 为 false | 这条感官记录未成功/状态改变 | 其他感官也失败、Actor 已死、整条目标记忆已清除 |
| 某刺激年龄到期 | 这条信息到了配置的时效边界 | 一定有 Actor 级遗忘回调，或黑板自动清空 |
| `OnTargetPerceptionForgotten` | 该目标的感知数据被忘记；公开说明包括所有刺激到期或显式遗忘 | 单独 Sight 失去就触发，或等同死亡事件 |
| 来源 Actor 在处理前失效 | `OnTargetPerceptionUpdated` 可能不会调用 | 可以靠该委托完成所有销毁清理 |
| `GetCurrentlyPerceivedActors(Sense)` | 当前按指定感官仍在感知的目标集合 | 等于所有未遗忘历史信息 |
| `GetKnownPerceivedActors(Sense)` | 曾感知且尚未遗忘的目标集合 | 其中每个目标现在都能看见 |

自然遗忘要结合 Project Settings → Engine → AI System → Forget Stale Actors 与各 Sense 的 Max Age。多感官目标只有视觉到期、听觉仍有效时，不应期待它已经整体被忘记；Max Age 为 0 的配置也要纳入检查。[S11、S12]

`OnTargetPerceptionUpdated` 针对有效 Actor 通知；需要处理来源已失效的更新时，公开文档指向带 source id 的 `OnTargetPerceptionInfoUpdated`。即便采用后者，玩法对象销毁和会话结束仍应主动清理自己的目标选择/查询状态；source id 不能被当作永久可解引用 Actor。[S13]

`FAIStimulus` 的 Age、IsExpired、WasSuccessfullySensed 与 Strength 是不同字段/接口。引擎刺激老化不等于通用的“置信度线性衰减”。若设计需要怀疑度逐渐下降，项目自行定义公式、计时和信息合并规则，详见通用 AI 主篇。[S14]

## 3. 感知的蓝图与 C++ 接入

### 3.1 蓝图最小操作路线（待在目标工程执行）

1. 创建 AIController 蓝图并确认 Pawn 实际由它 Possess；给 Controller 加 AI Perception。
2. 添加 Sight/Hearing 配置，设置范围、半角、Max Age 和 affiliation。起步参数是场景假设，先用一个明确允许检测的目标。
3. 给非 Pawn 目标加 Stimuli Source 并注册 Sight；在脚步/开枪逻辑另调用 Report Noise Event。
4. 创建/初始化黑板，确保 Object、Vector、Bool 键的类型与用途一致；感知回调先确认黑板可用。
5. 在 On Target Perception Updated 中识别感官类型，更新这一感官的数据，再让目标选择逻辑决定是否写入 TargetActor。要保留最后已知位置就存刺激位置。
6. 加入目标失效、换队与 Controller 退出处理；配置自然遗忘时再观察 Forgotten，不能拿它代替所有退出事件。
7. 最后连接行为树黑板装饰器与中断策略。感知更新不自动使任意正在执行的节点立即重选。

预期验证信号是注册状态、分感官记录、黑板值和 BT 分支能相互解释；这不是本轮已经完成的运行记录。

### 3.2 C++ 配置片段的前置合同

下面是**已有 AIController 子类构造函数内的配置片段**，不是完整项目。模块需可引用 `AIModule`；Controller 头文件要声明持有组件的 `UPROPERTY` 成员，动态回调要有匹配签名的 `UFUNCTION`。使用工程自己的类名、模块导出宏和黑板初始化流程。此处不提供假定万能的 AAController 宿主。

```cpp
// 构造函数内；VisionConfig/HearingConfig/Senses 为已声明的 UPROPERTY 成员。
// includes: Perception/AIPerceptionComponent.h, Perception/AISenseConfig_Sight.h,
// Perception/AISenseConfig_Hearing.h, Perception/AISense_Sight.h
Senses = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Senses"));
SetPerceptionComponent(*Senses);
VisionConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("VisionConfig"));
VisionConfig->SightRadius = 1500.0f;
VisionConfig->LoseSightRadius = 1800.0f;
VisionConfig->PeripheralVisionAngleDegrees = 60.0f;
VisionConfig->SetMaxAge(5.0f);
// 入门场景先允许各关系进入；目标选择者另做资格过滤。
VisionConfig->DetectionByAffiliation.bDetectEnemies = true;
VisionConfig->DetectionByAffiliation.bDetectFriendlies = true;
VisionConfig->DetectionByAffiliation.bDetectNeutrals = true;
Senses->ConfigureSense(*VisionConfig);
HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
HearingConfig->HearingRange = 2500.0f;
HearingConfig->SetMaxAge(10.0f);
HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;
HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
Senses->ConfigureSense(*HearingConfig);
Senses->SetDominantSense(UAISense_Sight::StaticClass());
```

配置与委托接口见 [S02、S05、S33、S34、S35]。这些厘米、角度和秒数只演示字段，不是推荐性能预算。事件绑定应在项目确定的生命周期安装一次并配对解除；不要每次 Possess 都无条件重复绑定。异步记录到达时，回调须重新确认 Pawn/黑板/目标所属代次。

## 4. EQS：先保证候选合法，再比较偏好

### 4.1 查询的数据流与运行模式

一次查询以明确 Querier 和参数开始。Generator 产生 Actor 或位置，Context 为生成器/测试提供参照数据；Test 按 Test Purpose 过滤、评分或同时执行。过滤失败的项不再作为候选；评分只表达合法候选间的偏好。启用自动排序时，应考虑过滤作用与成本，不能按权重大小理解执行顺序。[S15、S16]

常见入口的作用：

| 类别 | 例子 | 使用边界 |
| --- | --- | --- |
| 网格 | Simple Grid、Pathing Grid | 范围和间距决定密度；投影到 NavMesh 不等于路径一定满足玩法要求 |
| 几何分布 | Circle、Cone、Donut | 适合环绕、方向搜索；仍需导航/碰撞约束 |
| 当前点 | Current Location | 检查现位置是否合适 |
| Actor 集合 | Actors of Class | 控制搜索范围，明确 Actor 类型及数据有效性 |
| 自定义生成器 | `UEnvQueryGenerator_BlueprintBase` / C++ | 生成正确 Item 类型，避免每查询全世界扫描 |
| 位置/路径测试 | Distance、Pathfinding | 直线距离与路径成本不同；路径方向和导航过滤器也有语义 |
| 场景约束 | Trace、Overlap、Project | Project 可以改变候选位置；Trace 命中不能直接解释为“有掩体” |
| 方向/语义 | Dot、Gameplay Tags | Dot 需要明确两向量；Actor Tag/Gameplay Tag 和接口要求须匹配 |

`UEnvQueryGenerator_ProjectedPoints` 应作为投影生成器的实现基类定位，不当成一个已经定义好分布的通用可选资产；公开文档支持蓝图自定义 Generator/Context，自定义 Test 的入口是 C++，不继续使用未核对的 `EnvQueryTest_BlueprintBase`。[S15、S16]

公开运行枚举列出 `SingleResult`、`RandomBest5Pct`、`RandomBest25Pct`、`AllMatching`。它们分别用于选单个、在高分区间随机选取、或取全部匹配；本篇不把 5%/25% 解释为“排序后固定取前几个条目”，精确阈值/同分处理需核对目标版本实现。这里没有通用 `ItemCount` 资产参数用于返回任意前 N 项。需要 Top N 时，应明确在结果集合上由项目截取及如何处理同分；若只要一个落点，优先让查询以单结果模式表达意图。[S18]

### 4.2 评分公式的三个不同层次

1. **原始量**：如距离、路径长度、点积或布尔命中结果。
2. **过滤与归一化**：先根据约束剔除不合法项；评分范围可来自测试值或指定界限。Absolute 与 Relative to Scores 是归一化基准选项，前者以 0 为基底，后者以最低测试值为基底；它们不是评分方程，也不表示“Absolute 就直接使用厘米数”。
3. **评分方程与权重**：对归一化值应用 Constant、Linear、Square、Inverse Linear、Square Root 等方程，再乘 Scoring Factor；多个测试共同影响结果。[S16、S19]

“想要近处”与“希望处于某个距离带”是不同目标：前者可用适当的评分方向，后者应先过滤过近/过远项，再用 Reference Value 或其他明确的偏好。负 Scoring Factor 的用法在官方 Quick Start 中有例子；复制前仍要看当前资产的归一化、方程和因子一起产生什么效果。[S20]

反例：给不可达点很低的分，不会使它不合法；若其他点都更差，它仍可能被选中。要禁止不可达，必须有相应过滤约束。另一个反例是把不同查询的归一化分直接当成全局距离/风险指标：候选集合变了，相对归一化的标尺也可能变了。

多 Context 还需要定义过滤是 All Pass 还是 Any Pass、评分如何聚合；“任一敌人看不见”与“所有敌人都看不见”显然不是同一掩护条件。所有候选原始量相同、只有一个候选或分数并列时，不宣称排序稳定；项目若依赖稳定选点，须另外定义并验证规则。

### 4.3 正确区分掩体与射击位

原“EQS 找掩体”同时要求对目标无遮挡，实际上混入了射击位用途。把两个查询明确分开：

| 项目设计 | 硬约束 | 排序偏好 | 仍须另外确认 |
| --- | --- | --- | --- |
| `EQS_FindFiringPosition` | 对目标的射击高度采样无遮挡、所需路径成立、距离在武器范围内 | 移动成本小、距理想射程近 | 枪口/动画/真实攻击几何、运行中目标变化 |
| `EQS_FindCover` | 敌人采样点到候选身体采样点有合适阻挡、移动可达 | 路径成本低、离威胁合适 | 掩体尺寸、站/蹲姿、多威胁、占用/预约和到达后复核 |

Trace Test 的布尔值与是否命中有关；在简单无遮挡射击位例中，官方 Quick Start 的 Bool Match 关闭用于保留能看见目标的点。不要把“击中地面”与“到敌人无遮挡”塞进同一布尔解释。改成掩体时也不能只反转一个勾选就宣称完成战术掩体系统：采样高度、忽略对象、碰撞响应和阻挡物语义必须与玩法一致。[S16、S20]

## 5. 蓝图路线：感知目标→位置查询→行为树

下面保留掩体/射击位资产搭建用途，但仅承诺操作方案，尚未在编辑器执行。

### 5.1 三个入口统一到 Pawn，决策 Controller 另有身份

官方 Contexts 文档将标准行为树发起查询的 Querier 定义为当前受 AIController 控制的 Pawn。AI System Settings 的 Allow Controllers as EQSQuerier 默认关闭；关闭时部分入口会把 Controller 转换为 Pawn，绕过转换或没有 Pawn 时会警告。这个说明不是“所有入口强制拒绝 Controller”，也不能用一条项目约定抹掉入口转换。[S11、S39]

本篇采用一个可追踪的合同：EQS 请求 Owner/Querier 为 `QueryPawn`；项目决策拥有者为 `DecisionController`。发起时必须同时满足两者有效、`DecisionController->GetPawn() == QueryPawn`、`QueryPawn->GetController() == DecisionController`。Context 从 QueryPawn 的当前 Controller 取得黑板；结果 Owner 与 QueryPawn 比较，不能与 DecisionController 比较。

| 入口 | 本文的 EQS Querier | 从哪里取得项目黑板 | 完成/退出责任 |
| --- | --- | --- | --- |
| 标准 BT 的 Run EQS Query Task | 按官方标准 BT 语义为当前受控 Pawn | Context：Pawn → 当前 AIController → 黑板 | 内置任务管理自身完成/Abort；BT 分支、目标变化与黑板的项目资格仍须另行处理 |
| Blueprint 的 Run EQS Query 函数 | Querier 引脚显式传入当前受控 Pawn；不传 Controller Self | 同上；直接完成事件保存本次 Pawn/Controller 对 | wrapper 完成时核对本次对象、Pawn/Controller 对和 generation，再允许写黑板 |
| C++ 的 FEnvQueryRequest | 构造函数显式用 QueryPawn 作 RequestOwner | 同上；完成委托可绑定 DecisionController，但不改变显式 RequestOwner | 结果 Owner 期待 QueryPawn，项目会话归 DecisionController；按§7拒绝旧结果 |

这不要求启用或更改 Allow Controllers as EQSQuerier。若项目定制了 BT 任务、Context 或发起入口，必须观察实际 Querier/Owner，类型不符就拒绝使用本示例，不能改成任意类型回退或默认玩家0。

### 5.2 蓝图资产接入步骤

1. 准备可导航测试关卡、AIController、黑板和行为树；确认 Environment Query Editor 在目标安装中可用，必要时按工程流程启用。
2. 黑板建立 Actor 型 `TargetActor` 和 Vector 型 `FiringLocation`。目标选择者写入有效目标，BT 装饰器在目标缺失时走其他分支。
3. 新建 `EnvQueryContext_BlueprintBase` 派生的目标 Context。在 `Provide Single Actor` 开始先将 Resulting Actor 设为空；把 Querier Actor 转为 Pawn，再从 Pawn 的 Controller 转为 AIController，确认该 Controller 仍控制此 Pawn，最后读取它的黑板 TargetActor。各步无效或类型不符就保留空结果并走失败路线；不要把 QuerierObject 直接当 Controller，也不要退回玩家0或世界原点。
4. 新建 `EQS_FindFiringPosition`，从 Querier 周围生成 Grid/Pathing Grid 候选。按移动方向、导航过滤器和角色体型验证可达性。
5. 用目标 Context 做视线过滤；设定 Trace 的通道、方向和双方高度。再添加距离范围过滤和距离/路径成本评分；命名参数仅传数值/布尔等受支持值。
6. BT 用 `Run EQS Query` 任务（类名 `UBTTask_RunEQSQuery`），选择查询、运行模式与 `FiringLocation` 键；后接 Move To。查询失败时转到明确的 fallback 分支，核对失败时黑板更新选项，不能默认旧坐标已被清除。
7. 为目标切换/丢失设计装饰器中断及任务退出。查询成功后、移动前仍确认目标和位置所属的决策代次有效；移动失败也要回到重规划或保守行为。

Blueprint Context 节点见 [S21]，BT 任务/API 见 [S22]。标准 BT 任务已提供完成、Abort 和节点内存入口，优先利用它，避免无必要地重写一整套潜伏任务。它不会因为本文定义了 generation 就自动执行§7的项目票据检查；本步骤是资产接通路线，不是已验证的动态换目标/换控安全宿主。要求这些边界时，项目需给该 BT 分支提供相应失效、中断及结果接受策略并在工程验证。自定义 BT 节点若非实例化，不能把每个 AI 的请求状态塞进共享节点对象成员。[S22]

脱离 BT 时，先验证本次 QueryPawn/DecisionController 对、黑板和有效目标；缺少任一项就不发起。`Run EQS Query` 的 Querier 引脚显式传入 QueryPawn，返回 `UEnvQueryInstanceBlueprintWrapper`；在完成事件中按状态读取 `Get Query Results as Locations/Actors`，检查读取成功和数组长度，再写入目标类型的黑板键。Actor 结果还要检查当前有效性。保存 wrapper 及本次 QueryPawn/DecisionController 对和 generation 以辨认所属请求，并处理取消/退出；不再使用未核对的“Find EQS Query Results 返回 FEnvQueryResult”节点说法。[S23、S40]

## 6. C++：上下文、Test 与请求片段

以下是教学代码，未经过 UHT/编译或运行。示例的 `MYGAME_API` 应替换为实际模块导出宏，模块依赖须包含所用 Engine/AIModule；生成头文件保持该头文件最后一个 include。代码刻意采用 Pawn 请求 Owner/Querier、AIController 项目决策拥有者的窄合同，不尝试通吃 Controller Querier、组件与任意 UObject。

### 6.1 目标 Context：从 Pawn 找到当前 Controller 黑板

`FEnvQueryResult::Owner` 是弱 UObject 指针；本篇先 `.Get()` 再 Cast 为 Pawn，然后沿当前控制关系取得 AIController。它不是项目 DecisionController 指针，不能直接 Cast 为 AIController，也不能对弱 UObject 直接调用 Actor 的 `FindComponentByClass`。本例固定键名，避免给 Context 放一个未解析的 Blackboard Key Selector 却没有解释初始化。[S17、S24、S25]

```cpp
// EnvQueryContext_BlackboardTarget.h
#pragma once
#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvQueryContext_BlackboardTarget.generated.h"

UCLASS()
class MYGAME_API UEnvQueryContext_BlackboardTarget : public UEnvQueryContext
{
    GENERATED_BODY()
public:
    virtual void ProvideContext(FEnvQueryInstance& QueryInstance,
        FEnvQueryContextData& ContextData) const override;
};
```

```cpp
// EnvQueryContext_BlackboardTarget.cpp
#include "EnvQueryContext_BlackboardTarget.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"

void UEnvQueryContext_BlackboardTarget::ProvideContext(
    FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
    const APawn* QueryPawn = Cast<APawn>(QueryInstance.Owner.Get());
    const AAIController* Controller = IsValid(QueryPawn)
        ? Cast<AAIController>(QueryPawn->GetController()) : nullptr;
    const UBlackboardComponent* BB = IsValid(Controller) && Controller->GetPawn() == QueryPawn
        ? Controller->GetBlackboardComponent() : nullptr;
    if (!IsValid(BB))
    {
        return; // 缺失上下文；调用者/Test 必须有明确失败策略
    }
    const AActor* Target = Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")));
    if (!IsValid(Target))
    {
        return;
    }
    UEnvQueryItemType_Actor::SetContextHelper(ContextData, Target);
}
```

这读取的是 Context 准备时、QueryPawn 当前 Controller 的黑板内容，不保存发起者身份，不能声称“等于请求发出时的目标快照”或“查询每一步都重新读”。若业务依赖某一目标，发起者须在查询期间保证上下文所属代次不变，并在回调前再次比较目标/generation；改变目标后原查询结果作废。需要不可变输入时，要另行实现明确的快照上下文，不能默认引擎替你冻结所有世界状态。

### 6.2 自定义可见性 Test：使用 Iterator 提交布尔结果

若内置 Trace 已满足用途，优先使用。下面保留原文“多个观察者，任一/全部可见”的扩展用途，收窄为：**游戏线程同步 RunTest、Point 候选、Actor Context、LineTraceSingleByChannel 的无遮挡判定**。它不是通用掩体系统；不覆盖穿透、复杂采样、阵营过滤、身体形状或导航。缺失任何必需观察者/世界时让整条查询失败，而不是静默把所有点当作合格。资产需将 ObserverContext 设为输出具有正确眼部位置的敌人 Pawn/场景 Actor 的 Context，不输出 Controller；ItemHeightOffset 的 100 厘米仅为示例采样高度。视点、迹检测和数据绑定接口见 [S36、S37、S38]。

```cpp
// EnvQueryTest_IsVisibleTo.h
#pragma once
#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvQueryTest_IsVisibleTo.generated.h"

UCLASS()
class MYGAME_API UEnvQueryTest_IsVisibleTo : public UEnvQueryTest
{
    GENERATED_BODY()
public:
    UEnvQueryTest_IsVisibleTo();
    UPROPERTY(EditDefaultsOnly, Category="Test")
    TSubclassOf<UEnvQueryContext> ObserverContext;
    UPROPERTY(EditDefaultsOnly, Category="Test")
    bool bRequireAllVisible = false;
    UPROPERTY(EditDefaultsOnly, Category="Test")
    float ItemHeightOffset = 100.0f;
    virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
};
```

```cpp
// EnvQueryTest_IsVisibleTo.cpp
#include "EnvQueryTest_IsVisibleTo.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"

UEnvQueryTest_IsVisibleTo::UEnvQueryTest_IsVisibleTo()
{
    Cost = EEnvTestCost::High;
    ValidItemType = UEnvQueryItemType_Point::StaticClass();
    SetWorkOnFloatValues(false);
    TestPurpose = EEnvTestPurpose::Filter;
    FilterType = EEnvTestFilterType::Match;
    BoolValue.DefaultValue = true;
}

void UEnvQueryTest_IsVisibleTo::RunTest(FEnvQueryInstance& QueryInstance) const
{
    APawn* QueryPawn = Cast<APawn>(QueryInstance.Owner.Get());
    const AAIController* Controller = IsValid(QueryPawn)
        ? Cast<AAIController>(QueryPawn->GetController()) : nullptr;
    UWorld* World = QueryInstance.World;
    TArray<AActor*> Observers;
    if (!IsValid(QueryPawn) || !IsValid(Controller) || Controller->GetPawn() != QueryPawn ||
        !IsValid(World) || !ObserverContext ||
        !QueryInstance.PrepareContext(ObserverContext, Observers) || Observers.IsEmpty())
    {
        QueryInstance.MarkAsFailed();
        return;
    }
    for (AActor* Observer : Observers)
    {
        if (!IsValid(Observer))
        {
            QueryInstance.MarkAsFailed();
            return;
        }
    }
    BoolValue.BindData(QueryPawn, QueryInstance.QueryID);
    const bool bExpected = BoolValue.GetValue();

    for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
    {
        const FVector Candidate = GetItemLocation(QueryInstance, It)
            + FVector(0.0f, 0.0f, ItemHeightOffset);
        bool bCombined = bRequireAllVisible;
        for (AActor* Observer : Observers)
        {
            FVector EyeLocation;
            FRotator EyeRotation;
            Observer->GetActorEyesViewPoint(EyeLocation, EyeRotation);
            FCollisionQueryParams Params(SCENE_QUERY_STAT(EQS_VisibilityExample), true);
            Params.AddIgnoredActor(Observer);
            Params.AddIgnoredActor(QueryPawn);
            FHitResult Hit;
            const bool bVisible = !World->LineTraceSingleByChannel(
                Hit, EyeLocation, Candidate, ECC_Visibility, Params);
            bCombined = bRequireAllVisible ? (bCombined && bVisible)
                                          : (bCombined || bVisible);
        }
        It.SetScore(TestPurpose, FilterType, bCombined, bExpected);
    }
}
```

`PrepareContext` 负责取得上下文，`GetItemLocation` 按 Item 类型读取位置，`ItemIterator::SetScore` 把布尔测量交给 EQS 的过滤/评分流程。不要直接遍历全部 Items 然后调用不存在于本轮公开合同中的 `QueryInstance.SetScore(ItemIdx, ...)`，也不要手工覆盖总分绕过 Test Purpose。[S19、S26、S27]

本例先把多个观察者聚合成一个布尔量，因此资产中的 Multiple Context 运算并不再决定观察者的聚合方式；由 `bRequireAllVisible` 负责。Bool Match=true 时是“任一/全部可见”；Bool Match=false 与任一可见取反可表达“没有观察者看见”。这只是逻辑合同，未做纸例或运行验证。

Iterator 可以在项之间让出执行，但一个候选内部遍历很多观察者的工作仍可能很贵。不要调用 `IgnoreTimeLimit()` 以掩盖预算问题；应限制 Context 规模、采样数量与查询频率。观察者数据在执行期间可变，本片段没有实现多帧稳定快照；要求稳定采样时需先设计相应输入所有权再扩展。

### 6.3 请求参数与结果读出：两个局部接口片段

Actor 通过 Context 提供。公开 `FEnvQueryRequest` 支持 SetFloatParam/SetIntParam/SetBoolParam 和 `FEnvNamedValue` 等入口；不能用 `SetNamedParam("TargetActor", ActorPointer)` 传对象。[S28]

```cpp
// 已有决策发起处的接口片段，非完整生命周期实现。
// includes: AIController.h, GameFramework/Pawn.h, BehaviorTree/BlackboardComponent.h,
// EnvironmentQuery/EnvQueryManager.h；外层为允许直接return的void入口。
// QueryAsset 已由项目持有并验证；DecisionController 为本次项目决策拥有者。
APawn* QueryPawn = IsValid(DecisionController) ? DecisionController->GetPawn() : nullptr;
if (!IsValid(QueryPawn) || QueryPawn->GetController() != DecisionController)
{
    return; // 尚未创建票据；按项目入口返回失败，不发起查询
}
UBlackboardComponent* DecisionBB = DecisionController->GetBlackboardComponent();
AActor* QueryTarget = IsValid(DecisionBB)
    ? Cast<AActor>(DecisionBB->GetValueAsObject(TEXT("TargetActor"))) : nullptr;
if (!IsValid(QueryTarget))
{
    return; // 无黑板或有效目标；不发起依赖TargetActor的本查询
}
// 此处先按§7建立捕获 QueryPawn/DecisionController/QueryTarget/generation 的票据与 FinishedDelegate。
FEnvQueryRequest Request(QueryAsset, QueryPawn);
Request.SetFloatParam(TEXT("PreferredDistance"), PreferredDistanceCm);
const int32 ReturnedQueryId = Request.Execute(
    EEnvQueryRunMode::SingleResult, FinishedDelegate);
// 返回值如何判为未成功提交，按目标版本 Execute 实现补充核对；
// 不把拿到 int32 或函数返回当成已经产出有效点。
```

下面函数只做**结果结构、Pawn 归属与当前控制关系检查**；它不替代调用方的 generation/目标/任务会话检查。ExpectedQueryPawn 必须取自请求票据，ExpectedDecisionController 必须取自同一票据，不能在回调时改用“当前 Pawn”来接受旧结果。为了保持读取合同简单，只接受 Point 类型结果；不把任意 ItemType 的内存当位置。

```cpp
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"

bool TryReadFirstPoint(const TSharedPtr<FEnvQueryResult>& Result,
    const APawn* ExpectedQueryPawn, const AAIController* ExpectedDecisionController,
    FVector& OutPoint)
{
    if (!IsValid(ExpectedQueryPawn) || !IsValid(ExpectedDecisionController) ||
        ExpectedQueryPawn->GetController() != ExpectedDecisionController ||
        ExpectedDecisionController->GetPawn() != ExpectedQueryPawn ||
        !Result.IsValid() || !Result->IsSuccessful() ||
        Result->Owner.Get() != ExpectedQueryPawn || Result->Items.IsEmpty() ||
        Result->ItemType != UEnvQueryItemType_Point::StaticClass())
    {
        return false;
    }
    const FVector Point = Result->GetItemAsLocation(0);
    if (!FMath::IsFinite(Point.X) || !FMath::IsFinite(Point.Y) || !FMath::IsFinite(Point.Z))
    {
        return false;
    }
    OutPoint = Point;
    return true;
}
```

成功状态、非空结果、Item 类型和 Pawn Owner 都有意义；发起者的 Controller 与结果的 Owner 不能混为同一身份。Actor 查询改用对应访问器后还需验证 Actor 当前有效且有玩法资格。不要对失败/空数组访问第0项，不要用零向量代表无结果；世界原点也可以是合法坐标。[S17]

## 7. 查询异步、重入与退出责任

`UEnvQueryManager` 提供排队/调度入口和 `RunInstantQuery`，并有时间切片相关接口；“异步”在此首先表示调用与完成分离，不能据此断言任意 Test 都在工作线程运行，或每次都跨一帧。`RunInstantQuery` 也不是通用提速开关。[S29]

本轮公开 `Execute` / `RunQuery` 页没有给出“所有路径的完成委托都在函数返回后调用”的保证。因此下面是**防御性的项目集成合同**，不声称已经观察到 UE 5.8 某条同步回调路径，也不伪造具体启动失败返回值或 Abort 回调顺序。[S28、S30]

以下顺序假定发起、完成消费和退出状态变更都在游戏线程进行，不提供跨线程共享状态协议。建议先选“每个决策拥有者最多一条有效查询”的窄范围，再实现：

1. 发起前确认 QueryPawn/DecisionController 当前互相对应，再创建请求票据，分别保存弱 QueryPawn（EQS请求Owner）与 DecisionController（项目决策owner）、目标标识、决策 generation、预期 Item 类型；把票据设为当前，并标记 awaiting。完成委托捕获这张票据，不能只捕获会不断变化的当前目标成员。
2. 然后调用 Execute。委托即使重入，也能先检查票据是否仍为当前、是否 awaiting、票据中的 QueryPawn 与 DecisionController 是否仍有效且互相对应、目标是否仍属于同一代次。已结束或过期的票据不能写黑板。
3. Execute 返回后，仅当票据仍为当前且仍 awaiting，才记录返回的 QueryID/处理启动结果。不能无条件把已经完成的票据重新设为等待，也不能只靠“回调 QueryID 等于刚赋值的 RequestID”接受结果。
4. 回调进入成功消费前，检查结果有效、状态、Owner等于票据QueryPawn、数量和类型，再检查目标资格及票据DecisionController的黑板可用。终结该票据后才能发起下一次决策；若接受结果触发新的查询，旧回调不得再清理新票据的键。
5. 当前查询失败或无可用结果时，由同一拥有者清理自己拥有的旧位置/有效标志并触发 fallback。旧请求的失败不能擦掉新请求的成功结果。
6. 切换目标、Abort、任一方的UnPossess/换控、QueryPawn或DecisionController的EndPlay/销毁、World 清理时，先让票据失效并结束写入资格，再按实际 QueryID 和管理器状态请求取消。`AbortQuery` 或按 Querier 移除只是停止工作的手段；不能把“请求取消成功”当成“绝不会再收到回调”的未核对保证。

这套顺序是项目责任，不是 EQS 自动提供的事务隔离。QueryID 可用于日志与取消，但不能单独证明任务会话、Pawn 或目标仍相同。即使同一Pawn失控后又被同一Controller重新Possess，当前对象对可能再次相等，仍必须靠已推进的generation拒绝旧请求。使用弱引用也只能防止失效对象访问，不能阻止旧查询写入一个仍活着但已经换目标的 Controller。

公开 API 提供 `AbortQuery`、`RemoveAllQueriesByQuerier`、`SilentlyRemoveAllQueriesByQuerier` 与 World cleanup 入口。本篇按 Querier 移除时指的是原票据 QueryPawn，不是 DecisionController，也不是换控后的新Pawn。若一个 Querier 还承载别的业务查询，不要为取消本功能而粗暴移除它的全部查询；应使用精确请求所有权。退出是否执行完成委托、启动失败标识、Task 中断时清键细节，均需在实际 checkout 核对后才能写成可编译宿主。[S29]

## 8. 成本与调试：先定位哪一段在做工作

### 8.1 成本由输入规模与实际工作决定

- 感知：监听者数、注册目标数、空间范围、视线复杂度和更新调度共同影响工作量；缩小 Sight Radius 只改变其中一部分，不能代替测量。
- EQS 生成：网格范围和 Space Between 共同控制密度；`GridHalfSize` 是空间范围参数，不是“5×5 个点”的计数器。
- EQS 测试：候选数×Context 数×每项测试成本是检查放大来源的实用视角，不是测得的耗时公式。先用便宜且有淘汰力的约束缩小输入，再做昂贵 Trace/Pathfinding；自动排序也要看实际配置。
- 时间切片：能分摊部分工作，但不能让任意自定义一次调用自动满足硬时限；单次昂贵生成器或每候选的大内层循环仍需处理。
- 发起策略：目标/环境改变时按需重查，合并重复需求、避免每 Tick 堆积，允许服务间隔和偏差错峰；间隔由玩法响应性与实际测量决定。

公开 `FEnvQueryInstanceCache` 说明的是含已排序 Tests 的实例模板缓存，不能推出“相同参数自动复用已完成结果”或万能 `bUseCache`。如果项目需要结果缓存，自己定义键与失效条件：导航/障碍变化、目标变化、Querier 位置变化、占用状态和时间新鲜度都会影响可复用性。[S15、S31]

旧文“PathingGrid ≤100”“0.25 秒足够”等没有在本文获得性能证据，保留的应是限制输入、错峰和测量的方法，而非普遍阈值。

### 8.2 调试顺序

1. **先看输入**：实际 Controller/Pawn、感官是否启用、目标是否注册、队伍归类、刺激类型/年龄/位置。
2. **再看决策数据**：黑板是否初始化，当前目标与最后已知位置是否被不同语义覆盖，目标切换是否让旧请求失效。
3. **再看 EQS**：是否有候选、哪个过滤器剔除它、原始值和归一化值是什么、Context 是否是预期 Actor，以及结果状态和 Item 类型。
4. **最后看行动**：BT 是否中断/失败，Move To 的导航条件和目标接受半径是否满足；EQS 成功与移动成功分开记录。

Epic AI Debugging 文档的默认入口是运行中按 apostrophe，再用 Numpad 3 看 EQS、Numpad 4 看感知、Numpad 2 看 BT/黑板；项目可改绑定。EQS 详细表可查看测试值，Visual Logger 可辅助追踪 EQS。EQS Testing Pawn 用于编辑器中检查查询，但不代替真实 Controller、Context 和生命周期验证。[S32]

颜色由工具/版本/配置决定，先读图例与数值，不写死“绿高红低”。原 `EQS.Query`、`EQS.Debugger` 及固定菜单路径未获本轮一手合同支持，改用上述已核对入口。

## 9. 常见问题 FAQ

**Q1：AI 看不到玩家，先改哪个参数？**
先确认 Pawn 被正确 Controller 控制、监听配置启用、目标来源已注册、affiliation 是否允许目标，再查视角/距离/遮挡及目标观察点。不要首先加大所有半径；这可能增加成本且完全绕不开错误队伍分类。

**Q2：离开视野后没有 Forgotten，是失败了吗？**
不一定。区分 Sight 丢失、刺激到期与目标整体遗忘，检查 Forget Stale Actors、Max Age=0、其他感官仍有效。黑板记忆和目标生命周期由项目清理，不由一个事件包办。

**Q3：听到脚步却没有视线，为什么 TargetActor 被清了？**
看是否把任意失败刺激都当成目标整体失效。按感官维护事实，统一重选；如果玩法要求仅攻击可见目标，应由 HasLineOfSight/攻击资格表达，而不是伪造“目标已遗忘”。

**Q4：感知有效但行为树不变？**
检查黑板实例、键名和类型、目标选择者有没有写值，以及装饰器观察/Abort 设置。正在进行的任务是否允许中断是独立问题。

**Q5：EQS 总选一个点，或高分点仍不可达？**
检查 Test Purpose：硬条件必须过滤；比较原始值、归一化、方程、因子及同分情况。查询模式不是任意 ItemCount；NavMesh 投影也不替代路径和移动验收。

**Q6：Trace 全部失败，或掩体点暴露？**
核对 Context、方向、两个端点高度、忽略对象、通道和 Bool Match。对于可见性示例，地面命中不是“看到敌人”；对于掩体，单一阻挡命中也不证明身体受保护。

**Q7：查询完成了却写回旧位置？**
先对照票据 generation、DecisionController/QueryPawn/目标身份及当前控制关系，再确认结果 Owner 是票据QueryPawn并核对QueryID。取消旧请求与拒绝旧结果必须同时有责任人；不能只检查 Result 成功。

**Q8：查询卡顿能否开启缓存解决？**
先测生成数量、每项 Context 数、昂贵测试和并发请求量。公开实例缓存不是通用结果缓存。减少无效查询、限制输入、错峰，再根据已测瓶颈优化。

**Q9：自定义气味感知或 EQS Test 怎么扩展？**
感官扩展需 Sense、Config、事件输入和监听生命周期一起设计；EQS Test 优先复用内置功能，必须自定义时使用 PrepareContext/ItemIterator/SetScore，并明确空上下文和预算的失败策略。代码片段存在不等于已能在工程构建。

**Q10：蓝图直接查询如何给行为树结果？**
完成事件中检查 wrapper 状态与结果数组，核对请求所属会话后再写同一个黑板实例。只为行为树服务时优先使用 Run EQS Query 任务；不要让直接回调和 BT Task 同时争写同一个位置键。

## 10. 验证建议与停止条件（全部待执行）

本轮实际证据仅为官方资料与文档静态核对。下表是未来目标工程的验收设计，未执行、未收集日志，也不构成 L3/L4。先记录引擎版本/revision、平台、构建配置、Pawn/Controller/黑板/资产、感官配置、导航数据和碰撞通道。

| 场景 | 操作与待观察信号 | 判定目标/停止条件 |
| --- | --- | --- |
| 来源接通 | 对照 Sight 注册/未注册的非 Pawn 目标；分别播放音频和显式报告噪声 | 解释何种输入形成刺激；不能只因有声音就判听觉已接通 |
| 感官分离 | 看见后遮挡，期间另报告听觉；记录分感官状态、当前/已知集合 | Sight 丢失不抹掉仍有效的其他感官事实 |
| 老化/遗忘 | 对比 Forget Stale Actors、有限 Max Age、Max Age=0 | 按实际事件解释自然遗忘，不伪造精确到某一帧的保证 |
| 生命周期/队伍 | 处理前销毁来源、目标死亡/换队、Pawn销毁/换控、Controller退出、同一对象对重新Possess | 无失效访问；旧目标/旧Pawn/generation不能继续接收写入 |
| 三入口身份 | 标准BT、显式Pawn的蓝图函数、显式Pawn的C++请求分别记录Owner/Querier与决策Controller | Context成功取得同一Pawn对应黑板；Controller不冒充结果Owner；定制入口类型不符则失败 |
| 过滤/评分 | 去掉路径、改变 Trace Bool Match、令候选原始值相同 | 硬约束不以低分替代；归一化/同分解释与实际配置一致 |
| 空输入/空结果 | 目标 Context 缺失、没有候选、全部被过滤 | 不读第0项、不写原点、不沿用无主旧坐标；有明确 fallback |
| 重入/旧结果 | 在实际工程建立可控启动失败、取消、目标切换与完成顺序 | 先核对 Execute/Abort 实现；旧票据不能覆盖或清理新票据数据 |
| 自定义 Test | 单/多观察者、任一/全部可见、Observer 失效、碰撞自遮挡 | 明确失败和聚合语义；发现与内置 Trace 不一致时停止采用并定位 |
| 成本 | 改变 AI/目标/候选/Context 数与并发请求数 | 采集实际耗时和等待时间；没有测量就不发布普遍预算 |
| BT/行动 | 查询成功后中断、Move To 失败、World 退出 | 查询结果与动作成功分开；退出后没有继续写黑板 |

遇到接口签名、资产节点、委托顺序或来源与目标版本不一致，先冻结使用该片段并核对对应头文件/实现；不要为让示例工作而扩大到未授权的全局设置修改或伪造测试结果。

<a id="来源与核对范围"></a>
## 11. 来源与核对范围

以下均为 Epic 一手页面，核对日期 2026-10-09；标题当时显示 UE 5.8。URL 为此次实际可访问入口，并非不可变源码快照。只查阅公开说明、字段和签名；没有读取或新增引擎源码全文。没有用 5.5 搜索结果补作 5.8 证据。

| 编号 | 一手来源与位置 | 本文采用范围 |
| --- | --- | --- |
| S01 | [AI Perception](https://dev.epicgames.com/documentation/unreal-engine/ai-perception-in-unreal-engine)，Sense 配置、事件、Stimuli Source | 参数/节点操作入口；不认证具体调度实现 |
| S02 | [UAIPerceptionComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/UAIPerceptionComponent)，类说明与查询/配置函数 | 监听、批量刺激、当前/已知查询接口 |
| S03 | [UAIPerceptionStimuliSourceComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/UAIPerceptionStimuliSourceCompon-)，注册/注销函数 | 来源与监听配置分离 |
| S04 | [UAIPerceptionSystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AIModule/UAIPerceptionSystem)，继承、Tick/AgeStimuli/ReportEvent | AI 子系统、注册/事件入口边界 |
| S05 | [UAISenseConfig_Sight](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AIModule/UAISenseConfig_Sight)，公开配置字段 | 半角、首次/已见视距、最后见到位置自动成功范围 |
| S06 | [UAISense_Sight](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AIModule/UAISense_Sight)，查询容器、ComputeVisibility、Update | 调度/可见性入口存在；不认证内部顺序或默认预算 |
| S07 | [UAISense_Hearing](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/UAISense_Hearing?lang=en-US)，ReportNoiseEvent | 报告噪声签名 |
| S08 | [UAISense_Touch](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/UAISense_Touch)，ReportTouchEvent/RegisterEvent | 显式事件入口 |
| S09 | [UAISense_Damage](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/UAISense_Damage?lang=en-US)，ReportDamageEvent | 显式伤害事件入口 |
| S10 | [IGenericTeamAgentInterface](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AIModule/IGenericTeamAgentInterface) | 团队接口定位；实际态度/换队刷新须核对项目 |
| S11 | [AI System Settings](https://dev.epicgames.com/documentation/unreal-engine/ai-system-settings-in-the-unreal-engine-project-settings)，Forget Stale Actors、Allow Controllers as EQSQuerier | 遗忘设置及Controller部分入口转换/默认警告边界，不据此改设置 |
| S12 | [On Target Perception Forgotten](https://dev.epicgames.com/documentation/unreal-engine/BlueprintAPI/EventDispatchers/OnTargetPerceptionForgotten) | 全部刺激到期/显式遗忘的目标级通知 |
| S13 | [On Target Perception Updated](https://dev.epicgames.com/documentation/unreal-engine/BlueprintAPI/EventDispatchers/OnTargetPerceptionUpdated) | Actor 已无效时通知缺口与 InfoUpdated 指引 |
| S14 | [FAIStimulus](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/FAIStimulus)，字段/年龄和状态函数 | 类型、FName Tag、强度/年龄/成功状态分离 |
| S15 | [EQS Generators](https://dev.epicgames.com/documentation/en-us/unreal-engine/eqs-node-reference-generators-in-unreal-engine)，各生成器参数 | 候选类型、网格密度、自动排序、蓝图生成器 |
| S16 | [EQS Tests](https://dev.epicgames.com/documentation/unreal-engine/eqs-node-reference-tests-in-unreal-engine?lang=en-US)，Common、Pathfinding、Project、Trace | 过滤与评分、归一化/方程、测试用途 |
| S17 | [FEnvQueryResult](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/FEnvQueryResult)，Items/ItemType/Owner/QueryID/状态访问器 | 结果接收检查；不保证世界状态持续有效 |
| S18 | [EEnvQueryRunMode::Type](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/EEnvQueryRunMode__Type?lang=en-US) | 四个枚举；精确百分比阈值/同分算法未核对 |
| S19 | [UEnvQueryTest](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/UEnvQueryTest)，TestPurpose/BoolValue/GetItemLocation/RunTest | Test 扩展 API 与评分职责 |
| S20 | [EQS Quick Start](https://dev.epicgames.com/documentation/unreal-engine/environment-query-system-quick-start-in-unreal-engine)，Trace/Distance 与 BT 接入 | 无遮挡射击位、负评分因子、过滤优先/成本排序 |
| S21 | [UEnvQueryContext_BlueprintBase](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AIModule/UEnvQueryContext_BlueprintBase)，Provide Single/Set | Blueprint Context 输入/输出 |
| S22 | [UBTTask_RunEQSQuery](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AIModule/UBTTask_RunEQSQuery)，EQSRequest/AbortTask/节点内存 | 任务类名、失败更新选项和生命周期入口 |
| S23 | [UEnvQueryInstanceBlueprintWrapper](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AIModule/UEnvQueryInstanceBlueprintWrappe-)，结果读取/完成事件 | 直接蓝图查询返回对象与读取路径 |
| S24 | [UEnvQueryContext](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/UEnvQueryContext)，ProvideContext | C++ Context 扩展签名 |
| S25 | [UEnvQueryItemType_Actor](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/UEnvQueryItemType_Actor)，SetContextHelper | 将 Actor 提供给 Context |
| S26 | [FEnvQueryInstance](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/FEnvQueryInstance?lang=en-US)，World/PrepareContext | 取得世界与上下文；不承诺快照一致性 |
| S27 | [FItemIterator](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/FEnvQueryInstance/FItemIterator)，SetScore/IgnoreTimeLimit | 逐项过滤评分与时间切片边界 |
| S28 | [FEnvQueryRequest](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/FEnvQueryRequest?lang=en-US)，Execute/SetFloatParam/SetNamedParam | 请求参数与回调签名；无全路径回调时序保证 |
| S29 | [UEnvQueryManager](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/UEnvQueryManager?lang=en-US)，取消、清理、RunInstantQuery | 管理器入口；细节需实际实现验证 |
| S30 | [UEnvQueryManager::RunQuery](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/UEnvQueryManager/RunQuery?lang=en-US) | 重载与实现路径，仅定位未读实现 |
| S31 | [FEnvQueryInstanceCache](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/FEnvQueryInstanceCache) | 实例模板及排序测试缓存，不认证结果复用 |
| S32 | [AI Debugging](https://dev.epicgames.com/documentation/unreal-engine/ai-debugging-in-unreal-engine?lang=en-US)，EQS/Perception/BT | 调试键、详细测试值、Visual Logger 和 Testing Pawn 入口 |
| S33 | [AAIController](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AIModule/AAIController)，SetPerceptionComponent/UseBlackboard | Controller 接入感知与黑板 |
| S34 | [UAISenseConfig_Hearing](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AIModule/UAISenseConfig_Hearing) | HearingRange/affiliation/LoSHearingRange 字段 |
| S35 | [UAISenseConfig](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AIModule/UAISenseConfig)，SetMaxAge | 感官配置基类接口 |
| S36 | [AActor::GetActorEyesViewPoint](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/AActor/GetActorEyesViewPoint?lang=en-US) | 观察者视点接口，不等于通用摄像机位置 |
| S37 | [UWorld](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UWorld?lang=en-US)，LineTraceSingleByChannel | 返回首个阻挡命中的迹检测接口 |
| S38 | [FAIDataProviderValue](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/FAIDataProviderValue)，BindData | BoolValue 读取前绑定 Owner/RequestId |
| S39 | [EQS Contexts](https://dev.epicgames.com/documentation/en-us/unreal-engine/eqs-node-reference-contexts-in-unreal-engine)，EnvQueryContext_Querier | 标准BT发起查询的Querier为当前受控Pawn |
| S40 | [Run EQSQuery（Blueprint）](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/AI/EQS/RunEQSQuery)，Querier输入/Return Value | 直接蓝图函数显式输入Querier；不把object类型当Controller语义保证 |

API 页给出的可移植头文件定位包括 `Engine/Source/Runtime/AIModule/Classes/Perception/AIPerceptionComponent.h`、`Engine/Source/Runtime/AIModule/Classes/EnvironmentQuery/EnvQueryManager.h` 和 `Engine/Source/Runtime/AIModule/Classes/EnvironmentQuery/EnvQueryTypes.h`。这些是后续源码核对入口，本轮未验证其在本地磁盘存在，更未声称逐行读过对应实现。

## 12. 关联阅读与前后置专题

- [01-行为树详解](01-行为树详解.md)：黑板、BT 流程与中断策略。
- [03-NavMesh寻路](../导航移动与群体协同/03-NavMesh寻路.md)：导航投影、路径与实际移动边界。
- [08-AI调试与性能分析](../评测安全与运行预算/08-AI调试与性能分析.md)：从输入、决策、执行到预算的排查。
- [12-行为树与AI源码](12-行为树与AI源码.md)：源码阅读入口；仍以该专题自己的版本/证据为准。
- [46-Lyra-AI机器人与队伍源码](../战斗战术与机器人/46-Lyra-AI机器人与队伍源码.md)：机器人和队伍实现专题；本篇未重新验证 Lyra 使用了哪些感知/EQS 资产。
- [01-AI总体架构与感知](01-AI总体架构与感知.md)：引擎无关的感知、记忆、决策和预算原理。

旧文所列 AITesting 工程与“Epic 掩体教程”是历史扩展阅读线索，没有在本轮访问具体工程或定位对应教程，不把它们作为已核对的运行证据。可直接复核的官方查询案例以本篇 S20 为准，其主要用途是射击观察位置。
