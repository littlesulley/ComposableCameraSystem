// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraOcclusionFadeNode.h"

#include "CollisionQueryParams.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DataAssets/ComposableCameraTargetInfo.h"
#include "Engine/HitResult.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

#if !UE_BUILD_SHIPPING
#include "Cameras/ComposableCameraCameraBase.h"
#include "Debug/ComposableCameraDebugDrawSink.h"
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// Per-node opt-in toggle. Gated by master `CCS.Debug.Viewport`.
	static TAutoConsoleVariable<int32> CVarShowOcclusionFadeGizmo(
		TEXT("CCS.Debug.Viewport.OcclusionFade"),
		0,
		TEXT("Show OcclusionFadeNode gizmo: red endpoint sphere at target (always), red sweep line camera-to-target (F8 / SIE only), cyan proximity sphere at camera (always).\n")
		TEXT("Requires `CCS.Debug.Viewport 1`."),
		ECVF_Default);
}
#endif

void UComposableCameraOcclusionFadeNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	// Clear anything left over from a previous activation. A re-used node
	// UObject could still hold records to destroyed components, and we want
	// every activation to start from a clean slate.
	RestoreAllOverrides();
	PendingSweepHandle = FTraceHandle{};

	// One-shot misconfiguration warning: a node without an OcclusionMaterial
	// silently does nothing at runtime; make the root cause visible to the
	// asset author instead of leaving them chasing "why isn't anything fading".
	if (!OcclusionMaterial)
	{
		UE_LOG(LogComposableCameraSystem, Warning,
			TEXT("OcclusionFadeNode: OcclusionMaterial is null on '%s'; node will no-op until one is assigned."),
			*GetNameSafe(this));
	}
}

void UComposableCameraOcclusionFadeNode::BeginDestroy()
{
	// Mandatory cleanup: any object still swapped to OcclusionMaterial when
	// this node is destroyed would remain transparent forever. Restore now.
	RestoreAllOverrides();

	Super::BeginDestroy();
}

void UComposableCameraOcclusionFadeNode::OnTickNode_Implementation(
	float /*DeltaTime*/, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	// World resolves through the owning camera (an AActor), which works both
	// with and without a PCM. Important for the Level Sequence authoring
	// path where PCM is null.
	UWorld* World = GetOwningCamera() ? GetOwningCamera()->GetWorld() : nullptr;
	if (!World || !OcclusionMaterial)
	{
		// No world or no transparency material. Node is a no-op this tick.
		// The null-material case was already logged at OnInitialize.
		return;
	}

	// -- This is a position-agnostic node; OutCameraPose is passed through
	// untouched. Read camera position from the upstream pose. --
	const FVector CameraPos = OutCameraPose.Position;
	LastCameraPosition = CameraPos;

	FVector TargetPos = CameraPos;  // fallback if target resolve fails
	const bool bHasTarget = ResolveTargetPoint(TargetPos);
	LastResolvedTargetPoint = TargetPos;

	// -- Gather this frame's desired-to-fade set from both detection paths.
	// DesiredFadedScratch is a member-scoped TSet. Reset on entry clears
	// any leftover from a (defensive: shouldn't happen) prior abnormal
	// exit, then Reserve to the empirical >=6 steady-state size so the
	// internal bucket array sits at full capacity from frame two onward.
	// Reset (NOT Empty) keeps the existing allocation. --
	DesiredFadedScratch.Reset();
	DesiredFadedScratch.Reserve(16);

	// Scenario A: consume last frame's sweep and submit a fresh one.
	if (bFadeOccluders && bHasTarget)
	{
		ConsumePendingSweep(World, DesiredFadedScratch);
		SubmitOcclusionSweep(World, CameraPos, TargetPos);
	}
	else
	{
		// Detection disabled or target missing. Drop any in-flight sweep
		// handle so we don't consume a stale result on the next enabled frame.
		PendingSweepHandle = FTraceHandle{};
	}

	// Scenario B: synchronous overlap at the camera position.
	if (bFadeNearbyActors)
	{
		RunProximityQuery(World, CameraPos, DesiredFadedScratch);
	}

	// -- Delta against AppliedMaterialOverrides: restore components that left
	// the set, then apply to components that just entered. Iterate backwards
	// because RestoreAndRemoveOverrideAt does a RemoveAtSwap. --
	for (int32 i = AppliedMaterialOverrides.Num() - 1; i >= 0; --i)
	{
		UPrimitiveComponent* Comp = AppliedMaterialOverrides[i].Component.Get();
		if (!Comp || !DesiredFadedScratch.Contains(Comp))
		{
			RestoreAndRemoveOverrideAt(i);
		}
	}

	for (UPrimitiveComponent* Comp : DesiredFadedScratch)
	{
		ApplyOcclusionMaterial(Comp);
	}

	// -- End-of-tick clear. See lifetime contract on DesiredFadedScratch.
	// Raw UPrimitiveComponent* entries must NOT live across a GC sweep;
	// Reset() drops them while keeping the bucket-array allocation hot
	// for the next tick.
	DesiredFadedScratch.Reset();
}

