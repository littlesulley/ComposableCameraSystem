// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "ComposableCameraAutoRotateNode.generated.h"

class UComposableCameraInterpolatorBase;

UENUM()
enum class EComposableCameraAutoRotateDirectionMode : uint8
{
	// Use an explicit direction vector (MainDirection) as the reference forward.
	Direction,

	// Use PrimaryActor's forward vector as the reference forward each frame.
	ActorForward
};

/**
 * Node for auto-rotating around a given "main direction." The main direction is
 * supplied either as an explicit direction vector or by reading an actor's
 * forward vector each frame, selected via DirectionMode. Both inputs are pins
 * so they can be wired from upstream compute nodes or context parameters.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, CollapseCategories, meta = (ToolTip = "Automatically rotates the camera back towards a specified direction when beyond valid range."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraAutoRotateNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraAutoRotateNode() { PaletteCategory = TEXT("Rotation"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

protected:

public:
	// Selects how the reference forward direction is resolved each frame:
	// Direction = use the MainDirection vector; ActorForward = use PrimaryActor's forward.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraAutoRotateDirectionMode DirectionMode { EComposableCameraAutoRotateDirectionMode::Direction };

	// Explicit reference forward direction (used when DirectionMode == Direction).
	// Typically driven via an upstream wire or a context parameter; the authored
	// default is X-forward so a freshly placed node has something sensible.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "DirectionMode == EComposableCameraAutoRotateDirectionMode::Direction", EditConditionHides))
	FVector MainDirection { FVector::ForwardVector };

	// Actor whose forward vector is used as the reference direction (used when
	// DirectionMode == ActorForward). Typically wired to the player pawn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "DirectionMode == EComposableCameraAutoRotateDirectionMode::ActorForward", EditConditionHides))
	TObjectPtr<AActor> PrimaryActor;

	// Whether user rotation input should interrupt an in-progress auto-rotation
	// and count toward MaxCountAfterInputInterrupt. When false, CameraRotationInput
	// is ignored entirely and only the BeyondValidRangeCooldown gates auto-rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bInterruptOnUserInput { true };

	// Rotation input this frame (x=yaw, y=pitch) — used to detect user interrupt
	// when bInterruptOnUserInput is true. Almost always wired from
	// ControlRotateNode's CameraRotationInput output.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "bInterruptOnUserInput", EditConditionHides))
	FVector2D CameraRotationInput { 0.f, 0.f };

	// World space yaw range around the main direction. If current camera is beyond this range, will start to auto-rotate yaw, if other conditions are met.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector2D YawRange { 0.f, 0.f };

	// Local space pitch range around the main direction. If current camera is beyond this range, will start to auto-rotate pitch, if other conditions are met.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "bYawOnly == false", EditConditionHides))
	FVector2D PitchRange { 0.f, 0.f };

	// Whether only auto-rotating yaw.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bYawOnly { true };

	// When beyond this range, will wait for at least this amount of time to start auto-rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float BeyondValidRangeCooldown { 0.f };

	// When auto-rotation is interrupted by user input, will wait for at least this amount of time to start auto-rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "bInterruptOnUserInput", EditConditionHides))
	float InputInterruptCooldown { 0.f };

	// Max allowed count to enable auto-rotation after user input interruption.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "bInterruptOnUserInput", EditConditionHides))
	int32 MaxCountAfterInputInterrupt { -1 };

	// Rotator interpolator driving yaw+pitch as a single unified rotation — both
	// axes progress on the same curve so they reach the target together rather
	// than one axis finishing before the other. If null, the camera teleports to
	// the target boundary rotation in one frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = InputParameters)
	TObjectPtr<UComposableCameraInterpolatorBase> RotateInterpolator;

private:
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<FRotator>>> Interpolator_T;

	bool bInAutoRotate { false };
	float BeyondValidRangeCooldownRemaining { 0.f };
	float InputInterruptCooldownRemaining { 0.f };

	UPROPERTY(VisibleInstanceOnly)
	int32 UsedCountAfterInputInterrupt { 0 };

private:
	std::pair<bool, bool> CheckIfInValidRange(const FVector2D& ValidRangeYaw, const FVector2D& ValidRangePitch, const FRotator& Rotation);
};
