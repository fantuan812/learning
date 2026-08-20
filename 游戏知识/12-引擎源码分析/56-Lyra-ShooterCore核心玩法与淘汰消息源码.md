---
type: Mechanism
title: "Lyra 56：ShooterCore 核心玩法与淘汰消息源码"
status: stable
verified: []
maturity: L2
---
# Lyra 56：ShooterCore 核心玩法与淘汰消息源码

> 版本基准：UE 5.8.0；本机 CL 55116800，分支 ++UE5+Release-5.8。
> 源码依据：LyraStarterGame 5.8 的 Plugins/GameFeatures/ShooterCore 实际 C++、Tags 配置，以及 Source/LyraGame AbilitySystem 的命中上下文代码。
> 适用范围：团队竞技出生点、辅助瞄准生产侧、伤害/淘汰消息、助攻/连杀/连胜处理器、Accolade 数据注册和异步展示。
> 兼容性边界：ShooterCore 是 Lyra 样例 GameFeature，不是 UE 默认玩法；蓝图武器、Experience、DataRegistry 行和 UAsset 资产仍需单独验证。
> 官方参考：https://dev.epicgames.com/documentation/en-us/unreal-engine
> 最后更新：2026-08-18（新增 ShooterCore 核心玩法插件和 GAS 命中上下文的真实源码证据）。
> 知识成熟度：L2（本机源码静态核对；完整射击资产和联机比赛流程仍需运行验证）。

## 一、覆盖目标

ShooterCore 虽然是样例插件，但承载了 Lyra 最典型的核心玩法：

- 按敌方距离选择团队竞技出生点。
- 在辅助瞄准查询里排除同队、死亡和排除标签目标。
- 用 GameplayMessageSubsystem 把伤害/淘汰变成可消费的 VerbMessage。
- 从同一条淘汰消息派生助攻、连杀链、连胜计数和 Accolade 播报。

这些是可复用的玩法扩展范式，不再只作为路径清单或“示例范围外”。

## 二、核心玩法数据流

TeamSubsystem + LyraPlayerStart → TDM_PlayerSpawningManagmentComponent → 选择远离敌人的出生点。

DamageExecution/HealthSet → Lyra.Damage.Message → AssistProcessor。

Lyra.Elimination.Message → ElimChainProcessor/ElimStreakProcessor → NotificationMessage → LyraAccoladeHostWidget。

## 三、团队竞技出生点

UTDM_PlayerSpawningManagmentComponent::OnChoosePlayerStart 先从 World TeamSubsystem 查 Player 的 TeamId；TeamId 尚未产生时返回空，把早期登录阶段交回通用选择器。随后遍历 GameState PlayerArray，只比较敌方队伍 Pawn 与候选 PlayerStart 的距离。


### TDM 远离敌方的出生点选择

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/TDM_PlayerSpawningManagmentComponent.cpp

```cpp
AActor* UTDM_PlayerSpawningManagmentComponent::OnChoosePlayerStart(AController* Player, TArray<ALyraPlayerStart*>& PlayerStarts)
{
	ULyraTeamSubsystem* TeamSubsystem = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
	if (!ensure(TeamSubsystem))
	{
		return nullptr;
	}

	const int32 PlayerTeamId = TeamSubsystem->FindTeamFromObject(Player);

	// We should have a TeamId by now, but early login stuff before post login can try to do stuff, ignore it.
	if (!ensure(PlayerTeamId != INDEX_NONE))
	{
		return nullptr;
	}

	ALyraGameState* GameState = GetGameStateChecked<ALyraGameState>();

	ALyraPlayerStart* BestPlayerStart = nullptr;
	double MaxDistance = 0;
	ALyraPlayerStart* FallbackPlayerStart = nullptr;
	double FallbackMaxDistance = 0;

	for (APlayerState* PS : GameState->PlayerArray)
	{
		const int32 TeamId = TeamSubsystem->FindTeamFromObject(PS);

		// We should have a TeamId by now...
		if (PS->IsOnlyASpectator() || !ensure(TeamId != INDEX_NONE))
		{
			continue;
		}

		// If the other player isn't on the same team, lets find the furthest spawn from them.
		if (TeamId != PlayerTeamId)
		{
			for (ALyraPlayerStart* PlayerStart : PlayerStarts)
			{
				if (APawn* Pawn = PS->GetPawn())
				{
					const double Distance = PlayerStart->GetDistanceTo(Pawn);

					if (PlayerStart->IsClaimed())
					{
						if (FallbackPlayerStart == nullptr || Distance > FallbackMaxDistance)
						{
							FallbackPlayerStart = PlayerStart;
							FallbackMaxDistance = Distance;
						}
					}
					else if (PlayerStart->GetLocationOccupancy(Player) < ELyraPlayerStartLocationOccupancy::Full)
					{
						if (BestPlayerStart == nullptr || Distance > MaxDistance)
						{
							BestPlayerStart = PlayerStart;
							MaxDistance = Distance;
						}
					}
				}
			}
		}
	}

	if (BestPlayerStart)
	{
		return BestPlayerStart;
	}

	return FallbackPlayerStart;
}
```


### AssistProcessor 伤害累积与助攻

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/MessageProcessors/AssistProcessor.cpp

```cpp
void UAssistProcessor::StartListening()
{
	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
	AddListenerHandle(MessageSubsystem.RegisterListener(TAG_Lyra_Elimination_Message, this, &ThisClass::OnEliminationMessage));
	AddListenerHandle(MessageSubsystem.RegisterListener(TAG_Lyra_Damage_Message, this, &ThisClass::OnDamageMessage));
}

void UAssistProcessor::OnDamageMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload)
{
	if (Payload.Instigator != Payload.Target)
	{
		if (APlayerState* InstigatorPS = ULyraVerbMessageHelpers::GetPlayerStateFromObject(Payload.Instigator))
		{
			if (APlayerState* TargetPS = ULyraVerbMessageHelpers::GetPlayerStateFromObject(Payload.Target))
			{
				FPlayerAssistDamageTracking& Damage = DamageHistory.FindOrAdd(TargetPS);
				float& DamageTotalFromTarget = Damage.AccumulatedDamageByPlayer.FindOrAdd(InstigatorPS);
				DamageTotalFromTarget += Payload.Magnitude;
			}
		}
	}
}


void UAssistProcessor::OnEliminationMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload)
{
	if (APlayerState* TargetPS = Cast<APlayerState>(Payload.Target))
	{
		// Grant an assist to each player who damaged the target but wasn't the instigator
		if (FPlayerAssistDamageTracking* DamageOnTarget = DamageHistory.Find(TargetPS))
		{
			for (const auto& KVP : DamageOnTarget->AccumulatedDamageByPlayer)
			{
				if (APlayerState* AssistPS = KVP.Key)
				{
					if (AssistPS != Payload.Instigator)
					{
						FLyraVerbMessage AssistMessage;
						AssistMessage.Verb = TAG_Lyra_Assist_Message;
						AssistMessage.Instigator = AssistPS;
						//@TODO: Get default tags from a player state or save off most recent tags during assist damage?
						//AssistMessage.InstigatorTags = ;
						AssistMessage.Target = TargetPS;
						AssistMessage.TargetTags = Payload.TargetTags;
						AssistMessage.ContextTags = Payload.ContextTags;
						AssistMessage.Magnitude = KVP.Value;

						UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
						MessageSubsystem.BroadcastMessage(AssistMessage.Verb, AssistMessage);
					}
				}
			}

			// Clear the damage log for the eliminated player
			DamageHistory.Remove(TargetPS);
		}
	}
}
```


### ElimChainProcessor

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/MessageProcessors/ElimChainProcessor.cpp

```cpp
void UElimChainProcessor::StartListening()
{
	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
	AddListenerHandle(MessageSubsystem.RegisterListener(ElimChain::TAG_Lyra_Elimination_Message, this, &ThisClass::OnEliminationMessage));
}

void UElimChainProcessor::OnEliminationMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload)
{
	// Track elimination chains for the attacker (except for self-eliminations)
	if (Payload.Instigator != Payload.Target)
	{
		if (APlayerState* InstigatorPS = Cast<APlayerState>(Payload.Instigator))
		{
			const double CurrentTime = GetServerTime();

			FPlayerElimChainInfo& History = PlayerChainHistory.FindOrAdd(InstigatorPS);
			const bool bStreakReset = (History.LastEliminationTime == 0.0) || (History.LastEliminationTime + ChainTimeLimit < CurrentTime);

			History.LastEliminationTime = CurrentTime;
			if (bStreakReset)
			{
				History.ChainCounter = 1;
			}
			else
			{
				++History.ChainCounter;

				if (FGameplayTag* pTag = ElimChainTags.Find(History.ChainCounter))
				{
					FLyraVerbMessage ElimChainMessage;
					ElimChainMessage.Verb = *pTag;
					ElimChainMessage.Instigator = InstigatorPS;
					ElimChainMessage.InstigatorTags = Payload.InstigatorTags;
					ElimChainMessage.ContextTags = Payload.ContextTags;
					ElimChainMessage.Magnitude = History.ChainCounter;

					UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
					MessageSubsystem.BroadcastMessage(ElimChainMessage.Verb, ElimChainMessage);
				}
			}
		}
	}
}
```


