# Lyra 55：输入重映射与辅助瞄准源码

> 版本基准：UE 5.8.0；本机 CL 55116800，分支 ++UE5+Release-5.8。
> 源码依据：LyraStarterGame 5.8 的 Source/LyraGame/Input 与 GameFeatures/ShooterCore/Input 实际 C++。
> 适用范围：Enhanced Input 用户设置、可重映射配置档、死区/灵敏度/反转修正、Latency Marker、ShooterCore 辅助瞄准。
> 兼容性边界：辅助瞄准是 ShooterCore 项目玩法，不是 Enhanced Input 默认行为；平台支持和延迟模块须按构建目标复查。
> 官方参考：https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine
> 最后更新：2026-08-18（新增项目输入设置、配置档、延迟标记和辅助瞄准真实源码）。
> 知识成熟度：L2（本机源码静态核对；手柄/鼠标平台矩阵和实机体验仍需运行验证）。

## 一、覆盖目标

42 篇覆盖了 LyraInputConfig、LyraInputComponent、InputTag 到 GAS Ability 的激活路径，但以下项目输入文件此前只被路径或类名点到：

1. LyraInputModifiers 把 LyraSettingsShared 的死区、灵敏度、反转选项变成 Enhanced Input 的 FInputActionValue。
2. LyraInputUserSettings 与 LyraPlayerMappableKeyProfile 是用户设置和配置档的实际扩展钩子。
3. LyraPlayerInput 在 InputKey 边界触发 Latency Marker，而不是在 Ability 激活后猜测输入时间。
4. ShooterCore 辅助瞄准先做世界 Overlap，再做队伍、死亡、Tag、投影和可见性筛选，最后改写输入。

## 二、数据流

ULyraSettingsShared → LyraInputModifier::ModifyRaw → UEnhancedPlayerInput → ULyraPlayerInput::InputKey → Latency Marker。

ShooterCore AimAssistTargetManager → Overlap/AimAssist channel → IAimAssistTarget options → Team/Health/Tag/Range filter → ScreenBounds/SortScore/Visibility → AimAssistInputModifier。

## 三、设置驱动的输入修正

ULyraSettingBasedScalar 使用 ULyraSettingsShared 的反射属性名查找并缓存 X/Y/Z 三个 FProperty；Axis1D/2D/3D 按值类型取设置，再按 Clamp 范围返回缩放值。


### SettingBasedScalar

来源：Source/LyraGame/Input/LyraInputModifiers.cpp

```cpp
FInputActionValue ULyraSettingBasedScalar::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime)
{
	if (ensureMsgf(CurrentValue.GetValueType() != EInputActionValueType::Boolean, TEXT("Setting Based Scalar modifier doesn't support boolean values.")))
	{
		if (ULyraLocalPlayer* LocalPlayer = LyraInputModifiersHelpers::GetLocalPlayer(PlayerInput))
		{
			const UClass* SettingsClass = ULyraSettingsShared::StaticClass();
			ULyraSettingsShared* SharedSettings = LocalPlayer->GetSharedSettings();

			const bool bHasCachedProperty = PropertyCache.Num() == 3;

			const FProperty* XAxisValue = bHasCachedProperty ? PropertyCache[0] : SettingsClass->FindPropertyByName(XAxisScalarSettingName);
			const FProperty* YAxisValue = bHasCachedProperty ? PropertyCache[1] : SettingsClass->FindPropertyByName(YAxisScalarSettingName);
			const FProperty* ZAxisValue = bHasCachedProperty ? PropertyCache[2] : SettingsClass->FindPropertyByName(ZAxisScalarSettingName);

			if (PropertyCache.IsEmpty())
			{
				PropertyCache.Emplace(XAxisValue);
				PropertyCache.Emplace(YAxisValue);
				PropertyCache.Emplace(ZAxisValue);
			}

			FVector ScalarToUse = FVector(1.0, 1.0, 1.0);

			switch (CurrentValue.GetValueType())
			{
			case EInputActionValueType::Axis3D:
				ScalarToUse.Z = ZAxisValue ? *ZAxisValue->ContainerPtrToValuePtr<double>(SharedSettings) : 1.0;
				//[[fallthrough]];
			case EInputActionValueType::Axis2D:
				ScalarToUse.Y = YAxisValue ? *YAxisValue->ContainerPtrToValuePtr<double>(SharedSettings) : 1.0;
				//[[fallthrough]];
			case EInputActionValueType::Axis1D:
				ScalarToUse.X = XAxisValue ? *XAxisValue->ContainerPtrToValuePtr<double>(SharedSettings) : 1.0;
				break;
			}

			ScalarToUse.X = FMath::Clamp(ScalarToUse.X, MinValueClamp.X, MaxValueClamp.X);
			ScalarToUse.Y = FMath::Clamp(ScalarToUse.Y, MinValueClamp.Y, MaxValueClamp.Y);
			ScalarToUse.Z = FMath::Clamp(ScalarToUse.Z, MinValueClamp.Z, MaxValueClamp.Z);

			return CurrentValue.Get<FVector>() * ScalarToUse;
		}
	}

	return CurrentValue;
}
```


### DeadZone

来源：Source/LyraGame/Input/LyraInputModifiers.cpp

```cpp
FInputActionValue ULyraInputModifierDeadZone::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime)
{
	EInputActionValueType ValueType = CurrentValue.GetValueType();
	ULyraLocalPlayer* LocalPlayer = LyraInputModifiersHelpers::GetLocalPlayer(PlayerInput);
	if (ValueType == EInputActionValueType::Boolean || !LocalPlayer)
	{
		return CurrentValue;
	}

	ULyraSettingsShared* Settings = LocalPlayer->GetSharedSettings();
	ensure(Settings);

	float LowerThreshold =
		(DeadzoneStick == EDeadzoneStick::MoveStick) ?
		Settings->GetGamepadMoveStickDeadZone() :
		Settings->GetGamepadLookStickDeadZone();

	LowerThreshold = FMath::Clamp(LowerThreshold, 0.0f, 1.0f);

	auto DeadZoneLambda = [LowerThreshold, this](const float AxisVal)
	{
		// We need to translate and scale the input to the +/- 1 range after removing the dead zone.
		return FMath::Min(1.f, (FMath::Max(0.f, FMath::Abs(AxisVal) - LowerThreshold) / (UpperThreshold - LowerThreshold))) * FMath::Sign(AxisVal);
	};

	FVector NewValue = CurrentValue.Get<FVector>();
	switch (Type)
	{
	case EDeadZoneType::Axial:
		NewValue.X = DeadZoneLambda(NewValue.X);
		NewValue.Y = DeadZoneLambda(NewValue.Y);
		NewValue.Z = DeadZoneLambda(NewValue.Z);
		break;
	case EDeadZoneType::Radial:
		if (ValueType == EInputActionValueType::Axis3D)
		{
			NewValue = NewValue.GetSafeNormal() * DeadZoneLambda(NewValue.Size());
		}
		else if (ValueType == EInputActionValueType::Axis2D)
		{
			NewValue = NewValue.GetSafeNormal2D() * DeadZoneLambda(NewValue.Size2D());
		}
		else
		{
			NewValue.X = DeadZoneLambda(NewValue.X);
		}
		break;
	}

	return NewValue;
}
```


### Sensitivity 与反转

来源：Source/LyraGame/Input/LyraInputModifiers.cpp

```cpp
FInputActionValue ULyraInputModifierGamepadSensitivity::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime)
{
	// You can't scale a boolean action type
	ULyraLocalPlayer* LocalPlayer = LyraInputModifiersHelpers::GetLocalPlayer(PlayerInput);
	if (CurrentValue.GetValueType() == EInputActionValueType::Boolean || !LocalPlayer || !SensitivityLevelTable)
	{
		return CurrentValue;
	}

	ULyraSettingsShared* Settings = LocalPlayer->GetSharedSettings();
	ensure(Settings);

	const ELyraGamepadSensitivity Sensitivity = (TargetingType == ELyraTargetingType::Normal) ? Settings->GetGamepadLookSensitivityPreset() : Settings->GetGamepadTargetingSensitivityPreset();

	const float Scalar = SensitivityLevelTable->SensitivtyEnumToFloat(Sensitivity);

	return CurrentValue.Get<FVector>() * Scalar;
}

//////////////////////////////////////////////////////////////////////
// ULyraInputModifierAimInversion

FInputActionValue ULyraInputModifierAimInversion::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime)
{
	ULyraLocalPlayer* LocalPlayer = LyraInputModifiersHelpers::GetLocalPlayer(PlayerInput);
	if (!LocalPlayer)
	{
		return CurrentValue;
	}

	ULyraSettingsShared* Settings = LocalPlayer->GetSharedSettings();
	ensure(Settings);

	FVector NewValue = CurrentValue.Get<FVector>();

	if (Settings->GetInvertVerticalAxis())
	{
		NewValue.Y *= -1.0f;
	}

	if (Settings->GetInvertHorizontalAxis())
	{
		NewValue.X *= -1.0f;
	}

	return NewValue;
```


### Latency Marker InputKey

来源：Source/LyraGame/Input/LyraPlayerInput.cpp

```cpp
bool ULyraPlayerInput::InputKey(const FInputKeyEventArgs& Params)
{
	const bool bResult = Super::InputKey(Params);

	// Note: Since Lyra is only going to support the "Reflex" plugin to handle latency markers,
	// we could #if PLATFORM_DESKTOP this away to save on other platforms. However, for the sake
	// of extensibility for this same project we will not do that.
	ProcessInputEventForLatencyMarker(Params);

	return bResult;
}

void ULyraPlayerInput::ProcessInputEventForLatencyMarker(const FInputKeyEventArgs& Params)
{
	if (!bShouldTriggerLatencyFlash)
	{
		return;
	}

	// Flash the latency marker on left mouse down
	if (Params.Key == EKeys::LeftMouseButton)
	{
		TArray<ILatencyMarkerModule*> LatencyMarkerModules = IModularFeatures::Get().GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());

		for (ILatencyMarkerModule* LatencyMarkerModule : LatencyMarkerModules)
		{
			// TRIGGER_FLASH is 7
			LatencyMarkerModule->SetCustomLatencyMarker(7, GFrameCounter);
		}
	}
}

void ULyraPlayerInput::BindToLatencyMarkerSettingChange()
{
	if (!ULyraSettingsLocal::DoesPlatformSupportLatencyMarkers())
	{
		return;
	}

	ULyraSettingsLocal* Settings = ULyraSettingsLocal::Get();
	if (!Settings)
	{
		return;
	}

	Settings->OnLatencyFlashInidicatorSettingsChangedEvent().AddUObject(this, &ThisClass::HandleLatencyMarkerSettingChanged);

	// Initalize the settings and make sure that the input latency modules are enabled
	HandleLatencyMarkerSettingChanged();
}

void ULyraPlayerInput::UnbindLatencyMarkerSettingChangeListener()
{
	ULyraSettingsLocal* Settings = ULyraSettingsLocal::Get();
	if (!Settings)
	{
		return;
	}

	Settings->OnLatencyFlashInidicatorSettingsChangedEvent().RemoveAll(this);
}

void ULyraPlayerInput::HandleLatencyMarkerSettingChanged()
{
	// Make sure that we only ever get this callback on platforms which support latency markers
	ensure(ULyraSettingsLocal::DoesPlatformSupportLatencyMarkers());

	const ULyraSettingsLocal* Settings = ULyraSettingsLocal::Get();
	if (!Settings)
	{
		return;
	}

	// Enable or disable the latency flash on all the marker modules according to the settings change
	bShouldTriggerLatencyFlash = Settings->GetEnableLatencyFlashIndicators();

	TArray<ILatencyMarkerModule*> LatencyMarkerModules = IModularFeatures::Get().GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());
	for (ILatencyMarkerModule* LatencyMarkerModule : LatencyMarkerModules)
	{
		LatencyMarkerModule->SetFlashIndicatorEnabled(bShouldTriggerLatencyFlash);
	}
```


### AimAssist 候选 Overlap

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Input/AimAssistTargetManagerComponent.cpp

```cpp
void UAimAssistTargetManagerComponent::GetVisibleTargets(const FAimAssistFilter& Filter, const FAimAssistSettings& Settings, const FAimAssistOwnerViewData& OwnerData, const TArray<FLyraAimAssistTarget>& OldTargets, OUT TArray<FLyraAimAssistTarget>& OutNewTargets)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UAimAssistTargetManagerComponent::GetVisibleTargets);
	OutNewTargets.Reset();
	const APlayerController* PC = OwnerData.PlayerController;

	if (!PC)
	{
		UE_LOG(LogAimAssist, Error, TEXT("Invalid player controller passed to GetVisibleTargets!"));
		return;
	}

	const APawn* OwnerPawn = PC->GetPawn();

	if (!OwnerPawn)
	{
		UE_LOG(LogAimAssist, Error, TEXT("Could not find a valid pawn for aim assist!"));
		return;
	}

	const FVector ViewLocation = OwnerData.ViewTransform.GetTranslation();
	const FVector ViewForward = OwnerData.ViewTransform.GetUnitAxis(EAxis::X);

	const float FOVScale = GetFOVScale(PC, ECommonInputType::Gamepad);
	const float InvFieldOfViewScale = (FOVScale > 0.0f) ? (1.0f / FOVScale) : 1.0f;
	const float TargetRange = (Settings.TargetRange.GetValue() * InvFieldOfViewScale);

	// Use the field of view to scale the reticle projection.  This maintains the same reticle size regardless of field of view.
	const float ReticleDepth = (Settings.ReticleDepth * InvFieldOfViewScale);

	// Calculate the bounds of this reticle in screen space
	const FBox2D AssistInnerReticleBounds = OwnerData.ProjectReticleToScreen(Settings.AssistInnerReticleWidth.GetValue(), Settings.AssistInnerReticleHeight.GetValue(), ReticleDepth);
	const FBox2D AssistOuterReticleBounds = OwnerData.ProjectReticleToScreen(Settings.AssistOuterReticleWidth.GetValue(), Settings.AssistOuterReticleHeight.GetValue(), ReticleDepth);
	const FBox2D TargetingReticleBounds = OwnerData.ProjectReticleToScreen(Settings.TargetingReticleWidth.GetValue(), Settings.TargetingReticleHeight.GetValue(), ReticleDepth);

	static TArray<FOverlapResult> OverlapResults;
	// Do a world trace on the Aim Assist channel to get any visible targets
	{
		UWorld* World = GetWorld();

		OverlapResults.Reset();

		const FVector PawnLocation = OwnerPawn->GetActorLocation();
		ECollisionChannel AimAssistChannel = GetAimAssistChannel();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AimAssist_QueryTargetsInRange), true);
		Params.AddIgnoredActor(OwnerPawn);

		// Need to multiply these by 0.5 because MakeBox takes in half extents
		FCollisionShape BoxShape = FCollisionShape::MakeBox(FVector3f(ReticleDepth * 0.5f, Settings.AssistOuterReticleWidth.GetValue() * 0.5f, Settings.AssistOuterReticleHeight.GetValue() * 0.5f));
		World->OverlapMultiByChannel(OUT OverlapResults, PawnLocation, OwnerData.PlayerTransform.GetRotation(), AimAssistChannel, BoxShape, Params);

#if ENABLE_DRAW_DEBUG && !UE_BUILD_SHIPPING
		if(LyraConsoleVariables::bDrawDebugViewfinder)
		{
			DrawDebugBox(World, PawnLocation, BoxShape.GetBox(), OwnerData.PlayerTransform.GetRotation(), FColor::Red);
		}
#endif
	}

	// Gather target options from any visibile hit results that implement the IAimAssistTarget interface
	TArray<FAimAssistTargetOptions> NewTargetData;
	{
		for (const FOverlapResult& Overlap : OverlapResults)
		{
			TScriptInterface<IAimAssistTaget> TargetActor(Overlap.GetActor());
			if (TargetActor)
			{
				FAimAssistTargetOptions TargetData;
				TargetActor->GatherTargetOptions(TargetData);
				NewTargetData.Add(TargetData);
			}

			TScriptInterface<IAimAssistTaget> TargetComponent(Overlap.GetComponent());
			if (TargetComponent)
			{
				FAimAssistTargetOptions TargetData;
				TargetComponent->GatherTargetOptions(TargetData);
				NewTargetData.Add(TargetData);
			}
		}
	}
```


### AimAssist 投影、评分、截断

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Input/AimAssistTargetManagerComponent.cpp

