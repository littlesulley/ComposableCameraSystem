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
		AActor* PivotActor = GetInputPinValue<AActor*>("PivotActor");
		if (IsValid(PivotActor))
		{
			SkeletalMeshComponentForPivotActor = PivotActor->GetComponentByClass<USkeletalMeshComponent>();
		}
	}
}

void UComposableCameraCollisionPushNode::OnTickNode_Implementation(float DeltaTime,
                                                                   const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose)
{
	OriginalCameraPosition = CurrentCameraPose.Position;
	FVector PivotPosition = FVector::ZeroVector;

	if (bUseBoneForDetection && IsValid(SkeletalMeshComponentForPivotActor))
	{
		PivotPosition = SkeletalMeshComponentForPivotActor->GetSocketLocation(BoneName);
	}
	else
	{
		AActor* PivotActor = GetInputPinValue<AActor*>("PivotActor");
		if (!IsValid(PivotActor))
		{
			UE_LOG(LogComposableCameraSystem, Warning, TEXT("Cannot find a valid pivot actor for collision push."))
			return;
		}
		PivotPosition = PivotActor->GetActorLocation() + FVector(0, 0, PivotZOffset);
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
	PinDecl.Tooltip = NSLOCTEXT("ComposableCameraCollisionPushNode", "PivotActorTip", "The actor from which to retrieve collision detection target position.");
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
