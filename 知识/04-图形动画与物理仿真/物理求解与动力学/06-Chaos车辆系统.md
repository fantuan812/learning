---
type: Concept
title: "06 Chaos 车辆系统（Chaos Vehicles Dynamics & Tuning）"
status: stable
verified: []
maturity: L2
updated: 2026-10-10
---

# 06 Chaos 车辆系统（Chaos Vehicles Dynamics & Tuning）

> 知识成熟度：L2。依据官方公开文档与 API 声明静态核对车辆搭建、动力链、调参和观测边界；没有本机引擎实现、编译、PIE、设备、弱网或性能实测证据。
> 版本基准：主要采用明确带 `application_version=5.6` 的 UE 5.6 文档与 Python API 页面。后文另列 2026-10-10 读取、页面标题标为 UE 5.8 的异步接口与调试命令快照；其中 AsyncInput、AsyncCallback 与调试命令的固定版本 URL 有读取失败记录；Manager 的固定版本页本轮未读取。四项均不视为可长期锁定的 5.8 实现基线，也不反推 5.6 完全相同。
> 适用范围：赛车、开放世界载具及游戏内多轮车辆的 Chaos Vehicles 骨骼网格方案。本文的玩法辅助制动仅用于游戏，不提供真实车辆安全控制或工程认证方案。
> 事实边界：公开 API 的字段、单位和说明是接口证据；不代表读过对应 `.cpp`、构造默认值或已证明热更新/多线程/网络行为。旧版本的本机路径、CL 与“工业级实战代码已核对”声明不作为本轮证据。
> 官方参考：[Chaos Vehicles 官方文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/chaos-vehicles-in-unreal-engine)；本页具体结论采用下文逐项来源，不由总入口代证。
> 最后更新：2026-10-10（校正模型、单位、输入与线程网络边界，保留建车、车型调参、路面切换和故障排查用途）。

## 1. 从“车轮会转”到“车身受到正确的力”

车辆至少有三套容易混淆的表示：美术骨骼决定轮子画在哪、如何转；车身碰撞与质量属性决定刚体如何响应；车轮配置和悬挂查询决定支撑与轮胎作用。动画正常只能证明第一套表示看起来正常，不能证明另外两套正确。

对于本文的普通 Chaos Vehicles，先用一辆固定拓扑的四轮车理解链路，再按同一索引合同扩展更多轮子。它不是“每个可见车轮都必须作为独立动态刚体与车身关节相连”的通用多体车辆框架。

```text
玩家输入/AI意图
  → 车辆输入、当前挡位、发动机状态
  → 发动机输出和传动/驱动分配
  → 每轮接触与悬挂状态、轮胎作用
  → 车身刚体受力/运动
  → 已发布的轮状态与车身状态
  → 轮骨动画、镜头、音效、界面
```

这是学习用因果图，不是经源码核实的逐函数执行顺序。更改轮胎抓地时，车身速度变化还受到车重、挡位、制动、空气阻力和接触状态约束。更改 Mesh 动画也不必然改变物理轮半径或支撑点。

## 2. 最小建车合同：骨架、坐标、单位与资产

### 2.1 先建立可追溯的轮表

