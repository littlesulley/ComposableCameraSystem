// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraCollisionPushNode.h"

#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "CollisionShape.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "KismetTraceUtils.h"
#include "Kismet/KismetSystemLibrary.h"

#if !UE_BUILD_SHIPPING
#include "Debug/ComposableCameraDebugDrawSink.h"
#include "Debug/ComposableCameraViewportDebug.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarShowCollisionPushGizmo(
		TEXT("CCS.Debug.Viewport.CollisionPush"),
		0,
		TEXT("Show CollisionPushNode gizmos. Possessed play: trace sphere at pivot\n")
		TEXT("(green = clear, red = blocked) + hit sphere if blocked. F8 eject:\n")
		TEXT("adds the full pivot -> camera trace line and the self-collision\n")
		TEXT("sphere around the camera. Requires `CCS.Debug.Viewport 1`."),
		ECVF_Default);
}
#endif

namespace
{
	// Lazy-resolve the SkelMesh on `Actor` only when it differs from what the
	// cache last resolved against. PivotActor is an input pin so the active
	// actor can change every frame; resolving lazily avoids per-frame
	// `GetComponentByClass` walks while still picking up actor swaps and
	// component churn.
	static void ResolveSkelMeshForPivotActor(
		AActor* Actor,
		TWeakObjectPtr<USkeletalMeshComponent>& InOutSkelMesh,
		TWeakObjectPtr<AActor>& InOutLastResolvedActor)
	{
		if (!IsValid(Actor))
		{
			InOutSkelMesh.Reset();
			InOutLastResolvedActor.Reset();
			return;
		}
		if (InOutLastResolvedActor.Get() == Actor && InOutSkelMesh.IsValid())
		{
			return; // cache hit
		}
		InOutLastResolvedActor = Actor;
		InOutSkelMesh = Actor->GetComponentByClass<USkeletalMeshComponent>();
	}
}

void UComposableCameraCollisionPushNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	// Subobject pin values (e.g. PushInterpolator.Speed) are auto-applied by
	// the base class in Initialize(), before this OnInitialize runs.

	PushInterpolator_T = IsValid(PushInterpolator) ? PushInterpolator->BuildDoubleInterpolator() : nullptr;
	PullInterpolator_T = IsValid(PullInterpolator) ? PullInterpolator->BuildDoubleInterpolator() : nullptr;

	// Don't resolve the SkelMesh component here. PivotActor can be driven
	// by an input pin and change every frame. Resolution happens lazily in
	// Tick when the active PivotActor differs from `LastResolvedPivotActor`.
	SkeletalMeshComponentForPivotActor.Reset();
	LastResolvedPivotActor.Reset();
	bHasOriginalCameraPosition = false;

	// Snapshot the ignore-list once at activation. Earlier behavior was a
	// per-Tick `GetAllActorsOfClass` walk that scaled with the entire
	// world's actor count for every camera tick. Snapshotting here means
	// dynamically-spawned ignore-class actors created AFTER initialization
	// won't be in the list. Acceptable trade because (a) typical use is
	// "ignore the player capsule / specific level geo", which is stable
	// for the camera's lifetime, and (b) the snapshot uses TWeakObjectPtr
	// so destroyed entries silently drop on per-tick rebuild rather than
	// leaving dangling raw pointers in the trace ignore list.
	ActorsToIgnoreWeak.Reset();
	ResolvedActorsToIgnore.Reset();
	for (const TSoftClassPtr<AActor>& ActorType : ActorTypesToIgnore)
	{
		if (!ActorType.IsValid())
		{
			continue;
		}
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(this, ActorType.Get(), Found);
		ActorsToIgnoreWeak.Reserve(ActorsToIgnoreWeak.Num() + Found.Num());
		for (AActor* Ignored : Found)
		{
			ActorsToIgnoreWeak.Add(Ignored);
		}
	}
}

