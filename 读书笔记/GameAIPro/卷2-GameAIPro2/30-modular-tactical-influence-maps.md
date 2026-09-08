---
type: Reference
title: "第30章 Modular Tactical Influence Maps"
description: "Game AI Pro 工业级精读：Modular Tactical Influence Maps。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
tags:
  - game-ai
  - game-ai-pro
  - tactical-movement
  - combat-ai
  - steering
  - navigation
status: stable
verified: []
maturity: L2
updated: 2026-09-07
---

# 第30章 Modular Tactical Influence Maps

> 来源：*Game AI Pro 2: More Collected Wisdom of Game AI Professionals*, Chapter 30.  
> 原文作者 / 资源：[Modular Tactical Influence Maps](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter30_Modular_Tactical_Influence_Maps.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷2-GameAIPro2](README.md) ｜ [专栏首页](../README.md)

---

在当代射击游戏（Shooters）与角色扮演游戏（RPGs）中，智能体（Agent）表现出的拟真度与沉浸感，很大程度上取决于其与环境交互的空间行为质量。传统的静态路径寻路（Pathfinding）及静态掩体选择算法（Automatic Cover Selection）虽然构建了基础的空间移动框架，但无法让智能体理解动态博弈态势。智能体不仅需要感知静态关卡几何结构（Static Level Geometry），还必须能够感知并推断局部空间中所有友方（Allies）与敌方（Enemies）智能体所形成的动态力场。

模块化战术影响图（Modular Tactical Influence Maps）为智能体的战术定位、态势分析（Situation Analysis）、技能释放引导（Spell Targeting）以及群落涌现行为（Emergent Group Behavior）提供了统一的底层空间推理（Spatial Reasoning）基础设施。

---

## 核心战术应用场景与求解目标

战术影响图并非指令系统（Instruction System），而是一个**空间态势信息黑板系统（Spatial Information Blackboard System）**。该架构针对战术微观与局部小队战斗（Tactical Situations & Small Group Combat）提供高效信息提取，直接驱动下述工业级游戏 AI 战术决策：

```
+-------------------------------------------------------------------------------+
|                       战术影响图驱动的核心 AI 行为矩阵                           |
+------------------------------------+------------------------------------------+
| 战术决策目标                        | 空间推理与影响图查询逻辑                   |
+------------------------------------+------------------------------------------+
| 肉盾阻断站位                        | 将高生存力单位定位在敌方威胁与易伤友方   |
| (Tank-style Positioning)           | （如法师 Spellcasters）连线的负影响通道中 |
+------------------------------------+------------------------------------------+
| 瞬时相对威胁评估                    | 查询智能体当前占据单元格及周围邻域的     |
| (Relative Threat Evaluation)       | 敌方威胁影响标量叠加总值                 |
+------------------------------------+------------------------------------------+
| 紧急规避与撤退搜索                  | 在自身机动半径内搜索敌方威胁极小值且     |
| (Safe Evade / Withdraw Locations)  | 友方邻近度较高的安全单元格               |
+------------------------------------+------------------------------------------+
| 范围伤害技能落点（AOE Targeting）   | 识别敌方邻近度（Proximity）极值点与      |
| 与阻断法术落点（Blocking Spells）  | 敌军高密度簇（Clusters of Enemies）      |
+------------------------------------+------------------------------------------+
| 友军防聚团间距控制                  | 避免己方邻近度超过安全阈值，防止发生     |
| (Anti-Bunching Spacing)            | 扎堆聚团（Bunching Up）                  |
+------------------------------------+------------------------------------------+
| 目标包夹防过饱和控制                | 判定目标周边友军数量是否达到承载上限，   |
| (Anti-Piling-On Thresholding)      | 防止过度围殴单一目标（Piling On）        |
+------------------------------------+------------------------------------------+
```

---

## 战术影响图底层拓扑与数学原理

### 共享空间信息与计算复杂度降维

在多智能体对抗空间中，若每个智能体均独立计算与其余各主体的相对几何向量，其算法时间复杂度为 $\mathcal{O}(n^2)$。通过引入战术影响图作为**统一空间缓存黑板（Shared Spatial Cache Blackboard）**，系统只需单次将所有智能体的影响力光栅化至网格中，各智能体随后均可以 $\mathcal{O}(1)$ 的时间复杂度采样空间数据。

影响图将连续的 3D/2D 空间离散化为规则的正方形网格拓扑（Regular Square Grid）。每个单元格（Cell）记录一个浮点标量值，用于表征特定概念（如战斗力、危险度、占有度、战术价值等）。

```
                +-------------------+-------------------+
                | 传统几何计算模型  | 战术影响图模型    |
+---------------+-------------------+-------------------+
| 复杂度        | O(n^2) 相互测距   | O(n * R^2) 栅格化 |
| 决策维度      | "目标在何处?"     | "何处安全/无人?"  |
| 阵营博弈表达  | 复杂的判定逻辑    | 连续代数空间叠加  |
+---------------+-------------------+-------------------+
```

空间查询的核心视角转变：从正向提问*“目标在哪里”*转向战术否定提问（Negative Spatial Query）：
* “敌人的火力打击范围无法覆盖哪些位置？”
* “未来数秒内敌方不可抵达的安全死角在哪里？”
* “何处既能维持战术支援又不会侵犯友军的个人空间（Personal Space）？”

### 影响力的数学传播与衰减响应曲线（Propagation Response Curves）

影响力自智能体中心位点（Locus）向外周辐射，其强度随着中心距离 $d$ 的增加而衰减，直至达到最大传播半径 $R_{\max}$（即 MaxDistance）。

#### 1. 线性衰减函数（Linear Propagation）

线性传播适用于基础距离感知与简单阻尼模型：

$$I_{\text{linear}}(d) = I_{\max} - \left( I_{\max} \times \frac{d}{R_{\max}} \right) = I_{\max} \left( 1 - \frac{d}{R_{\max}} \right), \quad \text{for } 0 \le d \le R_{\max}$$

* 当 $d = 0$ 时，$I = I_{\max}$；当 $d \ge R_{\max}$ 时，$I = 0$。

#### 2. 反多项式衰减函数（Inverse Polynomial Propagation）

为了模拟具有陡峭边缘或平滑缓冲区的空间威胁，引入高阶多项式响应曲线：

$$I_{\text{poly}}(d) = I_{\max} - I_{\max} \times \left( \frac{d}{R_{\max}} \right)^p = I_{\max} \left[ 1 - \left( \frac{d}{R_{\max}} \right)^p \right], \quad p \in \{2, 4\}$$

* **二次衰减 ($p = 2$)**：在位点中心附近保持较高的平坦影响区，中距离加速衰减；
* **四次衰减 ($p = 4$)**：构建宽阔的高影响力“核心控制区”，仅在边界处迅速跌落至零。

```
影响力标量 (Influence)
1.0 +-----------------------..-----------------------+
    |                     .'  |  `.                   |
0.8 |                   .'    |    `.   <-- p = 4     |
    |                 .'      |      `.               |
0.6 |               .'        |        `.             |
    |              /          |          \  <-- p = 2 |
0.4 |            .'           |           `.          |
    |           /             |             \         |
0.2 |         .'              |              `. <-- 线性 (Linear)
    |       .'                |                `.     |
0.0 +-------+-----+-----+-----+-----+-----+-----+-----+
   -10     -8    -6    -4     0     4     6     8    10
                        网格空间位移 (Units)
