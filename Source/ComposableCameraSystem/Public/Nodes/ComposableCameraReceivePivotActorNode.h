// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "ComposableCameraReceivePivotActorNode.generated.h"

/**
 * Reads a pivot actor's location and publishes it as the pivot position for downstream nodes.
 * This node runs every tick.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Reads a pivot actor's location and publishes it as the pivot position for downstream nodes."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraReceivePivotActorNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()
	
public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

	// Overrides the Pivot component of the pose from an external actor — if the
	// upstream has a meaningful pivot already, this node discards it. Legitimate
	// use in a Patch (e.g. "retarget to a different actor for a moment") but
	// worth flagging as an author confirmation.
	virtual EComposableCameraNodePatchCompatibility GetPatchCompatibility_Implementation() const override
	{
		return EComposableCameraNodePatchCompatibility::CompatibleWithCaveat;
	}

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

public:
	// Actor whose location is published as the pivot position. Typically driven
	// at runtime via a context parameter (e.g. the player pawn); kept as a
	// UPROPERTY so the Details panel renders a proper object picker and an
	// authored default is available when unwired.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	TObjectPtr<AActor> PivotActor;

	// Whether to use a bone as the target pivot point.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters)
	bool bUseBoneForPivot { false };

	// If use bone, specify the bone name. If we cannot find such a bone, will use the pivot actor's root position.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = InputParameters, meta = (EditCondition = "bUseBoneForPivot == true"))
	FName BoneName;

private:
	USkeletalMeshComponent* SkeletalMeshComponentForPivotActor { nullptr };
};