### ElimStreakProcessor

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/MessageProcessors/ElimStreakProcessor.cpp

```cpp
void UElimStreakProcessor::StartListening()
{
	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
	AddListenerHandle(MessageSubsystem.RegisterListener(ElimStreak::TAG_Lyra_Elimination_Message, this, &ThisClass::OnEliminationMessage));
}

void UElimStreakProcessor::OnEliminationMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload)
{
	// Track elimination streaks for the attacker (except for self-eliminations)
	if (Payload.Instigator != Payload.Target)
	{
		if (APlayerState* InstigatorPS = Cast<APlayerState>(Payload.Instigator))
		{
			int32& StreakCount = PlayerStreakHistory.FindOrAdd(InstigatorPS);
			StreakCount++;

			if (FGameplayTag* pTag = ElimStreakTags.Find(StreakCount))
			{
				FLyraVerbMessage ElimStreakMessage;
				ElimStreakMessage.Verb = *pTag;
				ElimStreakMessage.Instigator = InstigatorPS;
				ElimStreakMessage.InstigatorTags = Payload.InstigatorTags;
				ElimStreakMessage.ContextTags = Payload.ContextTags;
				ElimStreakMessage.Magnitude = StreakCount;

				UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
				MessageSubsystem.BroadcastMessage(ElimStreakMessage.Verb, ElimStreakMessage);
			}
		}
	}

	// End the elimination streak for the target
	if (APlayerState* TargetPS = Cast<APlayerState>(Payload.Target))
	{
		PlayerStreakHistory.Remove(TargetPS);
	}
}
```


### Accolade 通知、Registry 与软加载

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Accolades/LyraAccoladeHostWidget.cpp

```cpp
void ULyraAccoladeHostWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
	ListenerHandle = MessageSubsystem.RegisterListener(TAG_Lyra_AddNotification_Message, this, &ThisClass::OnNotificationMessage);
}

void ULyraAccoladeHostWidget::NativeDestruct()
{
	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
	MessageSubsystem.UnregisterListener(ListenerHandle);

	CancelAsyncLoading();

	Super::NativeDestruct();
}

void ULyraAccoladeHostWidget::OnNotificationMessage(FGameplayTag Channel, const FLyraNotificationMessage& Notification)
{
	if (Notification.TargetChannel == TAG_Lyra_ShooterGame_Accolade)
	{
		// Ignore notifications for other players
		if (Notification.TargetPlayer != nullptr)
		{
			APlayerController* PC = GetOwningPlayer();
			if ((PC == nullptr) || (PC->PlayerState != Notification.TargetPlayer))
			{
				return;
			}
		}

		// Load the data registry row for this accolade
		const int32 NextID = AllocatedSequenceID;
		++AllocatedSequenceID;

		FDataRegistryId ItemID(NAME_AccoladeRegistryID, Notification.PayloadTag.GetTagName());
		if (!UDataRegistrySubsystem::Get()->AcquireItem(ItemID, FDataRegistryItemAcquiredCallback::CreateUObject(this, &ThisClass::OnRegistryLoadCompleted, NextID)))
		{
			UE_LOG(LogLyra, Error, TEXT("Failed to find accolade registry for tag %s, accolades will not appear"), *Notification.PayloadTag.GetTagName().ToString());
			--AllocatedSequenceID;
		}
	}
}

void ULyraAccoladeHostWidget::OnRegistryLoadCompleted(const FDataRegistryAcquireResult& AccoladeHandle, int32 SequenceID)
{
	if (const FLyraAccoladeDefinitionRow* AccoladeRow = AccoladeHandle.GetItem<FLyraAccoladeDefinitionRow>())
	{
		FPendingAccoladeEntry& PendingEntry = PendingAccoladeLoads.AddDefaulted_GetRef();
		PendingEntry.Row = *AccoladeRow;
		PendingEntry.SequenceID = SequenceID;

		TArray<FSoftObjectPath> AssetsToLoad;
		AssetsToLoad.Add(AccoladeRow->Sound.ToSoftObjectPath());
		AssetsToLoad.Add(AccoladeRow->Icon.ToSoftObjectPath());
		AsyncLoad(AssetsToLoad, [this, SequenceID]
		{
			FPendingAccoladeEntry* EntryThatFinishedLoading = PendingAccoladeLoads.FindByPredicate([SequenceID](const FPendingAccoladeEntry& Entry) { return Entry.SequenceID == SequenceID; });
			if (ensure(EntryThatFinishedLoading))
			{
				EntryThatFinishedLoading->Sound = EntryThatFinishedLoading->Row.Sound.Get();
				EntryThatFinishedLoading->Icon = EntryThatFinishedLoading->Row.Icon.Get();
				EntryThatFinishedLoading->bFinishedLoading = true;
				ConsiderLoadedAccolades();
			}
		});
		StartAsyncLoading();
	}
	else
	{
		ensure(false);
	}
}

void ULyraAccoladeHostWidget::ConsiderLoadedAccolades()
{
	int32 PendingIndexToDisplay;
	do
	{
		PendingIndexToDisplay = PendingAccoladeLoads.IndexOfByPredicate([DesiredID=NextDisplaySequenceID](const FPendingAccoladeEntry& Entry) { return Entry.bFinishedLoading && Entry.SequenceID == DesiredID; });
		if (PendingIndexToDisplay != INDEX_NONE)
		{
			FPendingAccoladeEntry Entry = MoveTemp(PendingAccoladeLoads[PendingIndexToDisplay]);
			PendingAccoladeLoads.RemoveAtSwap(PendingIndexToDisplay);

			ProcessLoadedAccolade(Entry);
			++NextDisplaySequenceID;
		}
	} while (PendingIndexToDisplay != INDEX_NONE);
}

void ULyraAccoladeHostWidget::ProcessLoadedAccolade(const FPendingAccoladeEntry& Entry)
{
	if (Entry.Row.LocationTag == LocationName)
	{
		bool bRecreateWidget = PendingAccoladeDisplays.Num() == 0;
		for (int32 Index = 0; Index < PendingAccoladeDisplays.Num(); )
		{
			if (PendingAccoladeDisplays[Index].Row.AccoladeTags.HasAny(Entry.Row.CancelAccoladesWithTag))
			{
				if (UUserWidget* OldWidget = PendingAccoladeDisplays[Index].AllocatedWidget)
				{
					DestroyAccoladeWidget(OldWidget);
					bRecreateWidget = true;
				}
				PendingAccoladeDisplays.RemoveAt(Index);
			}
			else
			{
				++Index;
			}
		}

		PendingAccoladeDisplays.Add(Entry);

		if (bRecreateWidget)
		{
			DisplayNextAccolade();
		}
	}
}

void ULyraAccoladeHostWidget::DisplayNextAccolade()
{
	if (PendingAccoladeDisplays.Num() > 0)
	{
		FPendingAccoladeEntry& Entry = PendingAccoladeDisplays[0];

		GetWorld()->GetTimerManager().SetTimer(NextTimeToReconsiderHandle, this, &ThisClass::PopDisplayedAccolade, Entry.Row.DisplayDuration);
		Entry.AllocatedWidget = CreateAccoladeWidget(Entry);
	}
}

void ULyraAccoladeHostWidget::PopDisplayedAccolade()
{
	if (PendingAccoladeDisplays.Num() > 0)
	{
		if (UUserWidget* OldWidget = PendingAccoladeDisplays[0].AllocatedWidget)
		{
			DestroyAccoladeWidget(OldWidget);
		}
		PendingAccoladeDisplays.RemoveAt(0);
	}

	DisplayNextAccolade();
}
```


### Lyra EffectContext

来源：Source/LyraGame/AbilitySystem/LyraGameplayEffectContext.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraGameplayEffectContext.h"

#include "AbilitySystem/LyraAbilitySourceInterface.h"
#include "Engine/HitResult.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Serialization/GameplayEffectContextNetSerializer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraGameplayEffectContext)

class FArchive;

FLyraGameplayEffectContext* FLyraGameplayEffectContext::ExtractEffectContext(struct FGameplayEffectContextHandle Handle)
{
	FGameplayEffectContext* BaseEffectContext = Handle.Get();
	if ((BaseEffectContext != nullptr) && BaseEffectContext->GetScriptStruct()->IsChildOf(FLyraGameplayEffectContext::StaticStruct()))
	{
		return (FLyraGameplayEffectContext*)BaseEffectContext;
	}

	return nullptr;
}