```

#### 3. 非中心峰值与非欧氏距离传播修正

* **环状威胁拓扑（Ring-of-Influence Topology）**：远程攻击单位（如投石机 Catapult、迫击炮、远程狙击手）具有最小射程限制与最有效射程。此时最大影响力的产生位点 $d_{\text{peak}} \neq 0$，影响力在 $d \in [R_{\min}, R_{\max}]$ 区间内达到峰值，而在 $d < R_{\min}$（贴身近战盲区）迅速降至极低值；
* **视线阻挡阻断（Line-of-Sight Blockage）**：直射武器威胁必须受几何阻挡剔除，不可见单元格不进行传播；
* **非欧氏路径距离（Non-Euclidean Path-Distance Propagation）**：在复杂障碍物（如栅栏、断崖）环境中，直接使用欧几里得距离 $\sqrt{\Delta x^2 + \Delta y^2}$ 会导致错误的越界威胁判断。此时需采用**无向迪杰斯特拉搜索（Undirected Dijkstra Search）**沿静态导航网格（NavMesh）或离散网格预先计算拓扑路径距离 $d_{\text{path}}$：

$$I(c) = f(d_{\text{path}}(\mathbf{x}_{\text{agent}}, \mathbf{x}_{c}))$$

---

## 多智能体代数叠加与阵营对抗拓扑

### 空间叠加原理（Superposition Principle）

当同阵营多个智能体相互靠近时，其在网格中散布的影响力执行代数累加。对于网格中任意坐标单元格 $c$：

$$I_{\text{total}}(c) = \sum_{k=1}^{N} I_k(c)$$

当两个或多个智能体的影响半径重叠时，单元格影响值将超过单个智能体的上限标量 $I_{\max} = 1.0$（例如累加至 $1.38$ 乃至更高），在空间中形成明显的“高浓度驻留/危险聚集区”。

### 阵营对抗极性地图（Factional Polarization Map）

通过为不同阵营分配相反的代数符号，可以构建出具备正负连续梯度的态势拓扑空间。以二元阵营对立为例：
* **己方阵营（Friendly Force）**：放射正向标量值 $+I(c)$；
* **敌方阵营（Hostile Force）**：放射负向标量值 $-I(c)$。

网格单元代数和公式为：

$$I_{\text{battlefield}}(c) = \sum_{i \in \text{Allies}} I_i(c) - \sum_{j \in \text{Enemies}} I_j(c)$$

```
                                 交火前线零势面 (Battlefront)
                                              |
      [ 己方腹地 (Strongly Ours) ]            |            [ 敌方控制区 (Strongly Theirs) ]
+---------------------------------------------+---------------------------------------------+
| +1.00 | +0.88 | +0.63 | +0.38 | +0.13 | 0.00| -0.13 | -0.38 | -0.63 | -0.88 | -1.00 |
+---------------------------------------------+---------------------------------------------+
                                              |
                                              +-> [ 中立争夺区 (Neutral Zone: I ≈ 0.0) ]
```

该地图为高级战术行为提供了决定性的连续场输入：
* **前线判定（Battlefront Line）**：沿数值为 $0.0$ 的等值线分布，即正负势能平衡处；
* **包抄寻路（Flanking Paths）**：引导突击单位绕过前线零势能正面，沿着敌方侧翼的低绝对值区域移动；
* **战术脱出（Disengagement）**：引导残血单位沿正梯度最大的方向（$\nabla I_{\text{battlefield}} > 0$）退回己方控制区。

---

## 模块化影响图系统架构（System Architecture）

该架构采用分层解耦的模块化拓扑设计，将地图解构为：底层基础地图（Base Maps）、离散预生成模板（Templates）、动态工作地图（Working Maps）。

```
+------------------------------------------------------------------------------------+
|                         模块化战术影响图系统架构总线                                  |
+------------------------------------------------------------------------------------+
       |
       +---> [ 基础地图层 (Base Maps) ] (全局内存分配，固定拓扑网格)
       |        |
       |        +-- 己方邻近图 (Faction A Proximity Map)
       |        +-- 己方威胁图 (Faction A Threat Map)
       |        +-- 敌方邻近图 (Faction B Proximity Map)
       |        +-- 敌方威胁图 (Faction B Threat Map)
       |        +-- 环境/危险源图 (Environmental Hazards Map)
       |
       +---> [ 静态离散模板库 (Static Template Library) ] (引擎初始化预烘焙)
       |        |
       |        +-- 模板 [近战, R=3, 线性衰减]
       |        +-- 模板 [移动范围, R=8, 2阶多项式]
       |        +-- 模板 [投石机/狙击, 环状响应曲线]
       |
       +---> [ 运算执行管线 (Baking & Blitting Pipeline) ]
       |        |
       |        +-- 轴对齐包围盒裁剪 (AABB Radius Clamping)
       |        +-- 内存块快速叠加写入 (Cache-friendly Blit)
       |
       +---> [ 临时工作地图 (Working Maps) ] (按需分配/栈式生命周期)
                |
                +-- 战术算子：Diff(Threat_B, Threat_A) -> 相对危险地图
                +-- 空间推理器直接采样：获取最佳站位、AOE落点
```

### 基础地图数据结构（Base Map Structure）

基础地图通常为覆盖全关卡几何的 2D 规则连续平面数组。其空间分辨率（Granularity）由智能体的“思考粒度”决定。例如：若设计要求智能体在空间站位与落点判定时以 $1.0\,\text{m}$ 为最小步长，则网格单元边长设为 $1.0\,\text{m}$。

```cpp
// 基础战术影响图数据容器
struct BaseMap {
    int32_t width;
    int32_t height;
    float   cellSize;        // 单个 Cell 的世界空间物理尺寸（单位：米）
    Vector3 worldOrigin;     // 网格 (0,0) 对应的世界坐标原点
    std::vector<float> data; // 扁平化的单精度浮点连续内存

    inline size_t GetIndex(int x, int y) const {
        return static_cast<size_t>(y * width + x);
    }

    inline bool WorldToGrid(const Vector3& worldPos, int& outX, int& outY) const {
        outX = static_cast<int>((worldPos.x - worldOrigin.x) / cellSize);
        outY = static_cast<int>((worldPos.z - worldOrigin.z) / cellSize);
        return (outX >= 0 && outX < width && outY >= 0 && outY < height);
    }
};
```

### 基础地图的工业分类

工业管线中，针对每个交战阵营（Faction，至少包括友方与敌方两个阵营）各自分配两类基础地图：

#### 1. 邻近度地图（Proximity Map / "Prox Map"）
* **物理语义**：表征智能体当前实体位置以及未来极短时间窗口（如 2~3 秒）内可能突入的机动空间。
* **数学构建**：以实体当前坐标为中心，以其短时间内最大机动距离为半径绘制衰减圆盘。单元格数值越高，表示该位置被该阵营实体物理占据或控制的概率越大。

#### 2. 威胁地图（Threat Map）
* **物理语义**：表征智能体当前射程及武器投射效能所覆盖的杀伤空间。
* **数学构建**：区别于机动范围，根据武器形态构建辐射场。近战单位为近身实心衰减核；远程投射单位（如箭塔、迫击炮）为远离本体的空心环状威慑带。

---

## 模板化光栅化与计算优化机制

若在运行时对每个智能体均遍历周边单元格计算开方距离：

$$d = \sqrt{(x - x_0)^2 + (y - y_0)^2}$$

会导致每帧产生大量重复的浮点运算，严重影响性能。架构采用**预烘焙模板技术（Precomputed Influence Templates）**规避冗余运算。

### 模板预计算机制（Template Precomputation）

在系统初始化期，针对不同的角色类型、武器射程及响应曲线，离散化生成一系列局部相对坐标核（Kernel Matrices）。模板尺寸严格由 $2R_{\max} + 1$ 的方形包围盒限定。

```cpp
// 预计算离散影响图模板
struct InfluenceTemplate {
    int32_t radius;          // 模板影响半径（以网格单元为单位）
    int32_t dimension;       // 模板边长: 2 * radius + 1
    std::vector<float> kernel; // 尺寸为 dimension * dimension 的预烘焙权重数组

