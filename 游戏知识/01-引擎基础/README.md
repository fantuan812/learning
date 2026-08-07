# 01 引擎基础

## 分类简介

「01-引擎基础」是 UE（Unreal Engine）客户端知识库的第一个分类，面向希望系统掌握虚幻引擎运行机制的客户端开发者。本分类不涉及具体业务玩法，而是聚焦引擎本身最核心、最容易被误解的四块地基：

- **对象与反射**：UObject 对象模型、UHT 代码生成、垃圾回收与 `UPROPERTY`/`UFUNCTION`/`UCLASS` 宏系统；
- **生命周期与 Tick**：Actor 与 Component 从生成、注册、BeginPlay 到销毁的完整流程，以及引擎的 Tick 调度体系；
- **Gameplay 框架**：GameMode / GameState / PlayerController / Pawn / Character 等框架类的职责划分与协作方式；
- **模块与启动**：UE 模块系统（Build.cs / Target.cs / 插件）与引擎启动各阶段的执行顺序。

这四个主题是所有上层知识（网络同步、动画、AI、UMG、GameplayAbility 等）的共同前提。建议按顺序阅读，并在自己的小工程中动手验证文中代码示例。

## 文件列表

| 文件 | 一句话简介 |
| --- | --- |
| [01-UObject与反射系统.md](./01-UObject与反射系统.md) | UObject 对象模型、反射机制（UHT）、垃圾回收（GC）以及 UPROPERTY/UFUNCTION/UCLASS/UENUM 宏系统详解，含对象创建、CDO、引用追踪与 RPC 反射基础。 |
| [02-Actor与Component生命周期.md](./02-Actor与Component生命周期.md) | AActor 与 UActorComponent 的生成、注册、初始化、BeginPlay/EndPlay、Tick 调度与销毁全流程，含 UE5 延迟 BeginPlay 与流送场景说明。 |
| [03-Gameplay框架与游戏模式.md](./03-Gameplay框架与游戏模式.md) | GameMode/GameState/PlayerController/Pawn/Character/PlayerState 等框架类职责、登录与 Possess 流程、Match State 状态机及多人协作分工。 |
| [04-引擎启动流程与模块架构.md](./04-引擎启动流程与模块架构.md) | UE 模块系统（IModuleInterface、LoadingPhase、Build.cs/Target.cs、插件）与引擎 PreInit→Init→LoadMap 启动时序，含自定义模块示例。 |
| [05-场景组件与变换体系.md](./05-场景组件与变换体系.md) | USceneComponent 组件树：Attach 挂接、相对/世界变换、Socket 挂点、注册与变换更新、组件查询。 |
| [06-定时器与引擎Ticker.md](./06-定时器与引擎Ticker.md) | FTimerManager 定时器（SetTimer/循环/暂停/时间膨胀/服务器定时器）与引擎级 FTSTicker。 |
| [07-World关卡与Subsystem体系.md](./07-World关卡与Subsystem体系.md) | UWorld/ULevel 组成、关卡与流送概念，以及 UWorld/GameInstance/Engine/LocalPlayer 四种子系统生命周期。 |
| [08-关卡流送LevelStreaming.md](./08-关卡流送LevelStreaming.md) | 传统 Level Streaming：Persistent/Streaming Level、流送体积、Load/Unload 流程、性能与网络注意点。 |
| [09-WorldPartition大世界.md](./09-WorldPartition大世界.md) | World Partition：ActorDesc 分散、DataLayer、运行时流送策略、HLOD 与大型开放世界实践。 |
| [10-FName与FText底层.md](./10-FName与FText底层.md) | FName（FNamePool/全局名表）、FString 堆分配陷阱、FText 本地化管线与三者选型。 |
| [11-多线程与任务系统.md](./11-多线程与任务系统.md) | 线程模型与任务系统使用层：FRunnable/FThread、FAsyncTask、Async()/ParallelFor、TaskGraph 与 ENamedThreads、线程安全原语与 UObject 线程限制。 |

## 学习顺序建议

### 路线一：按依赖顺序精读（推荐）

1. **先读 `01-UObject与反射系统.md`**。反射与 GC 是 UE 一切对象行为的地基：不理解 `UPROPERTY` 引用追踪，就无法解释 Actor 为什么"莫名其妙被回收"；不理解 UHT 生成代码，就无法理解蓝图与 C++ 是如何"打通"的。
2. **再读 `02-Actor与Component生命周期.md`**。在理解 UObject 之后，学习引擎中最常用的两个类（Actor/Component）何时构造、何时 BeginPlay、何时 Tick、何时销毁，以及它们的初始化顺序。
3. **然后读 `03-Gameplay框架与游戏模式.md`**。有了对象模型与生命周期基础，再学习游戏性框架类之间的"生产关系"：谁创建谁、谁复制给谁、谁拥有谁。
4. **最后读 `04-引擎启动流程与模块架构.md`**。此时你已熟悉引擎内的对象行为，再回头看引擎"开机"全过程与模块加载时序，会对之前所有机制的触发时机产生全局理解（例如：为什么 BeginPlay 在 LoadMap 之后）。

### 路线二：按实践需求速查

- 只想快速上手写游戏逻辑：先读 03 与 02，遇到对象被回收/属性不生效再回头查 01；
- 正在排查启动崩溃或模块不加载：直接读 04；
- 正在做内存/GC 优化：精读 01 的 GC 章节 + 02 的销毁章节。

### 配套练习建议

1. 新建一个空 C++ 工程，定义一个 `UCLASS` 的 `UMyItem`，在关卡蓝图中动态 `NewObject` 并打印属性——验证反射与 GC 章节的内容；
2. 自定义 `AActor` 与 `UActorComponent`，在各生命周期函数中打印日志，观察 `SpawnActor` 后的完整调用顺序；
3. 创建自定义 `AGameMode`、`ACharacter` 并设置为默认，跑一个多人联机（Listen Server）验证 GameState 复制与 RPC；
4. 在工程里添加一个自定义 Runtime 模块（LoadingPhase=Default），在 `StartupModule` 打印日志，对比启动日志中模块加载的先后顺序。

## 前置知识

- C++ 基础（类、继承、虚函数、智能指针概念）；
- 基本了解虚幻编辑器界面（Content Browser、关卡编辑器、蓝图编辑器）；
- 了解蓝图与 C++ 的"双向"关系（蓝图继承 C++ 类、C++ 暴露节点给蓝图）。

## 后续分类预告

完成本分类后，可继续学习知识库中的后续分类：网络同步与 RPC 深入、动画系统（Animation Blueprint）、AI（Behavior Tree / EQS）、UI（UMG/Slate）、GameplayAbility 框架、渲染与 Niagara 等。这些分类都将反复引用本分类中的对象模型、生命周期与框架类概念。

## 阅读约定

- 文中代码以 UE5（5.0+）为主，兼容 UE4.27；个别 API 差异会标注版本；
- 所有代码示例建议放入独立测试工程验证；
- "服务器/客户端"指 Dedicated/Listen Server 与 Client 的网络角色划分。