```cpp
	// Gather targets that are in front of the player
	{
		const FVector PawnLocation = OwnerPawn->GetActorLocation();

		for (FAimAssistTargetOptions& AimAssistTarget : NewTargetData)
		{
			if (!DoesTargetPassFilter(OwnerData, Filter, AimAssistTarget, TargetRange))
			{
				continue;
			}

			AActor* OwningActor = AimAssistTarget.TargetShapeComponent->GetOwner();

			FTransform TargetTransform;
			FCollisionShape TargetShape;
			FVector TargetShapeOrigin;

			if (!GatherTargetInfo(OwningActor, AimAssistTarget.TargetShapeComponent.Get(), TargetTransform, TargetShape, TargetShapeOrigin))
			{
				continue;
			}

			const FVector TargetViewLocation = TargetTransform.TransformPositionNoScale(TargetShapeOrigin);
			const FVector TargetViewVector = (TargetViewLocation - ViewLocation);

			FVector TargetViewDirection;
			float TargetViewDistance;
			TargetViewVector.ToDirectionAndLength(TargetViewDirection, TargetViewDistance);
			const float TargetViewDot = FVector::DotProduct(TargetViewDirection, ViewForward);
			if (TargetViewDot <= 0.0f)
			{
				continue;
			}

			const FLyraAimAssistTarget* OldTarget = FindTarget(OldTargets, AimAssistTarget.TargetShapeComponent.Get());

			// Calculate the screen bounds for this target
			FBox2D TargetScreenBounds(ForceInitToZero);
			const bool bUpdateTargetProjections = true;
			if (bUpdateTargetProjections)
			{
				TargetScreenBounds = OwnerData.ProjectShapeToScreen(TargetShape, TargetShapeOrigin, TargetTransform);
			}
			else
			{
				// Target projections are not being updated so use the values from the previous frame if the target existed.
				if (OldTarget)
				{
					TargetScreenBounds = OldTarget->ScreenBounds;
				}
			}

			if (!TargetScreenBounds.bIsValid)
			{
				continue;
			}

			if (!TargetingReticleBounds.Intersect(TargetScreenBounds))
			{
				continue;
			}

			FLyraAimAssistTarget NewTarget;

			NewTarget.TargetShapeComponent = AimAssistTarget.TargetShapeComponent;
			NewTarget.Location = TargetTransform.GetTranslation();
			NewTarget.ScreenBounds = TargetScreenBounds;
			NewTarget.ViewDistance = TargetViewDistance;
			NewTarget.bUnderAssistInnerReticle = AssistInnerReticleBounds.Intersect(TargetScreenBounds);
			NewTarget.bUnderAssistOuterReticle = AssistOuterReticleBounds.Intersect(TargetScreenBounds);

			// Transfer target data from last frame.
			if (OldTarget)
			{
				NewTarget.DeltaMovement = (NewTarget.Location - OldTarget->Location);
				NewTarget.AssistTime = OldTarget->AssistTime;
				NewTarget.AssistWeight = OldTarget->AssistWeight;
				NewTarget.VisibilityTraceHandle = OldTarget->VisibilityTraceHandle;
			}

			// Calculate a score used for sorting based on previous weight, distance from target, and distance from reticle.
			const float AssistWeightScore = (NewTarget.AssistWeight * Settings.TargetScore_AssistWeight);
			const float ViewDotScore = ((TargetViewDot * Settings.TargetScore_ViewDot) - Settings.TargetScore_ViewDotOffset);
			const float ViewDistanceScore = ((1.0f - (TargetViewDistance / TargetRange)) * Settings.TargetScore_ViewDistance);

			NewTarget.SortScore = (AssistWeightScore + ViewDotScore + ViewDistanceScore);

			OutNewTargets.Add(NewTarget);
		}
	}

	// Sort the targets by their score so if there are too many so we can limit the amount of visibility traces performed.
	if (OutNewTargets.Num() > Settings.MaxNumberOfTargets)
	{
		OutNewTargets.Sort([](const FLyraAimAssistTarget& TargetA, const FLyraAimAssistTarget& TargetB)
		{
			return (TargetA.SortScore > TargetB.SortScore);
		});

		OutNewTargets.SetNum(Settings.MaxNumberOfTargets, EAllowShrinking::No);
	}

	// Do visibliity traces on the targets
	{
		for (FLyraAimAssistTarget& Target : OutNewTargets)
		{
			DetermineTargetVisibility(Target, Settings, Filter, OwnerData);
		}
	}
}
```


### AimAssist 过滤

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Input/AimAssistTargetManagerComponent.cpp

```cpp
bool UAimAssistTargetManagerComponent::DoesTargetPassFilter(const FAimAssistOwnerViewData& OwnerData, const FAimAssistFilter& Filter, const FAimAssistTargetOptions& Target, const float AcceptableRange) const
{
	const APawn* OwnerPawn = OwnerData.PlayerController ? OwnerData.PlayerController->GetPawn() : nullptr;

	if (!Target.bIsActive || !OwnerPawn || !Target.TargetShapeComponent.IsValid())
	{
		return false;
	}

	const AActor* TargetOwningActor = Target.TargetShapeComponent->GetOwner();
	check(TargetOwningActor);
	if (TargetOwningActor == OwnerPawn || TargetOwningActor == OwnerPawn->GetInstigator())
	{
		return false;
	}

	const FVector PawnLocation = OwnerPawn->GetActorLocation();

	// Do a distance check on the given actor
	const FVector TargetVector = TargetOwningActor->GetActorLocation() - PawnLocation;
	const float TargetViewDistanceCheck = FVector::DotProduct(OwnerData.ViewForward, TargetVector);

	if ((TargetViewDistanceCheck < 0.0f) || (TargetViewDistanceCheck > AcceptableRange))
	{
		return false;
	}

	if (const ACharacter* TargetCharacter = Cast<ACharacter>(TargetOwningActor))
	{
		// If the given target is on the same team as the owner, then exclude it from the search
		if (!Filter.bIncludeSameFriendlyTargets)
		{
			if (const ALyraPlayerState* PS = TargetCharacter->GetPlayerState<ALyraPlayerState>())
			{
				if (PS->GetTeamId() == OwnerData.TeamID)
				{
					return false;
				}
			}
		}

		// Exclude dead or dying characters
		if (Filter.bExcludeDeadOrDying)
		{
			if (const ULyraHealthComponent* HealthComponent = ULyraHealthComponent::FindHealthComponent(TargetCharacter))
			{
				if (HealthComponent->IsDeadOrDying())
				{
					return false;
				}
			}
		}
	}

	// If this target has any tags that the filter wants to exlclude, then ignore it
	if (Target.AssociatedTags.HasAny(Filter.ExclusionGameplayTags))
	{
		return false;
	}

	if (Filter.ExcludedClasses.Contains(TargetOwningActor->GetClass()))
	{
		return false;
	}

	return true;
```


### AimAssist 可见性与通道

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Input/AimAssistTargetManagerComponent.cpp

```cpp
void UAimAssistTargetManagerComponent::DetermineTargetVisibility(FLyraAimAssistTarget& Target, const FAimAssistSettings& Settings, const FAimAssistFilter& Filter, const FAimAssistOwnerViewData& OwnerData)
{
	UWorld* World = GetWorld();
	check(World);

	const AActor* Actor = Target.TargetShapeComponent->GetOwner();
	if (!Actor)
	{
		ensure(false);
		return;
	}

	FVector TargetEyeLocation;
	FRotator TargetEyeRotation;
	Actor->GetActorEyesViewPoint(TargetEyeLocation, TargetEyeRotation);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AimAssist_DetermineTargetVisibility), true);
	InitTargetSelectionCollisionParams(QueryParams, *Actor, Filter);
	QueryParams.AddIgnoredActor(Actor);

	const UShooterCoreRuntimeSettings* ShooterSettings = GetDefault<UShooterCoreRuntimeSettings>();
	const ECollisionChannel AimAssistChannel = ShooterSettings->GetAimAssistCollisionChannel();

	FCollisionResponseParams ResponseParams;
	ResponseParams.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	ResponseParams.CollisionResponse.SetResponse(AimAssistChannel, ECR_Ignore);

	if (Target.bIsVisible && Settings.bEnableAsyncVisibilityTrace)
	{
		// Query for previous asynchronous trace result.
		if (Target.VisibilityTraceHandle.IsValid())
		{
			FTraceDatum TraceDatum;
			if (World->QueryTraceData(Target.VisibilityTraceHandle, TraceDatum))
			{
				Target.bIsVisible = (FHitResult::GetFirstBlockingHit(TraceDatum.OutHits) == nullptr);
			}
			else
			{
				UE_LOG(LogAimAssist, Warning, TEXT("UAimAssistTargetManagerComponent::DetermineTargetVisibility() - Failed to find async visibility trace data!"));
				Target.bIsVisible = false;
			}

			// Invalidate the async trace handle.
			Target.VisibilityTraceHandle = FTraceHandle();
		}

		// Only start a new asynchronous trace for next frame if the target is still visible.
		if (Target.bIsVisible)
		{
			Target.VisibilityTraceHandle = World->AsyncLineTraceByChannel(EAsyncTraceType::Test, OwnerData.ViewTransform.GetTranslation(), TargetEyeLocation, ECC_Visibility, QueryParams, ResponseParams);
		}
	}
	else
	{
		Target.bIsVisible = !World->LineTraceTestByChannel(OwnerData.ViewTransform.GetTranslation(), TargetEyeLocation, ECC_Visibility, QueryParams, ResponseParams);

		// Invalidate the async trace handle.
		Target.VisibilityTraceHandle = FTraceHandle();
	}
}

void UAimAssistTargetManagerComponent::InitTargetSelectionCollisionParams(FCollisionQueryParams& OutParams, const AActor& RequestedBy, const FAimAssistFilter& Filter) const
{
	// Exclude Requester
	if (Filter.bExcludeRequester)
	{
		OutParams.AddIgnoredActor(&RequestedBy);
	}

	// Exclude attached to Requester
	if (Filter.bExcludeAllAttachedToRequester)
	{
		TArray<AActor*> ActorsAttachedToRequester;
		RequestedBy.GetAttachedActors(ActorsAttachedToRequester);

		OutParams.AddIgnoredActors(ActorsAttachedToRequester);
	}

	if (Filter.bExcludeInstigator)
	{
		OutParams.AddIgnoredActor(RequestedBy.GetInstigator());
	}

	// Exclude attached to Instigator
	if (Filter.bExcludeAllAttachedToInstigator && RequestedBy.GetInstigator())
	{
		TArray<AActor*> ActorsAttachedToInstigator;
		RequestedBy.GetInstigator()->GetAttachedActors(ActorsAttachedToInstigator);

		OutParams.AddIgnoredActors(ActorsAttachedToInstigator);
	}

	OutParams.bTraceComplex = Filter.bTraceComplexCollision;
}

ECollisionChannel UAimAssistTargetManagerComponent::GetAimAssistChannel() const
{
	const UShooterCoreRuntimeSettings* ShooterSettings = GetDefault<UShooterCoreRuntimeSettings>();
	const ECollisionChannel AimAssistChannel = ShooterSettings->GetAimAssistCollisionChannel();

	ensureMsgf(AimAssistChannel != ECollisionChannel::ECC_MAX, TEXT("The aim assist collision channel has not been set! Do this in the ShooterCoreRuntime plugin settings"));

	return AimAssistChannel;
```


DeadZone 根据设置选择 MoveStick 或 LookStick 阈值，分别处理 Axial 和 Radial；GamepadSensitivity 通过 ULyraAimSensitivityData 把枚举映射成标量；AimInversion 读取共享设置翻转最终 X/Y。Boolean Action 都有前置保护。

## 四、用户设置和可重映射配置

LyraInputUserSettings::ApplySettings 先调用父类，再保留项目设置应用钩子；LyraPlayerMappableKeyProfile::EquipProfile 和 UnEquipProfile 先调用父类，再保留配置档切换扩展。当前源码没有虚构额外保存逻辑，持久化仍由 Enhanced Input 用户设置层负责。

设置变更不改 InputAction 资产，而是在下一次 ModifyRaw 动态读取 SharedSettings；这要求 LocalPlayer 的共享设置生命周期先于 PlayerInput。

## 五、Latency Marker

ULyraPlayerInput 构造函数跳过 CDO/Archetype，实例对象才绑定 LatencyFlash 设置委托。InputKey 先调用父类，再处理左鼠标按下；开启后枚举 ILatencyMarkerModule，以 GFrameCounter 触发 marker 7。析构 RemoveAll，避免静态监听残留。



## 六、辅助瞄准目标发现

UAimAssistTargetManagerComponent::GetVisibleTargets 根据 FOV 缩放 ReticleDepth，投影内/外/锁定准星的 FBox2D，再通过 ShooterCore AimAssistCollisionChannel 做 OverlapMultiByChannel。OwnerPawn 被加入忽略列表，结果还必须实现 IAimAssistTarget 接口。



DoesTargetPassFilter 依次检查 active、OwnerPawn/self-instigator、前方距离、同队、DeadOrDying、排除 GameplayTags 和 ExcludedClasses。目标必须与 TargetingReticleBounds 相交，随后按历史权重、ViewDot、距离计算 SortScore，超过 MaxNumberOfTargets 时先排序截断，再做可见性 Trace。

## 七、输入修正的职责分离

TargetManager 负责目标候选、可见性和评分；AimAssistInputModifier 负责 DeltaMovement、RotationToTarget、Pull/Slow 作用到玩家输入。目标查询与输入修正可以独立替换。

GetFOVScale 对手柄/触摸使用基准 FOV 的 tan 半角比例，对鼠标沿 DefaultInputSettings 的 FOVScale 路径。旋转计算先变换到玩家空间，再求 Pitch/Yaw，不能直接用世界坐标相减。

辅助瞄准复用 ALyraPlayerState::GetTeamId 和 ULyraHealthComponent::IsDeadOrDying；它只改变本地输入表现，伤害权威仍在 GAS/服务器。

## 八、验证入口

项目文件：Source/LyraGame/Input/LyraInputModifiers.h/cpp、LyraInputUserSettings.h/cpp、LyraPlayerMappableKeyProfile.h/cpp、LyraAimSensitivityData.h/cpp、LyraPlayerInput.h/cpp，以及 ShooterCore Input 目录。

验证符号：ModifyRaw_Implementation、PropertyCache、GetGamepadLookSensitivityPreset、InputKey、SetCustomLatencyMarker、GetVisibleTargets、DoesTargetPassFilter、OverlapMultiByChannel、DetermineTargetVisibility。

建议命令：

    $Lyra = 'C:\Users\zhaozhiqi\Documents\Unreal Projects\LyraStarterGame'
    Test-Path "$Lyra\Source\LyraGame\Input\LyraInputModifiers.cpp"
    Test-Path "$Lyra\Plugins\GameFeatures\ShooterCore\Source\ShooterCoreRuntime\Private\Input\AimAssistTargetManagerComponent.cpp"
    rg -n 'ModifyRaw_Implementation|PropertyCache|GetInvertVerticalAxis' "$Lyra\Source\LyraGame\Input"
    rg -n 'InputKey|LatencyMarker|SetFlashIndicatorEnabled' "$Lyra\Source\LyraGame\Input\LyraPlayerInput.cpp"
    rg -n 'GetVisibleTargets|DoesTargetPassFilter|DetermineTargetVisibility' "$Lyra\Plugins\GameFeatures\ShooterCore\Source\ShooterCoreRuntime\Private\Input"

## 九、失败路径和常见误用

| 误用 | 源码事实 |
| --- | --- |
| 设置界面直接修改 InputAction | 修正器在 ModifyRaw 动态读 SharedSettings |
| 对 Boolean Action 乘灵敏度 | Scalar 明确拒绝 Boolean |
| 把 Overlap 结果当最终目标 | 还要过接口、队伍、生命、Tag、投影和可见性 |
| 用客户端队伍表决定伤害 | 辅助瞄准不改变服务器结算 |
| 每帧对所有目标做 Trace | 先排序、按 MaxNumberOfTargets 截断 |
| CDO 绑定设置委托 | PlayerInput 显式跳过 CDO/Archetype |

## 十、关联阅读

- 25-EnhancedInput与GameplayTags源码：引擎 Enhanced Input Trigger/Modifier。
- 42-Lyra-输入GAS与武器战斗源码：InputConfig、InputTag 和 GAS 输入。
- 53-Lyra核心生成移动与状态源码：Pawn、Team、MovementStopped。
- 54-Lyra网络复制与引擎模块化特性源码：项目网络和输入配置边界。
- 56-Lyra-ShooterCore核心玩法与淘汰消息源码：淘汰、助攻和表现。

## 十一、覆盖声明

本文覆盖 Lyra 项目输入层和 ShooterCore 辅助瞄准的真实函数；Enhanced Input 引擎内部由 25 篇负责，资产中的 Mapping Context、CurveFloat 和 ShooterCoreSettings 参数需要 Editor/PIE 复核。


## 附录：核心文件完整源码

以下文件来自本机 LyraStarterGame 5.8，逐文件收录；代码围栏内只统一行尾和缩进空白，未删改源码内容、注释或条件编译。

### 附录文件 1：Source/LyraGame/Input/LyraInputModifiers.h

来源：Source/LyraGame/Input/LyraInputModifiers.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InputModifiers.h"

#include "UObject/UnrealType.h"
#include "LyraInputModifiers.generated.h"

