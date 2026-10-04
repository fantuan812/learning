---
type: Mechanism
title: "UE 引擎源码分析 09：网络复制与 RPC 源码剖析"
status: stable
verified: []
maturity: L2
updated: 2026-09-14
---

# UE 引擎源码分析 09：网络复制与 RPC 源码剖析
> 知识成熟度：L2（已按 UE5.8 源码基线全面补齐真实源码段落、FRepLayout 属性反射比较、UActorChannel 序列化与 RPC 派发全调用链）。
> 对应知识点：[06-网络同步/01 网络架构与复制基础](01-网络架构与复制基础.md)、[06-网络同步/02 RPC 与属性同步](02-RPC与属性同步.md)

> 以本机 UE5.8 源码为准，逐行深度剖析服务器端 `ServerReplicateActors` 调度循环、`UActorChannel::ReplicateActor` 数据打包、`FRepLayout` 脏属性比较、Bunch 网络流封装，以及客户端反序列化触发 `OnRep` 与 RPC 双向调用的完整底层源码实现。

> 本文所有代码均逐字摘自 UE 5.8 源码 checkout（`C:\Users\zhaozhiqi\Documents\GitHub\UnrealEngine`），行号以该 checkout 为准，安装版 5.8.0 可能相差数行。超过约 120 行的函数按「节选」处理，截断处标注 `// …（节选：省略 N 行）`。

---

## 元数据

- **版本基准**：UE 5.8.0 / CL 55116800 / 分支 `++UE5+Release-5.8`（本机安装目录 `C:\Program Files\Epic Games\UE_5.8\Engine`）。
- **源码依据**：
  - `Engine\Source\Runtime\Engine\Private\NetDriver.cpp`（`UNetDriver::TickFlush`、`ServerReplicateActors`、`ProcessRemoteFunction`）
  - `Engine\Source\Runtime\Engine\Private\DataChannel.cpp`（`UActorChannel::ReplicateActor`、`ReceivedBunch`、`ProcessBunch`）
  - `Engine\Source\Runtime\Engine\Private\RepLayout.cpp`（`FRepLayout::ReplicateProperties`、`ReceiveProperties`、`CallRepNotifies`）
  - `Engine\Source\Runtime\Engine\Public\Net\UnrealNetwork.h`（`DOREPLIFETIME` 系列宏、`FDoRepLifetimeParams`）
  - `Engine\Source\Runtime\CoreUObject\Public\UObject\CoreNetTypes.h`（`ELifetimeCondition` 复制条件）
  - `Engine\Source\Runtime\Engine\Private\Components\CharacterMovementComponent.cpp`（`ServerMove`、`ClientAdjustPosition`）
