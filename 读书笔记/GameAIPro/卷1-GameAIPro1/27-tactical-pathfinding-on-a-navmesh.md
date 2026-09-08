---
type: Reference
title: "第27章 Tactical Pathfinding on a NavMesh"
description: "Game AI Pro 工业级精读：Tactical Pathfinding on a NavMesh。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
tags:
  - game-ai
  - game-ai-pro
  - decision-making
  - navmesh
  - architecture
  - state-machines
status: stable
verified: []
maturity: L2
updated: 2026-09-07
---

# 第27章 Tactical Pathfinding on a NavMesh

> 来源：*Game AI Pro 1: Collected Wisdom of Game AI Professionals*, Chapter 27.  
> 原文作者 / 资源：[Tactical Pathfinding on a NavMesh](http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter27_Tactical_Pathfinding_on_a_NavMesh.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷1-GameAIPro1](README.md) ｜ [专栏首页](../README.md)

---

在现代战术射击（Tactical Shooter）与即时战略（RTS）游戏中，传统的寻路系统仅聚焦于计算从起点 $A$ 到终点 $B$ 的**几何最短路径（Shortest Route）**已无法满足玩家对高拟真度 AI 的诉求。AI 智能体（Agents）在交火区域中直挺挺地穿越空旷地带，会严重破坏战术沉浸感。智能体必须寻找**战术上最合理（Tactically Sound）且最适宜的路线（Most Appropriate Route）**——一条能够最大化规避敌方视线（Concealment）、利用掩体屏蔽敌方火力线、并降低穿越暴露区域风险的路径。

长期以来，工业界的主流战术寻路方案深度绑定规则路标网格（Regular Waypoint Grids），但此类方案存在严重的内存占用与计算负载瓶颈。本文基于工业界成熟经验，提出了一套直接在**导航网格（NavMesh）**上进行几何细分、线段级掩体建模（Cover Representation）与 A* 启发式代价修偏（Cost Biasing）的战术寻路技术架构。

---

## 1. 行业方案演进与对比分析 (Comparative Architecture Analysis)

为了在寻路中纳入“敌方能否在位置 A 观测到位于位置 B 的我方”这一核心视线判定（Line-of-Sight, LOS），工业界先后演化出多种空间推理（Spatial Reasoning）与战术寻路管线。

```
[传统战术管线 (Regular Waypoint Grids)]
  ┌──────────────────┐      ┌─────────────────────────┐      ┌──────────────────┐
  │ 密集采样路标点网格  │ ───> │ 实时射线检测 / 查表缓存   │ ───> │ 高负载 A* 搜索   │
  │ (High Memory)    │      │ (Raycasts / O(n²) Table)│      │ (Slow Traversal) │
  └──────────────────┘      └─────────────────────────┘      └──────────────────┘

[本章架构 (Tactical Pathfinding on NavMesh)]
  ┌──────────────────┐      ┌─────────────────────────┐      ┌──────────────────┐
  │ 凸多边形 NavMesh │ ───> │ 掩体映射表 (CoverMap)   │ ───> │ 视锥线段裁剪修偏  │
  │ 战术图元细分/标记 │      │ 关联显著掩体线段 (O(1))  │      │ 连续暴露比例计算  │
  └──────────────────┘      └─────────────────────────┘      └──────────────────┘
```

### 1.1 主流战术寻路方案对比

