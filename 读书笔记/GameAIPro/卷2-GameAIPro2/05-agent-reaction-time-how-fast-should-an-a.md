---
type: Reference
title: "第5章 Agent Reaction Time: How Fast Should an AI React?"
description: "Game AI Pro 工业级精读：Agent Reaction Time: How Fast Should an AI React?。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第5章 Agent Reaction Time: How Fast Should an AI React?

> 来源：*Game AI Pro 2: More Collected Wisdom of Game AI Professionals*, Chapter 5.  
> 原文作者 / 资源：[Agent Reaction Time: How Fast Should an AI React?](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter05_Agent_Reaction_Time_How_Fast_Should_An_AI_React.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷2-GameAIPro2](README.md) ｜ [专栏首页](../README.md)

---

> **文集出处**：*Game AI Pro 2: More Collected Wisdom of Game AI Professionals*  
> **核心章节**：Chapter 5 - *Agent Reaction Time: How Fast Should an AI React?* (Steve Rabin)  
> **核心范式**：认知计时学（Mental Chronometry）、感知-动作时延建模（Perception-Action Latency）、辨识决策流（Go/No-Go Decision Pipeline）

---

## 1. 绪论：非真实感超人类反应的工程陷阱与认知计时学

### 1.1 工业痛点与问题陈述
在三维现代数字游戏（尤其是第一人称射击 FPS、动作游戏 ACT 与战术拟真射击游戏）的设计与工程实现中，游戏 AI 开发者常面临一个看似直观实则影响全局体验的平衡性难题：**当敌对智能体（Enemy AI Agent）在视线内侦测到玩家时，其从感知建立到执行射击动作应当经历多长的物理时间？**

若不加限制，传统的基于确定性状态机（Finite State Machines, FSM）或行为树（Behavior Trees, BT）的高频更新循环，会在游戏世界状态满足可视性条件（Line-of-Sight, LOS）的瞬间（即下一个逻辑帧，时延 $\le 16.6\,\text{ms}$ / $60\,\text{FPS}$）立即触发瞄准与射击逻辑。这种近乎零时延的“神经机械反射”（Aimbot-like Reflexes）会导致玩家产生严重的挫败感，违背了人类对“与真实人类/拟人化生物对抗”的沉浸预期，破坏了游戏的心流体验（Flow State）。

因此，建立逼真且具沉浸感的人类反应时间（Human Reaction Time）数学模型，是拟人化游戏 AI 感知与执行架构的核心模块。

### 1.2 认知计时学（Mental Chronometry）
科学解决该问题的理论根基来源于认知心理学与神经科学中具有逾百年研究历史的**认知计时学（Mental Chronometry）**[Posner 05]。认知计时学通过对受试者执行特定感知与认知任务时的反应时间（Reaction Time, RT）进行纳秒/毫秒级精密测定，以量化中枢神经系统内部信息加工、神经电信号传导、特征识别与运动皮层触发的微观时间消耗。

* **工程经验法则（Rule of Thumb）**：
  在绝大多数面向人类玩家的游戏上下文中，智能体的反应时延应当设计在 **$0.2\,\text{s} \sim 0.4\,\text{s}$** 的基准区间内；在此基础上，系统必须根据动态感知上下文（Contextual Stimuli）、认知复杂度与注意力衰减引入更长的动态时间调制。

---

## 2. 反应时延的认知上下文与心理学分类模型

心理生理学家与眼科学家 F. C. Donders 早在 1868 年就确立了神经反应速度的经典分级实验范式 [Donders 69]。根据信息处理通道的复杂度，智能体在游戏情境中主要面临两类典型反应决策上下文，以及一类更复杂的离散选择上下文：

