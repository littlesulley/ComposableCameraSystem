// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "ComposableCameraScreenSpacePivotNode.generated.h"

class UComposableCameraInterpolatorBase;

UENUM()
enum class EComposableCameraScreenSpaceMethod : uint8
{
	// Keep screen space constraints by camera translation.
	Translate,

	// Keep screen space constraints by camera rotation.
	Rotate
};

UENUM()
enum class EComposableCameraScreenSpacePivotSource : uint8
{
	// Pivot is a world-space FVector authored on the node (or wired from upstream).
	WorldPosition,

	// Pivot is derived from an actor's world location plus a world-up offset.
	// Useful when the pivot should track a character / prop at runtime.
	ActorPosition
};

USTRUCT(BlueprintType)
struct FComposableCameraScreenSpaceTranslationParams
{
	GENERATED_BODY()
	
	// Camera distance to the pivot.
	UPROPERTY(EditAnywhere)
	double CameraDistance { 300. };
	
	// X axis interpolator in the camera space.
	UPROPERTY(EditAnywhere, Instanced)
	UComposableCameraInterpolatorBase* XInterpolator;

	// Y axis interpolator in the camera space.
	UPROPERTY(EditAnywhere, Instanced)
	UComposableCameraInterpolatorBase* YInterpolator;

	// Z axis interpolator in the camera space.
	UPROPERTY(EditAnywhere, Instanced)
	UComposableCameraInterpolatorBase* ZInterpolator;
};

USTRUCT(BlueprintType)
struct FComposableCameraScreenSpaceRotationParams
{
	GENERATED_BODY()
	
	// Yaw axis interpolator in the world space.
	UPROPERTY(EditAnywhere, Instanced)
	UComposableCameraInterpolatorBase* YawInterpolator;

	// Pitch axis interpolator in the camera space.
	UPROPERTY(EditAnywhere, Instanced)
	UComposableCameraInterpolatorBase* PitchInterpolator;
};

/**
 * Node for positioning the given pivot point in the given screen space.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Positions a pivot point at a specific screen-space location through translation or rotation."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraScreenSpacePivotNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
	
public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void BeginDestroy() override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

protected:

public:
	// How the pivot world-space position is resolved at runtime.
	// WorldPosition → use PivotWorldPosition directly.
	// ActorPosition → read PivotActor's world location and add PivotWorldUpOffset along world up.
	// Declared as a pin with bDefaultAsPin = false: it renders in the Details panel
	// as a design-time choice by default, but individual node instances can
	// promote it to a wired pin when runtime variation is actually required.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraScreenSpacePivotSource PivotSource { EComposableCameraScreenSpacePivotSource::WorldPosition };

	// Pivot in world space. Used when PivotSource == WorldPosition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "PivotSource == EComposableCameraScreenSpacePivotSource::WorldPosition", EditConditionHides))
	FVector PivotWorldPosition { FVector::ZeroVector };

	// Actor whose world location supplies the pivot. Used when PivotSource == ActorPosition.
	// If unresolved at runtime, GetCurrentPivot() falls back to FVector::ZeroVector.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "PivotSource == EComposableCameraScreenSpacePivotSource::ActorPosition", EditConditionHides))
	TObjectPtr<AActor> PivotActor;

	// World-up offset added to PivotActor->GetActorLocation(). Useful for targeting a
	// character's head / chest when the actor origin is at their feet. Used when
	// PivotSource == ActorPosition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "PivotSource == EComposableCameraScreenSpacePivotSource::ActorPosition", EditConditionHides))
	float PivotWorldUpOffset { 0.f };

	// The method to keep screen space constraints, translation or rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraScreenSpaceMethod Method;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "Method == EComposableCameraScreenSpaceMethod::Translate", EditConditionHides))
	FComposableCameraScreenSpaceTranslationParams TranslationParams;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "Method == EComposableCameraScreenSpaceMethod::Rotate", EditConditionHides))
	FComposableCameraScreenSpaceRotationParams RotationParams;
	
	// Screen space safe zone center (w, h). (0, 0) is the screen center, (0.5, 0.5) is the top-right corner. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMax = "0.5", ClampMin = "-0.5"))
	FVector2D SafeZoneCenter { 0.0, 0.0 };

	// Screen space safe zone left half-width and right half-width, from -1 to 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMax = "1", ClampMin = "-1"))
	FVector2D SafeZoneWidth { -0.1, 0.1 };

	// Screen space safe zone bottom half-height and top half-height, from -1 to 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMax = "1", ClampMin = "-1"))
	FVector2D SafeZoneHeight { -0.1, 0.1 };

private:
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> XInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> YInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> ZInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> YawInterpolator_T;
	TUniquePtr<TCameraInterpolator<TValueTypeWrapper<double>>> PitchInterpolator_T;

	FDelegateHandle DrawDebugHandle;

private:
	void EnsureWithinBoundsTranslation(const FVector& CameraSpacePivotPosition, FVector& CameraSpaceDampedOffset, const float& AspectRatio, const float& TanHalfHOR, const float& CameraDistance);
	void EnsureWithinBoundsRotation(const FRotator& CameraRotation, const FVector& LookAtRotation, FRotator& DeltaRotation, float AspectRatio, float DegTanHalfHor);
	std::pair<float, float> GetTanHalfHORAndAspectRatio(const FComposableCameraPose& OutCameraPose);
	FVector GetScreenSpaceTranslateAmount(const FVector& Pivot, const FComposableCameraPose& OutCameraPose, float DeltaTime);
	std::pair<float, float> CalibrateRotationOffsetLM(float TanHalfHOR, float AspectRatio, FVector Direction, FRotator LookAtRotation, float ScreenX, float ScreenY);
	std::pair<float, float> CalibrateRotationOffsetNewton(float TanHalfHOR, float AspectRatio, FVector Direction, FRotator LookAtRotation, float ScreenX, float ScreenY);
	FRotator GetScreenSpaceRotateAmount(const FVector& Pivot, const FComposableCameraPose& OutCameraPose, float DeltaTime);

	FVector GetCurrentPivot();
	void DrawDebugInfo(AHUD* HUD, UCanvas* Canvas);
};
