---
type: Reference
title: "第34章 A Simple and Robust Knowledge Representation System"
description: "Game AI Pro 工业级精读：A Simple and Robust Knowledge Representation System。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第34章 A Simple and Robust Knowledge Representation System

> 来源：*Game AI Pro 1: Collected Wisdom of Game AI Professionals*, Chapter 34.  
> 原文作者 / 资源：[A Simple and Robust Knowledge Representation System](http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter34_A_Simple_and_Robust_Knowledge_Representation_System.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷1-GameAIPro1](README.md) ｜ [专栏首页](../README.md)

---

## 1. 架构背景与设计哲学（Design Philosophy & Trade-offs）

在伴侣型人工智能（Companion AI）及具备长生命周期（High Longevity）的非玩家角色（NPC）设计中，决策逻辑的丰富度直接取决于底层知识表示（Knowledge Representation）的深度与组织形式。对于短寿命的消耗型敌对单位，扁平且固定的数据结构足以支撑有限的状态转移；但对于全程伴随玩家、需对环境及社交事件展现连续态度与复杂反应的伴侣角色，决策条件往往高度依赖多层级的动态事实推导。

```
                    ┌─────────────────────────┐
                    │     Sensing / Events    │
                    └────────────┬────────────┘
                                 ▼
┌─────────────────────────────────────────────────────────────┐
│             Dynamic Knowledge Base (Blackboard)             │
│  - Hierarchical Variant Storage (Entity / Property Trees)   │
│  - Set Memberships & Flattened Valence Fallbacks            │
│  - Temporal Decay & Aggregated Appraisal Engine             │
└──────────────┬───────────────────────────────┬──────────────┘
               │ Query & Evaluation            │ Introspection & RPC
               ▼                               ▼
┌─────────────────────────────┐ ┌─────────────────────────────┐
│  Decision Logic: Behavior   │ │ Web Debugger / JSON Export  │
│  Tree / Utility / HTN Logic │ │ Script Interface (Lua/Poco) │
└──────────────┬──────────────┘ └─────────────────────────────┘
               │ Dispatched Actions
               ▼
┌─────────────────────────────┐
│ Execution: AI GameComponent │
└─────────────────────────────┘
```

### 1.1 伴侣 AI 的认知诉求
伴侣 NPC 不仅需要感知实时的空间几何数据（如位置、线速度），更需维持对世界多层面的认知结构：
* 对周围实体角色的态度变化与社交关系沉淀。
* 对离散事件的短期记忆与情绪评估（Appraisal）。
* 对其他代理人认知状态（Knowledge of Others）的间接建模。

若逻辑判定表达为标准条件动作对：
$$\text{Condition}(\mathcal{K}) \implies \text{Action}$$
则随着行为复杂度的非线性上升，知识集 $\mathcal{K}$ 的结构敏捷度直接决定了开发迭代效率。

### 1.2 工业级权衡：迭代效率优先于原生效率
在项目原型及迭代期，硬编码强类型的黑板（Blackboard）系统会引入高昂的维护成本——每次新增认知项均需在原生层修改类接口、重新绑定脚本导出层并更新持久化协议。本系统确立了以下核心工程权衡准则：

| 决策维度 | 传统强类型黑板 (Static Blackboard) | 变体分层黑板 (Variant Hierarchical Blackboard) |
| :--- | :--- | :--- |
| **迭代速度 (Iteration Speed)** | 较低；增删数据项需修改原生 C++ 类与接口声明 | 极高；直接利用动态命名变体存取，无需重新编译 |
| **内存开销 (Memory Footprint)** | 极低；内存紧凑对齐，无虚函数与类型元信息开销 | 适度增加；包含变体类型标签、容器开销与动态类型元数据 |
| **访问效率 (Access Overhead)** | 极高；直接指针偏移与原生内存寻址 | 具有哈希查询开销与变体类型安全转换开销 |
| **脚本集成度 (Script Usability)**| 需针对每个字段编写反射导出胶水代码 | 统一自动化序列化为脚本字典/表（如 Lua Tables） |
| **发布期优化路径** | 结构僵化，后期重构难度高 | 原型期自由迭代，稳定后针对高频访问项针对性固化为原生类型 |

---