bool FLyraGameplayEffectContext::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	FGameplayEffectContext::NetSerialize(Ar, Map, bOutSuccess);

	// Not serialized for post-activation use:
	// CartridgeID

	return true;
}

namespace UE::Net
{
	// Forward to FGameplayEffectContextNetSerializer
	// Note: If FLyraGameplayEffectContext::NetSerialize() is modified, a custom NetSerializer must be implemented as the current fallback will no longer be sufficient.
	UE_NET_IMPLEMENT_FORWARDING_NETSERIALIZER_AND_REGISTRY_DELEGATES(LyraGameplayEffectContext, FGameplayEffectContextNetSerializer);
}

void FLyraGameplayEffectContext::SetAbilitySource(const ILyraAbilitySourceInterface* InObject, float InSourceLevel)
{
	AbilitySourceObject = MakeWeakObjectPtr(Cast<const UObject>(InObject));
	//SourceLevel = InSourceLevel;
}

const ILyraAbilitySourceInterface* FLyraGameplayEffectContext::GetAbilitySource() const
{
	return Cast<ILyraAbilitySourceInterface>(AbilitySourceObject.Get());
}

const UPhysicalMaterial* FLyraGameplayEffectContext::GetPhysicalMaterial() const
{
	if (const FHitResult* HitResultPtr = GetHitResult())
	{
		return HitResultPtr->PhysMaterial.Get();
	}
	return nullptr;
}

```


### Lyra TargetData

来源：Source/LyraGame/AbilitySystem/LyraGameplayAbilityTargetData_SingleTargetHit.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraGameplayAbilityTargetData_SingleTargetHit.h"

#include "LyraGameplayEffectContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraGameplayAbilityTargetData_SingleTargetHit)

struct FGameplayEffectContextHandle;

//////////////////////////////////////////////////////////////////////

void FLyraGameplayAbilityTargetData_SingleTargetHit::AddTargetDataToContext(FGameplayEffectContextHandle& Context, bool bIncludeActorArray) const
{
	FGameplayAbilityTargetData_SingleTargetHit::AddTargetDataToContext(Context, bIncludeActorArray);

	// Add game-specific data
	if (FLyraGameplayEffectContext* TypedContext = FLyraGameplayEffectContext::ExtractEffectContext(Context))
	{
		TypedContext->CartridgeID = CartridgeID;
	}
}

bool FLyraGameplayAbilityTargetData_SingleTargetHit::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	FGameplayAbilityTargetData_SingleTargetHit::NetSerialize(Ar, Map, bOutSuccess);

	Ar << CartridgeID;

	return true;
}

```


### Lyra AbilitySource 接口

来源：Source/LyraGame/AbilitySystem/LyraAbilitySourceInterface.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "LyraAbilitySourceInterface.generated.h"

class UObject;
class UPhysicalMaterial;
struct FGameplayTagContainer;

/** Base interface for anything acting as a ability calculation source */
UINTERFACE()
class ULyraAbilitySourceInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class ILyraAbilitySourceInterface
{
	GENERATED_IINTERFACE_BODY()

	/**
	 * Compute the multiplier for effect falloff with distance
	 *
	 * @param Distance			Distance from source to target for ability calculations (distance bullet traveled for a gun, etc...)
	 * @param SourceTags		Aggregated Tags from the source
	 * @param TargetTags		Aggregated Tags currently on the target
	 *
	 * @return Multiplier to apply to the base attribute value due to distance
	 */
	virtual float GetDistanceAttenuation(float Distance, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr) const = 0;

