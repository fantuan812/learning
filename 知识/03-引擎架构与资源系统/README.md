---
type: Index
title: "引擎架构与资源系统"
status: stable
verified: []
maturity: L0
updated: 2026-10-03
---

# 引擎架构与资源系统

> 知识成熟度：L0（导航条目，不代表主题内容已实测）

本页按知识职责导航，每份正文只登记一个主域。概念、实现、案例和来源分别列出；阅读材料的关联不等于已验证其全部结论。

正文迁移沿用稳定身份，不复制另一份权威正文。书籍与工作日志原文、日期、附件完整保留。

## 分类目录

- [插件装配与初始化](<插件装配与初始化/README.md>)

## 概念与机制

- [01 UObject 与反射系统](<../../游戏知识/01-引擎基础/01-UObject与反射系统.md>)
- [02 Actor 与 Component 生命周期](<../../游戏知识/01-引擎基础/02-Actor与Component生命周期.md>)
- [03 Gameplay 框架与游戏模式](<../../游戏知识/01-引擎基础/03-Gameplay框架与游戏模式.md>)
- [04 引擎启动流程与模块架构](<../../游戏知识/01-引擎基础/04-引擎启动流程与模块架构.md>)
- [06 定时器与引擎 Ticker](<../../游戏知识/01-引擎基础/06-定时器与引擎Ticker.md>)
- [07 World 关卡与 Subsystem 体系](<../../游戏知识/01-引擎基础/07-World关卡与Subsystem体系.md>)
- [08 关卡流送（Level Streaming）](<../../游戏知识/01-引擎基础/08-关卡流送LevelStreaming.md>)
- [09 World Partition 大世界](<../../游戏知识/01-引擎基础/09-WorldPartition大世界.md>)
- [10 FName / FString / FText 底层](<../../游戏知识/01-引擎基础/10-FName与FString底层.md>)
- [11 多线程与任务系统（Multithreading & Task Systems）](<../../游戏知识/01-引擎基础/11-多线程与任务系统.md>)
- [04 委托、事件与对象通信](<../../游戏知识/03-游戏玩法编程/04-委托事件与对象通信.md>)
- [04 PCG 程序化内容生成（Procedural Content Generation）](<../../游戏知识/13-世界构建与过场/04-PCG程序化内容生成.md>)
- [05 GameFeatures 特性插件](<插件装配与初始化/05-GameFeatures特性插件.md>)

## 源码解析

- [UE 引擎源码分析 01：UPROPERTY 与反射系统源码剖析](<../../游戏知识/12-引擎源码分析/01-UPROPERTY与反射系统源码.md>)
- [UE 引擎源码分析 02：UObject 与垃圾回收源码剖析](<../../游戏知识/12-引擎源码分析/02-UObject与垃圾回收源码.md>)
- [UE 引擎源码分析 03：Actor 与 Component 生命周期源码剖析](<../../游戏知识/12-引擎源码分析/03-Actor与Component生命周期源码.md>)
- [UE 引擎源码分析 06：委托与事件系统源码剖析](<../../游戏知识/12-引擎源码分析/06-委托与事件系统源码.md>)
- [UE 引擎源码分析 07：容器与内存管理源码剖析](<../../游戏知识/12-引擎源码分析/07-容器与内存管理源码.md>)
- [UE 引擎源码分析 08：Tick 调度与模块系统源码剖析](<../../游戏知识/12-引擎源码分析/08-Tick与模块系统源码.md>)
- [UE 引擎源码分析 13：资源加载与异步加载源码剖析](<../../游戏知识/12-引擎源码分析/13-资源加载与异步加载源码.md>)
- [UE 引擎源码分析 22：World Partition 与 World Streaming 源码分析](<../../游戏知识/12-引擎源码分析/22-WorldPartition与WorldStreaming源码.md>)
- [UE 引擎源码分析 38：PCG 程序化内容生成源码剖析（UE5.8）](<../../游戏知识/12-引擎源码分析/38-PCG源码.md>)
- [UE5.8 Lyra 源码解析 40：Experience 与 GameFeature 玩法装配](<../../游戏知识/12-引擎源码分析/40-Lyra-Experience与GameFeature源码.md>)
- [UE5.8 Lyra 源码解析 41：Pawn 初始化与模块化组件状态机](<../../游戏知识/12-引擎源码分析/41-Lyra-Pawn初始化与模块化组件源码.md>)
- [UE5.8 Lyra 源码解析 48：扩展插件源码](<../../游戏知识/12-引擎源码分析/48-Lyra扩展插件源码.md>)

## 书籍阅读关联

- [第2章 Efficient, Event-Based Simulations](<../../读书笔记/GameAIPro/卷4-OnlineEdition2021/02-efficient-event-based-simulations.md>)
- [第1章 导论（Introduction）](<../../读书笔记/游戏引擎架构/卷1-基础与核心引擎系统/01-导论.md>)
- [第6章 引擎支持系统（Engine Support Systems）](<../../读书笔记/游戏引擎架构/卷1-基础与核心引擎系统/06-引擎支持系统.md>)
- [第7章 资源与文件系统（Resources and the File System）](<../../读书笔记/游戏引擎架构/卷1-基础与核心引擎系统/07-资源与文件系统.md>)
- [第8章 游戏循环与实时模拟（The Game Loop and Real-Time Simulation）](<../../读书笔记/游戏引擎架构/卷1-基础与核心引擎系统/08-游戏循环与实时模拟.md>)
- [第16章 玩法系统导论（Introduction to Gameplay Systems）](<../../读书笔记/游戏引擎架构/卷2-图形动作与声音/16-玩法系统导论.md>)
- [第17章 运行时玩法系统（Runtime Gameplay Systems）](<../../读书笔记/游戏引擎架构/卷2-图形动作与声音/17-运行时玩法系统.md>)
- [组件模式（Component）](<../../读书笔记/游戏编程模式/component.md>)
- [游戏循环（Game Loop）](<../../读书笔记/游戏编程模式/game-loop.md>)
- [序列模式（Sequencing Patterns）](<../../读书笔记/游戏编程模式/sequencing-patterns.md>)
- [更新方法（Update Method）](<../../读书笔记/游戏编程模式/update-method.md>)

## 主题路线

- [游戏全栈运行闭环总览](<../../游戏知识/00-全栈运行闭环.md>)
- [UE 引擎源码分析 19：UE5.8 高优先级源码覆盖路线图](<../../游戏知识/12-引擎源码分析/19-高优先级源码覆盖路线图.md>)

## 边界与扩展

一个主题按主要问题确定主责，其他领域引用它。运行在服务端的玩法规则仍属于 Gameplay；UE 源码分析按具体机制归类。新概念先找已有主文，再决定扩充或新建。

[八域总览](<../README.md>) · [主责与关系](<../../00_Index/跨域关系.md>)
