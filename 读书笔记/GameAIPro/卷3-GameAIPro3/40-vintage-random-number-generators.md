---
type: Reference
title: "第40章 Vintage Random Number Generators"
description: "Game AI Pro 工业级精读：Vintage Random Number Generators。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
tags:
  - game-ai
  - game-ai-pro
  - behavior-trees
  - utility-ai
  - pathfinding
  - spatial-reasoning
status: stable
verified: []
maturity: L2
updated: 2026-09-07
---

# 第40章 Vintage Random Number Generators

> 来源：*Game AI Pro 3: Balanced Cuts of Game AI Professionals*, Chapter 40.  
> 原文作者 / 资源：[Vintage Random Number Generators](http://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter40_Vintage_Random_Number_Generators.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷3-GameAIPro3](README.md) ｜ [专栏首页](../README.md)

---

在现代游戏人工智能与游戏系统工程中，无论是行为树（Behavior Trees, BT）中的概率条件装饰节点、效用系统（Utility Systems）中的动作加权抖动、分层任务网络（Hierarchical Task Networks, HTN）的分解变体选择，还是导向行为（Steering Behaviors）中的巡逻扰动向量计算，其底层均高度依赖**伪随机数发生器（Pseudo-Random Number Generator, PRNG）**。

然而，许多游戏引擎与工业代码库因追求极致轻量与快速原型开发，仍大量依赖 C/C++ 标准库内置的 `rand()`。这些经典线性同余发生器（Linear Congruential Generators, LCGs）由于内在的数学结构缺陷，会在游戏 AI 的长期模拟中引发严重的确定性偏差、超平面聚集效应与低比特位短周期循环，进而破坏决策的自然度，导致玩家感知到“AI 作弊”或机械重复。

本文基于 Éric Jacopin 的研究，系统性剖析经典 LCG 的代数机理、标准库实现的严重缺陷、布尔决策退化模型，并提供工业级的高质量混合 LCG（Combined LCG）架构实现方案。

---

## 1. 线性同余发生器（LCG）的数学公理与代数推导

### 1.1 递推关系与通项公式展开
线性同余发生器本质上是一个一阶线性差分方程在模运算下的离散动力系统。其递推公理定义为：

$$x_n = (a \cdot x_{n-1} + b) \pmod m$$

其中参数空间受以下约束限制：
- 模数 $m \in \mathbb{Z}^+, m > 0$；
- 乘数 $a \in \mathbb{Z}, 0 < a < m$；
- 增量（偏移量）$b \in \mathbb{Z}, 0 \le b < m$；
- 种子（初始状态）$x_0$。

#### 通项代数展开
若将同余方程展开为关于种子 $x_0$ 的显式形式：
$$x_1 = a x_0 + b$$
$$x_2 = a(a x_0 + b) + b = a^2 x_0 + a b + b$$
$$x_3 = a(a^2 x_0 + a b + b) + b = a^3 x_0 + a^2 b + a b + b$$

归纳推导得到 $x_n$ 未取模时的展开式：
$$x_n = a^n x_0 + b \sum_{i=0}^{n-1} a^i$$

当 $a > 0, b \ge 0, x_0 > 0$ 时，该递推数列在欧氏空间中呈指数级发散：
$$\lim_{n \to \infty} x_n = \infty$$

例如，若设 $a = 2, b = 3, x_0 = 0$：
- $x_{15} = 98301$（已无法容纳于 16 位无符号整数 `uint16_t`，上限 65535）；
- $x_{29} = 1610612733$（逼近 32 位有符号整数 `int32_t` 的上限 $2^{31}-1 = 2147483647$）。

因此，必须依赖模运算（Modulo Operation）将状态空间严格映射回有限离散环 $\mathbb{Z}/m\mathbb{Z} = \{0, 1, \dots, m-1\}$。

### 1.2 同余代数性质与周期性
两个整数 $u$ 与 $v$ 在模 $m$ 下同余，当且仅当二者之差被 $m$ 整除：
$$u \equiv v \pmod m \iff (u \bmod m) = (v \bmod m)$$

由于有限状态环的大小至多为 $m$，根据鸽巢原理（Pigeonhole Principle），LCG 必然生成一个循环序列，其输出具有严格的周期性（Periodicity）。即存在最小正整数 $p \le m$，对任意足够大的 $n$，均满足：
$$x_{n+p} = x_n$$

### 1.3 乘同余发生器（Multiplicative LCG）的奇点与设计准则
当增量 $b = 0$ 时，LCG 退化为乘同余发生器（Multiplicative LCG, MLCG）：
$$x_n = (a \cdot x_{n-1}) \pmod m$$

#### MLCG 的工程致命奇点（Zero-Trap）
若在 MLCG 中种子初始化为 $x_0 = 0$，则：
$$x_1 = (a \cdot 0) \pmod m = 0 \implies \forall n \ge 0,\; x_n = 0$$
发生器将立即陷入零陷阱（Zero-Trap），丧失一切伪随机特征。因此，**乘同余发生器的有效种子区间必须严格截断在** $x_0 \in [1, m-1]$。

#### 零点规避定理
为防止非零初值在后续迭代中演化出 $x_n = 0$，乘同余系统的参数必须满足：仅当 $x_{n-1} \equiv 0 \pmod m$ 或模数因子化发生抵消时才会出现 0。工业级 MLCG（例如 Derrick Lehmer 于 1949 年提出的经典发生器）通常选取 $m$ 为大质数，并设计乘数 $a$ 为模 $m$ 的原根（Primitive Root），从而保证只有 $x_{n-1} = m$（在同余环内等价于 0）才会导致 $x_n = 0$。由于所有合法生成值严格处于 $[1, m-1]$，系统在全周期内永远无法触及 0。

- **Lehmer 1949 经典配置**：
  $$x_n = (23 \cdot x_{n-1}) \pmod{10^8 + 1}$$
  该系统的周期经严格代数证明为：
  $$P = 5\,882\,352$$

---

## 2. 空间几何病理分析：超平面网格与相关性陷阱

为评估 PRNG 的内在均匀性，工业界常将连续生成的随机数对构造成二维或高维相空间向量：
$$P_n = (x_{n-1}, x_n)$$

将坐标归一化到单位正方形 $[0, 1) \times [0, 1)$：
$$\hat{P}_n = \left( \frac{x_{n-1}}{m}, \frac{x_n}{m} \right)$$

### 2.1 二维相空间网格化现象（Lattice Structure / Marsaglia 效应）
理想的真随机数发生器在连续相空间投影中应当呈现各向同性的二维泊松点分布（Poisson Point Process）。然而，由于 LCG 的线性迭代法则：
$$x_n = a x_{n-1} + b - k \cdot m \quad (k \in \mathbb{Z})$$
归一化坐标后满足一族平行的线性超平面方程：
$$\frac{x_n}{m} = a \cdot \left( \frac{x_{n-1}}{m} \right) + \frac{b}{m} - k$$

```
   Xn/m
   1.0 ^       /       /       /       /
       |      /       /       /       /   斜率极为陡峭的
       |     /       /       /       /    平行线簇 (Slope = a)
       |    /       /       /       /
       |   /       /       /       /    在这些晶格超平面之外，
       |  /       /       /       /     相空间存在巨大的“空白带”
   0.0 +----------------------------------> Xn-1/m
       0.0                                1.0
```

当乘数 $a$ 较小或模数 $m$ 不足以支撑高维分布时，散点图会退化为明显倾斜的晶格线段（如 $a=23, m=10^8+1$ 的相空间所示）。即使是在 32 位模数下，Marsaglia 极限定理（Marsaglia 1968, "Random Numbers Fall Mainly in the Planes"）证明：$d$ 维空间中的所有 LCG 采样点全部落在至多 $(d! \cdot m)^{1/d}$ 个平行超平面上。

这在游戏 AI 系统中会产生严重的病理表现：
- **空间巡逻与环境散布（Spatial Sampling）**：在导航网格（NavMesh）上使用连续生成的 `(rand(), rand())` 撒点时，NPC 的空间位置会出现肉眼可见的条带状聚集，完全破坏拟真环境的自然散布。
- **感知与视野抖动（Perception Jittering）**：AI 视锥范围的微扰向量若陷入超平面相关性，会导致 NPC 视线在特定扫描角度出现无法覆盖的“感知盲区盲带”。

---

## 3. 经典编译器内置 `rand()` 解构与比特周期衰减

### 3.1 ANSI C 标准库参考实现
ANSI C (X3J11 1988) 提出的标准便携参考实现如下：

```c
static unsigned long int next = 1;

int rand(void) /* 假定 RAND_MAX 为 32767 */
{
    next = next * 1103515245 + 12345;
    return (unsigned int)(next / 65536) % 32768;
}
```

该实现的数学参数为：
- $a = 1103515245$
- $b = 12345$
- 内部完整模数为 $2^{32}$（依赖 32 位无符号整数的溢出截断自然取模）
- 截断模数 $m = 32768 = 2^{15}$，即 `RAND_MAX` = 32767

### 3.2 Visual Studio C/C++ 运行时实现
Microsoft Visual C++ (VC++ 2015 及历史版本) 采用的 LCG 公式为：

$$x_n = (214013 \cdot x_{n-1} + 2531011) \pmod{2^{31}}$$

该发生器的增量 $b = 2531011 \ne 0$，因此种子可安全选取在 $[0, 2^{31}-1]$ 闭区间内。整个状态环中存在且仅存在唯一的定点（Fixed Predecessor）$x_{n-1} = 561051201$，使得下一个状态恰好归零：

$$(214013 \cdot 561051201 + 2531011) \pmod{2^{31}} = 0$$

### 3.3 模 $2^k$ 型发生器的比特周期衰减定理（Bit-Slice Period Degradation）
当模数取 $2^k$ 的二次幂形式时，LCG 具有一个致命的代数缺陷：**低有效位（Least Significant Bits, LSB）的周期极短**。

根据 L'Écuyer (1990) 的形式化证明：对于模 $m = 2^k$ 的 LCG，其状态序列的第 $i$ 个比特位（Bit $i$，从最低位 $i=0$ 开始计数）的循环周期至多为：

$$\text{Period}(\text{Bit } i) = 2^{i+1}$$

在 Visual C++ LCG（初值 $x_0 = 0$）的连续状态二进制切片中，低比特位呈现出严格的周期性跳变：

| 比特位置 | 提取表达式 | 输出序列展开（前 16 次迭代） | 比特独立周期 |
| :--- | :--- | :--- | :--- |
| **Bit 0** | $x_n \ \& \ 1$ | $1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 \dots$ | $2^1 = 2$ |
| **Bit 1** | $x_n \ \& \ 2$ | $1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0 \dots$ | $2^2 = 4$ |
| **Bit 2** | $x_n \ \& \ 4$ | $0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0 \dots$ | $2^3 = 8$ |
| **Bit 3** | $x_n \ \& \ 8$ | $0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0 \dots$ | $2^4 = 16$ |

#### MSVC 的工程掩盖手段与残余缺陷
微软工程师意识到低位周期的严重缺陷，利用代数因式分解：
$$2^{31} = 2^{16 + 15} = 2^{16} \times 2^{15}$$
在 `rand()` 实现中，将 $x_n$ 右移 16 位（即整除 $2^{16}$），完全丢弃最低的 16 个低位（这些位包含最短的重复周期）：
$$\text{rand}() = (x_n \gg 16) \ \& \ 0\text{x}7\text{FFF}$$
返回结果严格落在 $[0, 2^{15}-1]$ 范围内。

然而，这种截断并不能根治缺陷：
- 截断后输出值的最低位（实际来源于原始状态的第 16 位）其周期仅仅恢复到 $2^{17} = 131072$；
- 整个生成序列的有效自由度仅为 15 个比特位（`RAND_MAX = 32767`）。这在大型游戏循环中，短短数秒的频繁调用就会遍历完其动态范围。

---

## 4. 游戏 AI 决策中的布尔退化病理

在游戏行为树的条件分支评估中，经常需要依据概率生成布尔决策结果（例如：“是否执行战术规避”、“是否发起掩护射击”）。工程实践中主要存在两种经典的错误生成方式。

### 4.1 取模法：`RandBoolMod`
开发者利用最低有效位直接求模：

```cpp
bool RandBoolMod(void)
{
    return ((rand() % 2) == 1) ? true : false;
}
```

#### 病理分析
由于截断后的 `rand()` 其最低有效位实质上对应原发生器的第 16 位，其周期仅为 $2^{17}$。更严重的是，在部分未做高位截断的简易 LCG 中，`rand() % 2` 对应 Bit 0，其值将变成严格交替的 `1, 0, 1, 0, 1, 0...`。这会导致 AI 的行为树在“攻击”与“发呆”之间高频震荡，产生严重的系统抖动。

### 4.2 归一化截断法：`RandBoolDiv`（虚幻引擎 4 历史实现方式）
利用浮点归一化投影到单位区间后乘 2 截断：

```cpp
bool RandBoolDiv(void)
{
    int b = (int)((2.0f * rand()) / (RAND_MAX + 1));
    return (b == 1) ? true : false;
}
```

### 4.3 实验对比与“AI 作弊”感知（Perception of Cheating）
Éric Jacopin 的蒙特卡洛统计实验记录了两种方法生成“连续相同布尔值游程长度（Length of a Series of the Same Boolean Value）”的分布频率：

```
系列出现频次 (Counts)
600,000 ^  [*] (RandBoolDiv: 产生极长的连续相同串)
500,000 |   .  (RandBoolMod: 串长度衰减极为迅速)
400,000 |
300,000 |   [*]
200,000 |    .
100,000 |       [*]
      0 +--------.---[*]--[*]--[*]---------------->
        0        5   10   15   20   25 (游程长度)
```

#### 决策缺陷机理
1. **连续同值游程过长**：`RandBoolDiv` 提取的是高有效位。高位数字在空间中变化较缓，倾向于生成极长的全 `true` 或全 `false` 序列（游程长度达到 15 至 20 以上）；
2. **玩家心理学偏差（Rabin 2004）**：人类玩家对概率的认知存在典型的“赌徒谬误（Gambler's Fallacy）”。当一个命中概率为 50% 的 AI 射击决策依赖 `RandBoolDiv` 时，系统极易连续出现 15 次“命中”。对于玩家而言，这种统计偏差不会被归因于数学随机性，而是被直接感知为“**系统在作弊（Game is Cheating）**”。反之，当 AI 连续 15 次判定规避失败时，又表现得极其愚蠢。

---

## 5. 高阶单体 LCG 参数配置矩阵

在无法引入复杂重型算法（如 Mersenne Twister MT19937，其需要 2.5KB 内部状态缓冲区及昂贵的提取开销）的高性能受限场景下，应选择经数学证明具有最大周期与优秀谱测试（Spectral Test）分数的参数：

### 5.1 工业与航天级参数拓扑

| 参数体系 | 乘数 $a$ | 增量 $b$ | 模数 $m$ | 周期长度 $P$ | 来源与特性评估 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Lehmer 1949** | $23$ | $0$ | $10^8 + 1$ | $5\,882\,352$ | 早期计算机经典乘同余系统，存在晶格条带 |
| **NASA TP-2105 (1)** | $16\,807$ | $0$ | $2^{31} - 1$ | $2^{31} - 2$ | Howell & Rheinfurth (1982)，通过多项均匀性检验 |
| **NASA TP-2105 (2)** | $29\,903\,947$ | $0$ | $2^{31} - 1$ | $2^{31} - 2$ | Howell & Rheinfurth (1982)，具备优异的谱表现 |
| **Park-Miller 最小标准** | $16\,807$ | $0$ | $2^{31} - 1$ | $2^{31} - 2$ | Park & Miller (1988), L'Écuyer (1988) 广泛验证的基础标准 |
| **L'Écuyer 最优乘同余 (1)**| $742\,938\,285$ | $0$ | $2^{31} - 1$ | $2^{31} - 2$ | 谱测试高维评价极佳 |
| **L'Écuyer 最优乘同余 (2)**| $950\,706\,376$ | $0$ | $2^{31} - 1$ | $2^{31} - 2$ | 谱测试高维评价极佳 |
| **L'Écuyer 最优乘同余 (3)**| $630\,360\,016$ | $0$ | $2^{31} - 1$ | $2^{31} - 2$ | 谱测试高维评价极佳 |
| **L'Écuyer 特选大模数** | $40\,692$ | $0$ | $2\,147\,483\,399$ | $2\,147\,483\,398$ | 模数为 $2^{31}-85$，专为组合发生器设计的高性能参数 |

> 注：模数 $2^{31} - 1 = 2\,147\,483\,647$ 为第 8 个梅森素数（Mersenne Prime $M_{31}$）。选取素数作为模数可以确保乘同余发生器在适当乘数下达到最大理论周期 $m - 1$。

---

## 6. 组合线性同余发生器（Combined LCGs）

即便单体 LCG 的参数调至最优，其相空间依然无法摆脱超平面网格化的固有缺陷。Pierre L'Écuyer 在 1988 年提出了突破性的架构方案：**通过非线性差分组合两个或多个独立周期的单体 LCG，能够在极低算力开销下彻底粉碎晶格结构，将发生器周期推向天文数字级别。**

### 6.1 组合发生器数学架构
定义两个使用不同素数模数的独立 MLCG 子发生器系统：

$$\begin{aligned}
x_{1, n} &= (a_1 \cdot x_{1, n-1} + b_1) \pmod{m_1} \\
x_{2, n} &= (a_2 \cdot x_{2, n-1} + b_2) \pmod{m_2}
\end{aligned}$$

L'Écuyer 组合算子定义为两者在调整模数空间中的非对称差分：

$$Z_n = (x_{1, n} - x_{2, n}) \pmod{(m_1 - 1)}$$

最终生成的标准离散随机序列定义在环 $\mathbb{Z}/(m_1 - 1)\mathbb{Z}$ 上：
$$x_n = \begin{cases} 
Z_n + (m_1 - 1) & \text{若 } Z_n < 0 \\
Z_n & \text{若 } Z_n > 0 \\
m_1 - 1 & \text{若 } Z_n = 0 
\end{cases}$$

归一化连续值通过下式映射至实数开区间 $(0, 1)$：
$$U_n = \frac{x_n}{m_1}$$

#### 理论突破
- **周期叠加效应**：若两个发生器的模数均为素数，且参数互质，组合发生器的周期长度扩展至：
  $$P \approx \frac{(m_1 - 1)(m_2 - 1)}{2} \approx \mathcal{O}(m_1 \cdot m_2)$$
  对于 32 位系统，周期可从 $2 \times 10^9$ 跃升至约 $2.3 \times 10^{18}$；
- **几何结构重塑**：两个不同频率与空间维度的晶格发生器相减，打破了单一超平面向量约束。相空间散点图在二维及三维投影下均表现出完全各向同性的均匀分布（如图 40.5 所示），消除了感知条带。

---

## 7. 工业级 C++ 工程实现与 AI 系统集成

在现代游戏引擎中，PRNG 模块必须满足：
1. **零动态内存分配与 Cache 友好**：内部状态轻量（仅需 8 到 16 字节），可直接作为组件内嵌到 AI 代理的黑板（Blackboard）中；
2. **多线程确定性保证（Thread-Safe Determinism）**：禁止使用全局静态变量，确保每个 AI 实体拥有独立的随机流，保障网络同屏与录像回放的一致性；
3. **消除 64 位整数除法溢出**：采用 Schrage 算法技术或利用现代 64 位整型寄存器内联优化。

### 7.1 工业级 Combined LCG 核心实现（Modern C++17/20）

```cpp
#pragma once
#include <cstdint>
#include <limits>
#include <type_traits>

namespace GameAI::Math {

/**
 * @brief 基于 L'Écuyer 架构的双流组合线性同余发生器 (Combined LCG)
 * 内存占用: 8 字节 (两个 uint32_t 状态变量)
 * 周期: 约 2.30584 x 10^18 次迭代
 */
class CombinedLCG final
{
public:
    // 组合参数定义 (L'Écuyer 1988)
    static constexpr int32_t A1 = 40014;
    static constexpr int32_t M1 = 2147483563; // 素数 m1 = 2^31 - 85
    
    static constexpr int32_t A2 = 40692;
    static constexpr int32_t M2 = 2147483399; // 素数 m2 = 2^31 - 249

    /**
     * @brief 构造函数，包含合法种子范围强校验
     * @param seed1 发生器 1 初始种子，合法范围 [1, M1 - 1]
     * @param seed2 发生器 2 初始种子，合法范围 [1, M2 - 1]
     */
    constexpr explicit CombinedLCG(uint32_t seed1 = 12345, uint32_t seed2 = 67890) noexcept
        : m_s1((seed1 % (M1 - 1)) + 1)
        , m_s2((seed2 % (M2 - 1)) + 1)
    {
    }

    /**
     * @brief 生成下一个 31-bit 有符号伪随机整数
     * @return 离散取值范围 [1, M1 - 1]
     */
    inline int32_t NextInt() noexcept
    {
        // 子发生器 1 迭代 (利用 64 位宽寄存器避免模运算溢出)
        m_s1 = static_cast<int32_t>((static_cast<int64_t>(A1) * m_s1) % M1);

        // 子发生器 2 迭代
        m_s2 = static_cast<int32_t>((static_cast<int64_t>(A2) * m_s2) % M2);

        // L'Écuyer 差分组合算法
        int32_t z = m_s1 - m_s2;
        if (z < 1)
        {
            z += (M1 - 1);
        }

        return z;
    }

    /**
     * @brief 生成 [0.0, 1.0) 范围的单精度浮点数
     */
    inline float NextFloat() noexcept
    {
        return static_cast<float>(NextInt() - 1) / static_cast<float>(M1 - 1);
    }

    /**
     * @brief 生成 [0.0, 1.0) 范围的双精度浮点数
     */
    inline double NextDouble() noexcept
    {
        return static_cast<double>(NextInt() - 1) / static_cast<double>(M1 - 1);
    }

    /**
     * @brief 高可靠布尔决策生成 (彻底避免位周期衰减与长全同游程)
     * @param probabilityTrue 判定为 true 的概率阈值，范围 [0.0f, 1.0f]
     */
    inline bool NextBool(float probabilityTrue = 0.5f) noexcept
    {
        return NextFloat() < probabilityTrue;
    }

    /**
     * @brief 生成指定闭区间 [min, max] 内的离散整数
     */
    inline int32_t NextRange(int32_t min, int32_t max) noexcept
    {
        return min + static_cast<int32_t>(NextFloat() * (max - min + 1));
    }

private:
    int32_t m_s1;
    int32_t m_s2;
};

} // namespace GameAI::Math
```

---

### 7.2 行为树与效用系统中的集成拓扑

在具备高拟真度要求的游戏 AI 运行时中，随机数发生器严禁以全局单例形式存在。必须将其作为**确定性执行上下文（ExecutionContext）**的一部分注入决策管线：

```
[Agent AI Blackboard]
  ├── State Context
  ├── Navigation Memory
  └── Local PRNG (CombinedLCG)  <-- 每个 Agent 独立保持状态流
          │
          ├──> [Behavior Tree: Decorator Node]
          │       └── 判定概率规避: LocalLCG.NextBool(0.35f)
          │
          ├──> [Utility System: Action Scorer]
          │       └── 效用分权重微扰: UtilityScore += LocalLCG.NextFloat() * Epsilon
          │
          └──> [Steering Behaviors: Wander]
                  └── 圆周扰动采样: Angle = LocalLCG.NextFloat() * TWO_PI
```

#### 架构收益
1. **多线程并发安全**：各个智能体的行为评估在不同的 Worker 线程并发执行时无锁且无原子争用（Lock-Free, Cache-Line Independent）；
2. **复盘可重现性（Determinism for Replay System）**：仅需记录每个 Agent 在帧初始时的 8 字节种子，便可在服务端逻辑与客户端回放中完全还原所有 AI 决策行为，根除网络同步中的“反常状态断层（De-sync）”；
3. **消除作弊认知**：组合发生器的长周期与均匀谱分布避免了相同连续结果的堆叠，使 NPC 的概率响应完全符合自然心理预期。

---

## 8. 总结与现代游戏 AI 选型准则

```
================================================================================
                    游戏工程伪随机数发生器 (PRNG) 技术选型全景
================================================================================
  算法体系         内部状态空间  理论周期       相空间分布    工业级适用场景
--------------------------------------------------------------------------------
  标准库 rand()    4 Bytes       2^31 - 1       条带晶格严重  严禁在核心逻辑中使用
  单体高阶 LCG     4 Bytes       2^31 - 2       存在晶格缺陷  极度受限微控制器/极弱算力
  组合 LCG (cLCG)  8 Bytes       ~2.3 x 10^18   各向同性良好  游戏 AI 行为树/效用决策/轻量级物理
  PCG-XSH-RR       8~16 Bytes    2^64           统计测试极佳  现代现代游戏首选 (替代传统 LCG)
  Xoroshiro128+    16 Bytes      2^128 - 1      均匀度极高    高性能过程内容生成 (PCG)
  MT19937          2500 Bytes    2^19937 - 1    高维无瑕疵    大型静态预处理 / 严禁内嵌至 Agent
================================================================================
```

在系统级游戏 AI 的工程落地中，基础数学组件的严谨性直接决定了上层行为逻辑的鲁棒性。淘汰具有数十年技术负债的 `rand()`，全面采用具备空间解耦特征的**组合线性同余发生器（Combined LCG）**或现代置换同余算法，是构建高拟真、防作弊感知、确定性 AI 决策系统的底层关键基石。
