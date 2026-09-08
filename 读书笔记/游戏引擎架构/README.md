---
type: Index
title: "《游戏引擎架构（第4版）》中文翻译与精读专栏"
description: "Jason Gregory 经典巨著《Game Engine Architecture》第4版全2卷18章中文翻译、精读与工程实践对照。"
tags:
  - game-engine
  - architecture
  - jason-gregory
  - translation
  - reading-notes
status: stable
verified: []
maturity: L1
updated: 2026-09-07
---

# 《游戏引擎架构（第4版）》中文翻译与精读专栏

> 原著：*Game Engine Architecture, 4th Edition* (2024/2025)
> 原作者：**Jason Gregory**（Naughty Dog 资深首席引擎程序员，曾参与《神秘海域》系列与《最后生还者》开发）
> 权威英文原版归档于：`读书笔记/`（全书分为两卷，共 1344 页）

---

## 专栏定位与翻译原则

本书被誉为游戏引擎工程领域的“圣经”。第四版相较前版全面重构并扩充为两卷，深入涵盖了以现代硬件架构（多核高并发、SIMD/AVX、GPGPU、现代图形 API 如 DX12/Vulkan、次时代技术如 Nanite/Lumen、现代动画与物理中间件）为基础的引擎全景。

本专栏遵循以下原则进行**顺序翻译与工程精读**：

1. **按顺序逐步推进**：严格遵循全书 18 章逻辑顺序逐章翻译（卷1 第1章至第10章 $\rightarrow$ 卷2 第11章至第18章）。
2. **术语严谨规范**：统一建立中英专业术语对照表，保留关键英文缩写（如 RHI、ACP、SIMD、ECS、CAS 等），消除歧义。
3. **架构图与流程图还原**：将原书文字与架构层级用 Markdown、Mermaid 及清晰的 ASCII 架构图完整复原。
4. **现代工业界实践对照**：结合当代 AAA 商业引擎（Unreal Engine 5.8、Unity、Frostbite、自研引擎）的源码实现与架构决策进行注解与延伸。

---

## 分卷导航与章节总览

### [卷1：基础与核心引擎系统（Volume I: Foundations and Core Engine Systems）](卷1-基础与核心引擎系统/README.md)

卷1聚焦于游戏引擎的底层系统软件工程、并发编程、3D数学基础、内存与支持系统、资源管线、游戏循环以及调试工具。

| 章节 | 英文原名 | 中文译名 | 状态 | 正文入口 |
| :--- | :--- | :--- | :---: | :--- |
| **第1章** | Introduction | 导论：游戏、引擎、类型差异与系统全貌 | **已完成** | [01-导论.md](卷1-基础与核心引擎系统/01-导论.md) |
| **第2章** | Tools of the Trade | 开发者专业工具链（版本控制、编译链接、硬件断点调试、分析器） | **已完成** | [02-专业工具.md](卷1-基础与核心引擎系统/02-专业工具.md) |
| **第3章** | Fundamentals of Software Engineering for Games | 游戏软件工程基础（现代C++取舍、内存布局、对齐优化、虚表模型、多级缓存） | **已完成** | [03-游戏软件工程基础.md](卷1-基础与核心引擎系统/03-游戏软件工程基础.md) |
| **第4章** | Parallelism and Concurrent Programming | 并行与并发编程（任务/数据并行、纤程协程、无锁CAS、C++内存模型、SIMD与GPU） | **已完成** | [04-并行与并发编程.md](卷1-基础与核心引擎系统/04-并行与并发编程.md) |
| **第5章** | 3D Math for Games | 游戏中的3D数学（坐标系手性、齐次变换矩阵、法向量逆转置、四元数与SLERP、RNG） | **已完成** | [05-游戏中的3D数学.md](卷1-基础与核心引擎系统/05-游戏中的3D数学.md) |
| **第6章** | Engine Support Systems | 引擎支持系统（子系统生命周期、栈/池/双缓冲内存分配器、平坦哈希、字符串池化与Cvars） | **已完成** | [06-引擎支持系统.md](卷1-基础与核心引擎系统/06-引擎支持系统.md) |
| **第7章** | Resources and the File System | 资源管理器与虚拟文件系统（异步I/O、内存就绪In-Place、指针缝合与资源注册表） | **已完成** | [07-资源与文件系统.md](卷1-基础与核心引擎系统/07-资源与文件系统.md) |
| **第8章** | The Game Loop and Real-Time Simulation | 游戏循环与实时模拟（非阻塞消息泵、时间线抽象、步长夹断、垂直同步、纤程Job系统） | **已完成** | [08-游戏循环与实时模拟.md](卷1-基础与核心引擎系统/08-游戏循环与实时模拟.md) |
| **第9章** | Human Interface Devices | 人机交互设备（自适应扳机、径向死区滤波、和弦按键缓冲、搓招状态机、动作映射） | **已完成** | [09-人机交互设备.md](卷1-基础与核心引擎系统/09-人机交互设备.md) |
| **第10章** | Tools for Debugging and Development | 调试与开发工具（多通道日志、3D调试绘制、Dear ImGui、内嵌控制台、层级性能剖析） | **已完成** | [10-调试与开发工具.md](卷1-基础与核心引擎系统/10-调试与开发工具.md) |