	virtual float GetPhysicalMaterialAttenuation(const UPhysicalMaterial* PhysicalMaterial, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr) const = 0;
};
```


未 Claim 且未 Full 的点竞争 BestPlayerStart，以距离最大的点为胜；已 Claim 的点只进入 Fallback。没有 Best 时返回 Fallback。53 篇的通用组件负责 PIE、缓存、Empty/Partial/Full 和 Claim，ShooterCore 只覆盖 OnChoosePlayerStart 的队伍规则。

## 四、伤害与淘汰消息

AssistProcessor::StartListening 注册 Lyra.Elimination.Message 和 Lyra.Damage.Message。收到非自伤 Damage 时，以目标 PlayerState 为外层键、Instigator PlayerState 为内层键累加 Magnitude；收到淘汰时，给所有非最终击杀者广播 AssistMessage，最后清理目标 damage history。



ElimChainProcessor 维护 LastEliminationTime 与 ChainCounter，超过 ChainTimeLimit 重新从 1 开始，只有命中 ElimChainTags 的计数才广播专属 Tag。ElimStreakProcessor 不使用时间窗口，只递增攻击者计数，并在目标被淘汰时删除目标计数。



消息处理器只消费已经生成的 VerbMessage。真正的伤害仍在 GAS Execution/AttributeSet，客户端不能通过伪造 AssistMessage 或 ElimStreakMessage 获得服务器奖励。

## 五、Accolade 的异步数据注册和顺序显示

ULyraAccoladeHostWidget::NativeConstruct 注册通知频道；收到消息后先过滤 TargetChannel=Lyra.ShooterGame.Accolade，再检查 TargetPlayer 是否匹配拥有该 Widget 的 PlayerController。其他玩家的通知不会进入本地播报队列。

OnNotificationMessage 把 PayloadTag 转成 Accolades DataRegistryId，AcquireItem 异步取得 FLyraAccoladeDefinitionRow；回调把 Sound/Icon 软引用加入 AsyncLoad，资源完成后按 SequenceID 进入 PendingAccoladeLoads。



ConsiderLoadedAccolades 只处理 NextDisplaySequenceID，防止后到资源越过先到通知。ProcessLoadedAccolade 按 CancelAccoladesWithTag 移除冲突播报，DisplayNextAccolade 用 TimerManager 控制持续时间。

## 六、辅助瞄准和消息结果的边界

55 篇负责目标查询、FOV 和输入 Pull/Slow；56 负责 ShooterCore 的玩法目标和消息结果。辅助瞄准不改变服务器 DamageExecution，也不绕过 TeamSubsystem 的伤害过滤。

## 七、GAS 命中上下文

LyraAbilitySystemGlobals、LyraGameplayEffectContext 和 LyraGameplayAbilityTargetData_SingleTargetHit 把 Instigator、SourceObject、HitResult、PhysicalMaterial 等上下文带入 Execution。本篇收录这些未被前文全文覆盖的边界文件，闭合武器命中、TargetData、DamageExecution 和 ShooterCore 消息之间的关系。



## 八、验证入口

验证项目文件：ShooterCore Config/Tags/ShooterCoreTags.ini；ShooterCoreRuntime 的 TDM_PlayerSpawningManagmentComponent、AimAssist、MessageProcessors、Accolades 文件；LyraGame AbilitySystem 的 Globals、EffectContext、TargetData、AbilitySource 文件。

建议命令：

    $Lyra = 'C:\Users\zhaozhiqi\Documents\Unreal Projects\LyraStarterGame'
    Test-Path "$Lyra\Plugins\GameFeatures\ShooterCore\Source\ShooterCoreRuntime\Private\TDM_PlayerSpawningManagmentComponent.cpp"
    rg -n 'OnChoosePlayerStart|FindTeamFromObject|MaxDistance|FallbackPlayerStart' "$Lyra\Plugins\GameFeatures\ShooterCore\Source\ShooterCoreRuntime"
    rg -n 'Lyra\.Damage\.Message|Lyra\.Elimination\.Message|RegisterListener|BroadcastMessage' "$Lyra\Plugins\GameFeatures\ShooterCore\Source\ShooterCoreRuntime"
    rg -n 'AcquireItem|AsyncLoad|SequenceID|CancelAccoladesWithTag' "$Lyra\Plugins\GameFeatures\ShooterCore\Source\ShooterCoreRuntime"
    rg -n 'NetSerialize|HitResult|PhysicalMaterial|TargetData' "$Lyra\Source\LyraGame\AbilitySystem"

## 九、失败路径和安全边界

| 失败点 | 源码行为 | 结论 |
| --- | --- | --- |
| Player 尚未有 TeamId | TDM 选择器返回空 | 早期登录不能假定团队已就绪 |
| 没有敌方 Pawn | Best 为空，返回 Fallback | “最远”不等于有效敌方距离 |
| Instigator 等于 Target | Assist/Chain/Streak 跳过统计 | 自伤不会生成助攻/连杀 |
| 找不到 Registry 行 | 记录错误并回收 SequenceID | 消息不阻塞整个 UI |
| 软资源加载失败 | Pending 条目不显示 | 必须做资产校验 |
| 伪造客户端消息 | 消息只驱动表现 | 奖励必须由服务器生成 |

## 十、关联阅读

- 42-Lyra-输入GAS与武器战斗源码：武器命中、DamageExecution、HealthSet。
- 43-Lyra-背包装备消息与UI源码：消息路由和 AssistProcessor 注册。
- 46-Lyra-AI机器人与队伍源码：TeamSubsystem 和机器人队伍。
- 51-Lyra-GAS扩展与能力费用源码：AbilityCost、AttributeSet、GameplayCue。
- 53-Lyra核心生成移动与状态源码：通用生成点、Pawn/Controller 状态。
- 55-Lyra输入重映射与辅助瞄准源码：AimAssist 输入侧查询和修正。

## 十一、覆盖声明

本文覆盖 ShooterCoreRuntime 的核心玩法文件和 Lyra GAS 命中上下文文件；ShooterMaps、TopDownArena 的具体玩法规则和蓝图资产仍标为示例/待补，不因本篇完成而自动计入。


## 附录：核心文件完整源码

以下文件来自本机 LyraStarterGame 5.8，逐文件收录；代码围栏内只统一行尾和缩进空白，未删改源码内容、注释或条件编译。

### 附录文件 1：Plugins/GameFeatures/ShooterCore/Config/Tags/ShooterCoreTags.ini

来源：Plugins/GameFeatures/ShooterCore/Config/Tags/ShooterCoreTags.ini

```ini
[/Script/GameplayTags.GameplayTagsList]
GameplayTagList=(Tag="Ability.ActivateFail.MagazineFull",DevComment="")
GameplayTagList=(Tag="Ability.ActivateFail.NoSpareAmmo",DevComment="")
GameplayTagList=(Tag="Event.Movement.ADS",DevComment="")
GameplayTagList=(Tag="Event.Movement.Dash",DevComment="")
GameplayTagList=(Tag="Event.Movement.Melee",DevComment="")
GameplayTagList=(Tag="Event.Movement.Reload",DevComment="")
GameplayTagList=(Tag="Event.Movement.WeaponFire",DevComment="")
GameplayTagList=(Tag="Gameplay.Message.ADS",DevComment="Message to UI, Reticle")
GameplayTagList=(Tag="Gameplay.Message.Nameplate.Add",DevComment="Register Nameplate Source")
GameplayTagList=(Tag="Gameplay.Message.Nameplate.Discover",DevComment="Looking for nameplates")
GameplayTagList=(Tag="Gameplay.Message.Nameplate.Remove",DevComment="Unregister Nameplate Source")
GameplayTagList=(Tag="GameplayCue.Character.Spawn",DevComment="At spawning of the player in shooter game")
GameplayTagList=(Tag="GameplayCue.ShooterGame.Interact.Collect",DevComment="")
GameplayTagList=(Tag="GameplayCue.ShooterGame.Interact.WeaponPickup",DevComment="GCN for weapon pick FX attached to pawn")
GameplayTagList=(Tag="GameplayCue.ShooterGame.UserMessage.MatchDecided",DevComment="")
GameplayTagList=(Tag="GameplayCue.ShooterGame.UserMessage.WaitingForPlayers",DevComment="")
GameplayTagList=(Tag="GameplayEvent.ReloadDone",DevComment="")
GameplayTagList=(Tag="HUD.Slot.EliminationFeed",DevComment="")
GameplayTagList=(Tag="HUD.Slot.Equipment",DevComment="")
GameplayTagList=(Tag="HUD.Slot.ModeStatus",DevComment="")
GameplayTagList=(Tag="HUD.Slot.PerfStats.Graph",DevComment="")
GameplayTagList=(Tag="HUD.Slot.PerfStats.Text",DevComment="")
GameplayTagList=(Tag="HUD.Slot.Reticle",DevComment="")
GameplayTagList=(Tag="HUD.Slot.TeamScore",DevComment="")
GameplayTagList=(Tag="HUD.Slot.TopAccolades",DevComment="")
GameplayTagList=(Tag="InputTag.Ability.Emote",DevComment="")
GameplayTagList=(Tag="InputTag.Ability.Interact",DevComment="")
GameplayTagList=(Tag="InputTag.Ability.Quickslot.CycleBackward",DevComment="")
GameplayTagList=(Tag="InputTag.Ability.Quickslot.CycleForward",DevComment="")
GameplayTagList=(Tag="InputTag.Ability.Quickslot.SelectSlot",DevComment="Used to directly select one of the quickbar slots. Intended to be accompanied with a 0-based slot index.")
GameplayTagList=(Tag="InputTag.Ability.ShowLeaderboard",DevComment="")
GameplayTagList=(Tag="InputTag.Ability.ToggleInventory",DevComment="")
GameplayTagList=(Tag="InputTag.Ability.ToggleMap",DevComment="")
GameplayTagList=(Tag="InputTag.Ability.ToggleMarkerInWorld",DevComment="")
GameplayTagList=(Tag="Lyra.AddNotification.KillFeed",DevComment="SendKillFeedInfo to UI")
GameplayTagList=(Tag="Lyra.ShooterGame.Accolade.EliminationChain",DevComment="")
GameplayTagList=(Tag="Lyra.ShooterGame.Accolade.EliminationChain.2x",DevComment="")
GameplayTagList=(Tag="Lyra.ShooterGame.Accolade.EliminationChain.3x",DevComment="")
GameplayTagList=(Tag="Lyra.ShooterGame.Accolade.EliminationChain.4x",DevComment="")
GameplayTagList=(Tag="Lyra.ShooterGame.Accolade.EliminationChain.5x",DevComment="")
GameplayTagList=(Tag="Lyra.ShooterGame.Accolade.EliminationStreak",DevComment="")
GameplayTagList=(Tag="Lyra.ShooterGame.Accolade.EliminationStreak.5",DevComment="")
GameplayTagList=(Tag="Lyra.ShooterGame.Accolade.EliminationStreak.10",DevComment="")
GameplayTagList=(Tag="Lyra.ShooterGame.Accolade.EliminationStreak.15",DevComment="")
GameplayTagList=(Tag="Lyra.ShooterGame.Accolade.EliminationStreak.20",DevComment="")
GameplayTagList=(Tag="Lyra.ShooterGame.TDM.TeamScore",DevComment="")
GameplayTagList=(Tag="Lyra.ShooterGame.Weapon.MagazineAmmo",DevComment="")
GameplayTagList=(Tag="Lyra.ShooterGame.Weapon.MagazineSize",DevComment="")
GameplayTagList=(Tag="Lyra.ShooterGame.Weapon.SpareAmmo",DevComment="")
GameplayTagList=(Tag="ShooterGame.ControlPoint.Captured.Message",DevComment="Fired when a control point has been captured by a team")
GameplayTagList=(Tag="ShooterGame.ControlPoint.TeamScore",DevComment="")
GameplayTagList=(Tag="ShooterGame.ExtensionPoint.AbilityBar",DevComment="")
GameplayTagList=(Tag="ShooterGame.GamePhase.Playing",DevComment="")
GameplayTagList=(Tag="ShooterGame.GamePhase.PostGame",DevComment="")
GameplayTagList=(Tag="ShooterGame.GamePhase.Warmup",DevComment="")
GameplayTagList=(Tag="ShooterGame.Score.Assists",DevComment="")
GameplayTagList=(Tag="ShooterGame.Score.ControlPointCapture",DevComment="")
GameplayTagList=(Tag="ShooterGame.Score.Deaths",DevComment="")
GameplayTagList=(Tag="ShooterGame.Score.Eliminations",DevComment="")
GameplayTagList=(Tag="TODO.GameModeDamageImmunity",DevComment="")


```

### 附录文件 2：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/TDM_PlayerSpawningManagmentComponent.h

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/TDM_PlayerSpawningManagmentComponent.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Player/LyraPlayerSpawningManagerComponent.h"

#include "TDM_PlayerSpawningManagmentComponent.generated.h"

class AActor;
class AController;
class ALyraPlayerStart;
class UObject;

/**
 *
 */
UCLASS()
class UTDM_PlayerSpawningManagmentComponent : public ULyraPlayerSpawningManagerComponent
{
	GENERATED_BODY()

public:

	UTDM_PlayerSpawningManagmentComponent(const FObjectInitializer& ObjectInitializer);

	virtual AActor* OnChoosePlayerStart(AController* Player, TArray<ALyraPlayerStart*>& PlayerStarts) override;
	virtual void OnFinishRestartPlayer(AController* Player, const FRotator& StartRotation) override;

protected:

};

```

### 附录文件 3：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/TDM_PlayerSpawningManagmentComponent.cpp

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/TDM_PlayerSpawningManagmentComponent.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "TDM_PlayerSpawningManagmentComponent.h"

#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameModes/LyraGameState.h"
#include "Player/LyraPlayerStart.h"
#include "Teams/LyraTeamSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TDM_PlayerSpawningManagmentComponent)

class AActor;

UTDM_PlayerSpawningManagmentComponent::UTDM_PlayerSpawningManagmentComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