    // 预计算构造函数
    InfluenceTemplate(int32_t r, float maxValue, float (*decayFunc)(float dist, float maxDist))
        : radius(r), dimension(2 * r + 1), kernel(dimension * dimension, 0.0f) 
    {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (dist <= static_cast<float>(radius)) {
                    int idx = (dy + radius) * dimension + (dx + radius);
                    kernel[idx] = maxValue * decayFunc(dist, static_cast<float>(radius));
                }
            }
        }
    }
};
```

### 运行时图块光栅化操作（Runtime Blitting Pipeline）

在每帧更新周期中，智能体向地图注记影响力的过程简化为**模板块向大网格的数据传送与叠加（Memory Blit & Additive Stamp）**。计算被严格限制在由影响半径裁剪出的轴对齐边界矩形（AABB）内部，避免遍历全图单元格。

```cpp
// 将模板高效印刻叠加至目标基础地图中
void StampTemplate(BaseMap& destMap, const InfluenceTemplate& tmpl, int centerGridX, int centerGridY, float scalarMultiplier) {
    // 计算世界地图上的相交矩形区域（AABB 裁剪）
    int minX = std::max(0, centerGridX - tmpl.radius);
    int maxX = std::min(destMap.width - 1, centerGridX + tmpl.radius);
    int minY = std::max(0, centerGridY - tmpl.radius);
    int maxY = std::min(destMap.height - 1, centerGridY + tmpl.radius);

    for (int y = minY; y <= maxY; ++y) {
        int tmplY = y - (centerGridY - tmpl.radius);
        int destRowOffset = y * destMap.width;
        int tmplRowOffset = tmplY * tmpl.dimension;

        for (int x = minX; x <= maxX; ++x) {
            int tmplX = x - (centerGridX - tmpl.radius);
            float stampVal = tmpl.kernel[tmplRowOffset + tmplX] * scalarMultiplier;
            
            // 执行代数加算叠加
            destMap.data[destRowOffset + x] += stampVal;
        }
    }
}
```

---

## 核心设计模式与工程权衡

```
+---------------------------------------------------------------------------------------+
|                               战术影响图架构权衡矩阵                                     |
+---------------------+-------------------------------+---------------------------------+
| 设计维度            | 选型决策                      | 架构权衡评估与代价分析           |
+---------------------+-------------------------------+---------------------------------+
| 连续性 vs 离散化    | 规则正方形网格 (Regular Grid) | 放弃全精度浮点坐标，获得缓存局部性与线性内存连续读取 |
+---------------------+-------------------------------+---------------------------------+
| 欧氏度量 vs 寻路拓扑| 混合分流模式                  | 视线与空旷区域采用预计算欧氏模板；强障碍阻断采用     |
|                     |                               | 无向 Dijkstra 路径距离衰减，防止隔墙虚假威慑   |
+---------------------+-------------------------------+---------------------------------+
| 运算时机            | 统一单次光栅化 + 多代理采样   | 将 O(N^2) 的主体交互转变为空间图写入与采样，大幅降低 CPU 开销 |
+---------------------+-------------------------------+---------------------------------+
| 阵营数据隔离        | 分离式基础图 + 按需合成工作图 | 友方/敌方的邻近与威胁图物理解耦，方便利用 SIMD 执行加减运算 |
+---------------------+-------------------------------+---------------------------------+
```

通过这一套分层的战术影响图机制，AI 系统成功将离散的几何环境转变为包含丰富敌我博弈意图与动态态势的标量场。上层的行为树（Behavior Trees）、效用系统（Utility Systems）以及有限状态机（FSM）无需处理复杂的几何测距逻辑，只需以较低的计算开销向战术影响图发起空间采样请求，即可驱动智能体做出符合战术情境的高级战斗决策。

---

在现代 AAA 级游戏 AI 架构中，空间推理（Spatial Reasoning）与态势感知（Situational Awareness）是驱动非玩家角色（NPC）实现高级战术移动、集火选标及威胁规避的核心基石。传统的单层全局影响图（Influence Maps）面临着频繁开方计算瓶颈、维度单一以及缺乏运行时灵活装配能力等问题。本文基于工业界成熟的“模块化战术影响图”（Modular Tactical Influence Maps）架构，系统性重构其算法机理、离线预计算模板机制、运行时拓扑装配流水线及工程落地范式。

---

## 1. 影响图计算瓶颈与模板印戳机制（Template Stamping）

在离散网格空间中，实时向周围单元格（Cells）扩散影响值通常需要评估几何欧几里得距离：

$$d = \sqrt{(x - x_0)^2 + (y - y_0)^2}$$

若场景中存在数百个动态 Agent，且每一帧或每一逻辑 Tick 均需对周围数十甚至数百个单元格执行距离与非线性响应衰减计算，密集的平方根与浮点幂运算将迅速使 CPU 算力饱和。

### 1.1 预计算模板（Precalculated Templates）
为彻底消除运行时的重复距离评估与响应曲线求值，模块化系统引入了**模板（Templates）**设计模式。模板为游戏引擎启动或关卡加载时预先计算并持久化常驻内存的离散二维核矩阵（Kernel Grid）。

```
+-----------------------------------------------------------+
|               离线预计算 (Offline / Startup)               |
|                                                           |
|  数学衰减公式 (Equation 30.1 / 30.3)                         |
|                 │                                         |
|                 ▼                                         |
|  各规格归一化核矩阵模板 (Normalized Kernels, [0.0, 1.0])       |
|  - 位置/邻近模板 (Proximity/Location Templates)             |
|  - 威胁模板 (Threat Templates)                              |
|  - 个人兴趣模板 (Personal Interest Templates)                |
+-----------------------------------------------------------+
                             │
                             ▼ 运行时装配 (Runtime Stamp)
+-----------------------------------------------------------+
| Base Map (动态基图) ◄─── O(1) 内存偏移加权累加 ─── Template |
+-----------------------------------------------------------+
```

运行时，影响扩散引擎由原本繁重的“数学计算模式”转变为内存块遍历的“印戳模式”（Stamping Pattern）：
1. 依据 Agent 属性选取预计算好的核矩阵模板；
2. 依据空间位置将模板中心与基础图（Base Map）上的目标单元格对齐；
3. 将模板单元格内的归一化权重乘以 Agent 强度系数，直接累加（Add）到基础图的对应单元格中。

> **物理拓扑局限性与阻挡规避**：
> “印戳法”基于直线距离（Line-of-Sight Euclidean Distance）扩散，不穿透物理阻挡。当空间中存在复杂的静态/动态几何障碍物导致通道阻断（如高墙、死胡同）时，基于欧几里得距离的印戳会导致影响值“穿墙泄漏”。针对此类强物理遮蔽区域，尤其是邻近度感知图，系统必须降级回退至基于导航网格（NavMesh）的测地距离（Path Distance / Geodesic Distance）拓扑扩散计算。

---

## 2. 模板数学建模与分类学（Mathematical Formulations & Taxonomy）

所有离线模板在空间上均定义为对称正方形网格 $N \times N$，其几何中心索引为 $(\lfloor N/2 \rfloor, \lfloor N/2 \rfloor)$。模板内部的影响值均严格归一化至 $[0.0, 1.0]$ 区间，以实现底层几何衰减特征与上层 Agent 强度（Strength Magnitude）的解耦。

系统将模板划分为三大拓扑类别：

| 模板类型 (Template Type) | 数学衰减形态 (Attenuation Profile) | 典型应用场景 (Application) | 驱动索引参数 (Lookup Metric) |
| :--- | :--- | :--- | :--- |
| **位置/邻近模板 (Location / Proximity Template)** | 线性梯级衰减 (Linear Gradient) | 评估短时物理可达性、单位拥挤度 | 最大移动速度与更新周期乘积 ($v_{\max} \cdot \Delta t$) |
| **威胁模板 (Threat Template)** | 4阶高次多项式平台衰减 (Polynomial Degrade) | 评估武器/法术射程杀伤压制区 | 武器最大杀伤/施法射程 ($R_{\mathrm{threat}}$) |
| **个人兴趣模板 (Personal Interest Template)** | 自定义聚焦衰减 (Focus Decay) | 运行时视野局部掩膜加权过滤 | Agent 感兴趣的战术评估半径 ($R_{\mathrm{interest}}$) |

### 2.1 位置/邻近度模板（Location/Proximity Templates）
用于建模 Agent 在特定时间切片内“能够迅速抵达的空间范围”。其物理扩散距离受限于 Agent 在地图更新周期 $\Delta t$ 内所能跨越的最大位移：

$$R_{\max} = v_{\max} \cdot \Delta t$$

若离散网格单元分辨率为 $1\ \mathrm{m}$（每个单元格对应 $1\ \mathrm{m}^2$ 物理空间），AI 系统的刷新周期为 $\Delta t = 1.0\ \mathrm{s}$，Agent 最大位移速度为 $v_{\max} = 10\ \mathrm{m/s}$，则单向扩散半径为 $10$ 格。包含中心点在内，最大核矩阵尺寸为：

$$\text{Size} = (2 \cdot R_{\max} + 1) \times (2 \cdot R_{\max} + 1) = 21 \times 21$$

其衰减曲线遵循线性递减模型（Linear Gradient, Equation 30.1）：

$$I_{\mathrm{location}}(d) = 1.0 - \frac{d}{R_{\max}} \quad (\forall d \le R_{\max})$$

对于支持多种移动速率的游戏（如 RPG 或 RTS），系统按整型离散速度阶梯（如 $1\ \mathrm{m/s} \sim 10\ \mathrm{m/s}$）预先生成并持久化 10 组尺度各异的基底核模板。

### 2.2 威胁模板（Threat Templates）
战斗实体（远程射手、施法者）投射的威胁在有效射程内大多维持高危态，仅在逼近射程极限边缘处急剧衰减。威胁模板采用 4 阶高次多项式曲线建模（Equation 30.3）：

$$I_{\mathrm{threat}}(d) = I_{\max} - I_{\max} \cdot \left( \frac{d}{R_{\mathrm{threat\_max}}} \right)^4$$

在模板生成阶段，$I_{\max} = 1.0$，该公式退化为：

$$I_{\mathrm{threat\_norm}}(d) = 1.0 - \left( \frac{d}{R_{\mathrm{threat\_max}}} \right)^4 \quad (\forall d \le R_{\mathrm{threat\_max}})$$

```
Influence (I)
1.0 ┼───────────╮ Threat (Equation 30.3)
    │           │
    │            \