void UComposableCameraCollisionPushNode::OnTickNode_Implementation(float DeltaTime,
                                                                   const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	OriginalCameraPosition = CurrentCameraPose.Position;
	bHasOriginalCameraPosition = true;
	FVector PivotPosition = FVector::ZeroVector;

	// PivotActor / bUseBoneForDetection / PivotZOffset / SelfSphereDistanceOffsetFromCenter /
	// ExtraPushDistance are pin-matched UPROPERTYs. Resolved by the base TickNode
	// prologue. Refresh the SkelMesh cache against the just-written PivotActor
	// before reading either branch.
	AActor* InPivotActor = ComposableCameraSystem::ResolveActorInput(
		PivotActorSource, PivotActor.Get(), GetOwningPlayerCameraManager(), this);
	ResolveSkelMeshForPivotActor(InPivotActor, SkeletalMeshComponentForPivotActor, LastResolvedPivotActor);

	USkeletalMeshComponent* PivotSkelMesh = SkeletalMeshComponentForPivotActor.Get();
	if (bUseBoneForDetection && IsValid(PivotSkelMesh))
	{
		PivotPosition = PivotSkelMesh->GetSocketLocation(BoneName);
	}
	else
	{
		if (!IsValid(InPivotActor))
		{
			UE_LOG(LogComposableCameraSystem, Warning, TEXT("Cannot find a valid pivot actor for collision push."))
			return;
		}
		PivotPosition = InPivotActor->GetActorLocation() + FVector(0, 0, PivotZOffset);
	}
	
	if (UWorld* World = GetWorld())
	{
		FComposableCameraHitResult HitResult = FindCollisionPoint(DeltaTime, PivotPosition, CurrentCameraPose.Position, CurrentCameraPose.Rotation);

		FVector OutCameraPosition;
		
		if (HitResult.bHasHit)
		{
			OutCameraPosition = StartResolveCollision(DeltaTime, HitResult.HitTargetLocation, CurrentCameraPose.Position);
		}
		else
		{
			OutCameraPosition = ResumeFromCollision(DeltaTime, PivotPosition, CurrentCameraPose.Position);
		}

		OutCameraPose.Position = OutCameraPosition;
	}
}

void UComposableCameraCollisionPushNode::OnPreTick(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	Super::OnPreTick(DeltaTime,  CurrentCameraPose, OutCameraPose);

	if (!bHasOriginalCameraPosition)
	{
		return;
	}

	OutCameraPose.Position = OriginalCameraPosition;

	if (OwningCamera)
	{
		OwningCamera->CameraPose.Position = OriginalCameraPosition;
	}
}

void UComposableCameraCollisionPushNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	FComposableCameraNodePinDeclaration PinDecl;

	// PivotActorSource Input
	PinDecl.PinName = TEXT("PivotActorSource");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraCollisionPushNode", "PivotActorSource", "Pivot Actor Source");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Enum;
	PinDecl.EnumType = StaticEnum<EComposableCameraActorInputSource>();
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(PivotActorSource)) : FString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraCollisionPushNode", "PivotActorSourceTip", "Selects whether the collision pivot comes from the controller's controlled pawn or an explicit actor.");
	OutPins.Add(PinDecl);

	// PivotActor Input
	PinDecl = {};
	PinDecl.PinName = TEXT("PivotActor");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraCollisionPushNode", "PivotActor", "Pivot Actor");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Actor;
	PinDecl.bRequired = false;
	PinDecl.DefaultValueString = FString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraCollisionPushNode", "PivotActorTip", "The actor from which to retrieve collision detection target position.");
	OutPins.Add(PinDecl);

	// SelfSphereDistanceOffsetFromCenter Input
	PinDecl = {};
	PinDecl.PinName = TEXT("SelfSphereDistanceOffsetFromCenter");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraCollisionPushNode", "SelfSphereDistOffset", "Self Sphere Distance Offset");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Double;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = FString::SanitizeFloat(SelfSphereDistanceOffsetFromCenter);
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraCollisionPushNode", "SelfSphereDistOffsetTip",
		"Distance along the camera's forward axis between the self-collision sphere center and the camera position.");
	OutPins.Add(PinDecl);

	// ExtraPushDistance Input
	PinDecl = {};
	PinDecl.PinName = TEXT("ExtraPushDistance");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraCollisionPushNode", "ExtraPushDistance", "Extra Push Distance");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Double;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = FString::SanitizeFloat(ExtraPushDistance);
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraCollisionPushNode", "ExtraPushDistanceTip",
		"Additional distance the camera is pushed toward the pivot after a collision is resolved.");
	OutPins.Add(PinDecl);

	// PivotZOffset Input
	PinDecl = {};
	PinDecl.PinName = TEXT("PivotZOffset");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraCollisionPushNode", "PivotZOffset", "Pivot Z Offset");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Double;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = FString::SanitizeFloat(PivotZOffset);
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraCollisionPushNode", "PivotZOffsetTip",
		"World-space Z offset added to the pivot actor's location when sourcing the collision origin.");
	OutPins.Add(PinDecl);

	// bUseBoneForDetection Input
	PinDecl = {};
	PinDecl.PinName = TEXT("bUseBoneForDetection");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraCollisionPushNode", "UseBoneForDetection", "Use Bone For Detection");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Bool;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = bUseBoneForDetection ? TEXT("true") : TEXT("false");
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraCollisionPushNode", "UseBoneForDetectionTip",
		"When true, use the named bone on the pivot actor's skeletal mesh as the collision detection origin.");
	OutPins.Add(PinDecl);

	// BoneName Input
	PinDecl = {};
	PinDecl.PinName = TEXT("BoneName");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraCollisionPushNode", "BoneName", "Bone Name");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Name;
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = BoneName.ToString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraCollisionPushNode", "BoneNameTip",
		"Name of the bone on the pivot actor's skeletal mesh used as the collision detection origin when bUseBoneForDetection is true.");
	OutPins.Add(PinDecl);

	// TraceCollisionChannel Input
	PinDecl = {};
	PinDecl.PinName = TEXT("TraceCollisionChannel");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraCollisionPushNode", "TraceCollisionChannel", "Trace Collision Channel");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Enum;
	PinDecl.EnumType = StaticEnum<ETraceTypeQuery>();
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(TraceCollisionChannel.GetValue())) : FString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraCollisionPushNode", "TraceCollisionChannelTip",
		"Collision channel used by the pivot-to-camera occlusion trace.");
	OutPins.Add(PinDecl);

	// SelfCollisionChannel Input
	PinDecl = {};
	PinDecl.PinName = TEXT("SelfCollisionChannel");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraCollisionPushNode", "SelfCollisionChannel", "Self Collision Channel");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Enum;
	PinDecl.EnumType = StaticEnum<ETraceTypeQuery>();
	PinDecl.bRequired = false;
	PinDecl.bDefaultAsPin = false;
	PinDecl.DefaultValueString = PinDecl.EnumType ? PinDecl.EnumType->GetNameStringByValue(static_cast<int64>(SelfCollisionChannel.GetValue())) : FString();
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraCollisionPushNode", "SelfCollisionChannelTip",
		"Collision channel used by the self-collision sphere sweep around the camera.");
	OutPins.Add(PinDecl);

	// Subobject pins (e.g. PushInterpolator.Speed) are auto-appended by
	// GatherAllPinDeclarations in the base class, no manual calls needed.
}


