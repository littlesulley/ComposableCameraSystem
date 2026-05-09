// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Utils/ComposableCameraActorInputSource.h"
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
	UPROPERTY(EditAnywhere, Category = "Input Parameters")
	double CameraDistance { 300. };
	
	// X axis interpolator in the camera space.
	UPROPERTY(EditAnywhere, Instanced, Category = "Input Parameters")
	TObjectPtr<UComposableCameraInterpolatorBase> XInterpolator { nullptr };

	// Y axis interpolator in the camera space.
	UPROPERTY(EditAnywhere, Instanced, Category = "Input Parameters")
	TObjectPtr<UComposableCameraInterpolatorBase> YInterpolator { nullptr };

	// Z axis interpolator in the camera space.
	UPROPERTY(EditAnywhere, Instanced, Category = "Input Parameters")
	TObjectPtr<UComposableCameraInterpolatorBase> ZInterpolator { nullptr };
};

USTRUCT(BlueprintType)
struct FComposableCameraScreenSpaceRotationParams
{
	GENERATED_BODY()
	
	// Yaw axis interpolator in the world space.
	UPROPERTY(EditAnywhere, Instanced, Category = "Input Parameters")
	TObjectPtr<UComposableCameraInterpolatorBase> YawInterpolator { nullptr };

	// Pitch axis interpolator in the camera space.
	UPROPERTY(EditAnywhere, Instanced, Category = "Input Parameters")
	TObjectPtr<UComposableCameraInterpolatorBase> PitchInterpolator { nullptr };
};

/**
 * Node for positioning the given pivot point in the given screen space.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Positions a pivot point at a specific screen-space location through translation or rotation."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraScreenSpacePivotNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraScreenSpacePivotNode() { PaletteCategory = TEXT("Framing"); }
	
public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

#if !UE_BUILD_SHIPPING
	// 3D: teal sphere at the resolved world-space pivot.
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;

	// 2D: safe-zone rectangle + center marker + projected-pivot marker on
	// the HUD. Handles both `bConstrainAspectRatio = false` (whole viewport)
	// and `bConstrainAspectRatio = true` (letterboxed â€?uses ProjectionData
	// to account for the editor viewport offset) cases, matching the logic
	// the node itself uses for its screen-space math.
	virtual void DrawNodeDebug2D(UCanvas* Canvas, APlayerController* PC) const override;
#endif

	// Compatible with the Level Sequence path. Viewport size for screen-space
	// math is resolved through UE::ComposableCameras::TryGetEffectiveViewportSize
	// (PCM â†?GameViewport â†?16:9 fallback), so the node no longer requires a
	// PlayerCameraManager.

protected:

public:
	// How the pivot world-space position is resolved at runtime.
	// WorldPosition â†?use PivotWorldPosition directly.
	// ActorPosition â†?read PivotActor's world location and add PivotWorldUpOffset along world up.
	// Declared as a pin with bDefaultAsPin = false: it renders in the Details panel
	// as a design-time choice by default, but individual node instances can
	// promote it to a wired pin when runtime variation is actually required.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraScreenSpacePivotSource PivotSource { EComposableCameraScreenSpacePivotSource::WorldPosition };

	// Selects whether the ActorPosition pivot actor is the controller's
	// controlled pawn or the explicitly supplied PivotActor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "PivotSource == EComposableCameraScreenSpacePivotSource::ActorPosition", EditConditionHides))
	EComposableCameraActorInputSource PivotActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Pivot in world space. Used when PivotSource == WorldPosition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "PivotSource == EComposableCameraScreenSpacePivotSource::WorldPosition", EditConditionHides))
	FVector PivotWorldPosition { FVector::ZeroVector };

	// Actor whose world location supplies the pivot. Used when PivotSource == ActorPosition.
	// If unresolved at runtime, GetCurrentPivot() falls back to FVector::ZeroVector.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "PivotSource == EComposableCameraScreenSpacePivotSource::ActorPosition && PivotActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
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

private:
	void EnsureWithinBoundsTranslation(const FVector& CameraSpacePivotPosition, FVector& CameraSpaceDampedOffset, const float& AspectRatio, const float& TanHalfHOR, const float& CameraDistance);
	void EnsureWithinBoundsRotation(const FRotator& CameraRotation, const FVector& LookAtRotation, FRotator& DeltaRotation, float AspectRatio, float DegTanHalfHor);
	std::pair<float, float> GetTanHalfHORAndAspectRatio(const FComposableCameraPose& OutCameraPose);
	FVector GetScreenSpaceTranslateAmount(const FVector& Pivot, const FComposableCameraPose& OutCameraPose, float DeltaTime);
	FRotator GetScreenSpaceRotateAmount(const FVector& Pivot, const FComposableCameraPose& OutCameraPose, float DeltaTime);

	FVector GetCurrentPivot() const;
};