struct FInputActionValue;

class FProperty;
class UEnhancedPlayerInput;
class ULyraAimSensitivityData;
class UObject;

/**
*  Scales input basedon a double property in the SharedUserSettings
*/
UCLASS(NotBlueprintable, MinimalAPI, meta = (DisplayName = "Setting Based Scalar"))
class ULyraSettingBasedScalar : public UInputModifier
{
	GENERATED_BODY()

public:

	/** Name of the property that will be used to clamp the X Axis of this value */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category=Settings)
	FName XAxisScalarSettingName = NAME_None;

	/** Name of the property that will be used to clamp the Y Axis of this value */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category=Settings)
	FName YAxisScalarSettingName = NAME_None;

	/** Name of the property that will be used to clamp the Z Axis of this value */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category=Settings)
	FName ZAxisScalarSettingName = NAME_None;

	/** Set the maximium value of this setting on each axis. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category=Settings)
	FVector MaxValueClamp = FVector(10.0, 10.0, 10.0);

	/** Set the minimum value of this setting on each axis. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category=Settings)
	FVector MinValueClamp = FVector::ZeroVector;

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime) override;

	/** FProperty Cache that will be populated with any found FProperty's on the settings class so that we don't need to look them up each frame */
	TArray<const FProperty*> PropertyCache;
};

/** Represents which stick that this deadzone is for, either the move or the look stick */
UENUM()
enum class EDeadzoneStick : uint8
{
	/** Deadzone for the movement stick */
	MoveStick = 0,

	/** Deadzone for the looking stick */
	LookStick = 1,
};

/**
 * This is a deadzone input modifier that will have it's thresholds driven by what is in the Lyra Shared game settings.
 */
UCLASS(NotBlueprintable, MinimalAPI, meta = (DisplayName = "Lyra Settings Driven Dead Zone"))
class ULyraInputModifierDeadZone : public UInputModifier
{
	GENERATED_BODY()

public:

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category=Settings, Config)
	EDeadZoneType Type = EDeadZoneType::Radial;

	// Threshold above which input is clamped to 1
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category=Settings, Config)
	float UpperThreshold = 1.0f;

	/** Which stick this deadzone is for. This controls which setting will be used when calculating the deadzone */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category=Settings, Config)
	EDeadzoneStick DeadzoneStick = EDeadzoneStick::MoveStick;

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime) override;

	// Visualize as black when unmodified. Red when blocked (with differing intensities to indicate axes)
	// Mirrors visualization in https://www.gamasutra.com/blogs/JoshSutphin/20130416/190541/Doing_Thumbstick_Dead_Zones_Right.php.
	virtual FLinearColor GetVisualizationColor_Implementation(FInputActionValue SampleValue, FInputActionValue FinalValue) const override;
};

/** The type of targeting sensitity that should be considered */
UENUM()
enum class ELyraTargetingType : uint8
{
	/** Sensitivity to be applied why normally looking around */
	Normal = 0,

	/** The sensitivity that should be applied while Aiming Down Sights */
	ADS = 1,
};

/** Applies a scalar modifier based on the current gamepad settings in Lyra Shared game settings.  */
UCLASS(NotBlueprintable, MinimalAPI, meta = (DisplayName = "Lyra Gamepad Sensitivity"))
class ULyraInputModifierGamepadSensitivity : public UInputModifier
{
	GENERATED_BODY()
public:

	/** The type of targeting to use for this Sensitivity */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category=Settings, Config)
	ELyraTargetingType TargetingType = ELyraTargetingType::Normal;

	/** Asset that gives us access to the float scalar value being used for sensitivty */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(AssetBundles="Client,Server"))
	TObjectPtr<const ULyraAimSensitivityData> SensitivityLevelTable;

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime) override;
};

/** Applies an inversion of axis values based on a setting in the Lyra Shared game settings */
UCLASS(NotBlueprintable, MinimalAPI, meta = (DisplayName = "Lyra Aim Inversion Setting"))
class ULyraInputModifierAimInversion : public UInputModifier
{
	GENERATED_BODY()

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime) override;
};

```

### 附录文件 2：Source/LyraGame/Input/LyraInputModifiers.cpp

来源：Source/LyraGame/Input/LyraInputModifiers.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraInputModifiers.h"

#include "EnhancedPlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "Input/LyraAimSensitivityData.h"
#include "Player/LyraLocalPlayer.h"
#include "Settings/LyraSettingsShared.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraInputModifiers)

DEFINE_LOG_CATEGORY_STATIC(LogLyraInputModifiers, Log, All);

//////////////////////////////////////////////////////////////////////
// LyraInputModifiersHelpers

namespace LyraInputModifiersHelpers
{
	/** Returns the owning LyraLocalPlayer of an Enhanced Player Input pointer */
	static ULyraLocalPlayer* GetLocalPlayer(const UEnhancedPlayerInput* PlayerInput)
	{
		if (PlayerInput)
		{
			if (APlayerController* PC = Cast<APlayerController>(PlayerInput->GetOuter()))
			{
				return Cast<ULyraLocalPlayer>(PC->GetLocalPlayer());
			}
		}
		return nullptr;
	}

}

//////////////////////////////////////////////////////////////////////
// ULyraSettingBasedScalar

FInputActionValue ULyraSettingBasedScalar::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime)
{
	if (ensureMsgf(CurrentValue.GetValueType() != EInputActionValueType::Boolean, TEXT("Setting Based Scalar modifier doesn't support boolean values.")))
	{
		if (ULyraLocalPlayer* LocalPlayer = LyraInputModifiersHelpers::GetLocalPlayer(PlayerInput))
		{
			const UClass* SettingsClass = ULyraSettingsShared::StaticClass();
			ULyraSettingsShared* SharedSettings = LocalPlayer->GetSharedSettings();

			const bool bHasCachedProperty = PropertyCache.Num() == 3;

			const FProperty* XAxisValue = bHasCachedProperty ? PropertyCache[0] : SettingsClass->FindPropertyByName(XAxisScalarSettingName);
			const FProperty* YAxisValue = bHasCachedProperty ? PropertyCache[1] : SettingsClass->FindPropertyByName(YAxisScalarSettingName);
			const FProperty* ZAxisValue = bHasCachedProperty ? PropertyCache[2] : SettingsClass->FindPropertyByName(ZAxisScalarSettingName);

			if (PropertyCache.IsEmpty())
			{
				PropertyCache.Emplace(XAxisValue);
				PropertyCache.Emplace(YAxisValue);
				PropertyCache.Emplace(ZAxisValue);
			}

			FVector ScalarToUse = FVector(1.0, 1.0, 1.0);

			switch (CurrentValue.GetValueType())
			{
			case EInputActionValueType::Axis3D:
				ScalarToUse.Z = ZAxisValue ? *ZAxisValue->ContainerPtrToValuePtr<double>(SharedSettings) : 1.0;
				//[[fallthrough]];
			case EInputActionValueType::Axis2D:
				ScalarToUse.Y = YAxisValue ? *YAxisValue->ContainerPtrToValuePtr<double>(SharedSettings) : 1.0;
				//[[fallthrough]];
			case EInputActionValueType::Axis1D:
				ScalarToUse.X = XAxisValue ? *XAxisValue->ContainerPtrToValuePtr<double>(SharedSettings) : 1.0;
				break;
			}

			ScalarToUse.X = FMath::Clamp(ScalarToUse.X, MinValueClamp.X, MaxValueClamp.X);
			ScalarToUse.Y = FMath::Clamp(ScalarToUse.Y, MinValueClamp.Y, MaxValueClamp.Y);
			ScalarToUse.Z = FMath::Clamp(ScalarToUse.Z, MinValueClamp.Z, MaxValueClamp.Z);

			return CurrentValue.Get<FVector>() * ScalarToUse;
		}
	}

	return CurrentValue;
}

//////////////////////////////////////////////////////////////////////
// ULyraInputModifierDeadZone

FInputActionValue ULyraInputModifierDeadZone::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime)
{
	EInputActionValueType ValueType = CurrentValue.GetValueType();
	ULyraLocalPlayer* LocalPlayer = LyraInputModifiersHelpers::GetLocalPlayer(PlayerInput);
	if (ValueType == EInputActionValueType::Boolean || !LocalPlayer)
	{
		return CurrentValue;
	}

	ULyraSettingsShared* Settings = LocalPlayer->GetSharedSettings();
	ensure(Settings);

	float LowerThreshold =
		(DeadzoneStick == EDeadzoneStick::MoveStick) ?
		Settings->GetGamepadMoveStickDeadZone() :
		Settings->GetGamepadLookStickDeadZone();

	LowerThreshold = FMath::Clamp(LowerThreshold, 0.0f, 1.0f);

	auto DeadZoneLambda = [LowerThreshold, this](const float AxisVal)
	{
		// We need to translate and scale the input to the +/- 1 range after removing the dead zone.
		return FMath::Min(1.f, (FMath::Max(0.f, FMath::Abs(AxisVal) - LowerThreshold) / (UpperThreshold - LowerThreshold))) * FMath::Sign(AxisVal);
	};

	FVector NewValue = CurrentValue.Get<FVector>();
	switch (Type)
	{
	case EDeadZoneType::Axial:
		NewValue.X = DeadZoneLambda(NewValue.X);
		NewValue.Y = DeadZoneLambda(NewValue.Y);
		NewValue.Z = DeadZoneLambda(NewValue.Z);
		break;
	case EDeadZoneType::Radial:
		if (ValueType == EInputActionValueType::Axis3D)
		{
			NewValue = NewValue.GetSafeNormal() * DeadZoneLambda(NewValue.Size());
		}
		else if (ValueType == EInputActionValueType::Axis2D)
		{
			NewValue = NewValue.GetSafeNormal2D() * DeadZoneLambda(NewValue.Size2D());
		}
		else
		{
			NewValue.X = DeadZoneLambda(NewValue.X);
		}
		break;
	}

	return NewValue;
}

FLinearColor ULyraInputModifierDeadZone::GetVisualizationColor_Implementation(FInputActionValue SampleValue, FInputActionValue FinalValue) const
{
	// Taken from UInputModifierDeadZone::GetVisualizationColor_Implementation
	if (FinalValue.GetValueType() == EInputActionValueType::Boolean || FinalValue.GetValueType() == EInputActionValueType::Axis1D)
	{
		return FLinearColor(FinalValue.Get<float>() == 0.f ? 1.f : 0.f, 0.f, 0.f);
	}
	return FLinearColor((FinalValue.Get<FVector2D>().X == 0.f ? 0.5f : 0.f) + (FinalValue.Get<FVector2D>().Y == 0.f ? 0.5f : 0.f), 0.f, 0.f);
}

//////////////////////////////////////////////////////////////////////
// ULyraInputModifierGamepadSensitivity

FInputActionValue ULyraInputModifierGamepadSensitivity::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime)
{
	// You can't scale a boolean action type
	ULyraLocalPlayer* LocalPlayer = LyraInputModifiersHelpers::GetLocalPlayer(PlayerInput);
	if (CurrentValue.GetValueType() == EInputActionValueType::Boolean || !LocalPlayer || !SensitivityLevelTable)
	{
		return CurrentValue;
	}

	ULyraSettingsShared* Settings = LocalPlayer->GetSharedSettings();
	ensure(Settings);

	const ELyraGamepadSensitivity Sensitivity = (TargetingType == ELyraTargetingType::Normal) ? Settings->GetGamepadLookSensitivityPreset() : Settings->GetGamepadTargetingSensitivityPreset();

	const float Scalar = SensitivityLevelTable->SensitivtyEnumToFloat(Sensitivity);

	return CurrentValue.Get<FVector>() * Scalar;
}

//////////////////////////////////////////////////////////////////////
// ULyraInputModifierAimInversion

FInputActionValue ULyraInputModifierAimInversion::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime)
{
	ULyraLocalPlayer* LocalPlayer = LyraInputModifiersHelpers::GetLocalPlayer(PlayerInput);
	if (!LocalPlayer)
	{
		return CurrentValue;
	}

	ULyraSettingsShared* Settings = LocalPlayer->GetSharedSettings();
	ensure(Settings);

	FVector NewValue = CurrentValue.Get<FVector>();

	if (Settings->GetInvertVerticalAxis())
	{
		NewValue.Y *= -1.0f;
	}

	if (Settings->GetInvertHorizontalAxis())
	{
		NewValue.X *= -1.0f;
	}

	return NewValue;
}

```

### 附录文件 3：Source/LyraGame/Input/LyraInputUserSettings.h

来源：Source/LyraGame/Input/LyraInputUserSettings.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UserSettings/EnhancedInputUserSettings.h"
#include "PlayerMappableKeySettings.h"

#include "LyraInputUserSettings.generated.h"

#define UE_API LYRAGAME_API

/**
 * Custom settings class for any input related settings for the Lyra game.
 * This will be serialized out at the same time as the Lyra Shared Settings and is
 * compatible with cloud saves through by calling the "Serialize" function.
 */
UCLASS(MinimalAPI)
class ULyraInputUserSettings : public UEnhancedInputUserSettings
{
	GENERATED_BODY()
public:
	//~ Begin UEnhancedInputUserSettings interface
	UE_API virtual void ApplySettings() override;
	//~ End UEnhancedInputUserSettings interface

	// Add any additional Input Settings here!
	// Some ideas could be:
	// - "toggle vs. hold" to trigger in game actions
	// - aim sensitivity should go here
	// - etc

	// Make sure to mark your properties with the "SaveGame" metadata to have them serialize when saved
	//UPROPERTY(SaveGame, BlueprintReadWrite, Category="Enhanced Input|User Settings")
	// bool bSomeExampleProperty;
};

/**
 * Player Mappable Key settings are settings that are accessible per-action key mapping.
 * This is where you could place additional metadata that may be used by your settings UI,
 * input triggers, or other places where you want to know about a key setting.
 */
UCLASS(MinimalAPI)
class ULyraPlayerMappableKeySettings : public UPlayerMappableKeySettings
{
	GENERATED_BODY()

public:

	/** Returns the tooltip that should be displayed on the settings screen for this key */
	UE_API const FText& GetTooltipText() const;

protected:
	/** The tooltip that should be associated with this action when displayed on the settings screen */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta=(AllowPrivateAccess=true))
	FText Tooltip = FText::GetEmpty();
};

#undef UE_API

```

### 附录文件 4：Source/LyraGame/Input/LyraInputUserSettings.cpp

来源：Source/LyraGame/Input/LyraInputUserSettings.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/LyraInputUserSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraInputUserSettings)

void ULyraInputUserSettings::ApplySettings()
{
	Super::ApplySettings();

	// Add any functionality you want to happen when the input settings are applied to the user
	// This is a good place to put a breakpoint in your debugger to see the flow of
	// how input settings are used :)
}

```

### 附录文件 5：Source/LyraGame/Input/LyraPlayerMappableKeyProfile.h

来源：Source/LyraGame/Input/LyraPlayerMappableKeyProfile.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UserSettings/EnhancedInputUserSettings.h"

#include "LyraPlayerMappableKeyProfile.generated.h"

#define UE_API LYRAGAME_API

UCLASS(MinimalAPI)
class ULyraPlayerMappableKeyProfile : public UEnhancedPlayerMappableKeyProfile
{
	GENERATED_BODY()

protected:

	//~ Begin UEnhancedPlayerMappableKeyProfile interface
	UE_API virtual void EquipProfile() override;
	UE_API virtual void UnEquipProfile() override;
	//~ End UEnhancedPlayerMappableKeyProfile interface
};

#undef UE_API

```

### 附录文件 6：Source/LyraGame/Input/LyraPlayerMappableKeyProfile.cpp

来源：Source/LyraGame/Input/LyraPlayerMappableKeyProfile.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/LyraPlayerMappableKeyProfile.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraPlayerMappableKeyProfile)

void ULyraPlayerMappableKeyProfile::EquipProfile()
{
	Super::EquipProfile();

	// Do anything you may want to when a new key profile is equipped
}

void ULyraPlayerMappableKeyProfile::UnEquipProfile()
{
	Super::UnEquipProfile();

	// Do anything you may want to when a new key profile is unequipped
}

```

### 附录文件 7：Source/LyraGame/Input/LyraAimSensitivityData.h

来源：Source/LyraGame/Input/LyraAimSensitivityData.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"

#include "LyraAimSensitivityData.generated.h"

#define UE_API LYRAGAME_API

enum class ELyraGamepadSensitivity : uint8;

class UObject;

/** Defines a set of gamepad sensitivity to a float value. */
UCLASS(MinimalAPI, BlueprintType, Const, Meta = (DisplayName = "Lyra Aim Sensitivity Data", ShortTooltip = "Data asset used to define a map of Gamepad Sensitivty to a float value."))
class ULyraAimSensitivityData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UE_API ULyraAimSensitivityData(const FObjectInitializer& ObjectInitializer);

	UE_API const float SensitivtyEnumToFloat(const ELyraGamepadSensitivity InSensitivity) const;