```
                    [ 物理世界刺激源 (Stimulus Input) ]
                                    │
                                    ▼
       ┌─────────────────────────────────────────────────────────┐
       │                视觉/听觉感知阶段 (Sensing)                 │
       │     (光信号转电信号、视网膜/听觉神经转导、初级感知皮层输入)       │
       └────────────────────────────┬────────────────────────────┘
                                    │
                ┌───────────────────┴───────────────────┐
                ▼                                       ▼
     【单一预期刺激上下文】                    【多重辨识刺激上下文】
   (Simple Reaction Scenario)              (Go/No-Go Context)
                │                                       │
                ▼                                       ▼
 ┌──────────────────────────────┐        ┌──────────────────────────────┐
 │     刺激存在性确认           │        │     刺激模式辨识与分类       │
 │   (Stimulus Detection)       │        │ (Pattern Recognition / ID)   │
 └──────────────┬───────────────┘        └──────────────┬───────────────┘
                │                                       │
                │                                       ├─────────────┐
                │                                       │ (Match)     │ (Mismatch)
                │                                       ▼             ▼
                │                        ┌──────────────────┐  ┌──────────────┐
                │                        │ 决策通过 (Go)    │  │ 决策抑制      │
                │                        └────────┬─────────┘  │ (No-Go /     │
                │                                 │            │  Inhibition) │
                │                                 │            └──────┬───────┘
                ▼                                 ▼                   │
 ┌──────────────────────────────────────────────────────┐             ▼
 │               动作决策形成 (Decision to Act)          │           [中断]
 └──────────────────────────────┬───────────────────────┘
                                │
                                ▼
 ┌──────────────────────────────────────────────────────┐
 │         传出神经驱动 (Motor Neuron Output)           │
 │               (向效应器/指部肌肉发送收缩冲动)          │
 └──────────────────────────────┬───────────────────────┘
                                │
                                ▼
 ┌──────────────────────────────────────────────────────┐
 │           物理机械行程与微动触发 (Physical Action)     │
 │            (扣动扳机行程、击针撞击底火、武器发射)       │
 └──────────────────────────────────────────────────────┘
```

### 2.1 简单反应时间（Simple Reaction Time, SRT）
* **战术情景定义**：
  AI 智能体正将枪口预瞄准（Pre-aiming）于一处已知门道（Doorway），心理处于高度专注与预期状态，明确知晓只有潜在敌人会通过该门。当门后出现目标图像的瞬刻，AI 立即扣动扳机击发。
* **神经与物理时延链解构**：
  $$\Delta t_{\text{SRT}} = \Delta t_{\text{trans}} + \Delta t_{\text{detect}} + \Delta t_{\text{decision}} + \Delta t_{\text{motor\_eff}} + \Delta t_{\text{mech}}$$
  1. **感知转导（Sensory Transduction, $\Delta t_{\text{trans}}$）**：光子进入视网膜并转换为视神经冲动的传导时间；
  2. **存在性检测（Stimulus Detection, $\Delta t_{\text{detect}}$）**：初级视觉皮层辨识出视场中出现了有效刺激；
  3. **动作驱动决策（Action Initiation Decision, $\Delta t_{\text{decision}}$）**：预设反射被激活，形成扣动扳机的神经指令；
  4. **外周传导与肌电激活（Motor Efferent & Muscle Activation, $\Delta t_{\text{motor\_eff}}$）**：动作电位由运动皮层经由脊髓运动神经元传递至手指肌群并促使其收缩；
  5. **机械行程（Mechanical Travel, $\Delta t_{\text{mech}}$）**：扳机物理位移行程到达击发临界点至击针撞击子弹底火。

### 2.2 识别或去/不去反应时间（Recognition or Go/No-Go Time）
* **战术情景定义**：
  AI 智能体枪口对准门道，已知从门外出现的可能为友军目标（Teammate）亦可能为敌军目标（Enemy）。当视场出现人体轮廓时，AI 必须首先提取目标特征识别身份，只有当确认目标为敌人时才执行扣机（Go）；若识别为友军，则抑制射击冲动（No-Go）。
* **神经与认知开销差异**：
  相比于简单反应，Go/No-Go 范式增加了**特征提取与模式辨识（Pattern Recognition）**与**响应选择/反应抑制（Response Selection / Inhibition）**阶段。此过程需要大脑高级认知中枢介入，因此整体时延出现阶梯式上升。

### 2.3 复杂认知任务反应时间（Complex Cognitive Task Reaction Time）
当目标从单一通道扩展至“多选一”（Choice-Reaction Task, 譬如从多个掩体暴露的目标中评估威胁等级并选择最佳目标击发）时，依据 Donders B 类反应测试理论，决策空间的大小直接导致神经认知负荷呈非线性增长。此类任务无法直接套用恒定常数，必须依赖特定场景的人类工效学测试来建立参数分布。

---

## 3. 认知科学实验测定数据与反应时间基线

认知心理学文献为上述范式提供了严格受控实验环境下的统计学样本均值：

