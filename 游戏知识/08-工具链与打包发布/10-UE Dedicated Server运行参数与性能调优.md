# UE Dedicated Server 运行参数与性能调优

> 以本机 UE5.8 源码已核对的网络驱动与引擎循环事实为边界，串起服务器帧率、Tick 预算、带宽、连接上限、网络仿真与 DDoS 防护的运行调优方法。

## 元数据

- 版本基线：UE5.8.0 / CL55116800 / ++UE5+Release-5.8
- 适用范围：Dedicated Server 运行期的帧率、Tick、带宽、连接数、网络仿真与连接保护的参数选择与调优；Win64/Linux 服务器进程。
- 事实边界：参数名与默认值以本机 UE5.8 指定源码为证据；项目 Target、地图、端口、连接数和具体预算数字均为项目现场事实，本文只给方法。
- 事实边界：本文不把未核对的 CVar 名称写成引擎既有事实；需要确认某个开关时，先在本机源码或目标构建帮助中核对。
- 官方参考：https://dev.epicgames.com/documentation/en-us/unreal-engine
- 最后更新：2026-08-07

## 概述

Dedicated Server 运行调优的目标不是"把数值调大"，而是建立一套可测量的参数模型：
帧率决定 Tick 与网络更新的节拍，带宽决定单连接能承载的复制量，连接上限决定单实例容量，
网络仿真参数决定测试环境如何逼近真实链路，DDoS 防护决定异常流量下的存活能力。

服务器调优的第一层是"帧率与 Tick"：引擎按帧推进世界与网络，帧率过高浪费 CPU，过低放大延迟。
第二层是"带宽与预算"：每个连接、每个 Actor 的复制都需要预算，预算不足时表现为丢更新、卡顿和抖动。
第三层是"连接与保护"：连接上限、连接速率和 DDoS 防护决定服务器在攻击与尖峰下的行为。
第四层是"仿真与验证"：用延迟、丢包、抖动参数在测试环境复现真实网络，验证调优结果。

调优必须与观测配套：先记录基线指标，再改参数，再对比指标，而不是凭感觉反复重启。
任何参数调整都要记录构建 ID、引擎基线、地图、人口、硬件和网络条件。
调优结论只在目标平台和目标配置上成立，Win64 与 Linux、Development 与 Shipping 都应单独验证。

## 1. 事实边界与源码证据

本文只把以下本机 UE5.8 文件作为版本敏感事实来源。

| 编号 | 本机源码路径 | 核对重点 |
| --- | --- | --- |
| 1 | `C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\Engine\Private\NetDriver.cpp` | `MaxClientRate(15000)` 默认值、`SetNetServerMaxTickRate`、`InitBase`、DDoS 初始化与 `GetNetServerMaxTickRate` 关联 |
| 2 | `C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\Engine\Classes\Engine\NetDriver.h` | `NetServerMaxTickRate` 属性与 `Get/SetNetServerMaxTickRate` 访问器（5.3 起变量将私有化） |
| 3 | `C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\Engine\Private\NetConnection.cpp` | `PacketSimulationSettings` 的 PktLag/PktLagMin/PktLagMax/PktLagVariance/PktJitter/PktLoss/PktOrder 仿真分支 |
| 4 | `C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\Engine\Private\GameEngine.cpp` | `bUseFixedFrameRate = false`、`FixedFrameRate = 30.f` 的默认值 |
| 5 | `C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\Engine\Private\World.cpp` | `UWorld::ServerTravel`、`GameMode->CanServerTravel/ProcessServerTravel`、`NetDriver->ServerTravelPause` |
| 6 | `C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\Runtime\ReplicationGraph\Source\Private\ReplicationGraph.cpp` | ReplicationGraph 实现入口（调优关联阅读） |

行号只作为本机快照的导航提示，符号与文件路径才是长期证据。
本文不声称项目已经拥有某个实际参数值；所有 `<...>` 数值都是占位符。

## 2. 核心概念表