protected:
	/** Map of SensitivityMap settings to their corresponding float */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<ELyraGamepadSensitivity, float> SensitivityMap;
};

#undef UE_API

```

### 附录文件 8：Source/LyraGame/Input/LyraAimSensitivityData.cpp

来源：Source/LyraGame/Input/LyraAimSensitivityData.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraAimSensitivityData.h"

#include "Settings/LyraSettingsShared.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraAimSensitivityData)

ULyraAimSensitivityData::ULyraAimSensitivityData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SensitivityMap =
	{
		{ ELyraGamepadSensitivity::Slow,			0.5f },
		{ ELyraGamepadSensitivity::SlowPlus,		0.75f },
		{ ELyraGamepadSensitivity::SlowPlusPlus,	0.9f },
		{ ELyraGamepadSensitivity::Normal,		1.0f },
		{ ELyraGamepadSensitivity::NormalPlus,	1.1f },
		{ ELyraGamepadSensitivity::NormalPlusPlus,1.25f },
		{ ELyraGamepadSensitivity::Fast,			1.5f },
		{ ELyraGamepadSensitivity::FastPlus,		1.75f },
		{ ELyraGamepadSensitivity::FastPlusPlus,	2.0f },
		{ ELyraGamepadSensitivity::Insane,		2.5f },
	};
}

const float ULyraAimSensitivityData::SensitivtyEnumToFloat(const ELyraGamepadSensitivity InSensitivity) const
{
	if (const float* Sens = SensitivityMap.Find(InSensitivity))
	{
		return *Sens;
	}

	return 1.0f;
}


```

### 附录文件 9：Source/LyraGame/Input/LyraPlayerInput.h

来源：Source/LyraGame/Input/LyraPlayerInput.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EnhancedPlayerInput.h"

#include "LyraPlayerInput.generated.h"

/**
 * Custom player input class for Lyra. This extends the functionality of Enhanced Input to also include
 * some input latency tracking on key press.
 *
 * Extend this class if you have any special logic which you may want to run relating to when keys are pressed
 * or when input is flushed.
 */
UCLASS(config = Input, transient)
class ULyraPlayerInput : public UEnhancedPlayerInput
{
	GENERATED_BODY()

public:
	ULyraPlayerInput();
	virtual ~ULyraPlayerInput() override;

protected:
	//~ Begin UEnhancedPlayerInput Interface
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	//~ End of UEnhancedPlayerInput interface

	void ProcessInputEventForLatencyMarker(const FInputKeyEventArgs& Params);
	void BindToLatencyMarkerSettingChange();
	void UnbindLatencyMarkerSettingChangeListener();
	void HandleLatencyMarkerSettingChanged();

	bool bShouldTriggerLatencyFlash = false;
};
```

### 附录文件 10：Source/LyraGame/Input/LyraPlayerInput.cpp

来源：Source/LyraGame/Input/LyraPlayerInput.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraPlayerInput.h"
#include "Performance/LatencyMarkerModule.h"
#include "Settings/LyraSettingsLocal.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraPlayerInput)

ULyraPlayerInput::ULyraPlayerInput()
	: Super()
{
	// Don't bind to any settings delegates on the CDO, otherwise there would be a constant bound listener
	// and it wouldn't even do anything because it doesn't get ticked/process input
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return;
	}

	BindToLatencyMarkerSettingChange();
}

ULyraPlayerInput::~ULyraPlayerInput()
{
	UnbindLatencyMarkerSettingChangeListener();
}

bool ULyraPlayerInput::InputKey(const FInputKeyEventArgs& Params)
{
	const bool bResult = Super::InputKey(Params);

	// Note: Since Lyra is only going to support the "Reflex" plugin to handle latency markers,
	// we could #if PLATFORM_DESKTOP this away to save on other platforms. However, for the sake
	// of extensibility for this same project we will not do that.
	ProcessInputEventForLatencyMarker(Params);

	return bResult;
}

void ULyraPlayerInput::ProcessInputEventForLatencyMarker(const FInputKeyEventArgs& Params)
{
	if (!bShouldTriggerLatencyFlash)
	{
		return;
	}

	// Flash the latency marker on left mouse down
	if (Params.Key == EKeys::LeftMouseButton)
	{
		TArray<ILatencyMarkerModule*> LatencyMarkerModules = IModularFeatures::Get().GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());

		for (ILatencyMarkerModule* LatencyMarkerModule : LatencyMarkerModules)
		{
			// TRIGGER_FLASH is 7
			LatencyMarkerModule->SetCustomLatencyMarker(7, GFrameCounter);
		}
	}
}

void ULyraPlayerInput::BindToLatencyMarkerSettingChange()
{
	if (!ULyraSettingsLocal::DoesPlatformSupportLatencyMarkers())
	{
		return;
	}

	ULyraSettingsLocal* Settings = ULyraSettingsLocal::Get();
	if (!Settings)
	{
		return;
	}

	Settings->OnLatencyFlashInidicatorSettingsChangedEvent().AddUObject(this, &ThisClass::HandleLatencyMarkerSettingChanged);

	// Initalize the settings and make sure that the input latency modules are enabled
	HandleLatencyMarkerSettingChanged();
}

void ULyraPlayerInput::UnbindLatencyMarkerSettingChangeListener()
{
	ULyraSettingsLocal* Settings = ULyraSettingsLocal::Get();
	if (!Settings)
	{
		return;
	}

	Settings->OnLatencyFlashInidicatorSettingsChangedEvent().RemoveAll(this);
}

void ULyraPlayerInput::HandleLatencyMarkerSettingChanged()
{
	// Make sure that we only ever get this callback on platforms which support latency markers
	ensure(ULyraSettingsLocal::DoesPlatformSupportLatencyMarkers());

	const ULyraSettingsLocal* Settings = ULyraSettingsLocal::Get();
	if (!Settings)
	{
		return;
	}

	// Enable or disable the latency flash on all the marker modules according to the settings change
	bShouldTriggerLatencyFlash = Settings->GetEnableLatencyFlashIndicators();

	TArray<ILatencyMarkerModule*> LatencyMarkerModules = IModularFeatures::Get().GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());
	for (ILatencyMarkerModule* LatencyMarkerModule : LatencyMarkerModules)
	{
		LatencyMarkerModule->SetFlashIndicatorEnabled(bShouldTriggerLatencyFlash);
	}
}

```

### 附录文件 11：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/Input/IAimAssistTargetInterface.h

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/Input/IAimAssistTargetInterface.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/AimAssistInputModifier.h"
#include "UObject/Interface.h"
#include "IAimAssistTargetInterface.generated.h"

USTRUCT(BlueprintType)
struct FAimAssistTargetOptions
{
	GENERATED_BODY()

	FAimAssistTargetOptions()
		: bIsActive(true)
	{}

	/** The shape component that should be used when considering this target's hitbox */
	TWeakObjectPtr<UShapeComponent> TargetShapeComponent;

	/**
	 * Gameplay tags that are associated with this target that can be used to filter it out.
	 *
	 * If the player's aim assist settings have any tags that match these, it will be excluded.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FGameplayTagContainer AssociatedTags;

	/** Whether or not this target is currently active. If false, it will not be considered for aim assist */
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	uint8 bIsActive : 1;
};


UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UAimAssistTaget : public UInterface
{
	GENERATED_BODY()
};

/**
 * Used to define the shape of an aim assist target as well as let the aim assist manager know
 * about any associated gameplay tags.
 *
 * The target will be considered when it is within the view of a player's outer reticle
 *
 * @see UAimAssistTargetComponent for an example
 */
class IAimAssistTaget
{
	GENERATED_BODY()

public:
	/** Populate the given target data with this interface. This will be called when a target is within view of the player */
	virtual void GatherTargetOptions(OUT FAimAssistTargetOptions& TargetData) = 0;
};

```

### 附录文件 12：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/Input/AimAssistTargetComponent.h

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/Input/AimAssistTargetComponent.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/CapsuleComponent.h"
#include "GameplayTagContainer.h"
#include "IAimAssistTargetInterface.h"

#include "AimAssistTargetComponent.generated.h"

#define UE_API SHOOTERCORERUNTIME_API

class UObject;

/**
 * This component can be added to any actor to have it register with the Aim Assist Target Manager.
 */
UCLASS(MinimalAPI, BlueprintType, meta=(BlueprintSpawnableComponent))
class UAimAssistTargetComponent : public UCapsuleComponent, public IAimAssistTaget
{
	GENERATED_BODY()

public:

	//~ Begin IAimAssistTaget interface
	UE_API virtual void GatherTargetOptions(OUT FAimAssistTargetOptions& TargetData) override;
	//~ End IAimAssistTaget interface

protected:

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FAimAssistTargetOptions TargetData {};
};

#undef UE_API

```

### 附录文件 13：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Input/AimAssistTargetComponent.cpp

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Input/AimAssistTargetComponent.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/AimAssistTargetComponent.h"

#include "Input/IAimAssistTargetInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AimAssistTargetComponent)

void UAimAssistTargetComponent::GatherTargetOptions(FAimAssistTargetOptions& OutTargetData)
{
	if (!TargetData.TargetShapeComponent.IsValid())
	{
		if (AActor* Owner = GetOwner())
		{
			TargetData.TargetShapeComponent = Owner->FindComponentByClass<UShapeComponent>();
		}
	}
	OutTargetData = TargetData;
}


```

### 附录文件 14：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/Input/AimAssistTargetManagerComponent.h

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/Input/AimAssistTargetManagerComponent.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/GameStateComponent.h"

#include "AimAssistTargetManagerComponent.generated.h"

#define UE_API SHOOTERCORERUNTIME_API

enum class ECommonInputType : uint8;

class APlayerController;
class UObject;
struct FAimAssistFilter;
struct FAimAssistOwnerViewData;
struct FAimAssistSettings;
struct FAimAssistTargetOptions;
struct FCollisionQueryParams;
struct FLyraAimAssistTarget;

/**
 * The Aim Assist Target Manager Component is used to gather all aim assist targets that are within
 * a given player's view. Targets must implement the IAimAssistTargetInterface and be on the
 * collision channel that is set in the ShooterCoreRuntimeSettings.
 */
UCLASS(MinimalAPI, Blueprintable)
class UAimAssistTargetManagerComponent : public UGameStateComponent
{
	GENERATED_BODY()

public:

	/** Gets all visible active targets based on the given local player and their ViewTransform */
	UE_API void GetVisibleTargets(const FAimAssistFilter& Filter, const FAimAssistSettings& Settings, const FAimAssistOwnerViewData& OwnerData, const TArray<FLyraAimAssistTarget>& OldTargets, OUT TArray<FLyraAimAssistTarget>& OutNewTargets);

	/** Get a Player Controller's FOV scaled based on their current input type. */
	static UE_API float GetFOVScale(const APlayerController* PC, ECommonInputType InputType);

	/** Get the collision channel that should be used to find targets within the player's view. */
	UE_API ECollisionChannel GetAimAssistChannel() const;

protected:

	/**
	 * Returns true if the given target passes the filter based on the current player owner data.
	 * False if the given target should be excluded from aim assist calculations
	 */
	UE_API bool DoesTargetPassFilter(const FAimAssistOwnerViewData& OwnerData, const FAimAssistFilter& Filter, const FAimAssistTargetOptions& Target, const float AcceptableRange) const;

	/** Determine if the given target is visible based on our current view data. */
	UE_API void DetermineTargetVisibility(FLyraAimAssistTarget& Target, const FAimAssistSettings& Settings, const FAimAssistFilter& Filter, const FAimAssistOwnerViewData& OwnerData);

	/** Setup CollisionQueryParams to ignore a set of actors based on filter settings. Such as Ignoring Requester or Instigator. */
	UE_API void InitTargetSelectionCollisionParams(FCollisionQueryParams& OutParams, const AActor& RequestedBy, const FAimAssistFilter& Filter) const;
};

#undef UE_API

```

### 附录文件 15：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Input/AimAssistTargetManagerComponent.cpp

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Input/AimAssistTargetManagerComponent.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/AimAssistTargetManagerComponent.h"
#include "CommonInputTypeEnum.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/InputSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/LyraHealthComponent.h"
#include "Input/AimAssistInputModifier.h"
#include "Player/LyraPlayerState.h"
#include "Character/LyraHealthComponent.h"
#include "Input/IAimAssistTargetInterface.h"
#include "ShooterCoreRuntimeSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AimAssistTargetManagerComponent)

namespace LyraConsoleVariables
{
	static bool bDrawDebugViewfinder = false;
	static FAutoConsoleVariableRef CVarDrawDebugViewfinder(
		TEXT("lyra.Weapon.AimAssist.DrawDebugViewfinder"),
		bDrawDebugViewfinder,
		TEXT("Should we draw a debug box for the aim assist target viewfinder?"),
		ECVF_Cheat);
}

const FLyraAimAssistTarget* FindTarget(const TArray<FLyraAimAssistTarget>& Targets, const UShapeComponent* TargetComponent)
{
	const FLyraAimAssistTarget* FoundTarget = Targets.FindByPredicate(
	[&TargetComponent](const FLyraAimAssistTarget& Target)
	{
		return (Target.TargetShapeComponent == TargetComponent);
	});

	return FoundTarget;
}

static bool GatherTargetInfo(const AActor* Actor, const UShapeComponent* ShapeComponent, FTransform& OutTransform, FCollisionShape& OutShape, FVector& OutShapeOrigin)
{
	check(Actor);
	check(ShapeComponent);

	const FCollisionShape TargetShape = ShapeComponent->GetCollisionShape();
	const bool bIsValidShape = (TargetShape.IsBox() || TargetShape.IsSphere() || TargetShape.IsCapsule());

	if (!bIsValidShape || TargetShape.IsNearlyZero())
	{
		return false;
	}

	FTransform TargetTransform;
	FVector TargetShapeOrigin(ForceInitToZero);

	if (const ACharacter* TargetCharacter = Cast<ACharacter>(Actor))
	{
		if (ShapeComponent == TargetCharacter->GetCapsuleComponent())
		{
			// Character capsules don't move smoothly for remote players.  Use the mesh location since it's smoothed out.
			const USkeletalMeshComponent* TargetMesh = TargetCharacter->GetMesh();
			check(TargetMesh);

			TargetTransform = TargetMesh->GetComponentTransform();
			TargetShapeOrigin = -TargetCharacter->GetBaseTranslationOffset();
		}
		else
		{
			TargetTransform = ShapeComponent->GetComponentTransform();
		}
	}
	else
	{
		TargetTransform = ShapeComponent->GetComponentTransform();
	}

	OutTransform = TargetTransform;
	OutShape = TargetShape;
	OutShapeOrigin = TargetShapeOrigin;

	return true;
}


void UAimAssistTargetManagerComponent::GetVisibleTargets(const FAimAssistFilter& Filter, const FAimAssistSettings& Settings, const FAimAssistOwnerViewData& OwnerData, const TArray<FLyraAimAssistTarget>& OldTargets, OUT TArray<FLyraAimAssistTarget>& OutNewTargets)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UAimAssistTargetManagerComponent::GetVisibleTargets);
	OutNewTargets.Reset();
	const APlayerController* PC = OwnerData.PlayerController;

	if (!PC)
	{
		UE_LOG(LogAimAssist, Error, TEXT("Invalid player controller passed to GetVisibleTargets!"));
		return;
	}

	const APawn* OwnerPawn = PC->GetPawn();

	if (!OwnerPawn)
	{
		UE_LOG(LogAimAssist, Error, TEXT("Could not find a valid pawn for aim assist!"));
		return;
	}

	const FVector ViewLocation = OwnerData.ViewTransform.GetTranslation();
	const FVector ViewForward = OwnerData.ViewTransform.GetUnitAxis(EAxis::X);

	const float FOVScale = GetFOVScale(PC, ECommonInputType::Gamepad);
	const float InvFieldOfViewScale = (FOVScale > 0.0f) ? (1.0f / FOVScale) : 1.0f;
	const float TargetRange = (Settings.TargetRange.GetValue() * InvFieldOfViewScale);

	// Use the field of view to scale the reticle projection.  This maintains the same reticle size regardless of field of view.
	const float ReticleDepth = (Settings.ReticleDepth * InvFieldOfViewScale);

	// Calculate the bounds of this reticle in screen space
	const FBox2D AssistInnerReticleBounds = OwnerData.ProjectReticleToScreen(Settings.AssistInnerReticleWidth.GetValue(), Settings.AssistInnerReticleHeight.GetValue(), ReticleDepth);
	const FBox2D AssistOuterReticleBounds = OwnerData.ProjectReticleToScreen(Settings.AssistOuterReticleWidth.GetValue(), Settings.AssistOuterReticleHeight.GetValue(), ReticleDepth);
	const FBox2D TargetingReticleBounds = OwnerData.ProjectReticleToScreen(Settings.TargetingReticleWidth.GetValue(), Settings.TargetingReticleHeight.GetValue(), ReticleDepth);

	static TArray<FOverlapResult> OverlapResults;
	// Do a world trace on the Aim Assist channel to get any visible targets
	{
		UWorld* World = GetWorld();

		OverlapResults.Reset();

		const FVector PawnLocation = OwnerPawn->GetActorLocation();
		ECollisionChannel AimAssistChannel = GetAimAssistChannel();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AimAssist_QueryTargetsInRange), true);
		Params.AddIgnoredActor(OwnerPawn);

		// Need to multiply these by 0.5 because MakeBox takes in half extents
		FCollisionShape BoxShape = FCollisionShape::MakeBox(FVector3f(ReticleDepth * 0.5f, Settings.AssistOuterReticleWidth.GetValue() * 0.5f, Settings.AssistOuterReticleHeight.GetValue() * 0.5f));
		World->OverlapMultiByChannel(OUT OverlapResults, PawnLocation, OwnerData.PlayerTransform.GetRotation(), AimAssistChannel, BoxShape, Params);