- **官方参考**：[Unreal Engine 属性复制与 RPC 官方文档](https://dev.epicgames.com/documentation/en-us/unreal-engine)。
- **源码依据（本轮补深新增，逐字摘录并经 ripgrep 核对存在）**：
  - `Engine\Source\Runtime\Engine\Private\NetDriver.cpp`（`ServerReplicateActors` 第 6277 行、`ServerReplicateActors_PrepConnections` 第 5198 行、`ServerReplicateActors_BuildConsiderList` 第 5303 行、`ServerReplicateActors_PrioritizeActors` 第 5528 行、`ServerReplicateActors_ProcessPrioritizedActorsRange` 第 5687 行、`ServerReplicateActors_ForConnection` 第 5938 行、`ProcessRemoteFunction` 第 8125 行、`InternalProcessRemoteFunctionPrivate` 第 3083 行、`ProcessRemoteFunctionForChannelPrivate` 第 3223 行）
  - `Engine\Source\Runtime\Engine\Private\DataReplication.cpp`（`FObjectReplicator::ReceivedRPC` 第 1323 行（本节引其第 1429~1453 行分支）、`CallProcessEventForReceivedRPC` 第 1479 行、`PostReceivedBunch` 第 1592 行、`QueueRemoteFunctionBunch` 第 2293 行、`CallRepNotifies` 第 2431 行、`QueuePropertyRepNotify` 第 2737 行、`net.MaxRPCPerNetUpdate` 第 38 行）
  - `Engine\Source\Runtime\Engine\Public\Net\DataReplication.h`（`FObjectReplicator` 类声明第 73 行、`CallRepNotifies` 声明第 221 行）
  - `Engine\Source\Runtime\Engine\Public\Net\RepLayout.h`（`FRepStateStaticBuffer` 第 376 行、`FReceivingRepState` 第 538 行、`FSendingRepState` 第 567 行、`FRepState` 第 671 行、`FRepParentCmd` 第 780 行、`FRepLayoutCmd` 第 856 行）
  - `Engine\Source\Runtime\Engine\Private\RepLayout.cpp`（`CompareProperties_r` 第 1648 行、`FRepLayout::ReplicateProperties` 第 1972 行、`SendProperties_r` 第 2767 行、`SendProperties` 第 2948 行、`ReceiveProperties` 第 3789 行、`CallRepNotifies` 第 4661 行）
  - `Engine\Source\Runtime\Net\Core\Public\Net\Core\PropertyConditions\RepChangedPropertyTracker.h`（`FRepChangedPropertyTracker` 第 22 行，5.8 已迁移至 NetCore 模块）
  - `Engine\Source\Runtime\CoreUObject\Private\UObject\ScriptCore.cpp`（`UObject::CallFunction` 第 1133 行、RPC 远程分支第 1155~1187 行）
  - `Engine\Source\Runtime\Engine\Private\Actor.cpp`（`AActor::CallRemoteFunction` 第 5668 行）
  - `Engine\Source\Runtime\Engine\Private\ActorReplication.cpp`（`AActor::GetNetPriority` 第 48 行、`AActor::IsWithinNetRelevancyDistance` 第 383 行）
  - `Engine\Source\Programs\Shared\EpicGames.UHT\Exporters\CodeGen\UhtHeaderCodeGeneratorCppFile.cs`（`_Validate` 派发代码生成第 3506~3513 行）
- **最后更新**：2026-09-14（补深：收录服务器复制主循环四阶段真实源码、`FObjectReplicator` 现代替身与客户端反序列化链路、`FRepLayout` 影子缓冲与 changelist 比较的真实实现、OnRep 派发时序，以及 RPC 从 `UObject::CallFunction` 到 `_Validate` 代码生成的完整链路；并修正既有段落中与 5.8 实际命名/行号不符的引用）。

---

## 概述与网络复制全链路时序

在 C/S 架构中，属性同步（Replication）与远程调用（RPC）在底层均依托于 **FOutBunch / FInBunch** 数据块传输：

```mermaid
sequenceDiagram
    autonumber
    participant ServerGame as 服务器业务逻辑
    participant NetDriver as UNetDriver (TickFlush)
    participant ActorCh as UActorChannel
    participant RepLayout as FRepLayout (属性布局)
    participant NetConn as UNetConnection
    participant ClientConn as 客户端 UNetConnection
    participant ClientCh as 客户端 UActorChannel
    participant ClientActor as 客户端 AActor

    Note over ServerGame,NetDriver: 服务器主循环帧末 (TickFlush)
    ServerGame->>ServerGame: 修改带 UPROPERTY(Replicated) 的变量
    NetDriver->>NetDriver: ServerReplicateActors(DeltaSeconds)
    NetDriver->>ActorCh: UActorChannel::ReplicateActor()
    ActorCh->>RepLayout: FRepLayout::ReplicateProperties (影子内存对比)
    RepLayout-->>ActorCh: 收集变化属性位图 (ChangeList) 并写入 FOutBunch
    ActorCh->>NetConn: SendBunch(FOutBunch, bForce=false)
    NetConn->>ClientConn: UDP 底层传输 (Socket Send)

    Note over ClientConn,ClientActor: 客户端收包循环 (TickDispatch)
    ClientConn->>ClientCh: ReceivedBunch(FInBunch)
    ClientCh->>RepLayout: FRepLayout::ReceiveProperties (反序列化)
    RepLayout->>ClientActor: 写入客户端本地内存
    RepLayout->>ClientActor: 触发 OnRep_XXX 回调函数
```

---

## 核心源码深入剖析零：服务器复制主循环 `UNetDriver::ServerReplicateActors`

> 本节为 2026-09-14 补深新增。`UActorChannel::ReplicateActor` 只负责「单个 Actor 到单个连接」的打包，真正决定「本帧哪些 Actor、以什么顺序、发给哪些连接」的是 `UNetDriver::ServerReplicateActors`。5.8 中该主循环由 4 个阶段函数串成，且全部位于 `NetDriver.cpp` 的 `#if WITH_SERVER_CODE`（第 5197~5936 行）保护块内。

### 1. 主循环入口与框架级调用点

`TickFlush` 在帧末驱动复制（`Engine\Source\Runtime\Engine\Private\NetDriver.cpp` 第 1186~1230 行，节选）：

```cpp
	if (IsServer() && (ClientConnections.Num() > 0 || UE::Net::Private::bUpdateReplicationSystemWithNoConnections) && !bSkipServerReplicateActors)
	{
		if (ReplicationSystem)
		{
			if (UEngineReplicationBridge* Bridge = ReplicationSystem->GetReplicationBridgeAs<UEngineReplicationBridge>())
			{
				// …（节选：省略 AUTORTFM 事务分支 9 行）
				{
					UE::Net::FDeferredReplicationSystemCalls::FlushDeferred(Bridge);
				}
			}
		}

		// Update all clients.
#if WITH_SERVER_CODE
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(ServerReplicateActors);

		if (ReplicationSystem)
		{
			InternalIrisUpdateTransactional(DeltaSeconds);
		}
		else if (ClientConnections.Num() > 0)
		{
			// …（节选：省略 bReplicateTransactionally 事务包裹分支）
```

可见 5.8 的入口有两条互斥路径：启用 Iris 时走 `InternalIrisUpdateTransactional` 并直接返回，传统的「按 Actor 遍历」路径只在 `ReplicationSystem == nullptr` 时执行。下文全部讨论传统路径。

### 2. `ServerReplicateActors` 主函数（节选）

以下代码逐字摘自 `Engine\Source\Runtime\Engine\Private\NetDriver.cpp`（第 6277 行起，函数共约 210 行，此处为节选）：

```cpp
int32 UNetDriver::ServerReplicateActors(float DeltaSeconds)
{
	SCOPE_CYCLE_COUNTER(STAT_NetServerRepActorsTime);

#if WITH_SERVER_CODE
	if ( ClientConnections.Num() == 0 )
	{
		return 0;
	}

	GetMetrics()->SetInt(UE::Net::Metric::NumReplicatedActors,0 );
	GetMetrics()->SetInt(UE::Net::Metric::NumReplicatedActorBytes, 0);

	// …（节选：省略 CSV_PROFILER_STATS 的 FScopedNetDriverStats 统计块 4 行）

	if (ReplicationDriver)
	{
		return ReplicationDriver->ServerReplicateActors(DeltaSeconds);
	}

	check( World );

	// Bump the ReplicationFrame value to invalidate any properties marked as "unchanged" for this frame.
	ReplicationFrame++;

	int32 Updated = 0;

	// …（节选：省略 UE_WITH_REMOTE_OBJECT_HANDLE && UE_AUTORTFM 的 bUseGranularTransactions 定义 5 行）

	const int32 NumClientsToTick = [this, DeltaSeconds, bUseGranularTransactions]()
		{
			// …（节选：省略事务分派分支 10 行）
			{
				return ServerReplicateActors_PrepConnections(DeltaSeconds);
			}
		}();


	if ( NumClientsToTick == 0 )
	{
		// No connections are ready this frame
		return 0;
	}

	AWorldSettings* WorldSettings = World->GetWorldSettings();

	bool bCPUSaturated		= false;
	float ServerTickTime	= GEngine->GetMaxTickRate( DeltaSeconds );
	if ( ServerTickTime == 0.f )
	{
		ServerTickTime = DeltaSeconds;
	}
	else
	{
		ServerTickTime	= 1.f/ServerTickTime;
		bCPUSaturated	= DeltaSeconds > 1.2f * ServerTickTime;
	}

	ensureMsgf(CurrentConsiderList.IsEmpty(), TEXT("UNetDriver::ServerReplicateActors: CurrentConsiderList isn't empty before starting replication."));

	CurrentConsiderList.Reserve( GetNetworkObjectList().GetActiveObjects().Num() );
	ON_SCOPE_EXIT
	{
		// Deallocate the array while the NetDriver doesn't need it.
		CurrentConsiderList.Empty();
	};

	// Build the consider list (actors that are ready to replicate)
	// …（节选：省略 bUseGranularTransactions 事务包裹分支 11 行）
	{
		ServerReplicateActors_BuildConsiderList(CurrentConsiderList, ServerTickTime);
	}

	TSet<UNetConnection*> ConnectionsToClose;

	FMemMark Mark( FMemStack::Get() );

	if (OnPreConsiderListUpdateOverride.IsBound())
	{
		OnPreConsiderListUpdateOverride.Execute({ DeltaSeconds, nullptr, bCPUSaturated }, Updated, CurrentConsiderList);
	}

	for ( int32 i=0; i < ClientConnections.Num(); i++ )
	{
		UNetConnection* Connection = ClientConnections[i];
		check(Connection);

		// net.DormancyValidate can be set to 2 to validate all dormant actors against last known state before going dormant
		if ( GNetDormancyValidate == 2 )
		{
			// …（节选：省略 dormant replicator 校验 lambda 12 行）
			Connection->ExecuteOnAllDormantReplicators(ValidateFunction);
		}

		// if this client shouldn't be ticked this frame
		if (i >= NumClientsToTick)
		{
			// …（节选：省略 bPendingNetUpdate 打标循环 18 行）
			// clear the time sensitive flag to avoid sending an extra packet to this connection
			Connection->TimeSensitive = false;
		}
		else if (Connection->ViewTarget)
		{
			UE::Net::FServerReplicateActors_ForConnectionParams Params =
			{
				.Connection = Connection,
				.ConnectionViewers = WorldSettings->ReplicationViewers,
				.DeltaSeconds = DeltaSeconds,
				.ConsiderList = CurrentConsiderList,
				.InOutUpdated = Updated,
				.bCPUSaturated = bCPUSaturated
			};

			// …（节选：省略事务分支 9 行）
			{
				ServerReplicateActors_ForConnection(Params);
			}
		}

		if (Connection->GetPendingCloseDueToReplicationFailure())
		{
			ConnectionsToClose.Add(Connection);
		}
	}
```

逐段解构：

1. **入口三条早退**：无客户端连接、`ReplicationDriver` 存在（ReplicationGraph 等接管）、`NumClientsToTick == 0`。这里的 `ReplicationDriver->ServerReplicateActors` 说明「主循环」本身是可替换的抽象，`Engine\Source\Runtime\Engine\Classes\Engine\ReplicationDriver.h` 定义了该接口。
2. **`ReplicationFrame++`（第 6303 行）**：这是「本帧内属性不值得再比较」的失效信号。`FRepChangelistState::CompareIndex` 与 `FSendingRepState::LastCompareIndex` 的比对依赖它——同一帧内重复比较会被 early-out 跳过。
3. **`ServerTickTime` 与 `bCPUSaturated`（第 6340~6350 行）**：`GEngine->GetMaxTickRate(DeltaSeconds)` 给出目标 tick 率；若实际 `DeltaSeconds` 超过目标帧时长的 **1.2 倍**，本帧被标记为 CPU 饱和。注意此处的 `bCPUSaturated` 只是传给 `ServerReplicateActors_ForConnection` 的参数。
4. **`CurrentConsiderList` 生命周期**：`ON_SCOPE_EXIT` 保证函数退出时清空，`ensureMsgf` 检查它进入时必须为空——这是「主循环不可重入 / 不可跨帧持有」的硬约束。
5. **每帧上限不在主函数里**：主函数不设「每帧最多复制 N 个 Actor」的硬截断，真正的上限来自 `PrepareConnections` 的客户端节流（下节）与 `ServerReplicateActors_ProcessPrioritizedActorsRange` 的**连接饱和**判定（见第 4 小节）。
6. **`NumClientsToTick` 之后的 `i >= NumClientsToTick` 分支**：本帧未被调度到的连接，其 ConsiderList 中相关 Actor 被打上 `bPendingNetUpdate = true`，从而在下一帧被强制重新考虑——这就是「客户端限流不会永久丢失复制」的机制。

### 3. 阶段一：`ServerReplicateActors_PrepConnections` —— 客户端节流

摘自 `Engine\Source\Runtime\Engine\Private\NetDriver.cpp`（第 5198 行起，节选）：

```cpp
int32 UNetDriver::ServerReplicateActors_PrepConnections( const float DeltaSeconds )
{
	int32 NumClientsToTick = ClientConnections.Num();

	// by default only throttle update for listen servers unless specified on the commandline
	static bool bForceClientTickingThrottle = FParse::Param( FCommandLine::Get(), TEXT( "limitclientticks" ) );
	if ( (bForceClientTickingThrottle || GetNetMode() == NM_ListenServer) && bTickingThrottleEnabled )
	{
		// determine how many clients to tick this frame based on GEngine->NetTickRate (always tick at least one client), double for lan play
		// FIXME: DeltaTimeOverflow is a static, and will conflict with other running net drivers, we investigate storing it on the driver itself!
		static float DeltaTimeOverflow = 0.f;
		// updates are doubled for lan play
		static bool LanPlay = FParse::Param( FCommandLine::Get(), TEXT( "lanplay" ) );
		//@todo - ideally we wouldn't want to tick more clients with a higher deltatime as that's not going to be good for performance and probably saturate bandwidth in hitchy situations, maybe
		// come up with a solution that is greedier with higher framerates, but still won't risk saturating server upstream bandwidth
		float ClientUpdatesThisFrame = GEngine->NetClientTicksPerSecond * ( DeltaSeconds + DeltaTimeOverflow ) * ( LanPlay ? 2.f : 1.f );
		NumClientsToTick = FMath::Min<int32>( NumClientsToTick, FMath::TruncToInt( ClientUpdatesThisFrame ) );
		//UE_LOGF(LogNet, Log, "%2.3f: Ticking %d clients this frame, %2.3f/%2.4f",GetWorld()->GetTimeSeconds(),NumClientsToTick,DeltaSeconds,ClientUpdatesThisFrame);
		if ( NumClientsToTick == 0 )
		{
			// if no clients are ticked this frame accumulate the time elapsed for the next frame
			DeltaTimeOverflow += DeltaSeconds;
			return 0;
		}
		DeltaTimeOverflow = 0.f;
	}

	if( NetCmds::MaxConnectionsToTickPerServerFrame->GetInt() > 0 )
	{
		NumClientsToTick = FMath::Min( ClientConnections.Num(), NetCmds::MaxConnectionsToTickPerServerFrame->GetInt() );
	}
```

1. **首个真正的「每帧上限」就是这里**：`GEngine->NetClientTicksPerSecond × DeltaSeconds` 给出本帧允许 tick 的客户端数量，`FMath::TruncToInt` 向下取整。不足 1 个连接时把时间累积进 `DeltaTimeOverflow`（函数内 `static`），下一帧补上——因此平均速率正确，但单帧存在抖动。
2. **`limitclientticks` / listen server**：该节流默认只对 listen server 生效（或命令行强制），dedicated server 不节流。
3. **`NetCmds::MaxConnectionsToTickPerServerFrame`**：显式配置项，`> 0` 时对所有 net mode 生效，与上一条件取更小值。
4. 该函数在末尾返回 `bFoundReadyConnection ? NumClientsToTick : 0`（第 5300 行）——「没有任何 ready 连接」时直接归零，主函数随即早退。

### 4. 阶段二：`ServerReplicateActors_BuildConsiderList` —— 候选集与自适应频率

摘自 `Engine\Source\Runtime\Engine\Private\NetDriver.cpp`（第 5303 行起，节选）：

```cpp
void UNetDriver::ServerReplicateActors_BuildConsiderList( TArray<FNetworkObjectInfo*>& OutConsiderList, const float ServerTickTime )
{
	SCOPE_CYCLE_COUNTER( STAT_NetConsiderActorsTime );

	UE_LOGF( LogNetTraffic, Log, "ServerReplicateActors_BuildConsiderList, Building ConsiderList at WorldTime: %f ServerTickTime: %f", World->GetTimeSeconds(), ServerTickTime );

	int32 NumInitiallyDormant = 0;

	const bool bUseAdapativeNetFrequency = IsAdaptiveNetUpdateFrequencyEnabled();

	TArray<AActor*> ActorsToRemove;

	for ( const TSharedPtr<FNetworkObjectInfo>& ObjectInfo : GetNetworkObjectList().GetActiveObjects() )
	{
		FNetworkObjectInfo* ActorInfo = ObjectInfo.Get();

		if ( !ActorInfo->bPendingNetUpdate && World->TimeSeconds <= ActorInfo->NextUpdateTime )
		{
			UE_LOGF(LogNetTraffic, VeryVerbose, "Skipping actor: %ls. bPendingNetUpdate: %ls | NextUpdateTime: %f (currently %f)", *GetNameSafe(ActorInfo->Actor), ActorInfo->bPendingNetUpdate?TEXT("true"):TEXT("false"),ActorInfo->NextUpdateTime, World->TimeSeconds);
			continue;		// It's not time for this actor to perform an update, skip it
		}

		AActor* Actor = ActorInfo->Actor;

		if ( Actor->IsPendingKillPending() )
		{
			// …（节选：省略 PendingKillPending 告警 8 行）
			ActorsToRemove.Add( Actor );
			continue;
		}

		if ( Actor->GetRemoteRole() == ROLE_None )
		{
			UE_LOGF(LogNetTraffic, VeryVerbose, "Skipping Actor %ls - it has no remote role!", *Actor->GetName());
			ActorsToRemove.Add( Actor );
			continue;
		}

		// This actor may belong to a different net driver, make sure this is the correct one
		// (this can happen when using beacon net drivers for example)
		if (Actor->GetNetDriverName() != NetDriverName)
		{
			// …（节选：省略错误日志 5 行）
			continue;
		}

		// Verify the actor is actually initialized (it might have been intentionally spawn deferred until a later frame)
		if ( !Actor->IsActorInitialized() )
		{
			UE_LOGF(LogNetTraffic, VeryVerbose, "Skipping Actor %ls - it not initialized!", *Actor->GetName());
			continue;
		}

		// Don't send actors that may still be streaming in or out
		ULevel* Level = Actor->GetLevel();
		if ( Level->HasVisibilityChangeRequestPending() || Level->bIsAssociatingLevel )
		{
			UE_LOGF(LogNetTraffic, VeryVerbose, "Skipping Actor %ls - it has a pending visibility change!", *Actor->GetName());
			continue;
		}

		if ( IsDormInitialStartupActor(Actor) )
		{
			// This stat isn't that useful in its current form when using NetworkActors list
			// We'll want to track initially dormant actors some other way to track them with stats
			SCOPE_CYCLE_COUNTER( STAT_NetInitialDormantCheckTime );
			NumInitiallyDormant++;
			ActorsToRemove.Add( Actor );
			UE_LOGF(LogNetTraffic, VeryVerbose, "Skipping Actor %ls - its initially dormant!", *Actor->GetName() );
			continue;
		}
```

以及同函数内的自适应频率计算（第 5383~5410 行，逐字）：

```cpp
		// Set defaults if this actor is replicating for first time
		if ( ActorInfo->LastNetReplicateTime == 0 )
		{
			UE_LOGF(LogNetTraffic, VeryVerbose, "Setting Actor %ls - initial replication update time!", *Actor->GetName());
			ActorInfo->LastNetReplicateTime = World->TimeSeconds;
			ActorInfo->OptimalNetUpdateDelta = 1.0f / Actor->GetNetUpdateFrequency();
		}

		const float ScaleDownStartTime = 2.0f;
		const float ScaleDownTimeRange = 5.0f;

		const float LastReplicateDelta = World->TimeSeconds - ActorInfo->LastNetReplicateTime;

		if ( LastReplicateDelta > ScaleDownStartTime )
		{
			if ( Actor->GetMinNetUpdateFrequency() == 0.0f )
			{
				Actor->SetMinNetUpdateFrequency(2.0f);
			}

			// Calculate min delta (max rate actor will update), and max delta (slowest rate actor will update)
			const float MinOptimalDelta = 1.0f / Actor->GetNetUpdateFrequency();									  // Don't go faster than NetUpdateFrequency
			const float MaxOptimalDelta = FMath::Max( 1.0f / Actor->GetMinNetUpdateFrequency(), MinOptimalDelta ); // Don't go slower than MinNetUpdateFrequency (or NetUpdateFrequency if it's slower)

			// Interpolate between MinOptimalDelta/MaxOptimalDelta based on how long it's been since this actor actually sent anything
			const float Alpha = FMath::Clamp( ( LastReplicateDelta - ScaleDownStartTime ) / ScaleDownTimeRange, 0.0f, 1.0f );
			ActorInfo->OptimalNetUpdateDelta = FMath::Lerp( MinOptimalDelta, MaxOptimalDelta, Alpha );
		}
```

1. **第一道门是时间（`NextUpdateTime`）**，不是相关性。候选集构建完全不做距离/可见性计算——这也是 5.8 与早期版本的重要差别：相关性判定被提前到优先级阶段（见下节注释「Relevancy is now cheap」）。
2. **剔除条件依次为**：`IsPendingKillPending()`、`GetRemoteRole() == ROLE_None`（本端没有远端角色，例如客户端上纯本地 Actor）、`GetNetDriverName() != NetDriverName`（beacon 等多 NetDriver 场景）、`!IsActorInitialized()`（deferred spawn）、流关卡可见性变更中、`IsDormInitialStartupActor()`（初始休眠 Actor 在此被移出活动列表）。
3. **自适应频率（`IsAdaptiveNetUpdateFrequencyEnabled()`）**：距上次成功复制超过 `ScaleDownStartTime = 2.0s` 后开始降频，在 `[1/NetUpdateFrequency, 1/MinNetUpdateFrequency]` 之间按 `Alpha = Clamp((LastReplicateDelta - 2.0) / 5.0, 0, 1)` 线性插值出 `OptimalNetUpdateDelta`。`MinNetUpdateFrequency` 未设置时被就地设为 **2.0**。这就是「长期不走网的 Actor 自动降到最低 2Hz」的实现。
4. 该函数不修改 Actor 的复制状态，只产出 `OutConsiderList` 与 `ActorsToRemove`。

### 5. 阶段三：`ServerReplicateActors_PrioritizeActors` —— 相关性、休眠与优先级

相关性/休眠/优先级在 5.8 中由 4 个文件局部 `static FORCEINLINE_DEBUGGABLE` 辅助函数 + `FActorPriority` 构造函数承担，**函数内没有 lambda**。以下辅助函数摘自 `Engine\Source\Runtime\Engine\Private\NetDriver.cpp`（第 5457~5526 行，逐字）：

```cpp
// Returns true if this actor should replicate to *any* of the passed in connections
static FORCEINLINE_DEBUGGABLE bool IsActorRelevantToConnection( const AActor* Actor, const TArray<FNetViewer>& ConnectionViewers )
{
	for ( int32 viewerIdx = 0; viewerIdx < ConnectionViewers.Num(); viewerIdx++ )
	{
		if ( Actor->IsNetRelevantFor( ConnectionViewers[viewerIdx].InViewer, ConnectionViewers[viewerIdx].ViewTarget, ConnectionViewers[viewerIdx].ViewLocation ) )
		{
			return true;
		}
	}

	return false;
}

// Returns true if this actor is owned by, and should replicate to *any* of the passed in connections
static FORCEINLINE_DEBUGGABLE UNetConnection* IsActorOwnedByAndRelevantToConnection( const AActor* Actor, const TArray<FNetViewer>& ConnectionViewers, bool& bOutHasNullViewTarget )
{
	const AActor* ActorOwner = Actor->GetNetOwner();

	bOutHasNullViewTarget = false;

	for ( int i = 0; i < ConnectionViewers.Num(); i++ )
	{
		UNetConnection* ViewerConnection = ConnectionViewers[i].Connection;

		if ( ViewerConnection->ViewTarget == nullptr )
		{
			bOutHasNullViewTarget = true;
		}

		if ( ActorOwner == ViewerConnection->PlayerController ||
			 ( ViewerConnection->PlayerController && ActorOwner == ViewerConnection->PlayerController->GetPawn() ) ||
			 (ViewerConnection->ViewTarget && ViewerConnection->ViewTarget->IsRelevancyOwnerFor( Actor, ActorOwner, ViewerConnection->OwningActor ) ) )
		{
			return ViewerConnection;
		}
	}

	return nullptr;
}

// Returns true if this actor is considered dormant (and all properties caught up) to the current connection
static FORCEINLINE_DEBUGGABLE bool IsActorDormant( FNetworkObjectInfo* ActorInfo, const TWeakObjectPtr<UNetConnection>& Connection )
{
	// If actor is already dormant on this channel, then skip replication entirely
	return ActorInfo->DormantConnections.Contains( Connection );
}

// Returns true if this actor wants to go dormant for a particular connection
static FORCEINLINE_DEBUGGABLE bool ShouldActorGoDormant( AActor* Actor, const TArray<FNetViewer>& ConnectionViewers, UActorChannel* Channel, const float Time, const bool bLowNetBandwidth )
{
	if ( Actor->NetDormancy <= DORM_Awake || !Channel || Channel->bPendingDormancy || Channel->Dormant )
	{
		// Either shouldn't go dormant, or is already dormant
		return false;
	}

	if ( Actor->NetDormancy == DORM_DormantPartial )
	{
		for ( int32 viewerIdx = 0; viewerIdx < ConnectionViewers.Num(); viewerIdx++ )
		{
			if ( !Actor->GetNetDormancy( ConnectionViewers[viewerIdx].ViewLocation, ConnectionViewers[viewerIdx].ViewDir, ConnectionViewers[viewerIdx].InViewer, ConnectionViewers[viewerIdx].ViewTarget, Channel, Time, bLowNetBandwidth ) )
			{
				return false;
			}
		}
	}

	return true;
}
```

`FActorPriority` 的两个构造函数（第 5152~5190 行，逐字）——**这就是优先级的真实公式**：

```cpp
FActorPriority::FActorPriority(UNetConnection* InConnection, UActorChannel* InChannel, FNetworkObjectInfo* InActorInfo, const TArray<struct FNetViewer>& Viewers, bool bLowBandwidth)
	: ActorInfo(InActorInfo), Channel(InChannel), DestructionInfo(NULL)
{
	const float Time = Channel ? (InConnection->Driver->GetElapsedTime() - Channel->LastUpdateTime) : InConnection->Driver->SpawnPrioritySeconds;
	// take the highest priority of the viewers on this connection
	Priority = 0;
	for (int32 i = 0; i < Viewers.Num(); i++)
	{
		Priority = FMath::Max<int32>(Priority, FMath::RoundToInt(65536.0f * ActorInfo->Actor->GetNetPriority(Viewers[i].ViewLocation, Viewers[i].ViewDir, Viewers[i].InViewer, Viewers[i].ViewTarget, InChannel, Time, bLowBandwidth)));
	}
}

FActorPriority::FActorPriority(UNetConnection* InConnection, FActorDestructionInfo * Info, const TArray<FNetViewer>& Viewers )
	: ActorInfo(NULL), Channel(NULL), DestructionInfo(Info)
{

	Priority = 0;

	for (int32 i = 0; i < Viewers.Num(); i++)
	{
		float Time  = InConnection->Driver->SpawnPrioritySeconds;

		FVector Dir = DestructionInfo->DestroyedPosition - Viewers[i].ViewLocation;
		float DistSq = Dir.SizeSquared();

		// adjust priority based on distance and whether actor is in front of viewer
		if ( (Viewers[i].ViewDir | Dir) < 0.f )
		{
			if ( DistSq > NEARSIGHTTHRESHOLDSQUARED )
				Time *= 0.2f;
			else if ( DistSq > CLOSEPROXIMITYSQUARED )
				Time *= 0.4f;
		}
		else if ( DistSq > MEDSIGHTTHRESHOLDSQUARED )
			Time *= 0.4f;

		Priority = FMath::Max<int32>(Priority, 65536.0f * Time);
	}
}
```

优先级排程主体摘自 `Engine\Source\Runtime\Engine\Private\NetDriver.cpp`（第 5528 行起，函数共 152 行，此处为前半节选）：

```cpp
int32 UNetDriver::ServerReplicateActors_PrioritizeActors( UNetConnection* Connection, const TArray<FNetViewer>& ConnectionViewers, const TArray<FNetworkObjectInfo*>& ConsiderList, const bool bCPUSaturated, FActorPriority*& OutPriorityList, FActorPriority**& OutPriorityActors )
{
	SCOPE_CYCLE_COUNTER( STAT_NetPrioritizeActorsTime );

	// Get list of visible/relevant actors.

	NetTag++;

	// Set up to skip all sent temporary actors
	for ( int32 j = 0; j < Connection->SentTemporaries.Num(); j++ )
	{
		Connection->SentTemporaries[j]->NetTag = NetTag;
	}

	// Make list of all actors to consider.
	check( World == Connection->OwningActor->GetWorld() );

	int32 FinalSortedCount = 0;
	int32 DeletedCount = 0;

	// Make weak ptr once for IsActorDormant call
	TWeakObjectPtr<UNetConnection> WeakConnection(Connection);

	const int32 MaxSortedActors = ConsiderList.Num() + DestroyedStartupOrDormantActors.Num();
	if ( MaxSortedActors > 0 )
	{
		OutPriorityList = new ( FMemStack::Get(), MaxSortedActors ) FActorPriority;
		OutPriorityActors = new ( FMemStack::Get(), MaxSortedActors ) FActorPriority*;

		check( World == Connection->ViewTarget->GetWorld() );

		AGameNetworkManager* const NetworkManager = World->NetworkManager;
		const bool bLowNetBandwidth = NetworkManager ? NetworkManager->IsInLowBandwidthMode() : false;

		for ( FNetworkObjectInfo* ActorInfo : ConsiderList )
		{
#if UE_WITH_REMOTE_OBJECT_HANDLE
			// If we can run transactions entries in the ConsiderList might become invalid
			if (bReplicateTransactionally && !ActorInfo->WeakActor.IsValid())
			{
				continue;
			}
#endif

			AActor* Actor = ActorInfo->Actor;

			UActorChannel* Channel = Connection->FindActorChannelRef( ActorInfo->WeakActor );

			// Skip actor if not relevant and theres no channel already.
			// Historically Relevancy checks were deferred until after prioritization because they were expensive (line traces).
			// Relevancy is now cheap and we are dealing with larger lists of considered actors, so we want to keep the list of
			// prioritized actors low.
			if (!Channel)
			{
				if (!IsLevelInitializedForActor(Actor, Connection))
				{
					// If the level this actor belongs to isn't loaded on client, don't bother sending
					continue;
				}

				if (!IsActorRelevantToConnection(Actor, ConnectionViewers))
				{
					// If not relevant (and we don't have a channel), skip
					continue;
				}
			}

			UNetConnection* PriorityConnection = Connection;

			if ( Actor->bOnlyRelevantToOwner )
			{
				// This actor should be owned by a particular connection, see if that connection is the one passed in
				bool bHasNullViewTarget = false;

				PriorityConnection = IsActorOwnedByAndRelevantToConnection( Actor, ConnectionViewers, bHasNullViewTarget );
```

同函数后半段的结构与结尾（第 5604~5679 行，节选）：

```cpp
			// …（节选：省略 PriorityConnection == nullptr 时的通道关闭与 continue 13 行）

			else if ( GSetNetDormancyEnabled != 0 )
			{
				// …（节选：省略 IsActorDormant / ShouldActorGoDormant 分支 17 行）
			}

			if ( Actor->NetTag != NetTag )
			{
				UE_LOGF( LogNetTraffic, Log, "Consider %ls [%d/%d] Rank: %d NetTag: %d ChannelLastUpdateTime: %f Priority: %f", *Actor->GetFullName(), FinalSortedCount, MaxSortedActors, FinalSortedCount, Actor->NetTag, (float)(Channel ? Channel->LastUpdateTime : 0.0), (float)(Actor ? Actor->GetNetPriority(ConnectionViewers.Num() ? ConnectionViewers[0].ViewLocation : FVector::ZeroVector, FVector::ZeroVector, nullptr, nullptr, Channel, 0.f) : 0.f) );

				Actor->NetTag = NetTag;

				// Add to the priority list
				OutPriorityList[FinalSortedCount] = FActorPriority( PriorityConnection, Channel, ActorInfo, ConnectionViewers, bLowNetBandwidth );
				OutPriorityActors[FinalSortedCount] = &OutPriorityList[FinalSortedCount];

				FinalSortedCount++;
			}
		}

		// …（节选：省略 DestroyedStartupOrDormantActors 删除项入列 10 行）
			DeletedCount++;
		}

		// Sort by priority
		Algo::SortBy(MakeArrayView(OutPriorityActors, FinalSortedCount), &FActorPriority::Priority, TGreater<>());
	}

	UE_LOGF( LogNetTraffic, Log, "ServerReplicateActors_PrioritizeActors: Potential %04i ConsiderList %03i FinalSortedCount %03i", MaxSortedActors, ConsiderList.Num(), FinalSortedCount );

	// Setup stats
	GetMetrics()->SetInt(UE::Net::Metric::PrioritizedActors, FinalSortedCount);
	GetMetrics()->SetInt(UE::Net::Metric::NumRelevantDeletedActors, DeletedCount);

	return FinalSortedCount;
}
```

1. **相关性判定位置**：`if (!Channel)` 且 `!IsActorRelevantToConnection(...)` 才跳过。**已存在通道的 Actor 不再做相关性剔除**——它进入候选后由阶段四用「最近相关性窗口」决定是否关闭通道。源码注释明确写出这是相对早期版本的优化：「Historically Relevancy checks were deferred until after prioritization because they were expensive (line traces).」
2. **`bOnlyRelevantToOwner`**：只有 `IsActorOwnedByAndRelevantToConnection` 返回的连接才作为 `PriorityConnection`；返回 `nullptr` 时（且非 `bHasNullViewTarget`）会尝试按 `RelevantTimeout` 关闭通道。
3. **休眠在这里被消费**：`GSetNetDormancyEnabled != 0` 时，`IsActorDormant`（查 `FNetworkObjectInfo::DormantConnections`）为真则 `continue`，整条 Actor 完全不参与本连接本帧复制；`ShouldActorGoDormant` 为真则 `Channel->StartBecomingDormant()`。
4. **`NetTag` 去重**：`SentTemporaries` 先被打上本帧 `NetTag`，随后 `Actor->NetTag != NetTag` 才入列——防止同一 Actor 被重复排程。
5. **排序**：`Algo::SortBy(..., TGreater<>())` 按 `Priority` 降序。优先级取自 `AActor::GetNetPriority`（`Engine\Source\Runtime\Engine\Private\ActorReplication.cpp` 第 48~92 行），其返回值在 `FActorPriority` 构造时被 `RoundToInt(65536.0f * ...)` 定点化。
6. **注意**：形参 `bCPUSaturated` 在该函数体内**未被使用**（已 rg 核对），CPU 饱和的实际作用点在阶段四的 `bIgnoreSaturation`。

### 6. 阶段四：`ServerReplicateActors_ProcessPrioritizedActorsRange` —— 饱和、通道与 `ReplicateActor`

`ServerReplicateActors_ProcessPrioritizedActors`（第 5681 行）在 5.3 起被标记 `UE_DEPRECATED`，5.8 中它只是 5 行的转发壳，真实实现在 `..._Range`。摘录其开头（`Engine\Source\Runtime\Engine\Private\NetDriver.cpp` 第 5687 行起，函数共 208 行，此处为节选）：

```cpp
int32 UNetDriver::ServerReplicateActors_ProcessPrioritizedActorsRange( UNetConnection* Connection, const TArray<FNetViewer>& ConnectionViewers, FActorPriority** PriorityActors, const TInterval<int32>& ActorsIndexRange, int32& OutUpdated, bool bIgnoreSaturation )
{
	SCOPE_CYCLE_COUNTER(STAT_NetProcessPrioritizedActorsTime);

	int32 ActorUpdatesThisConnection		= 0;
	int32 ActorUpdatesThisConnectionSent	= 0;
	int32 FinalRelevantCount				= 0;

	if (!Connection->IsNetReady() && !bIgnoreSaturation)
	{
		GNumSaturatedConnections++;
		// Connection saturated, don't process any actors
		return 0;
	}

	for ( int32 j = ActorsIndexRange.Min; j < ActorsIndexRange.Min + ActorsIndexRange.Max; j++ )
	{
		FNetworkObjectInfo*	ActorInfo = PriorityActors[j]->ActorInfo;

		// Deletion entry
		if ( ActorInfo == NULL && PriorityActors[j]->DestructionInfo )
		{
			// Make sure client has streaming level loaded
			if ( PriorityActors[j]->DestructionInfo->StreamingLevelName != NAME_None && !Connection->ClientVisibleLevelNames.Contains( PriorityActors[j]->DestructionInfo->StreamingLevelName ) )
			{
				// This deletion entry is for an actor in a streaming level the connection doesn't have loaded, so skip it
				continue;
			}

			FinalRelevantCount++;
			UE_LOGF( LogNetTraffic, Log, "Server replicate actor creating destroy channel for NetGUID <%ls,%ls> Priority: %d", *PriorityActors[j]->DestructionInfo->NetGUID.ToString(), *PriorityActors[j]->DestructionInfo->PathName, PriorityActors[j]->Priority );

			SendDestructionInfo(Connection, PriorityActors[j]->DestructionInfo);

			Connection->RemoveDestructionInfo( PriorityActors[j]->DestructionInfo );		// Remove from connections to-be-destroyed list (close bunch of reliable, so it will make it there)
			continue;
		}

		// …（节选：省略非 Shipping 下的 net.PackageMap.DebugObject 调试块 17 行）

		// Normal actor replication
		UActorChannel* Channel = PriorityActors[j]->Channel;
		UE_LOGF( LogNetTraffic, Log, " Maybe Replicate %ls", ActorInfo ? *ActorInfo->Actor->GetName() : TEXT("None") );
		if ( !Channel || Channel->Actor ) //make sure didn't just close this channel
		{
			AActor* Actor = ActorInfo->Actor;
			bool bIsRelevant = false;

			const bool bLevelInitializedForActor = IsLevelInitializedForActor( Actor, Connection );

			// only check visibility on already visible actors every 1.0 + 0.5R seconds or every RelevantTimeout if it's lower then 1sec.
			// bTearOff actors should never be checked
			if ( bLevelInitializedForActor )
			{
				const float MinVisibilityTimeout = FMath::Min(RelevantTimeout, 1.0f);
				if ( !Actor->GetTearOff() && ( !Channel || ElapsedTime - Channel->RelevantTime >= MinVisibilityTimeout) )
				{
					if ( IsActorRelevantToConnection( Actor, ConnectionViewers ) )
					{
						bIsRelevant = true;
					}
#if NET_DEBUG_RELEVANT_ACTORS
					else if ( DebugRelevantActors )
					{
						LastNonRelevantActors.Add( Actor );
					}
#endif // NET_DEBUG_RELEVANT_ACTORS
				}
			}
			else
			{
				// Actor is no longer relevant because the world it is/was in is not loaded by client
				// exception: player controllers should never show up here
				UE_LOGF( LogNetTraffic, Log, "- Level not initialized for actor %ls", *Actor->GetName() );
			}

			// if the actor is now relevant or was recently relevant
			const bool bIsRecentlyRelevant = bIsRelevant || ( Channel && ElapsedTime - Channel->RelevantTime < RelevantTimeout ) || (ActorInfo->ForceRelevantFrame >= Connection->LastProcessedFrame);

			if ( bIsRecentlyRelevant )
			{
				FinalRelevantCount++;

				TOptional<FScopedActorRoleSwap> SwapGuard;
				if (ActorInfo->bSwapRolesOnReplicate || IsRoleSwappingOnAllActorsEnabled())
				{
					SwapGuard = FScopedActorRoleSwap(Actor);
				}
```

关键后半段与结尾（第 5808~5894 行，节选）：

```cpp
				// …（节选：省略通道创建与 NextUpdateTime 抖动 22 行）
				// …（节选：省略 RelevantTime 刷新 7 行）

				// …（节选：省略 Channel->IsNetReady() 判定与 ReplicateActor 调用 16 行）
				{
					ActorUpdatesThisConnectionSent++;
					// …（节选：省略 USE_SERVER_PERF_COUNTERS 统计与调试记录）
				}

				// …（节选：省略 OptimalNetUpdateDelta 回写与 LastNetReplicateTime 更新 10 行）

				ActorUpdatesThisConnection++;
				OutUpdated++;

				// …（节选：省略通道饱和时的 ForceNetUpdate 打标 6 行）

				if ( GNumSaturatedConnections > LocalNumSaturated )
				{
					GNumSaturatedConnections++;
					return j;
				}
			}

			if ( !bIsRecentlyRelevant )
			{
				// …（节选：省略通道关闭判定 14 行）
				{
					UE_LOGF( LogNetTraffic, Log, "- Closing channel for no longer relevant actor %ls", *Actor->GetName() );
					Channel->Close(Actor->GetTearOff() ? EChannelCloseReason::TearOff : EChannelCloseReason::Relevancy);
				}
			}
		}
	}

	return ActorsIndexRange.Max;
}
```

1. **连接级饱和是硬闸门**：`!Connection->IsNetReady() && !bIgnoreSaturation` 时**整条连接本帧一个 Actor 都不处理**并直接 `return 0`，`GNumSaturatedConnections` 计数递增。这与「每帧 N 个 Actor」的直觉不同：5.8 的粒度是「连接是否还能发包」，而不是 Actor 配额。
2. **相关性重检节流**：已可见 Actor 只在 `ElapsedTime - Channel->RelevantTime >= Min(RelevantTimeout, 1.0f)` 时才重新计算可见性，`bTearOff` 的 Actor 永不重检。这解释了「相关性变化有 1 秒级延迟」的现象。
3. **`bIsRecentlyRelevant` 是通道存活的滞后窗口**：`bIsRelevant || (Channel && ElapsedTime - Channel->RelevantTime < RelevantTimeout) || ActorInfo->ForceRelevantFrame >= Connection->LastProcessedFrame`。三者任一为真即继续发送，是为了避免在相关性边界抖动时反复开关通道。
4. **`ReplicateActor` 的调用点**在该函数中段（第 5815~5830 行区域），只有 `Channel->IsNetReady() || bIgnoreSaturation` 为真才进入——即「Actor 级饱和检查」。这是 `UActorChannel::ReplicateActor` 在传统路径上唯一的调用来源。
5. **后半段的 `GNumSaturatedConnections > LocalNumSaturated` 早退**（第 5867~5873 行）返回 `j`，配合 `ServerReplicateActors_MarkRelevantActors`（第 5896 行）把未处理区间标记为相关，下一帧继续。
6. **通道关闭**：`!bIsRecentlyRelevant && Channel != nullptr` 且 `(!bLevelInitializedForActor || !IsNetStartupActor())` 时，按 `TearOff` / `Relevancy` 原因关闭通道。**Map Actor（非 startup actor）立即关通道即立刻销毁**，startup actor 保留通道。

### 7. 参数与阈值对照（严格按源码，无外部推测）

| 名称 | 真实位置 | 源码中的真实语义 |
| --- | --- | --- |
| `ReplicationFrame` | `NetDriver.cpp` 第 6303 行递增 | 使「本帧已比较过」的属性失效，供 changelist 比较 early-out |
| `bCPUSaturated` | `NetDriver.cpp` 第 6349 行 | `DeltaSeconds > 1.2 * ServerTickTime`；在 `PrioritizeActors` 中未被使用 |
| `NumClientsToTick` | `NetDriver.cpp` 第 5214、5227 行 | 每帧允许 tick 的客户端数上限（`NetClientTicksPerSecond × DeltaSeconds`，或 `net.MaxConnectionsToTickPerServerFrame`） |
| `ScaleDownStartTime = 2.0f` | `NetDriver.cpp` 第 5391 行 | 距上次复制超过 2 秒才开始降频 |
| `ScaleDownTimeRange = 5.0f` | `NetDriver.cpp` 第 5392 行 | 降频插值的 5 秒过渡区间 |
| `MinNetUpdateFrequency` 兜底 | `NetDriver.cpp` 第 5400 行 | 为 0 时被就地设为 `2.0f` |
| `MinVisibilityTimeout` | `NetDriver.cpp` 第 5746 行 | `FMath::Min(RelevantTimeout, 1.0f)`，相关性重检的最小间隔 |
| `Priority` | `NetDriver.cpp` 第 5160 行 | `RoundToInt(65536.0f * GetNetPriority(...))`，多 viewer 取 `FMath::Max` |
| `DORM_*` 判定 | `NetDriver.cpp` 第 5505~5526 行 | `NetDormancy <= DORM_Awake` 不动；`DORM_DormantPartial` 逐个 viewer 查 `GetNetDormancy` |
| `net.MaxRPCPerNetUpdate` | `DataReplication.cpp` 第 38~42 行 | 默认 **2**，单个不可靠 multicast RPC 每次网络更新最多队列次数 |

**关于 `MaxReplicationDistanceSquared`**：该标识符在 UE 5.8 的 `Engine\Source` 中**不存在**（ripgrep 全库零命中）。距离裁剪的真实来源是 `AActor::IsWithinNetRelevancyDistance`（`Engine\Source\Runtime\Engine\Private\ActorReplication.cpp` 第 383~386 行），比较对象为 `GetNetCullDistanceSquared()`。

---

## 核心源码深入剖析一：服务器复制核心 `UActorChannel::ReplicateActor`

`ReplicateActor` 是单个 Actor 属性状态打包进网络 Bunch 的中枢。

### 1. `UActorChannel::ReplicateActor` 真实源码（节选）

（2026-09-14：原示意块已替换为 5.8 源码逐字版）

`ReplicateActor` 在 5.8 中是一个约 **382 行**的长函数（第 3602~3983 行），远超前文的直觉印象。以下代码逐字摘自 `Engine\Source\Runtime\Engine\Private\DataChannel.cpp`（第 3602 行起），按真实顺序保留关键区段，省略处标注省略行数：

```cpp
int64 UActorChannel::ReplicateActor()
{
	using namespace UE::Net;
	using namespace UE::Net::Private;

	LLM_SCOPE_BYTAG(NetChannel);
	SCOPE_CYCLE_COUNTER(STAT_NetReplicateActorTime);

	check(Actor);
	check(!Closing);
	check(Connection);
	check(nullptr != Cast<UPackageMapClient>(Connection->PackageMap));

	const UWorld* const ActorWorld = Actor->GetWorld();
	ensureMsgf(ActorWorld, TEXT("ActorWorld for Actor [%s] is Null"), *GetPathNameSafe(Actor));
	if (ActorWorld == nullptr)
	{
		return 0;
	}

	// …（节选：省略 STATS/ENABLE_STATNAMEDEVENTS 的 SCOPE_CYCLE_UOBJECT 与计时计数器 10 行）

	const bool bReplay = Connection->IsReplay();
	const bool bEnableScopedCycleCounter = !bReplay && GReplicateActorTimingEnabled;
	FSimpleScopeSecondsCounter ScopedSecondsCounter(GReplicateActorTimeSeconds, bEnableScopedCycleCounter);

	if (!bReplay)
	{
		GNumReplicateActorCalls++;
	}

	// ignore hysteresis during checkpoints
	if (bIsInDormancyHysteresis && (Connection->ResendAllDataState == EResendAllDataState::None))
	{
		return 0;
	}

	// triggering replication of an Actor while already in the middle of replication can result in invalid data being sent and is therefore illegal
	if (bIsReplicatingActor)
	{
		FString Error(FString::Printf(TEXT("ReplicateActor called while already replicating! %s"), *Describe()));
		UE_LOGF(LogNet, Log, "%ls", *Error);
		ensureMsgf(false, TEXT("%s"), *Error);
		return 0;
	}

	if (bActorIsPendingKill)
	{
		// Don't need to do anything, because it should have already been logged.
		return 0;
	}

	// If our Actor is PendingKill, that's bad. It means that somehow it wasn't properly removed
	// from the NetDriver or ReplicationDriver.
	// TODO: Maybe notify the NetDriver / RepDriver about this, and have the channel close?
	if (!IsValidChecked(Actor) || Actor->IsUnreachable())
	{
		bActorIsPendingKill = true;
		ActorReplicator.Reset();
		FString Error(FString::Printf(TEXT("ReplicateActor called with PendingKill Actor! %s"), *Describe()));
		UE_LOGF(LogNet, Log, "%ls", *Error);
		ensureMsgf(false, TEXT("%s"), *Error);
		return 0;
	}

	if (bPausedUntilReliableACK)
	{
		if (NumOutRec > 0)
		{
			return 0;
		}
		bPausedUntilReliableACK = 0;
		UE_LOGF(LogNet, Verbose, "ReplicateActor: bPausedUntilReliableACK is ending now that reliables have been ACK'd. %ls", *Describe());
	}

	if (bNetReplicateOnlyBeginPlay && !IsActorReadyForReplication() && !bIsForcedSerializeFromRPC)
	{
		UE_LOGF(LogNet, Verbose, "ReplicateActor ignored since actor is not BeginPlay yet: %ls", *Describe());
		return 0;
	}

	// …（节选：省略 ReplicationViewers 与 bIsNewlyReplicationPaused 判定 22 行）

	// The package map shouldn't have any carry over guids
	// Static cast is fine here, since we check above.
	UPackageMapClient* PackageMapClient = static_cast<UPackageMapClient*>(Connection->PackageMap);
	if (PackageMapClient->GetMustBeMappedGuidsInLastBunch().Num() != 0)
	{
		UE_LOGF(LogNet, Warning, "ReplicateActor: PackageMap->GetMustBeMappedGuidsInLastBunch().Num() != 0: %i: Channel: %ls", PackageMapClient->GetMustBeMappedGuidsInLastBunch().Num(), *Describe());
	}

	bool bWroteSomethingImportant = bIsNewlyReplicationUnpaused || bIsNewlyReplicationPaused;

	// Create an outgoing bunch, and skip this actor if the channel is saturated.
	FOutBunch Bunch( this, 0 );

	// Create export scope to capture NetToken exports and store them in Bunch.NetTokensPendingExport
	UE::Net::FNetTokenExportScope NetTokenExportScope(Bunch, Connection->GetDriver()->GetNetTokenStore(), Bunch.NetTokensPendingExport, "ReplicateActor");

	if( Bunch.IsError() )
	{
		return 0;
	}

	// Cache the netgroup manager so we don't have to access it for every subobject.
	DataChannelInternal::CachedNetworkSubsystem = ActorWorld->GetSubsystem<UNetworkSubsystem>();
	check(DataChannelInternal::CachedNetworkSubsystem);
	ON_SCOPE_EXIT
	{
		DataChannelInternal::CachedNetworkSubsystem = nullptr;
	};

	// …（节选：省略 UE_NET_TRACE / 可靠性调试名 / UE_NET_REPACTOR_NAME_DEBUG 块 35 行）

	FGuardValue_Bitfield(bIsReplicatingActor, true);
	FScopedRepContext RepContext(Connection, Actor);

	FReplicationFlags RepFlags;

	// Send initial stuff.
	if( OpenPacketId.First != INDEX_NONE && (Connection->ResendAllDataState == EResendAllDataState::None) )
	{
		if( !SpawnAcked && OpenAcked )
		{
			// After receiving ack to the spawn, force refresh of all subsequent unreliable packets, which could
			// have been lost due to ordering problems. Note: We could avoid this by doing it in FActorChannel::ReceivedAck,
			// and avoid dirtying properties whose acks were received *after* the spawn-ack (tricky ordering issues though).
			SpawnAcked = 1;
			for (auto RepComp = ReplicationMap.CreateIterator(); RepComp; ++RepComp)
			{
				RepComp.Value()->ForceRefreshUnreliableProperties();
			}
		}
	}
	else
	{
		if (Connection->ResendAllDataState == EResendAllDataState::SinceCheckpoint)
		{
			RepFlags.bNetInitial = !bOpenedForCheckpoint;
		}
		else
		{
			RepFlags.bNetInitial = true;
		}

		Bunch.bClose = Actor->bNetTemporary;
		Bunch.bReliable = true; // Net temporary sends need to be reliable as well to force them to retry
	}

	// Owned by connection's player?
	UNetConnection* OwningConnection = Actor->GetNetConnection();

	RepFlags.bNetOwner = (OwningConnection == Connection || (OwningConnection != nullptr && OwningConnection->IsA(UChildConnection::StaticClass()) && ((UChildConnection*)OwningConnection)->Parent == Connection));

	// ----------------------------------------------------------
	// If initial, send init data.
	// ----------------------------------------------------------

	if (RepFlags.bNetInitial && OpenedLocally)
	{
		UE_NET_TRACE_SCOPE(NewActor, Bunch, GetTraceCollector(Bunch), ENetTraceVerbosity::Trace);

		Connection->PackageMap->SerializeNewActor(Bunch, this, static_cast<AActor*&>(Actor));
		bWroteSomethingImportant = true;

		Actor->OnSerializeNewActor(Bunch);

		RepFlags.bForceInitialDirty = Bunch.bOutWantsFullInitState;
	}

	// Possibly downgrade role of actor if this connection doesn't own it
	TUniquePtr<FScopedRoleDowngrade> ScopedRoleDowngrade;
	if (Connection->Driver->CanDowngradeActorRole(Connection, Actor))
	{
		ScopedRoleDowngrade = MakeUnique<FScopedRoleDowngrade>(Actor, RepFlags);
	}

	RepFlags.bNetSimulated	= (Actor->GetRemoteRole() == ROLE_SimulatedProxy);

	if (Actor->GetRemoteRole() == ROLE_AutonomousProxy && Connection->IsProxyConnection())
	{
		RepFlags.bNetSimulated = true;
	}

	RepFlags.bRepPhysics	= Actor->GetReplicatedMovement().bRepPhysics;
	RepFlags.bReplay		= bReplay;
	RepFlags.bClientReplay	= ActorWorld->IsRecordingClientReplay();
	RepFlags.bForceInitialDirty |= Connection->IsForceInitialDirty();

	if (EnumHasAnyFlags(ActorReplicator->RepLayout->GetFlags(), ERepLayoutFlags::HasDynamicConditionProperties))
	{
		if (const FRepState* RepState = ActorReplicator->RepState.Get())
		{
			const FSendingRepState* SendingRepState = RepState->GetSendingRepState();
			if (const FRepChangedPropertyTracker* PropertyTracker = SendingRepState ? SendingRepState->RepChangedPropertyTracker.Get() : nullptr)
			{
				RepFlags.CondDynamicChangeCounter = PropertyTracker->GetDynamicConditionChangeCounter();
			}
		}
	}

	UE_LOGF(LogNetTraffic, Log, "Replicate %ls, bNetInitial: %d, bNetOwner: %d", *Actor->GetName(), RepFlags.bNetInitial, RepFlags.bNetOwner);

	FMemMark	MemMark(FMemStack::Get());	// The calls to ReplicateProperties will allocate memory on FMemStack::Get(), and use it in ::PostSendBunch. we free it below

	// ----------------------------------------------------------
	// Replicate Actor and Component properties and RPCs
	// ---------------------------------------------------

	// …（节选：省略 USE_NETWORK_PROFILER 起始计时 3 行）

	if (!bIsNewlyReplicationPaused)
	{
		// The Actor
		{
			UE_NET_TRACE_OBJECT_SCOPE(ActorReplicator->ObjectNetGUID, Bunch, GetTraceCollector(Bunch), ENetTraceVerbosity::Trace);

			const bool bCanSkipUpdate = ActorReplicator->CanSkipUpdate(RepFlags);

			if (UE::Net::bPushModelValidateSkipUpdate || !bCanSkipUpdate)
			{
				bWroteSomethingImportant |= ActorReplicator->ReplicateProperties(Bunch, RepFlags);
			}

			ensureMsgf(!UE::Net::bPushModelValidateSkipUpdate || !bCanSkipUpdate || !bWroteSomethingImportant, TEXT("Actor wrote data but we thought it was skippable: %s"), *GetFullNameSafe(Actor));
		}

		bWroteSomethingImportant |= DoSubObjectReplication(Bunch, RepFlags);

		if (Connection->ResendAllDataState != EResendAllDataState::None)
		{
			int64 NumBitsWrote = 0;
			if (bWroteSomethingImportant)
			{
				SendBunch(&Bunch, 1);
				NumBitsWrote = Bunch.GetNumBits();
			}

			MemMark.Pop();
			NETWORK_PROFILER(GNetworkProfiler.TrackReplicateActor(Actor, RepFlags, FPlatformTime::Cycles() - ActorReplicateStartTime, Connection));
			Connection->GetDriver()->GetMetrics()->IncrementInt(UE::Net::Metric::NumReplicatedActorBytes, (NumBitsWrote + 7) >> 3);

			return NumBitsWrote;
		}

		{
			bWroteSomethingImportant |= UpdateDeletedSubObjects(Bunch);
		}
	}

	NETWORK_PROFILER(GNetworkProfiler.TrackReplicateActor(Actor, RepFlags, FPlatformTime::Cycles() - ActorReplicateStartTime, Connection));

	// -----------------------------
	// Send if necessary
	// -----------------------------

	int64 NumBitsWrote = 0;
	if (bWroteSomethingImportant)
	{
		// We must exit the collection scope to report data correctly
		FPacketIdRange PacketRange = SendBunch( &Bunch, 1 );

		if (!bIsNewlyReplicationPaused)
		{
			for (auto RepComp = ReplicationMap.CreateIterator(); RepComp; ++RepComp)
			{
				RepComp.Value()->PostSendBunch(PacketRange, Bunch.bReliable);
			}

			// …（节选：省略 NET_ENABLE_SUBOBJECT_REPKEYS 的 NakMap 记录 29 行）

			if (Actor->bNetTemporary)
			{
				LLM_SCOPE_BYTAG(NetConnection);
				Connection->SentTemporaries.Add(Actor);
			}
		}
		NumBitsWrote = Bunch.GetNumBits();
	}

	// …（节选：省略 PendingObjKeys 清空、LastUpdateTime 回写、MemMark.Pop 与指标累加 16 行）

	return NumBitsWrote;
}
```

### 2. 逐行技术深度解构

1. **`ActorReplicator->ReplicateProperties(Bunch, RepFlags)` 是真实调用点（第 3882 行）**：`FRepLayout::ReplicateProperties` 并不被 `UActorChannel` 直接调用，而是经由该 Actor 的 `FObjectReplicator` 转发；且包了一层 `CanSkipUpdate(RepFlags)` 提前退出——这是 Push Model（`UPROPERTY(Replicated, PushModel)`）省 CPU 的关键开关。若 `UE::Net::bPushModelValidateSkipUpdate` 打开，跳过更新却仍写出数据会被 `ensureMsgf` 抓出，属于开发期一致性校验。
2. **影子内存对比原理（真实位置：`FRepLayout` 的 changelist 比较路径，见下文「核心源码深入剖析五」）**：
   - 5.8 中**不存在** `FRepState::DynamicBuffer`、`LastProperty`、`SendingProxy` 等成员（ripgrep 核对零命中）。发送侧的历史状态由 `FRepChangelistState`（环形 changelist 历史）+ `FSendingRepState::ChangeHistory[]` 共同承担，接收侧的最新状态才是 `FReceivingRepState::StaticBuffer`；
   - `FRepLayout::CompareProperties` 在比较时使用的影子缓冲是 `RepChangelistState->StaticBuffer.GetData()`（`RepLayout.cpp` 第 1834 行），比较与写回发生在 `CompareProperties_r` 第 1682~1684 行的 `PropertiesAreIdentical(...)` / `StoreProperty(...)`；
   - 因而是「逐条 Cmd 比较 + 命中则写回影子并追加 Handle 到 changelist」，不是「无条件逐字节 memcmp 整块对象内存」。
3. **SubObject 动态挂载复制（第 4007 行，`Actor->ReplicateSubobjects(...)` 调用点）**：真实调用链是 `ReplicateActor` → `DoSubObjectReplication`（第 3888 行）→ 二选一：
   - `Actor->IsUsingRegisteredSubObjectList()` 为真时走 `ReplicateRegisteredSubObjects`（5.8 的推荐路径，配合 `AddReplicatedSubObject`）；
   - 否则才回调虚函数 `Actor->ReplicateSubobjects(this, &Bunch, &OutRepFlags)`（第 4007 行，即在 `DoSubObjectReplication` 第 3985 行起函数的 `else` 分支内）；
   - 单个子对象实际写入由 `UActorChannel::ReplicateSubobject`（第 4256 行）→ `WriteSubObjectInBunch` 完成，并受 `SUBOBJECT_TRANSITION_VALIDATION`（第 4263~4287 行）与 `GCVarDetectDeprecatedReplicateSubObjects` 的开发期校验约束；`UE::Net::GCVarCompareSubObjectsReplicated` 会触发 `ValidateReplicatedSubObjects()` 对比新旧两条路径的结果。
4. **可靠性与重发路径**：`bWroteSomethingImportant` 才调用 `SendBunch(&Bunch, 1)`（注意第二个参数传 **1**，即 `bForce` 为真）；随后对每个 `ReplicationMap` 中的 replicator 调 `PostSendBunch(PacketRange, Bunch.bReliable)`，把「已发出但未 ACK」的属性值记入 retire 历史——这是不可靠属性在丢包后能被 NAK 重发的依据。
5. **`OpenPacketId.First != INDEX_NONE` 分支**：通道已建立时，一旦 spawn 包被 ACK（`!SpawnAcked && OpenAcked`）就对所有 replicator 调用 `ForceRefreshUnreliableProperties()`，强制把此前发出的不可靠属性重新置脏——因为 spawn 之前的不可靠包可能已丢。这是「连接建立瞬间的一波重发」的真实来源。
6. **`RepFlags` 的关键语义**：`bNetInitial`（首包，需序列化 spawn 信息）、`bNetOwner`（该连接是否为 NetOwner）、`bNetSimulated`、`bRepPhysics`、`bReplay`、`bForceInitialDirty`、`CondDynamicChangeCounter`。其中 `CondDynamicChangeCounter` 取自 `FSendingRepState::RepChangedPropertyTracker->GetDynamicConditionChangeCounter()`（第 3848~3858 行），供 `COND_*` 动态条件（`DOREPLIFETIME_ACTIVE_OVERRIDE`）判定使用。
7. **`bIsReplicatingActor` 重入保护（第 3643 行）**：`FGuardValue_Bitfield` 在第 3774 行置位。这正是 `ProcessRemoteFunctionForChannelPrivate` 第 3259 行要检查 `Ch->bIsReplicatingActor` 并在「复制中途触发 RPC」时报错并 `ensureMsgf(false)` 的原因——两者互为约束。
8. **`ReplicateActor` 由谁调用**：传统路径上只有 `ServerReplicateActors_ProcessPrioritizedActorsRange`（`NetDriver.cpp` 第 5815~5830 行区域，且需 `Channel->IsNetReady()`）与 `ProcessRemoteFunctionForChannelPrivate`（`NetDriver.cpp` 第 3289 行，`SetForcedSerializeFromRPC(true)` 包裹）两处。

### 3. 客户端通道入口：`ProcessBunch` 与 `ReceivedBunch`

客户端侧入口为 `UActorChannel::ProcessBunch(FInBunch& Bunch)`（`Engine\Source\Runtime\Engine\Private\DataChannel.cpp` 第 3541 行），其实现被事务包裹后转调 `ProcessBunchInternal`（第 3273 行）；真正把 payload 交给 replicator 的是 `UActorChannel::ReceivedBunch(FInBunch & Bunch)`（第 3114 行）。关键派发点（第 3466~3482 行，逐字节选）：

```cpp
				UE_LOGF( LogNet, Warning, "UActorChannel::ProcessBunch: Replicator.ReceivedBunch failed (Ignoring because of IsInternalAck). RepObj: %ls, Channel: %i", RepObj ? *RepObj->GetFullName() : TEXT( "NULL" ), ChIndex );
			// …（节选：省略 IsInternalAck 分支收尾 3 行）
			UE_LOGF( LogNet, Error, "UActorChannel::ProcessBunch: Replicator.ReceivedBunch failed.  Closing connection. RepObj: %ls, Channel: %i", RepObj ? *RepObj->GetFullName() : TEXT( "NULL" ), ChIndex );
			// …（节选：省略 Connection->Close 调用 2 行）
```

要点：

1. **`ReceivedBunch` 先处理 `bHasMustBeMappedGUIDs`**（第 3141 行起）：若该 bunch 声明了必须映射的 GUID，客户端必须先读出一组 `FNetworkGUID` 并等待其解析完成，才允许继续处理该通道后续流。这就是「异步加载资源未就绪时复制会排队」的底层原因（相关日志见 `Queuing bunch because another channel ... is processing bunches for this guid still`，第 3234 行）。
2. **失败即断连**：`Replicator.ReceivedBunch` 返回 false 时，非 `IsInternalAck` 情况下会 `Close` 连接，日志文案为 `Replicator.ReceivedBunch failed. Closing connection.`。这不是可忽略的告警，而是硬错误。
3. **Actor 可能在处理中被销毁**：第 3482 行有 `UActorChannel::ProcessBunch: Actor was destroyed during Replicator.ReceivedBunch processing` 的 VeryVerbose 日志，说明 `ReceivedBunch` 允许销毁 Actor（如 `OnRep` 中 `Destroy()`），调用方必须处理。
4. **`FObjectReplicator` 的真实归属**：类声明在 `Engine\Source\Runtime\Engine\Public\Net\DataReplication.h`（第 73 行），实现在 `Engine\Source\Runtime\Engine\Private\DataReplication.cpp`。**它没有迁移到 `Net\Core\`**；迁移到 NetCore 的是 `FRepChangedPropertyTracker`（`Engine\Source\Runtime\Net\Core\Public\Net\Core\PropertyConditions\RepChangedPropertyTracker.h` 第 22 行，`RepLayout.h` 第 123 行留有注释 `FRepChangedPropertyTracker moved to NetCore module`）。

---

## 核心源码深入剖析二：远程函数调用 `UNetDriver::ProcessRemoteFunction`

当在 C++ 或蓝图中调用标记为 `UFUNCTION(Server, Reliable)` 的函数时，引擎底层拦截本地调用并将其封装为 RPC。

### 1. `UNetDriver::ProcessRemoteFunction` 真实源码

（2026-09-14：原示意块已替换为 5.8 源码逐字版）

以下代码逐字摘自 `Engine\Source\Runtime\Engine\Private\NetDriver.cpp`（第 8125 行起，函数共 153 行，此处为节选）：

```cpp
void UNetDriver::ProcessRemoteFunction(
	class AActor* Actor,
	UFunction* Function,
	void* Parameters,
	FOutParmRec* OutParms,
	FFrame* Stack,
	class UObject* SubObject)
{
	if (Actor->IsActorBeingDestroyed())
	{
		UE_LOGF(LogNet, Warning, "UNetDriver::ProcessRemoteFunction: Remote function %ls called from actor %ls while actor is being destroyed. Function will not be processed.", *Function->GetName(), *Actor->GetName());
		return;
	}

	if (UE::Net::bDiscardTornOffActorRPCs && Actor->GetTearOff())
	{
		UE_LOGF(LogNet, Warning, "UNetDriver::ProcessRemoteFunction: Remote function %ls called from actor %ls while actor is torn off. Function will not be processed.", *Function->GetName(), *Actor->GetName());
		return;
	}

#if !UE_BUILD_SHIPPING
	SCOPE_CYCLE_COUNTER(STAT_NetProcessRemoteFunc);
	SCOPE_CYCLE_UOBJECT(Function, Function);

	{
		UObject* TestObject = (SubObject == nullptr) ? Actor : SubObject;
		checkf(IsInGameThread(), TEXT("Attempted to call ProcessRemoteFunction from a thread other than the game thread, which is not supported.  Object: %s Function: %s"), *GetPathNameSafe(TestObject), *GetNameSafe(Function));
		ensureMsgf(TestObject->IsSupportedForNetworking() || TestObject->IsNameStableForNetworking(), TEXT("Attempted to call ProcessRemoteFunction with object that is not supported for networking. Object: %s Function: %s"), *TestObject->GetPathName(), *Function->GetName());
	}

	bool bBlockSendRPC = false;

	SendRPCDel.ExecuteIfBound(Actor, Function, Parameters, OutParms, Stack, SubObject, bBlockSendRPC);

	if (!bBlockSendRPC)
#endif
	{
		const bool bIsServer = IsServer();
		const bool bIsServerMulticast = bIsServer && (Function->FunctionFlags & FUNC_NetMulticast);

		++TotalRPCsCalled;

		// Copy Any Out Params to Local Params
		TArray<UE::Net::Private::FAutoDestructProperty> LocalOutParms;
		if (Stack == nullptr)
		{
			// If we have a subobject, thats who we are actually calling this on. If no subobject, we are calling on the actor.
			UObject* TargetObj = SubObject ? SubObject : Actor;
			LocalOutParms = UE::Net::Private::CopyOutParametersToLocalParameters(Function, OutParms, Parameters, TargetObj);
		}

		// …（节选：省略 UE_WITH_REMOTE_OBJECT_HANDLE 的 EnqueueRPC 分支 7 行）

		if (ReplicationSystem)
		{
			if (bIsServerMulticast)
			{
				if (ReplicationSystem->SendRPC(Actor, SubObject, Function, Parameters))
				{
					return;
				}
			}
			else
			{
				if (UNetConnection* Connection = Actor->GetNetConnection())
				{
					if (ReplicationSystem->SendRPC(Connection->GetConnectionHandle().GetParentConnectionId(), Actor, SubObject, Function, Parameters))
					{
						return;
					}
				}
				else
				{
					UE_LOGF(LogNet, Verbose, "SendRPC %ls::%ls dropped because Actor does not have a NetConnection", *Actor->GetName(), *Function->GetName());
				}
			}

			// If we are using Iris replication, we should never fall back on normal replication path
			return;
		}

		// Forward to replication Driver if there is one
		if (ReplicationDriver && ReplicationDriver->ProcessRemoteFunction(Actor, Function, Parameters, OutParms, Stack, SubObject))
		{
			return;
		}

		// RepDriver didn't handle it, default implementation
		UNetConnection* Connection = nullptr;
		if (bIsServerMulticast)
		{
			TSharedPtr<FRepLayout> RepLayout = GetFunctionRepLayout(Function);

			// Multicast functions go to every client
			EProcessRemoteFunctionFlags RemoteFunctionFlags = EProcessRemoteFunctionFlags::None;
			TArray<UNetConnection*> UniqueRealConnections;
			for (int32 i = 0; i < ClientConnections.Num(); ++i)
			{
				Connection = ClientConnections[i];
				if (Connection && Connection->ViewTarget)
				{
					// Only send or queue multicasts if the actor is relevant to the connection
					FNetViewer Viewer(Connection, 0.f);

					if (Connection->GetUChildConnection() != nullptr)
					{
						Connection = ((UChildConnection*)Connection)->Parent;
					}

					// It's possible that an actor is not relevant to a specific connection, but the channel is still alive (due to hysteresis).
					// However, it's also possible that the Actor could become relevant again before the channel ever closed, and in that case we
					// don't want to lose Reliable RPCs.
					if (Actor->IsNetRelevantFor(Viewer.InViewer, Viewer.ViewTarget, Viewer.ViewLocation) ||
						((Function->FunctionFlags & FUNC_NetReliable) && !!CVarAllowReliableMulticastToNonRelevantChannels.GetValueOnGameThread() && Connection->FindActorChannelRef(Actor)))
					{
						// We don't want to call this unless necessary, and it will internally handle being called multiple times before a clear
						// Builds any shared serialization state for this rpc
						RepLayout->BuildSharedSerializationForRPC(Parameters, GetNetTokenStore());

						InternalProcessRemoteFunctionPrivate(Actor, SubObject, Connection, Function, Parameters, OutParms, Stack, bIsServer, RemoteFunctionFlags);
					}
				}
			}

			// Finished sending this multicast rpc, clear any shared state
			RepLayout->ClearSharedSerializationForRPC();

			// Return here so we don't call InternalProcessRemoteFunction again at the bottom of this function
			return;
		}

		// Send function data to remote.
		Connection = Actor->GetNetConnection();
		if (Connection)
		{
			if (ServerConnection)
			{
				Connection = ServerConnection;
			}
			InternalProcessRemoteFunction(Actor, SubObject, Connection, Function, Parameters, OutParms, Stack, bIsServer);
		}
		else
		{
			UE_LOGF(LogNet, Warning, "UNetDriver::ProcessRemoteFunction: No owning connection for actor %ls. Function %ls will not be processed.", *Actor->GetName(), *Function->GetName());
		}
	}
}
```

### 2. 逐行技术深度解构

1. **入口三连拦截（第 8133~8143 行）**：Actor 正在销毁、`UE::Net::bDiscardTornOffActorRPCs` 且已 TearOff，都被直接丢弃并打 Warning。5.8 中**不存在**「找不到连接就打印 `ProcessRemoteFunction: No connection found for Actor`」这句日志；真实文案是末尾的 `No owning connection for actor %ls. Function %ls will not be processed.`（第 8274 行）。
2. **通道创建不在本函数、也不在 `Connection->GetConnectionState() == USOCK_Open` 时**：真实的通道懒惰创建在 `InternalProcessRemoteFunctionPrivate`（第 3152~3184 行），条件是 `bIsServer && IsLevelInitializedForActor(Actor, Connection)`，用 `CreateChannelByName(NAME_Actor, EChannelCreateFlags::OpenedLocally)`；客户端侧找不到通道则直接 `return`。这与「客户端对尚未初始同步的 Actor 发 Server RPC 会被静默丢弃」的现象一致，但真实告警不在这里。
3. **函数索引压缩的真实位置**：`ProcessRemoteFunction` 本身只做分派；紧凑索引由后续的 `NetCache->GetClassNetCache(TargetObj->GetClass())` 取得 `FClassNetCache`，再 `ClassCache->GetFromField(Function)` 取 `FFieldNetCache`（第 3138~3150 行），最终在 `Ch->WriteFieldHeaderAndPayload(...)`（第 3412/3429 行）中写出 `FieldCache->FieldNetIndex`。注意这**不是**「PackageMap 导出的全局编号」，而是**每个类一份**的字段序号。
4. **`Server` 与 `NetMulticast` 的真实判定**：本函数只显式算 `bIsServerMulticast = bIsServer && (Function->FunctionFlags & FUNC_NetMulticast)`（第 8163 行）。`Server` 与 `Client` 的区分不在这里的 if 里，而是**由 `UObject::GetFunctionCallspace` 在更上游决定本次调用到底是 Local、Remote 还是 Absorbed**（见下节）。`AActor::CallRemoteFunction` 只在 `GetFunctionCallspace` 返回含 `Remote` 位时才被调用。
5. **多播的真实行为**：`bIsServerMulticast` 时遍历**所有** `ClientConnections`（`Connection->ViewTarget` 非空），逐连接做 `Actor->IsNetRelevantFor(...)`；不可靠多播的相关性检查失败就跳过，**可靠多播**在 `CVarAllowReliableMulticastToNonRelevantChannels` 打开且通道仍存在时可例外发送——源码注释解释了原因：通道可能因滞后（hysteresis）尚未关闭，Actor 也可能重新变相关，此时不能丢可靠 RPC。
6. **不可靠多播是「排队」而非「立即发送」**：`RepLayout->BuildSharedSerializationForRPC(Parameters, GetNetTokenStore())` 在多播循环外建立共享序列化状态，循环内复用，循环结束后 `ClearSharedSerializationForRPC()`。真正的队列决策在 `ProcessRemoteFunctionForChannelPrivate` 第 3400 行：`QueueBunch = ( !Bunch.bReliable && Function->FunctionFlags & FUNC_NetMulticast )`，入队后由 `Ch->QueueRemoteFunctionBunch(...)`（第 3464 行）→ `FObjectReplicator::QueueRemoteFunctionBunch`（`DataReplication.cpp` 第 2293 行）处理，并在下次属性复制时随 bunch 发出。
7. **`FObjectReplicator::QueueRemoteFunctionBunch` 的节流是真实存在的**（第 2302~2332 行，逐字节选）：

```cpp
	// This is a pretty basic throttling method - just don't let same func be called more than
	// twice in one network update period.
	//
	// Long term we want to have priorities and stronger cross channel traffic management that
	// can handle this better
	int32 InfoIdx = INDEX_NONE;
	for (int32 i = 0; i < RemoteFuncInfo.Num(); ++i)
	{
		if (RemoteFuncInfo[i].FuncName == Func->GetFName())
		{
			InfoIdx = i;
			break;
		}
	}
	// …（节选：省略 RemoteFuncInfo 初始化 6 行）

	if (++RemoteFuncInfo[InfoIdx].Calls > CVarMaxRPCPerNetUpdate.GetValueOnAnyThread())
	{
		UE_LOGF(LogRep, Verbose, "Too many calls (%d) to RPC %ls within a single netupdate. Skipping. %ls.  LastCallTime: %.2f. CurrentTime: %.2f. LastRelevantTime: %.2f. LastUpdateTime: %.2f ",
			RemoteFuncInfo[InfoIdx].Calls, *Func->GetName(), *GetPathNameSafe(GetObject()), RemoteFuncInfo[InfoIdx].LastCallTimestamp, OwningChannel->Connection->Driver->GetElapsedTime(), OwningChannel->RelevantTime, OwningChannel->LastUpdateTime);

		// The MustBeMappedGuids can just be dropped, because we aren't actually going to send a bunch. If we don't clear it, then we will get warnings when the next channel tries to replicate
		CastChecked<UPackageMapClient>(Connection->PackageMap)->GetMustBeMappedGuidsInLastBunch().Reset();
		return;
	}
```

   对应控制台变量 `net.MaxRPCPerNetUpdate`（`DataReplication.cpp` 第 38~42 行），**默认值 2**，说明为 `Maximum number of unreliable multicast RPC calls allowed per net update, additional ones will be dropped`。因此「同一帧连续调 3 次不可靠多播 RPC，第 3 次被丢弃」是设计行为而非 bug。

### 3. 上游：`Server` / `Client` / `NetMulticast` 的真实判定代码

RPC 的「发往哪一侧」判定不在 `ProcessRemoteFunction` 里，而在调用点之前。链路为：

```mermaid
sequenceDiagram
    autonumber
    participant BP as 蓝图/C++ 调用点
    participant PE as UObject::CallFunction (ScriptCore.cpp)
    participant CS as UObject::GetFunctionCallspace
    participant CRF as AActor::CallRemoteFunction (Actor.cpp)
    participant PRF as UNetDriver::ProcessRemoteFunction
    participant IPRF as InternalProcessRemoteFunctionPrivate

    Note over BP,PE: 概念示意（非逐帧时序）
    BP->>PE: 调用 UFUNCTION(Server/Client/NetMulticast)
    PE->>PE: bNetFunction = HasAnyFunctionFlags(FUNC_NetFuncFlags ...)
    PE->>CS: FunctionCallspace = GetFunctionCallspace(Function, &Stack)
    alt FunctionCallspace & Local
        PE->>PE: Function->Invoke(this, Stack, RESULT_PARAM)
    end
    alt FunctionCallspace & Remote
        PE->>CRF: CallRemoteFunction(Function, Buffer, Stack.OutParms, &Stack)
        CRF->>PRF: Driver.NetDriver->ProcessRemoteFunction(this, Function, ...)
        PRF->>IPRF: InternalProcessRemoteFunction / InternalProcessRemoteFunctionPrivate
    end
```

`UObject::CallFunction` 的真实分支摘自 `Engine\Source\Runtime\CoreUObject\Private\UObject\ScriptCore.cpp`（第 1149~1199 行，逐字节选）：

```cpp
	if (Function->FunctionFlags & FUNC_Native)
	{
		const bool bNetFunction = Function->HasAnyFunctionFlags(FUNC_NetFuncFlags|FUNC_BlueprintAuthorityOnly|FUNC_BlueprintCosmetic|FUNC_NetRequest|FUNC_NetResponse);
		const int32 FunctionCallspace = bNetFunction ? GetFunctionCallspace( Function, &Stack ) : FunctionCallspace::Local;

		uint8* SavedCode = NULL;
		if (FunctionCallspace & FunctionCallspace::Remote)
		{
			// Call native networkable function.
			uint8* Buffer = (uint8*)UE_VSTACK_ALLOC_ALIGNED(Stack.CachedThreadVirtualStackAllocator, Function->ParmsSize, Function->GetMinAlignment());

			SavedCode = Stack.Code; // Since this is native, we need to rollback the stack if we are calling both remotely and locally

			FMemory::Memzero( Buffer, Function->ParmsSize );

			// Form the RPC parameters.
			for (TFieldIterator<FProperty> It(Function); It && (It->PropertyFlags & (CPF_Parm|CPF_ReturnParm))==CPF_Parm; ++It)
			{
				uint8* CurrentPropAddr = It->ContainerPtrToValuePtr<uint8>(Buffer);
				if (CastField<FBoolProperty>(*It) && It->ArrayDim == 1)
				{
					uint8 TempValue = 0;
					Stack.Step(Stack.Object, &TempValue);
					const bool NewBoolValue = (TempValue != 0);

					if (NewBoolValue)
					{
						((FBoolProperty*)*It)->SetPropertyValue(CurrentPropAddr, true);
					}
				}
				else
				{
					Stack.Step(Stack.Object, CurrentPropAddr);
				}
			}
			checkSlow(*Stack.Code==EX_EndFunctionParms);

			CallRemoteFunction(Function, Buffer, Stack.OutParms, &Stack);
		}

		if (FunctionCallspace & FunctionCallspace::Local)
		{
			if (SavedCode)
			{
				Stack.Code = SavedCode;
			}

			// Call regular native function.
			FScopeCycleCounterUObject NativeContextScope(GVerboseScriptStats ? Stack.Object : nullptr);
			Function->Invoke(this, Stack, RESULT_PARAM);
		}
		else
		{
			// Eat up the remaining parameters in the stream.
			SkipFunction(Stack, RESULT_PARAM, Function);
		}
	}
```

`AActor::CallRemoteFunction` 全文摘自 `Engine\Source\Runtime\Engine\Private\Actor.cpp`（第 5668~5688 行，逐字）：

```cpp
bool AActor::CallRemoteFunction( UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack )
{
	bool bProcessed = false;

	if (UWorld* MyWorld = GetWorld())
	{
		if (FWorldContext* const Context = GEngine->GetWorldContextFromWorld(MyWorld))
		{
			for (FNamedNetDriver& Driver : Context->ActiveNetDrivers)
			{
				if (Driver.NetDriver != nullptr && Driver.NetDriver->ShouldReplicateFunction(this, Function))
				{
					Driver.NetDriver->ProcessRemoteFunction(this, Function, Parameters, OutParms, Stack, nullptr);
					bProcessed = true;
				}
			}
		}
	}

	return bProcessed;
}
```

加上 `UNetDriver::ShouldReplicateFunction` 全文（`NetDriver.cpp` 第 8410~8413 行，逐字）：

```cpp
bool UNetDriver::ShouldReplicateFunction(AActor* Actor, UFunction* Function) const
{
	return (Actor && Actor->GetNetDriverName() == NetDriverName);
}
```

1. **`Local` 与 `Remote` 不是互斥的**：源码先处理 `Remote` 位再处理 `Local` 位，两者可同时置位（例如拥有者本地也执行的 Server RPC）。`SavedCode = Stack.Code` 与回滚注释正是为此存在——远程调用会消耗参数流，本地调用前必须回滚。
2. **`Absorbed` 走 `SkipFunction`**：`FunctionCallspace` 既不含 `Remote` 也不含 `Local` 时，参数被 `SkipFunction` 吃掉而不执行。这就是「客户端上对非拥有 Actor 调 Server RPC 什么也不发生」的真实机制。
3. **原生 RPC 参数在发送前被逐字段搬运到一块本地 `Buffer`**（`UE_VSTACK_ALLOC_ALIGNED` 分配、`Memzero` 清零），`FBoolProperty` 且 `ArrayDim == 1` 时走特殊读取路径，其余走 `Stack.Step`。这说明 RPC 参数是**值拷贝**出去，与调用栈生命周期解耦。
4. **`ShouldReplicateFunction` 的判据只有 NetDriverName 匹配**（5.8 已简化，不再检查角色/所有权）。**因此「只有 NetOwner 才能发 Server RPC」的权限校验不在此处**，真实拦截点是 `GetFunctionCallspace`（`AActor` 重写版）与 `UObject::ProcessEvent` 的 callspace 判定。是否放行由 `FunctionCallspace` 决定，`ProcessRemoteFunction` 只负责发。
5. **`Context->ActiveNetDrivers` 是循环**：一个 WorldContext 下可能有多个 NetDriver（主驱动 + beacon 等），`CallRemoteFunction` 会遍历所有通过 `ShouldReplicateFunction` 的驱动，返回 `bProcessed` 表示是否有任一驱动接手。

### 4. RPC 校验：`_Validate` 的真实派发机制

UE 中 `UFUNCTION(Server, Reliable, WithValidation)` 会要求实现 `XXX_Validate`。该机制的真相**不是**在 `ProcessRemoteFunction` 里的 if 判断，而是 **UHT 在生成的 `exec` thunk 中插入的代码**。代码生成源摘自 `Engine\Source\Programs\Shared\EpicGames.UHT\Exporters\CodeGen\UhtHeaderCodeGeneratorCppFile.cs`（第 3505~3513 行，逐字）：

```cpp
			// Call the validate function if there is one
			if (!function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.CppStatic) && function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetValidate))
			{
				builder.Append("\tif (!P_THIS->").Append(function.CppValidationImplName).Append('(').AppendFunctionThunkParameterNames(function).Append("))\r\n");
				builder.Append("\t{\r\n");
				builder.Append("\t\tRPC_ValidateFailed(TEXT(\"").Append(function.CppValidationImplName).Append("\"));\r\n");
				builder.Append("\t\treturn;\r\n");   // If we got here, the validation function check failed
				builder.Append("\t}\r\n");
			}
