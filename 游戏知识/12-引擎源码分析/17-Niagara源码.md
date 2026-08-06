# 17 Niagara 源码剖析（UE 5.8）
> 版本基准：UE 5.8.0（本机 `Engine/Build/Build.version`：Major 5 / Minor 8 / Patch 0 / CL 55116800，分支 `++UE5+Release-5.8`）。
> 源码依据：本机只读安装目录 `C:\Program Files\Epic Games\UE_5.8\Engine`，Niagara 运行时与编辑器位于 `Plugins/FX/Niagara/Source`，VectorVM 依赖位于 `Source/Runtime/VectorVM`。
> 适用范围：编辑器资产编译、客户端/服务端运行时系统实例、CPU VectorVM、GPU Compute 与渲染线程派发；GPU 模拟和渲染部分仍需按目标平台能力单独验收。
> 兼容性边界：UE 4.27 及 UE 5.0–5.7 仅作为迁移背景；插件目录、实例实现拆分和编译器私有类均以 UE 5.8 实际源码为准，不承诺跨版本 ABI。
> 官方参考：[Unreal Engine 官方文档总页](https://dev.epicgames.com/documentation/en-us/unreal-engine)。
> 最后更新：2026-08-06（补齐 UE5.8 元数据、官方入口与 Niagara 源码验收边界）。

> 对应知识点：[11-VFX与Niagara/01-Niagara粒子系统基础](../11-VFX与Niagara/01-Niagara粒子系统基础.md)
>
> 适用版本：UE 5.8（`Build.version` 实测：MajorVersion 5 / MinorVersion 8 / Changelist 55116800 / BranchName `++UE5+Release-5.8`）。
> 源码根目录：`C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\FX\Niagara\Source`（注意是 **Plugins\FX\Niagara**，
> 不是网上老教程常见的 `Engine\Source\Runtime\Niagara`；本机 5.8 不存在 Runtime 版 Niagara 目录）。
> 文中所有类名 / 函数名 / 文件行号均来自本机 5.8 真实源码，逐一用 `Test-Path` / `findstr` 验证；
> 标「摘选」的代码仅做裁剪、未改动任何符号；标「示意」的片段仅用于表达调用关系，请以实际源码为准。
>
> 特别提示：UE 5.8 的 Niagara 头文件布局与 UE4 ~ UE5.3 时代的老教程差异很大，例如：
> `NiagaraComponent.h` 已从 `Classes/` 移到 `Public/`；`NiagaraDataSet.h` 仍留在 `Classes/`；
> 5.3 时代的 `FNiagaraSystemInstanceImpl` 在 5.8 已不存在（实现合并回 `FNiagaraSystemInstance`，另有
> `Internal/NiagaraEmitterInstanceImpl.h` 承载发射器实现）；编译器 `FHlslNiagaraCompiler` 位于
> `NiagaraEditor/Private/`。本文一律以 5.8 实测为准，不沿用记忆中的旧名。

## 一、概述

### 1.1 本篇回答的问题

- 一个 `UNiagaraComponent` 挂到场景里之后，到底是谁在驱动它每帧模拟？组件与 `FNiagaraSystemInstance` 之间为什么隔着一个 `FNiagaraSystemInstanceController`？
- 5.8 里「CPU 模拟」和「GPU 模拟」分别由哪些类执行？粒子数据存在哪里？
- `FNiagaraDataSet` 和 `FNiagaraDataBuffer` 是什么关系？为什么说 Niagara 的数据是「帧缓冲」式的？
- 蓝图里拖出来的各种「数据接口」（骨骼网格、粒子读取、RenderTarget 等）在 C++ 层是什么结构、如何被调用？
- Niagara 资产从节点图到可执行的 VM 字节码 / HLSL，经过了哪些类？（`FNiagaraHlslTranslator` → `FHlslNiagaraCompiler`）
- 5.8 的 Niagara 有哪些关键类清单（全部为本机源码验证，非记忆中的 UE4 旧名）？

### 1.2 与知识库文章的对应关系

| 知识库文章 | 讲清了什么 | 本篇补充的源码层内容 |
| --- | --- | --- |
| [11-VFX与Niagara/01-Niagara粒子系统基础](../11-VFX与Niagara/01-Niagara粒子系统基础.md) | Niagara 是什么、组件/系统/发射器/模块的层级概念、与 Cascade 的区别 | `UNiagaraComponent`→`FNiagaraSystemInstanceController`→`FNiagaraSystemInstance` 的真实调用链与每帧流程 |
| 11-VFX与Niagara 后续文章（若存在） | 具体模块用法、参数与数据接口操作 | `UNiagaraDataInterface` 基类虚函数、常用子类真实头文件位置 |

建议先读知识库概念文章，再读本篇；两者配合可以回答「Niagara 为什么这样跑」与「性能问题该查哪个类」两类问题。

### 1.3 5.8 目录结构变化速览（与网上老教程最大的差异点）

- Niagara 是 **插件**：源码在 `Engine/Plugins/FX/Niagara/Source`，模块有 `Niagara`（运行时）、`NiagaraCore`、
  `NiagaraShader`（着色器绑定）、`NiagaraVertexFactories`（渲染工厂）、`NiagaraEditor`（编辑器/编译）等。
- `Niagara/Classes/` 与 `Niagara/Public/` 并存：UPROPERTY 反射类（UObject 类）多在 `Classes/`，
  纯 C++ 结构/类（`FNiagaraSystemInstance`、`FNiagaraSystemSimulation`、`FNiagaraSystemInstanceController`）在 `Public/`；
  但 `FNiagaraDataSet`、`FNiagaraEmitterInstance`、`FNiagaraScriptExecutionContext` 等又在 `Classes/`——
  因此定位文件时以实际路径为准，不能只凭目录名猜。
- 新增 `Niagara/Internal/` 目录：内部实现类（如 `FNiagaraEmitterInstanceImpl`）与 5.8 新引入的
  Stateless（无状态粒子）体系（`Internal/Stateless/`）放在这里。
- `FNiagaraSystemInstanceController`（`Public/NiagaraSystemInstanceController.h`）是 5.0 起引入的
  线程安全访问层，5.8 中组件对实例的所有操作都必须经由它。

## 二、源码定位

> 下表所有路径均已在 `C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\FX\Niagara\Source` 下用
> `Test-Path` 验证存在，关键符号均已用 `findstr /s /m` 验证类名真实存在；行号为验证时的实测行号，
> 后续小版本可能漂移。

| 模块 | 文件（相对 Source） | 关键符号（实测行号） | 作用 |
| --- | --- | --- | --- |
| Niagara | `Niagara/Public/NiagaraComponent.h` | `UNiagaraComponent`（L57，继承 `UFXSystemComponent`）；`SystemInstanceController` 成员（L150）；`GetSystemInstanceController()`（L408） | 场景中的粒子组件：资产的持有者与激活入口 |
| Niagara | `Niagara/Private/NiagaraComponent.cpp` | `UNiagaraComponent::InitializeSystem`（L1204）；`ActivateSystem`（L870）；`Activate`（L1274）；`Deactivate`（L1560）；`OnRegister`（L2021） | 组件生命周期：创建控制器、激活/停用、注册 |
| Niagara | `Niagara/Public/NiagaraSystemInstanceController.h` | `FNiagaraSystemInstanceController`（L56）；`Initialize`（L68）；`Release`（L71）；`SetVariable` 重载（L173~）；`NIAGARA_SYSTEM_INSTANCE_CONTROLLER_SHIM`（L29） | 线程安全的系统实例控制接口（组件与实例之间的中间层） |
| Niagara | `Niagara/Public/NiagaraSystemInstance.h` | `FNiagaraSystemInstance`（L81，继承 `FNiagaraSystemInstanceFixLayout`）；`EResetMode`（L102）；`Tick_GameThread/Tick_Concurrent/FinalizeTick_GameThread`（L217~225）；`TickDataInterfaces`（L251）；`GenerateAndSubmitGPUTick`（L227） | 单个系统实例：运行一个 `UNiagaraSystem` 资产的模拟状态机 |
| Niagara | `Niagara/Private/NiagaraSystemInstance.cpp` | `FNiagaraSystemInstance::Init`（L198）；`Activate`（L582）；`Reset`（L775）；`TickDataInterfaces`（L1863）；`Tick_GameThread`（L2584） | 系统实例各阶段实现 |
| Niagara | `Niagara/Public/NiagaraSystemSimulation.h` | `FNiagaraSystemSimulation`（L247）；`Tick_GameThread/Tick_Concurrent`（L262~264）；`Spawn_GameThread`（L272）；`NiagaraSystemTickBatchSize`（L40） | 世界级批处理：把同一资产的所有实例打包模拟（批大小 4） |
| Niagara | `Niagara/Classes/NiagaraEmitterInstance.h` | `FNiagaraEmitterInstance`（L25，抽象基类）；`Tick/ResetSimulation/HandleCompletion`（L34~46 纯虚接口）；`GetSimTarget()`（L54） | 发射器实例接口（CPU 有状态 / Stateless 两种实现） |
| Niagara | `Niagara/Internal/NiagaraEmitterInstanceImpl.h` | `FNiagaraEmitterInstanceImpl`（L23，`final`）；`FEventInstanceData`（L30） | 有状态发射器实例实现（5.8 内部实现类） |
| Niagara | `Niagara/Classes/NiagaraDataSet.h` | `FNiagaraSharedObject`（L21）；`FNiagaraDataBuffer`（L86）；`FNiagaraDataSet`（L267）；`BeginSimulate/EndSimulate`（L288~291） | 粒子数据容器：数据集（布局）+ 双缓冲帧数据 |
| Niagara | `Niagara/Classes/NiagaraScriptExecutionContext.h` | `FNiagaraScriptExecutionContextBase`（L129）；`FNiagaraScriptExecutionContext`（L200）；`FNiagaraSystemScriptExecutionContext`（L224） | CPU（VectorVM）脚本执行上下文 |
| Niagara | `Niagara/Classes/NiagaraComputeExecutionContext.h` | `FNiagaraComputeExecutionContext`（L67，继承 `INiagaraComputeDataBufferInterface`） | GPU 计算执行上下文 |
| Niagara | `Niagara/Classes/NiagaraGPUSystemTick.h` | `FNiagaraGPUSystemTick`、`FNiagaraComputeInstanceData`（L22） | GPU Tick 描述：游戏线程构造、渲染线程消费 |
| Niagara | `Niagara/Public/NiagaraSystemGpuComputeProxy.h` | `FNiagaraSystemGpuComputeProxy`（L14）；`QueueTick`（L28）；`PendingTicks`（L59） | 系统实例在渲染线程侧的代理，收集待派发 GPU Tick |
| Niagara | `Niagara/Public/NiagaraGpuComputeDispatchInterface.h` | `FNiagaraGpuComputeDispatchInterface`（L31，继承 `FFXSystemInterface`）；`static Get(UWorld*)`（L37）；`AddGpuComputeProxy`（L50） | GPU 计算派发的公共接口（FX 系统接口） |
| Niagara | `Niagara/Private/NiagaraGpuComputeDispatch.h` | `FNiagaraGpuComputeDispatch`（L85，接口的私有实现）；`PreInitViews`（L117）；`PreRender`（L123） | 渲染线程上的计算派发器（每场景一个） |
| Niagara | `Niagara/Public/NiagaraCommon.h` | `ENiagaraSimTarget`（L176：`CPUSim` / `GPUComputeSim`） | 模拟目标枚举：CPU 还是 GPU |
| Niagara | `Niagara/Classes/NiagaraDataInterface.h` | `UNiagaraDataInterface`（L584，继承 `UNiagaraDataInterfaceBase`）；`GetFunctions`（L882）；`GetVMExternalFunction`（L698） | 所有数据接口的基类 |
| Niagara | `Niagara/Classes/NiagaraDataInterfaceSkeletalMesh.h` | `UNiagaraDataInterfaceSkeletalMesh`（L700）；`SourceMode`（L707） | 骨骼网格采样数据接口 |
| Niagara | `Niagara/Classes/NiagaraDataInterfaceParticleRead.h` | `UNiagaraDataInterfaceParticleRead`（L12，继承 `UNiagaraDataInterfaceRWBase`）；`FShaderParameters`（L16~30） | 粒子属性读取（GPU 直接读另一发射器的粒子缓冲） |
| Niagara | `Niagara/Classes/NiagaraDataInterfaceNeighborGrid3D.h` | `UNiagaraDataInterfaceNeighborGrid3D` | 3D 邻居网格（流体/邻域查询） |
| Niagara | `Niagara/Classes/NiagaraDataInterfaceRenderTarget2D.h` | `UNiagaraDataInterfaceRenderTarget2D` | 2D RenderTarget 读写（GPU 绘制） |
| Niagara | `Niagara/Classes/NiagaraDataInterfaceGrid2DCollection.h` | `UNiagaraDataInterfaceGrid2DCollection` | 2D 网格集合（Grid2D 模拟） |
| Niagara | `Niagara/Classes/NiagaraDataInterfaceCollisionQuery.h` | `UNiagaraDataInterfaceCollisionQuery` | 场景碰撞查询 |
| Niagara | `Niagara/Classes/NiagaraSystem.h` | `UNiagaraSystem`（L238，继承 `UFXSystemAsset`）；`GetEmitterHandles`（L313） | 系统资产：发射器句柄容器 + 系统级脚本 |
| Niagara | `Niagara/Public/NiagaraSystemRenderData.h` | `FNiagaraSystemRenderData` | 渲染数据（渲染线程持有） |
| NiagaraEditor | `NiagaraEditor/Public/INiagaraCompiler.h` | `FNiagaraCompileResults`（L29）；`INiagaraCompiler` | 编译结果与编译器接口 |
| NiagaraEditor | `NiagaraEditor/Private/NiagaraCompiler.h` | `FNiagaraCompilerJob`（L19）；`FHlslNiagaraCompiler`（L32）；`FNiagaraShaderMapCompiler`（L64） | HLSL 编译器实现（VM 字节码 + Shader 提交） |
| NiagaraEditor | `NiagaraEditor/Private/NiagaraHlslTranslator.h` | `FNiagaraHlslTranslator`（L224）；`CodeChunks`（L248）；`FDataSetAccessInfo`（L228） | 节点图 → HLSL 翻译器 |
| Niagara | `Niagara/Private/NiagaraAsyncCompile.h` / `.cpp` | `FNiagaraAsyncCompile`（存在） | 异步编译任务 |
| VectorVM（引擎模块） | `Engine/Source/Runtime/VectorVM/Public/VectorVM.h` | `VectorVM::Runtime::FVectorVMState`（L24） | CPU 模拟的字节码虚拟机 |

## 三、运行时架构剖析

### 3.1 入口：UNiagaraComponent（组件）

`Niagara/Public/NiagaraComponent.h`（5.8 中位于 `Public/`，不再是老版本的 `Classes/`）：

```cpp
// NiagaraComponent.h:56-57（摘选）
UCLASS(ClassGroup = (Rendering, Common), Blueprintable, hidecategories = Object, ...)
class UNiagaraComponent : public UFXSystemComponent
```

逐行解释：
- `UCLASS(..., Blueprintable, ..., meta = (BlueprintSpawnableComponent, ...))`：可在蓝图里作为组件生成，
  对应编辑器里「Add Niagara Particle System Component」。
- `class UNiagaraComponent : public UFXSystemComponent`：5.8 中 Niagara 组件继承的是特效系统组件的
  抽象基类 `UFXSystemComponent`（FX 系统的统一入口，Cascade 组件同样继承它），而不是直接继承
  `UPrimitiveComponent`。组件本身仍是 primitive 组件（提供场景代理与渲染）。

组件与系统实例之间隔着控制器。`NiagaraComponent.h` 中的关键成员与访问器：

```cpp
// NiagaraComponent.h:34-35, 150, 408（摘选）
using FNiagaraSystemInstanceControllerPtr = TSharedPtr<FNiagaraSystemInstanceController, ESPMode::ThreadSafe>;
using FNiagaraSystemInstanceControllerConstPtr = TSharedPtr<const FNiagaraSystemInstanceController, ESPMode::ThreadSafe>;
...
FNiagaraSystemInstanceControllerPtr SystemInstanceController;          // L150：组件持有的控制器（线程安全共享指针）
...
FNiagaraSystemInstanceControllerPtr GetSystemInstanceController() { return SystemInstanceController; }  // L408
```

逐行解释：
- 组件不直接持有 `FNiagaraSystemInstance*`，而是持有 `FNiagaraSystemInstanceControllerPtr`
  （`TSharedPtr<..., ESPMode::ThreadSafe>`）。这是因为 5.x 起 Niagara 系统实例的并发 tick
  （`Tick_Concurrent` 可在工作线程执行）使裸指针访问变得不安全，控制器成了唯一的合法访问通道。
- `GetSystemInstanceController()` 是外部（包括游戏代码、渲染代理）获取控制器的标准入口；
  组件内部大量逻辑也通过它转发（见 3.2）。

控制器在哪里创建？`Niagara/Private/NiagaraComponent.cpp` 的 `InitializeSystem()`：

```cpp
// NiagaraComponent.cpp:1204-1224（摘选）
bool UNiagaraComponent::InitializeSystem()
{
	if (SystemInstanceController.IsValid() == false)
	{
		LLM_SCOPE(ELLMTag::Niagara);
		...
		const bool bPooled = PoolingMethod != ENCPoolMethod::None;
		OverrideParameters.MarkParametersDirty(); // new system instance means new lwc tile, ...

		SystemInstanceController = MakeShared<FNiagaraSystemInstanceController, ESPMode::ThreadSafe>();
		SystemInstanceController->Initialize(*World, *Asset, &OverrideParameters, this, TickBehavior, bPooled, RandomSeedOffset, RequiresSoloMode(), bOverrideWarmupSettings ? WarmupTickCount : -1, WarmupTickDelta);
		SystemInstanceController->SetOnPostTick(FNiagaraSystemInstance::FOnPostTick::CreateUObject(this, &UNiagaraComponent::PostSystemTick_GameThread));
		SystemInstanceController->SetOnComplete(FNiagaraSystemInstance::FOnComplete::CreateUObject(this, &UNiagaraComponent::OnSystemComplete));
		...
	}
}
```

逐行解释：
- `MakeShared<FNiagaraSystemInstanceController, ESPMode::ThreadSafe>()`：创建控制器（线程安全共享对象）。
- `SystemInstanceController->Initialize(*World, *Asset, &OverrideParameters, this, TickBehavior, ...)`：
  把世界、系统资产、用户参数覆盖、宿主组件、Tick 行为、是否池化、随机种子偏移、是否 Solo、
  预烘焙（Warmup）参数一次性交给控制器，由控制器内部创建真正的 `FNiagaraSystemInstance`。
- `SetOnPostTick / SetOnComplete`：把组件自己的回调注册给实例的委托，模拟完成一帧/系统结束后组件
  能得到通知（例如 `OnSystemComplete` 触发蓝图 `OnSystemFinished`）。

组件侧的激活入口（`NiagaraComponent.cpp:870-886`）：

```cpp
void UNiagaraComponent::ActivateSystem(bool bFlagAsJustAttached)
{
	// Attachment is handled different in niagara so the bFlagAsJustAttached is ignored here.
	if (IsActive())
	{
		// If the system is already active then activate with reset to reset the system simulation but
		// leave the emitter simulations active.
		bool bResetSystem = true;
		bool bIsFromScalability = false;
		ActivateInternal(bResetSystem, bIsFromScalability);
	}
	else
	{
		// Otherwise just follow the standard activate path.
		Activate();
	}
}
```

逐行解释：已激活时再次调用只重置系统模拟（保留发射器模拟），未激活时走标准 `Activate()` 路径
（`Activate()` 在 L1274，内部最终调用 `ActivateInternal` → `InitializeSystem` → 控制器激活实例）。

### 3.2 中间层：FNiagaraSystemInstanceController（控制器）

`Niagara/Public/NiagaraSystemInstanceController.h`：

```cpp
// NiagaraSystemInstanceController.h:11-14, 28-31, 53-59（摘选）
#ifndef NIAGARA_SYSTEM_INSTANCE_CONTROLLER_ASYNC
/** When true, instance handle operations are asynchronous. Otherwise, the interface is pass-through */
#define NIAGARA_SYSTEM_INSTANCE_CONTROLLER_ASYNC 0
#endif
...
/** Used to expose FNiagaraSystemInstance methods without actually providing the interface */
#define NIAGARA_SYSTEM_INSTANCE_CONTROLLER_SHIM(MethodName, FuncMod) \
	template <typename... ArgTypes> \
	inline auto MethodName(ArgTypes... Args) FuncMod { ensure(IsValid()); return SystemInstance->MethodName(Forward<ArgTypes>(Args)...); }
...
/**
 * This is the main asynchronous interface for controlling operation of a single instance of a Niagara System.
 */
class FNiagaraSystemInstanceController
	: public TSharedFromThis<FNiagaraSystemInstanceController, ESPMode::ThreadSafe>
	, private FGCObject
```

逐行解释：
- `NIAGARA_SYSTEM_INSTANCE_CONTROLLER_ASYNC` 默认 0：控制器目前是「透传」模式；若置 1 则所有操作
  进入延迟队列异步执行（接口通过延迟队列表达异步执行边界）。
- `NIAGARA_SYSTEM_INSTANCE_CONTROLLER_SHIM`：宏批量生成转发方法——`MethodName(...)` 展开为
  `ensure(IsValid()); return SystemInstance->MethodName(...)`，把控制器方法直接转发给内部实例，
  同时用 `ensure` 保证实例仍有效。
- 类继承 `TSharedFromThis<..., ESPMode::ThreadSafe>`（配合组件的 `TSharedPtr` 持有）与 `FGCObject`
  （保证控制器引用的 UObject 不被 GC 回收）。

控制器暴露的关键接口（L68~110、L173~184 摘选）：

```cpp
void Initialize(UWorld& World, UNiagaraSystem& System, FNiagaraUserRedirectionParameterStore* OverrideParameters, USceneComponent* AttachComponent,
	ENiagaraTickBehavior TickBehavior, bool bPooled, int32 RandomSeedOffset, bool bForceSolo, int32 WarmupTickCount, float WarmupTickDelta);
void Release();
inline bool IsValid() const { return SystemInstance.IsValid(); }
...
FNiagaraSystemInstance* GetSystemInstance_Unsafe() const { return SystemInstance.Get(); }   // 已标记 deprecated 风险
FNiagaraSystemInstance* GetSoloSystemInstance() const { return ensure(IsValid() && SystemInstance->IsSolo()) ? SystemInstance.Get() : nullptr; }
...
void SetVariable(FName InVariableName, bool InValue);
void SetVariable(FName InVariableName, int32 InValue);
void SetVariable(FName InVariableName, float InValue);
void SetVariable(FName InVariableName, FVector3f InValue);
void SetVariable(FName InVariableName, FLinearColor InValue);
```

逐行解释：
- `Initialize(...)`：唯一合法的创建入口，参数与组件侧一一对应（池化、随机种子、Solo、Warmup 等）。
- `GetSystemInstance_Unsafe()`：返回裸指针，注释明确警告「可能被并发访问」；只有 Solo 系统
  （`GetSoloSystemInstance`）可以安全直接访问实例——因为 Solo 系统手动 tick、不参与并发批处理。
- `SetVariable` 系列：游戏代码设置用户参数的最终转发点（蓝图 `Set Niagara Variable` 走这里）。

### 3.3 核心：FNiagaraSystemInstance（系统实例）

`Niagara/Public/NiagaraSystemInstance.h`。5.8 中 5.3 时代的 `FNiagaraSystemInstanceImpl` 已不存在，
实现合并回主类（实测 `findstr` 全树搜索 `FNiagaraSystemInstanceImpl` 无任何头文件命中），
而是引入了一个小的布局基类：

```cpp
// NiagaraSystemInstance.h:81, 102-112, 126-127（摘选）
class FNiagaraSystemInstance : public FNiagaraSystemInstanceFixLayout
{
	...
	enum class EResetMode : uint8
	{
		/** Resets the System instance and simulations. */
		ResetAll,
		/** Resets the System instance but not the simulations */
		ResetSystem,
		/** Full reinitialization of the system and emitters.  */
		ReInit,
		/** No reset */
		None
	};
	...
	NIAGARA_API FNiagaraSystemInstance(UWorld& InWorld, UNiagaraSystem& InAsset, FNiagaraUserRedirectionParameterStore* InOverrideParameters = nullptr,
		USceneComponent* InAttachComponent = nullptr, ENiagaraTickBehavior InTickBehavior = ENiagaraTickBehavior::UsePrereqs, bool bInPooled = false);
```

逐行解释：
- 一个 `FNiagaraSystemInstance` = 场景中「一个组件实例」运行的「一份系统资产模拟」，是模拟状态机的
  主体（状态机本身见 `ENiagaraSystemInstanceState SystemInstanceState`，L114）。
- `EResetMode` 定义重置粒度：`ResetAll` 连发射器一起重置；`ResetSystem` 只重置系统层；`ReInit`
  完全重建；`None` 不重置——组件 `ActivateSystem` 的注释里提到的「重置系统但保留发射器模拟」对应 `ResetSystem`。
- 构造函数直接接收世界、资产、参数覆盖、宿主组件、Tick 行为、是否池化。

每帧的三段式 tick 接口（L217-228）：

```cpp
/** Initial phase of system instance tick. Must be executed on the game thread. */
void Tick_GameThread(float DeltaSeconds);
/** Secondary phase of the system instance tick that can be executed on any thread. */
void Tick_Concurrent(bool bEnqueueGPUTickIfNeeded = true);
/** Final phase of system instance tick. Must be executed on the game thread. */
void FinalizeTick_GameThread(bool bEnqueueGPUTickIfNeeded = true);

void GenerateAndSubmitGPUTick();
void InitGPUTick(FNiagaraGPUSystemTick& OutTick);
```

逐行解释：CPU 模拟被拆成「游戏线程阶段 → 并发阶段 → 游戏线程收尾」三段，配合 TaskGraph 并行；
GPU 模拟则由 `GenerateAndSubmitGPUTick` 在游戏线程构造 `FNiagaraGPUSystemTick` 并提交给渲染线程
（见 3.8）。实例还持有双缓冲参数区（`GetParameterIndex`/`FlipParameterBuffers`，L168~182，
`GlobalParameters/SystemParameters/OwnerParameters/EmitterParameters` 各两份），避免并发读写竞争。

数据接口的实例数据查找（L323-330）：

```cpp
inline void* FindDataInterfaceInstanceData(const UNiagaraDataInterface* Interface)
{
	if (auto* InstDataOffsetPair = DataInterfaceInstanceDataOffsets.FindByPredicate([&](auto& Pair){ return Pair.Key.Get() == Interface;}))
	{
		return &DataInterfaceInstanceData[InstDataOffsetPair->Value];
	}
	return nullptr;
}
```

逐行解释：每个数据接口在系统实例里有一块「每实例数据」（per-instance data，例如骨骼网格 DI 持有的
蒙皮数据句柄），以 `TWeakObjectPtr<UNiagaraDataInterface> → 偏移` 的映射记录，`FindDataInterfaceInstanceData`
按接口对象反查偏移并返回内存块首地址；`TickDataInterfaces`（见 4.3）会驱动这些数据的更新与重建。

### 3.4 批量调度：FNiagaraSystemSimulation（系统模拟）

`Niagara/Public/NiagaraSystemSimulation.h`：

```cpp
// NiagaraSystemSimulation.h:40-41, 246-247, 262-274（摘选）
#define NiagaraSystemTickBatchSize 4
typedef TArray<FNiagaraSystemInstance*, TInlineAllocator<NiagaraSystemTickBatchSize>> FNiagaraSystemTickBatch;
...
/** Simulation performing all system and emitter scripts for a instances of a UNiagaraSystem in a world. */
class FNiagaraSystemSimulation : public TSharedFromThis<FNiagaraSystemSimulation, ESPMode::ThreadSafe>
{
	...
	/** First phase of system sim tick. Must run on GameThread. */
	void Tick_GameThread(float DeltaSeconds, const FGraphEventRef& MyCompletionGraphEvent);
	/** Second phase of system sim tick that can run on any thread. */
	void Tick_Concurrent(FNiagaraSystemSimulationTickContext& Context);
	...
	void Spawn_GameThread(float DeltaSeconds, bool bPostActorTick);
	void Spawn_Concurrent(FNiagaraSystemSimulationTickContext& Context);
	void RemoveInstance(FNiagaraSystemInstance* Instance);
	void AddInstance(FNiagaraSystemInstance* Instance);
```

逐行解释：
- 「系统模拟」是世界级（每世界、每系统资产）的批处理对象：同一个 `UNiagaraSystem` 的几十个实例
  会注册进同一个 `FNiagaraSystemSimulation`（`AddInstance`），按批（每批 4 个实例，
  `NiagaraSystemTickBatchSize`）打包执行系统脚本，摊薄脚本调用开销。
- 同样拆成 `Tick_GameThread`（游戏线程，负责生成任务）与 `Tick_Concurrent`（任意线程，实际执行模拟），
  通过 `FGraphEventRef` 完成图事件与游戏线程同步。
- `Spawn_GameThread / Spawn_Concurrent` 专门处理本帧新激活的实例（延迟生成，避免在 tick 中途插入）。

并发任务的实际载体在 `Niagara/Private/NiagaraSystemSimulation.cpp`（L312~341 摘选）：

```cpp
// Task to run FNiagaraSystemSimulation::Tick_Concurrent
struct FNiagaraSystemSimulationTickConcurrentTask
{
	...
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		...
		Context.Owner->Tick_Concurrent(Context);
		CompletionTask->Unlock();
	}
	...
};
```

逐行解释：`TGraphTask` 把 `FNiagaraSystemSimulation::Tick_Concurrent` 包成一个可并行的任务，
`CompletionTask`（`FNiagaraSystemSimulationAllWorkCompleteTask`）在全部并发工作完成后解锁并通知游戏线程收尾。

### 3.5 发射器：FNiagaraEmitterInstance / FNiagaraEmitterInstanceImpl

`Niagara/Classes/NiagaraEmitterInstance.h`（L22-54 摘选）：

```cpp
/**
* Base class for different emitter instances
*/
class FNiagaraEmitterInstance
{
	...
	//~Begin: Define Emitter Interface
	virtual void Init(int32 InEmitterIdx);
	virtual void ResetSimulation(bool bKillExisting = true) = 0;
	virtual void SetEmitterEnable(bool bNewEnableState) = 0;
	virtual void OnPooledReuse() = 0;
	virtual bool HandleCompletion(bool bForce = false) = 0;
	...
	virtual bool ShouldTick() const = 0;
	virtual void PreTick() {}
	virtual void Tick(float DeltaSeconds) = 0;
	//~End: Define Emitter Interface
	...
	ENiagaraSimTarget GetSimTarget() const { return SimTarget; }
```

逐行解释：
- 5.8 中 `FNiagaraEmitterInstance` 是**抽象基类**（纯虚 `Tick`/`ResetSimulation`/`HandleCompletion` 等），
  不再直接承载实现；注释 "Base class for different emitter instances" 说明了它的定位。
- `GetSimTarget()` 返回 `ENiagaraSimTarget`（`CPUSim` / `GPUComputeSim`，定义于 `NiagaraCommon.h:176`），
  即发射器是 CPU 模拟还是 GPU 模拟由资产的 Sim Target 决定。

具体实现类在 `Niagara/Internal/NiagaraEmitterInstanceImpl.h`（L21-44 摘选）：

```cpp
/**
* Implementation of a stateful Niagara particle simulation
*/
class FNiagaraEmitterInstanceImpl final : public FNiagaraEmitterInstance
{
	...
	struct FEventInstanceData
	{
		TArray<FNiagaraScriptExecutionContext> EventExecContexts;
		...
		TArray<FNiagaraDataSet*> UpdateScriptEventDataSets;
		TArray<FNiagaraDataSet*> SpawnScriptEventDataSets;
		...
		TArray<FNiagaraEventHandlingInfo> EventHandlingInfo;
		int32 EventSpawnTotal = 0;
	};
```

逐行解释：`FNiagaraEmitterInstanceImpl`（`final`，不可再继承）是「有状态」发射器的实现，
持有事件处理数据（`FEventInstanceData`：每个事件的执行上下文、事件数据集、事件产生的粒子数等）；
5.8 新增的 Stateless（无状态）发射器则走 `Internal/Stateless/` 下的另一套实现
（`FNiagaraStatelessEmitterInstance`），两者都挂在 `FNiagaraEmitterInstance` 接口之下。

### 3.6 数据：FNiagaraDataSet / FNiagaraDataBuffer

粒子数据是「布局（DataSet）+ 帧缓冲（DataBuffer）」两层结构，位于 `Niagara/Classes/NiagaraDataSet.h`：

```cpp
// NiagaraDataSet.h:19-21, 85-100（摘选）
//Base class for objects in Niagara that are owned by one object but are then passed for reading to other objects, potentially on other threads.
//This class allows us to know if the object is being used so we do not overwrite it and to ensure it's lifetime so we do not access freed data.
class FNiagaraSharedObject
{
	...
	inline void AddRef() { check(!IsBeingWritten()); ReadRefCount++; }
	inline void Release() { check(IsBeingRead()); ReadRefCount--; }
	inline bool TryLock() { ... ReadRefCount.CompareExchange(Expected, INDEX_NONE); ... }
```

```cpp
/** Buffer containing one frame of Niagara simulation data. */
class FNiagaraDataBuffer : public FNiagaraSharedObject
{
	...
	NIAGARA_API void Allocate(uint32 NumInstances, bool bMaintainExisting = false);
	NIAGARA_API void AllocateGPU(FRHICommandListBase& RHICmdList, uint32 InNumInstances, ERHIFeatureLevel::Type FeatureLevel, const TCHAR* DebugSimName);
	NIAGARA_API void SwapGPU(FNiagaraDataBuffer* BufferToSwap);
	NIAGARA_API void KillInstance(uint32 InstanceIdx);
	NIAGARA_API void GPUCopyFrom(...);
	NIAGARA_API void PushCPUBuffersToGPU(...);
	NIAGARA_API void TransferGPUToCPUImmediate(...);
```

逐行解释：
- `FNiagaraSharedObject` 用原子 `ReadRefCount` 做读写仲裁：`INDEX_NONE`（-1）表示正在被写入，
  `>0` 表示正被读取；`TryLock` 用 `CompareExchange` 从 0 → `INDEX_NONE`，保证「无读者才允许写」。
  这样一帧数据可以被渲染线程/事件接收者安全引用（`TRefCountPtr`），而模拟线程不会覆盖仍在使用的缓冲。
- `FNiagaraDataBuffer` 是「一帧粒子数据」：按 Float/Int/Half 三类分量分块存储；`Allocate` 分配 CPU 侧，
  `AllocateGPU/SwapGPU` 管理 GPU 侧缓冲（CPU 模拟也可把结果推到 GPU 供渲染），
  `KillInstance` 支持持久 ID 系统下的粒子移除。

```cpp
// NiagaraDataSet.h:265-267, 279-291（摘选）
/**
General storage class for all per instance simulation data in Niagara.
*/
class FNiagaraDataSet
{
	...
	/** Initialize the data set with the compiled data */
	NIAGARA_API void Init(const FNiagaraDataSetCompiledData* InDataSetCompiledData, int32 DefaultNumBuffers=0);
	...
	/** Begins a new simulation pass and grabs a destination buffer. Returns the new destination data buffer. */
	NIAGARA_API FNiagaraDataBuffer& BeginSimulate(bool bResetDestinationData = true);
	/** Ends a simulation pass and sets the current simulation state. */
	NIAGARA_API void EndSimulate(bool SetCurrentData = true);
	...
	inline ENiagaraSimTarget GetSimTarget() const { return CompiledData->SimTarget; }
	inline bool RequiresPersistentIDs() const { return CompiledData->bRequiresPersistentIDs; }
	inline TArray<int32>& GetFreeIDTable() { return FreeIDsTable; }
	inline TArray<int32>& GetSpawnedIDsTable() { return SpawnedIDsTable; }
	inline FRWBuffer& GetGPUFreeIDs() { return GPUFreeIDs; }
```

逐行解释：
- `FNiagaraDataSet` 描述**布局**：变量列表、每个变量的分量偏移（`CompiledData`，编译期确定），
  以及一组数据缓冲（双缓冲/多缓冲）。`Init` 用编译数据初始化布局。
- `BeginSimulate()` 拿到「目标缓冲」（destination），`EndSimulate()` 把目标缓冲提升为「当前数据」
  （current），天然形成帧间双缓冲——这是理解 Niagara 数据流的核心模式。
- 持久 ID（`bRequiresPersistentIDs`）需要维护空闲 ID 表与新生 ID 表（`FreeIDsTable`/`SpawnedIDsTable`，
  GPU 侧为 `GPUFreeIDs`），保证粒子在事件/读取场景下有稳定身份。

### 3.7 CPU 执行上下文：FNiagaraScriptExecutionContext（VectorVM）

`Niagara/Classes/NiagaraScriptExecutionContext.h`（L129-210 摘选）：

```cpp
struct FNiagaraScriptExecutionContextBase
{
	UNiagaraScript* Script;
protected:
	TSharedPtr<const FNiagaraScriptRuntimeData> ScriptRuntimeData;
public:
	VectorVM::Runtime::FVectorVMState* VectorVMState = nullptr;
	/** Table of external function delegate handles called from the VM. */
	TArray<const FVMExternalFunction*> FunctionTable;
	/** Table of user ptrs to pass to the VM. */
	TArray<void*> UserPtrTable;
	/** Parameter store. Contains all data interfaces and a parameter buffer that can be used directly by the VM or GPU. */
	FNiagaraScriptInstanceParameterStore Parameters;
	TArray<FDataSetMeta, TInlineAllocator<2>> DataSetMetaTable;
	TArray<FNiagaraDataSetExecutionInfo, TInlineAllocator<2>> DataSetInfo;
	...
	virtual bool Init(FNiagaraSystemInstance* Instance, UNiagaraScript* InScript, TSharedPtr<const FNiagaraScriptRuntimeData> InScriptRuntimeData);
```

```cpp
struct FNiagaraScriptExecutionContext : public FNiagaraScriptExecutionContextBase
{
protected:
	/** Table of external function delegates unique to the instance. */
	TArray<FVMExternalFunction> LocalFunctionTable;
public:
	virtual bool Tick(class FNiagaraSystemInstance* Instance, ENiagaraSimTarget SimTarget) override;
};
```

逐行解释：
- CPU 模拟的脚本执行单位是「执行上下文」：它持有脚本运行时数据（`FNiagaraScriptRuntimeData`，
  包含编译产物 `FNiagaraVMExecutableData` 的运行时视图）、VectorVM 虚拟机状态（`FVectorVMState`，
  定义于引擎模块 `Engine/Source/Runtime/VectorVM/Public/VectorVM.h`）、外部函数表
  （`FunctionTable`/`LocalFunctionTable`，数据接口的函数通过 `FVMExternalFunction` 委托注册进来）、
  参数存储与数据集绑定（`DataSetInfo`）。
- `FNiagaraScriptExecutionContext::Tick(Instance, SimTarget)`：每帧驱动 VM 执行该脚本；
  `FNiagaraSystemScriptExecutionContext`（L224）是系统脚本（System Spawn/Update）的特化，
  额外处理跨实例的数据接口绑定。

### 3.8 GPU 执行：从 FNiagaraGPUSystemTick 到 FNiagaraGpuComputeDispatch

GPU 模拟不走 VectorVM，而是把粒子数据放 GPU 缓冲，由计算着色器驱动。游戏线程构造 Tick 描述、
渲染线程执行派发，相关类链如下：

`Niagara/Classes/NiagaraGPUSystemTick.h`（L56-60 注释，L22-47 摘选）：

```cpp
/*
	Represents all the information needed to dispatch a single tick of a FNiagaraSystemInstance.
	This object will be created on the game thread and passed to the renderthread.
	...
*/
struct FNiagaraComputeInstanceData
{
	...
	FNiagaraGpuSpawnInfo SpawnInfo;
	uint8* EmitterParamData = nullptr;
	uint8* ExternalParamData = nullptr;
	FNiagaraComputeExecutionContext* Context = nullptr;
	TArray<FNiagaraDataInterfaceProxy*> DataInterfaceProxies;
	TArray<FNiagaraDataInterfaceProxyRW*> IterationDataInterfaceProxies;
	TArray<FPerStageInfo, TInlineAllocator<1>> PerStageInfo;
	...
};
```

逐行解释：`FNiagaraGPUSystemTick` 及其内部的 `FNiagaraComputeInstanceData` 打包了
「一个系统实例的一帧 GPU 模拟」所需的一切：产生粒子数（`FNiagaraGpuSpawnInfo`）、参数数据、
执行上下文指针、每个数据接口的渲染线程代理（`FNiagaraDataInterfaceProxy`）、每个模拟阶段
（Simulation Stage）的迭代信息。注释明确：**游戏线程创建、渲染线程消费**。

`Niagara/Public/NiagaraSystemGpuComputeProxy.h`（L14-29, 58-59 摘选）：

```cpp
class FNiagaraSystemGpuComputeProxy
{
	friend class FNiagaraGpuComputeDispatch;
	...
	void AddToRenderThread(FNiagaraGpuComputeDispatchInterface* ComputeDispatchInterface);
	void RemoveFromRenderThread(FNiagaraGpuComputeDispatchInterface* ComputeDispatchInterface, bool bDeleteProxy);
	void QueueTick(FNiagaraGPUSystemTick& Tick);
	void ReleaseTicks(int32 NumTicksToRelease, bool bLastViewFamily);
	...
	TArray<FNiagaraComputeExecutionContext*>	ComputeContexts;
	TArray<FNiagaraGPUSystemTick>				PendingTicks;
};
```

逐行解释：每个有 GPU 发射器的系统实例在渲染线程侧有一个 `FNiagaraSystemGpuComputeProxy`，
`QueueTick` 把游戏线程提交的 `FNiagaraGPUSystemTick` 挂进 `PendingTicks`，等待渲染线程派发阶段消费。

`Niagara/Public/NiagaraGpuComputeDispatchInterface.h`（L29-52 摘选）：

```cpp
// Public API for Niagara's Compute Dispatcher
// This is generally used with DataInterfaces or Custom Renderers
class FNiagaraGpuComputeDispatchInterface : public FFXSystemInterface
{
	...
	static NIAGARA_API FNiagaraGpuComputeDispatchInterface* Get(class UWorld* World);
	...
	/** Add system instance proxy to the batcher for tracking. */
	virtual void AddGpuComputeProxy(FNiagaraSystemGpuComputeProxy* ComputeProxy) = 0;
	virtual void RemoveGpuComputeProxy(FNiagaraSystemGpuComputeProxy* ComputeProxy) = 0;
	...
	virtual bool AddSortedGPUSimulation(FRHICommandListBase& RHICmdList, struct FNiagaraGPUSortInfo& SortInfo) = 0;
```

逐行解释：这是 Niagara GPU 计算派发的**公共接口**，继承自 FX 系统的 `FFXSystemInterface`，
可通过 `Get(UWorld*)` 从世界拿到；数据接口与自定义渲染器通过它注册排序任务等。

其私有实现是 `Niagara/Private/NiagaraGpuComputeDispatch.h`（L85-123 摘选）：

```cpp
class FNiagaraGpuComputeDispatch : public FNiagaraGpuComputeDispatchInterface
{
	...
	virtual void AddGpuComputeProxy(FNiagaraSystemGpuComputeProxy* ComputeProxy) override;
	virtual void RemoveGpuComputeProxy(FNiagaraSystemGpuComputeProxy* ComputeProxy) override;
	...
	virtual void PreInitViews(FRDGBuilder& GraphBuilder, bool bAllowGPUParticleUpdate, ...) override;
	virtual void PreRender(FRDGBuilder& GraphBuilder, TConstStridedView<FSceneView> Views, FSceneUniformBuffer &SceneUniformBuffer, bool bAllowGPUParticleUpdate) override;
```

逐行解释：`FNiagaraGpuComputeDispatch` 是每场景一个的派发器，挂接渲染器的
`PreInitViews`/`PreRender` 等阶段（RDG 图构建期），把 `PendingTicks` 里的 GPU tick 按
`ENiagaraGpuComputeTickStage`（`NiagaraCommon.h`，如 PostOpaqueRender）分批派发计算着色器；
`FNiagaraGPUInstanceCountManager`（`Classes/NiagaraGPUInstanceCountManager.h`）负责粒子计数缓冲
（GPU 每帧写回粒子数，CPU 侧通过 readback 获取）。

### 3.9 一帧 CPU 运行流程（代码摘选 + 逐行解释）

`Niagara/Private/NiagaraSystemInstance.cpp` 的 `Tick_GameThread`（L2584-2657 摘选）：

```cpp
void FNiagaraSystemInstance::Tick_GameThread(float DeltaSeconds)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemInst_TickGT);
	LLM_SCOPE(ELLMTag::Niagara);
	...
	// We should have no pending async operations, but wait to be safe
	WaitForConcurrentTickAndFinalize(true);

	// If the attached component is marked pending kill the instance is no longer valid
	if ( GetAttachComponent() == nullptr )
	{
		Complete(true);
		return;
	}
	if (IsComplete())
	{
		return;
	}
	// If the interfaces have changed in a meaningful way, we need to potentially rebind and update the values.
	if (OverrideParameters->GetInterfacesDirty())
	{
		Reset(EResetMode::ReInit);
		...
		return;
	}
	CachedDeltaSeconds = DeltaSeconds;
	FixedBounds_CNC = FixedBounds_GT;
	TickInstanceParameters_GameThread(DeltaSeconds);
	TickDataInterfaces(DeltaSeconds, false);
	Age += DeltaSeconds;
	TickCount += 1;
	...
}
```

逐行解释：
- `WaitForConcurrentTickAndFinalize(true)`：先等上一帧的并发 tick 收尾，确保没有未决异步工作。
- 宿主组件失效 → `Complete(true)`；实例已完成 → 直接返回（不再模拟）。
- `OverrideParameters->GetInterfacesDirty()`：用户参数里的数据接口变了（蓝图运行时换 DI），
  需要 `Reset(EResetMode::ReInit)` 重建并重绑。
- 正常路径：缓存 DeltaSeconds → 更新实例参数（`TickInstanceParameters_GameThread`）→
  数据接口预模拟 tick（`TickDataInterfaces(DeltaSeconds, false)`）→ 累计 `Age`/`TickCount`。
- 之后控制器/系统模拟会调度 `Tick_Concurrent`（工作线程执行各发射器 `Tick`，内部跑
  `FNiagaraScriptExecutionContext` 的 VectorVM 脚本），最后 `FinalizeTick_GameThread` 收尾、
  提交 GPU tick（若有 GPU 发射器）。

## 四、数据接口剖析

### 4.1 基类：UNiagaraDataInterface

`Niagara/Classes/NiagaraDataInterface.h`（L582-604, 698, 882-890 摘选）：

```cpp
/** Base class for all Niagara data interfaces. */
UCLASS(abstract, EditInlineNew, MinimalAPI)
class UNiagaraDataInterface : public UNiagaraDataInterfaceBase
{
	...
#if WITH_EDITOR
	/** Generate the necessary HLSL to set up data when being added as a sim stage sim source. */
	virtual bool GenerateSetupHLSL(FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo, TConstArrayView<FNiagaraVariable> InArguments, bool bSpawnOnly, bool bPartialWrites, TArray<FText>& OutErrors, FString& OutHLSL) const { return false;}
	...
#endif
	...
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) { };
	...
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
	...
	virtual void GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const {};
```

逐行解释：
- `UCLASS(abstract, EditInlineNew)`：数据接口是**可内联编辑的抽象 UObject**，所有 DI 都是它的子类，
  在发射器/系统里作为 `UNiagaraDataInterface*` 属性内嵌（`EditInlineNew` 使编辑器可以创建实例）。
- `GetFunctions()` / `GetFunctionsInternal()`：向编译器申报本 DI 暴露给节点的**函数签名表**
  （如骨骼网格的 `GetBoneTransform`），编辑器的函数调用节点据此生成。
- `GetVMExternalFunction(...)`：CPU 侧把签名映射为实际的 `FVMExternalFunction` 委托
  （绑定到 `FNiagaraScriptExecutionContext::FunctionTable`），VM 执行到该函数时回调。
- `GenerateSetupHLSL` 等：GPU 侧生成 HLSL 声明（Setup/Teardown 阶段，供 Simulation Stage 使用）。

### 4.2 常用子类

| 数据接口（5.8 实测类名） | 头文件 | 典型用途 |
| --- | --- | --- |
| `UNiagaraDataInterfaceSkeletalMesh` | `Niagara/Classes/NiagaraDataInterfaceSkeletalMesh.h`（L700） | 采样骨骼网格：骨骼矩阵、蒙皮顶点、UV、三角面 |
| `UNiagaraDataInterfaceParticleRead` | `Niagara/Classes/NiagaraDataInterfaceParticleRead.h`（L12） | 粒子属性读取（读其他发射器的粒子缓冲） |
| `UNiagaraDataInterfaceNeighborGrid3D` | `Niagara/Classes/NiagaraDataInterfaceNeighborGrid3D.h` | 3D 邻居网格（邻域搜索，如流体） |
| `UNiagaraDataInterfaceRenderTarget2D` | `Niagara/Classes/NiagaraDataInterfaceRenderTarget2D.h` | 2D RenderTarget 读写（粒子画贴图） |
| `UNiagaraDataInterfaceGrid2DCollection` | `Niagara/Classes/NiagaraDataInterfaceGrid2DCollection.h` | 2D 网格集合（Grid2D 流体模拟） |
| `UNiagaraDataInterfaceCollisionQuery` | `Niagara/Classes/NiagaraDataInterfaceCollisionQuery.h` | 场景碰撞查询（`QuerySceneCollision` 等） |

`UNiagaraDataInterfaceParticleRead` 是 GPU 数据接口的典型（`Classes/NiagaraDataInterfaceParticleRead.h` L11-35 摘选）：

```cpp
UCLASS(EditInlineNew, Category = "ParticleRead", CollapseCategories, meta = (DisplayName = "Particle Attribute Reader"), MinimalAPI)
class UNiagaraDataInterfaceParticleRead : public UNiagaraDataInterfaceRWBase
{
	GENERATED_UCLASS_BODY()

	BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, )
		SHADER_PARAMETER(uint32,			IsLocalSpace)
		SHADER_PARAMETER(int,				NumSpawnedParticles)
		SHADER_PARAMETER(int,				SpawnedParticlesAcquireTag)
		SHADER_PARAMETER(uint32,			InstanceCountOffset)
		SHADER_PARAMETER(uint32,			ParticleStrideFloat)
		SHADER_PARAMETER(uint32,			ParticleStrideInt)
		SHADER_PARAMETER(uint32,			ParticleStrideHalf)
		...
		SHADER_PARAMETER_SRV(Buffer<int>,	SpawnedIDsBuffer)
		SHADER_PARAMETER_SRV(Buffer<int>,	IDToIndexTable)
		SHADER_PARAMETER_SRV(Buffer<float>,	InputFloatBuffer)
		SHADER_PARAMETER_SRV(Buffer<int>,	InputIntBuffer)
		SHADER_PARAMETER_SRV(Buffer<half>,	InputHalfBuffer)
	END_SHADER_PARAMETER_STRUCT()
```

逐行解释：
- 继承 `UNiagaraDataInterfaceRWBase`（定义于 `Classes/NiagaraDataInterfaceRW.h`）——读写型 DI 基类，
  支持 GPU 模拟阶段（Simulation Stage）的读写。
- `BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, ...)`：GPU 侧绑定的参数结构——粒子缓冲的
  float/int/half 三类分量的 stride、实例计数偏移、ID 表 SRV 等。CPU 模拟时这些变成普通读取路径；
  GPU 模拟时粒子数据就是这些 GPU 缓冲本身。
- `EmitterBinding` 属性决定读取哪个发射器（自身或命名发射器）。

### 4.3 数据接口的每实例数据生命周期

`Niagara/Private/NiagaraSystemInstance.cpp` 的 `TickDataInterfaces`（L1863-1888 摘选）：

```cpp
void FNiagaraSystemInstance::TickDataInterfaces(float DeltaSeconds, bool bPostSimulate)
{
	if (!GetSystem() || IsDisabled())
	{
		return;
	}
	bool bRebindVMFuncs = false;
	if (bPostSimulate)
	{
		for (int32 DIPairIndex : PostTickDataInterfaces)
		{
			TPair<TWeakObjectPtr<UNiagaraDataInterface>, int32>& Pair = DataInterfaceInstanceDataOffsets[DIPairIndex];
			if (UNiagaraDataInterface* Interface = Pair.Key.Get())
			{
				//Ideally when we make the batching changes, we can keep the instance data in big single type blocks ...
				if (Interface->PerInstanceTickPostSimulate(&DataInterfaceInstanceData[Pair.Value], this, DeltaSeconds))
				{
					// Destroy per instance data in order to not cause any errors on check(...) inside DIs when initializing
					Interface->DestroyPerInstanceData(&DataInterfaceInstanceData[Pair.Value], this);
					Interface->InitPerInstanceData(&DataInterfaceInstanceData[Pair.Value], this);
					bRebindVMFuncs = true;
				}
			}
		}
	}
```

逐行解释：
- 每帧模拟后，需要「后模拟 tick」的数据接口会收到 `PerInstanceTickPostSimulate(实例数据, 系统实例, DeltaSeconds)`。
- 若返回 `true` 表示「实例数据已失效」：先 `DestroyPerInstanceData` 销毁、再 `InitPerInstanceData` 重建，
  并置 `bRebindVMFuncs` 让外部函数表重绑（因为 `FVMExternalFunction` 里绑定的实例数据指针变了）。

## 五、Niagara 资产编译（NiagaraCompiler 简述）

Niagara 资产（系统/发射器/模块）在编辑器中编译：节点图 → 翻译成 HLSL/VM 中间码 → 分别产出
VectorVM 字节码（CPU 模拟）与计算着色器（GPU 模拟）。5.8 的编译器位于 **NiagaraEditor** 模块。

### 5.1 编译结果：FNiagaraCompileResults

`NiagaraEditor/Public/INiagaraCompiler.h`（L28-46 摘选）：

```cpp
/** Defines information about the results of a Niagara script compile. */
struct FNiagaraCompileResults
{
	/** Whether or not the script compiled successfully for VectorVM */
	bool bVMSucceeded = false;
	/** Whether or not the script compiled successfully for GPU compute */
	bool bComputeSucceeded = false;
	/** The actual final compiled data.*/
	TSharedPtr<FNiagaraVMExecutableData> Data;
	...
	/** Tracking any compilation warnings or errors that occur.*/
	TArray<FNiagaraCompileEvent> CompileEvents;
	uint32 NumErrors = 0;
	uint32 NumWarnings = 0;
	...
};
```

逐行解释：一次编译同时面向两条执行路径——`bVMSucceeded`（VectorVM 字节码，CPU）与
`bComputeSucceeded`（GPU compute），最终数据 `FNiagaraVMExecutableData` 包含字节码、参数布局、
数据接口信息等；`CompileEvents` 记录错误/警告（编辑器里显示为节点图上的红/黄标记）。

### 5.2 编译器实现：FHlslNiagaraCompiler

`NiagaraEditor/Private/NiagaraCompiler.h`（L19-48, 64 摘选）：

```cpp
struct FNiagaraCompilerJob
{
	TRefCountPtr<FShaderCompileJob> ShaderCompileJob;
	FNiagaraCompileResults CompileResults;
	double StartTime;
	FNiagaraTranslatorOutput TranslatorOutput;
	...
};

class FHlslNiagaraCompiler : public INiagaraCompiler
{
	...
	NIAGARAEDITOR_API virtual int32 CompileScript(const FStringView GroupName, const FNiagaraCompileOptions& InOptions, const FNiagaraTranslateResults& InTranslateResults, const FNiagaraTranslatorOutput& TranslatorOutput, const FString& TranslatedHLSL) override;
	NIAGARAEDITOR_API virtual uint32 CompileScriptVM(const FStringView GroupName, const FNiagaraCompileOptions& InOptions, const FNiagaraTranslateResults& InTranslateResults, const FNiagaraTranslatorOutput& TranslatorOutput, const FString& TranslatedHLSL, FNiagaraShaderType* NiagaraShaderType);
	NIAGARAEDITOR_API virtual int32 CreateShaderIntermediateData(...);
	...
private:
	TUniquePtr<FNiagaraCompilerJob> CompilationJob;
	...
};

class FNiagaraShaderMapCompiler
{
	...
	void CompileScript(const FNiagaraVMExecutableDataId& ScriptCompileId, FStringView SourceName, ...);
```

逐行解释：
- `FNiagaraCompilerJob`：一次编译任务——持有 `FShaderCompileJob`（Shader 编译任务句柄）、
  编译结果与翻译输出；`StartTime` 用于统计。
- `FHlslNiagaraCompiler : INiagaraCompiler`：编译器实现。`CompileScript` 提交 HLSL 给
  Shader 编译系统（SCW）；`CompileScriptVM` 负责从翻译输出生成 VectorVM 字节码（VM 指令）。
- `FNiagaraShaderMapCompiler`：负责把编译结果登记进 Niagara Shader 映射（按 `FNiagaraVMExecutableDataId`
  缓存，避免重复编译）。
- 异步编译任务在 `Niagara/Private/NiagaraAsyncCompile.h/.cpp`（`FNiagaraAsyncCompile`），
  编辑器保存资产后后台编译、完成后回调刷新。

### 5.3 翻译器：FNiagaraHlslTranslator

`NiagaraEditor/Private/NiagaraHlslTranslator.h`（L224-268 摘选）：

```cpp
class FNiagaraHlslTranslator : public INiagaraHlslTranslator
{
	...
	struct FDataSetAccessInfo
	{
		//Variables accessed.
		TArray<FNiagaraVariable> Variables;
		/** Code chunks relating to this access. */
		TArray<int32> CodeChunks;
	};
	...
	/** The set of all generated code chunks for this script. */
	TArray<FNiagaraCodeChunk> CodeChunks;
	/** Array of code chunks of each different type. */
	TArray<int32> ChunksByMode[(int32)ENiagaraCodeChunkMode::Num];
	...
	FDataSetAccessInfo InstanceRead;
	FDataSetAccessInfo InstanceWrite;
```

逐行解释：`FNiagaraHlslTranslator` 把节点图（`UNiagaraGraph`）遍历为**代码块**（`FNiagaraCodeChunk`），
跟踪每个数据集的读/写访问（`InstanceRead`/`InstanceWrite`、`FDataSetAccessInfo`），最终拼出完整 HLSL 字符串
（`FNiagaraTranslatorOutput`），再交给 `FHlslNiagaraCompiler` 编译成 VM 字节码与 GPU Shader。

完整编译链路（示意）：

```
模块/发射器节点图（UNiagaraGraph）
        │ FNiagaraHlslTranslator::Translate
        ▼
  HLSL + FNiagaraTranslatorOutput（代码块、数据集访问信息、参数布局）
        │ FHlslNiagaraCompiler::CompileScript / CompileScriptVM
        ├─► VectorVM 字节码（FNiagaraVMExecutableData，CPU 模拟）
        └─► FShaderCompileJob（GPU 计算着色器，SCW 异步编译）
                │ FNiagaraShaderMapCompiler
                ▼
        FNiagaraShader / FNiagaraScriptRuntimeData（运行时加载）
```

## 六、Mermaid 运行流程

### 6.1 组件生命周期与系统实例创建

```mermaid
flowchart TD
    A["关卡/蓝图生成 UNiagaraComponent"] --> B["OnRegister 注册（NiagaraComponent.cpp L2021）"]
    B --> C{"Activate / ActivateSystem 激活"}
    C -->|"首次激活"| D["InitializeSystem（L1204）"]
    D --> E["MakeShared&lt;FNiagaraSystemInstanceController, ThreadSafe&gt;（L1218）"]
    E --> F["Controller->Initialize(World, Asset, OverrideParameters, 组件, TickBehavior, ...)（L1219）"]
    F --> G["内部创建 FNiagaraSystemInstance 并 Init"]
    G --> H["按 Sim Target 创建发射器实例<br/>CPU: FNiagaraEmitterInstanceImpl / GPU: ComputeExecutionContext"]
    H --> I["注册进世界级 FNiagaraSystemSimulation（AddInstance）"]
    C -->|"再次激活"| J["ActivateInternal(重置系统模拟, 保留发射器)（L879）"]
    I --> K["每帧 Tick（见 6.2）"]
    K -->|"系统完成"| L["OnSystemComplete -> 蓝图 OnSystemFinished"]
```

### 6.2 每帧模拟（CPU / GPU 双路径）

```mermaid
flowchart TD
    S["FNiagaraSystemSimulation::Tick_GameThread（批处理，每批4实例）"] --> S2["FNiagaraSystemInstance::Tick_GameThread（L2584）"]
    S2 --> S3["等待上一帧并发 tick 收尾 WaitForConcurrentTickAndFinalize"]
    S3 --> S4["TickInstanceParameters_GameThread + TickDataInterfaces(预模拟)"]
    S4 --> S5["Tick_Concurrent（工作线程，TaskGraph 任务）"]
    S5 --> C1{"发射器 Sim Target"}
    C1 -->|"CPUSim"| C2["FNiagaraEmitterInstanceImpl::Tick"]
    C2 --> C3["FNiagaraScriptExecutionContext::Tick<br/>VectorVM 执行 Spawn/Update 字节码"]
    C3 --> C4["读写 FNiagaraDataSet 帧缓冲<br/>BeginSimulate/EndSimulate 双缓冲"]
    C4 --> C5["FNiagaraDataBuffer -> 渲染线程（CPU 数据上传 GPU）"]
    C1 -->|"GPUComputeSim"| G1["FNiagaraSystemInstance::GenerateAndSubmitGPUTick"]
    G1 --> G2["构造 FNiagaraGPUSystemTick -> SystemGpuComputeProxy::QueueTick"]
    G2 --> G3["渲染线程 FNiagaraGpuComputeDispatch<br/>PreInitViews/PreRender 阶段派发计算着色器"]
    G3 --> G4["GPU 写 FNiagaraDataBuffer（计数经 FNiagaraGPUInstanceCountManager readback）"]
    C5 --> F1["FinalizeTick_GameThread 收尾（游戏线程）"]
    G4 --> F1
    F1 --> F2["PostTick 回调 -> 渲染器/场景代理更新（FNiagaraSystemRenderData）"]
```

### 6.3 资产编译流程

```mermaid
flowchart LR
    N["Niagara 节点图<br/>（NiagaraEditor）"] --> T["FNiagaraHlslTranslator<br/>（Private/NiagaraHlslTranslator.h）"]
    T --> O["FNiagaraTranslatorOutput + HLSL"]
    O --> C["FHlslNiagaraCompiler<br/>（Private/NiagaraCompiler.h）"]
    C --> VM["VectorVM 字节码<br/>FNiagaraVMExecutableData"]
    C --> SH["FShaderCompileJob<br/>（SCW 异步）"]
    VM --> RT["FNiagaraScriptRuntimeData<br/>CPU 执行"]
    SH --> RT2["FNiagaraShader<br/>GPU 执行"]
```

## 七、与业务关联

- **性能定位先分路径**：CPU 模拟慢 → 查 `FNiagaraScriptExecutionContext`（VectorVM 脚本、外部函数表）与
  `FNiagaraDataSet` 的分配；GPU 模拟慢 → 查 `FNiagaraGpuComputeDispatch` 的派发阶段与
  `FNiagaraGPUInstanceCountManager` 的计数 readback；两者共用的问题（发射器太多）→
  查 `FNiagaraSystemSimulation` 的批处理与 `NiagaraSystemTickBatchSize`。
- **实例数多时优先批处理**：同一资产的大量实例共享一个 `FNiagaraSystemSimulation`，系统脚本按批执行；
  业务上应避免让每个实例都 Solo（Solo 系统不走批处理、手动 tick，仅调试/需要精确控制时使用——
  `GetSoloSystemInstance` 的注释印证了这一点）。
- **跨线程访问红线**：游戏代码不要直接拿 `FNiagaraSystemInstance*` 操作（`GetSystemInstance_Unsafe`
  已被标记危险），一律走组件 `GetSystemInstanceController()` → 控制器方法/`SetVariable`。
- **数据接口是性能分水岭**：`UNiagaraDataInterface` 的 CPU 路径（`GetVMExternalFunction`）与 GPU 路径
  （`GenerateSetupHLSL` + `FShaderParameters` SRV）决定数据能否留在 GPU。粒子读取（ParticleRead）、
  RenderTarget、Grid2D 等读写型 DI 继承 `UNiagaraDataInterfaceRWBase`，用于 GPU 端到端模拟；
  骨骼网格采样（SkeletalMesh DI）注意 `SourceMode` 与每实例数据的重建开销
  （`PerInstanceTickPostSimulate` 返回 true 时会销毁重建实例数据）。
- **固定 tick 与 Warmup**：`Initialize` 参数里的 `WarmupTickCount/WarmupTickDelta` 支持预热；
  固定 tick 相关字段在 `FNiagaraTickInfo`（`NiagaraSystemSimulation.h` L28）。
- **持久 ID 与事件**：需要事件/稳定粒子身份时开启持久 ID——`FNiagaraDataSet::RequiresPersistentIDs`
  与 `FreeIDsTable/SpawnedIDsTable` 会引入额外开销，非必要不开启。
- **参数更新**：蓝图 `Set Niagara Variable` 最终走控制器 `SetVariable` 重载 → 参数存储；
  实例参数是双缓冲的（`FlipParameterBuffers`），避免并发读写竞争，业务代码不应绕过它直接改参数缓冲。

## 八、FAQ

**Q1：为什么组件不直接持有 FNiagaraSystemInstance，而要隔一层 Controller？**
因为 5.x 起系统实例的 `Tick_Concurrent` 在工作线程执行，裸指针访问不安全。
`FNiagaraSystemInstanceController` 是线程安全的共享指针（`TSharedPtr<..., ESPMode::ThreadSafe>`）封装，
并提供 `ensure(IsValid())` 的转发层（`NIAGARA_SYSTEM_INSTANCE_CONTROLLER_SHIM`），
还保留了异步化兼容开关（`NIAGARA_SYSTEM_INSTANCE_CONTROLLER_ASYNC`），是否启用必须以本机 5.8 的宏定义和构建配置为准。

**Q2：FNiagaraDataSet 和 FNiagaraDataBuffer 有什么区别？**
`FNiagaraDataSet` 是「布局 + 缓冲池」：保存编译期确定的变量/分量布局（`FNiagaraDataSetCompiledData`）
与一组帧缓冲；`FNiagaraDataBuffer` 是「一帧实际粒子数据」（float/int/half 三块 + 计数），
模拟用 `BeginSimulate/EndSimulate` 在缓冲间切换，形成帧间双缓冲。

**Q3：CPU 模拟和 GPU 模拟的代码路径分别是什么？**
CPU：`FNiagaraSystemSimulation::Tick_Concurrent` → `FNiagaraEmitterInstanceImpl::Tick` →
`FNiagaraScriptExecutionContext::Tick` → VectorVM 字节码执行，数据在 `FNiagaraDataSet` 的 CPU 缓冲。
GPU：游戏线程 `GenerateAndSubmitGPUTick` 构造 `FNiagaraGPUSystemTick` →
`FNiagaraSystemGpuComputeProxy::QueueTick` → 渲染线程 `FNiagaraGpuComputeDispatch`
在 `PreInitViews/PreRender` 派发计算着色器。

**Q4：数据接口的「每实例数据」是什么？**
每个系统实例为每个 DI 分配一块内存（`DataInterfaceInstanceData`），由 `InitPerInstanceData` 初始化、
`PerInstanceTick(PostSimulate)` 更新；失效时销毁重建并重绑 VM 函数。查找入口
`FNiagaraSystemInstance::FindDataInterfaceInstanceData`。

**Q5：编译出来的东西到底是什么？**
一次编译产出两条产物：VectorVM 字节码（CPU 模拟执行，`FNiagaraVMExecutableData`）与
计算着色器（GPU 模拟执行）。`FNiagaraCompileResults::bVMSucceeded/bComputeSucceeded` 分别标记两者成败。

**Q6：5.8 里 FNiagaraSystemInstanceImpl 去哪了？**
已不存在（`findstr` 全树无命中）。5.3 拆分出的实现类在 5.8 合并回 `FNiagaraSystemInstance`
（类体直接包含实现），内部实现类移到 `Niagara/Internal/`（如 `FNiagaraEmitterInstanceImpl`）。

**Q7：NiagaraComponent.h 为什么在 Public/ 而不是 Classes/?**
5.8 中 UObject 类头文件分布有调整，`NiagaraComponent.h` 位于 `Niagara/Public/`，
而 `NiagaraDataSet.h`、`NiagaraEmitterInstance.h` 等仍在 `Classes/`；定位源码以本文「二、源码定位」
表格（已逐一 Test-Path 验证）为准，不要凭目录名猜测。

**Q8：如何判断一个发射器是 CPU 还是 GPU？**
资产/发射器的 Sim Target 对应 `ENiagaraSimTarget`（`NiagaraCommon.h` L176）：
`CPUSim` 与 `GPUComputeSim`；运行时可通过 `FNiagaraEmitterInstance::GetSimTarget()` 查询。

## 九、关联阅读

- [11-VFX与Niagara/01-Niagara粒子系统基础](../11-VFX与Niagara/01-Niagara粒子系统基础.md)：本文的
  概念前置（组件/系统/发射器/模块层级、Niagara 与 Cascade 的差异），建议先读。
- `12-引擎源码分析` 目录下其他源码剖析文章（渲染线程、Tick 系统等），可与本文的
  `PreInitViews/PreRender` 派发、`TaskGraph` 并发等章节互相印证。
- 本机源码速查：
  - 运行时：`C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\FX\Niagara\Source\Niagara`
  - 编译：`C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\FX\Niagara\Source\NiagaraEditor`
  - CPU 虚拟机：`C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\VectorVM\Public\VectorVM.h`

---

> 本文所有路径、类名、函数名与行号均于撰写当日在本机 UE 5.8（CL 55116800）源码上验证；
> 如引擎升级导致行号漂移，请以 `findstr /s /m` 重新定位。
