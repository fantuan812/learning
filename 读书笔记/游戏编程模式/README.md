---
type: Index
title: "游戏编程模式"
description: "Bob Nystrom 经典著作《Game Programming Patterns》（游戏编程模式）全书27章中文权威重构版，完整收录全部图表架构与C++源码实现。"
tags:
  - reading-notes
  - game-programming-patterns
  - design-patterns
  - cpp
status: stable
verified: []
maturity: L1
updated: 2026-09-14
---

# 游戏编程模式（Game Programming Patterns）

> 原著作者：**Bob Nystrom**（Robert Nystrom，前 EA 资深程序员，《Crafting Interpreters》作者）  
> 官方网站：[gameprogrammingpatterns.com](https://gameprogrammingpatterns.com/)  
> 中文站点：[gpp.tkchu.me](https://gpp.tkchu.me) / [GitHub: tkchu/Game-Programming-Patterns-CN](https://github.com/tkchu/Game-Programming-Patterns-CN)  
> 本地归档：`读书笔记/游戏编程模式/`（全书完结，包含全部 6 大知识板块、27 个核心章节、69 张高清架构拓扑图与完整 C++ 示例代码）

---

## 嘿，游戏开发者们！

- 为代码整体规划而苦苦挣扎？
- 发现随着代码库增长，越来越难以做出改动？
- 感觉你的游戏就是一个大毛线球，任何东西都和其他所有东西缠绕在一起？
- 思考有哪些经典设计模式可以巧妙应用到游戏工程中？
- 听说过“数据局部性”、“缓存命中”和“对象池”，却不知道如何落地使用它们来大幅加速你的游戏？

《游戏编程模式》正是为了回答这些问题而生！本书是作者在 EA 等工业级游戏研发一线奋战多年实践经验的精华结晶，总结了游戏开发中最核心的设计模式与架构策略，旨在让游戏代码更整洁、更易懂、更容易扩展、运行速度更快。

---

## 全书目录导航


### 序言与导读（Introduction）

- **[致谢（Acknowledgements）](acknowledgements.md)**：作者 Bob Nystrom 对参与本书审阅、反馈、编辑与支持的所有同行与朋友们的真挚谢意。
- **[序（Introduction）](introduction.md)**：游戏编程的初衷、代码架构与性能之间的深刻辩证关系，以及本书核心理念的诞生历程。
- **[架构，性能和游戏（Architecture, Performance, and Games）](architecture-performance-and-games.md)**：什么是优秀的软件架构？在游戏开发中如何在代码的可维护性、灵活性与极限运行性能之间做出平衡与取舍。

### 重访设计模式（Design Patterns Revisited）

- **[重访设计模式（Design Patterns Revisited）](design-patterns-revisited.md)**：经典 GoF 设计模式在游戏工程语境下的重新审视、扬弃与现代化演进。
- **[命令模式（Command）](command.md)**：将请求封装为对象：命令是具现化的方法调用，用于输入映射、AI驱动、可撤销重做与网络同步。
- **[享元模式（Flyweight）](flyweight.md)**：通过在多个上下文之间共享微小对象的状态，消除数以万计同类实体的海量内存重复开销。
- **[观察者模式（Observer）](observer.md)**：在对象间定义一对多依赖关系，实现成就系统、物理碰撞与事件解耦，并探讨其在游戏中的内存与性能陷阱。
- **[原型模式（Prototype）](prototype.md)**：通过克隆现有原型实例来创建新对象，探讨基于原型的语言机制、数据建模与Spawner生成器应用。
- **[单例模式（Singleton）](singleton.md)**：单例模式的深度反思与警示：全局状态的危害、生命周期陷阱，以及传递上下文、静态工具类等现代替代方案。
- **[状态模式（State）](state.md)**：从庞大的 switch-case 分支到有限状态机（FSM）、并发状态机与下推自动机，彻底理顺角色的复杂动作状态流转。

### 序列模式（Sequencing Patterns）

- **[序列模式（Sequencing Patterns）](sequencing-patterns.md)**：游戏时间、帧率与模拟步长的掌控：让离散计算机模拟真实世界中连续流淌的时间与瞬时交互。
- **[双缓冲模式（Double Buffer）](double-buffer.md)**：让一系列顺序执行的操作看起来是瞬间完成或同时发生的，广泛应用于图形渲染帧缓冲与游戏世界多实体状态同步。
- **[游戏循环（Game Loop）](game-loop.md)**：游戏引擎的心跳：将游戏的逻辑进行和玩家输入、硬件刷新率与处理器计算速度深度解耦。
- **[更新方法（Update Method）](update-method.md)**：让世界中的每个对象每帧独立处理一小步行为，通过统一的 Update 轮询模拟一群并发活体实体的动态交互。

### 行为模式（Behavioral Patterns）

- **[行为模式（Behavioral Patterns）](behavioral-patterns.md)**：为游戏对象注入丰富生命力与复杂逻辑，同时避免陷入庞大臃肿的继承体系与脚本维护泥潭。
- **[字节码（Bytecode）](bytecode.md)**：将行为编码为虚拟机指令，赋予其数据的灵活性：在不重新编译C++代码的前提下让策划安全地编写复杂的法术与AI逻辑。
- **[子类沙箱（Subclass Sandbox）](subclass-sandbox.md)**：用基类封装好的高质量操作集定义子类行为，为策划与内容创作者构建安全受控的技能创作沙盒。
- **[类型对象（Type Object）](type-object.md)**：通过创建一个类让其实例代表一种对象类型，将硬编码的C++类继承转化为纯数据驱动的动态品种与实体系统。

### 解耦模式（Decoupling Patterns）

- **[解耦模式（Decoupling Patterns）](decoupling-patterns.md)**：切断代码模块之间的脆弱纠缠，让各子系统能够独立迭代、测试与复用，防止代码膨胀为不可收拾的巨型毛线球。
- **[组件模式（Component）](component.md)**：允许单一游戏实体跨越渲染、物理、音效与AI等多个领域而互不耦合，现代游戏引擎对象架构的核心基石。
- **[事件队列（Event Queue）](event-queue.md)**：解耦消息或事件的发送时间和处理时间，平滑突发请求峰值，并实现多线程安全与异步音频调度。
- **[服务定位器（Service Locator）](service-locator.md)**：提供服务的全局访问入口，避免使用者与底层具体实现类紧密绑定，支持运行时无缝替换音频或日志服务提供者。

### 优化模式（Optimization Patterns）

- **[优化模式（Optimization Patterns）](optimization-patterns.md)**：利用现代硬件物理特性（CPU缓存行、内存对齐、指令流水线）榨取极限算力，保障丝滑的实时帧率。
- **[数据局部性（Data Locality）](data-locality.md)**：合理组织内存布局，充分利用CPU高速缓存行（L1/L2/L3 Cache）消除致命的Cache Miss，现代面向数据设计（DOD）的基础。
- **[脏标识模式（Dirty Flag）](dirty-flag.md)**：将耗时计算推迟至真正需要其结果时才执行，避免在场景图（Scene Graph）世界变换矩阵计算中的海量重复运算。
- **[对象池模式（Object Pool）](object-pool.md)**：通过从预分配的固定池中循环复用对象，消除高频实体创建/销毁带来的内存碎片与堆分配性能抖动。
- **[空间分区（Spatial Partition）](spatial-partition.md)**：将对象按三维/二维空间几何位置组织在网格、四叉树或八叉树中，将O(N^2)的爆炸性碰撞检测复杂度骤降至近常数级。

---

## 作者与创作背景

![Bob Nystrom](images/dogshot.jpg)

**Bob Nystrom**：他在 EA（Electronic Arts）工作的 8 年时间里，参与了诸多大型游戏的研发。在那段岁月里，他见过许多极其优美优雅的代码，也领教过许多令人窒息的可怕代码。他希望能将从这些优美代码中学到的精髓系统性地总结沉淀下来，帮助更多开发者写出高质量、高可维护性、兼顾极致运行效率的游戏系统。

如果你想与作者进一步交流，可以在 Twitter/X 上关注 [`@munificentbob`](https://twitter.com/intent/user?screen_name=munificentbob)，或阅读他的另一部经典力作《Crafting Interpreters》。

---

## 规格与工程复刻说明

1. **完整性**：100% 完整复刻 `https://gpp.tkchu.me` 全书所有章节内容，包含全部 27 章正文、代码实现与全部作者旁注；
2. **源码闭环**：全书 268 处核心 C++ 算法与系统代码均从 `code/cpp/` 源码库完整抽提并嵌入标准 Markdown 代码围栏（`cpp` 语言高亮），杜绝外链失效；
3. **高清图表**：全书 69 张高清架构流程图、内存布局图与 UML 拓扑图完整保存在本地 `images/` 目录并采用标准 Markdown 引用；
4. **双向导航**：各章节均内嵌上下文面包屑、前驱/后继章节导航与主目录跳转，在 Obsidian、VS Code 及 GitHub 网页端均可实现无缝平滑浏览体验。