| 概念 | 英文/参数 | 作用 | 本机锚点 | 常见误区 |
| --- | --- | --- | --- | --- |
| 服务器最大 Tick 率 | `NetServerMaxTickRate` | 限制服务器每帧网络/世界更新的最大速率 | `NetDriver.cpp`、`NetDriver.h` | 以为越大越好，忽略 CPU 与带宽 |
| 固定帧率 | `bUseFixedFrameRate` / `FixedFrameRate` | 以固定频率推进帧循环，默认关闭、默认 30 | `GameEngine.cpp` | 以为固定帧率一定适合所有玩法 |
| 连接带宽上限 | `MaxClientRate` | 单连接每秒最大发送字节数，默认 15000 | `NetDriver.cpp` | 以为只调这一个值就能解决卡顿 |
| 连接数 | 连接上限/租约 | 单实例可承载的连接与端口 | 项目配置 | 把连接数当成 CCU，忽略端口与资源 |
| 网络仿真 | `PktLag`/`PktLoss`/`PktJitter`/`PktOrder` 等 | 模拟延迟、丢包、抖动与乱序 | `NetConnection.cpp` | 只在客户端测，不在服务器测 |
| DDoS 防护 | NetDriver DDoS 逻辑 | 限制异常收包速率与连接成本 | `NetDriver.cpp` | 以为防火墙能替代进程内防护 |
| 旅行暂停 | `ServerTravelPause` | 服务端换图前暂停/等待的窗口 | `World.cpp`、`NetDriver` | 忽略换图对 Tick 与连接的冲击 |

## 3. 服务器帧率与 Tick

### 3.1 帧率决定什么

服务器帧循环每帧执行世界 Tick、网络收包与复制输出。
帧率上限越高，单帧时间越短，网络输出节拍越细，但 CPU 成本越高。
帧率上限越低，单帧时间越长，输入延迟与复制间隔放大，但 CPU 占用更低。

本机 `GameEngine.cpp` 中 `bUseFixedFrameRate` 默认 `false`、`FixedFrameRate` 默认 `30.f`。
这意味着引擎默认不强制固定帧率；服务器实际帧率取决于机器负载与参数。
生产服务器通常需要明确选择一个帧率策略：固定帧率保证行为可预测，可变帧率在低负载时更省 CPU。

```text
策略 A：固定帧率（确定性优先）
  bUseFixedFrameRate = true
  FixedFrameRate = <target>

策略 B：上限帧率（资源优先）
  NetServerMaxTickRate = <tick-limit>
  帧循环按上限运行，低负载自然降频
```

两种策略不是二选一的简单开关，`NetServerMaxTickRate` 与固定帧率作用在不同层。
`NetServerMaxTickRate` 限制网络驱动的更新节拍，固定帧率限制引擎帧循环本身。
具体到某个项目的正确组合，必须以实际玩法、硬件与测量为准。

### 3.2 NetServerMaxTickRate 的本机事实

本机 `NetDriver.h` 中 `NetServerMaxTickRate` 是 `int32` 属性，注释说明从 5.3 起变量将私有化，
读取应使用 `GetNetServerMaxTickRate()`，修改应使用 `SetNetServerMaxTickRate()`。
本机 `NetDriver.cpp` 的 `SetNetServerMaxTickRate` 在值变化时更新内部状态并广播变化委托。
本机 `InitBase` 中 DDoS 初始化使用 `FMath::Clamp(GetNetServerMaxTickRate(), 1, 1000)`，
说明服务器最大 Tick 率同时参与 DDoS 防护的速率估算。

```cpp
// 方案示意：运行期修改 Tick 率（示例 API 来自本机已核对符号）。
if (UNetDriver* Driver = World->GetNetDriver())
{
    Driver->SetNetServerMaxTickRate(<new-rate>);
    UE_LOG(LogTemp, Log, TEXT("Server tick rate -> %d"), Driver->GetNetServerMaxTickRate());
}
```

不要把 `NetServerMaxTickRate` 当成"客户端帧率"或"世界 Tick 频率"。
它限制的是网络驱动层面的更新节拍，世界 Tick 由帧循环驱动。

### 3.3 Tick 预算与长帧

服务器 Tick 预算应拆成玩法模拟、网络收包、复制准备、网络发包与观测开销。
平均帧率不能代表体验，P95/P99 与最长帧窗口才是卡顿来源。
长帧诊断要记录帧号、DeltaTime、活跃连接、收包数、发包数与待处理队列。

```text
每帧预算 = 玩法模拟 + 网络收包 + 控制消息 + 复制准备 + 网络发包 + 观测开销

若超预算：
1. 记录长帧上下文（帧号、连接数、消息数、耗时分解）；
2. 区分 CPU、锁等待、I/O、分配与网络队列；
3. 先保护权威规则与连接状态；
4. 再按策略降低非关键观测或装饰性工作。
```

## 4. 带宽与复制预算

### 4.1 MaxClientRate 默认值