#if ENABLE_DRAW_DEBUG && !UE_BUILD_SHIPPING
		if(LyraConsoleVariables::bDrawDebugViewfinder)
		{
			DrawDebugBox(World, PawnLocation, BoxShape.GetBox(), OwnerData.PlayerTransform.GetRotation(), FColor::Red);
		}
#endif
	}

	// Gather target options from any visibile hit results that implement the IAimAssistTarget interface
	TArray<FAimAssistTargetOptions> NewTargetData;
	{
		for (const FOverlapResult& Overlap : OverlapResults)
		{
			TScriptInterface<IAimAssistTaget> TargetActor(Overlap.GetActor());
			if (TargetActor)
			{
				FAimAssistTargetOptions TargetData;
				TargetActor->GatherTargetOptions(TargetData);
				NewTargetData.Add(TargetData);
			}

			TScriptInterface<IAimAssistTaget> TargetComponent(Overlap.GetComponent());
			if (TargetComponent)
			{
				FAimAssistTargetOptions TargetData;
				TargetComponent->GatherTargetOptions(TargetData);
				NewTargetData.Add(TargetData);
			}
		}
	}

	// Gather targets that are in front of the player
	{
		const FVector PawnLocation = OwnerPawn->GetActorLocation();

		for (FAimAssistTargetOptions& AimAssistTarget : NewTargetData)
		{
			if (!DoesTargetPassFilter(OwnerData, Filter, AimAssistTarget, TargetRange))
			{
				continue;
			}

			AActor* OwningActor = AimAssistTarget.TargetShapeComponent->GetOwner();

			FTransform TargetTransform;
			FCollisionShape TargetShape;
			FVector TargetShapeOrigin;

			if (!GatherTargetInfo(OwningActor, AimAssistTarget.TargetShapeComponent.Get(), TargetTransform, TargetShape, TargetShapeOrigin))
			{
				continue;
			}

			const FVector TargetViewLocation = TargetTransform.TransformPositionNoScale(TargetShapeOrigin);
			const FVector TargetViewVector = (TargetViewLocation - ViewLocation);

			FVector TargetViewDirection;
			float TargetViewDistance;
			TargetViewVector.ToDirectionAndLength(TargetViewDirection, TargetViewDistance);
			const float TargetViewDot = FVector::DotProduct(TargetViewDirection, ViewForward);
			if (TargetViewDot <= 0.0f)
			{
				continue;
			}

			const FLyraAimAssistTarget* OldTarget = FindTarget(OldTargets, AimAssistTarget.TargetShapeComponent.Get());

			// Calculate the screen bounds for this target
			FBox2D TargetScreenBounds(ForceInitToZero);
			const bool bUpdateTargetProjections = true;
			if (bUpdateTargetProjections)
			{
				TargetScreenBounds = OwnerData.ProjectShapeToScreen(TargetShape, TargetShapeOrigin, TargetTransform);
			}
			else
			{
				// Target projections are not being updated so use the values from the previous frame if the target existed.
				if (OldTarget)
				{
					TargetScreenBounds = OldTarget->ScreenBounds;
				}
			}

			if (!TargetScreenBounds.bIsValid)
			{
				continue;
			}

			if (!TargetingReticleBounds.Intersect(TargetScreenBounds))
			{
				continue;
			}

			FLyraAimAssistTarget NewTarget;

			NewTarget.TargetShapeComponent = AimAssistTarget.TargetShapeComponent;
			NewTarget.Location = TargetTransform.GetTranslation();
			NewTarget.ScreenBounds = TargetScreenBounds;
			NewTarget.ViewDistance = TargetViewDistance;
			NewTarget.bUnderAssistInnerReticle = AssistInnerReticleBounds.Intersect(TargetScreenBounds);
			NewTarget.bUnderAssistOuterReticle = AssistOuterReticleBounds.Intersect(TargetScreenBounds);

			// Transfer target data from last frame.
			if (OldTarget)
			{
				NewTarget.DeltaMovement = (NewTarget.Location - OldTarget->Location);
				NewTarget.AssistTime = OldTarget->AssistTime;
				NewTarget.AssistWeight = OldTarget->AssistWeight;
				NewTarget.VisibilityTraceHandle = OldTarget->VisibilityTraceHandle;
			}

			// Calculate a score used for sorting based on previous weight, distance from target, and distance from reticle.
			const float AssistWeightScore = (NewTarget.AssistWeight * Settings.TargetScore_AssistWeight);
			const float ViewDotScore = ((TargetViewDot * Settings.TargetScore_ViewDot) - Settings.TargetScore_ViewDotOffset);
			const float ViewDistanceScore = ((1.0f - (TargetViewDistance / TargetRange)) * Settings.TargetScore_ViewDistance);

			NewTarget.SortScore = (AssistWeightScore + ViewDotScore + ViewDistanceScore);

			OutNewTargets.Add(NewTarget);
		}
	}

	// Sort the targets by their score so if there are too many so we can limit the amount of visibility traces performed.
	if (OutNewTargets.Num() > Settings.MaxNumberOfTargets)
	{
		OutNewTargets.Sort([](const FLyraAimAssistTarget& TargetA, const FLyraAimAssistTarget& TargetB)
		{
			return (TargetA.SortScore > TargetB.SortScore);
		});

		OutNewTargets.SetNum(Settings.MaxNumberOfTargets, EAllowShrinking::No);
	}

	// Do visibliity traces on the targets
	{
		for (FLyraAimAssistTarget& Target : OutNewTargets)
		{
			DetermineTargetVisibility(Target, Settings, Filter, OwnerData);
		}
	}
}

bool UAimAssistTargetManagerComponent::DoesTargetPassFilter(const FAimAssistOwnerViewData& OwnerData, const FAimAssistFilter& Filter, const FAimAssistTargetOptions& Target, const float AcceptableRange) const
{
	const APawn* OwnerPawn = OwnerData.PlayerController ? OwnerData.PlayerController->GetPawn() : nullptr;

	if (!Target.bIsActive || !OwnerPawn || !Target.TargetShapeComponent.IsValid())
	{
		return false;
	}

	const AActor* TargetOwningActor = Target.TargetShapeComponent->GetOwner();
	check(TargetOwningActor);
	if (TargetOwningActor == OwnerPawn || TargetOwningActor == OwnerPawn->GetInstigator())
	{
		return false;
	}

	const FVector PawnLocation = OwnerPawn->GetActorLocation();

	// Do a distance check on the given actor
	const FVector TargetVector = TargetOwningActor->GetActorLocation() - PawnLocation;
	const float TargetViewDistanceCheck = FVector::DotProduct(OwnerData.ViewForward, TargetVector);

	if ((TargetViewDistanceCheck < 0.0f) || (TargetViewDistanceCheck > AcceptableRange))
	{
		return false;
	}

	if (const ACharacter* TargetCharacter = Cast<ACharacter>(TargetOwningActor))
	{
		// If the given target is on the same team as the owner, then exclude it from the search
		if (!Filter.bIncludeSameFriendlyTargets)
		{
			if (const ALyraPlayerState* PS = TargetCharacter->GetPlayerState<ALyraPlayerState>())
			{
				if (PS->GetTeamId() == OwnerData.TeamID)
				{
					return false;
				}
			}
		}

		// Exclude dead or dying characters
		if (Filter.bExcludeDeadOrDying)
		{
			if (const ULyraHealthComponent* HealthComponent = ULyraHealthComponent::FindHealthComponent(TargetCharacter))
			{
				if (HealthComponent->IsDeadOrDying())
				{
					return false;
				}
			}
		}
	}

	// If this target has any tags that the filter wants to exlclude, then ignore it
	if (Target.AssociatedTags.HasAny(Filter.ExclusionGameplayTags))
	{
		return false;
	}

	if (Filter.ExcludedClasses.Contains(TargetOwningActor->GetClass()))
	{
		return false;
	}

	return true;
}

float UAimAssistTargetManagerComponent::GetFOVScale(const APlayerController* PC, ECommonInputType InputType)
{
	float FovScale = 1.0f;
	const UInputSettings* DefaultInputSettings = GetDefault<UInputSettings>();
	check(DefaultInputSettings && PC);

	if (PC->PlayerCameraManager && DefaultInputSettings->bEnableFOVScaling)
	{
		const float FOVAngle = PC->PlayerCameraManager->GetFOVAngle();
		switch (InputType)
		{
		case ECommonInputType::Gamepad:
		case ECommonInputType::Touch:
		{
			static const float PlayerInput_BaseFOV = 80.0f;
			// This is the proper way to scale based off FOV changes.
			// Ideally mouse would use this too but changing it now will cause sensitivity to change for existing players.
			const float BaseHalfFOV = PlayerInput_BaseFOV * 0.5f;
			const float HalfFOV = FOVAngle * 0.5f;
			const float BaseTanHalfFOV = FMath::Tan(FMath::DegreesToRadians(BaseHalfFOV));
			const float TanHalfFOV = FMath::Tan(FMath::DegreesToRadians(HalfFOV));

			check(BaseTanHalfFOV > 0.0f);
			FovScale = (TanHalfFOV / BaseTanHalfFOV);
			break;
		}
		case ECommonInputType::MouseAndKeyboard:
			FovScale = (DefaultInputSettings->FOVScale * FOVAngle);
			break;
		default:
			ensure(false);
			break;
		}
	}
	return FovScale;
}

void UAimAssistTargetManagerComponent::DetermineTargetVisibility(FLyraAimAssistTarget& Target, const FAimAssistSettings& Settings, const FAimAssistFilter& Filter, const FAimAssistOwnerViewData& OwnerData)
{
	UWorld* World = GetWorld();
	check(World);

	const AActor* Actor = Target.TargetShapeComponent->GetOwner();
	if (!Actor)
	{
		ensure(false);
		return;
	}

	FVector TargetEyeLocation;
	FRotator TargetEyeRotation;
	Actor->GetActorEyesViewPoint(TargetEyeLocation, TargetEyeRotation);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AimAssist_DetermineTargetVisibility), true);
	InitTargetSelectionCollisionParams(QueryParams, *Actor, Filter);
	QueryParams.AddIgnoredActor(Actor);

	const UShooterCoreRuntimeSettings* ShooterSettings = GetDefault<UShooterCoreRuntimeSettings>();
	const ECollisionChannel AimAssistChannel = ShooterSettings->GetAimAssistCollisionChannel();

	FCollisionResponseParams ResponseParams;
	ResponseParams.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	ResponseParams.CollisionResponse.SetResponse(AimAssistChannel, ECR_Ignore);

	if (Target.bIsVisible && Settings.bEnableAsyncVisibilityTrace)
	{
		// Query for previous asynchronous trace result.
		if (Target.VisibilityTraceHandle.IsValid())
		{
			FTraceDatum TraceDatum;
			if (World->QueryTraceData(Target.VisibilityTraceHandle, TraceDatum))
			{
				Target.bIsVisible = (FHitResult::GetFirstBlockingHit(TraceDatum.OutHits) == nullptr);
			}
			else
			{
				UE_LOG(LogAimAssist, Warning, TEXT("UAimAssistTargetManagerComponent::DetermineTargetVisibility() - Failed to find async visibility trace data!"));
				Target.bIsVisible = false;
			}

			// Invalidate the async trace handle.
			Target.VisibilityTraceHandle = FTraceHandle();
		}

		// Only start a new asynchronous trace for next frame if the target is still visible.
		if (Target.bIsVisible)
		{
			Target.VisibilityTraceHandle = World->AsyncLineTraceByChannel(EAsyncTraceType::Test, OwnerData.ViewTransform.GetTranslation(), TargetEyeLocation, ECC_Visibility, QueryParams, ResponseParams);
		}
	}
	else
	{
		Target.bIsVisible = !World->LineTraceTestByChannel(OwnerData.ViewTransform.GetTranslation(), TargetEyeLocation, ECC_Visibility, QueryParams, ResponseParams);

		// Invalidate the async trace handle.
		Target.VisibilityTraceHandle = FTraceHandle();
	}
}

void UAimAssistTargetManagerComponent::InitTargetSelectionCollisionParams(FCollisionQueryParams& OutParams, const AActor& RequestedBy, const FAimAssistFilter& Filter) const
{
	// Exclude Requester
	if (Filter.bExcludeRequester)
	{
		OutParams.AddIgnoredActor(&RequestedBy);
	}

	// Exclude attached to Requester
	if (Filter.bExcludeAllAttachedToRequester)
	{
		TArray<AActor*> ActorsAttachedToRequester;
		RequestedBy.GetAttachedActors(ActorsAttachedToRequester);

		OutParams.AddIgnoredActors(ActorsAttachedToRequester);
	}

	if (Filter.bExcludeInstigator)
	{
		OutParams.AddIgnoredActor(RequestedBy.GetInstigator());
	}

	// Exclude attached to Instigator
	if (Filter.bExcludeAllAttachedToInstigator && RequestedBy.GetInstigator())
	{
		TArray<AActor*> ActorsAttachedToInstigator;
		RequestedBy.GetInstigator()->GetAttachedActors(ActorsAttachedToInstigator);

		OutParams.AddIgnoredActors(ActorsAttachedToInstigator);
	}

	OutParams.bTraceComplex = Filter.bTraceComplexCollision;
}

ECollisionChannel UAimAssistTargetManagerComponent::GetAimAssistChannel() const
{
	const UShooterCoreRuntimeSettings* ShooterSettings = GetDefault<UShooterCoreRuntimeSettings>();
	const ECollisionChannel AimAssistChannel = ShooterSettings->GetAimAssistCollisionChannel();

	ensureMsgf(AimAssistChannel != ECollisionChannel::ECC_MAX, TEXT("The aim assist collision channel has not been set! Do this in the ShooterCoreRuntime plugin settings"));

	return AimAssistChannel;
}