0.5 │             \  Location / Proximity (Equation 30.1)
    │              \
    │               \
0.0 ┼────────────────┴───────────► Distance (d)
    0               R_max
```

* 远程施法者（$R_{\mathrm{threat}} = 30\ \mathrm{m}$）：采用 $61 \times 61$ 规模的离散模板。
* 近战单位（$R_{\mathrm{threat}} = 2\ \mathrm{m}$）：采用 $5 \times 5$ 规模的紧缩模板。
* **离散量化阶梯（Slush Handling）**：在武器射程连续多变的情况下，按固定空间区间（如每 $5\ \mathrm{m}$ 步长）量化核矩阵。向上取整（Round-up）会夸大敌方射程感知，使 AI 表现得更为谨慎或过早规避；向下取整（Round-down）则会压缩威胁感知区，导致 AI 表现出轻敌冒进的战术特征。

### 2.3 个人兴趣模板（Personal Interest Templates）
个人兴趣模板不直接印戳到持久的基础图上，而是在决策阶段作为空间加权掩膜（Spatial Mask）。其核尺寸通常匹配 Agent 当前可执行行动的综合范围（例如：$R_{\mathrm{interest}} = R_{\mathrm{threat}} + v \cdot \Delta t$），用于强行将战术注意力聚焦在自身周边，压制超视距或距离过远的空间噪音。

---

## 3. 工作图（Working Maps）设计模式与内存拓扑

```
+-------------------------------------------------------------------+
| Base Map (全局基础图, 涵盖整个关卡，例如 1024 x 1024)                 |
|                                                                   |
|                   ┌──────────────────┐                            |
|                   │ Working Map      │                            |
|                   │ (局部工作图)     │                            |
|                   │ (Centered on AI) │                            |
|                   │                  │                            |
|                   │        ★ Agent   │                            |
|                   └──────────────────┘                            |
+-------------------------------------------------------------------+
```

### 3.1 局部计算与足迹优化
基础图覆盖整个战场关卡，若在 Agent 做出局部决策时遍历整张大图执行图层混合，其时间复杂度为 $O(W_{\mathrm{world}} \times H_{\mathrm{world}})$，伴随严重的 Cache Miss。

系统引入**工作图（Working Maps）**机制：
1. **尺寸界定**：工作图仅保留与当前决策强相关的局部空间，其尺寸严格等于当前决策中涉及的最大模板的几何维度。
2. **中心对齐**：工作图以发起决策查询的 Agent 物理坐标为几何中心。
3. **数据抽取**：逆向执行印戳操作，将各基础图在局部包围盒（AABB）内的单元格截取并拷贝/计算至工作图，计算复杂度压缩至 $O(W_{\mathrm{local}} \times H_{\mathrm{local}})$。

### 3.2 零分配复用池（Zero-Allocation Memory Pool）
在单线程决策系统或基于时间切片（Time-Slicing）轮询的执行管线中，生命周期短暂的工作图如果频繁申请与释放堆内存，将导致内存碎片并触发垃圾回收（GC）或内存管理器加锁。工业级做法为：
* 预先在全局或线程局部存储（TLS）中分配一块足以容纳最大可能模板的常驻工作图连续内存缓冲区（Padded Memory Buffer）；
* 每次评估前重置其有效活动窗口指针与偏移，完全消除运行时的动态内存分配开销。

---

## 4. 基础图填充流水线（Population Pipeline）

基础图记录阵营维度的宏观态势，其更新流水线必须在性能损耗与感知时效之间取得平衡。

```
[开始更新周期 (Tick: 0.5s - 2.0s)]
                 │
                 ▼
[图层清零: Zero-Out Base Maps (Memset 0)]
                 │
                 ▼
[遍历场景所有存活 Agent (Iteration)]
                 │
  ┌──────────────┴──────────────┐
  ▼                             ▼
[阵营识别: Faction Lookup]    [模板匹配: Template Selection]
  │                             │
  └──────────────┬──────────────┘
                 │
                 ▼
[边界裁剪计算: AABB Clamp (Map Edges)]
                 │
                 ▼
[带权印戳累加: Stamp to Base Map]
(BaseCell += TemplateValue * AgentStrength)
```

### 4.1 更新时序与频率约束
* **刷新窗口**：推荐更新频率界定在 $0.5\ \mathrm{s} \le \Delta t \le 2.0\ \mathrm{s}$。
  * $\Delta t < 0.5\ \mathrm{s}$：计算开销激增，且超出大多数战术决策树的重评周期；
  * $\Delta t > 2.0\ \mathrm{s}$：数据过度陈旧（Stale Data），诱发 Agent 决策滞后与“幽灵目标”追踪。
* **数据清零机制**：基础图在每次更新周期开始时必须执行全图重置（Zeroed-Out，例如执行底层内存块重置 `memset`）。由于模板本身已包含空间衰减信息，基础图不需要执行帧间衰减积分，直接体现当前瞬间各阵营的纯态势叠加。

### 4.2 阵营双图拓扑（Dual-Map Topology per Faction）
每个阵营（Faction）分配两张独立的基础图：
1. **阵营物理邻近图（Faction Proximity Map）**：以移动速度为模板索引，记录阵营内单位的物理分布密集度。
2. **阵营威胁投射图（Faction Threat Map）**：以武器射程与攻击力为模板索引，记录阵营的杀伤覆盖压制区。

### 4.3 边界安全裁剪算法（Boundary Clamping）
当 Agent 逼近地图边界时，模板的外接矩形将超出基础图内存范围。印戳遍历算法必须执行严格的 2D 包围盒相交裁剪，防止数组越界引发内存访问越界故障（Access Violation）。

#### 算法实现：安全模板印戳（Safe Template Stamping）
```cpp
struct InfluenceMap {
    int width;
    int height;
    float cellSize; // 单位：米
    std::vector<float> grid;

    inline int GetIndex(int x, int y) const { return y * width + x; }
};

struct TemplateKernel {
    int radius;     // 单元格半径
    int size;       // 边长 = 2 * radius + 1
    std::vector<float> weights; // 归一化衰减权重 [0.0, 1.0]

    inline float GetValue(int tx, int ty) const { return weights[ty * size + tx]; }
};