本机 `NetDriver.cpp` 构造函数中 `MaxClientRate(15000)` 表明单连接默认带宽上限为 15000 字节/秒。
这个默认值只是引擎基线，不是任何项目的推荐值。
FPS、动作游戏与 MMO 的每连接带宽需求差异很大，必须以目标玩法实测。

| 玩法类型 | 单连接带宽量级（示意） | 主要消耗 |
| --- | --- | --- |
| 小型竞技（4-8 人） | 每连接 8-30 KB/s | 移动、射击、技能、命中判定 |
| 中型合作（8-32 人） | 每连接 5-20 KB/s | 移动、AI、事件、状态 |
| 大型 MMO（50+ 人） | 每连接 2-10 KB/s | 兴趣管理内的状态与事件 |

数值是量级示意，不是引擎默认值，也不是项目结论。
带宽预算必须与兴趣管理（ReplicationGraph 或经典相关性）一起设计：
先决定"谁能看到谁"，再决定"每连接发多少"。

### 4.2 带宽不足的症状

| 症状 | 可能原因 | 优先检查 |
| --- | --- | --- |
| 远处角色瞬移 | 相关性或复制频率不足 | ReplicationGraph 配置、`NetUpdateFrequency` |
| 命中判定不一致 | 高优先级事件被低优先级淹没 | 优先级、可靠队列、带宽预算 |
| 玩家状态回跳 | 预测与权威冲突 + 带宽不足 | 移动模型、复制频率、延迟 |
| 全员卡顿 | 服务器带宽或 CPU 打满 | Tick 时间、总带宽、连接数 |

### 4.3 复制预算方法

复制预算不是单参数调优，而是系统性分配：

1. 按玩法对象分级：核心角色、子弹、装饰物、UI 数据各有预算等级。
2. 按连接分级：对局内连接与观战连接的预算不同。
3. 按频率分级：属性变化频率、`NetUpdateFrequency` 与 `NetPriority` 配合。
4. 按兴趣分级：ReplicationGraph 网格、距离、可见性与团队关系。
5. 用测量闭环：记录每连接字节、每对象更新数与拒绝数。

```text
预算分配示意（项目方案，不是引擎默认）：
  核心玩家属性   -> 每连接 40% 预算，高优先级
  战斗事件       -> 每连接 30% 预算，可靠优先
  移动同步       -> 每连接 20% 预算，不可靠高频
  装饰与 UI      -> 每连接 10% 预算，低优先级
```

## 5. 连接上限与端口

### 5.1 连接与 CCU 的区分

连接数（connection）是网络层承载，CCU（并发在线用户）是业务层承载。
一个用户可能建立多个连接（断线重连、多端、观战），一个连接也可能承载多个会话状态。
容量规划必须先定义"单实例最大连接数"与"单实例最大 CCU"两个指标。

连接上限受端口、内存、带宽、Tick 时间与业务状态共同约束。
只调大连接数而不调预算，只是把问题从"连不上"变成"连上后卡死"。

### 5.2 端口与监听

服务器监听端口、查询端口、管理端口与观测端口应明确分离。
生产环境不建议依赖"默认值碰巧可用"，每个端口要有租约、校验与冲突检测。
同一物理机的多实例部署要使用端口池与原子分配。

```powershell
# 验证命令示意：只读检查端口监听与占用。
Get-NetTCPConnection -State Listen -LocalPort <port>
Test-NetConnection -ComputerName <host> -Port <port>
```

UDP 服务不能只用 TCP 命令验证，应结合协议层探测与服务器日志。

## 6. 网络仿真参数

本机 `NetConnection.cpp` 的包仿真逻辑读取 `PacketSimulationSettings` 中的字段：
`PktLag`、`PktLagMin`、`PktLagMax`、`PktLagVariance`、`PktJitter`、`PktLoss`、`PktOrder`。
这些字段用于在测试环境模拟真实网络条件，本机代码按不同组合选择延迟、丢包与乱序分支。

| 参数 | 本机语义（核对结论） | 典型用途 |
| --- | --- | --- |
| `PktLag` | 固定附加延迟（毫秒） | 模拟固定 RTT 的基线延迟 |
| `PktLagMin`/`PktLagMax` | 延迟区间（毫秒） | 模拟波动延迟 |
| `PktLagVariance` | 延迟方差 | 模拟抖动范围 |
| `PktJitter` | 抖动叠加 | 模拟网络抖动 |
| `PktLoss` | 丢包百分比 | 模拟丢包 |
| `PktOrder` | 乱序开关 | 模拟包乱序 |