| 技术方案 | 空间表示 (Spatial Rep.) | 视线判定方式 (LOS Method) | 动态破坏支持 (Destructible Cover) | 内存复杂度 (Memory) | 运行时 CPU 开销 (Runtime CPU) | 主要工业缺陷 (Trade-offs) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **全量离线可见性查表**<br>*(Lidén 02)* | 规则路标网格 (Waypoint Grid) | 离线预烘焙 $N \times N$ 可见性矩阵（PVS Lookup Table） | 极差，破坏后需重新烘焙或打补丁 | $O(n^2)$ 指数级膨胀 | $O(1)$ 快速查表 | 随地图规模增大，内存呈平方爆炸；无法应对大地图。 |
| **实时物理射线检测**<br>*(Brute-force Raycast)* | 任意表示 (Grid / NavMesh) | 实时物理光线投射 (`Physics.Raycast`) | 极佳，完全基于实时碰撞体 | $O(1)$ 无额外存储 | 极高，A* 展开时每条边均发射射线导致卡顿 | 性能极度昂贵；对细微几何缝隙过敏，产生“穿帮”错误。 |
| **径向距离场 / 深度立方体图**<br>*(Straatman 05, van der Leeuw 09)* | 空间离散探针 / 节点 | 2D 径向距离场 (Radial Distance Field) / 3D 深度立方体图 (Depth Cube-Maps) | 中等，需重绘局部探针深度 | 探针数 $\times$ 分辨率开销中等 | 中等，向量方向比对即可测算遮挡 | 探针分辨率与内存的权衡；存在采样走样（Aliasing）。 |
| **覆盖光栅化网格**<br>*(Jurney 07)* | 战术 2D 网格 | 掩体投影光栅化，叠加敌方视锥权重图 (Rasterized View Cone) | 优异，直接从网格注销并重新光栅化局部掩体 | 线性依赖网格分辨率 | 视锥光栅化开销与网格遍历开销 | 需要独立于主寻路网格的战术网格，存在系统间同步开销。 |
| **NavMesh 运行时重采样网格**<br>*(Bamford 12)* | NavMesh + 战术点图 | 在交火区域采样 NavMesh 动态生成局部密集路标图 | 良好，局部更新 | 中等（局部网格占用） | 需要在交火时维护双层寻路系统 | 寻路数据冗余，维护两套寻路底层逻辑，维护成本高。 |
| **NavMesh 视锥几何裁剪 (本章架构)** | 细分凸多边形 NavMesh | 掩体线段 (Cover Segment) 构建视锥体裁剪路径线段 | 优异，动态增删 CoverMap 引用即可 | $O(M)$ 仅存储多边形关联掩体索引表 | 极低，仅针对候选边执行解析几何裁剪运算 | 需关卡编辑辅助标记或离线预细分战术多边形。 |

### 1.2 射线检测的“穿帮与遮蔽伪影”抗性分析
在由复杂几何体组成的 3D 环境中，完全依赖精准的几何射线检测（Raycast）极易出现鲁棒性漏洞：
1. **细微裂隙漏检（False Exposure）：** 掩体模型拼缝间的微小间隙可能导致射线穿过，判定为完全暴露，而实际上该位置能提供极佳掩护；
2. **边缘微扰失效（False Occlusion）：** 射线起点或敌方位置的亚像素级偏移可能使检测结果在“完全遮挡”与“完全暴露”之间剧烈抖动；
3. **低分辨率几何逼近的优越性：** 使用结构化的掩体线段（Cover Segments）配合视锥体剪裁，将视线遮挡转化为宏观几何投影，有效过滤了微观几何噪声，不仅执行速度提升数个数量级，更显著增强了行为树（Behavior Trees）与战术决策层的系统稳定性。

---

## 2. 战术 NavMesh 的细分与语义标注 (Tessellation & Annotation)

### 2.1 传统“最优 NavMesh”在战术层面的局限性
传统寻路算法追求 NavMesh 的**凸多边形数量最小化（Near-Optimal NavMesh）**，以最大化多边形跨度并压缩 A* 搜索图规模（Tozour 02, Farnstrom 06）。但在战术寻路中，这种极简化表示会抹杀空间战术差异。

例如一个带有外凸回廊/阳台的庭院，最优几何算法会将其合并为一个巨大的单体凸多边形：