void StampTemplate(InfluenceMap& baseMap, 
                   const TemplateKernel& kernel, 
                   int centerMapX, 
                   int centerMapY, 
                   float strengthMagnitude) 
{
    // 计算基础图上的有效覆盖包围盒（裁剪到基础图物理边界）
    const int startMapX = std::max(0, centerMapX - kernel.radius);
    const int endMapX   = std::min(baseMap.width - 1, centerMapX + kernel.radius);
    const int startMapY = std::max(0, centerMapY - kernel.radius);
    const int endMapY   = std::min(baseMap.height - 1, centerMapY + kernel.radius);

    for (int mapY = startMapY; mapY <= endMapY; ++mapY) {
        // 映射回模板内部局部坐标
        const int templateY = mapY - (centerMapY - kernel.radius);
        const int baseRowOffset = mapY * baseMap.width;

        for (int mapX = startMapX; mapX <= endMapX; ++mapX) {
            const int templateX = mapX - (centerMapX - kernel.radius);
            
            // 基础图累加：模板权重 * 实体强度系数
            baseMap.grid[baseRowOffset + mapX] += 
                kernel.GetValue(templateX, templateY) * strengthMagnitude;
        }
    }
}
```

---

## 5. 信息检索与图层代数运算（Layer Algebra & Retrieval）

信息提取是影响图架构的最终服务目标。模块化体系通过对空间图层定义统一的线性代数算子，支持在运行时通过声明式配方（Recipes）装配出任意高阶空间认知。

### 5.1 单点与邻域查询模式对比
1. **单点多图聚合（Point Sampling）**：无需分配工作图，直接在传入的世界坐标索引处，对相关阵营图层的重叠单元格求和。例如快速求得当前站立点的敌方威胁综合评价值：
   $$T(\mathbf{x}_{\mathrm{self}}) = \sum_{f \in \mathrm{Enemies}} \mathrm{Map}_{\mathrm{threat}}^{f}(\mathbf{x}_{\mathrm{self}})$$
2. **区域极值提取（Regional Extremum Search）**：需要将基础图拷贝进入工作图，进行邻域扫描，寻找极值点（Highest/Lowest Point）。

### 5.2 图层代数算子语法（Map Operator Grammar）
工作图作为承载图层代数运算的载体，支持链式流水线操作：

```cpp
// 战术意图配方示例：寻找最佳集火/接敌位置
WorkingMap.New(MyLocation);
WorkingMap.AddMap(EnemyLocationMap(MyLocation), 1.0f);     // 累加敌方分布
WorkingMap.AddMap(AllyLocationMap(MyLocation), -0.5f);     // 惩罚友军密集区（防止扎堆）
WorkingMap.MultiplyMap(InterestTemplate(MyLocation), 1.0f); // 乘法压制：削减远距离权重
Location bestTacticSpot = WorkingMap.GetHighestPoint();    // 提取全局最高得分点
```

### 5.3 特殊图元函数数学原理

#### 5.3.1 动态极值归一化算子（Dynamic Normalization）
仅关心局部拓扑趋势（地形坡度），消除不同单位绝对数值缩放的干扰。
设图层中所有单元格的极小值为 $V_{\min}$，极大值为 $V_{\max}$，对每个单元格数值 $V(x, y)$ 进行仿射变换：

$$\operatorname{Normalize}(V(x, y)) = \frac{V(x, y) - V_{\min}}{V_{\max} - V_{\min}}$$

若基图各点均非负且底色基准值 $V_{\min} = 0$，则退化为：

$$\operatorname{Normalize}(V(x, y)) = \frac{V(x, y)}{V_{\max}}$$

*示例*：若局部工作图最大威胁值为 $1.4$，某单元格威胁值为 $0.7$，归一化后该单元格映射为 $0.5$。

#### 5.3.2 逆转算子（Inversion Operator）
将地图的“山峰”反转为“山谷”，使得安全点成为局部极大值点。
反转运算通常定义在归一化图层上，对每个单元格执行互补反相：

$$\operatorname{Inverse}(V(x, y)) = 1.0 - V(x, y)$$

* **阵营对抗合成优势**：若希望寻找“没有友军驻守但敌人扎堆”的区域，传统做法是将敌军图与友军图直接相减（$\mathrm{Map}_{\mathrm{Enemy}} - \mathrm{Map}_{\mathrm{Ally}}$），可能产生大量负值，破坏后续乘法算子与搜索算子的稳定性。
  引入逆转算子后，通过正向加权累加（$\mathrm{Map}_{\mathrm{Enemy}} + \operatorname{Inverse}(\mathrm{Map}_{\mathrm{Ally}})$），将“避开友军”重构为一个正向激励分量，完美保留 `GetHighestPoint()` 等极值搜索算子的代数单调性。

---

## 6. 战术实战决策范式（Tactical Applied Paradigms）

模块化影响图系统支持三大类战术行为驱动模式：

```
                              ┌── 1. 态势信息感知 (Information Querying)
                              │    - 当前位置危险度评估 (撤退判定)
                              │    - 局部友军拥挤度查询 (避让调整)
                              │
战术影响图决策输出体系 ───────┼── 2. 战术目标选定 (Target Selection)
                              │    - AoE 技能质心锁定 (Center of Mass)
                              │    - 避开友军的群体歼灭打击
                              │
                              └── 3. 移动目的地解算 (Movement Destination)
                                   - 动态威胁规避路径点
                                   - 最优放风筝 (Kiting) 战术位置搜索
```

### 6.1 空间信息感知（Information Querying）
* **站立点威胁度评估**：AI 决策系统（如行为树条件节点或效用系统曲线输入）在逻辑周期内采样当前坐标的威胁图值。若 $T(\mathbf{x}) > \text{PanicThreshold}$，立即向决策树抛出打断信号，触发后撤或掩体搜寻任务。
* **群聚度状态识别**：检索周围一定范围内敌军的重叠密度最大值。多个重叠模板形成的极值代表实体在物理空间上的聚集程度。

### 6.2 质心集火与 AoE 打击选标（Targeting & Center of Mass）
在群体战斗中，投射范围伤害（AoE）技能的最佳位置往往不是具体的单体实体坐标，而是群体敌人的**物理质心（Center of Mass）**。

```
网格局部密度叠加矩阵示意 (多敌人重叠区域形成极值峰顶):
0.24  0.37  0.48  0.57  0.65  0.70  0.68  0.60
0.38  0.60  0.72  0.82  0.96  1.03  0.89  0.71
0.55  0.88  1.12  1.18 [1.22] 1.12  0.94  0.75  <-- [1.22] High Point (最佳 AoE 投射质心)
0.47  0.75  1.01  1.14  1.20  1.08  0.88  0.65
0.33  0.55  0.72  0.80  0.91  0.82  0.64  0.48
```

#### 质心解算流水线
1. 提取敌方邻近度图（Enemy Proximity Map）的局部窗口至工作图；
2. 由于敌对实体彼此相邻，其线性递减模板在交叠区域发生算术相加，于群落几何中心堆叠出数值最高的局部峰值点（High Point）；
3. 扫描该工作图获取峰值点网格坐标，反算回世界坐标 $(\text{WorldX}, \text{WorldY})$，直接作为技能释放的目标矢量。此方法自然支持加权聚类，无需执行复杂的几何聚类算法（如 K-Means 或 DBSCAN）。

### 6.3 个人兴趣模板加权过滤（Locality Prioritization via Interest Filtering）
纯粹的全局或局部极值往往存在战术缺陷：在全图范围内，远端可能存在由数十个敌军形成的高密度峰值点（极高值），而当前 Agent 身边仅有一支两人小队。若直接根据最大值寻路，会导致 Agent 盲目忽视身边的迫在眉睫之敌，穿越战场长途奔袭远端目标。

#### 乘法压制机制
将提取后的邻近度图与 Agent 自身的“个人兴趣模板”执行**逐元素相乘（Hadamard Product）**：

$$\mathrm{Map}_{\mathrm{Working}}(x, y) = \mathrm{Map}_{\mathrm{Raw}}(x, y) \odot \mathrm{Template}_{\mathrm{Interest}}(x, y)$$

```
  原始敌军分布图 (a)              个人兴趣模板 (b)                综合评估结果图 (c)