AActor* UTDM_PlayerSpawningManagmentComponent::OnChoosePlayerStart(AController* Player, TArray<ALyraPlayerStart*>& PlayerStarts)
{
	ULyraTeamSubsystem* TeamSubsystem = GetWorld()->GetSubsystem<ULyraTeamSubsystem>();
	if (!ensure(TeamSubsystem))
	{
		return nullptr;
	}

	const int32 PlayerTeamId = TeamSubsystem->FindTeamFromObject(Player);

	// We should have a TeamId by now, but early login stuff before post login can try to do stuff, ignore it.
	if (!ensure(PlayerTeamId != INDEX_NONE))
	{
		return nullptr;
	}

	ALyraGameState* GameState = GetGameStateChecked<ALyraGameState>();

	ALyraPlayerStart* BestPlayerStart = nullptr;
	double MaxDistance = 0;
	ALyraPlayerStart* FallbackPlayerStart = nullptr;
	double FallbackMaxDistance = 0;

	for (APlayerState* PS : GameState->PlayerArray)
	{
		const int32 TeamId = TeamSubsystem->FindTeamFromObject(PS);

		// We should have a TeamId by now...
		if (PS->IsOnlyASpectator() || !ensure(TeamId != INDEX_NONE))
		{
			continue;
		}

		// If the other player isn't on the same team, lets find the furthest spawn from them.
		if (TeamId != PlayerTeamId)
		{
			for (ALyraPlayerStart* PlayerStart : PlayerStarts)
			{
				if (APawn* Pawn = PS->GetPawn())
				{
					const double Distance = PlayerStart->GetDistanceTo(Pawn);

					if (PlayerStart->IsClaimed())
					{
						if (FallbackPlayerStart == nullptr || Distance > FallbackMaxDistance)
						{
							FallbackPlayerStart = PlayerStart;
							FallbackMaxDistance = Distance;
						}
					}
					else if (PlayerStart->GetLocationOccupancy(Player) < ELyraPlayerStartLocationOccupancy::Full)
					{
						if (BestPlayerStart == nullptr || Distance > MaxDistance)
						{
							BestPlayerStart = PlayerStart;
							MaxDistance = Distance;
						}
					}
				}
			}
		}
	}

	if (BestPlayerStart)
	{
		return BestPlayerStart;
	}

	return FallbackPlayerStart;
}

void UTDM_PlayerSpawningManagmentComponent::OnFinishRestartPlayer(AController* Player, const FRotator& StartRotation)
{

}

```

### 附录文件 4：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/MessageProcessors/AssistProcessor.h

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/MessageProcessors/AssistProcessor.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Messages/GameplayMessageProcessor.h"

#include "AssistProcessor.generated.h"

class APlayerState;
class UObject;
struct FGameplayTag;
struct FLyraVerbMessage;
template <typename T> struct TObjectPtr;

// Tracks the damage done to a player by other players
USTRUCT()
struct FPlayerAssistDamageTracking
{
	GENERATED_BODY()

	// Map of damager to damage dealt
	UPROPERTY(Transient)
	TMap<TObjectPtr<APlayerState>, float> AccumulatedDamageByPlayer;
};

// Tracks assists (dealing damage to another player without finishing them)
UCLASS()
class UAssistProcessor : public UGameplayMessageProcessor
{
	GENERATED_BODY()

public:
	virtual void StartListening() override;

private:
	void OnDamageMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload);
	void OnEliminationMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload);

private:
	// Map of player to damage dealt to them
	UPROPERTY(Transient)
	TMap<TObjectPtr<APlayerState>, FPlayerAssistDamageTracking> DamageHistory;
};

```

### 附录文件 5：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/MessageProcessors/AssistProcessor.cpp

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/MessageProcessors/AssistProcessor.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageProcessors/AssistProcessor.h"

#include "GameFramework/PlayerState.h"
#include "Messages/LyraVerbMessage.h"
#include "Messages/LyraVerbMessageHelpers.h"
#include "NativeGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssistProcessor)

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Lyra_Elimination_Message, "Lyra.Elimination.Message");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Lyra_Damage_Message, "Lyra.Damage.Message");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Lyra_Assist_Message, "Lyra.Assist.Message");

void UAssistProcessor::StartListening()
{
	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
	AddListenerHandle(MessageSubsystem.RegisterListener(TAG_Lyra_Elimination_Message, this, &ThisClass::OnEliminationMessage));
	AddListenerHandle(MessageSubsystem.RegisterListener(TAG_Lyra_Damage_Message, this, &ThisClass::OnDamageMessage));
}

void UAssistProcessor::OnDamageMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload)
{
	if (Payload.Instigator != Payload.Target)
	{
		if (APlayerState* InstigatorPS = ULyraVerbMessageHelpers::GetPlayerStateFromObject(Payload.Instigator))
		{
			if (APlayerState* TargetPS = ULyraVerbMessageHelpers::GetPlayerStateFromObject(Payload.Target))
			{
				FPlayerAssistDamageTracking& Damage = DamageHistory.FindOrAdd(TargetPS);
				float& DamageTotalFromTarget = Damage.AccumulatedDamageByPlayer.FindOrAdd(InstigatorPS);
				DamageTotalFromTarget += Payload.Magnitude;
			}
		}
	}
}


void UAssistProcessor::OnEliminationMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload)
{
	if (APlayerState* TargetPS = Cast<APlayerState>(Payload.Target))
	{
		// Grant an assist to each player who damaged the target but wasn't the instigator
		if (FPlayerAssistDamageTracking* DamageOnTarget = DamageHistory.Find(TargetPS))
		{
			for (const auto& KVP : DamageOnTarget->AccumulatedDamageByPlayer)
			{
				if (APlayerState* AssistPS = KVP.Key)
				{
					if (AssistPS != Payload.Instigator)
					{
						FLyraVerbMessage AssistMessage;
						AssistMessage.Verb = TAG_Lyra_Assist_Message;
						AssistMessage.Instigator = AssistPS;
						//@TODO: Get default tags from a player state or save off most recent tags during assist damage?
						//AssistMessage.InstigatorTags = ;
						AssistMessage.Target = TargetPS;
						AssistMessage.TargetTags = Payload.TargetTags;
						AssistMessage.ContextTags = Payload.ContextTags;
						AssistMessage.Magnitude = KVP.Value;

						UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
						MessageSubsystem.BroadcastMessage(AssistMessage.Verb, AssistMessage);
					}
				}
			}

			// Clear the damage log for the eliminated player
			DamageHistory.Remove(TargetPS);
		}
	}
}


```

### 附录文件 6：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/MessageProcessors/ElimChainProcessor.h

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/MessageProcessors/ElimChainProcessor.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Messages/GameplayMessageProcessor.h"

#include "ElimChainProcessor.generated.h"

class APlayerState;
class UObject;
struct FGameplayTag;
struct FLyraVerbMessage;
template <typename T> struct TObjectPtr;

USTRUCT()
struct FPlayerElimChainInfo
{
	GENERATED_BODY()

	double LastEliminationTime = 0.0;

	int32 ChainCounter = 1;
};

// Tracks a chain of eliminations (X eliminations without more than Y seconds passing between each one)
UCLASS(Abstract)
class UElimChainProcessor : public UGameplayMessageProcessor
{
	GENERATED_BODY()

public:
	virtual void StartListening() override;

protected:
	UPROPERTY(EditDefaultsOnly)
	float ChainTimeLimit = 4.5f;

	// The event to rebroadcast when a user gets a chain of a certain length
	UPROPERTY(EditDefaultsOnly)
	TMap<int32, FGameplayTag> ElimChainTags;

private:
	void OnEliminationMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload);

private:
	UPROPERTY(Transient)
	TMap<TObjectPtr<APlayerState>, FPlayerElimChainInfo> PlayerChainHistory;
};

```

### 附录文件 7：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/MessageProcessors/ElimChainProcessor.cpp

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/MessageProcessors/ElimChainProcessor.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageProcessors/ElimChainProcessor.h"

#include "GameFramework/PlayerState.h"
#include "Messages/LyraVerbMessage.h"
#include "NativeGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ElimChainProcessor)

namespace ElimChain
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Lyra_Elimination_Message, "Lyra.Elimination.Message");
}

void UElimChainProcessor::StartListening()
{
	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
	AddListenerHandle(MessageSubsystem.RegisterListener(ElimChain::TAG_Lyra_Elimination_Message, this, &ThisClass::OnEliminationMessage));
}