```
(a) 传统最优 NavMesh (单个大凸多边形)        (b) 战术细分 NavMesh (语义标记)
┌───────────────────────────────────┐    ┌───────────────────────────────────┐
│                                   │    │\      掩体阴影区 (Covered Edges)   /│
│                                   │    │ ┌───────────────────────────────┐ │
│                                   │    │ │                               │ │
│        单一 NavMesh 多边形         │    │ │    暴露危险区 ("Unsafe")      │ │
│     无法区分边缘与暴露中心区域     │    │ │        (High Cost)            │ │
│                                   │    │ │                               │ │
│                                   │    │ └───────────────────────────────┘ │
│                                   │    │/                                 \│
└───────────────────────────────────┘    └───────────────────────────────────┘
```

在此结构下，A* 搜索在多边形内部只执行直线连接，AI 会直接穿过庭院中心开阔地带，无法感知到“沿回廊掩体移动”的战术优势。

### 2.2 几何细分与战术元数据标注
解决此问题的核心在于引入**受限几何细分（Constrained Tessellation）**。关卡设计师或离线分析工具在多边形内部增加战术多边形轮廓约束（Contour Boundaries），使网格生成算法固定这些边界，将其切分为不同战术属性的细分凸多边形，并注入战术权重标签：

*   **水域（Water Polygons）：** 标注 `Slow Movement`（增加移动消耗）或 `Only Passable by Amphibious Units`（通行能力过滤）。
*   **高草丛（High Grass Polygons）：** 标注 `Provides Concealment`（降低受威胁代价）。
*   **茂密灌木（Dense Undergrowth）：** 同时叠加 `Slow Movement` 与 `Provides Concealment`。
*   **空旷危险区（Unsafe / Vulnerable Areas）：** 将天井中央标注为 `Unsafe`，提升通过代价，促使智能体沿边缘行进。

---

## 3. 掩体线段建模与 CoverMap 空间映射 (Cover Representation & CoverMap)

### 3.1 离散掩体点（Cover Points） vs. 连续掩体线段（Cover Segments）
路标网格方案通常将掩体表述为离散的路标点，连续的掩体则用点之间的拓扑连线表达。本架构将其抽象为由几何线段定义的**掩体线段（Cover Segment）**：
*   **几何定义：** 二维平面线段 $S = \overline{AB}$，附带世界空间绝对高度或相对高度属性 $h$；
*   **战术类型：** 
    *   全高掩体（Tall / Standing Cover, $h \ge 1.8\text{m}$）：完全遮断站立射击视线；
    *   半高掩体（Waist-High / Crouching Cover, $h \approx 0.8\text{m} \sim 1.0\text{m}$）：允许蹲伏隐蔽，站立暴露。

### 3.2 掩体映射表（CoverMap）架构设计
为避免在全图范围遍历数以千计的掩体线段，系统为 NavMesh 中的每个凸多边形维护一个索引表——**掩体映射表（CoverMap）**。该表建立局部多边形到有效掩体线段的紧凑拓扑关联。

```
┌─────────────────────────────────────────────────────────────┐
│                       NavMesh 场景                           │
│  ┌───────────────────────┐       ┌───────────────────────┐  │
│  │     Cover [C-D-A-B]   │       │    Cover [G-H-E-F]    │  │
│  └───────────────────────┘       └───────────────────────┘  │
│              \                               /              │
│               \                             /               │
│                ▼                           ▼                │
│             ┌─────────────────────────────────┐             │
│             │          NavMesh Poly 1         │             │
│             └─────────────────────────────────┘             │
│                              │                              │
│                              ▼                              │
│             ┌─────────────────────────────────┐             │
│             │          CoverMap 索引          │             │
│             │ Poly 1 -> {A, B, E, H, I, J, M} │             │
│             │ Poly 2 -> {E, J, K, L, O, P}    │             │
│             └─────────────────────────────────┘             │
└─────────────────────────────────────────────────────────────┘
```

### 3.3 显著掩体线段选择启发式规则 (Cover Selection Heuristic)
每个多边形仅存储与其最相关的少量掩体线段（通常限制在 8~16 条以保证缓存局部性）。离线生成或动态关联时，依据以下启发式公式评估掩体线段 $S_i$ 相对于多边形 $P_k$ 的重要度评分：