这些参数的读取位置与分支逻辑已在本机核对，但具体命令行入口、ini 键名与目标构建支持情况
必须在本机目标、项目配置与构建帮助中再次确认，不能从本文推断。
网络仿真应同时覆盖服务器侧与客户端侧，验证调优在真实链路下的表现。

## 7. DDoS 防护与连接保护

本机 `NetDriver.cpp` 的 `InitBase` 中，DDoS 初始化使用服务器最大 Tick 率做速率估算。
这表明 UE 网络驱动自带进程内 DDoS 防护逻辑，与边缘防护（防火墙、网关）协同工作。
进程内防护关注异常收包速率、连接成本与全局流量，边缘防护关注放大攻击与带宽清洗。

```text
防护分层示意：
  边缘层：ACL、DDoS 清洗、带宽限速
  接入层：连接速率限制、来源 IP 限制、ticket 校验
  进程层：NetDriver DDoS、连接上限、收包限速、登录成本控制
  业务层：PreLogin 校验、账号状态、重试退避
```

调优 DDoS 相关参数时，先在小流量环境验证拒绝路径与告警，再应用到生产。
防护参数不能以牺牲正常连接为代价，所有限速都要可观测、可回滚。

## 8. 调优方法论

### 8.1 六步闭环

```mermaid
flowchart LR
    A[定义目标] --> B[采集基线]
    B --> C[定位瓶颈]
    C --> D[修改参数]
    D --> E[复测对比]
    E --> F[记录固化]
    F --> A
```

1. 定义目标：明确玩法类型、目标人口、帧率/延迟/带宽预算与通过标准。
2. 采集基线：在目标硬件上跑空房间、小规模、满房间与尖峰场景。
3. 定位瓶颈：用 Tick 时间分解、连接字节、CPU/内存/网络曲线定位瓶颈层。
4. 修改参数：一次只改一个变量，记录旧值、新值与理由。
5. 复测对比：同一场景、同一工具链复测，比较 P50/P95/P99 与资源曲线。
6. 记录固化：参数、证据、结论进入构建记录与发布门禁。

### 8.2 单变量原则

一次只改一个参数，避免多个变量互相掩盖。
例如先固定帧率，再调带宽；先调带宽，再调兴趣管理。
每个参数调整都要有假设："为什么改、预期效果、如何验证"。

### 8.3 场景矩阵

| 场景 | 关注指标 | 调优重点 |
| --- | --- | --- |
| 空房间 | 启动耗时、内存、Tick 空闲成本 | 服务器空闲降频、资源预热 |
| 低人口（2-4） | 手感、延迟、复制延迟 | 帧率与带宽平衡 |
| 中人口（8-16） | Tick P95、带宽分布 | 复制预算与兴趣管理 |
| 满人口（上限） | 资源水位、长帧、丢更新 | 连接上限与预算再分配 |
| 尖峰连接 | 连接建立耗时、拒绝率 | 连接保护与 DDoS 参数 |
| 长稳（Soak） | 内存增长、连接泄漏、延迟漂移 | 资源回收与队列上限 |

## 9. 配置与命令示例

以下示例都是方案示意，`<...>` 必须由项目替换；ini 键名与命令行入口
在使用前必须在本机目标构建、源码与官方文档中核对。

```ini
; 方案示意：项目 Config/DefaultEngine.ini 的网络驱动相关配置。
; 具体 Section、键名与取值以本机目标与官方文档核对为准。
[/Script/OnlineSubsystemUtils.IpNetDriver]
; NetServerMaxTickRate 等属性是否可在此配置，以本机 NetDriver 配置解析为准。
```

```powershell
# 方案示意：运行期查询与修改参数（具体控制台命令名需在本机构建核对）。
# Driver->GetNetServerMaxTickRate() / SetNetServerMaxTickRate() 对应符号已核对。
```

```text
调优记录模板（项目方案）：
  buildId: <build-id>
  engineBaseline: UE5.8.0/CL55116800/++UE5+Release-5.8
  map: <map>
  population: <n>
  hardware: <cpu/mem/network>
  change: <parameter old -> new>
  metric: <tickP95/memPeak/bandwidth before -> after>
  result: <passed/failed + evidence>
```

## 10. 失败路径

| 症状 | 优先检查 | 处置方向 |
| --- | --- | --- |
| 帧率上不去 | CPU 热点、固定帧率、Tick 分解 | 定位热点后降负载或加节点 |
| 带宽打满 | MaxClientRate、复制量、兴趣范围 | 收紧预算与相关性 |
| 连接被拒绝 | 连接上限、DDoS 防护、端口 | 检查拒绝原因与速率参数 |
| 卡顿集中在换图 | ServerTravelPause、地图加载 | 错峰加载与旅行策略 |
| 仿真参数不生效 | 参数入口、目标构建支持 | 核对命令与版本 |
| 调优后更差 | 多变量同时修改、场景不一致 | 回滚到基线逐项复测 |