void UElimChainProcessor::OnEliminationMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload)
{
	// Track elimination chains for the attacker (except for self-eliminations)
	if (Payload.Instigator != Payload.Target)
	{
		if (APlayerState* InstigatorPS = Cast<APlayerState>(Payload.Instigator))
		{
			const double CurrentTime = GetServerTime();

			FPlayerElimChainInfo& History = PlayerChainHistory.FindOrAdd(InstigatorPS);
			const bool bStreakReset = (History.LastEliminationTime == 0.0) || (History.LastEliminationTime + ChainTimeLimit < CurrentTime);

			History.LastEliminationTime = CurrentTime;
			if (bStreakReset)
			{
				History.ChainCounter = 1;
			}
			else
			{
				++History.ChainCounter;

				if (FGameplayTag* pTag = ElimChainTags.Find(History.ChainCounter))
				{
					FLyraVerbMessage ElimChainMessage;
					ElimChainMessage.Verb = *pTag;
					ElimChainMessage.Instigator = InstigatorPS;
					ElimChainMessage.InstigatorTags = Payload.InstigatorTags;
					ElimChainMessage.ContextTags = Payload.ContextTags;
					ElimChainMessage.Magnitude = History.ChainCounter;

					UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
					MessageSubsystem.BroadcastMessage(ElimChainMessage.Verb, ElimChainMessage);
				}
			}
		}
	}
}


```

### 附录文件 8：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/MessageProcessors/ElimStreakProcessor.h

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/MessageProcessors/ElimStreakProcessor.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Messages/GameplayMessageProcessor.h"

#include "ElimStreakProcessor.generated.h"

class APlayerState;
class UObject;
struct FGameplayTag;
struct FLyraVerbMessage;
template <typename T> struct TObjectPtr;

// Tracks a streak of eliminations (X eliminations without being eliminated)
UCLASS(Abstract)
class UElimStreakProcessor : public UGameplayMessageProcessor
{
	GENERATED_BODY()

public:
	virtual void StartListening() override;

protected:
	// The event to rebroadcast when a user gets a streak of a certain length
	UPROPERTY(EditDefaultsOnly)
	TMap<int32, FGameplayTag> ElimStreakTags;

private:
	void OnEliminationMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload);

private:
	UPROPERTY(Transient)
	TMap<TObjectPtr<APlayerState>, int32> PlayerStreakHistory;
};

```

### 附录文件 9：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/MessageProcessors/ElimStreakProcessor.cpp

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/MessageProcessors/ElimStreakProcessor.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageProcessors/ElimStreakProcessor.h"

#include "GameFramework/PlayerState.h"
#include "Messages/LyraVerbMessage.h"
#include "NativeGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ElimStreakProcessor)

namespace ElimStreak
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Lyra_Elimination_Message, "Lyra.Elimination.Message");
}

void UElimStreakProcessor::StartListening()
{
	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
	AddListenerHandle(MessageSubsystem.RegisterListener(ElimStreak::TAG_Lyra_Elimination_Message, this, &ThisClass::OnEliminationMessage));
}

void UElimStreakProcessor::OnEliminationMessage(FGameplayTag Channel, const FLyraVerbMessage& Payload)
{
	// Track elimination streaks for the attacker (except for self-eliminations)
	if (Payload.Instigator != Payload.Target)
	{
		if (APlayerState* InstigatorPS = Cast<APlayerState>(Payload.Instigator))
		{
			int32& StreakCount = PlayerStreakHistory.FindOrAdd(InstigatorPS);
			StreakCount++;

			if (FGameplayTag* pTag = ElimStreakTags.Find(StreakCount))
			{
				FLyraVerbMessage ElimStreakMessage;
				ElimStreakMessage.Verb = *pTag;
				ElimStreakMessage.Instigator = InstigatorPS;
				ElimStreakMessage.InstigatorTags = Payload.InstigatorTags;
				ElimStreakMessage.ContextTags = Payload.ContextTags;
				ElimStreakMessage.Magnitude = StreakCount;

				UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
				MessageSubsystem.BroadcastMessage(ElimStreakMessage.Verb, ElimStreakMessage);
			}
		}
	}

	// End the elimination streak for the target
	if (APlayerState* TargetPS = Cast<APlayerState>(Payload.Target))
	{
		PlayerStreakHistory.Remove(TargetPS);
	}
}


```

### 附录文件 10：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/Accolades/LyraAccoladeDefinition.h

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/Accolades/LyraAccoladeDefinition.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"

#include "LyraAccoladeDefinition.generated.h"

class UObject;
class USoundBase;

USTRUCT(BlueprintType)
struct FLyraAccoladeDefinitionRow : public FTableRowBase
{
	GENERATED_BODY()

public:
	// The message to display
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText DisplayName;

	// The sound to play
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<USoundBase> Sound;

	// The icon to display
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(DisplayThumbnail="true", AllowedClasses="/Script/Engine.Texture,/Script/Engine.MaterialInterface,/Script/Engine.SlateTextureAtlasInterface", DisallowedClasses="/Script/MediaAssets.MediaTexture"))
	TSoftObjectPtr<UObject> Icon;

	// Duration (in seconds) to display this accolade
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float DisplayDuration = 1.0f;

	// Location to display this accolade
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag LocationTag;

	// Tags associated with this accolade
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTagContainer AccoladeTags;

	// When this accolade is displayed, any existing displayed/pending accolades with any of
	// these tags will be removed (e.g., getting a triple-elim will suppress a double-elim)
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTagContainer CancelAccoladesWithTag;
};

/**
 *
 */
UCLASS(BlueprintType)
class ULyraAccoladeDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	// The sound to play
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<USoundBase> Sound;

	// The icon to display
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayThumbnail="true", AllowedClasses="/Script/Engine.Texture,/Script/Engine.MaterialInterface,/Script/Engine.SlateTextureAtlasInterface", DisallowedClasses="/Script/MediaAssets.MediaTexture"))
	TObjectPtr<UObject> Icon;

	// Tags associated with this accolade
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTagContainer AccoladeTags;

	// When this accolade is displayed, any existing displayed/pending accolades with any of
	// these tags will be removed (e.g., getting a triple-elim will suppress a double-elim)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTagContainer CancelAccoladesWithTag;
};

```

### 附录文件 11：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Accolades/LyraAccoladeDefinition.cpp

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Accolades/LyraAccoladeDefinition.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Accolades/LyraAccoladeDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraAccoladeDefinition)


```

### 附录文件 12：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/Accolades/LyraAccoladeHostWidget.h

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/Accolades/LyraAccoladeHostWidget.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Accolades/LyraAccoladeDefinition.h"
#include "AsyncMixin.h"
#include "CommonUserWidget.h"
#include "GameFramework/GameplayMessageSubsystem.h"

#include "LyraAccoladeHostWidget.generated.h"

class UObject;
class USoundBase;
class UUserWidget;
struct FDataRegistryAcquireResult;
struct FLyraNotificationMessage;

USTRUCT(BlueprintType)
struct FPendingAccoladeEntry
{
	GENERATED_BODY();

	UPROPERTY(BlueprintReadOnly)
	FLyraAccoladeDefinitionRow Row;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<USoundBase> Sound = nullptr;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UObject> Icon = nullptr;

	UPROPERTY()
	TObjectPtr<UUserWidget> AllocatedWidget = nullptr;

	int32 SequenceID = 0;

	bool bFinishedLoading = false;

	void CancelDisplay();
};

/**
 *
 */
UCLASS(BlueprintType)
class ULyraAccoladeHostWidget : public UCommonUserWidget, public FAsyncMixin
{
	GENERATED_BODY()

public:
	// The location tag (used to filter incoming messages to only display the appropriate accolades in a given location)
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag LocationName;

	//~UUserWidget interface
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~End of UUserWidget interface

	UFUNCTION(BlueprintImplementableEvent)
	void DestroyAccoladeWidget(UUserWidget* Widget);

	UFUNCTION(BlueprintImplementableEvent)
	UUserWidget* CreateAccoladeWidget(const FPendingAccoladeEntry& Entry);
private:
	FGameplayMessageListenerHandle ListenerHandle;

	int32 NextDisplaySequenceID = 0;
	int32 AllocatedSequenceID = 0;

	FTimerHandle NextTimeToReconsiderHandle;

	// List of async pending load accolades (which might come in the wrong order due to the row read)
	UPROPERTY(Transient)
	TArray<FPendingAccoladeEntry> PendingAccoladeLoads;

	// List of pending accolades (due to one at a time display duration; the first one in the list is the current visible one)
	UPROPERTY(Transient)
	TArray<FPendingAccoladeEntry> PendingAccoladeDisplays;


	void OnNotificationMessage(FGameplayTag Channel, const FLyraNotificationMessage& Notification);
	void OnRegistryLoadCompleted(const FDataRegistryAcquireResult& AccoladeHandle, int32 SequenceID);

	void ConsiderLoadedAccolades();
	void PopDisplayedAccolade();
	void ProcessLoadedAccolade(const FPendingAccoladeEntry& Entry);
	void DisplayNextAccolade();
};

```

### 附录文件 13：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Accolades/LyraAccoladeHostWidget.cpp

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Accolades/LyraAccoladeHostWidget.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Accolades/LyraAccoladeHostWidget.h"

#include "DataRegistrySubsystem.h"
#include "LyraLogChannels.h"
#include "Messages/LyraNotificationMessage.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraAccoladeHostWidget)

