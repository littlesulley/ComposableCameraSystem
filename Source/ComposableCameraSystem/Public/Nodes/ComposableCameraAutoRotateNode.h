// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "ComposableCameraAutoRotateNode.generated.h"

class UComposableCameraInterpolatorBase;

/**
 * Node for auto-rotating around a given "main direction." The main direction is
 * supplied through the MainDirection input pin, typically wired from an upstream
 * node (e.g. a compute node that reads character forward vector) or set as a
 * context parameter override.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, CollapseCategories, meta = (ToolTip = "Automatically rotates the camera back towards a specified direction when beyond valid range."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraAutoRotateNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

protected:

public:
	// Reference forward direction the camera auto-rotates toward. Typically driven
	// at runtime through a context parameter or an upstream node wire; the
	// authored default is X-forward so a freshly instanced node has something
	// sensible to fall back on.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector MainDirection { FVector::ForwardVector };

	// Rotation input this frame (x=yaw, y=pitch) — used to detect user interrupt.
	// Almost always wired from ControlRotateNode's CameraRotationInput output; kept
	// as a UPROPERTY so the Details panel renders a native FVector2D widget and
	// an authored default of zero is available when unwired.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector2D CameraRotationInput { 0.f, 0.f };

	// World space yaw range around the main direction. If current camera is beyond this range, will start to auto-rotate yaw, if other conditions are met.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector2D YawRange { 0.f, 0.f };

	// Local space pitch range around the main direction. If current camera is beyond this range, will start to auto-rotate pitch, if other conditions are met.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector2D PitchRange { 0.f, 0.f };

	// Whether only auto-rotating yaw.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	bool bYawOnly { true };

	// When beyond this range, will wait for at least this amount of time to start auto-rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float BeyondValidRangeCooldown { 0.f };

	// When auto-rotation is interrupted by user input, will wait for at least this amount of time to start auto-rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float InputInterruptCooldown { 0.f };

	// Max allowed count to enable auto-rotation after user input interruption.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	int32 MaxCountAfterInputInterrupt { -1 };

	// Rotate interpolator for yaw. If no interpolator exists, will teleport to the boundary of the valid range.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = InputParameters)
	TObjectPtr<UComposableCameraInterpolatorBase> RotateInterpolatorForYaw;

	// Rotate interpolator for pitch. If no interpolator exists, will teleport to the boundary of the valid range.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = InputParameters, meta = (EditCondition = "bYawOnly == false", EditConditionHides))
	TObjectPtr<UComposableCameraInterpolatorBase> RotateInterpolatorForPitch;

private:
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> InterpolatorYaw_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> InterpolatorPitch_T;

	bool bInAutoRotate { false };
	float BeyondValidRangeCooldownRemaining { 0.f };
	float InputInterruptCooldownRemaining { 0.f };

	UPROPERTY(VisibleInstanceOnly)
	int32 UsedCountAfterInputInterrupt { 0 };

private:
	std::pair<bool, bool> CheckIfInValidRange(const FVector2D& ValidRangeYaw, const FVector2D& ValidRangePitch, const FRotator& Rotation);
};