## 11. 最佳实践

### 实践 1：先定目标再调参

没有目标的调优只是数字游戏。先写清楚玩法、人口、帧率/延迟/带宽预算与通过标准。

### 实践 2：固定帧率与 Tick 率分开决策

`bUseFixedFrameRate`/`FixedFrameRate` 与 `NetServerMaxTickRate` 作用在不同层。
按玩法的确定性需求与资源预算分别选择，并在构建记录中写明理由。

### 实践 3：单变量修改

一次只改一个参数，保留旧值、新值与假设，用同一场景复测。

### 实践 4：带宽预算与兴趣管理一起设计

先决定谁能看到谁，再决定每连接发多少。没有兴趣管理的带宽调优不可持续。

### 实践 5：仿真覆盖服务器与客户端两侧

`PktLag`/`PktLoss`/`PktJitter` 等参数在测试环境复现真实链路，两侧都要验证。

### 实践 6：DDoS 防护分层

边缘防护与进程内防护协同，防护参数可观测、可回滚，不牺牲正常连接。

### 实践 7：观测与调优同步

每个参数调整都带指标证据，调优记录进入构建记录与发布门禁。

### 实践 8：平台分别验证

Win64 与 Linux、Development 与 Shipping 的调优结论不能互相替代。

## 12. 常见问题 FAQ

### Q1：NetServerMaxTickRate 调越大越好吗？

不是。它是网络驱动更新节拍上限，越大 CPU 与带宽成本越高，还可能放大不必要更新。
应根据玩法延迟需求与资源预算选择，并用测量验证。

### Q2：固定帧率和 NetServerMaxTickRate 有什么区别？

固定帧率限制引擎帧循环本身，`NetServerMaxTickRate` 限制网络驱动更新节拍。
二者作用在不同层，可以组合使用，具体组合以项目测量为准。

### Q3：MaxClientRate 默认 15000 够用吗？

默认值只是引擎基线。实际需求由玩法、复制量与兴趣管理决定，必须实测后设置。

### Q4：为什么只调大连接数还是卡？

连接数只是上限，带宽、Tick、内存与业务状态共同决定容量。
先定义单实例连接数与 CCU 两个指标，再逐项调优。

### Q5：网络仿真参数在哪个文件里？

本机 `NetConnection.cpp` 读取 `PacketSimulationSettings` 的 Pkt* 字段。
具体命令行入口与 ini 键名必须在目标构建中核对，不能从本文推断。

### Q6：DDoS 防护只靠防火墙够吗？

不够。边缘防护处理带宽清洗，进程内防护（如 NetDriver DDoS）处理协议层异常。
两层协同并保持可观测。

### Q7：服务器 Tick P95 很高怎么办？

先分解 Tick：玩法、收包、复制准备、发包与观测分别计时。
定位热点后再决定降负载、优化复制或增加节点。

### Q8：调优结论能直接搬到 Linux 吗？

不能。平台、编译器、网络栈与硬件都不同，必须分别在目标平台复测。

## 13. 关联阅读

- [UE Dedicated Server构建烘焙与运行](<09-UE Dedicated Server构建烘焙与运行.md>)
- [UE Dedicated Server启动与监听源码](<../12-引擎源码分析/32-UE Dedicated Server启动与监听源码.md>)
- [ReplicationGraph兴趣管理](../06-网络同步/05-ReplicationGraph兴趣管理.md)
- [网络复制与RPC源码](../12-引擎源码分析/09-网络复制与RPC源码.md)
- [UE Dedicated Server实例生命周期与平台化](<../../游戏服务端/05-UE Dedicated Server平台化/01-UE Dedicated Server实例生命周期与平台化.md>)
- [UE Dedicated Server联机验收与Gauntlet](<../../游戏测试与质量/06-UE Dedicated Server联机验收与Gauntlet.md>)

## 14. 更新日志

| 日期 | 版本 | 更新内容 |
| --- | --- | --- |
| 2026-08-07 | v1.0 | 新增 UE5.8 Dedicated Server 运行参数与性能调优专题；核对 NetDriver/NetConnection/GameEngine/World 源码锚点。 |

本篇首次创建，所有项目数值、ini 键名与命令行入口均为示意，使用前必须在本机目标核对。