## 2. 知识表示的多维分类模型（Knowledge Taxonomy）

为将现实世界的认知模型投射至紧凑且具备表现力的数据结构中，系统将知识划分为以下八大正交抽象：

```
                    ┌───────────────────────────┐
                    │      Knowledge Base       │
                    └──────┬─────────────┬──────┘
                           │             │
        ┌──────────────────┴──┐       ┌──┴──────────────────┐
        ▼                     ▼       ▼                     ▼
┌──────────────┐      ┌──────────────┐┌──────────────┐┌──────────────┐
│ Entity State │      │ Set / Tag    ││ Relationship ││ Events & Mood│
│ (Pos, Vel)   │      │ Membership   ││ & Attitudes  ││ (Appraisal)  │
└──────────────┘      └──────────────┘└──────────────┘└──────────────┘
```

### 2.1 实体属性与存在性（Attributes & Existence）
* **实体属性（Entity Attributes）：** 维护每个代理人感知范围内的实体动态物理参数（例如 $\mathbf{p} \in \mathbb{R}^3, \mathbf{v} \in \mathbb{R}^3$、朝向、当前动画状态）。在黑板中组织为动态变体实体列表。
* **存在性推理（Existence）：** 在战术决策时，首要判定往往是某一类别的实体是否存在（如 $\text{Count}(\mathcal{S}_{\text{enemy}}) > 0$），随后再下钻至具体目标选择逻辑。将存在性抽象为集合势（Cardinality）的逻辑谓词，避免了高开销的每帧全局扫描。

### 2.2 实体分类与动态集合归属（Classification & Set Membership）
单一继承式的实体类型划分无法满足复杂叙事与交互场景的需求。例如经典案例中，“罗密欧（Romeo）”在不同交互情境下同时具备多重身份特征：
$$\text{Romeo} \in \{\text{Male}, \text{Human}, \text{Montague}, \text{Lover}\}$$

* **集合表示：** 系统使用包含实体唯一标识符（Entity ID）的动态数组表示命名集合（Named Sets）。
* **多分类匹配：** 代理人对实体的反应逻辑依赖其实际隶属的集合标签。由于集合具备动态增删特性，实体可随剧情发展从单一分类动态扩展至多元分类。

### 2.3 关系、态度与扁平化层级降级（Relationships & Attitudes）
关系被建模为包含**正负效价值（Valence）**的集合系统。态度（Attitudes）在实现上等价于对静态物件或环境事件的关系。

为了在行为树（Behavior Trees）或效用评估中实现快速上下文回退，实体存储其所属集合时采用从具体到泛化的扁平化层级序列（Flattened Hierarchy Array）：

```
[更具体的社交关系] ───────────────> [宽泛阵营归属] ───────────────> [基础物种归属]
   Lover (Valence: +1.0)           Montague (Valence: -0.8)          Human (Fallback)
```

**决策求值顺序与回退机制：**
算法自左至右单向遍历实体隶属的集合序列：
1. **优先特化反应：** 若命中特异性极高的集合（如 `Lover`），立即触发亲昵表现逻辑，阻断后续通用逻辑；
2. **阵营次级反应：** 若未命中，则回退至阵营级集合（如 `Montague`），触发对立防御逻辑；
3. **全局兜底回退：** 若均未命中，最终降级至物种级通用逻辑（如 `Human`）。

### 2.4 他人知识、语义知识与事件记忆（Knowledge of Others, Semantics, Events）
* **他人知识模型（Knowledge of Others）：** 在多代理人协同与同伴 AI 中，代理人需推演周围伙伴的已知情报（心理理论 Theory of Mind）。黑板中通过将同伴实体的引用加入专用集合，支持代理人间接检索目标黑板的视距与认知缓存。
* **语义推断（Semantic Knowledge）：** 即“is-a”与“like-a”的本体论推导（Ontology Inference）。游戏实践表明，绝大多数运行时决策可通过扁平化集合与标签解决，完整语义网推导在标准运行时过于沉重，可作为离线数据生成或扩展实验。
* **事件记忆与情绪评估（Events & Appraisal）：** 记录具有时间相关性的瞬态感知事件（如听到手雷落地或目睹同伴被攻击）。通过评估器（Appraisal System）对事件集合进行聚合积分，映射为连续的情绪变量（如疲劳度、战斗应激指数）。