$$Score(S_i, P_k) = w_d \cdot \frac{1}{\operatorname{dist}(S_i, P_k) + \epsilon} + w_\theta \cdot \operatorname{Diversity}(\vec{n}_{S_i}, \mathcal{S}_{selected}) - w_o \cdot \operatorname{Occlusion}(S_i, \mathcal{S}_{selected})$$

*   **距离因子（Proximity）：** 掩体到多边形中心的欧几里得距离 $\operatorname{dist}(S_i, P_k)$，近距离掩体优先保留；
*   **方向覆盖多样性（Directional Coverage Diversity）：** 掩体法线 $\vec{n}_{S_i}$ 应尽量均匀覆盖各个方向，避免同一朝向保留多条共线掩体；
*   **相互遮挡惩罚（Mutual Occlusion）：** 若掩体 $S_i$ 已经被距离多边形更近的掩体完全遮蔽，则其重要度评分大幅下降，从列表中剔除。

---

## 4. 视锥几何裁剪与 A* 战术代价计算 (Frustum Clipping & Cost Modification)

战术 A* 寻路的核心，是将传统以几何距离为主的边代价计算，转变为结合暴露比例、掩体修正因子与设计师标记的加权战术时代价计算。

### 4.1 2D 阴影视锥体几何构建 (2D Frustum Construction)
在求值从多边形入口点到出口点的候选路径线段 $P_1P_2$ 时，算法从**敌方威胁点 $C$** 所在多边形索引其 `CoverMap`，提取与敌方相关的掩体线段 $AB$。掩体线段在敌方视线后方投射出一片“背风区”（Leeward Area，即不可见阴影区）。

```
                        敌方威胁点 C
                            ●
                           / \
                          /   \
                         /     \
                        /       \
      掩体线段端点 A   ┌─────────┐   端点 B
             ─────────●─────────●─────────
                     /│  掩体体  │\
                    / └─────────┘ \
  平面 planeAC 法线 /                \ 平面 planeCB 法线
             ┌───> /                  \ <───┐
            │     /                    \     │
            │    /      背风阴影区      \    │
                /   (Concealed Area)     \
   路径起点 P1 ●────────────[===========]─● 路径终点 P2
                       平面 planeAB (法线向上)
                             [=====] = 裁剪出的隐蔽路径片段
```

系统依据掩体线段两端点 $A, B$ 及威胁源 $C$，构建定义该阴影区的 3 个半空间平面（2D 中为带朝向的直线）：
1.  **掩体阻挡基面 $\Pi_{AB}$：** 由点 $A, B$ 构成，法线指向掩体背向敌方的一侧；
2.  **左侧视线边界面 $\Pi_{AC}$：** 由点 $A, C$ 构成，法线向内指向背风区；
3.  **右侧视线边界面 $\Pi_{CB}$：** 由点 $C, B$ 构成，法线向内指向背风区。

若以世界坐标上方向向量 $\vec{u}$（Up-Axis）构建 3D 平面方程，则平面的单位法向量 $\vec{N}$ 统一遵循右手法则指向阴影内侧：

$$\vec{N}_{AB} = \frac{(B - A) \times \vec{u}}{\|(B - A) \times \vec{u}\|}$$

$$\vec{N}_{AC} = \frac{(A - C) \times \vec{u}}{\|(A - C) \times \vec{u}\|}$$

$$\vec{N}_{CB} = \frac{(C - B) \times \vec{u}}{\|(C - B) \times \vec{u}\|}$$

对于任意平面 $\Pi: \vec{N} \cdot \vec{X} + D = 0$，点 $\vec{X}$ 在平面内侧的判别式为：

$$\vec{N} \cdot \vec{X} + D \ge 0$$

### 4.2 路径线段裁剪算法 (Segment Clipping Algorithm)
将候选路径线段 $P_1P_2$ 依次输入上述 3 个半空间，执行线段裁剪算法（Sutherland-Hodgman / Cyrus-Beck 线段裁剪）。保留在全部 3 个平面内侧的线段，即为被当前掩体遮蔽的**隐蔽线段（Concealed Segment）**。

