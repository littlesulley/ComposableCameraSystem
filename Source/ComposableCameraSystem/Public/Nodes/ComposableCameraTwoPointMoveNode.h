// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraTwoPointMoveNode.generated.h"

class UCurveFloat;

/**
 * Moves the camera from SourceTransform to TargetTransform over Duration.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Moves the camera from a source transform to a target transform over a fixed duration using a normalized curve."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraTwoPointMoveNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraTwoPointMoveNode() { PaletteCategory = TEXT("Position"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

	virtual EComposableCameraNodePatchCompatibility GetPatchCompatibility_Implementation() const override
	{
		return EComposableCameraNodePatchCompatibility::Incompatible;
	}

public:
	// Transform used at normalized time 0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FTransform SourceTransform { FTransform::Identity };

	// Transform used at normalized time 1, and held after Duration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FTransform TargetTransform { FTransform::Identity };

	// Curve sampled with X in [0,1]. The returned value is clamped to [0,1].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	TObjectPtr<UCurveFloat> Curve { nullptr };

	// Seconds spent moving from SourceTransform to TargetTransform.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "0.0"))
	float Duration { 1.f };

private:
	float ElapsedTime { 0.f };
};