---

## 3. 核心架构与变体黑板实现（Variant-Based Hierarchical Blackboard Architecture）

系统采用基于组件（Component-Based Architecture）的软件设计，实体以 `GameObject` 作为承载容器，组合扁平化的 `GameComponent` 派生实例。

```
┌────────────────────────────────────────────────────────┐
│                      GameObject                        │
│ ┌────────────────────────────────────────────────────┐ │
│ │ Components Flat List                               │ │
│ │  ├─ VisualMeshComponent                            │ │
│ │  ├─ PhysicsComponent                               │ │
│ │  └─ AIComponent ─────────────────────────────────┐ │ │
│ └──────────────────────────────────────────────────│─┘ │
└────────────────────────────────────────────────────┼───┘
                                                     ▼
┌────────────────────────────────────────────────────────┐
│                     AIComponent                        │
│ ┌────────────────────────────────────────────────────┐ │
│ │ Blackboard Instance                                │ │
│ │  ├─ Expiry Update Queue                            │ │
│ │  ├─ Hierarchical Variants Tree                     │ │
│ │  │    ├─ "Entities" -> [ID: 101, 102, ...]         │ │
│ │  │    ├─ "Events"   -> [Timestamped Variants]      │ │
│ │  │    └─ "Self"     -> [Custom Properties]         │ │
│ │  └─ Named External Blackboards Access Router       │ │
│ ├────────────────────────────────────────────────────┤ │
│ │ Behavior Tree Instance                             │ │
│ │  └─ Execute(AIComponent*, DeltaTime)               │ │
│ └────────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────┘
```

### 3.1 变体系统（Variant System）的设计与三项核心扩展
基础数据载体基于 POCO C++ 库的动态弱类型对象 `Poco::Dynamic::Var`（或其早期变种 `DynamicAny`），并在此基础上实施了三项工业级扩展：

1. **分层容器（Hierarchical Variants）：**
   单个变体对象既可持有标量原生类型，亦可持有一个由变体构成的动态数组（`std::vector<Variant>`），构成可递归遍历的树状属性结构。
2. **属性命名寻址（Named Access）：**
   向变体层级注入命名属性，支持以显式字符串键值定位节点，无缝映射外部脚本与数据驱动配置。
3. **时间驱动的自动衰减与过期机制（Temporal Decay & Expiry）：**
   赋予每个变体一个浮点型的生存倒计时 $t_{\text{expiry}}$：
   * $t_{\text{expiry}} = 0$：表示该知识为持久记忆（永久生效，不参与老化）；
   * $t_{\text{expiry}} > 0$：表示该知识为瞬态感知，每帧依时钟步长衰减，降为 0 时自动自黑板中剔除。

### 3.2 变体数据结构与黑板访问接口 C++ 规范实现

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cassert>
#include <Poco/Dynamic/Var.h>

class VariantNode {
public:
    using NodeList = std::vector<std::shared_ptr<VariantNode>>;

    std::string name;
    Poco::Dynamic::Var value;
    float expiryTime{0.0f}; // 0.0 表示不失效
    NodeList children;

    VariantNode() = default;
    VariantNode(std::string nodeName, const Poco::Dynamic::Var& val, float expiry = 0.0f)
        : name(std::move(nodeName)), value(val), expiryTime(expiry) {}

    [[nodiscard]] bool IsArray() const {
        return !children.empty();
    }

    [[nodiscard]] size_t Count() const {
        return children.size();
    }

    void AddChild(const std::shared_ptr<VariantNode>& child) {
        children.push_back(child);
    }

    // 递归进行生命周期衰减判定
    bool UpdateExpiry(float deltaTime) {
        if (expiryTime > 0.0f) {
            expiryTime -= deltaTime;
            if (expiryTime <= 0.0f) {
                return true; // 需被父节点剪枝
            }
        }

        // 迭代子节点并剔除过期项
        for (auto it = children.begin(); it != children.end(); ) {
            if ((*it)->UpdateExpiry(deltaTime)) {
                it = children.erase(it);
            } else {
                ++it;
            }
        }
        return false;
    }
};