### 4.3 暴露长度求值算法与代价值计算 (Cost Formulation)

以下为工业级战术代价评估伪代码实现，完整还原原书逻辑：

```cpp
// 基础图元定义
struct Point { float x, y, z; };
struct Line  { Point p1, p2; };
struct Plane { Point normal; float d; };

// 沿平面法线方向裁剪线段，保留半空间内侧片段
Line ClipLineByPlane(Line inputLine, Plane plane)
{
    float distP1 = DotProduct(plane.normal, inputLine.p1) + plane.d;
    float distP2 = DotProduct(plane.normal, inputLine.p2) + plane.d;

    // 两端点均在平面正面（内侧）
    if (distP1 >= 0.0f && distP2 >= 0.0f) {
        return inputLine;
    }
    // 两端点均在平面背面（外侧），被完全裁剪
    if (distP1 < 0.0f && distP2 < 0.0f) {
        return Line{ {0,0,0}, {0,0,0} }; // 返回零长度无效线段
    }

    // 计算交点参数 t (t in [0, 1])
    float t = distP1 / (distP1 - distP2);
    Point intersection = Lerp(inputLine.p1, inputLine.p2, t);

    if (distP1 >= 0.0f) {
        // P1 在内侧，P2 在外侧，保留 [P1, intersection]
        return Line{ inputLine.p1, intersection };
    } else {
        // P2 在内侧，P1 在外侧，保留 [intersection, P2]
        return Line{ intersection, inputLine.p2 };
    }
}

// 三点法计算平面方程（法线指向右手法则确定的半空间）
Plane CalcPlaneFrom3Points(Point p1, Point p2, Point p3)
{
    Point v1 = Sub(p2, p1);
    Point v2 = Sub(p3, p1);
    Point normal = Normalize(CrossProduct(v1, v2));
    float d = -DotProduct(normal, p1);
    return Plane{ normal, d };
}

// 计算指定路径线段在敌方视角下的总暴露长度
float GetSegmentExposedLength(Line pathSegmentP1P2, Point enemyPosition, 
                              const Array<Line>& relevantCoverSegments)
{
    float concealedLength = 0.0f;
    Point upAxis = Point{ 0.0f, 1.0f, 0.0f }; // 假设 Y 轴朝上

    for (const Line& coverSegmentAB : relevantCoverSegments)
    {
        // 1. 构建视锥体的 3 个边界平面
        Plane planeAB = CalcPlaneFrom3Points(coverSegmentAB.p1, coverSegmentAB.p2, 
                                             Add(coverSegmentAB.p1, upAxis));
        Plane planeAC = CalcPlaneFrom3Points(coverSegmentAB.p1, enemyPosition, 
                                             Add(coverSegmentAB.p1, upAxis));
        Plane planeCB = CalcPlaneFrom3Points(enemyPosition, coverSegmentAB.p2, 
                                             Add(enemyPosition, upAxis));

        // 2. 依次用 3 个半空间裁剪路径线段
        Line clippedLine = ClipLineByPlane(pathSegmentP1P2, planeAB);
        clippedLine      = ClipLineByPlane(clippedLine, planeAC);
        clippedLine      = ClipLineByPlane(clippedLine, planeCB);

        // 3. 累加被遮蔽的线段长度（若存在多掩体叠加，需做区间并集消除重叠，此处为基础近似）
        concealedLength += Length(clippedLine);
    }

    float totalLength = Length(pathSegmentP1P2);
    // 限制隐蔽总长度不得超过原始线段长度
    concealedLength = Min(concealedLength, totalLength);
    
    float exposedLength = totalLength - concealedLength;
    return exposedLength;
}

// 战术 A* 边代价计算核心入口
float CostForSegment(Line pathSegmentP1P2, PolygonFlags polyFlags, 
                     Point enemyPosition, const Array<Line>& relevantCover,
                     float agentCoverBias, float agentSafetyBias)
{
    float segmentLength = Length(pathSegmentP1P2);
    float exposedLength = GetSegmentExposedLength(pathSegmentP1P2, enemyPosition, relevantCover);

    // 基础几何与战术暴露代价加权
    // 当完全暴露时，代价为 segmentLength * agentCoverBias
    // 当完全隐蔽时，代价回归几何长度 segmentLength
    float cost = (exposedLength * agentCoverBias) + (segmentLength - exposedLength);

    // 叠加关卡设计师战术标记惩罚 (例如 Unsafe / Vulnerable 标记)
    cost += segmentLength * (polyFlags.isUnsafe ? 1.0f : 0.0f) * agentSafetyBias;

    return cost;
}
```