| 刺激模式 / 决策范式 | 典型实验室评估方法 | 人类受试者样本均值（$Mean$） | 权威文献来源 | 对应游戏 AI 场景 |
| :--- | :--- | :--- | :--- | :--- |
| **听觉简单反应 (Auditory SRT)** | 听到蜂鸣声后立即敲击按键 | $\approx \mathbf{0.16\,\text{s}}\ (160\,\text{ms})$ | [Kosinski 13] | 听到近距离脚步声/破门声时的转向警戒反射 |
| **视觉简单反应 (Visual SRT)** | 视标灯光亮起或颜色跳变（红变绿） | $\approx \mathbf{0.19\,\text{s}} \sim \mathbf{0.22\,\text{s}}$ | [Kosinski 13]<br>[Laming 68] | 高度架枪锁定状态下敌对目标探头（Peeking）击发 |
| **识别反应 (Go/No-Go Time)** | 视标呈现二位数，$\ge 50$ 敲击，$< 50$ 抑制 | $\approx \mathbf{0.38\,\text{s}} \sim \mathbf{0.40\,\text{s}}$ | [Laming 68] | 巡逻或架枪时辨识友军/敌军标签后的决断射击 |
| **复杂多项选择 (Choice RT)** | 面对 $N$ 种复杂刺激挑选最优项 | $>\mathbf{0.40\,\text{s}}$ 且随选项复杂度单调递增 | [Donders 69] | 战术射击中集火目标动态重选、换弹与撤退抉择 |

---

## 4. 调制因子：时延扩展的四大影响变量（Four Caveats）

实验室均值仅代表受试者处于**最佳照明、全神贯注、零运动行程**状态下的生理极限。在实际战场模拟中，以下四个物理与心理学变量会显著拉长反应时间基线：

```
                    ┌──────────────────────────────────────┐
                    │    基准时延 (Baseline Latency)       │
                    │   SRT: 0.2s   |   Go/No-Go: 0.4s     │
                    └──────────────────┬───────────────────┘
                                       │
        ┌──────────────────────────────┼──────────────────────────────┐
        ▼                              ▼                              ▼
【刺激强度与显著性】            【运动学对准开销】             【注意力分配状态】
(Stimulus Intensity / Salience) (Kinematic Aiming Overheads)   (Attentional Allocation)
  - 目标距离尺度衰减              - 视锥偏角与角速度积分         - 警戒（Vigilant）
  - 环境对比度与遮挡              - 关节角加速度极限             - 游离/松弛（Lapse）
  - 动态模糊/极端光照            - 武器质量与惯性矩             - 隧道效应（Tunnel Vision）
        │                              │                              │
        └──────────────────────────────┼──────────────────────────────┘
                                       │
                                       ▼
                    ┌──────────────────────────────────────┐
                    │  动态积分总时延 (Total Reaction Time) │
                    │     $T_{\text{reaction}} \ge 0.2s$   │
                    └──────────────────────────────────────┘
```

### 4.1 刺激强度与感知显著性（Stimulus Intensity & Saliency）
* **心理学机理**：Luce (1986) 证明刺激信号强度与大脑感知中枢响应时延成反比关系 [Luce 86]。
* **工业场景映射**：
  * **远距离微小轮廓（Weak Stimulus）**：当敌人在超远距离（如 $80\,\text{m}$ 外的像素级缝隙）闪现，AI 视网膜特征投影小、信噪比低，感知阈值累加变慢，需要更长时间确认目标存在。
  * **近距离全屏显现（Strong Stimulus）**：转角遭遇战贴脸出现（Close-up Encounter），强烈的轮廓与运动向量可在极短时间内冲破感知阈值，反应时间贴近生理下限。

### 4.2 运动学瞄准时间补偿（Kinematic Aiming Overhead）
* **工业场景映射**：
  基准反应时间仅涵盖了“决定扣动扳机至击发”的神经与微动耗时。若敌人在智能体视锥的边缘出现，枪口未与目标法线对齐：
  $$\Delta T_{\text{total}} = T_{\text{reaction}} + T_{\text{aim}}(\Delta \theta)$$
  其中 $T_{\text{aim}}$ 取决于智能体旋转躯干/手腕的角加速度、角速度以及武器的转动惯量（Moment of Inertia）。若把 $T_{\text{aim}}$ 误算在 $T_{\text{reaction}}$ 之内，会导致逻辑层感知与执行层物理运动脱节。

