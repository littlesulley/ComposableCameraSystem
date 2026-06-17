// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "Utils/ComposableCameraActorInputSource.h"
#include "ComposableCameraComputePositionBetweenActorsNode.generated.h"

/**
 * Computes one activation-time world position between two resolved actors.
 *
 * Position = Lerp(FirstActor.Location, SecondActor.Location, Alpha) + WorldZ(HeightOffset)
 */
UCLASS(ClassGroup = ComposableCameraSystem, meta = (DisplayName = "Begin Play: Position Between Actors", ToolTip = "Computes a world position between two actors, then applies a world-Z height offset at camera activation."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraComputePositionBetweenActorsNode
	: public UComposableCameraComputeNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraComputePositionBetweenActorsNode() { PaletteCategory = TEXT("Math"); }

public:
	virtual void ExecuteBeginPlay() override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

public:
	// Selects whether FirstActor comes from an explicit actor or the controller's controlled pawn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraActorInputSource FirstActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Explicit first actor. Used when FirstActorSource is ExplicitActor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "FirstActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> FirstActor { nullptr };

	// Selects whether SecondActor comes from an explicit actor or the controller's controlled pawn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraActorInputSource SecondActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Explicit second actor. Used when SecondActorSource is ExplicitActor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "SecondActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> SecondActor { nullptr };

	// Normalized position between first actor (0) and second actor (1).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Alpha { 0.5f };

	// World-Z offset applied after interpolation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	float HeightOffset { 0.f };
};