[UE 5.6 Vehicle Art Setup](https://dev.epicgames.com/documentation/en-us/unreal-engine/vehicle-art-setup-in-unreal-engine?application_version=5.6) 的基础约定是 X 向前、Z 向上；相应轮子绕 Y 轴滚动、绕 Z 轴转向。四轮最小骨架是根骨加四个轮骨；轮骨放在轮心，轮网格不要因错误权重随别的轮骨变形。它要求以厘米测量轮半径，不能把直径写成半径。

建议在项目中保存一张自己的轮表，例如下面的内容是教学输入，名称与半径均非引擎默认值：

| 索引 | Bone Name | 角色 | 半径 | 转向/驱动/手刹设计意图 |
| --- | --- | --- | --- | --- |
| 0 | wheel_fl | 左前 | 34 cm | 转向；按驱动方案供动力；无手刹 |
| 1 | wheel_fr | 右前 | 34 cm | 同左前 |
| 2 | wheel_rl | 左后 | 34 cm | 不转向；按驱动方案供动力；手刹 |
| 3 | wheel_rr | 右后 | 34 cm | 同左后 |

索引只是项目约定。官方[车辆设置流程](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-set-up-vehicles-in-unreal-engine?application_version=5.6)明确由 Bone Name 与 Wheel Class 配对，不由数组的前两个槽位自动识别“前轮”。把日志、UI、轮材质策略与此表绑定，防止交换轮类后仍给旧索引下指令。

### 2.2 配置顺序与最小成功条件

1. 导入尺寸与轴向正确的 Skeletal Mesh；用实际轮心和轮半径校对轮表。
2. 配置 Physics Asset 的车身碰撞，检查它是否提前顶住地面、轮位是否被车身包围。美术教程中的轮体处理属于该示例的资产流程，不能把“可见轮网格”当成轮地接触算法。
3. 创建前后 Wheel Class，明确转向、制动、手刹及驱动角色；在 Vehicle Movement 的 Wheel Setups 填 Bone Name 和 Wheel Class。
4. 建立 `WheeledVehiclePawn`、Mesh、Physics Asset 与车辆动画实例；官方流程使用 Wheel Controller 驱动轮动画。
5. 设定发动机扭矩曲线和传动；最后接入油门、刹车、转向、手刹的输入入口。配置好 Pawn/Controller 与输入上下文之后才谈“按键没有推动汽车”。

这五步是待实施步骤，本轮没有创建资产或运行引擎。未来的最小观察目标应是：平地静置无持续穿插；逐轮接触与索引相符；左右转向方向正确；松开控制后输入回零；加速、制动、倒车分别能解释其挡位与力的来源。只看到车身向前移动不足以通过这些条件。

### 2.3 不把一套单位覆盖所有字段

| 量 | 本文采用的单位与来源边界 | 易错处 |
| --- | --- | --- |
| 轮半径、资产空间尺寸 | 美术设置按 cm；纸面力学推导先换成 m | 34 cm 应变成 0.34 m；半径/直径错误会影响轮地关系 |
| 车身质量 | 5.6 移动组件的 `mass` 为 kg | 修改碰撞几何与固定质量设置的关系需明确 |
| 发动机/制动扭矩 | API 注释为 N·m | 不是“任意 UE 扭矩函数都直接用此数值”的保证 |
| 发动机转速 | RPM | 不是 rad/s；轮速和引擎转速还隔着传动 |
| 轮转向角 | `MaxSteerAngle` 为度 | 输入范围 ±1 与角度不是同一个量 |
| 转向速度曲线横轴 | 5.6 `steering_curve` 文档为 MPH | 不要把 km/h 横轴无转换导入 |
| `SpringRate` | 5.6 轮 API 标 N/m | 旧文的 N/cm 推荐表不能直接贴到该字段 |
| `SpringPreload` | 页面也标 N/m，但未给实现推导 | 不凭字段名推成力值或通用预压公式；实现待核对 |

单位依据：[5.6 轮 API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/ChaosVehicles/UChaosVehicleWheel?application_version=5.6)、[移动组件](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/ChaosWheeledVehicleMovementComponent?application_version=5.6)、[转向配置](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/VehicleSteeringConfig?application_version=5.6)。转向配置还区分同角、AngleRatio 与 Ackermann；选择它们不能代替对轴距、轮距和实际轮角的检查。

## 3. 质量、重心与惯性：三者分别控制什么

质量回答“同样合力产生多大加速度”；重心位置改变力臂和支撑载荷分布；惯性张量描述绕不同轴改变角速度的难易。降低重心不是减轻质量，增大惯性也不是添加阻尼。若同时修改这三者，转向响应变慢的原因便难以归因。

在所读 5.6 移动组件声明中，车身 `mass` 可固定质量；启用 `enable_center_of_mass_override` 后，`center_of_mass_override` 会覆盖计算重心，并忽略 Mesh 的重心偏移。不要在两个地方各改一次，然后假定偏移必然相加。`inertia_tensor_scale` 则是按车体前、右、上方向缩放惯性。[移动组件字段](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/ChaosWheeledVehicleMovementComponent?application_version=5.6)

[5.6 重心说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/vehicle-center-of-mass-in-unreal-engine?application_version=5.6)给出重量分布影响操控及空中姿态的方向性解释，编辑器 Mass Properties 可辅助观察重心与惯性。它的示例偏移不是所有车型的推荐值；“前重更容易推头、后重更容易甩尾”也应作为该说明的调参线索，与轮胎、制动和转向设置一起判断。

纸面解释：对已经声明的同一刚体，绕重心的力矩为 `τ = r × F`。悬挂施力点变化会改变 `r`；同一方向力矩下，较大的相应转动惯量通常产生较小的角加速度。这里解释的是理想刚体关系，未声称已复现 Chaos 内部离散积分或所有耦合项。

调参时先锁定资产尺度、车重、重心与惯性，再调轮胎和悬挂。翻车还可能来自碰撞顶起、支撑丢失、高速转向过大或网络校正，不能只看“重心高”就固定下移 20–35 cm。

## 4. 发动机、传动、差速器与输入

### 4.1 将发动机曲线与轮端扭矩连起来

[5.6 发动机配置](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/VehicleEngineConfig?application_version=5.6)把 Torque Curve 描述为给定 RPM 下的 0..1 归一化扭矩，乘以 `MaxTorque`。例如教学设定 `MaxTorque = 650 N·m`、某转速处曲线值 0.8，则该曲线给出的满输出参考为 520 N·m。不能仅写一个很大的 MaxTorque 而不检查曲线、RPM 与实际油门。

用于理解传动的理想稳态近似为：

```text
轮端总驱动扭矩 ≈ 发动机扭矩 × 当前挡齿比 × 主减速比 × 传动效率
理想周向力 ≈ 轮端扭矩 / 轮半径（以 m 代入）
```

它忽略换挡过程、发动机/车轮转动动态、离合行为和抓地限制，不是 Chaos 的逐步求解式。[传动配置](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/VehicleTransmissionConfig?application_version=5.6)列出正/倒挡齿比、主减速比、换挡耗时（秒）、上下换挡 RPM、效率及自动挡选项。挡位相同但轮半径不同，力和速度关系也会改变；“空转不走就增大轮半径”会同时改变几何与传动关系，不能作为抓地修复。

保留旧文七挡跑车的配置用途：650 N·m、7500 RPM、主减速比 3.73、七个正挡比 `[3.82, 2.36, 1.68, 1.31, 1.00, 0.79, 0.63]` 可以作为项目自定的起始输入，须另给非空曲线、怠速、换挡阈值和效率。这些是旧文示例值的重新标注，不是默认配置、真车标定或已验证配方。

### 4.2 分流方向不能凭属性名称猜

[5.6 DifferentialConfig](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/VehicleDifferentialConfig?application_version=5.6)的注释写明 `front_rear_split < 0.5` 偏前、`> 0.5` 偏后，仅用于其称为 4W 的类型。因此旧文把 0.3 解释为“前 30%、后 70%”与该注释方向相反；本文不继续把它当可靠配置。此 API 文案还使用 4W，而[同版枚举](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/VehicleDifferential?application_version=5.6)给出 ALL_WHEEL_DRIVE、FRONT_WHEEL_DRIVE、REAR_WHEEL_DRIVE、UNDEFINED。遇到此类名称差异，应核实际版本的映射与四轮扭矩读数，不据旧名字发明新模式。

轮资产的 `AxleType` 也影响驱动判定：未定义时使用 `bAffectedByEngine`；明确轴型时由差速配置决定动力分配，不能再假定修改这个布尔值必然覆盖轴型。接口语义见[5.6 ChaosVehicleWheel](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/ChaosVehicleWheel?application_version=5.6)。

运行期从后驱换到全驱，需要核对该版本提供哪些运行期 setter、哪些配置在创建物理状态时复制。直接赋 `DifferentialSetup` 只能证明游戏侧结构被改写，不能证明当前物理实例已经应用。5.6 绑定页列出分流 setter，但不由此推定所有差速模式都可无条件热切换。为每次调参记录配置版本、实际应用边界及后续轮状态；如果需要重建物理状态，应走项目批准的重建流程，避免凭空添加一个“刷新”调用。

### 4.3 输入适配：释放、换挡和控制权也属于合同

[5.6 基类输入 API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/ChaosVehicleMovementComponent?application_version=5.6)声明油门/刹车 0..1，转向 -1..1，手刹为布尔；目标挡 -1 为倒挡、0 为空挡、正数为前进挡。输入平滑速率、自动倒车、松油门制动等配置会影响“玩家松开了什么”与“车辆正在执行什么”。

下面是项目输入适配伪代码，未编译、未运行，不是引擎源码。`Output` 是交给项目已核实输入入口的数据记录；它不实现网络传输、物理状态重建或引擎内部换挡。

```text
SanitizeAxis(x, lo, hi):
    若 x 非有限数，返回 0
    否则返回 min(hi, max(lo, x))

BuildInput(raw, ownsLocalControl):
    若 ownsLocalControl 为 false：返回 NoLocalWrite
    若 raw.contextActive 为 false：
        返回 Output(throttle=0, brake=0, steer=0, handbrake=false)
    返回 Output(
        throttle=SanitizeAxis(raw.throttle, 0, 1),
        brake=SanitizeAxis(raw.brake, 0, 1),
        steer=SanitizeAxis(raw.steer, -1, 1),
        handbrake=raw.handbrakeHeld)

ChooseLowerForwardGear(currentGear, manualMode, downPressedThisUpdate):
    若 manualMode 且 downPressedThisUpdate 且 currentGear > 1：
        返回 RequestGear(currentGear - 1)
    否则返回 NoGearRequest
```

油门、刹车、转向要覆盖输入完成/取消；手刹按下和释放都要送达。输入上下文丢失时清除本地输入是本文项目策略，不是 API 自动保证。失去本地控制后 `NoLocalWrite` 防止旧控制器持续覆盖别人的输入；服务器自己的超时/接管策略另行定义。

强制降挡在一挡处停止，不顺手降到空挡或倒挡；速度、预计发动机超转保护及切换手动挡的许可仍属于游戏设计。旧示例声明而未实现的 `BeginPlay`、`Tick`、`ForceDownshift` 不能拼成“完整可编译 Pawn”；本节以明确的资产流程和输入合同保留它要解决的问题。

## 5. 轮胎、路面与悬挂：模型解释和引擎字段分开

### 5.1 接触、滑移和可用抓地

驱动扭矩只有通过接触才可能转成车辆牵引。纸面解释可用 `|F_t| ≤ μN` 表示有限摩擦能力，但这不是已核实的 Chaos 联合纵横向摩擦算法。转弯与加速会竞争有限的轮胎能力；车轮离地时，轮子在转不意味着车身获得相同的牵引。

纵向滑移描述轮周向速度与沿行驶方向接触相对速度不一致；侧偏描述轮朝向与相对运动方向的差异。接近静止、倒车、横向速度为零或接触刚切换时，符号、分母保护和阈值必须由具体模型定义，不能直接用一个除以车速的式子处理所有状态。轮地相对速度还应考虑地面运动，不能只使用车身世界速度。

公开轮接口有 `CorneringStiffness`、`FrictionForceMultiplier`、`SideSlipModifier`、`LateralSlipGraph` 及滑移阈值；仅有这些字段不能证明内部采用 Pacejka 魔术公式，也不能证明曲线是分段 Hermite 查表。旧文 B/C/D/E 数值表没有本轮实现依据，不能映射成这些引擎字段。保留的实践问题是如何区分小侧偏响应、峰值抓地与滑动后的控制感，而非强制指定轮胎公式。

`WheelLoadRatio` 的 5.6 文档端点含义为：0 时抓地不随轮载变化，1 时依照压向地面的力；这是游戏模型选项，不能据此保证物理逼真或某车型稳定。[轮字段与载荷说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/ChaosVehicleWheel?application_version=5.6)

### 5.2 路面切换：保留材质身份，避免每帧猜名称

干沥青、湿地、泥地和冰面应作为项目的路面配置键，关联可解释的轮胎策略。旧表 1.00/0.65/0.45/0.20、滚阻数值以及“泥地形成车辙”均不是由 ChaosVehicles 接口保证的物理材质常数；泥地视觉形变还需要自己的地形/特效系统。

公开 API 有轮状态查询、接触材质信息与摩擦乘数 setter；这些可作为适配入口，不需要假设存在 `Wheel->GetHitResult()`。本文的应用策略如下，仍是未运行伪代码：

```text
配置表 SurfaceGrip = {Dry:1.00, Wet:0.70, Mud:0.50, Ice:0.20}
默认 DefaultGrip = 1.00；每个值都是本示例自定的无量纲乘数
每轮状态保存 lastApplied = Unknown

ChooseGrip(contact, surfaceId):
    若 contact 为 false：返回 NoContactChange
    若 SurfaceGrip 包含 surfaceId：返回 SurfaceGrip[surfaceId]
    否则返回 DefaultGrip

ApplyPolicy(wheelIndex, observedStatus):
    若 wheelIndex 无效或状态不属于该车轮：返回 Reject
    desired = ChooseGrip(observedStatus.contact, observedStatus.surfaceId)
    若 desired 为 NoContactChange 或 desired == lastApplied：返回 NoWrite
    发出 RequestGrip(wheelIndex, desired, configRevision)
    只有项目适配层确认已应用后，才更新 lastApplied
```

这避免每帧依赖资产名是否含 `Ice`、未知路面继承上一块冰面的策略值，以及离地时把“无材质”误当干地。它不保证 setter 同步生效、线程安全或自动复制到服务端。官方声明入口见[5.6 WheeledVehicleMovement](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/ChaosWheeledVehicleMovementComponent?application_version=5.6)；运行接口需在自己的版本核实。

### 5.3 悬挂支撑、行程与阻尼

先问查询有没有命中、长度是否在可用行程，再问弹簧参数。轴向、轮心或轮半径错误会让轮子一直悬空或行程饱和；这时增加刚度不能修正几何关系。

理想单自由度教学模型中，以压缩量 `x > 0`、压缩速度 `xdot > 0` 表示压缩，弹簧/阻尼对该坐标的回复作用写作 `-kx - c xdot`；`k` 为 N/m、`c` 为 N·s/m。静态载荷估算 `x ≈ m_s g / k`、临界阻尼尺度 `c_crit = 2√(k m_s)` 只用于检查数量级，`m_s` 是假设分配给该悬挂的簧载质量。它不是把总车重除四后就得到所有场景下真实轮载，也不证明引擎如何从 DampingRatio 得到内部系数。

例如四轮均匀承载、车重 1200 kg、每轮 `m_s=300 kg`、`g=9.81 m/s²`、`k=30000 N/m` 的理想静态模型，压缩量约 0.0981 m。若模型只允许 0.05 m 压缩，便不能同时要求“不触底且保持该理想静态平衡”；需重新审视弹簧、预载、行程和质量分配。这是手算反例，不是推荐用 30000 N/m 或预测游戏中的车高。

5.6 接口有 `SuspensionAxis`、伸/压行程、`SuspensionDampingRatio` 和 `RollbarScaling`。防倾作用改变左右悬挂对车身侧倾的响应和轮载分配；把防倾无限调硬也可能改变单轮越障表现与失去接触的顺序。不能把“防倾更强”写成“高速零侧倾且永不翻车”。

关于轮地查询，教程中的 raycast 描述与 API 中 `SweepShape` 选项需同时读：5.6 [SweepShape](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/SweepShape?application_version=5.6)列出 ray、sphere 和 wheel collision shape 三种语义；因此应核对轮的 `SweepShape` 与 `SweepType`（simple/complex），不能使用旧文未经核实的组件字段 `SuspensionTraceType`。更厚的扫描形状可能跨过窄缝或改变接触位置，并不保证薄地形再不漏检。

## 6. 三类手感的调参流程

| 目标原型 | 可逐项改变的方向 | 要观察的收益与代价 |
| --- | --- | --- |
| 街头赛车/超跑 | 先确定速度相关转向与制动，再比较轮胎响应、重心和悬挂 | 同一弯道的转向跟随、侧倾与接触保持；更硬未必在起伏路面更快 |
| 越野皮卡 | 校对更大可用行程和障碍尺寸，再调支撑/阻尼 | 越坎后恢复、触底次数、单轮离地时间；大行程不自动保证平稳 |
| 后驱漂移车 | 核驱动轴、手刹影响轮、轮端制动与滑动后横向响应 | 是否能进入并退出滑动、是否还能纠偏；降低摩擦可能同时损失起步和刹车 |

每次从同一初态、同一路面和同一输入脚本做 A/B，先只改变一组具有明确因果的参数，再看轮状态与车身响应。若目标只是玩法，应明确哪些是人工稳定辅助，避免把“易操控”声称为更真实。没有受控结果时，本表是调参方向，不是可直接移植的车型参数矩阵。

## 7. GT/PT、异步更新与运行期配置边界

应先区分三个时钟：游戏线程/输入更新、物理求解步、网络输入与状态帧。它们不必一一对应。读取到一份轮状态时，应记录所属车辆、轮索引、物理帧或时间戳以及配置版本；不能把当前游戏帧的输入与上一个已发布输出组合成“本帧因果证据”。

本段具体符号仅依据 **2026-10-10 所读页面标题为 UE 5.8 的公开声明**：

- [FChaosVehicleAsyncInput](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/FChaosVehicleAsyncInput?lang=en-US)说明输入从 GT 到 PT，`Simulate` 的说明指向物理线程。
- [FChaosVehicleManagerAsyncCallback](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/ChaosVehicles/FChaosVehicleManagerAsyncCallbac-)说明车辆模拟通过物理引擎异步回调执行。
- [FChaosVehicleManager](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/FChaosVehicleManager?lang=en-US)公开输入调参与输出句柄等接口名。

这些符号解释“需要跨边界传递输入和结果”，不证明调用顺序、锁、默认异步开关、热更复制点或确定性。AsyncInput 与 AsyncCallback 的固定 5.6/5.8 页面有读取失败记录；Manager 仅采用上述有日期的动态 5.8 标题快照，其固定版本页本轮未读取。因此没有把这些声明拼成经源码核对的 5.6 时序。需要实现级判断时，应在目标版本阅读相关函数体和构造值。

工程上可采用以下合同：GT 收集并限定输入；通过该版本支持的通道交给物理模拟；PT 使用属于该步的快照；消费端只使用已发布结果。不要从任意线程持有并修改可变 UObject/物理句柄，也不要在 GT 和 PT 各加一次相同力。这是项目设计要求，不能用 `EditAnywhere` 或一个裸指针的存在来证明可并发访问。

[5.6 Sub-Stepping](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-sub-stepping-in-unreal-engine?application_version=5.6)解释了帧被分成多个物理子步，以及力维持、回调排队和代价。子步上限与最大步长仍需共同考虑；开启子步不意味着任何长帧都能保持期望步长，也不等于网络时间同步。本页不沿用未经核实的 `bEnableSuspensionSubstepping` 字段，不把该教程夹带的历史平台描述外推到所有现行平台。

## 8. 联机：权威、预测、校正和画面分开

### 8.1 权威是玩法合同，不是固定时间步保证

服务器决定允许的输入、车辆控制权和最终玩法结果；拥有车辆的客户端可以做预测，但不能把自己的最终 Transform 当可信事实。远端车的显示与本地驾驶响应可以采用不同策略。Dedicated Server 是否在固定步长、什么物理频率下运行，必须读该项目配置与记录，不能由“Dedicated”一词推出。

在项目设计中，输入应能关联控制者、车辆生命周期与模拟帧；服务器检查有限数、范围、时间窗和控制权，明确重复、迟到、乱序输入及断线后的处理。重生或车辆池复用时，旧车轮状态与旧输入不能继续驱动新一代车辆。以上是需要实现的合同，不是本文已经提供一个可靠 `FVehicleInputs` RPC 协议。

### 8.2 不强制所有车辆采用 100 ms Hermite

[5.6 Networked Physics Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/networked-physics-overview?application_version=5.6)区分 Default、Predictive Interpolation 与 Resimulation。前两者处理权威状态与客户端校正；Resimulation 还需要历史状态/输入、按对应物理帧比较与回溯重算，代价涉及 CPU 和内存。文档提及重算后的渲染平滑，不能据此说物理状态永不跳变。

选择模式后，应避免自写每帧 Transform 插值同时与引擎物理复制争夺车身。视觉层平滑和碰撞刚体校正是不同责任。Hermite 可以是项目自己的表现层设计，但并非所有 Chaos 车辆的强制算法，100 ms 与 15–30 cm 也不是跨项目正确的窗口或阈值。

弱网下排查要对齐输入采样帧、服务器处理帧、收到的权威帧和本地比较帧，区分真实轮地冲量、状态过期和网络校正。仅调大误差阈值可能掩盖问题并扩大偏差；仅开启 Substepping 也不能修复输入历史或时间轴错误。本轮没有运行目标网络测试。

## 9. 十个常见问题：先收集能区分原因的观察

| 症状 | 先观察什么 | 因果分支与有限处理方向 |
| --- | --- | --- |
| 高速弯翻车 | 实际重心/惯性、速度/轮角、接触丢失、碰撞冲量 | 区分大转向侧倾与碰撞顶起；再单项比较转向曲线、重心、防倾，而非统一下移重心 |
| 平地静置抖动 | 轮/车身交叠、行程饱和、弹簧力、物理步与休眠 | 先修几何和初始穿插，再比较刚度、阻尼、时间分辨率；没有“阻尼≥0.7必好”的承诺 |
| RPM 高但不走 | 当前挡、轮端扭矩、接触、轮速、滑移、制动力 | 无动力到轮、空挡、轮离地、抓地不足及刹车未松都可能；不能直接增大半径 |
| 轮下薄地形漏检 | 查询形状、范围、碰撞通道、simple/complex、命中法线 | 先确认正确碰撞资产；比较 ray/sphere 的接触变化，不承诺切球扫描消除所有漏检 |
| 手刹不能甩尾 | 手刹是否到达、受影响轮、制动扭矩和实际速度 | 核手刹角色与轮状态；不编造 `HandbrakeFrictionMultiplier`，锁轮不保证所需漂移 |
| 空中不能调姿 | 哪些轮离地、接触边界、控制输入、力矩轴与惯性 | 明确玩法辅助和落地退出条件；Pitch 绕 Y、Roll 绕 X、Yaw 绕 Z；不能把 `(Pitch,Roll,Yaw)` 直接当世界 XYZ 力矩 |
| 六/八轮动力不对 | 每轮骨骼/轮类/轴型、接触与扭矩清单 | Wheel Setups 可增加轮数；不代表存在任意“多轴差速”模式或每轴自动等分扭矩 |
| 高速撞墙穿插 | 车身碰撞形状、相对位移、步长和碰撞检测配置 | CCD 只覆盖其适用碰撞过程，不代替轮地查询；不假设默认迭代数为 4，也不保证改 8 消除穿透 |
| 游戏辅助刹车过早/过晚 | 障碍相对速度、路面/坡度、命中对象、制动输入是否已执行 | 用有界玩法策略、滞回及玩家接管规则；固定距离阈值不等于真实 AEB 或安全距离保证 |
| DS 偶发弹飞 | 初始穿插、网络校正、接触冲量及对应帧 | 先分清离线同场景是否会发生与网络时间轴证据；不把所有跳跃归为客户端服务器步调不齐 |

空中调姿若自己施加力矩，先说明局部到世界变换、用力矩还是角冲量、施加频率及权威端，防止每帧乘法与子步重复累计。也可评估所用版本的游戏式 torque/rotation control 配置；“存在 Pitch 输入”并不证明所有车辆开箱有空中控制。

游戏辅助制动的简单纸面关系 `d ≈ v²/(2a)` 只在已假设恒定减速度、平路、无反应延时下估算距离。它不能提供任意材质、坡度、碰撞查询延迟下的保证；本文不把它用于真实车辆。

## 10. Modular Vehicle 与调试入口的版本边界

[UE 5.6 Chaos Modular Vehicles Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/chaos-modular-vehicles-overview?application_version=5.6)已描述运行时组装/破坏、独立模拟模块以及 Network Physics 的 resimulation 支持。普通骨骼车辆与模块化方案可以并存；该文档的模块化组成基于 Geometry Collection 与 Cluster Union。这是 5.6 文档的构成描述，后续版本若改用不同表示应独立核实。

因此不能写成“5.8 才推出、全面接入 UMoverComponent、现代确定性回溯保证消除撕裂”。本文保留架构选型入口，既不展开通用模块化车辆教程，也不把 Mover 的存在当成普通车辆网络行为的证明。

下面的命令名称仅来自 **2026-10-10 页面标题为 UE 5.8 的[Vehicle Debug Commands](https://dev.epicgames.com/documentation/unreal-engine/vehicle-debug-commands-in-unreal-engine?lang=en-US)快照**。固定版本入口读取失败，使用前须在目标构建查询是否存在；本轮一条也未执行。

| 观测目标 | 页面列出的入口 | 它不能单独证明什么 |
| --- | --- | --- |
| 重心与模型原点 | `p.Vehicle.ShowCOM`、`p.Vehicle.ShowModelOrigin` | 不能用画面替代质量/惯性配置记录 |
| 悬挂命中与行程 | `p.Vehicle.ShowSuspensionRaycasts`、`p.Vehicle.ShowSuspensionLimits` | 一次命中不证明高速薄障碍稳定 |
| 轮与悬挂力 | `p.Vehicle.ShowWheelForces`、`p.Vehicle.ShowSuspensionForces` | 绘制比例不是精确数值采样 |
| 命中的物体/材质 | `p.Vehicle.ShowRaycastComponent`、`p.Vehicle.ShowRaycastMaterial` | 材质名出现不证明项目策略已经应用 |
| 车辆模拟开销 | `stat ChaosVehicle`、`stat ChaosVehicleManager` | 不是跨平台 FPS/毫秒预算 |

页面说明部分物理线程绘制还依赖 `p.chaos.debugdraw.enabled 1`。调试开关通常用 1/0 控制，但应区分显示类与覆盖输入/关闭力的命令；后者会改变实验条件。排障结束需要恢复并记录。旧文 `p.Vehicle.ShowDebug`、`ShowSuspension`、`ShowWheelFriction` 未在本轮所读列表得到支持，不应继续作为已核实速查命令。

## 11. 验证与基准建议：有限纸面案例

全部标为 **PAPER_EXPECTED / NOT_RUN**。下表只逐项检查文内已声明的输入合同、单位换算和理想模型；没有执行车辆、算法、模型或测试。有关实际接触、抓地、网络、性能的列举是以后采集证据的入口，绝不由纸面表自动变成通过。

| 案例 | 明确输入与操作 | PAPER_EXPECTED（手工推导或合同判定） | 判定边界 |
| --- | --- | --- | --- |
| P01 半径 | 美术量得直径 68 cm，填轮半径 | 填 34 cm；力学算式使用 0.34 m | 不证明资产实际尺度正确 |
| P02 轮索引 | Wheel Setups 0/2 交换，日志仍把 0 标左前 | 判轮表不一致，应同步身份映射 | 数组位置不构成前后轴身份 |
| P03 轴向 | 车体局部 X 前/Z 上，欲施 Pitch 力矩 | 局部 Y 轴；若用世界接口先转换 | 未验证符号正负与引擎调用 |
| P04 归一化扭矩 | MaxTorque=650，曲线值=0.8 | 满输出参考 520 N·m | 无换挡/油门动态推断 |
| P05 理想传动 | 扭矩100 N·m，挡比2，主减速3，效率0.9 | 轮端总扭矩近似540 N·m | 未分流到各轮、未含瞬态 |
| P06 半径反例 | P05 轮端总扭矩不变，半径0.3→0.6 m | 理想总周向力1800→900 N | 放大半径不是无代价增抓地 |
| P07 分流方向 | 5.6 注释，split=0.3 | 按该注释属于偏前侧，不能称前30后70 | 不推出精确轮端百分比 |
| P08 输入越界 | 有控制权且上下文有效：油门1.4，刹车-0.2，转向-2 | 适配输出1、0、-1 | 仅本节裁剪策略 |
| P09 非有限数 | ownsLocalControl=true且contextActive=true；油门NaN，刹车+∞，转向0.4 | 适配输出0、0、0.4 | 油门归零不是停车保证 |
| P10 释放与取消 | 本帧ownsLocalControl=true；前一帧手刹true，本帧contextActive=false | 发出全零轴与手刹false | 需实际输入入口接受才有效 |
| P11 控制权 | ownsLocalControl=false，所有输入满值 | NoLocalWrite | 服务器接管另有合同 |
| P12 降挡 | 手动模式，当前挡1，按下降挡 | NoGearRequest，不进空挡/倒挡 | 从3挡按下则RequestGear(2) |
| P13 路面改变 | 接触有效，Ice→未登记路面 | desired 从0.20变成DefaultGrip=1.00 | 策略请求，不是测得摩擦系数 |
| P14 离地 | 调用ChooseGrip；上次路面Ice，本次contact=false | 返回NoContactChange | 不把无接触伪装成Dry |
| P15 应用确认 | 新路面请求0.70但未确认生效 | lastApplied不更新 | 不能靠GT值宣布PT生效 |
| P16 悬挂静态 | 300 kg/轮，g=9.81，k=30000 N/m | 理想压缩0.0981 m | 均分载荷及线性模型假设 |
| P17 行程不足 | P16，最大压缩只允许0.05 m | 与“不触底的理想平衡”目标冲突 | 不预测引擎穿透或真实车高 |
| P18 重心覆盖 | 5.6启用COM override，同时修改Mesh COM offset | 按声明Mesh offset被忽略 | 未验证运行期赋值何时同步 |
| P19 帧对齐 | 输入属PT帧102，观测轮状态属帧100 | 不得称“102输入已造成该轮结果” | GT显示刷新不足以配对 |
| P20 网络校正 | 权威状态帧80，本地当前帧86 | 回溯比较应找同一帧历史；不可直接当86帧误差 | 具体网络模式和容差需实现 |
| P21 游戏刹车 | 理想平路、无延时，v=10 m/s，a=5 m/s² | 停止距离10 m；v=20时为40 m | 不用于真实车辆安全控制 |
| P22 轮数 | 轮数组从4增为6，没有配置新增轮的驱动角色 | 配置不完整，不能宣称自动六驱 | 允许加轮不等于自动传动方案 |

未来如实际验证，至少保存引擎版本/构建、地图/碰撞资产、车辆和路面配置、初始姿态与速度、输入序列、实际物理步、逐轮接触/力/扭矩与车身状态。联机还需端角色、网络条件、帧号与校正记录。成功样本与失败样本都保留，再分别报告“配置进入模拟”“所选场景达到玩法目标”与“性能达到目标平台预算”。本页没有这些运行记录，所以保持 L2 与 `verified: []`。

## 12. 来源使用与关联阅读

本页固定 5.6 来源负责资产、配置语义、输入范围、查询选项及网络模式；5.8 标题的动态公开页只补充明确标识的接口/命令快照。读取失败不是不存在的证明，也不支持把这些字段提升为已核对实现。公式与伪代码均是本页明确假设下的教学推导或项目设计；没有引用受限引擎源码全文。

- [01-Chaos物理引擎概览](01-Chaos物理引擎概览.md)：求解器与物理调度的主责入口。
- [02-碰撞检测与物理材质](02-碰撞检测与物理材质.md)：碰撞查询、物理材质与接触响应。
- [03-物理约束与关节](03-物理约束与关节.md)：约束与弹簧机制；本篇不复制关节求解器教程。
- [06-网络同步/03-客户端预测与延迟补偿](../../07-网络与游戏服务端/同步预测与回放/03-客户端预测与延迟补偿.md)：预测、状态帧与校正。
- [12-15 物理系统源码](15-物理系统源码.md)：进一步阅读引擎实现的入口；链接存在不代表本页已读实现。
- [00-13 数学与算法基础/02-概率线性代数与数值计算](../../02-数学与游戏算法/数学与数值计算/02-概率线性代数与数值计算.md)：向量、力矩与数值解释的前置知识。