```

### 附录文件 16：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/Input/AimAssistInputModifier.h

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Public/Input/AimAssistInputModifier.h

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "Math/IntRect.h"
#include "ScalableFloat.h"
#include "WorldCollision.h"
#include "Input/LyraInputModifiers.h"
#include "DrawDebugHelpers.h"
#include "AimAssistInputModifier.generated.h"

class APlayerController;
class UInputAction;
class ULocalPlayer;
class UShapeComponent;
class ULyraAimSensitivityData;
class ULyraSettingsShared;

DECLARE_LOG_CATEGORY_EXTERN(LogAimAssist, Log, All);

/** A container for some commonly used viewport data based on the current pawn */
struct FAimAssistOwnerViewData
{
	FAimAssistOwnerViewData() { ResetViewData(); }

	/**
	 * Update the "owner" information based on our current player controller. This calculates and stores things like the view matrix
	 * and current rotation that is used to determine what targets are visible
	 */
	void UpdateViewData(const APlayerController* PC);

	/** Reset all the properties on this set of data to their defaults */
	void ResetViewData();

	/** Returns true if this owner struct has a valid player controller */
	bool IsDataValid() const { return PlayerController != nullptr && LocalPlayer != nullptr; }

	FBox2D ProjectReticleToScreen(float ReticleWidth, float ReticleHeight, float ReticleDepth) const;
	FBox2D ProjectBoundsToScreen(const FBox& Bounds) const;
	FBox2D ProjectShapeToScreen(const FCollisionShape& Shape, const FVector& ShapeOrigin, const FTransform& WorldTransform) const;
	FBox2D ProjectBoxToScreen(const FCollisionShape& Shape, const FVector& ShapeOrigin, const FTransform& WorldTransform) const;
	FBox2D ProjectSphereToScreen(const FCollisionShape& Shape, const FVector& ShapeOrigin, const FTransform& WorldTransform) const;
	FBox2D ProjectCapsuleToScreen(const FCollisionShape& Shape, const FVector& ShapeOrigin, const FTransform& WorldTransform) const;

	/** Pointer to the player controller that can be used to calculate the data we need to check for visible targets */
	const APlayerController* PlayerController = nullptr;

	const ULocalPlayer* LocalPlayer = nullptr;

	FMatrix ProjectionMatrix = FMatrix::Identity;

	FMatrix ViewProjectionMatrix = FMatrix::Identity;

	FIntRect ViewRect = FIntRect(0, 0, 0, 0);

	FTransform ViewTransform = FTransform::Identity;

	FVector ViewForward = FVector::ZeroVector;

	// Player transform is the actor's location and the controller's rotation.
	FTransform PlayerTransform = FTransform::Identity;

	FTransform PlayerInverseTransform = FTransform::Identity;

	/** The movement delta between the current frame and the last */
	FVector DeltaMovement = FVector::ZeroVector;

	/** The ID of the team that this owner is from. It is populated from the ALyraPlayerState. If the owner does not have a player state, then it will be INDEX_NONE */
	int32 TeamID = INDEX_NONE;
};

/** A container for keeping the state of targets between frames that can be cached */
USTRUCT(BlueprintType)
struct FLyraAimAssistTarget
{
	GENERATED_BODY()

	FLyraAimAssistTarget() { ResetTarget(); }

	bool IsTargetValid() const { return TargetShapeComponent.IsValid(); }

	void ResetTarget();

	FRotator GetRotationFromMovement(const FAimAssistOwnerViewData& OwnerInfo) const;

	TWeakObjectPtr<UShapeComponent> TargetShapeComponent;

	FVector Location = FVector::ZeroVector;
	FVector DeltaMovement = FVector::ZeroVector;
	FBox2D ScreenBounds;

	float ViewDistance = 0.0f;
	float SortScore = 0.0f;

	float AssistTime = 0.0f;
	float AssistWeight = 0.0f;

	FTraceHandle VisibilityTraceHandle;

	uint8 bIsVisible : 1;

	uint8 bUnderAssistInnerReticle : 1;

	uint8 bUnderAssistOuterReticle : 1;

protected:

	float CalculateRotationToTarget2D(float TargetX, float TargetY, float OffsetY) const;
};

/** Options for filtering out certain aim assist targets */
USTRUCT(BlueprintType)
struct FAimAssistFilter
{
	GENERATED_BODY()

	FAimAssistFilter()
		: bIncludeSameFriendlyTargets(false)
		, bExcludeInstigator(true)
		, bExcludeAllAttachedToInstigator(false)
		, bExcludeRequester(true)
		, bExcludeAllAttachedToRequester(false)
		, bTraceComplexCollision(false)
		, bExcludeDeadOrDying(true)
	{}

	/** If true, then we should include any targets even if they are on our team */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	uint8 bIncludeSameFriendlyTargets : 1;

	/** Exclude 'RequestedBy->Instigator' Actor */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = TargetSelection)
	uint8 bExcludeInstigator : 1;

	/** Exclude all actors attached to 'RequestedBy->Instigator' Actor */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = TargetSelection)
	uint32 bExcludeAllAttachedToInstigator : 1;

	/** Exclude 'RequestedBy Actor */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = TargetSelection)
	uint8 bExcludeRequester : 1;

	/** Exclude all actors attached to 'RequestedBy Actor */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = TargetSelection)
	uint8 bExcludeAllAttachedToRequester : 1;

	/** Trace against complex collision. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = TargetSelection)
	uint8 bTraceComplexCollision : 1;

	/** Exclude all dead or dying targets */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = TargetSelection)
	uint8 bExcludeDeadOrDying : 1;

	/** Any target whose owning actor is of this type will be excluded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSet<TObjectPtr<UClass>> ExcludedClasses;

	/** Targets with any of these tags will be excluded. */
	FGameplayTagContainer ExclusionGameplayTags;

	/** Any target outside of this range will be excluded */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	double TargetRange = 10000.0;
};

/** Settings for how aim assist should behave when there are active targets */
USTRUCT(BlueprintType)
struct FAimAssistSettings
{
	GENERATED_BODY()

	FAimAssistSettings();

	float GetTargetWeightForTime(float Time) const;
	float GetTargetWeightMaxTime() const;

	// Width of aim assist inner reticle in world space.
	UPROPERTY(EditAnywhere)
	FScalableFloat AssistInnerReticleWidth;

	// Height of aim assist inner reticle in world space.
	UPROPERTY(EditAnywhere)
	FScalableFloat AssistInnerReticleHeight;

	// Width of aim assist outer reticle in world space.
	UPROPERTY(EditAnywhere)
	FScalableFloat AssistOuterReticleWidth;

	// Height of aim assist outer reticle in world space.
	UPROPERTY(EditAnywhere)
	FScalableFloat AssistOuterReticleHeight;

	// Width of targeting reticle in world space.
	UPROPERTY(EditAnywhere)
	FScalableFloat TargetingReticleWidth;

	// Height of targeting reticle in world space.
	UPROPERTY(EditAnywhere)
	FScalableFloat TargetingReticleHeight;

	// Range from player's camera used to gather potential targets.
	// Note: This is scaled using the field of view in order to limit targets by their screen size.
	UPROPERTY(EditAnywhere)
	FScalableFloat TargetRange;

	// How much weight the target has based on the time it has been targeted.  (0 = None, 1 = Max)
	UPROPERTY(EditAnywhere)
	TObjectPtr<const UCurveFloat> TargetWeightCurve = nullptr;

	// How much target and player movement contributes to the aim assist pull when target is under the inner reticle. (0 = None, 1 = Max)
	UPROPERTY(EditAnywhere)
	FScalableFloat PullInnerStrengthHip;

	// How much target and player movement contributes to the aim assist pull when target is under the outer reticle. (0 = None, 1 = Max)
	UPROPERTY(EditAnywhere)
	FScalableFloat PullOuterStrengthHip;

	// How much target and player movement contributes to the aim assist pull when target is under the inner reticle. (0 = None, 1 = Max)
	UPROPERTY(EditAnywhere)
	FScalableFloat PullInnerStrengthAds;

	// How much target and player movement contributes to the aim assist pull when target is under the outer reticle. (0 = None, 1 = Max)
	UPROPERTY(EditAnywhere)
	FScalableFloat PullOuterStrengthAds;

	// Exponential interpolation rate used to ramp up the pull strength.  Set to '0' to disable.
	UPROPERTY(EditAnywhere)
	FScalableFloat PullLerpInRate;

	// Exponential interpolation rate used to ramp down the pull strength.  Set to '0' to disable.
	UPROPERTY(EditAnywhere)
	FScalableFloat PullLerpOutRate;

	// Rotation rate maximum cap on amount of aim assist pull.  Set to '0' to disable.
	// Note: This is scaled based on the field of view so it feels the same regardless of zoom.
	UPROPERTY(EditAnywhere)
	FScalableFloat PullMaxRotationRate;

	// Amount of aim assist slow applied to desired turn rate when target is under the inner reticle. (0 = None, 1 = Max)
	UPROPERTY(EditAnywhere)
	FScalableFloat SlowInnerStrengthHip;

	// Amount of aim assist slow applied to desired turn rate when target is under the outer reticle. (0 = None, 1 = Max)
	UPROPERTY(EditAnywhere)
	FScalableFloat SlowOuterStrengthHip;

	// Amount of aim assist slow applied to desired turn rate when target is under the inner reticle. (0 = None, 1 = Max)
	UPROPERTY(EditAnywhere)
	FScalableFloat SlowInnerStrengthAds;

	// Amount of aim assist slow applied to desired turn rate when target is under the outer reticle. (0 = None, 1 = Max)
	UPROPERTY(EditAnywhere)
	FScalableFloat SlowOuterStrengthAds;

	// Exponential interpolation rate used to ramp up the slow strength.  Set to '0' to disable.
	UPROPERTY(EditAnywhere)
	FScalableFloat SlowLerpInRate;

	// Exponential interpolation rate used to ramp down the slow strength.  Set to '0' to disable.
	UPROPERTY(EditAnywhere)
	FScalableFloat SlowLerpOutRate;

	// Rotation rate minimum cap on amount to aim assist slow.  Set to '0' to disable.
	// Note: This is scaled based on the field of view so it feels the same regardless of zoom.
	UPROPERTY(EditAnywhere)
	FScalableFloat SlowMinRotationRate;

	/** The maximum number of targets that can be considered during a given frame. */
	UPROPERTY(EditAnywhere)
	int32 MaxNumberOfTargets = 6;

	/**  */
	UPROPERTY(EditAnywhere)
	float ReticleDepth = 3000.0f;

	UPROPERTY(EditAnywhere)
	float TargetScore_AssistWeight = 10.0f;

	UPROPERTY(EditAnywhere)
	float TargetScore_ViewDot = 50.0f;

	UPROPERTY(EditAnywhere)
	float TargetScore_ViewDotOffset = 40.0f;

	UPROPERTY(EditAnywhere)
	float TargetScore_ViewDistance = 0.25f;

	UPROPERTY(EditAnywhere)
	float StrengthScale = 1.0f;

	/** Enabled/Disable asynchronous visibility traces. */
	UPROPERTY(EditAnywhere)
	uint8 bEnableAsyncVisibilityTrace : 1;

	/** Whether or not we require input for aim assist to be applied */
	UPROPERTY(EditAnywhere)
	uint8 bRequireInput : 1;

	/** Whether or not pull should be applied to aim assist */
	UPROPERTY(EditAnywhere)
	uint8 bApplyPull : 1;

	/** Whether or not to apply a strafe pull based off of movement input */
	UPROPERTY(EditAnywhere)
	uint8 bApplyStrafePullScale : 1;

	/** Whether or not to apply a slowing effect during aim assist */
	UPROPERTY(EditAnywhere)
	uint8 bApplySlowing : 1;

	/** Whether or not to apply a dynamic slow effect based off of look input */
	UPROPERTY(EditAnywhere)
	uint8 bUseDynamicSlow : 1;

	/** Whether or not look rates should blend between yaw and pitch based on stick deflection using radial look rates */
	UPROPERTY(EditAnywhere)
	uint8 bUseRadialLookRates : 1;
};

/**
 * An input modifier to help gamepad players have better targeting.
 */
UCLASS()
class UAimAssistInputModifier : public UInputModifier
{
	GENERATED_BODY()

public:

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category=Settings, Config)
	FAimAssistSettings Settings {};

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category=Settings, Config)
	FAimAssistFilter Filter {};

	/** The input action that represents the actual movement of the player */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category=Settings)
	TObjectPtr<const UInputAction> MoveInputAction = nullptr;

	/** The type of targeting to use for this Sensitivity */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category=Settings, Config)
	ELyraTargetingType TargetingType = ELyraTargetingType::Normal;

	/** Asset that gives us access to the float scalar value being used for sensitivty */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(AssetBundles="Client,Server"))
	TObjectPtr<const ULyraAimSensitivityData> SensitivityLevelTable = nullptr;

protected:

	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime) override;

	/**
	* Swaps the target cache's and determines what targets are currently visible.
	* Updates the score of each target to determine
	* how much pull/slow effect should be applied to each
	*/
	void UpdateTargetData(float DeltaTime);

	FRotator UpdateRotationalVelocity(APlayerController* PC, float DeltaTime, FVector CurrentLookInputValue, FVector CurrentMoveInputValue);

	/** Calcualte the pull and slow strengh of a given target */
	void CalculateTargetStrengths(const FLyraAimAssistTarget& Target, float& OutPullStrength, float& OutSlowStrength) const;

	FRotator GetLookRates(const FVector& LookInput);

	void SwapTargetCaches() { TargetCacheIndex ^= 1; }
	const TArray<FLyraAimAssistTarget>& GetPreviousTargetCache() const	{ return ((TargetCacheIndex == 0) ? TargetCache1 : TargetCache0); }
	TArray<FLyraAimAssistTarget>& GetPreviousTargetCache()				{ return ((TargetCacheIndex == 0) ? TargetCache1 : TargetCache0); }

	const TArray<FLyraAimAssistTarget>& GetCurrentTargetCache() const	{ return ((TargetCacheIndex == 0) ? TargetCache0 : TargetCache1); }
	TArray<FLyraAimAssistTarget>& GetCurrentTargetCache()				{ return ((TargetCacheIndex == 0) ? TargetCache0 : TargetCache1); }

	bool HasAnyCurrentTargets() const { return !GetCurrentTargetCache().IsEmpty(); }

	const float GetSensitivtyScalar(const ULyraSettingsShared* SharedSettings) const;

	// Tracking of the current and previous frame's targets
	UPROPERTY()
	TArray<FLyraAimAssistTarget> TargetCache0;

	UPROPERTY()
	TArray<FLyraAimAssistTarget> TargetCache1;

	/** The current in use target cache */
	uint32 TargetCacheIndex;

	FAimAssistOwnerViewData OwnerViewData;

	float LastPullStrength = 0.0f;
	float LastSlowStrength = 0.0f;

#if ENABLE_DRAW_DEBUG
	float LastLookRateYaw;
	float LastLookRatePitch;

	FVector LastOutValue;
	FVector LastBaselineValue;

	// TODO: Remove this variable and move debug visualization out of this
	bool bRegisteredDebug = false;

	void AimAssistDebugDraw(class UCanvas* Canvas, APlayerController* PC);
	FDelegateHandle	DebugDrawHandle;
#endif
};

```

### 附录文件 17：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Input/AimAssistInputModifier.cpp

来源：Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime/Private/Input/AimAssistInputModifier.cpp

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/AimAssistInputModifier.h"
#include "CommonInputTypeEnum.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EnhancedPlayerInput.h"
#include "Input/AimAssistTargetManagerComponent.h"
#include "Input/LyraAimSensitivityData.h"
#include "Player/LyraLocalPlayer.h"
#include "Player/LyraPlayerState.h"
#include "SceneView.h"
#include "Settings/LyraSettingsShared.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AimAssistInputModifier)

#if ENABLE_DRAW_DEBUG
#include "Engine/Canvas.h"
#include "Debug/DebugDrawService.h"
#endif	// ENABLE_DRAW_DEBUG

DEFINE_LOG_CATEGORY(LogAimAssist);

namespace LyraConsoleVariables
{
	static bool bEnableAimAssist = true;
	static FAutoConsoleVariableRef CVarEnableAimAssist(
		TEXT("lyra.Weapon.EnableAimAssist"),
		bEnableAimAssist,
		TEXT("Should we enable aim assist while shooting?"),
		ECVF_Cheat);

	static bool bDrawAimAssistDebug = false;
	static FAutoConsoleVariableRef CVarDrawAimAssistDebug(
		TEXT("lyra.Weapon.DrawAimAssistDebug"),
		bDrawAimAssistDebug,
		TEXT("Should we draw some debug stats about aim assist?"),
		ECVF_Cheat);
}

///////////////////////////////////////////////////////////////////
// FLyraAimAssistTarget

void FLyraAimAssistTarget::ResetTarget()
{
	TargetShapeComponent = nullptr;

	Location = FVector::ZeroVector;
	DeltaMovement = FVector::ZeroVector;
	ScreenBounds.Init();

	ViewDistance = 0.0f;
	SortScore = 0.0f;

	AssistTime = 0.0f;
	AssistWeight = 0.0f;

	VisibilityTraceHandle = FTraceHandle();

	bIsVisible = false;
	bUnderAssistInnerReticle = false;
	bUnderAssistOuterReticle = false;
}

FRotator FLyraAimAssistTarget::GetRotationFromMovement(const FAimAssistOwnerViewData& OwnerInfo) const
{
	ensure(OwnerInfo.IsDataValid());

	// Convert everything into player space.
	// Account for player movement in new target location.
	const FVector OldLocation = OwnerInfo.PlayerInverseTransform.TransformPositionNoScale(Location - DeltaMovement);
	const FVector NewLocation = OwnerInfo.PlayerInverseTransform.TransformPositionNoScale(Location - OwnerInfo.DeltaMovement);

	FRotator RotationToTarget;
	RotationToTarget.Yaw = CalculateRotationToTarget2D(NewLocation.X, NewLocation.Y, OldLocation.Y);
	RotationToTarget.Pitch = CalculateRotationToTarget2D(NewLocation.X, NewLocation.Z, OldLocation.Z);
	RotationToTarget.Roll = 0.0f;

	return RotationToTarget;
}

float FLyraAimAssistTarget::CalculateRotationToTarget2D(float TargetX, float TargetY, float OffsetY) const
{
	if (TargetX <= 0.0f)
	{
		return 0.0f;
	}

	const float AngleA = FMath::RadiansToDegrees(FMath::Atan2(TargetY, TargetX));

	if (FMath::IsNearlyZero(OffsetY))
	{
		return AngleA;
	}

	const float Distance = FMath::Sqrt((TargetX * TargetX) + (TargetY * TargetY));
	ensure(Distance > 0.0f);

	const float AngleB = FMath::RadiansToDegrees(FMath::Asin(OffsetY / Distance));

	return FRotator::NormalizeAxis(AngleA - AngleB);
}

///////////////////////////////////////////////////////////////////
// FAimAssistSettings

