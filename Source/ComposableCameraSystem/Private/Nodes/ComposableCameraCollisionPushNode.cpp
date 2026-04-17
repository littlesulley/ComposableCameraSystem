// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraCollisionPushNode.h"

#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "KismetTraceUtils.h"
#include "EditorHooks/EditorHooks.h"
#include "Kismet/KismetSystemLibrary.h"

void UComposableCameraCollisionPushNode::OnInitialize_Implementation()
{
	Super::OnInitialize_Implementation();

	// Subobject pin values (e.g. PushInterpolator.Speed) are auto-applied by
	// the base class in Initialize(), before this OnInitialize runs.

	PushInterpolator_T = IsValid(PushInterpolator) ? PushInterpolator->BuildDoubleInterpolator() : nullptr;
	PullInterpolator_T = IsValid(PullInterpolator) ? PullInterpolator->BuildDoubleInterpolator() : nullptr;

	if (bUseBoneForDetection)
	{
		AActor* InPivotActor = GetInputPinValue<AActor*>("PivotActor");
		if (IsValid(InPivotActor))
		{
			SkeletalMeshComponentForPivotActor = InPivotActor->GetComponentByClass<USkeletalMeshComponent>();
		}
	}
}

void UComposableCameraCollisionPushNode::OnTickNode_Implementation(float DeltaTime,
                                                                   const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	OriginalCameraPosition = CurrentCameraPose.Position;
	FVector PivotPosition = FVector::ZeroVector;

	// PivotActor / bUseBoneForDetection / PivotZOffset / SelfSphereDistanceOffsetFromCenter /
	// ExtraPushDistance are pin-matched UPROPERTYs — resolved by the base TickNode
	// prologue.
	if (bUseBoneForDetection && IsValid(SkeletalMeshComponentForPivotActor))
	{
		PivotPosition = SkeletalMeshComponentForPivotActor->GetSocketLocation(BoneName);
	}
	else
	{
		AActor* InPivotActor = PivotActor.Get();
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

	if (OwningCamera)
	{
		OwningCamera->CameraPose.Position = OriginalCameraPosition;
	}
}

void UComposableCameraCollisionPushNode::GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const
{
	FComposableCameraNodePinDeclaration PinDecl;

	// PivotActor Input
	PinDecl.PinName = TEXT("PivotActor");
	PinDecl.DisplayName = NSLOCTEXT("ComposableCameraCollisionPushNode", "PivotActor", "Pivot Actor");
	PinDecl.Direction = EComposableCameraPinDirection::Input;
	PinDecl.PinType = EComposableCameraPinType::Actor;
	PinDecl.bRequired = true;
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
	// GatherAllPinDeclarations in the base class — no manual calls needed.
}


FComposableCameraHitResult UComposableCameraCollisionPushNode::FindCollisionPoint(double DeltaTime,  const FVector& PivotPosition, const FVector& CameraPosition, const FRotator& CameraRotation)
{
	// Do trace collision point first.
	const FVector Start = PivotPosition;
	const FVector End = CameraPosition;
	FVector Direction = Start - End;
	Direction.Normalize();
	
	TArray<AActor*> ActorsToIgnore;
	for (TSoftClassPtr<AActor> ActorType: ActorTypesToIgnore)
	{
		if (ActorType.IsValid())
		{
			TArray<AActor*> IgnoredActors;
			UGameplayStatics::GetAllActorsOfClass(this, ActorType.Get(), IgnoredActors);
			ActorsToIgnore.Append(IgnoredActors);
		}
	}

	FHitResult TraceCollisionHit;
#if ENABLE_DRAW_DEBUG
	EDrawDebugTrace::Type DrawDebugType =
		OwningPlayerCameraManager->bDrawDebugInformation ?
		EDrawDebugTrace::ForOneFrame :
		EDrawDebugTrace::None;

#if WITH_EDITOR
	if (!FIsSimulatingInEditor::GetIsSimulatingInEditor())
	{
		DrawDebugType = EDrawDebugTrace::None;
	}
#endif
#else
	EDrawDebugTrace::Type DrawDebugType = EDrawDebugTrace::None;
#endif
	
	if (bTraceUseSphere)
	{
		UKismetSystemLibrary::SphereTraceSingle(this, Start, End, TraceSphereRadius, TraceCollisionChannel, true, ActorsToIgnore, DrawDebugType, TraceCollisionHit, true);
	}
	else
	{
		UKismetSystemLibrary::LineTraceSingle(this, Start, End, TraceCollisionChannel, true, ActorsToIgnore, DrawDebugType, TraceCollisionHit, true);
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
	FVector SelfCollisionEnd = SelfCollisionStart;
	
	FHitResult SelfCollisionHit;
	UKismetSystemLibrary::SphereTraceSingle(this, SelfCollisionStart, SelfCollisionEnd, SelfSphereRadius, SelfCollisionChannel, true, ActorsToIgnore, DrawDebugType, SelfCollisionHit, true);

	if (SelfCollisionHit.bBlockingHit)
	{
		AActor* HitActor = SelfCollisionHit.GetActor();
		
		SelfCollisionStart = PivotPosition;
		TArray<FHitResult> SelfHitResults;

		FCollisionQueryParams QueryParams =  FCollisionQueryParams::DefaultQueryParam;
		QueryParams.AddIgnoredActors(ActorsToIgnore);
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

	return FComposableCameraHitResult  {
		.bHasHit = PendingTargetPosition != CameraPosition,
		.HitTargetLocation = PendingTargetPosition
	};
}

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