┌──────────────────────┐      ┌──────────────────────┐      ┌──────────────────────┐
│                      │      │      ...0.2...       │      │                      │
│                      │      │    .0.5     0.5.     │      │                      │
│  ★ 局部低聚集        │      │   .     1.0     .    │      │  ★ [最优优先决策点]  │
│    (值 = 1.0)        │  ×   │  0.2   (Agent)  0.2  │  =   │    (1.0 * 1.0 = 1.0) │
│                      │      │   .     1.0     .    │      │                      │
│            ▲ 远端聚集│      │    .0.5     0.5.     │      │            △ 远端被压制
│              (值=1.5)│      │      ...0.2...       │      │         (1.5 * 0.0 = 0.0)
└──────────────────────┘      └──────────────────────┘      └──────────────────────┘
```

* **抑制效应**：兴趣模板中心权重为 $1.0$，随半径增大衰减至边缘的 $0.0$。所有超出兴趣半径的远端区域均被零乘衰减强制抹平（归零）。
* **战术表现**：即便远端的威胁聚集程度客观上更高，经自身兴趣掩膜加权后，近处的潜在目标得分显著压倒远端，促使 Agent 聚焦于当前局部战区，契合生物学感知规律。

---

## 7. 模块化空间推理系统架构设计

为确保上述系统在多线程与高动态环境下的稳定运行，以下给出模块化战术影响图系统的核心类拓扑结构与执行接口：

```
+-------------------------------------------------------------------------+
|                       InfluenceMapSystem (全局子系统)                    |
+-------------------------------------------------------------------------+
| - m_FactionMaps : Map<FactionID, Pair<InfluenceMap, InfluenceMap>>     |
| - m_LocationTemplates : Vector<TemplateKernel>                         |
| - m_ThreatTemplates   : Vector<TemplateKernel>                         |
+-------------------------------------------------------------------------+
| + Update(float deltaTime) : void                                        |
| + GetWorkingMapForAgent(AgentID agent) : WorkingMap                     |
+-------------------------------------------------------------------------+
                                     │
                                     ▼ 分配
+-------------------------------------------------------------------------+
|                           WorkingMap (计算图层)                          |
+-------------------------------------------------------------------------+
| - m_Buffer : MemoryBuffer2D                                             |
| - m_OriginWorldPos : Vector3                                            |
+-------------------------------------------------------------------------+
| + AddMap(const InfluenceMap& baseMap, float weight) : WorkingMap&       |
| + MultiplyMap(const TemplateKernel& kernel, float weight) : WorkingMap& |
| + Normalize() : WorkingMap&                                             |
| + Inverse() : WorkingMap&                                               |
| + GetHighestPoint() : Vector3                                           |
| + GetLowestPoint() : Vector3                                            |
+-------------------------------------------------------------------------+
```

该架构将“全局持久数据（基础图）”、“离线静态数据（模板）”与“实体局部上下文（工作图）”严格解耦，构建了一条低耦合、无动态内存抖动、算子可组装的高性能战术空间推理流水线，为 AAA 级战斗 AI 提供了底层的态势感知支撑。

---

---

## 1. 核心数学原理与个人兴趣模板代数运算

在工业级战术决策系统（Tactical Decision-Making System）与空间推理（Spatial Reasoning）中，原始影响图（Influence Map）通常表征整个战场全局层面的威胁度、实体密度或领地控制权。然而，单个自主智能体（Autonomous Agent）受限于感知范围、机动能力（Mobility）以及当前战术角色，若直接基于全局极值点执行决策，将导致跨越不可达距离或非理性战术转移。

为了消除全局极值与局部个体可达性之间的脱节，模块化战术影响图架构引入了**个人兴趣模板（Personal Interest Template, $T_{\text{interest}}$）**。通过对局部工作图（Working Map）与个人兴趣模板进行逐格哈达玛积（Hadamard Product，即逐元素乘法），智能体能够在保持战术全局感知的同时，依据自身物理约束对候选位置进行加权收敛。

```
全局影响分布 (Complex Map)        个人兴趣模板 (Interest Template)          最终加权裁决图 (Result Map)
        [峰值 A]                                  [智能体]                                   [新决策峰值]
           /\                                       ||                                           /\
          /  \      [次峰 B]                    /--------\                                      /  \
         /    \       /\                       /     |    \                                    /    \
        /      \_____/  \                     /      |     \                               ___/      \___
---+---+---+---+---+---+---+---  $\bigodot$  ---+----+----+----+----+---  $\Longrightarrow$  ---+---+---+---+---+---+---
       (全局绝对高点)                               (可达半径衰减)                               (兼顾战术价值与距离代价)
```

### 1.1 兴趣模板乘法衰减机制

设空间离散网格为 $\Omega \subset \mathbb{Z}^2$，智能体当前坐标为 $\mathbf{x}_{\text{agent}} \in \Omega$。全局空间属性影响图记为 $M(\mathbf{x})$，其中 $\mathbf{x} \in \Omega$。个人兴趣模板定义为以智能体当前位置为中心的衰减权重核：

$$T_{\text{interest}}(\mathbf{x}; \mathbf{x}_{\text{agent}}, R_{\max}) = f\left(\frac{\|\mathbf{x} - \mathbf{x}_{\text{agent}}\|}{R_{\max}}\right)$$

其中：
- $R_{\max} = g(v_{\text{agent}}, \Delta t)$ 为智能体在规划时间窗口 $\Delta t$ 内由最大线速度 $v_{\text{agent}}$ 决定的最大有效机动半径（Coverage Radius）。
- 径向衰减核函数 $f(u): [0, 1] \to [0, 1]$ 满足单调非递增且有界：$f(0) = 1$，$f(1) = 0$；当 $u > 1$ 时，$f(u) = 0$。

复合运算生成的工作图 $W(\mathbf{x})$ 满足：

$$W(\mathbf{x}) = M(\mathbf{x}) \cdot T_{\text{interest}}(\mathbf{x}; \mathbf{x}_{\text{agent}}, R_{\max})$$

### 1.2 极值点漂移与有效决策空间投影

如图 30.8 所示，原始全局图 $M(\mathbf{x})$ 在网格坐标 $\mathbf{x} = 15$ 处存在全局实际最高点（Actual High Point, 评分为 $2.6$），但在智能体驻留位置（$\mathbf{x}_{\text{agent}} = 30$）附近存在次高点（评分约为 $2.1$）。

1. **截断与抑制**：超出兴趣模板有效支撑集（Support）的区域被直接归零：
   $$\forall \mathbf{x} \notin \text{supp}(T_{\text{interest}}), \quad W(\mathbf{x}) = 0$$
2. **极值重塑与偏移**：由于全局最高点位于模板低增益边缘（$T(15) \approx 0.7$），其调制后得分为：
   $$W(15) = 2.6 \times 0.7 = 1.82$$
   而次高点处于高机动收益区（$T(36) \approx 0.98$），调制后得分为：
   $$W(36) = 2.5 \times 0.98 = 2.45$$
   因此，全局决策最优点由远端的 $\mathbf{x} = 15$ 偏移至就近可达的 $\mathbf{x} = 36$（New High Point），防止了 AI 产生“舍近求远”的战术震荡（Thrashing）。

---

## 2. 威胁轴线推导与空间阻断几何模型

在范围阻断法术（Blocking Spells，如火墙术 Wall of Fire）判定或战线阵型编组（Formations Positioning）中，AI 需要对敌方集群质心（Center of Mass）与我方受保护目标之间的战术矢量进行几何投影。

```
              [敌方阵营集群 (Enemy Cluster)]
                 ●       ●   (Enemy Location)
                     ★ <--- 敌方密度极值点 / 空间质心 (High Point of Enemy Proximity Map)
                 ●       ●
                  \
                   \
                    \  <--- 威胁轴线 (Threat Axis: L)
                     \
         =============#=============  <--- 阻断法术 / 防火墙最佳落点 (Blocking Spell Location, t=0.5)
                       \
                        \
                         \
                          ★ <--- 友军质心或决策智能体当前位置 (Agent / Ally Center of Mass)
