---
type: Reference
title: "行为模式（Behavioral Patterns）"
description: "为游戏对象注入丰富生命力与复杂逻辑，同时避免陷入庞大臃肿的继承体系与脚本维护泥潭。"
tags:
  - game-programming-patterns
  - design-patterns
  - cpp
status: stable
verified: []
maturity: L2
updated: 2026-09-14
---

# 行为模式（Behavioral Patterns）

> **章节**：行为模式（Behavioral Patterns）  
> **原著**：Robert Nystrom (Bob Nystrom) · 《Game Programming Patterns》  
> **导航**：[目录](README.md) ｜ [上一章：更新方法](update-method.md) ｜ [下一章：字节码](bytecode.md)

---

一旦搭好游戏布景，用演员和道具把它装点起来，剩下的就是开场。
为此，你需要行为——告诉游戏中每个实体该做什么的剧本。

当然，所有代码都是“行为”，所有软件也都是在定义行为，
但游戏的不同之处在于，你需要实现的行为通常*范围*很广。
文字处理器也许有很长的功能清单，
但与普通角色扮演游戏中的居民、物品和任务数量相比，还是相形见绌。

本章的模式有助于快速定义和完善大量可维护的行为。
[类型对象](type-object.md)创建行为的类别，而无需拘泥于定义真正的类。
[子类沙箱](subclass-sandbox.md)给你一套安全的原语，用来定义各种行为。
最先进的选项是[字节码](bytecode.md)，它把行为完全移出代码，放入数据。

## 模式

* [字节码](bytecode.md)
* [子类沙箱](subclass-sandbox.md)
* [类型对象](type-object.md)

---

> **导航**：[目录](README.md) ｜ [上一章：更新方法](update-method.md) ｜ [下一章：字节码](bytecode.md)