class Blackboard {
private:
    std::unordered_map<std::string, std::shared_ptr<VariantNode>> m_RootStorage;

public:
    // 模板化属性读取：支持强类型转换
    template <typename T>
    T Get(const std::string& key) const {
        auto it = m_RootStorage.find(key);
        if (it == m_RootStorage.end()) {
            throw std::runtime_error("Blackboard key missing: " + key);
        }
        try {
            return it->second->value.extract<T>();
        } catch (const Poco::Exception& e) {
            // 原型阶段通过异常捕获不匹配，正式生产期建议替换为断言机制
            assert(false && "Type mismatch during Blackboard access!");
            throw;
        }
    }

    // 模板化属性写入
    template <typename T>
    void Set(const std::string& key, const T& val, float expiry = 0.0f) {
        auto it = m_RootStorage.find(key);
        if (it != m_RootStorage.end()) {
            it->second->value = Poco::Dynamic::Var(val);
            it->second->expiryTime = expiry;
        } else {
            m_RootStorage[key] = std::make_shared<VariantNode>(key, Poco::Dynamic::Var(val), expiry);
        }
    }

    std::shared_ptr<VariantNode> GetNode(const std::string& key) {
        auto it = m_RootStorage.find(key);
        return (it != m_RootStorage.end()) ? it->second : nullptr;
    }

    void Tick(float deltaTime) {
        for (auto it = m_RootStorage.begin(); it != m_RootStorage.end(); ) {
            if (it->second->UpdateExpiry(deltaTime)) {
                it = m_RootStorage.erase(it);
            } else {
                ++it;
            }
        }
    }
};
```

---

## 4. 运行期生命周期与执行管线（Execution Pipeline, Sensing & Expiry Management）

### 4.1 执行管线与职责解耦
在组件层（`AIComponent`）中，系统贯彻了**决策逻辑与执行逻辑的严格分离**：

```
[AIComponent::Update(deltaTime)]
         │
         ├───> 1. Blackboard::Tick(deltaTime)
         │        ├── 扣减所有非零 expiryTime: t ← t - Δt
         │        └── 自动移除过期实体或失效属性 (Pruning)
         │
         ├───> 2. AppraisalSystem::Evaluate(deltaTime)
         │        └── 聚合未处理的 Events，折算角色 Mood / Fatigue
         │
         ├───> 3. BehaviorTree::Execute(AIComponent*, deltaTime)
         │        ├── 只读/只写访问 Blackboard 节点状态
         │        └── 生成待执行动作命令并加入 AIComponent 队列
         │
         └───> 4. AIComponent::ExecuteActions(deltaTime)
                  └── 物理推进、移动导向行为（Steering）、播放骨骼动画
```

* **行为树（Behavior Trees）：** 只负责做决策。行为树节点（如条件判断、序列节点、装饰节点）通过 `blackboard<T>[key]` 接口查询当前上下文，并在成功决策后向 `AIComponent` 派发执行命令，树节点内部不直接操作底层游戏驱动逻辑。
* **执行组件（AIComponent）：** 负责调度底层物理引擎、导向行为（Steering Behaviors）、寻路导航网格（NavMesh）追踪及骨骼动画状态机。

### 4.2 数据注入途径：数据驱动初始化 vs 运行时感知（Sensing）
黑板中的数据具有两条注入通路：
1. **静态配置注入：** 实体在装配阶段，引擎解析其关联的 XML 模板（包含实体分类、预设集合隶属、基础人格特征标签），将静态认知灌入黑板。
2. **感知系统动态注入（Sensory System Pipeline）：** 空间感知循环在运行时持续刷新黑板：

```
                    ┌─────────────────────────┐
                    │    Perception Engine    │
                    │ (Vision/Audition Scans) │
                    └────────────┬────────────┘
                                 │
                                 ▼
                     /───────────────────────\
                    <  Is Entity in Blackboard? >
                     \───────────────────────/
                                 │
                 Yes ┌───────────┴───────────┐ No
                     ▼                       ▼
       ┌────────────────────────┐  ┌────────────────────────┐
       │ Refresh Expiry Time    │  │ Create New VariantNode │
       │ Update Pos/Vel Vectors │  │ Set Category Sets      │
       └────────────────────────┘  │ Set Expiry Time         │
                                   └────────────────────────┘