bool UComposableCameraOcclusionFadeNode::ResolveTargetPoint(FVector& OutTargetPoint) const
{
	// Shared target-info resolution keeps bone / offset fallback semantics
	// consistent with other camera nodes. Three cases:
	//   1. Bone mode + valid bone   -> socket location only, no Z offset.
	//   2. Bone mode + invalid bone ->fall back to ActorLocation + Z offset.
	//   3. Actor mode->ActorLocation + Z offset.
	// The struct's Offset is set to ZeroVector and the legacy Z offset is
	// added by THIS call site only when ResolveWorldPoint reports it did
	// NOT use the bone path (OutUsedBone == false). This preserves the
	// original "Z offset applies only on the actor branch" semantic exactly.
	FComposableCameraTargetInfo Info;
	// Explicit `.Get()` makes the TObjectPtr -> AActor* -> TSoftObjectPtr
	// conversion chain unambiguous.
	Info.Actor               = ComposableCameraSystem::ResolveActorInput(
		PivotActorSource, PivotActor.Get(), GetOwningPlayerCameraManager(), this);
	Info.bUseBoneAsPivot     = bUseBoneForDetection;
	Info.BoneName            = BoneName;
	Info.Offset              = FVector::ZeroVector;
	Info.bOffsetInLocalSpace = false;

	bool bUsedBone = false;
	if (!Info.ResolveWorldPoint(OutTargetPoint, &bUsedBone))
	{
		return false;
	}

	if (!bUsedBone)
	{
		OutTargetPoint += FVector(0.f, 0.f, PivotZOffset);
	}
	return true;
}

void UComposableCameraOcclusionFadeNode::ConsumePendingSweep(
	UWorld* World, TSet<UPrimitiveComponent*>& OutFadableComponents)
{
	if (!PendingSweepHandle.IsValid())
	{
		return;
	}

	FTraceDatum Datum;
	if (!World->QueryTraceData(PendingSweepHandle, Datum))
	{
		// Result not ready yet. Single-frame stall. We'll pick it up next
		// tick. Don't invalidate the handle.
		return;
	}

	for (const FHitResult& Hit : Datum.OutHits)
	{
		UPrimitiveComponent* Comp = Hit.GetComponent();
		if (PassesFadeFilters(Comp, /*bApplyOccluderTagFilter=*/true))
		{
			OutFadableComponents.Add(Comp);
		}
	}

	PendingSweepHandle = FTraceHandle{};
}

void UComposableCameraOcclusionFadeNode::SubmitOcclusionSweep(
	UWorld* World, const FVector& CameraPos, const FVector& TargetPos)
{
	static const FName OcclusionTraceTag(TEXT("ComposableCameraOcclusion"));
	static const FName OcclusionTraceOwnerTag(TEXT("OcclusionFadeNode"));

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ComposableCameraOcclusionFadeSweep), false);
	Params.TraceTag = OcclusionTraceTag;
	Params.OwnerTag = OcclusionTraceOwnerTag;

	// Auto-ignore the PivotActor. The sweep terminates at its location, and
	// if we didn't ignore it, the target itself would light up as the first
	// "occluder" every frame.
	AActor* EffectivePivotActor = ComposableCameraSystem::ResolveActorInput(
		PivotActorSource, PivotActor.Get(), GetOwningPlayerCameraManager(), this);
	if (EffectivePivotActor)
	{
		Params.AddIgnoredActor(EffectivePivotActor);
	}
	for (const TObjectPtr<AActor>& Extra : ExtraIgnoredActors)
	{
		if (Extra)
		{
			Params.AddIgnoredActor(Extra.Get());
		}
	}

	const FCollisionShape SweepShape = FCollisionShape::MakeSphere(FMath::Max(OcclusionSphereRadius, 0.f));

	PendingSweepHandle = World->AsyncSweepByChannel(
		EAsyncTraceType::Multi,
		CameraPos, TargetPos, FQuat::Identity,
		OcclusionChannel.GetValue(),
		SweepShape,
		Params,
		FCollisionResponseParams::DefaultResponseParam);