FAimAssistSettings::FAimAssistSettings()
{
	AssistInnerReticleWidth.SetValue(20.0f);
	AssistInnerReticleHeight.SetValue(20.0f);
	AssistOuterReticleWidth.SetValue(80.0f);
	AssistOuterReticleHeight.SetValue(80.0f);

	TargetingReticleWidth.SetValue(1200.0f);
	TargetingReticleHeight.SetValue(675.0f);
	TargetRange.SetValue(10000.0f);
	TargetWeightCurve = nullptr;

	PullInnerStrengthHip.SetValue(0.6f);
	PullOuterStrengthHip.SetValue(0.5f);
	PullInnerStrengthAds.SetValue(0.7f);
	PullOuterStrengthAds.SetValue(0.4f);
	PullLerpInRate.SetValue(60.0f);
	PullLerpOutRate.SetValue(4.0f);
	PullMaxRotationRate.SetValue(0.0f);

	SlowInnerStrengthHip.SetValue(0.6f);
	SlowOuterStrengthHip.SetValue(0.5f);
	SlowInnerStrengthAds.SetValue(0.7f);
	SlowOuterStrengthAds.SetValue(0.4f);
	SlowLerpInRate.SetValue(60.0f);
	SlowLerpOutRate.SetValue(4.0f);
	SlowMinRotationRate.SetValue(0.0f);

	bEnableAsyncVisibilityTrace = true;
	bRequireInput = true;
	bApplyPull = true;
	bApplySlowing = true;
	bApplyStrafePullScale = true;
	bUseDynamicSlow = true;
	bUseRadialLookRates = true;
}

float FAimAssistSettings::GetTargetWeightForTime(float Time) const
{
	if (!ensure(TargetWeightCurve != nullptr))
	{
		return 0.0f;
	}

	return FMath::Clamp(TargetWeightCurve->GetFloatValue(Time), 0.0f, 1.0f);
}

float FAimAssistSettings::GetTargetWeightMaxTime() const
{
	if (!ensure(TargetWeightCurve != nullptr))
	{
		return 0.0f;
	}

	float MinTime = 0.0f;
	float MaxTime = 0.0f;

	TargetWeightCurve->FloatCurve.GetTimeRange(MinTime, MaxTime);

	return MaxTime;
}

///////////////////////////////////////////////////////////////////
// FAimAssistOwnerViewData

void FAimAssistOwnerViewData::UpdateViewData(const APlayerController* PC)
{
	FSceneViewProjectionData ProjectionData;
	PlayerController = PC;
	LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;

	if (!IsDataValid() || !PlayerController || !LocalPlayer)
	{
		ResetViewData();
		return;
	}

	const APawn* Pawn = Cast<APawn>(PlayerController->GetPawn());

	if (!Pawn || !LocalPlayer || !LocalPlayer->ViewportClient || !LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, ProjectionData))
	{
		ResetViewData();
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	ProjectionMatrix = ProjectionData.ProjectionMatrix;
	ViewProjectionMatrix = ProjectionData.ComputeViewProjectionMatrix();
	ViewRect = ProjectionData.GetConstrainedViewRect();
	ViewTransform = FTransform(ViewRotation, ViewLocation);
	ViewForward = ViewTransform.GetUnitAxis(EAxis::X);

	const FVector OldLocation = PlayerTransform.GetTranslation();
	const FVector NewLocation = Pawn->GetActorLocation();
	const FRotator NewRotation = PC->GetControlRotation();

	PlayerTransform = FTransform(NewRotation, NewLocation);
	PlayerInverseTransform = PlayerTransform.Inverse();

	DeltaMovement = (NewLocation - OldLocation);

	// Set the Team ID
	if (ALyraPlayerState* LyraPS = PlayerController->GetPlayerState<ALyraPlayerState>())
	{
		TeamID = LyraPS->GetTeamId();
	}
	else
	{
		TeamID = INDEX_NONE;
	}
}

void FAimAssistOwnerViewData::ResetViewData()
{
	PlayerController = nullptr;
	LocalPlayer = nullptr;

	ProjectionMatrix = FMatrix::Identity;
	ViewProjectionMatrix = FMatrix::Identity;
	ViewRect = FIntRect(0, 0, 0, 0);
	ViewTransform = FTransform::Identity;

	PlayerTransform = FTransform::Identity;
	PlayerInverseTransform = FTransform::Identity;
	ViewForward = FVector::ZeroVector;

	DeltaMovement = FVector::ZeroVector;
	TeamID = INDEX_NONE;
}

FBox2D FAimAssistOwnerViewData::ProjectReticleToScreen(float ReticleWidth, float ReticleHeight, float ReticleDepth) const
{
	FBox2D ReticleBounds(ForceInitToZero);

	const FVector ReticleExtents((ReticleWidth * 0.5f), -(ReticleHeight * 0.5f), ReticleDepth);

	if (FSceneView::ProjectWorldToScreen(ReticleExtents, ViewRect, ProjectionMatrix, ReticleBounds.Max))
	{
		ReticleBounds.Min.X = ViewRect.Min.X + (ViewRect.Max.X - ReticleBounds.Max.X);
		ReticleBounds.Min.Y = ViewRect.Min.Y + (ViewRect.Max.Y - ReticleBounds.Max.Y);

		ReticleBounds.bIsValid = true;
	}

	return ReticleBounds;
}

FBox2D FAimAssistOwnerViewData::ProjectBoundsToScreen(const FBox& Bounds) const
{
	FBox2D Box2D(ForceInitToZero);

	if (Bounds.IsValid)
	{
		const FVector Vertices[] =
		{
			FVector(Bounds.Min),
			FVector(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.Z),
			FVector(Bounds.Min.X, Bounds.Max.Y, Bounds.Min.Z),
			FVector(Bounds.Max.X, Bounds.Min.Y, Bounds.Min.Z),
			FVector(Bounds.Max.X, Bounds.Max.Y, Bounds.Min.Z),
			FVector(Bounds.Max.X, Bounds.Min.Y, Bounds.Max.Z),
			FVector(Bounds.Min.X, Bounds.Max.Y, Bounds.Max.Z),
			FVector(Bounds.Max)
		};

		for (int32 VerticeIndex = 0; VerticeIndex < UE_ARRAY_COUNT(Vertices); ++VerticeIndex)
		{
			FVector2D ScreenPoint;
			if (FSceneView::ProjectWorldToScreen(Vertices[VerticeIndex], ViewRect, ViewProjectionMatrix, ScreenPoint))
			{
				Box2D += ScreenPoint;
			}
		}
	}

	return Box2D;
}

FBox2D FAimAssistOwnerViewData::ProjectShapeToScreen(const FCollisionShape& Shape, const FVector& ShapeOrigin, const FTransform& WorldTransform) const
{
	FBox2D Box2D(ForceInitToZero);

	switch (Shape.ShapeType)
	{
	case ECollisionShape::Box:
		Box2D = ProjectBoxToScreen(Shape, ShapeOrigin, WorldTransform);
		break;
	case ECollisionShape::Sphere:
		Box2D = ProjectSphereToScreen(Shape, ShapeOrigin, WorldTransform);
		break;
	case ECollisionShape::Capsule:
		Box2D = ProjectCapsuleToScreen(Shape, ShapeOrigin, WorldTransform);
		break;
	default:
		UE_LOG(LogAimAssist, Warning, TEXT("FAimAssistOwnerViewData::ProjectShapeToScreen() - Invalid shape type!"));
		break;
	}

	return Box2D;
}

FBox2D FAimAssistOwnerViewData::ProjectBoxToScreen(const FCollisionShape& Shape, const FVector& ShapeOrigin, const FTransform& WorldTransform) const
{
	check(Shape.IsBox());
	check(!Shape.IsNearlyZero());

	const FVector BoxExtents = Shape.GetBox();

	const FVector Vertices[] =
	{
		FVector(-BoxExtents.X, -BoxExtents.Y, -BoxExtents.Z),
		FVector(-BoxExtents.X, -BoxExtents.Y,  BoxExtents.Z),
		FVector(-BoxExtents.X,  BoxExtents.Y, -BoxExtents.Z),
		FVector(-BoxExtents.X,  BoxExtents.Y,  BoxExtents.Z),
		FVector( BoxExtents.X, -BoxExtents.Y, -BoxExtents.Z),
		FVector( BoxExtents.X, -BoxExtents.Y,  BoxExtents.Z),
		FVector( BoxExtents.X,  BoxExtents.Y, -BoxExtents.Z),
		FVector( BoxExtents.X,  BoxExtents.Y,  BoxExtents.Z)
	};

	FBox2D Box2D(ForceInitToZero);

	for (int32 VerticeIndex = 0; VerticeIndex < UE_ARRAY_COUNT(Vertices); ++VerticeIndex)
	{
		const FVector Vertex = WorldTransform.TransformPositionNoScale(Vertices[VerticeIndex] + ShapeOrigin);

		FVector2D ScreenPoint;
		if (FSceneView::ProjectWorldToScreen(Vertex, ViewRect, ViewProjectionMatrix, ScreenPoint))
		{
			Box2D += ScreenPoint;
		}
	}

	return Box2D;
}

FBox2D FAimAssistOwnerViewData::ProjectSphereToScreen(const FCollisionShape& Shape, const FVector& ShapeOrigin, const FTransform& WorldTransform) const
{
	check(Shape.IsSphere());
	check(!Shape.IsNearlyZero());

	const FVector ViewAxisY = ViewTransform.GetUnitAxis(EAxis::Y);
	const FVector ViewAxisZ = ViewTransform.GetUnitAxis(EAxis::Z);

	const float SphereRadius = Shape.GetSphereRadius();
	const FVector SphereLocation = WorldTransform.TransformPositionNoScale(ShapeOrigin);
	const FVector SphereExtent = (ViewAxisY * SphereRadius) + (ViewAxisZ * SphereRadius);

	const FVector Vertices[] =
	{
		FVector(SphereLocation + SphereExtent),
		FVector(SphereLocation - SphereExtent),
	};

	FBox2D Box2D(ForceInitToZero);

	for (int32 VerticeIndex = 0; VerticeIndex < UE_ARRAY_COUNT(Vertices); ++VerticeIndex)
	{
		FVector2D ScreenPoint;
		if (FSceneView::ProjectWorldToScreen(Vertices[VerticeIndex], ViewRect, ViewProjectionMatrix, ScreenPoint))
		{
			Box2D += ScreenPoint;
		}
	}

	return Box2D;
}

FBox2D FAimAssistOwnerViewData::ProjectCapsuleToScreen(const FCollisionShape& Shape, const FVector& ShapeOrigin, const FTransform& WorldTransform) const
{
	check(Shape.IsCapsule());
	check(!Shape.IsNearlyZero());

	const FVector ViewAxisY = ViewTransform.GetUnitAxis(EAxis::Y);
	const FVector ViewAxisZ = ViewTransform.GetUnitAxis(EAxis::Z);

	const float CapsuleAxisHalfLength = Shape.GetCapsuleAxisHalfLength();
	const float CapsuleRadius = Shape.GetCapsuleRadius();

	const FVector TopSphereLocation = WorldTransform.TransformPositionNoScale(FVector(0.0f, 0.0f, CapsuleAxisHalfLength) + ShapeOrigin);
	const FVector BottomSphereLocation = WorldTransform.TransformPositionNoScale(FVector(0.0f, 0.0f, -CapsuleAxisHalfLength) + ShapeOrigin);
	const FVector SphereExtent = (ViewAxisY * CapsuleRadius) + (ViewAxisZ * CapsuleRadius);

	const FVector Vertices[] =
	{
		FVector(TopSphereLocation + SphereExtent),
		FVector(TopSphereLocation - SphereExtent),
		FVector(BottomSphereLocation + SphereExtent),
		FVector(BottomSphereLocation - SphereExtent),
	};

	FBox2D Box2D(ForceInitToZero);

	for (int32 VerticeIndex = 0; VerticeIndex < UE_ARRAY_COUNT(Vertices); ++VerticeIndex)
	{
		FVector2D ScreenPoint;
		if (FSceneView::ProjectWorldToScreen(Vertices[VerticeIndex], ViewRect, ViewProjectionMatrix, ScreenPoint))
		{
			Box2D += ScreenPoint;
		}
	}

	return Box2D;
}

///////////////////////////////////////////////////////////////////
// UAimAssistInputModifier

static const float GamepadUserOptions_YawLookRateBase = 900.0f;
static const float GamepadUserOptions_PitchLookRateBase = (GamepadUserOptions_YawLookRateBase * 0.6f);

// TODO Make this a constexpr instead of a define
#define YawLookSpeedToRotationRate(_Speed)		((_Speed) / 100.0f * GamepadUserOptions_YawLookRateBase)
#define PitchLookSpeedToRotationRate(_Speed)	((_Speed) / 100.0f * GamepadUserOptions_PitchLookRateBase)

FRotator UAimAssistInputModifier::GetLookRates(const FVector& LookInput)
{
	FRotator LookRates;

	const float SensitivityHipLevel = 50.0f;
	{
		LookRates.Yaw = YawLookSpeedToRotationRate(SensitivityHipLevel);
		LookRates.Pitch = PitchLookSpeedToRotationRate(SensitivityHipLevel);
		LookRates.Roll = 0.0f;
	}

	LookRates.Yaw = FMath::Clamp(LookRates.Yaw, 0.0f, GamepadUserOptions_YawLookRateBase);
	LookRates.Pitch = FMath::Clamp(LookRates.Pitch, 0.0f, GamepadUserOptions_PitchLookRateBase);

	if (Settings.bUseRadialLookRates)
	{
		// Blend between yaw and pitch based on stick deflection.  This keeps diagonals accurate.
		const float RadialLerp = FMath::Atan2(FMath::Abs(LookInput.Y), FMath::Abs(LookInput.X)) / HALF_PI;
		const float RadialLookRate = FMath::Lerp(LookRates.Yaw, LookRates.Pitch, RadialLerp);

		LookRates.Yaw = RadialLookRate;
		LookRates.Pitch = RadialLookRate;
	}

	return LookRates;
}

FInputActionValue UAimAssistInputModifier::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UAimAssistInputModifier::ModifyRaw_Implementation);

#if ENABLE_DRAW_DEBUG
	if (LyraConsoleVariables::bDrawAimAssistDebug)
	{
		if (!DebugDrawHandle.IsValid())
		{
			DebugDrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &UAimAssistInputModifier::AimAssistDebugDraw));
		}
		else
		{
			UDebugDrawService::Unregister(DebugDrawHandle);
			DebugDrawHandle.Reset();
		}
		bRegisteredDebug = true;
	}
#endif

#if !UE_BUILD_SHIPPING
	if (!LyraConsoleVariables::bEnableAimAssist)
	{
		return CurrentValue;
	}
#endif //UE_BUILD_SHIPPING

	APlayerController* PC = PlayerInput ? Cast<APlayerController>(PlayerInput->GetOuter()) : nullptr;
	if (!PC)
	{
		return CurrentValue;
	}

	// Update the "owner" information based on our current player controller. This calculates and stores things like the view matrix
	// and current rotation that is used to determine what targets are visible
	OwnerViewData.UpdateViewData(PC);

	if (!OwnerViewData.IsDataValid())
	{
		return CurrentValue;
	}

	// Swaps the target cache's and determines what targets are currently visible. Updates the score of each target to determine
	// how much pull/slow effect should be applied to each
	UpdateTargetData(DeltaTime);

	FVector BaselineInput = CurrentValue.Get<FVector>();

	FVector OutAssistedInput = BaselineInput;
	FVector CurrentMoveInput = MoveInputAction ? PlayerInput->GetActionValue(MoveInputAction).Get<FVector>() : FVector::ZeroVector;

	// Something about the look rates is incorrect
	FRotator LookRates = GetLookRates(BaselineInput);

	const FRotator RotationalVelocity = UpdateRotationalVelocity(PC, DeltaTime, BaselineInput, CurrentMoveInput);

	if (LookRates.Yaw > 0.0f)
	{
		OutAssistedInput.X = (RotationalVelocity.Yaw / LookRates.Yaw);
		OutAssistedInput.X = FMath::Clamp(OutAssistedInput.X, -1.0f, 1.0f);
	}

	if (LookRates.Pitch > 0.0f)
	{
		OutAssistedInput.Y = (RotationalVelocity.Pitch / LookRates.Pitch);
		OutAssistedInput.Y = FMath::Clamp(OutAssistedInput.Y, -1.0f, 1.0f);
	}

#if ENABLE_DRAW_DEBUG
	LastBaselineValue = BaselineInput;
	LastLookRatePitch = LookRates.Pitch;
	LastLookRateYaw = LookRates.Yaw;
	LastOutValue = OutAssistedInput;
#endif
	return OutAssistedInput;
}