```

### 2.1 质心检测与高价值区域提取

设敌方邻近度影响图（Enemy Proximity Map）为 $M_{\text{enemy\_prox}}(\mathbf{x})$。敌方单位的物理集中区（Bulk of Enemies）对应于图层空间密度的局部极值点 $\mathbf{x}_{\text{enemy\_center}}$：

$$\mathbf{x}_{\text{enemy\_center}} = \arg\max_{\mathbf{x} \in \Omega} M_{\text{enemy\_prox}}(\mathbf{x})$$

当考虑避免友军伤害（Friendly Fire Avoidance）时，系统构建带惩罚权重的加权差分图：

$$M_{\text{target\_cluster}}(\mathbf{x}) = \alpha \cdot M_{\text{enemy\_loc}}(\mathbf{x}) - \beta \cdot M_{\text{ally\_loc}}(\mathbf{x})$$

其中系数 $\alpha, \beta > 0$ 用于调节杀伤收益与误伤惩罚比。此时的极值点对应于“脱离友军交织区的高浓度敌军核心”。

### 2.2 威胁轴线（Threat Axis）线段剖分算法

智能体通过连接自身位置（或友军集群质心 $\mathbf{x}_{\text{ally\_center}}$）与敌军空间质心 $\mathbf{x}_{\text{enemy\_center}}$，构建战术威胁轴线线段 $\mathcal{L}$：

$$\mathcal{L}(t) = (1 - t)\mathbf{x}_{\text{agent}} + t \mathbf{x}_{\text{enemy\_center}}, \quad t \in [0, 1]$$

- **阻断落点解算（Interception Point Selection）**：阻断法术（如火墙、拒马、减速场）的最佳放置点为威胁轴线上特定参数化插值点 $\mathbf{x}_{\text{block}} = \mathcal{L}(t_{\text{block}})$。工业界常用经验参数包括：
  - 中点中继拦截（Midpoint Interception）：$t_{\text{block}} = 0.5$；
  - 压制性前沿阻断（Suppressive Forward Blocking）：$t_{\text{block}} \in [0.6, 0.75]$；
  - 阵地近程防护（Close Guarding）：$t_{\text{block}} \in [0.2, 0.35]$。

---

## 3. 多智能体战术机动与空间排斥（Spacing）架构

在战术机动控制（Movement Commands）中，自主智能体需要同时解算多维战术意图：“规避敌方威胁”、“向掩体或战略点靠拢”以及“保持友军间距以规避范围伤害（AOE Dispersion）”。

### 3.1 空间排斥代数推导

为了在移动过程中实现自然的动态间距（Spacing Behavior），避免传统导向行为（Steering Behaviors）在复杂地形中出现的局部极小值（Local Minima）或刚性碰撞振荡，影响图系统在网格层级进行代数解耦。

对于决策智能体 $i$，其所属阵营友军集合记为 $\mathcal{A}$，敌军集合记为 $\mathcal{E}$。智能体自身的局部邻近度模板记为 $T_{\text{prox}, i}$。

友军间距图（Ally Spacing Map, $S_{\text{ally}, i}$）剥离智能体自身的空间影响，只保留编队中其他个体的排斥场：

$$S_{\text{ally}, i}(\mathbf{x}) = M_{\text{ally\_loc}}(\mathbf{x}) - T_{\text{prox}, i}(\mathbf{x}; \mathbf{x}_i, R_{\text{speed}})$$

由此，带间距约束的复合机动评估函数为：

$$W_{\text{position}}(\mathbf{x}) = \left[ M_{\text{objective}}(\mathbf{x}) - \lambda_{\text{spacing}} S_{\text{ally}, i}(\mathbf{x}) \right] \cdot T_{\text{interest}, i}(\mathbf{x})$$

其中 $\lambda_{\text{spacing}} \in [0, 1]$ 为排斥强度系数（工程上通常标定为 $0.5$），确保防扎堆行为不会覆盖核心战术移动目标。

### 3.2 阵营空间交互操作类型矩阵

| 操作语义 (Semantic Action) | 代数运算式 (Algebraic Formulation) | 战术输出特征 (Tactical Property) | 典型应用场景 (Use Case) |
| :--- | :--- | :--- | :--- |
| **火力集火区定位 (Target Clustering)** | $W = M_{\text{enemy\_loc}} \odot T_{\text{interest}}$ | 检索离自身最近的高密度敌军中心 | 范围伤害法术（AOE Spells）、手榴弹投掷 |
| **友军增益区定位 (Ally Buffing)** | $W = M_{\text{ally\_loc}} \odot T_{\text{interest}}$ | 检索离自身最近的高密度友军集群 | 群体治疗（Group Heal）、光环增益覆盖 |
| **单兵战术规避 (Withdrawal / Kiting)** | $W = (M_{\text{enemy\_loc}})^{-1} \odot T_{\text{interest}}$ | 寻找兼顾脱离敌人物理接触与自身位移最小的位置 | 远程射手风筝拉扯、刺客战术脱离 |
| **战场前沿推演 (Battlefront Frontline)** | $W = \text{Norm}(M_{\text{enemy\_threat}} \odot M_{\text{ally\_threat}}) \odot T_{\text{interest}}$ | 定位双方火力网或控制权交叠度最高的地带 | 重装步兵（Tanks）卡点、战线推进 |
| **阵型离散机动 (Frontline Formation)** | $W = \left[\text{Norm}(M_{\text{front}}) - 0.5 S_{\text{ally}}\right] \odot T_{\text{interest}}$ | 沿交火线展开排开，防止友军扎堆拥挤 | 步兵横向阵列推进、战壕防御编组 |

---

## 4. 模块化战术影响图管线实现

在生产级实现中，工作图（`WorkingMap`）作为线程安全或双缓冲的临时计算画布（Scratchpad Buffer），支持链式代数流水线处理。

### 4.1 核心操作接口设计模式

```
+-----------------------------------------------------------------------------------+
|                                   WorkingMap                                      |
+-----------------------------------------------------------------------------------+
| - m_gridBuffer: float[]                                                           |
| - m_width: int                                                                    |
| - m_height: int                                                                   |
| - m_cellSize: float                                                               |
+-----------------------------------------------------------------------------------+
| + New(origin: Vector3): void                                                      |
| + Add(source: IInfluenceLayer, scalar: float): void                               |
| + AddInverse(source: IInfluenceLayer, scalar: float): void                        |
| + Multiply(source: IInfluenceLayer, scalar: float): void                          |
| + Normalize(): void                                                               |
| + GetHighestLocation(): Vector3                                                   |
| + GetLowestLocation(): Vector3                                                    |
+-----------------------------------------------------------------------------------+
                                         | 依赖组合 (Composes)
                                         v
+-----------------------------------------------------------------------------------+
|                                 IInfluenceLayer                                   |
+-----------------------------------------------------------------------------------+
| + Sample(worldPos: Vector3): float                                                |
| + RasterizeTo(targetBuffer: float[], width: int, height: int, cellSize: float)    |
+-----------------------------------------------------------------------------------+
          ^                                              ^
          | 实现 (Implements)                            | 实现 (Implements)