### 4.4 启发式可采纳性（Admissibility）与搜索效率权衡 (Trade-offs)
在 A* 搜索中，代价值 $g(n)$ 与启发函数 $h(n)$ 的关系直接决定算法的正确性与性能：
1.  **下界可采纳性保证（$h(n) \le h^*(n)$）：**
    *   偏置因子必须满足 $\text{agentCoverBias} \ge 1.0$ 且 $\text{agentSafetyBias} \ge 0$；
    *   **禁忌设计：** 严禁使偏置因子 $< 1.0$ 来降低隐蔽节点的移动代价。如果掩体内部的边代价低于欧几里得直线几何距离，启发式函数 $h(n)$ 将不再满足可采纳性（Admissibility），A* 算法无法保证收敛到最优路径。
2.  **偏置系数的收敛代价：**
    *   若 $\text{agentCoverBias}$ 设置过大（如 $> 10.0$），暴露节点代价值将急剧飙升，导致 $g(n)$ 远超基于欧几里得距离的 $h(n)$。这会引发类似 Dijkstra 算法的泛洪式节点展开，导致 A* 搜索的闭合表（Closed List）急剧膨胀，带来严峻的 CPU 尖峰。实际生产中，建议值控制在 $[1.5, 3.0]$ 区间内，由行为树动态微调。

---

## 5. 3D 复杂空间战术拓展 (Extending the Technique to 3D)

上述 2D 视锥算法适用于平坦或轻微起伏的地形。然而在多层建筑、高地悬崖、立体交叉桥梁等真 3D 战术场景中，2D 假设会产生逻辑缺陷：
1.  **俯角越顶射击（Elevated LOS）：** 处于高处的敌人能够越过低矮掩体直接俯射目标；
2.  **纵向楼板阻隔（Vertical Structures）：** 处于下层的敌人无法穿透天花板或楼板视察上层目标，若仅计算 2D 掩体线段，将产生“假暴露”误判。

```
              高处敌方视线 C
                  ●
                 / \
                /   \________ 顶平面 Top Plane
               /     \       \
   掩体矩形   ┌───────┐       \
  (高度 H)   │       │\       \
       A ────●───────● ──── B  \
             │ 掩体体 │  \      ▼
             │       │   \    [========] 3D 隐蔽线段
             └───────┘    ▼   P1        P2
                        底平面 Bottom Plane
```

### 5.1 矩形掩体面（Cover Polygon）与 3D 视锥体构建
针对 3D 地形扩展，掩体线段由二维线段升级为世界空间垂直矩形面片 $Polygon_{AB}$：
*   **几何要素：** 掩体线段两底端点 $A, B$、掩体世界高度 $H$、掩体顶边两端点 $A_{top} = A + H\vec{u}, B_{top} = B + H\vec{u}$；
*   **3D 视锥体围成（Frustum Polyhedron）：** 从敌方威胁点 $C$ 出发，向矩形面片的 4 条边分别延伸，构成由 5 个半空间平面组成的 3D 阴影棱台：
    1.  **掩体正面阻挡面：** 经过 $A, B, A_{top}$；
    2.  **左侧面：** 经过 $C, A, A_{top}$；
    3.  **右侧面：** 经过 $C, B_{top}, B$；
    4.  **顶侧边界切面（Top Clipping Plane）：** 经过 $C, A_{top}, B_{top}$（限定敌人无法俯视遮蔽的仰角阴影）；
    5.  **底侧边界切面（Bottom Clipping Plane）：** 经过 $C, B, A$。