void UAimAssistInputModifier::UpdateTargetData(float DeltaTime)
{
	if(!ensure(OwnerViewData.PlayerController))
	{
		UE_LOG(LogAimAssist, Error, TEXT("[UAimAssistInputModifier::UpdateTargetData] Invalid player controller in owner view data!"));
		return;
	}

	UAimAssistTargetManagerComponent* TargetManager = nullptr;

	if (UWorld* World = OwnerViewData.PlayerController->GetWorld())
	{
		if (AGameStateBase* GameState = World->GetGameState())
		{
			TargetManager = GameState->FindComponentByClass<UAimAssistTargetManagerComponent>();
		}
	}

	if (!TargetManager)
	{
		return;
	}

	// Update the targets based on what is visible
	SwapTargetCaches();
	const TArray<FLyraAimAssistTarget>& OldTargetCache = GetPreviousTargetCache();
	TArray<FLyraAimAssistTarget>& NewTargetCache = GetCurrentTargetCache();

	TargetManager->GetVisibleTargets(Filter, Settings, OwnerViewData, OldTargetCache, NewTargetCache);

	//
	// Update target weights.
	//
	float TotalAssistWeight = 0.0f;

	for (FLyraAimAssistTarget& Target : NewTargetCache)
	{
		if (Target.bUnderAssistOuterReticle && Target.bIsVisible)
		{
			const float MaxAssistTime = Settings.GetTargetWeightMaxTime();
			Target.AssistTime = FMath::Min((Target.AssistTime + DeltaTime), MaxAssistTime);
		}
		else
		{
			Target.AssistTime = FMath::Max((Target.AssistTime - DeltaTime), 0.0f);
		}

		// Look up assist weight based on how long the target has been under the assist reticle.
		Target.AssistWeight = Settings.GetTargetWeightForTime(Target.AssistTime);

		TotalAssistWeight += Target.AssistWeight;
	}

	// Normalize the weights.
	if (TotalAssistWeight > 0.0f)
	{
		for (FLyraAimAssistTarget& Target : NewTargetCache)
		{
			Target.AssistWeight = (Target.AssistWeight / TotalAssistWeight);
		}
	}
}

const float UAimAssistInputModifier::GetSensitivtyScalar(const ULyraSettingsShared* SharedSettings) const
{
	if (SharedSettings && SensitivityLevelTable)
	{
		const ELyraGamepadSensitivity Sens = TargetingType == ELyraTargetingType::Normal ? SharedSettings->GetGamepadLookSensitivityPreset() : SharedSettings->GetGamepadTargetingSensitivityPreset();
		return SensitivityLevelTable->SensitivtyEnumToFloat(Sens);
	}

	UE_LOG(LogAimAssist, Warning, TEXT("SensitivityLevelTable is null, using default value!"));
	return (TargetingType == ELyraTargetingType::Normal) ? 1.0f : 0.5f;
}

FRotator UAimAssistInputModifier::UpdateRotationalVelocity(APlayerController* PC, float DeltaTime, FVector CurrentLookInputValue, FVector CurrentMoveInputValue)
{
	FRotator RotationalVelocity(ForceInitToZero);
	FRotator RotationNeeded(ForceInitToZero);

	float PullStrength = 0.0f;
	float SlowStrength = 0.0f;

	const TArray<FLyraAimAssistTarget>& TargetCache = GetCurrentTargetCache();

	float LookStickDeadzone = 0.25f;
	float MoveStickDeadzone = 0.25f;
	float SettingStrengthScalar = (TargetingType == ELyraTargetingType::Normal) ? 1.0f : 0.5f;

	if (ULyraLocalPlayer* LP = Cast<ULyraLocalPlayer>(PC->GetLocalPlayer()))
	{
		ULyraSettingsShared* SharedSettings = LP->GetSharedSettings();
		LookStickDeadzone = SharedSettings->GetGamepadLookStickDeadZone();
		MoveStickDeadzone = SharedSettings->GetGamepadMoveStickDeadZone();
		SettingStrengthScalar = GetSensitivtyScalar(SharedSettings);
	}

	for (const FLyraAimAssistTarget& Target : TargetCache)
	{
		if (Target.bUnderAssistOuterReticle && Target.bIsVisible)
		{
			// Add up total rotation needed to follow weighted targets based on target and player movement.
			RotationNeeded += (Target.GetRotationFromMovement(OwnerViewData) * Target.AssistWeight);

			float TargetPullStrength = 0.0f;
			float TargetSlowStrength = 0.0f;
			CalculateTargetStrengths(Target, TargetPullStrength, TargetSlowStrength);

			// Add up total amount of weighted pull and slow from the targets.
			PullStrength += TargetPullStrength;
			SlowStrength += TargetSlowStrength;
		}
	}

	// You could also apply some scalars based on the current weapon that is equipped, the player's movement state,
	// or any other factors you want here
	PullStrength *= Settings.StrengthScale * SettingStrengthScalar;
	SlowStrength *= Settings.StrengthScale * SettingStrengthScalar;

	const float PullLerpRate = (PullStrength > LastPullStrength) ? Settings.PullLerpInRate.GetValue() : Settings.PullLerpOutRate.GetValue();
	if (PullLerpRate > 0.0f)
	{
		PullStrength = FMath::FInterpConstantTo(LastPullStrength, PullStrength, DeltaTime, PullLerpRate);
	}

	const float SlowLerpRate = (SlowStrength > LastSlowStrength) ? Settings.SlowLerpInRate.GetValue() : Settings.SlowLerpOutRate.GetValue();
	if (SlowLerpRate > 0.0f)
	{
		SlowStrength = FMath::FInterpConstantTo(LastSlowStrength, SlowStrength, DeltaTime, SlowLerpRate);
	}

	LastPullStrength = PullStrength;
	LastSlowStrength = SlowStrength;

	const bool bIsLookInputActive =  (CurrentLookInputValue.SizeSquared() > FMath::Square(LookStickDeadzone));
	const bool bIsMoveInputActive = (CurrentMoveInputValue.SizeSquared() > FMath::Square(MoveStickDeadzone));

	const bool bIsApplyingLookInput = (bIsLookInputActive || !Settings.bRequireInput);
	const bool bIsApplyingMoveInput = (bIsMoveInputActive || !Settings.bRequireInput);
	const bool bIsApplyingAnyInput = (bIsApplyingLookInput || bIsApplyingMoveInput);

	// Apply pulling towards the target
	if (Settings.bApplyPull && bIsApplyingAnyInput && !FMath::IsNearlyZero(PullStrength))
	{
		// The amount of pull is a percentage of the rotation needed to stay on target.
		FRotator PullRotation = (RotationNeeded * PullStrength);

		if (!bIsApplyingLookInput && Settings.bApplyStrafePullScale)
		{
			// Scale pull strength by amount of player strafe if the player isn't actively looking around.
			// This helps prevent view yanks when running forward past targets.
			float StrafePullScale = FMath::Abs(CurrentMoveInputValue.Y);

			PullRotation.Yaw *= StrafePullScale;
			PullRotation.Pitch *= StrafePullScale;
		}

		// Clamp the maximum amount of pull rotation to prevent it from yanking the player's view too much.
		// The clamped rate is scaled so it feels the same regardless of field of view.
		const float FOVScale = UAimAssistTargetManagerComponent::GetFOVScale(PC, ECommonInputType::Gamepad);
		const float PullMaxRotationRate = (Settings.PullMaxRotationRate.GetValue() * FOVScale);
		if (PullMaxRotationRate > 0.0f)
		{
			const float PullMaxRotation = (PullMaxRotationRate * DeltaTime);

			PullRotation.Yaw = FMath::Clamp(PullRotation.Yaw, -PullMaxRotation, PullMaxRotation);
			PullRotation.Pitch = FMath::Clamp(PullRotation.Pitch, -PullMaxRotation, PullMaxRotation);
		}

		RotationNeeded -= PullRotation;
		RotationalVelocity += (PullRotation * (1.0f / DeltaTime));
	}

	FRotator LookRates = GetLookRates(CurrentLookInputValue);

	// Apply slowing
	if (Settings.bApplySlowing && bIsApplyingLookInput && !FMath::IsNearlyZero(SlowStrength))
	{
		// The slowed rotation rate is a percentage of the normal look rotation rates.
		FRotator SlowRates = (LookRates * (1.0f - SlowStrength));

		const bool bUseDynamicSlow = true;

		if (Settings.bUseDynamicSlow)
		{
			const FRotator BoostRotation = (RotationNeeded * (1.0f / DeltaTime));

			const float YawDynamicBoost = (BoostRotation.Yaw * FMath::Sign(CurrentLookInputValue.X));
			if (YawDynamicBoost > 0.0f)
			{
				SlowRates.Yaw += YawDynamicBoost;
			}

			const float PitchDynamicBoost = (BoostRotation.Pitch * FMath::Sign(CurrentLookInputValue.Y));
			if (PitchDynamicBoost > 0.0f)
			{
				SlowRates.Pitch += PitchDynamicBoost;
			}
		}

		// Clamp the minimum amount of slow to prevent it from feeling sluggish on low sensitivity settings.
		// The clamped rate is scaled so it feels the same regardless of field of view.
		const float FOVScale = UAimAssistTargetManagerComponent::GetFOVScale(PC, ECommonInputType::Gamepad);
		const float SlowMinRotationRate = (Settings.SlowMinRotationRate.GetValue() * FOVScale);
		if (SlowMinRotationRate > 0.0f)
		{
			SlowRates.Yaw = FMath::Max(SlowRates.Yaw, SlowMinRotationRate);
			SlowRates.Pitch = FMath::Max(SlowRates.Pitch, SlowMinRotationRate);
		}

		// Make sure the slow rate isn't faster then our default.
		SlowRates.Yaw = FMath::Min(SlowRates.Yaw, LookRates.Yaw);
		SlowRates.Pitch = FMath::Min(SlowRates.Pitch, LookRates.Pitch);

		RotationalVelocity.Yaw += (CurrentLookInputValue.X * SlowRates.Yaw);
		RotationalVelocity.Pitch += (CurrentLookInputValue.Y * SlowRates.Pitch);
		RotationalVelocity.Roll = 0.0f;
	}
	else
	{
		RotationalVelocity.Yaw += (CurrentLookInputValue.X * LookRates.Yaw);
		RotationalVelocity.Pitch += (CurrentLookInputValue.Y * LookRates.Pitch);
		RotationalVelocity.Roll = 0.0f;
	}

	return RotationalVelocity;
}

void UAimAssistInputModifier::CalculateTargetStrengths(const FLyraAimAssistTarget& Target, float& OutPullStrength, float& OutSlowStrength) const
{
	const bool bIsADS = (TargetingType == ELyraTargetingType::ADS);

	if (Target.bUnderAssistInnerReticle)
	{
		if (bIsADS)
		{
			OutPullStrength = Settings.PullInnerStrengthAds.GetValue();
			OutSlowStrength = Settings.SlowInnerStrengthAds.GetValue();
		}
		else
		{
			OutPullStrength = Settings.PullInnerStrengthHip.GetValue();
			OutSlowStrength = Settings.SlowInnerStrengthHip.GetValue();
		}
	}
	else if (Target.bUnderAssistOuterReticle)
	{
		if (bIsADS)
		{
			OutPullStrength = Settings.PullOuterStrengthAds.GetValue();
			OutSlowStrength = Settings.SlowOuterStrengthAds.GetValue();
		}
		else
		{
			OutPullStrength = Settings.PullOuterStrengthHip.GetValue();
			OutSlowStrength = Settings.SlowOuterStrengthHip.GetValue();
		}
	}
	else
	{
		OutPullStrength = 0.0f;
		OutSlowStrength = 0.0f;
	}

	OutPullStrength *= Target.AssistWeight;
	OutSlowStrength *= Target.AssistWeight;
}

#if ENABLE_DRAW_DEBUG
void UAimAssistInputModifier::AimAssistDebugDraw(UCanvas* Canvas, APlayerController* PC)
{
	if (!Canvas || !OwnerViewData.IsDataValid() || !LyraConsoleVariables::bDrawAimAssistDebug)
	{
		return;
	}

	const bool bIsADS = (TargetingType == ELyraTargetingType::ADS);

	FDisplayDebugManager& DisplayDebugManager = Canvas->DisplayDebugManager;
	DisplayDebugManager.Initialize(Canvas, GEngine->GetSmallFont(), FVector2D((bIsADS ? 4.0f : 170.0f), 150.0f));
	DisplayDebugManager.SetDrawColor(FColor::Yellow);

	DisplayDebugManager.DrawString(FString(TEXT("------------------------------")));
	DisplayDebugManager.DrawString(FString(TEXT("Aim Assist Debug Draw")));
	DisplayDebugManager.DrawString(FString(TEXT("------------------------------")));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("Strength Scale: (%.4f)"), Settings.StrengthScale));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("Pull Strength: (%.4f)"), LastPullStrength));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("Slow Strength: (%.4f)"), LastSlowStrength));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("Look Rate Yaw: (%.4f)"), LastLookRateYaw));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("Look Rate Pitch: (%.4f)"), LastLookRatePitch));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("Baseline Value: (%.4f, %.4f, %.4f)"), LastBaselineValue.X, LastBaselineValue.Y, LastBaselineValue.Z));
	DisplayDebugManager.DrawString(FString::Printf(TEXT("Assisted Value: (%.4f, %.4f, %.4f)"), LastOutValue.X, LastOutValue.Y, LastOutValue.Z));


	UWorld* World = OwnerViewData.PlayerController->GetWorld();
	check(World);

	const FBox2D AssistInnerReticleBounds = OwnerViewData.ProjectReticleToScreen(Settings.AssistInnerReticleWidth.GetValue(), Settings.AssistInnerReticleHeight.GetValue(), Settings.ReticleDepth);
	const FBox2D AssistOuterReticleBounds = OwnerViewData.ProjectReticleToScreen(Settings.AssistOuterReticleWidth.GetValue(), Settings.AssistOuterReticleHeight.GetValue(), Settings.ReticleDepth);
	const FBox2D TargetingReticleBounds = OwnerViewData.ProjectReticleToScreen(Settings.TargetingReticleWidth.GetValue(), Settings.TargetingReticleHeight.GetValue(), Settings.ReticleDepth);

	if (TargetingReticleBounds.bIsValid)
	{
		FLinearColor ReticleColor(0.25f, 0.25f, 0.25f, 1.0f);
		DrawDebugCanvas2DBox(Canvas, TargetingReticleBounds, ReticleColor, 1.0f);
	}

	if (AssistInnerReticleBounds.bIsValid)
	{
		FLinearColor ReticleColor(0.0f, 0.0f, 1.0f, 0.2f);

		FCanvasTileItem ReticleTileItem(AssistInnerReticleBounds.Min, AssistInnerReticleBounds.GetSize(), ReticleColor);
		ReticleTileItem.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(ReticleTileItem);

		ReticleColor.A = 1.0f;
		DrawDebugCanvas2DBox(Canvas, AssistInnerReticleBounds, ReticleColor, 1.0f);
	}

	if (AssistOuterReticleBounds.bIsValid)
	{
		FLinearColor ReticleColor(0.25f, 0.25f, 1.0f, 0.2f);

		FCanvasTileItem ReticleTileItem(AssistOuterReticleBounds.Min, AssistOuterReticleBounds.GetSize(), ReticleColor);
		ReticleTileItem.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(ReticleTileItem);

		ReticleColor.A = 1.0f;
		DrawDebugCanvas2DBox(Canvas, AssistOuterReticleBounds, ReticleColor, 1.0f);
	}

	const TArray<FLyraAimAssistTarget>& TargetCache = GetCurrentTargetCache();
	for (const FLyraAimAssistTarget& Target : TargetCache)
	{
		if (Target.ScreenBounds.bIsValid)
		{
			FLinearColor TargetColor = ((Target.AssistWeight > 0.0f) ? FLinearColor::LerpUsingHSV(FLinearColor::Yellow, FLinearColor::Green, Target.AssistWeight) : FLinearColor::Black);
			TargetColor.A = 0.2f;

			FCanvasTileItem TargetTileItem(Target.ScreenBounds.Min, Target.ScreenBounds.GetSize(), TargetColor);
			TargetTileItem.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(TargetTileItem);

			if (Target.bIsVisible)
			{
				TargetColor.A = 1.0f;
				DrawDebugCanvas2DBox(Canvas, Target.ScreenBounds, TargetColor, 1.0f);
			}

			FCanvasTextItem TargetTextItem(FVector2D::ZeroVector, FText::FromString(FString::Printf(TEXT("Weight: %.2f\nDist: %.2f\nScore: %.2f\nTime: %.2f"), Target.AssistWeight, Target.ViewDistance, Target.SortScore, Target.AssistTime)), GEngine->GetSmallFont(), FLinearColor::White);
			TargetTextItem.EnableShadow(FLinearColor::Black);
			Canvas->DrawItem(TargetTextItem, FVector2D(FMath::CeilToFloat(Target.ScreenBounds.Min.X), FMath::CeilToFloat(Target.ScreenBounds.Min.Y)));
		}
	}
}
#endif	// ENABLE_DRAW_DEBUG

```