```

### 4.3 多粒度时间衰减数学模型（Temporal Decay Formulation）
黑板衰减允许在两个正交粒度上进行时间管理：
* **实体宏观级（Aggregate Entity Level）：** 若实体脱离感知视野超过生命周期阀值 $T_{\text{max}}$，其对应的整个变体节点树连同子属性将被整体抹除。
* **属性微观级（Attribute Fine-grained Level）：** 若实体仍在视野内，但其某个瞬态状态（如闪避预测速度向量）仅在短时间内有效，可仅对该属性变体赋予微小的 $t_{\text{expiry}}$。

衰减计算公式如下：
$$t_{\text{remaining}}^{(k)} \leftarrow t_{\text{remaining}}^{(k)} - \Delta t$$
$$\text{Status}(k) = \begin{cases} 
\text{Active}, & \text{if } t_{\text{remaining}}^{(k)} > 0 \lor t_{\text{initial}}^{(k)} = 0 \\ 
\text{Pruned}, & \text{if } t_{\text{remaining}}^{(k)} \le 0 \land t_{\text{initial}}^{(k)} \ne 0 
\end{cases}$$

### 4.4 事件接入与情感评估（MessageManager & Appraisal Engine）
事件经由引擎全局事件中心 `MessageManager` 捕获后，调用 `AddEventMemory` 方法打包为变体并压入黑板中的 `Events` 集合。
评估器（`Appraisal`）负责实现情感模型与认知评价（如战意激化、战斗疲劳）：

```cpp
void AIComponent::AppraiseEvents(float deltaTime) {
    auto eventsNode = m_Blackboard.GetNode("Events");
    if (!eventsNode || !eventsNode->IsArray()) return;

    float battleStressDelta = 0.0f;
    for (const auto& eventVar : eventsNode->children) {
        std::string type = eventVar->value.extract<std::string>();
        if (type == "EXPLOSION_NEARBY") {
            battleStressDelta += 0.25f;
        } else if (type == "ALLY_DOWN") {
            battleStressDelta += 0.50f;
        }
    }

    // 更新黑板中的疲劳与情绪标量
    float currentStress = m_Blackboard.Get<float>("BattleStress");
    currentStress = std::clamp(currentStress + battleStressDelta - (0.05f * deltaTime), 0.0f, 1.0f);
    m_Blackboard.Set<float>("BattleStress", currentStress);
}
```

---

## 5. 脚本绑定、序列化与远程调试（Scripting, Serialization & Tooling）

分层变体架构对工具链与可观测性具有显著的结构优势。由于数据天然携带字符串键名与树状层级关系，系统可极低成本地打通调试管线。

```
┌────────────────────────────────────────────────────────┐
│                   Hierarchical Blackboard              │
└───────────────────────────┬────────────────────────────┘
                            │
              Recursive Serialize(IStreamVisitor)
                            │
       ┌────────────────────┼────────────────────┐
       ▼                    ▼                    ▼
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  JSON Dump   │     │  Lua Binding │     │ Binary Save  │
│ Web Debugger │     │ Script Logic │     │ Checkpointing│
└──────────────┘     └──────────────┘     └──────────────┘
```

### 5.1 递归序列化设计模式
通过在变体根节点应用访问者模式（Visitor Pattern）或直接递归，节点能将自身及下游所有子节点自动导出为任意目标数据流：

```cpp
void SerializeNodeToJSON(const std::shared_ptr<VariantNode>& node, std::ostream& os) {
    os << "{\"" << node->name << "\":";
    if (node->IsArray()) {
        os << "[";
        for (size_t i = 0; i < node->children.size(); ++i) {
            SerializeNodeToJSON(node->children[i], os);
            if (i + 1 < node->children.size()) os << ",";
        }
        os << "]";
    } else {
        // 利用 POCO 变体原生转化为字符串
        os << "\"" << node->value.convert<std::string>() << "\"";
    }
    os << "}";
}
```

### 5.2 嵌入式 Web 远程动态调试
伴侣 AI 在大范围动态世界漫游时，在客户端屏幕上绘制海量调试文字（Screen Space Overlay）易导致严重视觉遮挡与性能下降。
本系统将递归生成的完整 JSON 数据挂载至游戏内嵌入式 HTTP Web 服务的 REST 端点。开发人员或关卡策划可直接在第二屏幕的浏览器中，对任意选中的代理人黑板进行实时观测：
* 动态折叠展开实体的认知属性层级；
* 实时图表监控疲劳与压力曲线变化；
* 监视各个瞬态感知的 $t_{\text{expiry}}$ 倒计时进度条。

### 5.3 脚本语言（Lua）表映射
变体分层结构与弱类型脚本语言（如 Lua）的 Table 机制具有一一对应的同构特性：
* 标量变体通过类型感知直接导出为 `number`、`string` 或 `boolean`；
* 数组型变体自动绑定为以 1 为基地址的连续索引表（Dense Tables）；
* 命名子变体数组映射为以键值寻址的字典表（Key-Value Dictionaries）。
脚本编写者可在行为树条件脚本中直接使用：
```lua
if Blackboard.Entities[targetID].Valence < 0.0 then
    -- 执行敌对防御逻辑
