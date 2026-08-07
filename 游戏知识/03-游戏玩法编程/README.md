# 03 · 游戏玩法编程（Gameplay Programming）

> 面向 Unreal Engine 5 客户端开发者的中文知识库分类。本目录收录与"玩法逻辑"直接相关的核心编程主题：能力系统（GAS）、增强输入（Enhanced Input）、GameplayTag 与数据资产、委托与对象通信、蓝图与 C++ 协作。
>
> 每篇文档独立成文，统一采用「概述 → 核心概念 → 原理详解 → 代码示例 → 最佳实践 → 常见问题 FAQ → 关联阅读」的结构，示例以 C++ 为主、蓝图操作为辅。

## 分类简介

游戏玩法编程（Gameplay Programming）是 UE 客户端开发中最贴近"游戏性"的一层：

- **输入**：玩家操作如何从硬件设备（键盘、鼠标、手柄）流转到角色行为，UE5 的 Enhanced Input 提供了可配置、可重绑、支持触发条件与修饰器的现代输入管线。
- **能力系统（GAS）**：以 `AbilitySystemComponent` 为核心的属性、效果、技能、任务框架，是 RPG 与动作类游戏"技能/状态/数值"体系的事实标准。
- **标签与数据**：GameplayTag 为运行时状态提供轻量、可组合、可查询的语义标记；DataAsset / DataTable 负责把"数值与配置"从代码中剥离出来，交给策划与设计同学维护。
- **通信机制**：委托（Delegate / Event / 动态委托 / 多播 / Lambda）是 UE 对象间解耦通信的基石，也是 UI 刷新、事件驱动逻辑的核心。
- **双栈协作**：蓝图与 C++ 的分工与互操作（反射、`UFUNCTION` / `UPROPERTY`、继承与覆写）决定了项目的架构风格与迭代效率。

学习本分类前，建议先掌握 [01-引擎基础](../01-引擎基础/README.md) 中的 UObject 反射、Actor 生命周期等基础知识。

## 文件列表与一句话简介

| 文件 | 主题 | 一句话简介 |
| --- | --- | --- |
| [01-GameplayAbilitySystem能力系统.md](01-GameplayAbilitySystem能力系统.md) | GAS 能力系统 | 详解 ASC、GameplayAbility、GameplayEffect、AttributeSet、AbilityTask 五大核心及网络模型，附完整 C++ 示例。 |
| [02-EnhancedInput增强输入.md](02-EnhancedInput增强输入.md) | 增强输入 | 讲解 InputAction / InputMappingContext、触发条件与修饰器管线，并与旧输入系统对比迁移。 |
| [03-GameplayTag与数据资产.md](03-GameplayTag与数据资产.md) | 标签与数据资产 | 讲解 GameplayTag 层次、Container、Query 查询，以及 DataAsset / DataTable 的配置化实践。 |
| [04-委托事件与对象通信.md](04-委托事件与对象通信.md) | 委托与通信 | 系统梳理单播/多播/动态委托/事件与 Lambda 绑定的原理、写法与生命周期安全。 |
| [05-蓝图与C++协作.md](05-蓝图与C++协作.md) | 蓝图与 C++ 协作 | 围绕反射与 `UFUNCTION` / `UPROPERTY` 说明符，讲解事件、覆写、属性暴露与双向调用。 |
| [06-角色移动系统UCharacterMovement.md](06-角色移动系统UCharacterMovement.md) | 角色移动 | UCharacterMovementComponent 四种移动模式、速度/加速度模型、跳跃与 Custom 移动扩展。 |
| [07-相机系统与视口.md](07-相机系统与视口.md) | 相机系统 | UCameraComponent、PlayerCameraManager、ViewTarget 混合、CameraModifier/Shake 与 SpringArm。 |
| [08-ModularGameplay模块化玩法.md](08-ModularGameplay模块化玩法.md) | 模块化玩法 | GameFrameworkComponentManager、InitState 状态机与组件化 Actor 架构（Lyra 式）。 |
| [09-GameplayTask任务框架.md](09-GameplayTask任务框架.md) | 任务框架 | UGameplayTask/TasksComponent 生命周期与调度，GAS AbilityTask 的底层宿主。 |
| [10-输入设备抽象与手柄触控.md](10-输入设备抽象与手柄触控.md) | 输入设备 | FKey/EKeys、PlayerInput 处理栈、Gamepad 力反馈、触控与 InputSettings 配置。 |
| [11-常用移动与辅助组件.md](11-常用移动与辅助组件.md) | 移动辅助组件 | Projectile/Rotating/InterpTo/SpringArm/Cable 等常用组件的原理与用法。 |
| [12-SaveGame存档系统与序列化.md](12-SaveGame存档系统与序列化.md) | 存档系统 | USaveGame 基类与序列化链路、SaveGameToSlot/Async 读写、槽位管理、版本迁移与平台差异。 |
| [13-背包与装备系统.md](13-背包与装备系统.md) | 背包与装备 | 物品定义/实例分离、Inventory 组件、装备槽、FastArray 网络同步、UI 绑定与 Lyra Inventory 参考。 |

## 学习顺序建议

### 第 1 步：先建立"通信与协作"心智模型

1. **04-委托事件与对象通信**：无论 C++ 还是蓝图，对象间通信都依赖委托，先掌握绑定、广播、解绑与生命周期安全。
2. **05-蓝图与C++协作**：理解 `BlueprintImplementableEvent` / `BlueprintNativeEvent` / `BlueprintCallable` 与属性暴露，才能看懂后续所有"C++ 提供能力、蓝图编排玩法"的示例。

### 第 2 步：处理"输入与配置"

3. **02-EnhancedInput增强输入**：输入是玩法交互的入口，掌握 Action / MappingContext / 触发与修饰器。
4. **03-GameplayTag与数据资产**：标签是 GAS 的"语言"，数据资产是数值配置的基础，为进入 GAS 做准备。

### 第 3 步：进阶玩法框架

5. **01-GameplayAbilitySystem能力系统**：GAS 综合了属性、效果、技能、任务与网络，是玩法编程的集大成者，建议放在最后系统学习。

> 若只想快速上手某个功能（例如"给角色加一个技能"），也可以直接阅读对应单篇，文中已尽量自包含。

## 撰写规范（与总库对齐）

- 每篇文档包含：核心概念（表格）→ 原理详解 → 代码示例 → 最佳实践 → 常见问题。
- 涉及框架 / 流程处使用 Mermaid 图辅助说明。
- 示例代码以 C++ 为主，必要时补充蓝图操作说明。
- 代码基于 UE 5.x（文中标注版本差异时以 UE 5.8 为准）。

## 关联阅读

- [01-引擎基础](../01-引擎基础/README.md)：UObject / 反射 / Actor 生命周期，本分类的前置知识。
- [04-动画系统](../04-动画系统/README.md)：GAS 中的 Ability 常通过 AnimInstance 通知与动画蓝图联动。
- [05-AI系统](../05-AI系统/README.md)：行为树任务常通过委托与 GAS / 输入系统交互。
- [06-网络同步](../06-网络同步/README.md)：GAS 的网络授权模型、RPC 与属性复制与本分类的 GAS 篇直接相关。
- [07-UI与性能优化](../07-UI与性能优化/README.md)：UMG 绑定动态委托实现 UI 刷新，见委托篇与 GAS 篇。
