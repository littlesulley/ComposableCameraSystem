// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Utils/ComposableCameraActorInputSource.h"
#include "ComposableCameraPivotLookAheadNode.generated.h"

class AActor;

/**
 * Projects a pivot position forward by a velocity-derived look-ahead offset.
 *
 * Intended placement:
 * ReceivePivotActor -> PivotLookAhead -> PivotOffset / ScreenSpacePivot -> LookAt -> CameraOffset.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem,
	meta = (ToolTip = "Projects a pivot position ahead using actor velocity, with pivot-delta fallback and optional velocity damping."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraPivotLookAheadNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraPivotLookAheadNode() { PaletteCategory = TEXT("Pivot"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnFirstTickNode_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime,
		const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

public:
	// Pivot position to project. Usually wired from ReceivePivotActor or another pivot-producing node.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector PivotPosition { FVector::ZeroVector };

	// Selects whether VelocityActor comes from the controlled pawn or an explicit actor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraActorInputSource VelocityActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Actor whose velocity drives look-ahead. If unresolved, the node falls back to frame-to-frame PivotPosition velocity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters,
		meta = (EditCondition = "VelocityActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> VelocityActor { nullptr };

	// Seconds into the future to project PivotPosition using the resolved velocity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "0.0"))
	float LookAheadTime { 0.f };

	// Time used to damp velocity changes. Set to 0 for immediate velocity response.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "0.0"))
	float VelocityDampingTime { 0.f };

private:
	FVector LastPivotPosition { FVector::ZeroVector };
	FVector SmoothedVelocity { FVector::ZeroVector };
	FVector VelocitySmoothingVelocity { FVector::ZeroVector };
	bool bHasLastPivotPosition { false };

#if !UE_BUILD_SHIPPING
	FVector LastOutputPivotPosition { FVector::ZeroVector };
#endif
};