end
```

---

## 6. 工业级实践评估与生产优化（Production Considerations & Performance Optimization）

尽管基于动态变体的黑板大幅缩短了玩法探索期的代码重构周期，但在项目进入发布期（Shipping Phase）时，必须针对其固有缺陷进行系统性加固与优化。

### 6.1 异常处理 vs 静态断言策略
* **原型开发期（Prototyping）：** 允许在类型不匹配或键名缺失时抛出 C++ 标准异常或日志警报，使策划人员编写错误脚本或缺少字段时不致导致游戏直接崩溃；
* **正式发布期（Shipping）：** 必须关闭运行时重型异常抛出机制。类型转换与键查找逻辑应严格改用基于宏的快速断言（`assert`），杜绝因不合理的字符串隐式转换引发的额外堆开销与分支预测惩罚。

### 6.2 性能瓶颈与落地优化路线（Optimization Roadmap）

```
[原型设计与系统验证期] (Rapid Prototyping)
  └─ 完全依赖 POCO DynamicAny 命名分层变体
  └─ 频繁的哈希表字符串查寻 (std::unordered_map<std::string, ...>)
  └─ 基于堆分配的共享指针 (std::shared_ptr<VariantNode>)
                 │
                 ▼ 性能瓶颈分析 (Profiling & Memory Footprint)
                 │
[项目交付与发布构建期] (Final Shipping Build)
  ├─ 1. 键名哈希化：使用编译期字符串散列 (Compile-Time String Hash, 如 FNV-1a)
  ├─ 2. 内存局部性优化：使用连续内存扁平缓存区（Fixed Buffer / Flat Trees）
  └─ 3. 高频字段硬编码化：将 Position/Velocity 等高频访问项迁移为固定偏移成员
```

1. **键名散列化（Key Hashing）：**
   将所有运行时字符串寻址改为 32 位或 64 位整数哈希标识符（MurmurHash3 或 FNV-1a）。黑板查询接口由 `blackboard["Position"]` 重构为：
   ```cpp
   constexpr uint32_t HASH_POSITION = HashRuntime("Position");
   blackboard.Get<Vector3>(HASH_POSITION);
   ```
2. **内存池化与连续存储（Data-Oriented Cache-Friendly Layout）：**
   动态分配的大量 `std::shared_ptr<VariantNode>` 会引发内存碎片并导致 CPU L1/L2 缓存缺失（Cache Miss）。在优化阶段，可将黑板内部重构为紧凑的扁平数组（Flat Tree），所有子节点索引均使用偏移量而非堆指针。
3. **关键数据分离（Hot/Cold Splitting）：**
   对于每秒被行为树读取数百次的超高频空间数据（如自身与注视目标的位置、朝向），从变体字典中剥离，在 `AIComponent` 内部回归为连续内存的原生字段，而将变体黑板专注于维护长周期的社交关系、态度效价和事件队列等低频离散知识。

通过此种架构演进策略，团队在前期能以脚本级的灵活性打磨复杂伴侣 AI 的叙事与交互深度，在后期则可通过机械化的数据规整保障 AAA 级商业引擎的每帧帧率预算。