---

### [卷2：图形、动作与声音（Volume II: Graphics, Motion, and Sound）](卷2-图形动作与声音/README.md)

卷2聚焦于图形渲染管线、现代光照与后处理、骨骼与程序化动画系统、碰撞与物理模拟、三维空间音频以及运行时玩法框架。

| 章节 | 英文原名 | 中文译名 | 状态 | 正文入口 |
| :--- | :--- | :--- | :---: | :--- |
| **第11章** | Rendering | 渲染引擎原理（现代显式API、Mesh Shader、空间剔除、UE5 Nanite几何虚拟化） | **已完成** | [11-渲染.md](卷2-图形动作与声音/11-渲染.md) |
| **第12章** | Lighting and Post-Processing | 光照与后处理（辐射度学、PBR微表面理论、延迟/分簇光照、硬件光追、ACES色调映射） | **已完成** | [12-光照与后处理.md](卷2-图形动作与声音/12-光照与后处理.md) |
| **第13章** | Animation Systems | 动画系统（骨骼拓扑、蒙皮矩阵调色板、加性混合、曲线压缩、运动匹配） | **已完成** | [13-动画系统.md](卷2-图形动作与声音/13-动画系统.md) |
| **第14章** | Collision and Rigid Body Dynamics | 碰撞检测与刚体动力学（图元选型、SAT/GJK/EPA、CCD、辛欧拉/Verlet积分、PGS约束求解、CMC与布娃娃） | **已完成** | [14-碰撞与刚体动力学.md](卷2-图形动作与声音/14-碰撞与刚体动力学.md) |
| **第15章** | Audio | 音频引擎（声学物理、DSP冲激响应卷积、PCM量化、恒定功率Pan、双耳HRTF、声音传送门、混音总线） | **已完成** | [15-音频.md](卷2-图形动作与声音/15-音频.md) |
| **第16章** | Introduction to Gameplay Systems | 玩法系统导论（世界解构、动态实体模型、数据驱动哲学、世界编辑器架构） | **已完成** | [16-玩法系统导论.md](卷2-图形动作与声音/16-玩法系统导论.md) |
| **第17章** | Runtime Gameplay Systems | 运行时玩法系统（组件化/ECS、世界流式加载、句柄安全、分相更新调度、无锁并发、事件总线与脚本引擎） | **已完成** | [17-运行时玩法系统.md](卷2-图形动作与声音/17-运行时玩法系统.md) |
| **第18章** | You Mean There’s More? | 还有更多？（FMV视频管线、多人联机网络预测回滚、高级玩法手感、游戏AI行为树/GOAP、全书结语） | **已完成** | [18-还有更多.md](卷2-图形动作与声音/18-还有更多.md) |

---

## 核心技术术语对照表（Glossary）

为保证全书翻译一致性，专栏统一采用以下工业界标准术语对照：

| 英文术语 | 统一中文译名 | 简要说明 / 典型场景 |
| :--- | :--- | :--- |
| **Soft Real-Time Simulation** | 软实时模拟 | 偶尔错过帧期限仅导致掉帧，不会导致灾难性崩溃 |
| **Target Hardware** | 目标硬件 | PC、主机（PS5/Xbox）、移动端等宿主硬件平台 |
| **Platform Independence Layer** | 平台独立层 / 平台抽象层 | 对 OS 与硬件底层 API 的封装与隔离（HAL） |
| **Asset Conditioning Pipeline (ACP)** | 资产调节管线 | DCC 资产至引擎高效运行时二进制格式的离线编译管线 |
| **Scene Graph** | 场景图 | 组织空间层次与变换的树状/图状空间数据结构 |
| **Frustum Culling** | 视锥剔除 | 剔除摄像机视野视锥体外的物体的几何测试 |
| **Occlusion Culling** | 遮挡剔除 | 剔除被前景物体阻挡的后景物体的可见性测试 |
| **Binary Space Partitioning (BSP)** | 二叉空间分割 | 用于室内场景可见性与碰撞判定的经典空间分割技术 |
| **Digital Content Creation (DCC)** | 数字内容创作 | Maya、3ds Max、Blender、Houdini 等建模动画工具 |
| **Human Interface Device (HID)** | 人机接口设备 | 手柄、键鼠、触屏、体感手柄等输入输出硬件 |
| **Hitbox / Hurtbox** | 攻击判定框 / 受击判定框 | 格斗与动作游戏中用于判定打击与受创的几何体 |
| **Rollback Netcode** | 回滚网络代码 | 格斗与即时竞技中基于确定性状态回溯与预测的网络同步机制 |
| **Tick / Game Loop** | 心跳 / 游戏主循环 | 驱动时间推进与各子系统状态更新的中心循环 |
| **Rendering Hardware Interface (RHI)** | 渲染硬件接口 | 抹平 DirectX、Vulkan、Metal 等图形 API 差异的抽象层 |