class UUserWidget;

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Lyra_ShooterGame_Accolade, "Lyra.ShooterGame.Accolade");

static FName NAME_AccoladeRegistryID("Accolades");

void ULyraAccoladeHostWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
	ListenerHandle = MessageSubsystem.RegisterListener(TAG_Lyra_AddNotification_Message, this, &ThisClass::OnNotificationMessage);
}

void ULyraAccoladeHostWidget::NativeDestruct()
{
	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
	MessageSubsystem.UnregisterListener(ListenerHandle);

	CancelAsyncLoading();

	Super::NativeDestruct();
}

void ULyraAccoladeHostWidget::OnNotificationMessage(FGameplayTag Channel, const FLyraNotificationMessage& Notification)
{
	if (Notification.TargetChannel == TAG_Lyra_ShooterGame_Accolade)
	{
		// Ignore notifications for other players
		if (Notification.TargetPlayer != nullptr)
		{
			APlayerController* PC = GetOwningPlayer();
			if ((PC == nullptr) || (PC->PlayerState != Notification.TargetPlayer))
			{
				return;
			}
		}

		// Load the data registry row for this accolade
		const int32 NextID = AllocatedSequenceID;
		++AllocatedSequenceID;

		FDataRegistryId ItemID(NAME_AccoladeRegistryID, Notification.PayloadTag.GetTagName());
		if (!UDataRegistrySubsystem::Get()->AcquireItem(ItemID, FDataRegistryItemAcquiredCallback::CreateUObject(this, &ThisClass::OnRegistryLoadCompleted, NextID)))
		{
			UE_LOG(LogLyra, Error, TEXT("Failed to find accolade registry for tag %s, accolades will not appear"), *Notification.PayloadTag.GetTagName().ToString());
			--AllocatedSequenceID;
		}
	}
}

void ULyraAccoladeHostWidget::OnRegistryLoadCompleted(const FDataRegistryAcquireResult& AccoladeHandle, int32 SequenceID)
{
	if (const FLyraAccoladeDefinitionRow* AccoladeRow = AccoladeHandle.GetItem<FLyraAccoladeDefinitionRow>())
	{
		FPendingAccoladeEntry& PendingEntry = PendingAccoladeLoads.AddDefaulted_GetRef();
		PendingEntry.Row = *AccoladeRow;
		PendingEntry.SequenceID = SequenceID;

		TArray<FSoftObjectPath> AssetsToLoad;
		AssetsToLoad.Add(AccoladeRow->Sound.ToSoftObjectPath());
		AssetsToLoad.Add(AccoladeRow->Icon.ToSoftObjectPath());
		AsyncLoad(AssetsToLoad, [this, SequenceID]
		{
			FPendingAccoladeEntry* EntryThatFinishedLoading = PendingAccoladeLoads.FindByPredicate([SequenceID](const FPendingAccoladeEntry& Entry) { return Entry.SequenceID == SequenceID; });
			if (ensure(EntryThatFinishedLoading))
			{
				EntryThatFinishedLoading->Sound = EntryThatFinishedLoading->Row.Sound.Get();
				EntryThatFinishedLoading->Icon = EntryThatFinishedLoading->Row.Icon.Get();
				EntryThatFinishedLoading->bFinishedLoading = true;
				ConsiderLoadedAccolades();
			}
		});
		StartAsyncLoading();
	}
	else
	{
		ensure(false);
	}
}

void ULyraAccoladeHostWidget::ConsiderLoadedAccolades()
{
	int32 PendingIndexToDisplay;
	do
	{
		PendingIndexToDisplay = PendingAccoladeLoads.IndexOfByPredicate([DesiredID=NextDisplaySequenceID](const FPendingAccoladeEntry& Entry) { return Entry.bFinishedLoading && Entry.SequenceID == DesiredID; });
		if (PendingIndexToDisplay != INDEX_NONE)
		{
			FPendingAccoladeEntry Entry = MoveTemp(PendingAccoladeLoads[PendingIndexToDisplay]);
			PendingAccoladeLoads.RemoveAtSwap(PendingIndexToDisplay);

			ProcessLoadedAccolade(Entry);
			++NextDisplaySequenceID;
		}
	} while (PendingIndexToDisplay != INDEX_NONE);
}

void ULyraAccoladeHostWidget::ProcessLoadedAccolade(const FPendingAccoladeEntry& Entry)
{
	if (Entry.Row.LocationTag == LocationName)
	{
		bool bRecreateWidget = PendingAccoladeDisplays.Num() == 0;
		for (int32 Index = 0; Index < PendingAccoladeDisplays.Num(); )
		{
			if (PendingAccoladeDisplays[Index].Row.AccoladeTags.HasAny(Entry.Row.CancelAccoladesWithTag))
			{
				if (UUserWidget* OldWidget = PendingAccoladeDisplays[Index].AllocatedWidget)
				{
					DestroyAccoladeWidget(OldWidget);
					bRecreateWidget = true;
				}
				PendingAccoladeDisplays.RemoveAt(Index);
			}
			else
			{
				++Index;
			}
		}

		PendingAccoladeDisplays.Add(Entry);

		if (bRecreateWidget)
		{
			DisplayNextAccolade();
		}
	}
}

void ULyraAccoladeHostWidget::DisplayNextAccolade()
{
	if (PendingAccoladeDisplays.Num() > 0)
	{
		FPendingAccoladeEntry& Entry = PendingAccoladeDisplays[0];

		GetWorld()->GetTimerManager().SetTimer(NextTimeToReconsiderHandle, this, &ThisClass::PopDisplayedAccolade, Entry.Row.DisplayDuration);
		Entry.AllocatedWidget = CreateAccoladeWidget(Entry);
	}
}

void ULyraAccoladeHostWidget::PopDisplayedAccolade()
{
	if (PendingAccoladeDisplays.Num() > 0)
	{
		if (UUserWidget* OldWidget = PendingAccoladeDisplays[0].AllocatedWidget)
		{
			DestroyAccoladeWidget(OldWidget);
		}
		PendingAccoladeDisplays.RemoveAt(0);
	}

	DisplayNextAccolade();
}


```

### 附录文件 14：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/ShooterCoreRuntimeSettings.h

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/ShooterCoreRuntimeSettings.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"

#include "ShooterCoreRuntimeSettings.generated.h"

class UObject;

/** Runtime settings specific to the ShooterCoreRuntime plugin */
UCLASS(config = Game, defaultconfig)
class UShooterCoreRuntimeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UShooterCoreRuntimeSettings(const FObjectInitializer& Initializer);

	ECollisionChannel GetAimAssistCollisionChannel() const { return AimAssistCollisionChannel; }

private:

	/**
	 * What trace channel should be used to find available targets for Aim Assist.
	 * @see UAimAssistTargetManagerComponent::GetVisibleTargets
	 */
	UPROPERTY(config, EditAnywhere, Category = "Aim Assist")
	TEnumAsByte<ECollisionChannel> AimAssistCollisionChannel = ECollisionChannel::ECC_EngineTraceChannel5;
};

```

### 附录文件 15：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/ShooterCoreRuntimeSettings.cpp

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/ShooterCoreRuntimeSettings.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterCoreRuntimeSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ShooterCoreRuntimeSettings)

UShooterCoreRuntimeSettings::UShooterCoreRuntimeSettings(const FObjectInitializer& Initializer)
	: Super(Initializer)
{

}

```

### 附录文件 16：Source/LyraGame/AbilitySystem/LyraAbilitySystemGlobals.h

来源：Source/LyraGame/AbilitySystem/LyraAbilitySystemGlobals.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemGlobals.h"

#include "LyraAbilitySystemGlobals.generated.h"

class UObject;
struct FGameplayEffectContext;

UCLASS(Config=Game)
class ULyraAbilitySystemGlobals : public UAbilitySystemGlobals
{
	GENERATED_UCLASS_BODY()

	//~UAbilitySystemGlobals interface
	virtual FGameplayEffectContext* AllocGameplayEffectContext() const override;
	//~End of UAbilitySystemGlobals interface
};

```

### 附录文件 17：Source/LyraGame/AbilitySystem/LyraAbilitySystemGlobals.cpp

来源：Source/LyraGame/AbilitySystem/LyraAbilitySystemGlobals.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraAbilitySystemGlobals.h"

#include "LyraGameplayEffectContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraAbilitySystemGlobals)

struct FGameplayEffectContext;

