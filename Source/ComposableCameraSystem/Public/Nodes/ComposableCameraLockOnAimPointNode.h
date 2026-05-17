// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/ComposableCameraLockOnAimPoint.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Utils/ComposableCameraActorInputSource.h"
#include "ComposableCameraLockOnAimPointNode.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EComposableCameraLockOnAimPointSource : uint8
{
	// Point is a world-space FVector authored on the node or wired from upstream.
	WorldPosition,

	// Point is derived from an actor's world location plus a world-up offset.
	ActorPosition
};

/**
 * Builds a per-frame virtual aim point for lock-on composition.
 *
 * Intended placement:
 *   ScreenSpacePivot(follow) -> LockOnAimPoint -> ScreenSpacePivot(aim).
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem,
	meta = (DisplayName = "Lock-On Aim Point", ToolTip = "Builds a stable virtual aim point for lock-on screen-space framing."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraLockOnAimPointNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraLockOnAimPointNode() { PaletteCategory = TEXT("Pivot"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime,
		const FComposableCameraPose& CurrentCameraPose,
		FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

public:
	// How the player/follow point is resolved at runtime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraLockOnAimPointSource FollowSource { EComposableCameraLockOnAimPointSource::WorldPosition };

	// Selects whether the follow actor is the controller's controlled pawn or an explicit actor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "FollowSource == EComposableCameraLockOnAimPointSource::ActorPosition", EditConditionHides))
	EComposableCameraActorInputSource FollowActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Follow pivot in world space. Used when FollowSource == WorldPosition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "FollowSource == EComposableCameraLockOnAimPointSource::WorldPosition", EditConditionHides))
	FVector FollowWorldPosition { FVector::ZeroVector };

	// Actor whose world location supplies the follow point. Used when FollowSource == ActorPosition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "FollowSource == EComposableCameraLockOnAimPointSource::ActorPosition && FollowActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> FollowActor { nullptr };

	// World-up offset added to FollowActor->GetActorLocation().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "FollowSource == EComposableCameraLockOnAimPointSource::ActorPosition", EditConditionHides))
	float FollowWorldUpOffset { 0.f };

	// How the lock target/aim point is resolved at runtime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraLockOnAimPointSource AimSource { EComposableCameraLockOnAimPointSource::WorldPosition };

	// Selects whether the aim actor is the controller's controlled pawn or an explicit actor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "AimSource == EComposableCameraLockOnAimPointSource::ActorPosition", EditConditionHides))
	EComposableCameraActorInputSource AimActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Raw lock target pivot in world space. Used when AimSource == WorldPosition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "AimSource == EComposableCameraLockOnAimPointSource::WorldPosition", EditConditionHides))
	FVector AimWorldPosition { FVector::ZeroVector };

	// Actor whose world location supplies the raw aim point. Used when AimSource == ActorPosition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "AimSource == EComposableCameraLockOnAimPointSource::ActorPosition && AimActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> AimActor { nullptr };

	// World-up offset added to AimActor->GetActorLocation().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "AimSource == EComposableCameraLockOnAimPointSource::ActorPosition", EditConditionHides))
	float AimWorldUpOffset { 0.f };

	// Minimum horizontal projected distance before correction activates.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "0.0"))
	float Radius { 500.f };

	// Min/max pitch in degrees used by the pitch-preserving term while inside Radius.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector2D PitchRange { -45.f, 45.f };

	// Seconds used to fade the correction offset back to zero after leaving Radius.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "0.0"))
	float BlendOutTime { 0.15f };

	// Blend weights for pitch-preserving, camera-to-aim, and camera-forward additions.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	FVector Weights { 0.2f, 0.5f, 0.3f };

private:
	FComposableCameraLockOnAimPointState AimPointState;

#if !UE_BUILD_SHIPPING
	FVector LastRawAimPosition { FVector::ZeroVector };
	FVector LastOutputPivotPosition { FVector::ZeroVector };
	bool bLastAppliedCorrection { false };
#endif
};