### 4.3 注意力游离与聚焦衰减（Attentional Lapses）
* **心理学机理**：人类并非无状态的数字系统，其注意力机制受警觉度（Vigilance）、认知负荷（Cognitive Load）和疲劳度影响，存在偶发性的注意力游离（Momentary Attentional Lapse）。
* **工业场景映射**：
  巡逻状态下处于低警觉水平的 AI，其反应时间分布曲线应具备更长且不规则的长尾（Long-tail Distribution），以表现出“迟钝”、“发愣”或“分心”的逼真人性化特征；相反，在交火掩体后处于高度预警（High Alert）状态的 AI 则趋近正态分布的下限。

### 4.4 认知辨识复杂度分级（Cognitive Task Complexity）
* **工业场景映射**：
  识别是否为掩体后的有效敌对目标可能涉及复杂的环境推理（例如：该角色穿着吉利服、处于烟雾边缘、或倒地佯死）。这类模式匹配任务的认知延迟需按多阶辨识模型进行积分。

---

## 5. 游戏 AI 架构中的数学模型与工业级实现

为将 Donders 反应模型与认知计时学无缝集成到工业级行为树、效用系统或感知模块中，本节提供一套完整的可落地数学模型与 C++ 工业级架构实现。

### 5.1 反应时间概率分布数学推导：Wald 分布（逆高斯分布）
在生物物理与认知科学中，反应时间并不遵循理想的高斯分布，因为真实生理反应存在不可逾越的绝对物理下限，且往往伴随注意力游离带来的右偏长尾效应。工业界推荐使用**漂移扩散模型（Drift-Diffusion Model, DDM）**衍生出的 **Wald 分布（Inverse Gaussian Distribution）** 或**偏态正态分布（Skew-Normal Distribution）**。

#### 刺激累积阈值方程
假设刺激证据在神经系统内的累积过程由布朗运动驱动：
$$d X_t = v \cdot dt + \sigma \cdot d W_t$$
其中 $v$ 为信息漂移率（Drift Rate），受刺激强度（距离、对比度）正向调节；$W_t$ 为标准维纳过程；当累加器状态 $X_t$ 首次触碰决策阈值 $a$ 时，即触发决策。

#### 总反应时间积分模型
$$\tau \sim \text{Wald}(\mu, \lambda) = \left( \frac{\lambda}{2\pi t^3} \right)^{1/2} \exp\left( -\frac{\lambda (t - \mu)^2}{2\mu^2 t} \right)$$
最终呈现给游戏实体的总决策时延为：
$$T_{\text{reaction}} = t_0 + \tau_{\text{decision}}(\text{Context}) \cdot \Phi(\text{Salience}) \cdot \Psi(\text{Attention})$$
* $t_0$：不可压缩的最小神经物理底线（如针对视觉为 $0.15\,\text{s}$）；
* $\tau_{\text{decision}}$：根据任务类型选择的基本均值（SRT 为 $0.05\,\text{s}$，Go/No-Go 为 $0.23\,\text{s}$，使得中值落在 $0.2\,\text{s}$ 与 $0.4\,\text{s}$）；
* $\Phi(\text{Salience})$：显著性缩放算子（距离越远、光照越暗，该值越大）；
* $\Psi(\text{Attention})$：注意力状态乘子（警戒状态下为 $1.0$，涣散状态下为 $1.5 \sim 2.5$）。

---

### 5.2 工业级 C++17 认知反应仿真组件实现

以下工程实现展示了如何在游戏感知系统（Perception System）中构建解耦的、基于事件与注意力衰减的认知时延处理管道。