+-----------------------+                      +-----------------------+
|      SpatialMap       |                      |    InterestTemplate   |
| (Location/Threat Map) |                      |   (Kernel Footprint)  |
+-----------------------+                      +-----------------------+
```

### 4.2 工业级典型战术用例代码实现

#### 4.2.1 范围打击落点判定（AOE Attack Targeting）

```cpp
// 寻找最具杀伤价值且在施法射程/机动容许范围内的范围打击目标点
Vector3 EvaluateAOEAttackTarget(const Vector3& agentLocation, float agentSpeed)
{
    WorkingMap workingMap;
    // 1. 初始化以智能体为中心的工作计算缓冲区
    workingMap.New(agentLocation);

    // 2. 注入敌方密度图（权重 1.0）
    workingMap.Add(LocationMap(agentLocation, Faction::ENEMY), 1.0f);

    // 3. 施加个人兴趣模板（通过乘法截断并优先选择机动消耗低、靠近自身的区域）
    workingMap.Multiply(InterestTemplate(agentSpeed), 1.0f);

    // 4. 获取最优战术峰值坐标
    return workingMap.GetHighestLocation();
}
```

> **架构注记**：若将 `Faction::ENEMY` 替换为 `Faction::ALLY`，该函数直接复用为群体增益光环（Group Buff）的最佳落点判定模块。

#### 4.2.2 威胁反相规避机动（Movement to Safer Spot）

安全点检索依赖于反相（Inverse）算子。当需要规避物理接触或规避威胁场时，必须将最低风险区域转换为评分最高点，使得全局搜索算子统一为单调寻优接口 `GetHighestLocation()`。

若网格单元最大可能取值为 $V_{\max}$，反相算子在局部执行线性或非线性映射：
$$\text{AddInverse}(M, \omega) \implies W(\mathbf{x}) \leftarrow W(\mathbf{x}) + \omega \cdot \left( V_{\max} - M(\mathbf{x}) \right)$$

```cpp
// 寻求解脱物理缠斗或规避重度威胁场的移动目标点
Vector3 EvaluateSaferWithdrawalSpot(const Vector3& agentLocation, float agentSpeed, bool avoidThreatField)
{
    WorkingMap workingMap;
    workingMap.New(agentLocation);

    if (!avoidThreatField)
    {
        // 模式 A：规避物理近战包围（基于实体离散位置图的反相）
        workingMap.AddInverse(LocationMap(agentLocation, Faction::ENEMY), 1.0f);
    }
    else
    {
        // 模式 B：规避远程火力投射包线（基于威胁场图层的反相）
        workingMap.AddInverse(ThreatMap(agentLocation, Faction::ENEMY), 1.0f);
    }

    // 乘上个人移动能力约束，筛选距离可行的安全网格点
    workingMap.Multiply(InterestTemplate(agentSpeed), 1.0f);

    return workingMap.GetHighestLocation();
}
```

#### 4.2.3 动态战线阵地规划与防拥挤解耦（Nearest Battlefront with Spacing）

战线本质上是**敌我双方威胁投影的交界相交区域**。通过双重图层逐元素相乘，由于只有在双方威胁值均大于零的区域才会产生非零积，系统自动勾勒出对抗前沿线（Frontline）。

```
影响度 (Influence)
1.0 +                      [交战前沿峰值 Frontline Peak]
    |       敌方威胁场                   |                    友方威胁场
    |   (Enemy Threat)                 v                 (Ally Threat)
0.8 +         \                        /\                        /
    |          \                      /  \                      /
0.6 +           \                    /    \                    /
    |            \                  /      \                  /
0.4 +             \                /        \                /
    |              \              /          \              /
0.2 +               \            /            \            /
    |                \          /              \          /
0.0 +---+---+---+---+-\--------/----------------\--------/-+---+---+---+
    0   2   4   6   8  10 12 14 16 18 20 22 24 26 28 30 32 34 36 38 40 (Grid Index)
                      [敌我威胁重叠相乘区：乘积产生前线脊线]
```

前线定位复合算法如下：

```cpp
// 解算自身在战线上的分散卡位点，杜绝同盟单位阵列拥挤
Vector3 EvaluateBattlefrontPositionWithSpacing(const Vector3& agentLocation, float agentSpeed)
{
    // 步骤 1：构建友军局部排斥场（Ally Spacing Map）
    // 必须扣除智能体自身的空间足迹，避免自我排斥（Self-Repulsion）
    WorkingMap allySpacingMap;
    allySpacingMap.New(agentLocation);
    allySpacingMap.Add(LocationMap(agentLocation, Faction::ALLY), 1.0f);
    allySpacingMap.Add(LocationTemplate(agentLocation, agentSpeed), -1.0f);

    // 步骤 2：主决策工作图执行战线合成流水线
    WorkingMap workingMap;
    workingMap.New(agentLocation);

    // 2.1 注入敌方威胁投影
    workingMap.Add(ThreatMap(agentLocation, Faction::ENEMY), 1.0f);

    // 2.2 乘上友方威胁投影 -> 提取交火重叠脊线（Overlap Ridge）
    workingMap.Multiply(ThreatMap(agentLocation, Faction::ALLY), 1.0f);

    // 2.3 归一化动态范围至 [0.0, 1.0]，保证后续加权比率稳定
    workingMap.Normalize();

    // 2.4 施加友军防扎堆排斥修正（排斥权重标定为 -0.5f，确保战线吸引力占优）
    workingMap.Add(allySpacingMap, -0.5f);

    // 2.5 个人移动能力收敛约束
    workingMap.Multiply(InterestTemplate(agentSpeed), 1.0f);

    // 2.6 输出阵地卡位坐标
    return workingMap.GetHighestLocation();
}
```

---

## 5. 决策执行上下文：影响图与主流决策模型集成

模块化战术影响图并非孤立存在，而是作为**空间推理黑板（Spatial Reasoning Blackboard）**的服务提供者，嵌入到行为树（Behavior Trees, BT）、分层任务网络（Hierarchical Task Networks, HTN）或效用系统（Utility Systems）中。

```
+-----------------------------------------------------------------------------------+
|                        行为树节点拓扑 (Behavior Tree Topology)                      |
+-----------------------------------------------------------------------------------+
                                         |
                                [Selector: 战斗战术]
                                         |
            +----------------------------+----------------------------+
            |                                                         |
  [Sequence: 战线推进]                                      [Sequence: 战术后撤]
            |                                                         |
   +--------+--------+                                       +--------+--------+
   |                 |                                       |                 |
(Condition)       (Action)                                (Condition)       (Action)
受到近战威胁?   [计算战线展开点]                           生命值过低?    [计算反相安全脱离点]
(Threat < 0.2)       |                                    (HP < 25%)           |
            +--------v--------+                                       +--------v--------+
            |  Influence Map  |                                       |  Influence Map  |
            |   Battlefront   |                                       |   Safer Spot    |
            |     Pipeline    |                                       |     Pipeline    |
            +--------+--------+                                       +--------+--------+
                     | 写入黑板 (Set TargetPos)                                | 写入黑板 (Set TargetPos)
                     v                                                         v
            (Task: NavMeshMoveTo)                                   (Task: NavMeshMoveTo)
```

### 5.1 数据交互契约与时空分级执行

1. **黑板数据流（Blackboard Data Binding）**：
   - 行为树的条件评估节点（Decorator / Condition）周期性查询影响图局部采样值（例如：`Sample(MyPos, ThreatMap) > Threshold`）。
   - 执行叶节点（Task Node）调用流水线生成最优 `Vector3` 目标点，写入智能体私有黑板 `Key_MoveTarget`。
   - 底层路径规划器（NavMesh Pathfinding）与导向行为（Steering Behaviors）接收该目标点执行局部避障移动。

2. **异步与分时更新（Amortized Time-Slicing）**：
   - **全局层（Global Threat/Location）**：以低频（$2 \sim 5\ \text{Hz}$）异步更新，采用后台任务或图形管线（Compute Shader）并行生成。
   - **个体工作层（WorkingMap Pipeline）**：在决策发起帧动态由智能体即时分配并销毁，或基于对象池复用单帧计算内存，时间复杂度严格限制在 $O(K^2)$（其中 $K$ 为兴趣模板直径网格数，通常 $K \le 32$），从而满足 $60\ \text{FPS}$ 严格时间预算（$< 0.1\ \text{ms}$ 每次空间查询）。

---

## 6. 核心参考文献

- **[Tozour 01]** Tozour, P. 2001. *Influence mapping*. In *Game Programming Gems 2*, ed. M. DeLoura, pp. 287–297. Hingham, MA: Charles River Media.
- **[Woodcock 02]** Woodcock, S. 2002. *Recognizing strategic dispositions: Engaging the enemy*. In *AI Game Programming Wisdom*, ed. S. Rabin, pp. 221–232. Hingham, MA: Charles River Media.
