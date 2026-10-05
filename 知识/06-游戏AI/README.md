---
type: Index
title: "游戏AI"
status: stable
verified: []
maturity: L0
---

# 游戏AI

> 知识成熟度：L0（导航条目，不代表主题内容已实测）

本页按知识职责导航，每份正文只登记一个主域。概念、实现、案例和来源分别列出；阅读材料的关联不等于已验证其全部结论。

正文迁移沿用稳定身份，不复制另一份权威正文。书籍与工作日志原文、日期、附件完整保留。

## 分类目录

- [学习适应与社会行为](<学习适应与社会行为/README.md>)
- [导航移动与群体协同](<导航移动与群体协同/README.md>)
- [感知决策与行为规划](<感知决策与行为规划/README.md>)
- [战斗战术与机器人](<战斗战术与机器人/README.md>)
- [评测安全与运行预算](<评测安全与运行预算/README.md>)

## 概念与机制

- [游戏AI「学习型AI」全解](<学习适应与社会行为/03-学习型AI.md>)
- [玩家建模与自适应难度（Player Modeling / DDA）](<学习适应与社会行为/05-玩家建模与自适应难度.md>)
- [多智能体协作与强化学习（MARL）](<学习适应与社会行为/06-多智能体协作与强化学习.md>)
- [07 NPC 人格、对话与社交 AI](<学习适应与社会行为/07-NPC人格对话与社交AI.md>)
- [游戏AI「移动与群组行为」全解](<导航移动与群体协同/01-移动与群组行为.md>)
- [03 NavMesh 寻路](<导航移动与群体协同/03-NavMesh寻路.md>)
- [04 Mass 实体框架与群集模拟（Mass Entity Framework & Crowd Simulation）](<导航移动与群体协同/04-Mass实体框架与群集模拟.md>)
- [06 ZoneGraph 与 SmartObjects](<导航移动与群体协同/06-ZoneGraph与SmartObjects.md>)
- [01 AI 总体架构与感知](<感知决策与行为规划/01-AI总体架构与感知.md>)
- [01 行为树详解（Behavior Tree）](<感知决策与行为规划/01-行为树详解.md>)
- [02 感知系统与 EQS](<感知决策与行为规划/02-感知系统与EQS.md>)
- [02 状态机与层次状态机（FSM / HSM）](<感知决策与行为规划/02-状态机与层次状态机.md>)
- [03 行为树通用原理（Behavior Tree）](<感知决策与行为规划/03-行为树通用原理.md>)
- [04 效用 AI 与 GOAP（Utility AI / GOAP / HTN）](<感知决策与行为规划/04-Utility与GOAP.md>)
- [05 StateTree 状态树（State Tree）](<感知决策与行为规划/05-StateTree状态树.md>)
- [05 博弈搜索与对战 AI（Minimax / Alpha-Beta / MCTS）](<感知决策与行为规划/05-博弈搜索与对战AI.md>)
- [06 模糊逻辑与连续决策（Fuzzy Logic）](<感知决策与行为规划/06-模糊逻辑与连续决策.md>)
- [07 GameplayTasks、StateTree、GAS 与 AI 协同闭环](<感知决策与行为规划/07-GameplayTasks-StateTree-GAS-AI协同.md>)
- [游戏AI「战斗与Boss设计」全解](<战斗战术与机器人/02-战斗与Boss设计.md>)
- [07 战斗AI编排与战术协同](<战斗战术与机器人/07-战斗AI编排与战术协同.md>)
- [游戏AI 评测与回放](<评测安全与运行预算/01-AI评测回放.md>)
- [自动测试玩家与 AI 回归基准（Playtesting Bots）](<评测安全与运行预算/02-自动测试玩家与AI回归基准.md>)
- [游戏AI LLM NPC 安全](<评测安全与运行预算/03-LLM-NPC安全.md>)
- [游戏AI「服务端AI与性能」全解](<评测安全与运行预算/04-服务端AI与性能.md>)
- [08 AI 调试与性能分析（AI Debugging & Performance Analysis）](<评测安全与运行预算/08-AI调试与性能分析.md>)

## 源码解析