#if !UE_BUILD_SHIPPING
	DebugSweepStart = CameraPos;
	DebugSweepEnd = TargetPos;
	bDebugSweepSubmittedThisTick = true;
#endif
}

void UComposableCameraOcclusionFadeNode::RunProximityQuery(
	UWorld* World, const FVector& CameraPos, TSet<UPrimitiveComponent*>& OutFadableComponents)
{
	// Default to APawn when the class is unset. Matches the common
	// "characters and NPCs" intent without requiring configuration.
	const UClass* EffectiveClass = ProximityActorClass ? *ProximityActorClass : APawn::StaticClass();

	// Member-scoped scratch. Reset (not Empty) keeps the array allocation
	// hot across ticks. OverlapMultiByObjectType appends to OutOverlaps so
	// we must clear before the call. FOverlapResult uses TWeakObjectPtr
	// internally->GC-safe even if entries linger transiently.
	ProximityOverlapsScratch.Reset();
	const FCollisionShape Shape = FCollisionShape::MakeSphere(FMath::Max(ProximityRadius, 0.f));

	// Use an object-type query on ECC_Pawn. That's what Pawn-derived actors
	// register on in stock project settings, and it's the cheapest way to
	// avoid paging in every static prop near the camera. Non-pawn proximity
	// targets are rare; if ever needed, widen the query to an additional
	// channel and filter by class below.
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ComposableCameraOcclusionFadeProximity), false);
	AActor* EffectivePivotActor = ComposableCameraSystem::ResolveActorInput(
		PivotActorSource, PivotActor.Get(), GetOwningPlayerCameraManager(), this);
	if (EffectivePivotActor && bIgnorePivotActorInProximity)
	{
		QueryParams.AddIgnoredActor(EffectivePivotActor);
	}
	for (const TObjectPtr<AActor>& Extra : ExtraIgnoredActors)
	{
		if (Extra)
		{
			QueryParams.AddIgnoredActor(Extra.Get());
		}
	}

	World->OverlapMultiByObjectType(ProximityOverlapsScratch, CameraPos, FQuat::Identity, ObjParams, Shape, QueryParams);

	for (const FOverlapResult& Overlap : ProximityOverlapsScratch)
	{
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || !HitActor->IsA(EffectiveClass))
		{
			continue;
		}
		CollectFadableComponentsOnActor(HitActor, OutFadableComponents);
	}
}

bool UComposableCameraOcclusionFadeNode::PassesFadeFilters(
	UPrimitiveComponent* Component, bool bApplyOccluderTagFilter) const
{
	if (!Component)
	{
		return false;
	}

	// Mesh-type gates (per-switch).
	const bool bIsSkeletal = Component->IsA<USkeletalMeshComponent>();
	const bool bIsStatic   = Component->IsA<UStaticMeshComponent>();
	if (bIsSkeletal && !bAffectSkeletalMeshes) { return false; }
	if (bIsStatic   && !bAffectStaticMeshes)   { return false; }
	if (!bIsSkeletal && !bIsStatic)
	{
		// Other primitive kinds (instanced, geometry collection) pass through
		// both toggles. There's no separate switch today. If a project wants
		// to gate those, split the switches when the need actually appears.
	}

	// Component-tag filter. Sweep-only, proximity skips it.
	if (bApplyOccluderTagFilter && !OccluderComponentTag.IsNone())
	{
		if (!Component->ComponentHasTag(OccluderComponentTag))
		{
			return false;
		}
	}

	return true;
}