ULyraAbilitySystemGlobals::ULyraAbilitySystemGlobals(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FGameplayEffectContext* ULyraAbilitySystemGlobals::AllocGameplayEffectContext() const
{
	return new FLyraGameplayEffectContext();
}


```

### 附录文件 18：Source/LyraGame/AbilitySystem/LyraGameplayEffectContext.h

来源：Source/LyraGame/AbilitySystem/LyraGameplayEffectContext.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffectTypes.h"

#include "LyraGameplayEffectContext.generated.h"

class AActor;
class FArchive;
class ILyraAbilitySourceInterface;
class UObject;
class UPhysicalMaterial;

USTRUCT()
struct FLyraGameplayEffectContext : public FGameplayEffectContext
{
	GENERATED_BODY()

	FLyraGameplayEffectContext()
		: FGameplayEffectContext()
	{
	}

	FLyraGameplayEffectContext(AActor* InInstigator, AActor* InEffectCauser)
		: FGameplayEffectContext(InInstigator, InEffectCauser)
	{
	}

	/** Returns the wrapped FLyraGameplayEffectContext from the handle, or nullptr if it doesn't exist or is the wrong type */
	static LYRAGAME_API FLyraGameplayEffectContext* ExtractEffectContext(struct FGameplayEffectContextHandle Handle);

	/** Sets the object used as the ability source */
	void SetAbilitySource(const ILyraAbilitySourceInterface* InObject, float InSourceLevel);

	/** Returns the ability source interface associated with the source object. Only valid on the authority. */
	const ILyraAbilitySourceInterface* GetAbilitySource() const;

	virtual FGameplayEffectContext* Duplicate() const override
	{
		FLyraGameplayEffectContext* NewContext = new FLyraGameplayEffectContext();
		*NewContext = *this;
		if (GetHitResult())
		{
			// Does a deep copy of the hit result
			NewContext->AddHitResult(*GetHitResult(), true);
		}
		return NewContext;
	}

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FLyraGameplayEffectContext::StaticStruct();
	}

	/** Overridden to serialize new fields */
	virtual bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) override;

	/** Returns the physical material from the hit result if there is one */
	const UPhysicalMaterial* GetPhysicalMaterial() const;

public:
	/** ID to allow the identification of multiple bullets that were part of the same cartridge */
	UPROPERTY()
	int32 CartridgeID = -1;

protected:
	/** Ability Source object (should implement ILyraAbilitySourceInterface). NOT replicated currently */
	UPROPERTY()
	TWeakObjectPtr<const UObject> AbilitySourceObject;
};

template<>
struct TStructOpsTypeTraits<FLyraGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FLyraGameplayEffectContext>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};


```

### 附录文件 19：Source/LyraGame/AbilitySystem/LyraGameplayEffectContext.cpp

来源：Source/LyraGame/AbilitySystem/LyraGameplayEffectContext.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraGameplayEffectContext.h"

#include "AbilitySystem/LyraAbilitySourceInterface.h"
#include "Engine/HitResult.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Serialization/GameplayEffectContextNetSerializer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraGameplayEffectContext)

class FArchive;

FLyraGameplayEffectContext* FLyraGameplayEffectContext::ExtractEffectContext(struct FGameplayEffectContextHandle Handle)
{
	FGameplayEffectContext* BaseEffectContext = Handle.Get();
	if ((BaseEffectContext != nullptr) && BaseEffectContext->GetScriptStruct()->IsChildOf(FLyraGameplayEffectContext::StaticStruct()))
	{
		return (FLyraGameplayEffectContext*)BaseEffectContext;
	}

	return nullptr;
}

bool FLyraGameplayEffectContext::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	FGameplayEffectContext::NetSerialize(Ar, Map, bOutSuccess);

	// Not serialized for post-activation use:
	// CartridgeID

	return true;
}

namespace UE::Net
{
	// Forward to FGameplayEffectContextNetSerializer
	// Note: If FLyraGameplayEffectContext::NetSerialize() is modified, a custom NetSerializer must be implemented as the current fallback will no longer be sufficient.
	UE_NET_IMPLEMENT_FORWARDING_NETSERIALIZER_AND_REGISTRY_DELEGATES(LyraGameplayEffectContext, FGameplayEffectContextNetSerializer);
}

void FLyraGameplayEffectContext::SetAbilitySource(const ILyraAbilitySourceInterface* InObject, float InSourceLevel)
{
	AbilitySourceObject = MakeWeakObjectPtr(Cast<const UObject>(InObject));
	//SourceLevel = InSourceLevel;
}

const ILyraAbilitySourceInterface* FLyraGameplayEffectContext::GetAbilitySource() const
{
	return Cast<ILyraAbilitySourceInterface>(AbilitySourceObject.Get());
}

const UPhysicalMaterial* FLyraGameplayEffectContext::GetPhysicalMaterial() const
{
	if (const FHitResult* HitResultPtr = GetHitResult())
	{
		return HitResultPtr->PhysMaterial.Get();
	}
	return nullptr;
}


```

### 附录文件 20：Source/LyraGame/AbilitySystem/LyraGameplayAbilityTargetData_SingleTargetHit.h

来源：Source/LyraGame/AbilitySystem/LyraGameplayAbilityTargetData_SingleTargetHit.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Abilities/GameplayAbilityTargetTypes.h"

#include "LyraGameplayAbilityTargetData_SingleTargetHit.generated.h"

class FArchive;
struct FGameplayEffectContextHandle;


/** Game-specific additions to SingleTargetHit tracking */
USTRUCT()
struct FLyraGameplayAbilityTargetData_SingleTargetHit : public FGameplayAbilityTargetData_SingleTargetHit
{
	GENERATED_BODY()

	FLyraGameplayAbilityTargetData_SingleTargetHit()
		: CartridgeID(-1)
	{ }

	virtual void AddTargetDataToContext(FGameplayEffectContextHandle& Context, bool bIncludeActorArray) const override;

	/** ID to allow the identification of multiple bullets that were part of the same cartridge */
	UPROPERTY()
	int32 CartridgeID;

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

	virtual UScriptStruct* GetScriptStruct() const override
	{
		return FLyraGameplayAbilityTargetData_SingleTargetHit::StaticStruct();
	}
};

template<>
struct TStructOpsTypeTraits<FLyraGameplayAbilityTargetData_SingleTargetHit> : public TStructOpsTypeTraitsBase2<FLyraGameplayAbilityTargetData_SingleTargetHit>
{
	enum
	{
		WithNetSerializer = true	// For now this is REQUIRED for FGameplayAbilityTargetDataHandle net serialization to work
	};
};


```

### 附录文件 21：Source/LyraGame/AbilitySystem/LyraGameplayAbilityTargetData_SingleTargetHit.cpp

来源：Source/LyraGame/AbilitySystem/LyraGameplayAbilityTargetData_SingleTargetHit.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraGameplayAbilityTargetData_SingleTargetHit.h"

#include "LyraGameplayEffectContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraGameplayAbilityTargetData_SingleTargetHit)

struct FGameplayEffectContextHandle;

//////////////////////////////////////////////////////////////////////

void FLyraGameplayAbilityTargetData_SingleTargetHit::AddTargetDataToContext(FGameplayEffectContextHandle& Context, bool bIncludeActorArray) const
{
	FGameplayAbilityTargetData_SingleTargetHit::AddTargetDataToContext(Context, bIncludeActorArray);

	// Add game-specific data
	if (FLyraGameplayEffectContext* TypedContext = FLyraGameplayEffectContext::ExtractEffectContext(Context))
	{
		TypedContext->CartridgeID = CartridgeID;
	}
}

bool FLyraGameplayAbilityTargetData_SingleTargetHit::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	FGameplayAbilityTargetData_SingleTargetHit::NetSerialize(Ar, Map, bOutSuccess);

	Ar << CartridgeID;

	return true;
}


```

### 附录文件 22：Source/LyraGame/AbilitySystem/LyraAbilitySourceInterface.h

来源：Source/LyraGame/AbilitySystem/LyraAbilitySourceInterface.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "LyraAbilitySourceInterface.generated.h"

class UObject;
class UPhysicalMaterial;
struct FGameplayTagContainer;

/** Base interface for anything acting as a ability calculation source */
UINTERFACE()
class ULyraAbilitySourceInterface : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class ILyraAbilitySourceInterface
{
	GENERATED_IINTERFACE_BODY()

	/**
	 * Compute the multiplier for effect falloff with distance
	 *
	 * @param Distance			Distance from source to target for ability calculations (distance bullet traveled for a gun, etc...)
	 * @param SourceTags		Aggregated Tags from the source
	 * @param TargetTags		Aggregated Tags currently on the target
	 *
	 * @return Multiplier to apply to the base attribute value due to distance
	 */
	virtual float GetDistanceAttenuation(float Distance, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr) const = 0;

	virtual float GetPhysicalMaterialAttenuation(const UPhysicalMaterial* PhysicalMaterial, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr) const = 0;
};

```

### 附录文件 23：Source/LyraGame/AbilitySystem/LyraAbilitySourceInterface.cpp

来源：Source/LyraGame/AbilitySystem/LyraAbilitySourceInterface.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraAbilitySourceInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraAbilitySourceInterface)

ULyraAbilitySourceInterface::ULyraAbilitySourceInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}


```