- [UE 引擎源码分析 21：Mass 与 StateTree 源码分析](<导航移动与群体协同/21-Mass与StateTree源码.md>)
- [UE 行为树执行机制：公开合同与历史源码片段对照](<感知决策与行为规划/12-行为树与AI源码.md>)
- [UE5.8 Lyra 源码解析 46：AI 机器人与队伍系统](<战斗战术与机器人/46-Lyra-AI机器人与队伍源码.md>)

## 端到端案例

- [07-假人AI完整链路](<战斗战术与机器人/07-假人AI完整链路.md>)

## 书籍阅读关联

- [第1章 What is Game AI?](<../../读书笔记/GameAIPro/卷1-GameAIPro1/01-what-is-game-ai.md>)
- [第2章 Informing Game AI through the Study of Neurology](<../../读书笔记/GameAIPro/卷1-GameAIPro1/02-informing-game-ai-through-the-study-of-n.md>)
- [第4章 Behavior Selection Algorithms: An Overview](<../../读书笔记/GameAIPro/卷1-GameAIPro1/04-behavior-selection-algorithms-an-overvie.md>)
- [第5章 Structural Architecture—Common Tricks of the Trade](<../../读书笔记/GameAIPro/卷1-GameAIPro1/05-structural-architecture-common-tricks-of.md>)
- [第6章 The Behavior Tree Starter Kit](<../../读书笔记/GameAIPro/卷1-GameAIPro1/06-the-behavior-tree-starter-kit.md>)
- [第7章 Real-World Behavior Trees in Script](<../../读书笔记/GameAIPro/卷1-GameAIPro1/07-real-world-behavior-trees-in-script.md>)
- [第8章 Simulating Behavior Trees: A Behavior Tree / Planner Hybrid Approach](<../../读书笔记/GameAIPro/卷1-GameAIPro1/08-simulating-behavior-trees-a-behavior-tre.md>)
- [第9章 An Introduction to Utility Theory](<../../读书笔记/GameAIPro/卷1-GameAIPro1/09-an-introduction-to-utility-theory.md>)
- [第10章 Building Utility Decisions into Your Existing Behavior Tree](<../../读书笔记/GameAIPro/卷1-GameAIPro1/10-building-utility-decisions-into-your-exi.md>)
- [第11章 Reactivity and Deliberation in Decision Making Systems](<../../读书笔记/GameAIPro/卷1-GameAIPro1/11-reactivity-and-deliberation-in-decision-.md>)
- [第12章 Exploring HTN Planners through Example](<../../读书笔记/GameAIPro/卷1-GameAIPro1/12-exploring-htn-planners-through-example.md>)
- [第13章 Hierarchical Plan-Space Planning for Multi-Unit Combat Maneuvers](<../../读书笔记/GameAIPro/卷1-GameAIPro1/13-hierarchical-plan-space-planning-for-mul.md>)
- [第14章 Phenomenal AI Level-of-Detail Control with the LOD Trader](<../../读书笔记/GameAIPro/卷1-GameAIPro1/14-phenomenal-ai-level-of-detail-control-wi.md>)
- [第16章 Plumbing the Forbidden Depths: Scripting and AI](<../../读书笔记/GameAIPro/卷1-GameAIPro1/16-plumbing-the-forbidden-depths-scripting-.md>)
- [第21章 Techniques for Formation Movement using Steering Circles](<../../读书笔记/GameAIPro/卷1-GameAIPro1/21-techniques-for-formation-movement-using-.md>)
- [第26章 Tactical Position Selection: An Architecture and Query Language](<../../读书笔记/GameAIPro/卷1-GameAIPro1/26-tactical-position-selection-an-architect.md>)
- [第27章 Tactical Pathfinding on a NavMesh](<../../读书笔记/GameAIPro/卷1-GameAIPro1/27-tactical-pathfinding-on-a-navmesh.md>)
- [第28章 Beyond the Kung-Fu Circle: A Flexible System for Managing NPC Attacks](<../../读书笔记/GameAIPro/卷1-GameAIPro1/28-beyond-the-kung-fu-circle-a-flexible-sys.md>)
- [第29章 Hierarchical AI for Multiplayer Bots in Killzone 3](<../../读书笔记/GameAIPro/卷1-GameAIPro1/29-hierarchical-ai-for-multiplayer-bots-in-.md>)
- [第30章 Using Neural Networks to Control Agent Threat Response](<../../读书笔记/GameAIPro/卷1-GameAIPro1/30-using-neural-networks-to-control-agent-t.md>)
- [第32章 How to Catch a Ninja: NPC Awareness in a 2D Stealth Platformer](<../../读书笔记/GameAIPro/卷1-GameAIPro1/32-how-to-catch-a-ninja-npc-awareness-in-a-.md>)
- [第33章 Asking the Environment Smart Questions](<../../读书笔记/GameAIPro/卷1-GameAIPro1/33-asking-the-environment-smart-questions.md>)
- [第34章 A Simple and Robust Knowledge Representation System](<../../读书笔记/GameAIPro/卷1-GameAIPro1/34-a-simple-and-robust-knowledge-representa.md>)
- [第35章 A Simple and Practical Social Dynamics System](<../../读书笔记/GameAIPro/卷1-GameAIPro1/35-a-simple-and-practical-social-dynamics-s.md>)
- [第36章 Breathing Life into Your Background Characters](<../../读书笔记/GameAIPro/卷1-GameAIPro1/36-breathing-life-into-your-background-char.md>)
- [第37章 Alibi Generation: Fooling All the Players All the Time](<../../读书笔记/GameAIPro/卷1-GameAIPro1/37-alibi-generation-fooling-all-the-players.md>)
- [第38章 An Architecture Overview for AI in Racing Games](<../../读书笔记/GameAIPro/卷1-GameAIPro1/38-an-architecture-overview-for-ai-in-racin.md>)
- [第39章 Representing and Driving a Race Track for AI Controlled Vehicles](<../../读书笔记/GameAIPro/卷1-GameAIPro1/39-representing-and-driving-a-race-track-fo.md>)
- [第41章 The Heat Vision System for Racing AI: A Novel Way to Determine Optimal Track Positioning](<../../读书笔记/GameAIPro/卷1-GameAIPro1/41-the-heat-vision-system-for-racing-ai-a-n.md>)
- [第42章 A Rubber-Banding System for Gameplay and Race Management](<../../读书笔记/GameAIPro/卷1-GameAIPro1/42-a-rubber-banding-system-for-gameplay-and.md>)
- [第43章 An Architecture for Character-Rich Social Simulation](<../../读书笔记/GameAIPro/卷1-GameAIPro1/43-an-architecture-for-character-rich-socia.md>)
- [第44章 A Control-Based Architecture for Animal Behavior](<../../读书笔记/GameAIPro/卷1-GameAIPro1/44-a-control-based-architecture-for-animal-.md>)
- [第48章 Implementing N-grams for Player Prediction, Procedural Generation, and Stylized AI](<../../读书笔记/GameAIPro/卷1-GameAIPro1/48-implementing-n-grams-for-player-predicti.md>)
- [第1章 Game AI Appreciation, Revisited](<../../读书笔记/GameAIPro/卷2-GameAIPro2/01-game-ai-appreciation-revisited.md>)
- [第2章 Combat Dialogue in FEAR: The Illusion of Communication](<../../读书笔记/GameAIPro/卷2-GameAIPro2/02-combat-dialogue-in-fear-the-illusion-of-.md>)
- [第3章 Dual-Utility Reasoning](<../../读书笔记/GameAIPro/卷2-GameAIPro2/03-dual-utility-reasoning.md>)
- [第4章 Vision Zones and Object Identification Certainty](<../../读书笔记/GameAIPro/卷2-GameAIPro2/04-vision-zones-and-object-identification-c.md>)
- [第5章 Agent Reaction Time: How Fast Should an AI React?](<../../读书笔记/GameAIPro/卷2-GameAIPro2/05-agent-reaction-time-how-fast-should-an-a.md>)
- [第7章 Possibility Maps for Opportunistic AI and Believable Worlds](<../../读书笔记/GameAIPro/卷2-GameAIPro2/07-possibility-maps-for-opportunistic-ai-an.md>)
- [第8章 Production Rules Implementation in 1849](<../../读书笔记/GameAIPro/卷2-GameAIPro2/08-production-rules-implementation-in-1849.md>)
- [第9章 Production Systems: New Techniques in AAA Games](<../../读书笔记/GameAIPro/卷2-GameAIPro2/09-production-systems-new-techniques-in-aaa.md>)
- [第10章 Building a Risk-Free Environment to Enhance Prototyping: Hinted-Execution Behavior Trees](<../../读书笔记/GameAIPro/卷2-GameAIPro2/10-building-a-risk-free-environment-to-enha.md>)
- [第11章 Smart Zones to Create the Ambience of Life](<../../读书笔记/GameAIPro/卷2-GameAIPro2/11-smart-zones-to-create-the-ambience-of-li.md>)
- [第12章 Separation of Concerns Architecture for AI and Animation](<../../读书笔记/GameAIPro/卷2-GameAIPro2/12-separation-of-concerns-architecture-for-.md>)
- [第13章 Optimizing Practical Planning for Game AI](<../../读书笔记/GameAIPro/卷2-GameAIPro2/13-optimizing-practical-planning-for-game-a.md>)
- [第18章 Context Steering: Behavior-Driven Steering at the Macro Scale](<../../读书笔记/GameAIPro/卷2-GameAIPro2/18-context-steering-behavior-driven-steerin.md>)
- [第20章 Hierarchical Architecture for Group Navigation Behaviors](<../../读书笔记/GameAIPro/卷2-GameAIPro2/20-hierarchical-architecture-for-group-navi.md>)
- [第22章 Introduction to Search for Games](<../../读书笔记/GameAIPro/卷2-GameAIPro2/22-introduction-to-search-for-games.md>)
- [第23章 Personality Reinforced Search for Mobile Strategy Games](<../../读书笔记/GameAIPro/卷2-GameAIPro2/23-personality-reinforced-search-for-mobile.md>)
- [第24章 Interest Search: A Faster Minimax](<../../读书笔记/GameAIPro/卷2-GameAIPro2/24-interest-search-a-faster-minimax.md>)
- [第25章 Monte Carlo Tree Search and Related Algorithms for Games](<../../读书笔记/GameAIPro/卷2-GameAIPro2/25-monte-carlo-tree-search-and-related-algo.md>)
- [第27章 Looking for Trouble: Making NPCs Search Realistically](<../../读书笔记/GameAIPro/卷2-GameAIPro2/27-looking-for-trouble-making-npcs-search-r.md>)
- [第29章 Escaping the Grid: Infinite-Resolution Influence Mapping](<../../读书笔记/GameAIPro/卷2-GameAIPro2/29-escaping-the-grid-infinite-resolution-in.md>)
- [第30章 Modular Tactical Influence Maps](<../../读书笔记/GameAIPro/卷2-GameAIPro2/30-modular-tactical-influence-maps.md>)
- [第31章 Spatial Reasoning for Strategic Decision Making](<../../读书笔记/GameAIPro/卷2-GameAIPro2/31-spatial-reasoning-for-strategic-decision.md>)
- [第33章 Infected AI in The Last of Us](<../../读书笔记/GameAIPro/卷2-GameAIPro2/33-infected-ai-in-the-last-of-us.md>)
- [第34章 Human Enemy AI in The Last of Us](<../../读书笔记/GameAIPro/卷2-GameAIPro2/34-human-enemy-ai-in-the-last-of-us.md>)
- [第35章 Ellie: Buddy AI in The Last of Us](<../../读书笔记/GameAIPro/卷2-GameAIPro2/35-ellie-buddy-ai-in-the-last-of-us.md>)
- [第38章 Psychologically Plausible Methods for Character Behavior Design](<../../读书笔记/GameAIPro/卷2-GameAIPro2/38-psychologically-plausible-methods-for-ch.md>)
- [第39章 Analytics-Based AI Techniques for a Better Gaming Experience](<../../读书笔记/GameAIPro/卷2-GameAIPro2/39-analytics-based-ai-techniques-for-a-bett.md>)
- [第41章 Simulation Principles from Dwarf Fortress](<../../读书笔记/GameAIPro/卷2-GameAIPro2/41-simulation-principles-from-dwarf-fortres.md>)
- [第42章 Techniques for AI-Driven Experience Management in Interactive Narratives](<../../读书笔记/GameAIPro/卷2-GameAIPro2/42-techniques-for-ai-driven-experience-mana.md>)
- [第1章 The Illusion of Intelligence](<../../读书笔记/GameAIPro/卷3-GameAIPro3/01-the-illusion-of-intelligence.md>)
- [第4章 What You See Is Not What You Get: Player Perception of AI Opponents](<../../读书笔记/GameAIPro/卷3-GameAIPro3/04-what-you-see-is-not-what-you-get-player-.md>)
- [第8章 Modular AI](<../../读书笔记/GameAIPro/卷3-GameAIPro3/08-modular-ai.md>)
- [第9章 Overcoming Pitfalls in Behavior Tree Design](<../../读书笔记/GameAIPro/卷3-GameAIPro3/09-overcoming-pitfalls-in-behavior-tree-des.md>)
- [第10章 From Behavior to Animation: A Reactive AI Architecture for Networked First-Person Shooter Games](<../../读书笔记/GameAIPro/卷3-GameAIPro3/10-from-behavior-to-animation-a-reactive-ai.md>)
- [第11章 A Character Decision-Making System for FINAL FANTASY XV by Combining Behavior Trees and State Machines](<../../读书笔记/GameAIPro/卷3-GameAIPro3/11-a-character-decision-making-system-for-f.md>)
- [第12章 A Reusable, Light-Weight Finite-State Machine](<../../读书笔记/GameAIPro/卷3-GameAIPro3/12-a-reusable-light-weight-finite-state-mac.md>)
- [第13章 Choosing Effective Utility-Based Considerations](<../../读书笔记/GameAIPro/卷3-GameAIPro3/13-choosing-effective-utility-based-conside.md>)
- [第14章 Combining Scripted Behavior with Game Tree Search for Stronger, More Robust Game AI](<../../读书笔记/GameAIPro/卷3-GameAIPro3/14-combining-scripted-behavior-with-game-tr.md>)
- [第15章 Steering against Complex Vehicles in Assassin’s Creed Syndicate](<../../读书笔记/GameAIPro/卷3-GameAIPro3/15-steering-against-complex-vehicles-in-ass.md>)
- [第17章 Fast Cars, Big City: The AI of Driver San Francisco](<../../读书笔记/GameAIPro/卷3-GameAIPro3/17-fast-cars-big-city-the-ai-of-driver-san-.md>)
- [第24章 Being Where It Counts: Telling Paragon Bots Where to Go](<../../读书笔记/GameAIPro/卷3-GameAIPro3/24-being-where-it-counts-telling-paragon-bo.md>)
- [第25章 Combat Outcome Prediction for Real-Time Strategy Games](<../../读书笔记/GameAIPro/卷3-GameAIPro3/25-combat-outcome-prediction-for-real-time-.md>)
- [第26章 Guide to Effective Auto-Generated Spatial Queries](<../../读书笔记/GameAIPro/卷3-GameAIPro3/26-guide-to-effective-auto-generated-spatia.md>)
- [第27章 The Role of Time in Spatio-Temporal Reasoning: Three Examples from Tower Defense](<../../读书笔记/GameAIPro/卷3-GameAIPro3/27-the-role-of-time-in-spatio-temporal-reas.md>)
- [第28章 Pitfalls and Solutions When Using Monte-Carlo Tree Search for Strategy and Tactical Games](<../../读书笔记/GameAIPro/卷3-GameAIPro3/28-pitfalls-and-solutions-when-using-monte-.md>)
- [第29章 Petri Nets and AI Arbitration](<../../读书笔记/GameAIPro/卷3-GameAIPro3/29-petri-nets-and-ai-arbitration.md>)
- [第30章 Hierarchical Portfolio Search in Prismata](<../../读书笔记/GameAIPro/卷3-GameAIPro3/30-hierarchical-portfolio-search-in-prismat.md>)
- [第31章 Behavior Decision System: Dragon Age Inquisition’s Utility Scoring Architecture](<../../读书笔记/GameAIPro/卷3-GameAIPro3/31-behavior-decision-system-dragon-age-inqu.md>)
- [第32章 Paragon Bots: A Bag of Tricks](<../../读书笔记/GameAIPro/卷3-GameAIPro3/32-paragon-bots-a-bag-of-tricks.md>)
- [第33章 Using Your Combat AI Accuracy to Balance Difficulty](<../../读书笔记/GameAIPro/卷3-GameAIPro3/33-using-your-combat-ai-accuracy-to-balance.md>)
- [第34章 1000 NPCs at 60 FPS](<../../读书笔记/GameAIPro/卷3-GameAIPro3/34-1000-npcs-at-60-fps.md>)
- [第35章 Ambient Interactions: Improving Believability by Leveraging Rule-Based AI](<../../读书笔记/GameAIPro/卷3-GameAIPro3/35-ambient-interactions-improving-believabi.md>)
- [第37章 Simulating Character Knowledge Phenomena in Talk of the Town](<../../读书笔记/GameAIPro/卷3-GameAIPro3/37-simulating-character-knowledge-phenomena.md>)
- [第39章 Recommendation Systems in Games](<../../读书笔记/GameAIPro/卷3-GameAIPro3/39-recommendation-systems-in-games.md>)
- [第3章 Gearing the Tactics Genre: Simultaneous AI Actions in Gears Tactics](<../../读书笔记/GameAIPro/卷4-OnlineEdition2021/03-gearing-the-tactics-genre-simultaneous-a.md>)
- [第4章 Knowledge is Power, an Overview of AI Knowledge Representation in Games](<../../读书笔记/GameAIPro/卷4-OnlineEdition2021/04-knowledge-is-power-an-overview-of-ai-kno.md>)
- [第5章 Taming Spatial Queries – Tips for Natural Position Selection](<../../读书笔记/GameAIPro/卷4-OnlineEdition2021/05-taming-spatial-queries-tips-for-natural.md>)
- [第6章 Flooding the Influence Map for Chase in Dishonored 2](<../../读书笔记/GameAIPro/卷4-OnlineEdition2021/06-flooding-the-influence-map-for-chase-in-.md>)
- [第7章 Managing Pacing in Procedural Levels in Warframe](<../../读书笔记/GameAIPro/卷4-OnlineEdition2021/07-managing-pacing-in-procedural-levels-in-.md>)
- [第8章 Cinematic Gameplay in Watchdogs 2: Pose Matching and AI Coordination](<../../读书笔记/GameAIPro/卷4-OnlineEdition2021/08-cinematic-gameplay-in-watchdogs-2-pose-m.md>)
- [第10章 AI-Driven Autoplay Agents for Prelaunch Game Tuning](<../../读书笔记/GameAIPro/卷4-OnlineEdition2021/10-ai-driven-autoplay-agents-for-prelaunch-.md>)
- [第11章 You had me at 'AAAAHHH' - On the importance of reactions in game AI](<../../读书笔记/GameAIPro/卷4-OnlineEdition2021/11-you-had-me-at-aaaahhh-on-the-importance-.md>)
- [第12章 Squad Coordination in Days Gone](<../../读书笔记/GameAIPro/卷4-OnlineEdition2021/12-squad-coordination-in-days-gone.md>)
- [第13章 Template Tricks for Data-Driven Behavior Trees](<../../读书笔记/GameAIPro/卷4-OnlineEdition2021/13-template-tricks-for-data-driven-behavior.md>)
- [第16章 Open-world Enemy AI in Mafia III](<../../读书笔记/GameAIPro/卷4-OnlineEdition2021/16-open-world-enemy-ai-in-mafia-iii.md>)
- [第17章 Game Balancing using Genetic Algorithms to Generate Player Agents](<../../读书笔记/GameAIPro/卷4-OnlineEdition2021/17-game-balancing-using-genetic-algorithms-.md>)

## 工作日志关联

- [工作日志：2026-08-03](<../../工作日志/2026-08-03-假人AI-A星优化与耗时测试.md>)

## 主题路线

- [游戏AI：学习路线](<../../00_Index/学习路线/游戏AI.md>)

## 边界与扩展

一个主题按主要问题确定主责，其他领域引用它。运行在服务端的玩法规则仍属于 Gameplay；UE 源码分析按具体机制归类。新概念先找已有主文，再决定扩充或新建。

[八域总览](<../README.md>) · [主责与关系](<../../00_Index/跨域关系.md>)