### 5.2 3D 线段解析裁剪与楼板遮蔽平面
路径线段 $P_1P_2$ 顺次通过这 5 个 3D 半空间平面的裁剪。若场景中存在多层结构，将建筑物楼板（Floors）与天花板（Ceilings）同样作为水平阻挡矩形平面（Horizontal Cover Planes）注入敌方的 `CoverMap` 中，即可用完全一致的视锥裁剪管线判定垂直层级遮蔽。

---

## 6. 架构总结与核心参考文献

### 6.1 方案优缺点与工业落地方案权衡

#### 优势（Pros）
*   **零寻路网格冗余：** 战术寻路与运动导航无缝共用底层单套 NavMesh，杜绝了系统间数据一致性维护难题；
*   **极致内存效率：** 摆脱了传统方案中随节点数平方爆炸的 $O(n^2)$ 离线可见性表，仅为多边形存储极少量的掩体索引（$O(M)$）；
*   **连续暴露量测算：** 摆脱离散采样点，直接对路径线段计算出连续的绝对暴露几何长度，生成的规避路径平滑自然；
*   **对几何缝隙鲁棒：** 解析几何视锥裁剪天然免疫射线检测中由于缝隙与微小坐标漂移引起的判定抖动。

#### 局限与妥协（Cons & Trade-offs）
*   **多掩体叠加重叠（Overlapping Shadows）：** 当多个掩体共同投射阴影时，简单的线段长度直接累加可能导致隐蔽长度被重复计算，需采用一维区间合并算法（Interval Union）予以消除；
*   **A* 展开开销增加：** 相较于纯欧几里得距离，在每条搜索边上执行 3~5 次平面求交裁剪会增加单步展开的 CPU 指令周期。在工业级引擎中，建议将该逻辑挂载在 SIMD 并行指令集或 Job Worker 线程中异步执行。

### 6.2 参考文献 (References)

*   **[Bamford 12]** N. Bamford. “Situational Awareness: Terrain Reasoning for Tactical Shooter A.I.” *AI Summit, Game Developers Conference (GDC)*, 2012.
*   **[Farnstrom 06]** F. Farnstrom. “Improving on near-optimality: More techniques for building navigation meshes.” *AI Game Programming Wisdom 3*, edited by Steve Rabin. Charles River Media, pp. 113–128, 2006.
*   **[Jurney 07]** C. Jurney and S. Hubrick. “Dealing with Destruction: AI From the Trenches of Company of Heroes.” *Game Developers Conference (GDC)*, 2007.
*   **[Lidén 02]** L. Lidén. “Strategic and tactical reasoning with waypoints.” *AI Game Programming Wisdom*, edited by Steve Rabin. Charles River Media, pp. 211–220, 2002.
*   **[Snook 00]** G. Snook. “Simplified 3D movement and pathfinding using navigation meshes.” *Game Programming Gems*, edited by Mark DeLoura. Charles River Media, pp. 288–304, 2000.
*   **[Straatman 05]** R. Straatman, W. van der Sterren, and A. Beij. “Killzone’s AI: Dynamic Procedural Combat Tactics.” *Game Developers Conference (GDC)*, 2005.
*   **[Tozour 02]** P. Tozour. “Building a near-optimal navigation mesh.” *AI Game Programming Wisdom*, edited by Steve Rabin. Charles River Media, pp. 171–185, 2002.
*   **[Tozour 04]** P. Tozour. “Search space representations.” *AI Game Programming Wisdom 2*, edited by Steve Rabin. Charles River Media, pp. 85–102, 2004.
*   **[van der Leeuw 09]** M. van der Leeuw. “The PlayStation 3’s SPU’s in the Real World—A KILLZONE 2 Case Study.” *Game Developers Conference (GDC)*, 2009.
*   **[van der Sterren 02]** W. van der Sterren. “Tactical path-finding with A*.” *Game Programming Gems 3*, edited by Dante Treglia. Charles River Media, pp. 294–306, 2002.