FComposableCameraHitResult UComposableCameraCollisionPushNode::FindCollisionPoint(double DeltaTime,  const FVector& PivotPosition, const FVector& CameraPosition, const FRotator& CameraRotation)
{
	// Do trace collision point first.
	const FVector Start = PivotPosition;
	const FVector End = CameraPosition;
	FVector Direction = Start - End;
	Direction.Normalize();
	
	// Rebuild the raw ignore-list from the cached weak snapshot taken at
	// OnInitialize. Reuses `ResolvedActorsToIgnore`'s heap capacity across
	// frames (Reset preserves it) so steady-state operation is allocation-
	// free. Actors that have been destroyed since OnInitialize drop out
	// silently via the TWeakObjectPtr resolve. The previous code called
	// `GetAllActorsOfClass` every frame, which scaled with the entire
	// world's actor count.
	ResolvedActorsToIgnore.Reset(ActorsToIgnoreWeak.Num());
	for (const TWeakObjectPtr<AActor>& Weak : ActorsToIgnoreWeak)
	{
		if (AActor* Live = Weak.Get(); IsValid(Live))
		{
			ResolvedActorsToIgnore.Add(Live);
		}
	}

	FHitResult TraceCollisionHit;
	// Trace-builtin debug draw is intentionally disabled. Visualisation is
	// now routed through `UComposableCameraCameraNodeBase::DrawNodeDebug`
	// (enabled via the `CCS.Debug.Viewport` CVar + F8 eject), using the
	// cached trace state below. Letting KismetSystemLibrary paint its own
	// debug here would stack two independent draw paths for the same trace.
	constexpr EDrawDebugTrace::Type DrawDebugType = EDrawDebugTrace::None;

	if (bTraceUseSphere)
	{
		UKismetSystemLibrary::SphereTraceSingle(this, Start, End, TraceSphereRadius, TraceCollisionChannel, true, ResolvedActorsToIgnore, DrawDebugType, TraceCollisionHit, true);
	}
	else
	{
		UKismetSystemLibrary::LineTraceSingle(this, Start, End, TraceCollisionChannel, true, ResolvedActorsToIgnore, DrawDebugType, TraceCollisionHit, true);
	}

	// Gather trace collision information and do self collision.
	FVector PendingTargetPosition = CameraPosition;
	if (TraceCollisionHit.bBlockingHit)
	{
		ElapsedExemptionTime = FMath::Min(TraceOcclusionExemptionTime, ElapsedExemptionTime + DeltaTime);

		if (ElapsedExemptionTime >= TraceOcclusionExemptionTime)
		{
			PendingTargetPosition = TraceCollisionHit.Location;
			PendingTargetPosition += Direction * ExtraPushDistance;
		}
	}
	else
	{
		ElapsedExemptionTime = 0.;
	}

	FVector SelfCollisionStart = PendingTargetPosition + CameraRotation.RotateVector(FVector::ForwardVector) * SelfSphereDistanceOffsetFromCenter;
	FVector SelfCollisionEnd = PendingTargetPosition + CameraRotation.RotateVector(FVector::ForwardVector + FVector{0.1, 0., 0 }) * SelfSphereDistanceOffsetFromCenter;
	
	FHitResult SelfCollisionHit;
	UKismetSystemLibrary::SphereTraceSingle(this, SelfCollisionStart, SelfCollisionEnd, SelfSphereRadius, SelfCollisionChannel, false, ResolvedActorsToIgnore, DrawDebugType, SelfCollisionHit, true);

	if (SelfCollisionHit.bBlockingHit)
	{
		AActor* HitActor = SelfCollisionHit.GetActor();
		
		SelfCollisionStart = PivotPosition;
		TArray<FHitResult> SelfHitResults;

		FCollisionQueryParams QueryParams =  FCollisionQueryParams::DefaultQueryParam;
		QueryParams.AddIgnoredActors(ResolvedActorsToIgnore);
		QueryParams.bTraceComplex = true;

		AActor* IgnoredSelf = nullptr;
		{
			UObject* CurrentObject = this;
			while (CurrentObject)
			{
				CurrentObject = CurrentObject->GetOuter();
				IgnoredSelf = Cast<AActor>(CurrentObject);
				if (IgnoredSelf)
				{
					QueryParams.AddIgnoredActor(IgnoredSelf);
					break;
				}
			}
		}

		FCollisionResponseParams ResponseParams { ECR_Overlap };
		
		GetWorld()->SweepMultiByChannel(
			SelfHitResults,
			SelfCollisionStart,
			SelfCollisionEnd,
			FQuat::Identity,
			UEngineTypes::ConvertToCollisionChannel(SelfCollisionChannel),
			FCollisionShape::MakeSphere(SelfSphereRadius),
			QueryParams,
			ResponseParams);

		for (const FHitResult& HitResult : SelfHitResults)
		{
			if (HitResult.GetActor() == HitActor)
			{
				Direction = SelfCollisionStart - SelfCollisionEnd;
				Direction.Normalize();
				PendingTargetPosition = HitResult.Location;
				PendingTargetPosition += Direction * ExtraPushDistance;
				break;
			}
		}
	}

#if !UE_BUILD_SHIPPING
	LastTraceStart       = Start;
	LastTraceEnd         = End;
	LastSelfSphereCenter = SelfCollisionStart;
	bLastTraceBlocked    = TraceCollisionHit.bBlockingHit;
	LastTraceHitLocation = TraceCollisionHit.bBlockingHit ? TraceCollisionHit.Location : End;
#endif

	// Drop raw AActor* pointers before returning. `ResolvedActorsToIgnore`
	// is a non-UPROPERTY UObject-pointer container (UCLASS member); GC sweeps
	// between frames cannot see raw pointers stored here, so a stale pointer
	// from a since-destroyed actor would dangle invisibly. Reset() preserves
	// the underlying TArray capacity so the next-frame rebuild stays alloc-
	// free, while leaving the array empty whenever execution is outside this
	// function.
	ResolvedActorsToIgnore.Reset();

	return FComposableCameraHitResult  {
		.bHasHit = PendingTargetPosition != CameraPosition,
		.HitTargetLocation = PendingTargetPosition
	};
}