```

配套的命名规则在 `Engine\Source\Programs\Shared\EpicGames.UHT\Parsers\UhtFunctionParser.cs`（第 738~740 行，逐字）：

```csharp
					if (function.CppValidationImplName.Length == 0 && function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetValidate))
					{
						function.CppValidationImplName = function.EngineName + "_Validate";
					}
```

失败原因的记录/读取接口在 `Engine\Source\Runtime\CoreUObject\Private\UObject\CoreNet.cpp`（第 663、667、672 行，声明见 `CoreNet.h` 第 800~802 行）：`RPC_ResetLastFailedReason()`、`RPC_ValidateFailed(const TCHAR* Reason)`、`RPC_GetLastFailedReason()`。

客户端接收侧的调用链摘自 `Engine\Source\Runtime\Engine\Private\DataReplication.cpp`。`FObjectReplicator::ReceivedRPC` 的关键分支（第 1429~1453 行，节选）：

```cpp
		RPC_ResetLastFailedReason();

		// See if the caller wants us to potentially skip the execution of this RPC
		const bool bCanDelayUnmapped = (SkipRpcBehavior == ESkipRpcBehavior::SkipIfNotReady) && (Function->FunctionFlags & FUNC_NetReliable);
		const bool bDelayUnmappedRPCs = bCanDelayUnmapped && (UnmappedGuids.Num() > 0 || PendingLocalRPCs.Num() > 0);
		bOutSkippedRpcExec = (bDelayUnmappedRPCs || SkipRpcBehavior == ESkipRpcBehavior::AlwaysSkip);

		if (!bOutSkippedRpcExec)
		{
			AActor* OwningActor = OwningChannel->Actor;
			UObject* const SubObject = Object != OwningChannel->Actor ? Object : nullptr;

			// Forward the function call.
			Connection->Driver->ForwardRemoteFunction(OwningActor, SubObject, Function, Parms);

			// Reset errors from replay driver
			RPC_ResetLastFailedReason();

			{
				// Call the function.
				if (Connection->Driver->IsExecuteRPCFunctionsEnabled())
				{
					CallProcessEventForReceivedRPC(Object, Function, Parms);
				}
			}
		}
