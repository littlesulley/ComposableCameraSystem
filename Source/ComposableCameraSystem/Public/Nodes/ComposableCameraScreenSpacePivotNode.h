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
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraScreenSpacePivotNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
	
public:
	virtual void OnBeginPlayNode_Implementation(const FComposableCameraPose& CurrentCameraPose) override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void BeginDestroy() override;
	
protected:
	virtual void ReceiveInitializerNode(UComposableCameraCameraNodeBase* Initializer) override;
	
public:
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
	
	UPROPERTY(EditAnywhere, Category = ContextParameters)
	FVector3dComposableCameraContextParameter ContextPivotPosition;

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
	std::pair<float, float> CalibrateRotationOffset(float TanHalfHOR, float AspectRatio, FVector Direction, FRotator LookAtRotation, float ScreenX, float ScreenY);
	FRotator GetScreenSpaceRotateAmount(const FVector& Pivot, const FComposableCameraPose& OutCameraPose, float DeltaTime);

	FVector GetCurrentPivot();
	void DrawDebugInfo(AHUD* HUD, UCanvas* Canvas);
};