#if !UE_BUILD_SHIPPING
void UComposableCameraCollisionPushNode::DrawNodeDebug(FComposableCameraDebugDrawSink& Draw, bool bViewerIsOutsideCamera) const
{
	if (CVarShowCollisionPushGizmo.GetValueOnGameThread() == 0
		&& !FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()) { return; }

	// DepthPriority=1 (SDPG_Foreground) on every piece so the gizmos aren't
	// occluded by the character mesh or world geometry they sit on/inside.
	constexpr uint8 KForeground = 1;

	// Possessed play: just the pivot sphere (and hit sphere if blocked). A
	// line/arrow from pivot to camera is view-aligned in a standard 3rd-
	// person setup and every variant tried failed to read reliably. The
	// colour of the pivot sphere alone conveys "trace blocked this frame".
	// F8 eject: draw the full pivotamera trace line so the reader can see
	// what the trace is checking from the outside viewpoint.
	const FColor TraceColor = bLastTraceBlocked
		? FComposableCameraViewportDebugColors::CollisionPushBlocked()
		: FComposableCameraViewportDebugColors::CollisionPushClear();

	if (bViewerIsOutsideCamera)
	{
		Draw.DrawLine(LastTraceStart, LastTraceEnd, TraceColor, /*Thickness=*/0.f, KForeground);
	}

	// Trace sphere at pivot (start) when using sphere trace. Shows the actual
	// query geometry. Line trace: just a small marker.
	if (bTraceUseSphere)
	{
		Draw.DrawSphere(
			LastTraceStart, static_cast<float>(TraceSphereRadius),
			TraceColor, /*Alpha=*/90, KForeground, /*bSolid=*/true,
			/*Segments=*/16);
	}
	else
	{
		Draw.DrawPoint(LastTraceStart, TraceColor, /*Size=*/8.f, KForeground);
	}

	// Hit location. Red sphere when blocked, to show where the push resolved to.
	if (bLastTraceBlocked)
	{
		Draw.DrawSphere(
			LastTraceHitLocation, /*Radius=*/5.f, FComposableCameraViewportDebugColors::CollisionPushHit(),
			/*Alpha=*/140, KForeground, /*bSolid=*/true);
	}

	// Self-collision sphere sits AT the camera's position. Hermetically
	// encloses the player's view during live gameplay. Only useful from
	// outside the camera, so suppress unless the viewer IS outside
	// (F8 eject / SIE / AlwaysShow). Same rationale as the frustum.
	// Extra-low alpha (60) because this is the LARGEST sphere in the
	// plugin (SelfSphereRadius can be 100+ units). A solid translucent
	// ball that big needs to stay ghost-like to not drown the view.
	if (bViewerIsOutsideCamera)
	{
		Draw.DrawSphere(
			LastSelfSphereCenter, static_cast<float>(SelfSphereRadius),
			FComposableCameraViewportDebugColors::CollisionPushSelf(),
			/*Alpha=*/60, KForeground, /*bSolid=*/true,
			/*Segments=*/16);
	}
}
#endif

FVector UComposableCameraCollisionPushNode::StartResolveCollision(double DeltaTime, const FVector& TargetLocation,
	const FVector& CameraPosition)
{
	FVector Direction = TargetLocation - CameraPosition;
	Direction.Normalize();

	double TargetDistance = (TargetLocation - CameraPosition).Length();
	double DistanceOffset = TargetDistance - CurrentDistanceFromCamera;
	
	if (PushInterpolator_T)
	{
		PushInterpolator_T->Reset(0, DistanceOffset);
		DistanceOffset = PushInterpolator_T->Run(DeltaTime);
	}

	CurrentDistanceFromCamera += DistanceOffset;

	return CameraPosition + Direction * CurrentDistanceFromCamera;
}

FVector UComposableCameraCollisionPushNode::ResumeFromCollision(double DeltaTime, const FVector& PivotPosition,
	const FVector& CameraPosition)
{
	FVector Direction = PivotPosition - CameraPosition;
	Direction.Normalize();

	double DistanceOffset = -CurrentDistanceFromCamera;
	
	if (PullInterpolator_T)
	{
		PullInterpolator_T->Reset(0, DistanceOffset);
		DistanceOffset = PullInterpolator_T->Run(DeltaTime);
	}

	CurrentDistanceFromCamera += DistanceOffset;

	return CameraPosition + Direction * CurrentDistanceFromCamera;
}