void UComposableCameraOcclusionFadeNode::CollectFadableComponentsOnActor(
	AActor* Actor, TSet<UPrimitiveComponent*>& Out) const
{
	if (!Actor) { return; }

	// TInlineAllocator<8> stores the first 8 elements inline on the stack.
	// Most pawns carry a handful of mesh components, so the heap is never
	// touched in the common case. AActor::GetComponents<T> accepts an
	// allocator-templated TArray since UE 4.18.
	TArray<UPrimitiveComponent*, TInlineAllocator<8>> PrimComps;
	Actor->GetComponents<UPrimitiveComponent>(PrimComps);
	for (UPrimitiveComponent* Comp : PrimComps)
	{
		if (PassesFadeFilters(Comp, /*bApplyOccluderTagFilter=*/false))
		{
			Out.Add(Comp);
		}
	}
}

void UComposableCameraOcclusionFadeNode::ApplyOcclusionMaterial(UPrimitiveComponent* Component)
{
	if (!Component || !OcclusionMaterial)
	{
		return;
	}

	// Skip if this component is already recorded.
	for (const FComposableCameraOcclusionMaterialOverride& Existing : AppliedMaterialOverrides)
	{
		if (Existing.Component.Get() == Component)
		{
			return;
		}
	}

	FComposableCameraOcclusionMaterialOverride Record;
	Record.Component = Component;

	const int32 NumSlots = Component->GetNumMaterials();
	Record.OriginalMaterials.Reserve(NumSlots);
	Record.OverrideMaterials.Reserve(NumSlots);

	for (int32 SlotIdx = 0; SlotIdx < NumSlots; ++SlotIdx)
	{
		UMaterialInterface* Original = Component->GetMaterial(SlotIdx);
		Record.OriginalMaterials.Add(Original);

		// CreateDynamicMaterialInstance(slot, source) both creates the MID
		// wrapping the source (our OcclusionMaterial) AND sets it as the
		// component's slot material. We just capture the returned MID for
		// symmetry / GC pinning.
		UMaterialInstanceDynamic* MID = Component->CreateDynamicMaterialInstance(SlotIdx, OcclusionMaterial);
		Record.OverrideMaterials.Add(MID);
	}

	AppliedMaterialOverrides.Add(MoveTemp(Record));
}

void UComposableCameraOcclusionFadeNode::RestoreAndRemoveOverrideAt(int32 Index)
{
	if (!AppliedMaterialOverrides.IsValidIndex(Index))
	{
		return;
	}

	FComposableCameraOcclusionMaterialOverride& Record = AppliedMaterialOverrides[Index];
	if (UPrimitiveComponent* Comp = Record.Component.Get())
	{
		// Write originals back in their slot order. Guards against a rare case
		// where the component grew / shrank its material array while we held
		// the override. We only restore up to the lesser count, leaving any
		// newly-added slots with whatever they currently hold.
		const int32 NumToRestore = FMath::Min(Record.OriginalMaterials.Num(), Comp->GetNumMaterials());
		for (int32 SlotIdx = 0; SlotIdx < NumToRestore; ++SlotIdx)
		{
			Comp->SetMaterial(SlotIdx, Record.OriginalMaterials[SlotIdx]);
		}
	}

	AppliedMaterialOverrides.RemoveAtSwap(Index, EAllowShrinking::No);
}

void UComposableCameraOcclusionFadeNode::RestoreAllOverrides()
{
	for (int32 i = AppliedMaterialOverrides.Num() - 1; i >= 0; --i)
	{
		RestoreAndRemoveOverrideAt(i);
	}
}