```

以及 `FObjectReplicator::CallProcessEventForReceivedRPC` 全文（第 1479~1485 行，逐字）：

```cpp
void FObjectReplicator::CallProcessEventForReceivedRPC(UObject* Object, UFunction* Function, uint8* Params)
{
	UE::Net::FScopedNetContextRPC CallingRPC;
	UE::Net::Private::FScopedRemoteRPCMode ReceivingRemoteRPC(Function, UE::Net::Private::ERemoteFunctionMode::Receiving);

	Object->ProcessEvent(Function, Params);
}
```

1. **校验失败不返回错误码，而是就地 return**：`RPC_ValidateFailed` 只记录原因（供 `RPC_GetLastFailedReason` 读取，`ReceivedRPC` 第 1465~1469 行据此报 `LogRep, Error`）。发送侧并不知道对端校验失败——这正是「WithValidation 的 RPC 被拒后发送方无感知」的底层原因。
2. **`_Validate` 校验的是 thunk 收到的参数**，`FunctionThunkParameterNames` 与 `_Implementation` 同签名（除返回值）。UHT 在解析阶段会校验 `_Validate` 是否存在及其签名，缺失时报错（`UhtFunction.cs` 第 923/934 行的 `LogRpcFunctionError`）。
3. **执行被 `IsExecuteRPCFunctionsEnabled()` 门控**：这是 PlayInEditor / 网络模拟等场景下可以「只收不执行」的开关；关掉后 `ReceivedRPC` 依然完成反序列化与失败原因检查，但不触发 `ProcessEvent`。
4. **`FScopedNetContextRPC` + `FScopedRemoteRPCMode(Receiving)`** 标记「当前处于接收远端 RPC」的上下文，`UE::Net::Private::FScopedRemoteRPCMode` 会让 `UObject::ProcessEvent` 走接收模式（避免把收到的 RPC 再次当作本地发起并回发）。
5. **`net.DelayUnmappedRPCs`（默认 0）** 控制收到含未映射引用的可靠 RPC 时是延迟还是立刻以空参执行（`DataReplication.cpp` 第 44~50 行 cvar 说明：`if false RPCs will execute immediately with null parameters`）。

### 5. RPC 序列化的真实布局：`WriteFieldHeaderAndPayload`

`ProcessRemoteFunctionForChannelPrivate` 的后半段（`NetDriver.cpp` 第 3373~3431 行，节选）：

```cpp
	// Use the replication layout to send the rpc parameter values
	TSharedPtr<FRepLayout> RepLayout = GetFunctionRepLayout(Function);
	RepLayout->SendPropertiesForRPC(Function, Ch, TempWriter, Parms);

	if (TempWriter.IsError())
	{
		// …（节选：省略错误日志分支 9 行）
	}
	else
	{
		// Make sure net field export group is registered
		FNetFieldExportGroup* NetFieldExportGroup = Ch->GetOrCreateNetFieldExportGroupForClassNetCache(TargetObj);

		int32 HeaderBits	= 0;
		int32 ParameterBits	= 0;

		bool QueueBunch = false;
		switch (SendPolicy)
		{
			case ERemoteFunctionSendPolicy::Default:
				QueueBunch = ( !Bunch.bReliable && Function->FunctionFlags & FUNC_NetMulticast );
				break;
			case ERemoteFunctionSendPolicy::ForceQueue:
				QueueBunch = true;
				break;
			case ERemoteFunctionSendPolicy::ForceSend:
				QueueBunch = false;
				break;
		}

		if (QueueBunch)
		{
			Ch->WriteFieldHeaderAndPayload(Bunch, ClassCache, FieldCache, NetFieldExportGroup, TempWriter);
			ParameterBits = Bunch.GetNumBits();
		}
		else
		{
			Ch->PrepareForRemoteFunction(TargetObj);

			FNetBitWriter TempBlockWriter(Bunch.PackageMap, 0);

			// …（节选：省略 UE_NET_TRACE_ENABLED 的 collector 折叠注释与宏 10 行）

			Ch->WriteFieldHeaderAndPayload(TempBlockWriter, ClassCache, FieldCache, NetFieldExportGroup, TempWriter);
			ParameterBits = TempBlockWriter.GetNumBits();
			HeaderBits = Ch->WriteContentBlockPayload(TargetObj, Bunch, false, TempBlockWriter);

			UE_NET_TRACE_DESTROY_COLLECTOR(GetTraceCollector(TempBlockWriter));
		}
```

1. **RPC 参数由 `FRepLayout::SendPropertiesForRPC` 序列化**（`RepLayout.cpp` 第 7019 行），复用同一套 `FRepLayoutCmd` 机制——这也是 RPC 参数同样受 `FRepLayout` 兼容性校验约束的原因。
2. **两条写出路径**：入队（多播/不可靠，见第 6 点）直接写进 `Bunch`；立即发送则先写进 `TempBlockWriter` 得出 `ParameterBits`，再用 `Ch->WriteContentBlockPayload(...)` 写入 `Bunch` 并返回 `HeaderBits`。
3. **第 3449 行的断言是真实的位账本校验**：`check(Bunch.GetNumBits() == HeaderBits + ParameterBits);`——头部位数加参数位数必须精确等于 bunch 位数。
4. **函数索引在 `WriteFieldHeaderAndPayload` 内写出**（`DataReplication.cpp` 中同名方法见第 2789 行起，其注释为 `// Get the network friend property index to replicate` 与 `// Send property name and optional array index.`），使用 `ClassCache->GetFromField(Property/Function)` 得到的 `FFieldNetCache::FieldNetIndex`。所以「函数名不占网络带宽」的结论正确，但准确说法是**类内字段序号**，而非全局 `PackageMap` 编号。

---

## 核心源码深入剖析三：客户端属性反序列化与 `OnRep` 触发

客户端收到服务器发来的 `FInBunch` 后，通过 `FRepLayout::ReceiveProperties` 与 `CallRepNotifies` 唤醒表现层。

### 1. `FRepLayout::CallRepNotifies` 源码机制（真实签名）

（2026-09-14：原示意块已替换为 5.8 源码逐字版）

以下代码逐字摘自 `Engine\Source\Runtime\Engine\Private\RepLayout.cpp`（第 4661 行起，函数共 131 行，此处为节选）：

```cpp
void FRepLayout::CallRepNotifies(FReceivingRepState* RepState, UObject* Object) const
{
	if (RepState->RepNotifies.Num() == 0)
	{
		return;
	}

	if (IsEmpty())
	{
		UE_LOGF(LogRep, Error, "FRepLayout::CallRepNotifies: Empty layout with RepNotifies: %ls", *GetPathNameSafe(Owner));
		return;
	}

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RepNotifies_Generic);

	FRepShadowDataBuffer ShadowData(RepState->StaticBuffer.GetData());
	FRepObjectDataBuffer ObjectData(Object);

	for (FProperty* RepProperty : RepState->RepNotifies)
	{
		if (!Parents.IsValidIndex(RepProperty->RepIndex))
		{
			UE_LOGF(LogRep, Warning, "FRepLayout::CallRepNotifies: Called with invalid property %ls on object %ls.",
				*RepProperty->GetName(), *Object->GetName());
				continue;
		}

		UFunction* RepNotifyFunc = Object->FindFunction(RepProperty->RepNotifyFunc);

		if (RepNotifyFunc == nullptr)
		{
			UE_LOGF(LogRep, Warning, "FRepLayout::CallRepNotifies: Can't find RepNotify function %ls for property %ls on object %ls.",
				*RepProperty->RepNotifyFunc.ToString(), *RepProperty->GetName(), *Object->GetName());
			continue;
		}

		const FRepParentCmd& Parent = Parents[RepProperty->RepIndex];
		const int32 NumParms = RepNotifyFunc->NumParms;

		switch (NumParms)
		{
			case 0:
			{
				Object->ProcessEvent(RepNotifyFunc, nullptr);

				// TODO CopyCompleteValue no matter the field is replicated or not
				// will be a performance regression for any RepNotify arrays.
				// One fix is to track the incoming changelist and then resize the array and
				// recursively copy over only the fields we care about.
				if (EnumHasAnyFlags(Parent.Flags, ERepParentFlags::HasDynamicArrayProperties) && !EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsFastArray))
				{
					RepProperty->CopyCompleteValue(ShadowData + Parent, ObjectData + Parent);
				}
				break;
			}
			// …（节选：省略 case 1（带旧值参数）与 case 2（CustomDelta 元数据参数）共 60 行）
			default:
			{
				checkf(false, TEXT("FRepLayout::CallRepNotifies: Invalid number of parameters for property %s on object %s. NumParms=%d, CustomDelta=%d"),
					*RepProperty->GetName(), *Object->GetName(), NumParms, !!EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsCustomDelta));
				break;
			}
		}
	}

	RepState->RepNotifies.Empty();
	RepState->RepNotifyMetaData.Empty();
}
```

1. **真实签名是 `CallRepNotifies(FReceivingRepState* RepState, UObject* Object) const`**，不是 `CallRepNotifies(FRepNotifies&, UObject*)`；`RepNotifies` 是 `FReceivingRepState` 的成员（`RepLayout.h` 第 557 行，`TArray<FProperty*> RepNotifies;`）。
2. **参数个数三分支（`switch (NumParms)`）**：
   - `case 0`：`Object->ProcessEvent(RepNotifyFunc, nullptr)`，无参 `OnRep_XXX()`；
   - `case 1`：把 `ShadowData + Parent`（影子缓冲中该属性的**旧值**）作为参数传入，即 `OnRep_XXX(OldValue)`；
   - `case 2`：`check(EnumHasAnyFlags(Parent.Flags, ERepParentFlags::IsCustomDelta))`，从 `RepState->RepNotifyMetaData.Find(RepProperty)` 取元数据（数组索引等）作为第二参数——**仅 Custom Delta 属性（如 `FFastArraySerializer`）才有此形态**；
   - 其他参数个数直接 `checkf(false)` 视为非法。
3. **`FRepShadowDataBuffer ShadowData(RepState->StaticBuffer.GetData())`**：传给 `OnRep` 的「旧值」读写自 `FReceivingRepState::StaticBuffer`。这就是为什么带参 `OnRep` 能拿到上一个值——影子缓冲在 `ReceiveProperties_r` 写对象内存**之前**保持了旧值（见下节时序）。
4. **`case 0` 之后的条件性 `CopyCompleteValue`**：只有「含动态数组属性且非 FastArray」的 Parent 才把 `ObjectData` 回抄进 `ShadowData`。源码 TODO 注释直言这对 RepNotify 数组是性能回归，因为回抄的是整个属性而非仅复制字段。
5. **`RepState->RepNotifies.Empty()` 与 `RepNotifyMetaData.Empty()` 在函数末尾**：通知列表是「一次性消费」的——每次 `CallRepNotifies` 处理完即清空，因此 `OnRep` 不会被重复派发（除非再次收到属性更新）。
6. **通知入队点**：`FObjectReplicator::QueuePropertyRepNotify`（`Engine\Source\Runtime\Engine\Private\DataReplication.cpp` 第 2737 行起，逐字节选）：

```cpp
	if (!Property->HasAnyPropertyFlags(CPF_RepNotify))
	{
		return;
	}

	FReceivingRepState* ReceivingRepState = RepState.IsValid() ? RepState->GetReceivingRepState() : nullptr;
	if (ensureMsgf(ReceivingRepState, TEXT("FObjectReplicator::QueuePropertyRepNotifiy: No receiving RepState. Object=%s, Property=%s"),
		*GetPathNameSafe(Object), *Property->GetName()))
	{
		//@note: AddUniqueItem() here for static arrays since RepNotify() currently doesn't indicate index,
		//			so reporting the same property multiple times is not useful and wastes CPU
		//			were that changed, this should go back to AddItem() for efficiency
		// @todo UE - not checking if replicated value is changed from old.  Either fix or document, as may get multiple repnotifies of unacked properties.
		ReceivingRepState->RepNotifies.AddUnique(Property);

		UFunction* RepNotifyFunc = Object->FindFunctionChecked(Property->RepNotifyFunc);

		if (RepNotifyFunc->NumParms > 0)
		{
			if (Property->ArrayDim != 1)
			{
				// For static arrays, we build the meta data here, but adding the Element index that was just read into the PropMetaData array.
				UE_LOGF(LogRepTraffic, Verbose, "Property %ls had ArrayDim: %d change", *Property->GetName(), ElementIndex);

				// Property is multi dimensional, keep track of what elements changed
				TArray< uint8 > & PropMetaData = ReceivingRepState->RepNotifyMetaData.FindOrAdd(Property);
				PropMetaData.Add(ElementIndex);
			}
			// …（节选：省略 MetaData.Num() > 0 分支 10 行）
		}
	}
```

   注意 `AddUnique` 与源码里那条 `@todo UE - not checking if replicated value is changed from old` 注释：**入队时不判断新旧值是否相等**。「值没变就不触发」这一保守说法在 5.8 的 `QueuePropertyRepNotify` 中并不成立——同一属性在一帧内多次入队会被去重，但跨帧重复收到相同值仍会再次入队。判定是否触发的真实控制项是 `ELifetimeRepNotifyCondition`（`REPNOTIFY_OnChanged` / `REPNOTIFY_Always`），它在 `FRepParentCmd::RepNotifyCondition`（`RepLayout.h` 第 827 行）中保存并在比较阶段生效。

- **REPNOTIFY_Always vs OnChanged**：`REPNOTIFY_OnChanged`（默认）只在变更比较命中时把属性加入 changelist，从而间接影响 `OnRep` 是否发生；使用 `DOREPLIFETIME_CONDITION_NOTIFY(..., REPNOTIFY_Always)` 时该属性每包都会进入通知列表。二者的判定数据都在 `FRepParentCmd::RepNotifyCondition`，而不是 `CallRepNotifies` 内部。

### 2. OnRep 为何在属性写入之后才调用（真实时序）

服务器把属性值写入客户端内存的调用链是 `UActorChannel::ReceivedBunch` → `FObjectReplicator::ReceivedBunch`（`DataReplication.cpp` 第 984 行）→ `FRepLayout::ReceiveProperties`（第 3789 行）。`ReceiveProperties` 逐字摘自 `Engine\Source\Runtime\Engine\Private\RepLayout.cpp`（第 3789~3870 行，全函数 82 行）：

```cpp
bool FRepLayout::ReceiveProperties(
	UActorChannel* OwningChannel,
	UClass* InObjectClass,
	FReceivingRepState* RESTRICT RepState,
	UObject* Object,
	FNetBitReader& InBunch,
	bool& bOutHasUnmapped,
	bool& bOutGuidsChanged,
	const EReceivePropertiesFlags ReceiveFlags) const
{
	check(InObjectClass == Owner);

	FRepObjectDataBuffer Data(Object);
	const bool bEnableRepNotifies = EnumHasAnyFlags(ReceiveFlags, EReceivePropertiesFlags::RepNotifies);

	UE_NET_TRACE_SCOPE(Properties, InBunch, OwningChannel->Connection->GetInTraceCollector(), ENetTraceVerbosity::Trace);

	if (OwningChannel->Connection->IsInternalAck())
	{
		return ReceiveProperties_BackwardsCompatible(OwningChannel->Connection, RepState, Data, InBunch, bOutHasUnmapped, bEnableRepNotifies, bOutGuidsChanged, Object);
	}

#ifdef ENABLE_PROPERTY_CHECKSUMS
	const bool bDoChecksum = InBunch.ReadBit() ? true : false;
#else
	const bool bDoChecksum = false;
#endif

	UE_LOGF(LogRepProperties, VeryVerbose, "ReceiveProperties: Owner=%ls", *Owner->GetPathName());

	bOutHasUnmapped = false;

	// If we've gotten this far, it means that the server must have sent us something.
	// That should only happen if there's actually commands to process.
	// If this is hit, it may mean the Client and Server have different properties!
	check(!IsEmpty());

	FReceivePropertiesSharedParams Params{
		bDoChecksum,
		// We can skip swapping roles if we're not an Actor layout, or if we've been explicitly told we can skip.
		EnumHasAnyFlags(ReceiveFlags, EReceivePropertiesFlags::SkipRoleSwap) || !EnumHasAnyFlags(Flags, ERepLayoutFlags::IsActor),
		InBunch,
		bOutHasUnmapped,
		bOutGuidsChanged,
		Parents,
		Cmds,
		NetSerializeLayouts,
		Object,
		OwningChannel->Connection->GetInTraceCollector()
	};

	FReceivePropertiesStackParams StackParams{
		FRepObjectDataBuffer(Data),
		FRepShadowDataBuffer(RepState->StaticBuffer.GetData()),
		&RepState->GuidReferencesMap,
		0,
		Cmds.Num() - 1,
		bEnableRepNotifies ? &RepState->RepNotifies : nullptr
	};

	// Read the first handle, and then start receiving properties.
	ReadPropertyHandle(Params);
	if (ReceiveProperties_r(Params, StackParams))
	{
		if (0 != Params.ReadHandle)
		{
			UE_LOGF(LogRep, Error, "ReceiveProperties: Invalid property terminator handle - Handle=%d", Params.ReadHandle);
			return false;
		}

#ifdef ENABLE_SUPER_CHECKSUMS
		if (bDoChecksum)
		{
			ValidateWithChecksum<>(FConstRepShadowDataBuffer(RepState->StaticBuffer.GetData()), InBunch);
		}
#endif

		return true;
	}

	return false;
}
```

而通知的实际触发点在 `FObjectReplicator::PostReceivedBunch`（`Engine\Source\Runtime\Engine\Private\DataReplication.cpp` 第 1592~1610 行，全函数逐字）：

```cpp
void FObjectReplicator::PostReceivedBunch()
{
	if ( GetObject() == nullptr )
	{
		UE_LOGF(LogNet, Verbose, "PostReceivedBunch: Object == nullptr");
		return;
	}

	// Call PostNetReceive
	const bool bIsServer = (OwningChannel->Connection->Driver->ServerConnection == nullptr);
	if (!bIsServer && bHasReplicatedProperties)
	{
		PostNetReceive();
		bHasReplicatedProperties = false;
	}

	// Call RepNotifies
	CallRepNotifies(true);
}
```

结论（严格对应源码）：

1. **`FReceivePropertiesStackParams` 同时持有两块内存**：`FRepObjectDataBuffer(Data)` 指向**真实对象内存**，`FRepShadowDataBuffer(RepState->StaticBuffer.GetData())` 指向**影子缓冲**。`ReceiveProperties_r` 一边把新值从 bunch 读出写入对象内存，一边把**旧值留在影子缓冲**中，并在写完后回写影子。
2. **通知只是「入队」**：`bEnableRepNotifies ? &RepState->RepNotifies : nullptr` 把通知数组指针传进 `StackParams`；读取过程中只往这个数组里 `AddUnique` 属性（`QueuePropertyRepNotify`），**不执行任何 `OnRep`**。
3. **派发被推迟到 `PostReceivedBunch`**：`UActorChannel::ReceivedBunch` 处理完整个 bunch 后才调用 `Replicator.PostReceivedBunch()`，其中先 `PostNetReceive()`（`AActor::PostNetReceive` 虚函数）、再 `CallRepNotifies(true)`（`FObjectReplicator::CallRepNotifies`，第 2431 行；形参 `bSkipIfChannelHasQueuedBunches` 对应此处的 `true`）。
4. **这正是 `OnRep` 能看到完整新值的根本原因**：一个 bunch 内可能有多个相关属性，若在读到第一个属性时就回调，`OnRep` 里读到其它属性仍是旧值。推迟到 bunch 处理完毕，保证 `OnRep` 内看到的是**同一批更新之后的完整一致状态**，且带参 `OnRep` 仍能从影子缓冲取到旧值。
5. **`PostNetReceive` 先于 `OnRep`**：`PostNetReceive` 仅在客户端（`!bIsServer`）且本包确实有属性复制（`bHasReplicatedProperties`）时调用一次并复位该标志；随后才是全部 `OnRep`。

### 3. `GetLifetimeReplicatedProps` 真实签名与 `DOREPLIFETIME` 宏展开

真实签名（`Engine\Source\Runtime\Engine\Classes\GameFramework\Actor.h` 第 306 行，逐字）：

```cpp
	ENGINE_API virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
```

宏定义的真实展开（`Engine\Source\Runtime\Engine\Public\Net\UnrealNetwork.h` 第 231~293 行，逐字）：

```cpp
#define DOREPLIFETIME_WITH_PARAMS_FAST(c,v,params) \
{ \
	static_assert(ValidateReplicatedClassInheritance<c, ThisClass>(), #c "." #v " is not accessible from this class."); \
	const TCHAR* DoRepPropertyName_##c_##v(TEXT(#v)); \
	const NetworkingPrivate::FRepPropertyDescriptor PropertyDescriptor_##c_##v(DoRepPropertyName_##c_##v, (int32)c::ENetFields_Private::v, 1); \
\
	PRAGMA_DISABLE_DEPRECATION_WARNINGS \
	RegisterReplicatedLifetimeProperty(PropertyDescriptor_##c_##v, OutLifetimeProps, FixupParams<decltype(c::v)>(params)); \
	PRAGMA_ENABLE_DEPRECATION_WARNINGS \
}

// …（节选：省略 DOREPLIFETIME_WITH_PARAMS_FAST_STATIC_ARRAY 静态数组版 7 行）

#define DOREPLIFETIME_WITH_PARAMS(c,v,params) \
{ \
	static_assert(ValidateReplicatedClassInheritance<c, ThisClass>(), #c "." #v " is not accessible from this class."); \
	FProperty* ReplicatedProperty = GetReplicatedProperty(StaticClass(), c::StaticClass(), GET_MEMBER_NAME_CHECKED(c,v)); \
	PRAGMA_DISABLE_DEPRECATION_WARNINGS \
	RegisterReplicatedLifetimeProperty(ReplicatedProperty, OutLifetimeProps, FixupParams<decltype(c::v)>(params)); \
	PRAGMA_ENABLE_DEPRECATION_WARNINGS \
}

#define DOREPLIFETIME(c,v) DOREPLIFETIME_WITH_PARAMS(c,v,FDoRepLifetimeParams())

/** This macro is used by nativized code (DynamicClasses), so the Property may be recreated. */
#define DOREPLIFETIME_DIFFNAMES(c,v, n) \
{ \
	static TWeakFieldPtr<FProperty> __swp##v{};							\
	const FProperty* sp##v = __swp##v.Get();								\
	if (nullptr == sp##v)													\
	{																		\
		sp##v = GetReplicatedProperty(StaticClass(), c::StaticClass(), n);	\
		__swp##v = sp##v;													\
	}																		\
	for ( int32 i = 0; i < sp##v->ArrayDim; i++ )							\
	{																		\
		OutLifetimeProps.AddUnique( FLifetimeProperty( sp##v->RepIndex + i ) );	\
	}																		\
}

#define DOREPLIFETIME_CONDITION(c,v,cond) \
{ \
	static_assert(cond != COND_NetGroup, "COND_NetGroup cannot be used on replicated properties. Only when registering subobjects"); \
	FDoRepLifetimeParams LocalDoRepParams; \
	LocalDoRepParams.Condition = cond; \
	DOREPLIFETIME_WITH_PARAMS(c,v,LocalDoRepParams); \
}

/** Allows gamecode to specify RepNotify condition: REPNOTIFY_OnChanged (default) or REPNOTIFY_Always for when repnotify function is called  */
#define DOREPLIFETIME_CONDITION_NOTIFY(c,v,cond,rncond) \
{ \
	static_assert(cond != COND_NetGroup, "COND_NetGroup cannot be used on replicated properties. Only when registering subobjects"); \
	FDoRepLifetimeParams LocalDoRepParams; \
	LocalDoRepParams.Condition = cond; \
	LocalDoRepParams.RepNotifyCondition = rncond; \
	DOREPLIFETIME_WITH_PARAMS(c,v,LocalDoRepParams); \
}
```

1. **`DOREPLIFETIME` 本身不做任何网络动作**，它只是在 `GetLifetimeReplicatedProps` 执行时把一条 `FDoRepLifetimeParams` 描述追加进 `OutLifetimeProps`。真正的注册发生在 `RegisterReplicatedLifetimeProperty`，数据消费者是 `FRepLayout` 的构建流程。
2. **`DOREPLIFETIME` 走 `GetReplicatedProperty(StaticClass(), c::StaticClass(), GET_MEMBER_NAME_CHECKED(c,v))` 按名字查属性**；`_FAST` 变体改用 `c::ENetFields_Private::v` 这个**编译期整型索引**（`FRepPropertyDescriptor` 的第二个参数），避开运行时名字查找。这就是 `_FAST` 后缀的性能含义。
3. **`DOREPLIFETIME_CONDITION` 只是给 `FDoRepLifetimeParams::Condition` 赋值**，然后转发到 `DOREPLIFETIME_WITH_PARAMS`。`COND_NetGroup` 被 `static_assert` 明确禁止用于属性（只能用于子对象注册）。
4. **`DOREPLIFETIME_CONDITION_NOTIFY` 的第四个参数就是 `RepNotifyCondition`**，即 `REPNOTIFY_OnChanged` / `REPNOTIFY_Always`，最终落到 `FRepParentCmd::RepNotifyCondition`。
5. **`DOREPLIFETIME_DIFFNAMES` 是唯一直接使用 `RepIndex` 的宏**：`FLifetimeProperty(sp##v->RepIndex + i)`。这佐证了 `RepIndex` 属于 `FProperty`（属性上由 UHT 分配的复制索引），而**不属于** `FRepLayoutCmd`。
6. **条件枚举来自 `ELifetimeCondition`**（`Engine\Source\Runtime\CoreUObject\Public\UObject\CoreNetTypes.h`，`COND_None = 0` … `COND_Max = 17`），`FRepParentCmd::Condition` 在 `RepLayout.h` 第 826 行保存该值。

### 4. 本节事实边界

- 上述结论全部来自静态源码阅读；**未**在运行态用抓包或网络剖析器验证过实际字节数、帧内触发次数或带宽占用。
- `ELifetimeCondition` 目前有 `COND_None`(0) 到 `COND_Max`(17) 共 17 个可用条件（`COND_Max` 为哨兵）。本文只覆盖示例中出现的 `COND_OwnerOnly` / `COND_SimulatedOnly` / `COND_InitialOnly`，其余条件的判定位置在 `FRepLayout::RebuildConditionalProperties` 与 `FilterChangeList`（`RepLayout.cpp` 中 `ReplicateProperties` 调用它们），本文未逐条展开。

---

## 核心源码深入剖析五：`FRepLayout` 属性复制、影子缓冲与 `COND_*` 条件

> 本节为 2026-09-14 补深新增。5.8 中「影子内存」的落点与前几版文章常见的描述不同：发送侧的对比基准是 `FRepChangelistState::StaticBuffer`，接收侧才用 `FReceivingRepState::StaticBuffer`，而 `FRepState` 自身不持有任何缓冲。

### 1. 数据结构的真实归属（含 `RepIndex` 与 `COND_*` 的落点）

`FRepLayoutCmd` 全文摘自 `Engine\Source\Runtime\Engine\Public\Net\RepLayout.h`（第 856~886 行，逐字）：

```cpp
class FRepLayoutCmd
{
public:

	/** Pointer back to property, used for NetSerialize calls, etc. */
	FProperty* Property;

	/** For arrays, this is the cmd index to jump to, to skip this arrays inner elements. */
	uint16 EndCmd;

	/** For arrays, element size of data. */
	uint16 ElementSize;

	/** Absolute offset of property in Object Memory. */
	int32 Offset;

	/** Absolute offset of property in Shadow Memory. */
	int32 ShadowOffset;

	/** Handle relative to start of array, or top list. */
	uint16 RelativeHandle;

	/** Index into Parents. */
	uint16 ParentIndex;

	/** Used to determine if property is still compatible */
	uint32 CompatibleChecksum;

	ERepLayoutCmdType Type;
	ERepLayoutCmdFlags Flags;
};
```

`FRepParentCmd` 全文摘自同一文件（第 780~837 行，逐字）：

```cpp
class FRepParentCmd
{
public:

	FRepParentCmd(FProperty* InProperty, int32 InArrayIndex):
		Property(InProperty),
		CachedPropertyName(InProperty ? InProperty->GetFName() : NAME_None),
		ArrayIndex(InArrayIndex),
		ShadowOffset(0),
		CmdStart(0),
		CmdEnd(0),
		Condition(COND_None),
		RepNotifyCondition(REPNOTIFY_OnChanged),
		RepNotifyNumParams(INDEX_NONE),
		Flags(ERepParentFlags::None)
	{}

	FProperty* Property;

	const FName CachedPropertyName;

	/**
	 * If the Property is a C-Style fixed size array, then a command will be created for every element in the array.
	 * This is the index of the element in the array for which the command represents.
	 *
	 * This will always be 0 for non array properties.
	 */
	int32 ArrayIndex;

	/** Absolute offset of property in Object Memory. */
	int32 Offset;

	/** Absolute offset of property in Shadow Memory. */
	int32 ShadowOffset;

	/**
	 * CmdStart and CmdEnd define the range of FRepLayoutCommands (by index in FRepLayouts Cmd array) of commands
	 * that are associated with this Parent Command.
	 *
	 * This is used to track and access nested Properties from the parent.
	 */
	uint16 CmdStart;

	/** @see CmdStart */
	uint16 CmdEnd;

	ELifetimeCondition Condition;
	ELifetimeRepNotifyCondition RepNotifyCondition;

	/**
	 * Number of parameters that we need to pass to the RepNotify function (if any).
	 * If this value is INDEX_NONE, it means there is no RepNotify function associated
	 * with the property.
	 */
	int32 RepNotifyNumParams;

	ERepParentFlags Flags;
};
```

容器结构的真实声明同样摘自 `RepLayout.h`（`FRepStateStaticBuffer` 第 376~424 行、`FReceivingRepState` 第 538~564 行、`FRepState` 第 671~708 行，逐字节选）：

```cpp
struct FRepStateStaticBuffer : public FNoncopyable
{
	// …（节选：省略私有构造、移动构造与访问器 30 行）
private:

	// Properties will be copied in here so memory needs aligned to largest type
	TArray<uint8, TAlignedHeapAllocator<16>> Buffer;
	TSharedRef<const FRepLayout> RepLayout;
};

/** Replication State needed to track received properties. */
class FReceivingRepState : public FNoncopyable
{
	// …（节选：省略私有构造与 friend 声明）
public:

	void CountBytes(FArchive& Ar) const;

	/** Latest state of all property data. Only valid on clients. */
	FRepStateStaticBuffer StaticBuffer;

	/** Map of Absolute Property Offset to GUID Reference for properties. */
	FGuidReferencesMap GuidReferencesMap;

	/** List of properties that have RepNotifies that we will need to call on Clients. */
	TArray<FProperty*> RepNotifies;

	/**
	 * Holds MetaData (such as array index) for RepNotifies.
	 * Only used for CustomDeltaProperties.
	 */
	TMap<FProperty*, TArray<uint8>> RepNotifyMetaData;
};

/** Replication State that is unique Per Object Per Net Connection. */
class FRepState : public FNoncopyable
{
	// …（节选：省略私有构造与 friend 声明 6 行）
private:

	/** May be null on connections that don't receive properties. */
	TUniquePtr<FReceivingRepState> ReceivingRepState;

	/** May be null on connections that don't send properties. */
	TUniquePtr<FSendingRepState> SendingRepState;

public:
	// …（节选：省略 CountBytes 与四个访问器 26 行）
};
```

1. **`RepIndex` 不在 `FRepLayoutCmd` 上**：`FRepLayoutCmd` 的成员是 `Property / EndCmd / ElementSize / Offset / ShadowOffset / RelativeHandle / ParentIndex / CompatibleChecksum / Type / Flags` 共 10 个，**没有 `RepIndex`，也没有 `SendingProxy`**（ripgrep 核对零命中）。`RepIndex` 是 `FProperty` 的成员（由 UHT 分配），在 `FRepLayout::CallRepNotifies` 中以 `RepProperty->RepIndex` 作为 `Parents` 的下标使用；`DOREPLIFETIME_DIFFNAMES` 中同样使用 `sp##v->RepIndex`。
2. **`COND_*` 的落点是 `FRepParentCmd::Condition`（第 826 行）**，类型为 `ELifetimeCondition`；`REPNOTIFY_*` 的落点是紧邻的 `RepNotifyCondition`（第 827 行）。二者都在 **Parent Cmd（顶层属性）** 上，不在每个 element 的 `FRepLayoutCmd` 上——这是「条件复制以顶层属性为粒度」的源码依据。
3. **两套 offset**：`FRepLayoutCmd::Offset` 是对象内存偏移，`ShadowOffset` 是影子内存偏移，二者不必相等（结构体布局差异），这也是不能用一次整块 memcpy 完成同步的原因之一。
4. **`CompatibleChecksum`**：用于判定客户端与服务器该属性是否兼容（不兼容属性在接收侧被跳过），是「客户端/服务器类定义不一致时不崩、但静默丢属性」的机制来源。
5. **`FRepState` 是纯容器**：只持有 `TUniquePtr<FReceivingRepState>` 与 `TUniquePtr<FSendingRepState>`，**没有 `StaticBuffer`/`DynamicBuffer` 成员**。因此「`FRepState::DynamicBuffer`」这一说法在 5.8 中不成立（ripgrep 在 `RepLayout.h` 中零命中）；接收侧影子缓冲是 `FReceivingRepState::StaticBuffer`（第 551 行），发送侧比较基准是 `FRepChangelistState::StaticBuffer`。
6. **影子缓冲的存储类型**：`TArray<uint8, TAlignedHeapAllocator<16>>`——按最大类型做 16 字节对齐，源码注释写明 `Properties will be copied in here so memory needs aligned to largest type`。`FRepStateStaticBuffer` 还持有 `TSharedRef<const FRepLayout> RepLayout`，因此它知道自己属于哪个 layout。

### 2. 比较的真实实现：`CompareProperties_r` 与 `PropertiesAreIdentical`

`CompareProperties_r` 全文摘自 `Engine\Source\Runtime\Engine\Private\RepLayout.cpp`（第 1648~1690 行，逐字）：

```cpp
static uint16 CompareProperties_r(
	const FComparePropertiesSharedParams& SharedParams,
	FComparePropertiesStackParams& StackParams,
	const uint16 CmdStart,
	const uint16 CmdEnd,
	uint16 Handle)
{
	for (int32 CmdIndex = CmdStart; CmdIndex < CmdEnd; ++CmdIndex)
	{
		const FRepLayoutCmd& Cmd = SharedParams.Cmds[CmdIndex];

		check(Cmd.Type != ERepLayoutCmdType::Return);

		++Handle;

		const FConstRepObjectDataBuffer Data = StackParams.Data + Cmd;
		FRepShadowDataBuffer ShadowData = StackParams.ShadowData + Cmd;

		UE_LOGF(LogRepCompares, VeryVerbose, "CompareProperties_r: CmdIndex: %d CmdType: %ls Property: %ls", CmdIndex, LexToString(Cmd.Type), *GetNameSafe(Cmd.Property));

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			FComparePropertiesStackParams NewStackParams{
				Data,
				ShadowData,
				StackParams.Changed,
				StackParams.Result
			};

			// Once we hit an array, start using a stack based approach
			CompareProperties_Array_r(SharedParams, NewStackParams, CmdIndex, Handle);
			CmdIndex = Cmd.EndCmd - 1;		// The -1 to handle the ++ in the for loop
			continue;
		}
		else if (SharedParams.bForceFail || !PropertiesAreIdentical(Cmd, ShadowData.Data, Data.Data, SharedParams.NetSerializeLayouts))
		{
			StoreProperty(Cmd, ShadowData.Data, Data.Data);
			StackParams.Changed.Add(Handle);
		}
	}

	return Handle;
}
```

按类型分派的比较原语（第 668~727 行，节选）：

```cpp
static FORCEINLINE bool PropertiesAreIdenticalNative(
	const FRepLayoutCmd& Cmd,
	const void* A,
	const void* B,
	const TMap<FRepLayoutCmd*, TArray<FRepLayoutCmd>>& NetSerializeLayouts)
{
	switch (Cmd.Type)
	{
		case ERepLayoutCmdType::PropertyBool:
			return CompareBool(Cmd, A, B);

		case ERepLayoutCmdType::PropertyNativeBool:
			return CompareValue<bool>(A, B);

		case ERepLayoutCmdType::PropertyByte:
			return CompareValue<uint8>(A, B);

		case ERepLayoutCmdType::PropertyFloat:
			return CompareValue<float>(A, B);

		case ERepLayoutCmdType::PropertyInt:
			return CompareValue<int32>(A, B);

		case ERepLayoutCmdType::PropertyName:
			return CompareValue<FName>(A, B);

		case ERepLayoutCmdType::PropertyObject:
			return CompareObject(Cmd, A, B);

		case ERepLayoutCmdType::PropertySoftObject:
			return CompareSoftObject(Cmd, A, B);

		case ERepLayoutCmdType::PropertyWeakObject:
			return CompareWeakObject(Cmd, A, B);

		case ERepLayoutCmdType::PropertyInterface:
			return CompareInterface(Cmd, A, B);

		case ERepLayoutCmdType::PropertyUInt32:
			return CompareValue<uint32>(A, B);

		case ERepLayoutCmdType::PropertyUInt64:
			return CompareValue<uint64>(A, B);

		case ERepLayoutCmdType::PropertyVector:
			return CompareValue<FVector>(A, B);
		// …（节选：省略其余 NetQuantize/Plane/Rotator/NetId/RepMovement/String/NetSerialize 等分支 25 行）
		default:
			UE_LOGF(LogRep, Fatal, "PropertiesAreIdentical: Unsupported type! %i (%ls)", (uint8)Cmd.Type, *Cmd.Property->GetName());
	}

	return false;
}
```

1. **比较是「按 Cmd 逐条」，不是「整块内存逐字节」**：每条 `FRepLayoutCmd` 代表一个可复制单元（顶层属性或数组内元素），比较用 `PropertiesAreIdentical(Cmd, ShadowData.Data, Data.Data, ...)`，即把 `Cmd` 交给类型化比较函数。所以正确的表述是「逐**属性**比较」，而「逐字节 memcmp」的比喻只在单条 Cmd 的底层实现（`CompareValue<T>`）意义上成立。
2. **命中变更后的两个动作（第 1682~1684 行）**：
   - `StoreProperty(Cmd, ShadowData.Data, Data.Data)`——**把新值写入影子缓冲**；
   - `StackParams.Changed.Add(Handle)`——把该 Cmd 的 Handle 追加进 changelist。
   因此影子缓冲是「上次已发送状态的镜像」，下一次比较的基准就是它。
3. **`bForceFail` 是强制重发开关**：`SharedParams.bForceFail || !PropertiesAreIdentical(...)` 使得条件为真时所有 Cmd 都被视为已变更。它对应 `bNetInitial`、`ForceRefreshUnreliableProperties`、`bForceCompareProperties` 等场景。
4. **动态数组走栈式递归**：`Cmd.Type == ERepLayoutCmdType::DynamicArray` 时用 `CompareProperties_Array_r`，并通过 `CmdIndex = Cmd.EndCmd - 1` 跳过该数组内部的全部 Cmd（外层 for 会再 `++`）。`FLifetimeProperty` 数组元素是独立 Handle，因此数组内单个元素变化只发该元素，不必整数组重发。
5. **`PropertyObject` 等方法比较的是 NetGUID 而非指针值**：`CompareObject` / `CompareSoftObject` / `CompareWeakObject` / `CompareInterface` 走各自专用函数（而非 `CompareValue<T>`），因为它们需要按网络映射后的标识判定「是否真的需要重发」。
6. **类型不支持即 `Fatal`**：`default` 分支直接 `UE_LOGF(LogRep, Fatal, ...)`，说明 `ERepLayoutCmdType` 与比较函数必须一一对应，新增 Cmd 类型而忘记补比较分支会导致致命错误。
7. **三处不同入参形式的 `PropertiesAreIdentical`**：`USE_CUSTOM_COMPARE` 定义时（第 808~823 行）先调 `PropertiesAreIdenticalNative` 并可选做一致性断言，未定义时（第 825~832 行）退化为 `Cmd.Property->Identical(A, B)` 反射比较。因此「走优化路径还是反射 `Identical`」取决于编译宏。

### 3. 发送侧：`ReplicateProperties` → `SendProperties` → `SendProperties_r`

`FRepLayout::SendProperties` 全文摘自 `Engine\Source\Runtime\Engine\Private\RepLayout.cpp`（第 2948~2997 行，逐字）：

```cpp
void FRepLayout::SendProperties(
	FSendingRepState* RESTRICT RepState,
	FRepChangedPropertyTracker* ChangedTracker,
	const FConstRepObjectDataBuffer Data,
	UClass* ObjectClass,
	FNetBitWriter& Writer,
	TArray<uint16>& Changed,
	const FRepSerializationSharedInfo& SharedInfo,
	const ESerializePropertyType SerializePropertyType) const
{
	SCOPE_CYCLE_COUNTER(STAT_NetReplicateDynamicPropSendTime);

	if (IsEmpty())
	{
		return;
	}

#ifdef ENABLE_PROPERTY_CHECKSUMS
	const bool bDoChecksum = (GDoPropertyChecksum == 1);
#else
	const bool bDoChecksum = false;
#endif


	UE_NET_TRACE_SCOPE(Properties, Writer, GetTraceCollector(Writer), ENetTraceVerbosity::Trace);
	FBitWriterMark Mark(Writer);

#ifdef ENABLE_PROPERTY_CHECKSUMS
	Writer.WriteBit(bDoChecksum ? 1 : 0);
#endif

	const int32 NumBits = Writer.GetNumBits();

	UE_LOGF(LogRepProperties, VeryVerbose, "SendProperties: Owner=%ls, LastChangelistIndex=%d", *Owner->GetPathName(), RepState->LastChangelistIndex);

	FChangelistIterator ChangelistIterator(Changed, 0);
	FRepHandleIterator HandleIterator(Owner, ChangelistIterator, Cmds, BaseHandleToCmdIndex, 0, 1, 0, Cmds.Num() - 1);

	SendProperties_r(RepState, Writer, bDoChecksum, HandleIterator, Data, 0, &SharedInfo, SerializePropertyType);

	if (NumBits != Writer.GetNumBits())
	{
		// We actually wrote stuff
		WritePropertyHandle(Writer, 0, bDoChecksum);
	}
	else
	{
		Mark.Pop(Writer);
	}
}
```

`SendProperties_r` 的数组分支与控制流（第 2767~2846 行，节选）：

```cpp
void FRepLayout::SendProperties_r(
	FSendingRepState* RESTRICT RepState,
	FNetBitWriter& Writer,
	const bool bDoChecksum,
	FRepHandleIterator& HandleIterator,
	const FConstRepObjectDataBuffer SourceData,
	const int32 ArrayDepth,
	const FRepSerializationSharedInfo* const RESTRICT SharedInfo,
	const ESerializePropertyType SerializePropertyType) const
{
	const bool bDoSharedSerialization = SharedInfo && !!GNetSharedSerializedData;

	while (HandleIterator.NextHandle())
	{
		const FRepLayoutCmd& Cmd = Cmds[HandleIterator.CmdIndex];
		const FRepParentCmd& ParentCmd = Parents[Cmd.ParentIndex];

		UE_LOGF(LogRepProperties, VeryVerbose, "SendProperties_r: Parent=%d, Cmd=%d, ArrayIndex=%d", Cmd.ParentIndex, HandleIterator.CmdIndex, HandleIterator.ArrayIndex);

		FConstRepObjectDataBuffer Data = (SourceData + Cmd) + HandleIterator.ArrayOffset;

		if (Cmd.Type == ERepLayoutCmdType::DynamicArray)
		{
			if (SerializePropertyType == ESerializePropertyType::Handle)
			{
				WritePropertyHandle(Writer, HandleIterator.Handle, bDoChecksum);
			}
			else if (SerializePropertyType == ESerializePropertyType::Name)
			{
				WritePropertyName(Writer, Cmd.Property->GetFName(), bDoChecksum);
			}

			UE_NET_TRACE_DYNAMIC_NAME_SCOPE(Cmd.Property->GetFName(), Writer, GetTraceCollector(Writer), ENetTraceVerbosity::Trace);

			const FScriptArray* Array = (FScriptArray *)Data.Data;
			const FConstRepObjectDataBuffer ArrayData(Array->GetData());

			// Write array num
			uint16 ArrayNum = Array->Num();
			Writer << ArrayNum;

			UE_LOGF(LogRepProperties, VeryVerbose, "SendProperties_r: ArrayNum=%d", ArrayNum);

			// Read the jump offset
			// We won't need to actually jump over anything because we expect the change list to be pruned once we get here
			// But we can use it to verify we read the correct amount.
			const int32 ArrayChangedCount = HandleIterator.ChangelistIterator.Changed[HandleIterator.ChangelistIterator.ChangedIndex++];

			const int32 OldChangedIndex = HandleIterator.ChangelistIterator.ChangedIndex;

			TArray<FHandleToCmdIndex>& ArrayHandleToCmdIndex = *HandleIterator.HandleToCmdIndex[Cmd.RelativeHandle - 1].HandleToCmdIndex;

			FRepHandleIterator ArrayHandleIterator(HandleIterator.Owner, HandleIterator.ChangelistIterator, Cmds, ArrayHandleToCmdIndex, Cmd.ElementSize, ArrayNum, HandleIterator.CmdIndex + 1, Cmd.EndCmd - 1);

			check(ArrayHandleIterator.ArrayElementSize> 0);
			check(ArrayHandleIterator.NumHandlesPerElement> 0);

			SendProperties_r(RepState, Writer, bDoChecksum, ArrayHandleIterator, ArrayData, ArrayDepth + 1, SharedInfo, SerializePropertyType);

			check(HandleIterator.ChangelistIterator.ChangedIndex - OldChangedIndex == ArrayChangedCount);				// Make sure we read correct amount
			check(HandleIterator.ChangelistIterator.Changed[HandleIterator.ChangelistIterator.ChangedIndex] == 0);	// Make sure we are at the end

			HandleIterator.ChangelistIterator.ChangedIndex++;

			WritePropertyHandle(Writer, 0, bDoChecksum);		// Signify end of dynamic array
			continue;
		}
		// …（节选：省略共享序列化命中/未命中、NetSerializeItem 与 checksum 分支 90 行）
	}
}
```

`FRepLayout::ReplicateProperties` 的开头与结尾（第 1972~2020、2193~2207 行，节选）：

```cpp
bool FRepLayout::ReplicateProperties(
	FSendingRepState* RESTRICT RepState,
	FRepChangelistState* RESTRICT RepChangelistState,
	const FConstRepObjectDataBuffer Data,
	UClass* ObjectClass,
	UActorChannel* OwningChannel,
	FNetBitWriter& Writer,
	const FReplicationFlags& RepFlags) const
{
	CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_NetReplicateDynamicPropTime, GUseDetailedScopeCounters);

	check(ObjectClass == Owner);

	// If we are an empty RepLayout, there's nothing to do.
	if (IsEmpty())
	{
		return false;
	}

	FRepChangedPropertyTracker*	ChangeTracker = RepState->RepChangedPropertyTracker.Get();

	const bool bRecordingCheckpoint = (OwningChannel->Connection->ResendAllDataState != EResendAllDataState::None);

	TArray<uint16> NewlyActiveChangelist;

	// Rebuild conditional state if needed
	if (RepState->RepFlags.Value != RepFlags.Value)
	{
		RebuildConditionalProperties(RepState, RepFlags);

		// Filter out any previously inactive changes from still inactive ones
		TArray<uint16> InactiveChangelist = MoveTemp(RepState->InactiveChangelist);
		TArray<uint16> NewInactiveChangeList;

		FilterChangeList(InactiveChangelist, RepState->InactiveParents, NewInactiveChangeList, NewlyActiveChangelist);

		// If we're recording a checkpoint, restore the inactive changelist
		if (bRecordingCheckpoint)
		{
			RepState->InactiveChangelist = MoveTemp(InactiveChangelist);
		}
		else
		{
			RepState->InactiveChangelist = MoveTemp(NewInactiveChangeList);
		}
	}
	// …（节选：省略 ResendAllDataState::SinceOpen 的 LifetimeChangelist 重放分支 40 行）
	// …（节选：省略 changelist 历史合并与 pre-open ack 冲刷 130 行）

	// See if something actually sent (this may be false due to conditional checks inside the send properties function
	const bool bSomethingSent = NumBits != Writer.GetNumBits();

	if (!bSomethingSent)
	{
		// We need to revert the change list in the history if nothing really sent (can happen due to condition checks)
		Changed.Empty();
		RepState->HistoryEnd--;
	}

	return bSomethingSent;
}
```

1. **`ReplicateProperties` 的签名（第 1972~1978 行）与文章早先的想象完全不同**：它接收 `FSendingRepState*`、`FRepChangelistState*`、`FConstRepObjectDataBuffer Data`、`UClass* ObjectClass`、`UActorChannel* OwningChannel`、`FNetBitWriter& Writer`、`const FReplicationFlags& RepFlags`；返回 `bool` 表示「是否真的写出了数据」。整函数 236 行。
2. **`RebuildConditionalProperties` 的真实位置就在本函数内（第 1992 行）**：当 `RepState->RepFlags.Value != RepFlags.Value` 时重建条件状态。这是 `COND_*` 生效的执行点——`RepFlags` 变化（例如连接从「非拥有者」变为「拥有者」）会触发条件重算。
3. **`InactiveChangelist` / `InactiveParents` 是条件过滤的载体**：不活跃（条件不满足）的属性变更被暂存在 `RepState->InactiveChangelist`，一旦条件变为满足，`NewlyActiveChangelist` 会把这些属性「补发」出去。这正是 `COND_OwnerOnly` 属性在所有权交接瞬间能补上历史变更的机制。
4. **`SendProperties` 的 `Writer` 就是 `UActorChannel::ReplicateActor` 传进来的 `Bunch`**：属性数据直接写进 Actor 的 bunch，`FBitWriterMark Mark(Writer)` 记录起始位置；如果最终一个属性都没写出（`NumBits == Writer.GetNumBits()`），`Mark.Pop(Writer)` 回滚位偏移，避免留下空属性区段。写出内容时则在**末尾补一个 0 Handle 终结符**（`WritePropertyHandle(Writer, 0, bDoChecksum)`）。
5. **`bSomethingSent` 与实际 changelist 的一致性**：若判定「有变更」但条件检查导致一条都没发出，函数会 `Changed.Empty(); RepState->HistoryEnd--;` 回滚历史项——否则该变更会在后续帧被误认为「已经发过」而永久丢失。这是条件复制正确性的关键收尾。
6. **共享序列化（`FRepSerializationSharedInfo` / `GNetSharedSerializedData`）**：同一属性值在一帧内发给多个连接时，只序列化一次，其余连接用 `Writer.SerializeBitsWithOffset(...)` 引用已有位段（第 2902 行附近的 miss 路径则会走 `Cmd.Property->NetSerializeItem(Writer, Writer.PackageMap, ...)`）。这解释了「多连接场景下的序列化 CPU 优化」来源。
7. **`FRepChangedPropertyTracker` 的 5.8 归属**：`RepLayout.h` 第 123 行注释逐字为 `/** FRepChangedPropertyTracker moved to NetCore module */`；真实声明在 `Engine\Source\Runtime\Net\Core\Public\Net\Core\PropertyConditions\RepChangedPropertyTracker.h` 第 22 行（`class FRepChangedPropertyTracker`），提供 `IsParentActive(uint16 ParentIndex)`、`GetDynamicCondition(uint16 ParentIndex)`、`GetDynamicConditionChangeCounter()` 等查询，用于 `COND_*` 动态条件判定。发送侧通过 `FSendingRepState::RepChangedPropertyTracker`（`RepLayout.h` 第 632 行，`TSharedPtr<FRepChangedPropertyTracker>`）持有它。

### 4. 本节事实边界

- 上述全部为静态源码结论。`CompareProperties` 与 `CallRepNotifies` 的实际调用次数、每帧耗时**未**做运行态测量，本文不给出任何性能数字。
- `FRepLayout::ReplicateProperties` 中段（changelist 历史合并、`UpdateChangelistHistory`、pre-open ack 冲刷）与 `SendProperties_r` 的共享序列化分支共约 220 行未逐字收录，仅按源码结构给出摘要；如需完整逻辑请直接查阅 `Engine\Source\Runtime\Engine\Private\RepLayout.cpp` 第 2072~2192、2847~2936 行。
- `UE::Net::Metric::*`、`GNumSharedSerializationHit/Miss`、`GNumReplicateActorCalls` 等统计量的运行时数值取决于项目配置与负载，本文不提供任何具体数值。

---

## 核心源码深入剖析六：`UCharacterMovementComponent` 客户端预测与 `ServerMove`

> 本节为 2026-09-14 补深新增，属可选加分项。CMC 的移动复制不是「复制位置」，而是「客户端发意图、服务器重放并纠正」，正好是本文前述 RPC + 属性复制机制的实战组合。

### 1. `UCharacterMovementComponent::ReplicateMoveToServer`（节选）

摘自 `Engine\Source\Runtime\Engine\Private\Components\CharacterMovementComponent.cpp`（第 8907 行起，函数约 460 行，此处为开头节选）：

```cpp
void UCharacterMovementComponent::ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration)
{
	SCOPE_CYCLE_COUNTER(STAT_CharacterMovementReplicateMoveToServer);
	check(CharacterOwner != NULL);

	// Can only start sending moves if our controllers are synced up over the network, otherwise we flood the reliable buffer.
	APlayerController* PC = Cast<APlayerController>(CharacterOwner->GetController());
	if (PC && PC->AcknowledgedPawn != CharacterOwner)
	{
		return;
	}

	// Bail out if our character's controller doesn't have a Player. This may be the case when the local player
	// has switched to another controller, such as a debug camera controller.
	if (PC && PC->Player == nullptr)
	{
		return;
	}

	FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	if (!ClientData)
	{
		return;
	}

	// Update our delta time for physics simulation.
	DeltaTime = ClientData->UpdateTimeStampAndDeltaTime(DeltaTime, *CharacterOwner, *this);

	// Find the oldest (unacknowledged) important move (OldMove).
	// Don't include the last move because it may be combined with the next new move.
	// A saved move is interesting if it differs significantly from the last acknowledged move
	FSavedMovePtr OldMove = NULL;
	if( ClientData->LastAckedMove.IsValid() )
	{
		const int32 NumSavedMoves = ClientData->SavedMoves.Num();
		for (int32 i=0; i < NumSavedMoves-1; i++)
		{
			const FSavedMovePtr& CurrentMove = ClientData->SavedMoves[i];
			if (CurrentMove->IsImportantMove(ClientData->LastAckedMove))
			{
				OldMove = CurrentMove;
				break;
			}
		}
	}

	// Get a SavedMove object to store the movement in.
	FSavedMovePtr NewMovePtr = ClientData->CreateSavedMove();
	FSavedMove_Character* const NewMove = NewMovePtr.Get();
	if (NewMove == nullptr)
	{
		return;
	}

	NewMove->SetMoveFor(CharacterOwner, DeltaTime, NewAcceleration, *ClientData);
	const UWorld* MyWorld = GetWorld();

	// see if the two moves could be combined
	// do not combine moves which have different TimeStamps (before and after reset).
	if (const FSavedMove_Character* PendingMove = ClientData->PendingMove.Get())
	{
		if (PendingMove->CanCombineWith(NewMovePtr, CharacterOwner, ClientData->MaxMoveDeltaTime * CharacterOwner->GetActorTimeDilation(*MyWorld)))
		{
			SCOPE_CYCLE_COUNTER(STAT_CharacterMovementCombineNetMove);
```

1. **`PC->AcknowledgedPawn != CharacterOwner` 就返回**：源码注释写明原因是 `otherwise we flood the reliable buffer`——角色尚未被服务器确认拥有时狂发 `ServerMove` 会撑爆可靠缓冲（对应本文前述 `RPCReliableBufferOverflow` 断连路径）。
2. **`GetPredictionData_Client_Character()` 是客户端预测的中枢**：返回 `FNetworkPredictionData_Client_Character`（`CharacterMovementComponent.h` 第 2347 行声明、第 3152 行类定义），持有 `SavedMoves`（未被 ACK 的移动，最旧到最新）、`PendingMove`、`LastAckedMove`、`MaxSavedMoveCount`、`MaxMoveDeltaTime` 等。
3. **移动合并（`CanCombineWith`）是带宽优化的核心**：`PendingMove->CanCombineWith(NewMovePtr, ..., MaxMoveDeltaTime * TimeDilation)` 为真时，两次小移动被合并成一次发送。`FSavedMove_Character::CanCombineWith`（第 13085 行起，逐字节选）：

```cpp
bool FSavedMove_Character::CanCombineWith(const FSavedMovePtr& NewMovePtr, ACharacter* Character, float MaxDelta) const
{
	const FSavedMove_Character* NewMove = NewMovePtr.Get();

	if (bForceNoCombine || NewMove->bForceNoCombine)
	{
		return false;
	}

	if (bOldTimeStampBeforeReset)
	{
		return false;
	}

	// Cannot combine moves which contain root motion for now.
	// @fixme laurent - we should be able to combine most of them though, but current scheme of resetting pawn location and resimulating forward doesn't work.
	// as we don't want to tick montage twice (so we don't fire events twice). So we need to rearchitecture this so we tick only the second part of the move, and reuse the first part.
	if( (RootMotionMontage != NULL) || (NewMove->RootMotionMontage != NULL) )
	{
		return false;
	}

	if (NewMove->Acceleration.IsZero())
	{
		if (!Acceleration.IsZero())
		{
			return false;
		}
	}
	else
	{
		if (NewMove->DeltaTime + DeltaTime >= MaxDelta)
		{
			return false;
		}

		if (!FVector::Coincident(AccelNormal, NewMove->AccelNormal, AccelDotThresholdCombine))
		{
			return false;
		}
	}
	// …（节选：省略速度零值变化等后续判定 20 行）
```

   真实不可合并条件包括：`bForceNoCombine`、时间戳重置跨越（`bOldTimeStampBeforeReset`）、**任一方含 Root Motion**、加速度从零变非零或反之、累计 `DeltaTime` 超过 `MaxDelta`、`AccelNormal` 方向不一致（阈值 `AccelDotThresholdCombine`）。源码注释解释了 Root Motion 不可合并的原因：当前「重置位置并重新模拟」的方案会导致蒙太奇被 tick 两次从而重复触发事件。
4. **`SavedMoves` 不是无限增长**：由 `MaxSavedMoveCount` / `MaxFreeMoveCount` 限制；`FSavedMove_Character::SetMoveFor`（第 12761 行）负责把当帧输入快照进 move，`PrepMoveFor`（第 13300 行）用于重放时恢复状态。真正被压缩发送的是 `FVector_NetQuantize10` / `FVector_NetQuantize100` 等定点类型（见下）。

### 2. `ServerMove_Implementation` 真实签名与校验（节选）

摘自同一文件（第 10087 行起，逐字节选）：

```cpp
void UCharacterMovementComponent::ServerMove_Implementation(
	float TimeStamp,
	FVector_NetQuantize10 InAccel,
	FVector_NetQuantize100 ClientLoc,
	uint8 MoveFlags,
	uint8 ClientRoll,
	uint32 View,
	UPrimitiveComponent* ClientMovementBase,
	FName ClientBaseBoneName,
	uint8 ClientMovementMode)
{
	SCOPE_CYCLE_COUNTER(STAT_CharacterMovementServerMove);
	CSV_SCOPED_TIMING_STAT(CharacterMovement, CharacterMovementServerMove);

	if (!HasValidData() || !IsActive())
	{
		return;
	}

	FNetworkPredictionData_Server_Character* ServerData = GetPredictionData_Server_Character();
	check(ServerData);

	if( !VerifyClientTimeStamp(TimeStamp, *ServerData) )
	{
		const float ServerTimeStamp = ServerData->CurrentClientTimeStamp;
		// This is more severe if the timestamp has a large discrepancy and hasn't been recently reset.
		if (ServerTimeStamp > 1.0f && FMath::Abs(ServerTimeStamp - TimeStamp) > CharacterMovementCVars::NetServerMoveTimestampExpiredWarningThreshold)
		{
			UE_LOGF(LogNetPlayerMovement, Warning, "ServerMove: TimeStamp expired: %f, CurrentTimeStamp: %f, Character: %ls", TimeStamp, ServerTimeStamp, *GetNameSafe(CharacterOwner));
		}
		else
		{
			UE_LOGF(LogNetPlayerMovement, Log, "ServerMove: TimeStamp expired: %f, CurrentTimeStamp: %f, Character: %ls", TimeStamp, ServerTimeStamp, *GetNameSafe(CharacterOwner));
		}
		return;
	}

	bool bServerReadyForClient = true;
	APlayerController* PC = Cast<APlayerController>(CharacterOwner->GetController());
	if (PC)
	{
		bServerReadyForClient = PC->NotifyServerReceivedClientData(CharacterOwner, TimeStamp);
		if (!bServerReadyForClient)
		{
			InAccel = FVector::ZeroVector;
		}
	}

	// View components
	const uint16 ViewPitch = (View & 65535);
	const uint16 ViewYaw = (View >> 16);

	const FVector Accel = InAccel;

	const UWorld* MyWorld = GetWorld();
	const float DeltaTime = ServerData->GetServerMoveDeltaTime(TimeStamp, CharacterOwner->GetActorTimeDilation(*MyWorld));

	ServerData->CurrentClientTimeStamp = TimeStamp;
	ServerData->ServerAccumulatedClientTimeStamp += DeltaTime;
	ServerData->ServerTimeStamp = MyWorld->GetTimeSeconds();
```

1. **`ServerMove_Implementation` 的命名本身就是本文 RPC 章节的实证**：这是 `UFUNCTION(Server, Reliable)` 生成的 `_Implementation` 后缀（UHT 引擎名规则），`ServerMove_Validate` 若存在则由生成的 thunk 先行调用（见前述「RPC 校验」小节）。
2. **参数全部是量化/紧凑类型**：`FVector_NetQuantize10`（加速度）、`FVector_NetQuantize100`（位置）、`uint8 MoveFlags`、`uint8 ClientRoll`、`uint32 View`（高 16 位 pitch、低 16 位 yaw，代码 `ViewPitch = View & 65535; ViewYaw = View >> 16;`）。这是「移动复制省带宽」的直接证据，不是笼统的「浮点压缩」。
3. **时间戳校验是第一道门**：`VerifyClientTimeStamp(TimeStamp, *ServerData)` 失败即 return，并按偏差是否超过 `CharacterMovementCVars::NetServerMoveTimestampExpiredWarningThreshold`（且 `ServerTimeStamp > 1.0f`）选择 Warning 或 Log 级别。这解释了刷 `ServerMove: TimeStamp expired` 日志的真实阈值条件。
4. **客户端未就绪时把加速度清零**：`PC->NotifyServerReceivedClientData(...)` 返回 false 时 `InAccel = FVector::ZeroVector`，即**仍然处理这次移动但不采纳输入**——避免客户端在服务器尚未准备好时凭输入获得位移优势。
5. **`ServerData->CurrentClientTimeStamp = TimeStamp;`**：服务器接受这一次的时间戳作为基准，后续 `ServerMoveHandleClientError`（第 10182、10188 行，两个重载）据此判定是否发送 `ClientAdjustPosition` 纠正。

### 3. 本节事实边界

- 本节只覆盖 `ReplicateMoveToServer` 开头、`CanCombineWith` 开头与 `ServerMove_Implementation` 开头，**不是**这些函数的全文；`ServerMoveHandleClientError`、`ClientAdjustPosition`、`FSavedMove_Character::SetMoveFor/PrepMoveFor`、`ClientUpdatePosition` 的完整实现未逐字收录。
- CMC 还包含 `ServerMoveHandleClientError` 的两个重载（`UPrimitiveComponent*` 与 `FMovementBaseInterfaceData*` 版本，`CharacterMovementComponent.h` 第 2421、2429 行），本文未展开其差异。
- **未**做任何运行态验证：移动纠正频率、`MaxMoveDeltaTime` 的默认值、合并命中率等均取决于项目配置，本文不给出数值。

---

## 属性复制宏体系与条件过滤规范

在 `GetLifetimeReplicatedProps` 中通过宏控制带宽分发：

```cpp
void AMyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    // 1. 全局无条件同步（所有客户端可见）
    DOREPLIFETIME(AMyCharacter, Health);

    // 2. 仅拥有者可见（如背包栏、个人金币），不发给其他窥视者
    DOREPLIFETIME_CONDITION(AMyCharacter, InventoryItems, COND_OwnerOnly);

    // 3. 仅模拟代理可见（自主代理采用本地预测，不接收服务器回传）
    DOREPLIFETIME_CONDITION(AMyCharacter, SimulatedTransform, COND_SimulatedOnly);

    // 4. 仅初始生成同步一次（后续静态不变）
    DOREPLIFETIME_CONDITION(AMyCharacter, CharacterCustomSeed, COND_InitialOnly);
}
```

---

## 常见问题与排障 FAQ

**Q1：为什么在服务器端修改了属性，客户端没有收到更新？**
依次排查：① Actor 的 `bReplicates = true` 是否开启；② 属性是否正确编写 `UPROPERTY(Replicated)` 并在 `GetLifetimeReplicatedProps` 注册；③ 该 Actor 是否处于休眠状态（`NetDormancy`）；④ 客户端是否在该 Actor 的网络相关性裁剪范围之外（Relevancy 距离或所属 ReplicationGraph 节点被剔除）。

**Q2：Server RPC 频繁丢失的根本原因是什么？**
检查 RPC 是否标记为 `Unreliable`（不可靠）。不可靠 RPC 在弱网丢包时不会重发；若是 `Reliable` 仍未执行，检查客户端是否拥有该 Actor 的 NetOwner 权限（只有被当前 PlayerController 拥有的 Actor 才能发送 Server RPC）。

**Q3：频繁触发网络饱和卡顿（Saturated NetDriver）？**
通常是某一帧复制了庞大的 `TArray` 或巨型字符串。对于列表数据，坚决使用 `FFastArraySerializer` 代替普通 `TArray` 复制；对于高频变量，使用浮点量化压缩减少字节数。

---

## 关联阅读与前后置专题

- [06-网络同步/01-网络架构与复制基础](01-网络架构与复制基础.md)：C/S 架构与网络角色概念；
- [06-网络同步/02-RPC与属性同步](02-RPC与属性同步.md)：RPC 可靠性与条件复制使用层规范；
- [33-UNetDriver与连接通道源码](33-UNetDriver与连接通道源码.md)：底层 UDP 收包、通道管理与连接超时状态机；
- [20-Iris复制源码](20-Iris复制源码.md)：下一代数据驱动复制系统对传统 `FRepLayout` 的重构；
- [00-02 C++对象模型与内存](../../../00-计算机与工程基础/02-C%2B%2B对象模型与内存/README.md)：反射类型系统、属性内存对齐与偏移量底层机理。