```cpp
#pragma once

#include <cmath>
#include <random>
#include <algorithm>
#include <chrono>
#include <optional>

/**
 * @brief 认知刺激模式上下文枚举
 */
enum class ECognitiveTaskContext : uint8_t {
    SimpleReaction,   ///< 简单反应：已知目标必定为敌，预先对齐视线 (Baseline ~0.2s)
    GoNoGoDecision,   ///< 识别决策：需鉴别友军/敌军或真伪特征后扣机 (Baseline ~0.4s)
    ComplexChoice     ///< 复杂选择：多目标威胁评估、换弹与掩体转移动态权衡 (>0.4s)
};

/**
 * @brief 智能体注意力警觉度能级
 */
enum class EAttentionState : uint8_t {
    FocusedAlert,     ///< 绝对专注：枪口锁定目标可能出现方向
    RelaxedPatrol,    ///< 常规巡逻：警觉度中等，存在视野扫描间歇
    LapseDistracted   ///< 注意力游离：突遭意外或持续巡逻疲劳导致注意力发散
};

/**
 * @brief 目标感知显著性输入载荷
 */
struct PerceptionStimulusPayload {
    float distanceMeters;          ///< 目标相对于感知源的距离 (米)
    float normalizedContrast;      ///< 目标与背景的对比度/光照因子 [0.0, 1.0]
    float angularDeviationRad;     ///< 目标脱离智能体前向视准心的偏角 (弧度)
    bool  isIdentificationRequired;///< 是否需要执行目标模式识别逻辑
};

/**
 * @brief 智能体认知反应计时器与决策推导核心
 */
class AgentReactionTimeModel {
public:
    AgentReactionTimeModel(uint32_t entropySeed = 1337) 
        : m_rng(entropySeed) {}

    /**
     * @brief 计算从感知到动作触发所需的拟人化物理时延
     * @param stimulus 输入的刺激源度量数据
     * @param attention 智能体当前的心智注意力等级
     * @return float 总反应时间 (单位: 秒)
     */
    float EvaluateReactionLatency(const PerceptionStimulusPayload& stimulus, 
                                  EAttentionState attention) 
    {
        // 1. 确定认知范式上下文
        ECognitiveTaskContext context = stimulus.isIdentificationRequired 
            ? ECognitiveTaskContext::GoNoGoDecision 
            : ECognitiveTaskContext::SimpleReaction;

        // 2. 选取基线延迟中值 (基于认知神经科学先验数据)
        float baseMean = (context == ECognitiveTaskContext::SimpleReaction) ? 0.20f : 0.38f;
        float baseMin  = (context == ECognitiveTaskContext::SimpleReaction) ? 0.15f : 0.30f;

        // 3. 计算感知显著性调节算子 Phi(Salience)
        // 远距离、低对比度、边缘视野会拉长特征辨识延迟
        float distancePenalty = std::clamp((stimulus.distanceMeters - 10.0f) / 70.0f, 0.0f, 1.0f) * 0.15f;
        float contrastPenalty = (1.0f - std::clamp(stimulus.normalizedContrast, 0.0f, 1.0f)) * 0.10f;
        float peripheralPenalty = (stimulus.angularDeviationRad / 3.14159265f) * 0.12f;
        
        float salienceModifier = 1.0f + (distancePenalty + contrastPenalty + peripheralPenalty);

        // 4. 计算注意力状态调节算子 Psi(Attention)
        float attentionModifier = 1.0f;
        switch (attention) {
            case EAttentionState::FocusedAlert:
                attentionModifier = 1.0f;
                break;
            case EAttentionState::RelaxedPatrol:
                attentionModifier = 1.25f;
                break;
            case EAttentionState::LapseDistracted:
                // 模拟突发注意力游离，产生明显长尾延迟
                attentionModifier = GenerateLapseMultiplier();
                break;
        }

        // 5. 应用类 Wald 偏态分布引入神经电信号物理抖动 (Jitter)
        // 使用 Gamma 分布模拟认知漂移过程的首次越界时间
        float targetMean = baseMean * salienceModifier * attentionModifier;
        float dynamicShape = 9.0f; // 维持固定波形偏度
        float dynamicScale = (targetMean - baseMin) / dynamicShape;
        dynamicScale = std::max(dynamicScale, 0.005f);

        std::gamma_distribution<float> gammaDist(dynamicShape, dynamicScale);
        float stochasticLatency = baseMin + gammaDist(m_rng);

        return stochasticLatency;
    }

private:
    /**
     * @brief 模拟注意力游离时的指数衰减惩罚
     */
    float GenerateLapseMultiplier() {
        std::exponential_distribution<float> expDist(1.5f);
        return 1.4f + std::min(expDist(m_rng), 2.0f); // 产生 1.4x ~ 3.4x 严重反应延迟
    }

    std::mt19937 m_rng;
};
```

---

### 5.3 行为树（Behavior Tree）与黑板（Blackboard）集成架构

在商业引擎（如 Unreal Engine 5 行为树框架）中，不可直接在任务节点中使用固定时长的硬编码延迟（Hardcoded Delay）。必须将感知模块识别到的事件，通过反应计时器注入**黑板（Blackboard）**键槽，以保障逻辑可中断性与非阻塞执行。