void UComposableCameraOcclusionFadeNode::GetPinDeclarations_Implementation(
	TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	// Input: pivot actor source.
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotActorSource";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "OccFade_PivotActorSource", "Pivot Actor Source");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Enum;
		Pin.EnumType = StaticEnum<EComposableCameraActorInputSource>();
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = Pin.EnumType ? Pin.EnumType->GetNameStringByValue(static_cast<int64>(PivotActorSource)) : FString();
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "OccFade_PivotActorSource_Tip",
			"Selects whether the fade target comes from the controller's controlled pawn or an explicit actor.");
		OutPins.Add(Pin);
	}

	// Input: pivot actor (the line-of-sight target and proximity centre).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "PivotActor";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "OccFade_PivotActor", "Pivot Actor");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Actor;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "OccFade_PivotActor_Tip",
			"Actor whose location (+ PivotZOffset or BoneName) is the line-of-sight target and the auto-ignored pawn.");
		OutPins.Add(Pin);
	}

	// Input: occlusion material (often driven by a game-logic source).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "OcclusionMaterial";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "OccFade_Material", "Occlusion Material");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Object;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "OccFade_Material_Tip",
			"Material swapped onto each faded primitive's slots. Fade look and timing live in this material's shader.");
		OutPins.Add(Pin);
	}

	// Input: sphere radius for the sweep (often tuned live by gameplay).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "OcclusionSphereRadius";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "OccFade_Radius", "Occlusion Sphere Radius");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(OcclusionSphereRadius);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "OccFade_Radius_Tip",
			"Radius of the async sphere sweep used to detect line-of-sight occluders.");
		OutPins.Add(Pin);
	}

	// Input: proximity radius (commonly gameplay-driven, e.g. zoom level).
	{
		FComposableCameraNodePinDeclaration Pin;
		Pin.PinName = "ProximityRadius";
		Pin.DisplayName = NSLOCTEXT("ComposableCameraSystem", "OccFade_ProxRadius", "Proximity Radius");
		Pin.Direction = EComposableCameraPinDirection::Input;
		Pin.PinType = EComposableCameraPinType::Float;
		Pin.bRequired = false;
		Pin.bDefaultAsPin = false;
		Pin.DefaultValueString = FString::SanitizeFloat(ProximityRadius);
		Pin.Tooltip = NSLOCTEXT("ComposableCameraSystem", "OccFade_ProxRadius_Tip",
			"Radius of the sphere-overlap around the camera for proximity fade.");
		OutPins.Add(Pin);
	}
}

#if !UE_BUILD_SHIPPING
void UComposableCameraOcclusionFadeNode::DrawNodeDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const
{
	if (CVarShowOcclusionFadeGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()
		&& !Draw.ShouldForceDrawAllNodeGizmos()) { return; }

	// Sweep visualisation, split into two pieces that gate independently.
	// Same pattern as CollisionPushNode's pivot-sphere + trace-line split:
	//
	//  * Endpoint sphere at the target (on the character's head, if
	//    bUseBoneForDetection). Always drawn. This is the "target sphere"
	//    that answers "where exactly is the protected point?" and reads
	//    fine in both possessed play and F8 since it sits out in the world.
	//  * Line from camera to target: F8 / SIE only. The line axis matches
	//    the view axis in possessed play, projecting to near-zero screen
	//    length; see TechDoc Section 3.20.2 "view-aligned lines".
	if (bFadeOccluders && bDebugSweepSubmittedThisTick)
	{
		const FColor SweepColor = FComposableCameraViewportDebugColors::OcclusionFadeSweep();

		Draw.DrawSphere(
			DebugSweepEnd, /*Radius=*/FMath::Max(OcclusionSphereRadius, 4.f),
			SweepColor, /*Alpha=*/80, /*DepthPriority=*/0, /*bSolid=*/true,
			/*Segments=*/12, /*Thickness=*/0.0f, TEXT("Occlusion Target"));

		if (bViewerIsOutsideCamera)
		{
			Draw.DrawLine(DebugSweepStart, DebugSweepEnd, SweepColor, /*Thickness=*/1.0f, /*DepthPriority=*/0);
		}
	}

	// Proximity sphere: cyan translucent wireframe centred on the camera.
	// Stays visible in both possessed play and F8. The sphere is a volume
	// around the camera, not a line, so it reads from either viewpoint.
	if (bFadeNearbyActors)
	{
		const FColor ProximityColor = FComposableCameraViewportDebugColors::OcclusionFadeProximity();
		Draw.DrawSphere(
			LastCameraPosition, FMath::Max(ProximityRadius, 4.f),
			ProximityColor, /*Alpha=*/50, /*DepthPriority=*/0, /*bSolid=*/true,
			/*Segments=*/16, /*Thickness=*/0.0f, TEXT("Occlusion Proximity"));
	}
}
#endif
