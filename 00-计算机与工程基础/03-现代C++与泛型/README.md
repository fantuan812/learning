---
type: Index
title: "03-现代C++与泛型 · 分类"
status: stable
verified: []
maturity: L2
updated: 2026-08-20
---

# 03-现代C++与泛型 · 分类

> 定位：覆盖现代 C++（C++17/C++20/C++23）泛型编程、Concepts 约束契约、Ranges 管道视图、类型系统增强与标准库实现；回答“如何编写零运行时开销、类型安全且编译错误友好的高性能代码”。

---

## 1. 专题矩阵与当前状态

| 专题文件与 Canonical 路径 | 标题与核心范畴 | 知识类型 | 成熟度 | 核心工程问题与回答 |
| :--- | :--- | :---: | :---: | :--- |
| [01-Concepts、Ranges与泛型设计](01-Concepts、Ranges与泛型设计.md) | C++20 Concepts 约束语法、替代复杂 SFINAE、编译期契约验证、Ranges 惰性视图流水线、性能与可维护性 | Concept | L2 | 解决传统模板实例化错误信息动辄上百行、无法直观阅读的问题，实现零分配的流式数据转换流水线。 |
| [02-异常、类型系统与标准库实现](02-异常、类型系统与标准库实现.md) | 异常机制物理实现（Itanium ABI zero-cost exception vs SEH）、无异常模式（-fno-exceptions）、std::optional/std::expected、variant 与类型安全 | Concept | L2 | 回答游戏客户端与服务器为何普遍禁用 C++ 异常，并给出确定性错误处理（Error Code / Expected）的工程最佳实践。 |

---

## 2. 逻辑学习顺序与依赖关系

1. **第一步：告别晦涩的 SFINAE，拥抱 Concepts 编译期约束**
   - 研读 [01-Concepts、Ranges与泛型设计](01-Concepts、Ranges与泛型设计.md)；掌握 `requires` 子句、类型约束定义与重载决议优先级，理解 Ranges 视图的惰性求值与零拷贝特性。
2. **第二步：剖析异常模型与现代化无异常类型系统**
   - 研读 [02-异常、类型系统与标准库实现](02-异常、类型系统与标准库实现.md)；掌握异常表的寻表（Table-driven）开销，理解现代工程如何用 `std::expected` / `std::optional` 与值语义构建清晰的错误通道。
3. **前置与后置关联**：
   - 前置依赖：[01-C++核心](../01-C++核心/README.md)（值语义与所有权）；
   - 后置应用：[10-编译链接与ABI](../10-编译链接与ABI/README.md)（模板多重实例化膨胀与 ODR 规范）。

---

## 3. 游戏研发与工程落地对接

- **Unreal Engine (UE5)**：
  - UE 引擎构建环境全面禁用标准 C++ 异常（`-fno-exceptions`），深度使用 `check`、`ensure` 与错误返回码保证执行确定性；
  - UE 容器与序列化模板元编程：使用 `TEnableIf`（逐步过渡到现代模板约束）进行蓝图与序列化类型判定；
  - 基于概念的组件接口检查：在编译期验证 Gameplay 实体是否具备指定能力组件。
- **游戏服务端**：
  - 网络协议与 RPC 解析中，采用 `std::expected<Response, ErrorCode>` 代替捕获异常，确保协议编解码路径严格无锁、无分配、零分支跳转惩罚；
  - 任务管道中使用 Ranges 思想对在线玩家列表进行惰性过滤与筛选（如 AOI 视野裁剪），避免生成中间临时容器。

---

## 4. 跨域与相关导航

- [00-计算机与工程基础 总索引](../README.md)
- [计算机与工程基础 Domain MOC](../../00_Index/domains/计算机与工程基础.md)
- [01-C++核心](../01-C++核心/README.md)
- [10-编译链接与ABI](../10-编译链接与ABI/README.md)