```
                          [ 感知系统更新 (AIPerception Component) ]
                                            │
                                目标被视觉传感器捕获
                                            │
                                            ▼
                    [ 认知时延评估器 (AgentReactionTimeModel) ]
                                            │
                             计算得到动态延时 Latency 秒
                                            │
                                            ▼
                    [ 写入黑板 (Blackboard Data Mutation) ]
                     - TargetActor = DetectedEnemy
                     - ReactionExpirationTimestamp = Now() + Latency
                     - IsReactionPending = TRUE
                                            │
                                            ▼
                       【行为树执行周期 (Behavior Tree Tick)】
                                            │
                      ┌─────────────────────┴─────────────────────┐
                      ▼                                           ▼
             <Decorator: 是否存在待决反应?>              <Decorator: 反应时延已到?>
             (Is Reaction Pending == true)        (CurrentTime >= ReactionExpirationTimestamp)
                      │                                           │
                      ├─ No  ──► [ 维持常规巡逻/掩蔽 ]           ├─ No  ──► [ 维持预备/注视 ]
                      │                                           │          (保持头部锁定，延迟击发)
                      └─ Yes ──► 检查是否超时                     └─ Yes ──► 触发扣机行为
                                                                             - Clear Reaction Pending
                                                                             - Task: ExecuteFireWeapon()
```

#### 架构要点
1. **中断响应（Interruption Guarantee）**：
   若智能体在 $0.4\,\text{s}$ 的反应窗口期内受到致命伤害或目标脱离视线（LOS 丢失），黑板的 `IsReactionPending` 将立即由感知中断事件清除，阻止射击任务执行，杜绝“死后开枪”或“穿墙开枪”的异常判定。
2. **多通道运动掩盖（Kinematic Masking）**：
   在 $0.2\,\text{s} \sim 0.4\,\text{s}$ 的认知神经传导时延期间，智能体应当播放头部微调或瞳孔/眼动追踪微动画（Saccades），向玩家传递其“正在观察与辨识”的显式生理反馈，使延迟行为具备心理可信度而非静态卡顿。

---

## 6. 工业工程最佳实践与调优法则

1. **以情境为最高准则（Context Is King）**：
   * 严禁在代码库中通篇使用单一硬编码常量（如全量 `0.25f`）。
   * 区分**伏击状态（Ambush）**与**遭遇状态（Surprise Encounter）**：伏击状态属于简单反应时间（$\sim 0.2\,\text{s}$）；遭遇状态必须按识别反应时间计（$\sim 0.4\,\text{s}$）。

2. **听觉线索优先于视觉线索**：
   * 依据生理实验数据，听觉反应速度（$0.16\,\text{s}$）天生快于视觉反应速度（$0.19\,\text{s} \sim 0.22\,\text{s}$）。
   * 当玩家使用高噪音武器（如未消音射击）或暴力破门时，AI 应当以听觉刺激通道触发更短的基线时延。

3. **难度动态伸缩（Dynamic Difficulty Adjustment, DDA）**：
   * 在高难度模式（Hardcore/Nightmare）下，将时延均值压缩至认知极限（如 $0.20\,\text{s}$），降低注意力游离概率；
   * 在休闲难度（Casual/Easy）下，可通过拉高显著性衰减因子与注意力游离权重，将反应时延安全拓宽至 $0.6\,\text{s} \sim 0.8\,\text{s}$，为普通玩家提供充足的交火窗口期与战术决策容错率。

---

## 7. 原始学术文献索引

* **[Donders 69]** Donders, F. C. 1969. *Over de snelheid van psychische processen [On the speed of psychological processes]*. In *Attention and Performance: II*, ed. Koster, W. (Original work published 1868). Amsterdam, the Netherlands: North-Holland.
* **[Kosinski 13]** Kosinski, R. 2013. *A literature review on reaction time*. Clemson, SC: Clemson University.
* **[Laming 68]** Laming, D. R. J. 1968. *Information Theory of Choice-Reaction Times*. London, U.K.: Academic Press.
* **[Luce 86]** Luce, R. D. 1986. *Response Times: Their Role in Inferring Elementary Mental Organization*. New York: Oxford University Press.
* **[Posner 05]** Posner, M. 2005. *Timing the brain: Mental chronometry as a tool in neuroscience*. PLoS Biol 3(2): e51. `doi:10.1371/journal.pbio.0030051`.
