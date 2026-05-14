// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraCameraNodeBase.h"
#include "Utils/ComposableCameraActorInputSource.h"
#include "ComposableCameraRelativeFixedPoseNode.generated.h"

class USkeletalMeshComponent;

UENUM()
enum class EComposableCameraRelativeFixedPoseMethod : uint8
{
	// Fixed transform.
	RelativeToTransform,
	
	// Relative to an actor.
	RelativeToActor
};

/**
 * Node for maintaining a fixed pose relative to some given transform.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Maintains a fixed camera pose relative to a specified transform or actor."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraRelativeFixedPoseNode : public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraRelativeFixedPoseNode() { PaletteCategory = TEXT("Position"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

	// Synthesizes a full pose from a reference transform / actor socket - ignores
	// the upstream InPose entirely. In a Patch graph this erases whatever the
	// prior chain produced, which is the exact opposite of "additive overlay".
	virtual EComposableCameraNodePatchCompatibility GetPatchCompatibility_Implementation() const override
	{
		return EComposableCameraNodePatchCompatibility::Incompatible;
	}

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

protected:

public:
	// Method to use for fixed pose.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	EComposableCameraRelativeFixedPoseMethod Method;

	// Relative transform when method is RelativeToTransform.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "Method == EComposableCameraRelativeFixedPoseMethod::RelativeToTransform", EditConditionHides))
	FTransform RelativeTransform;

	// Selects whether the relative actor comes from the controller's controlled
	// pawn or from the explicitly supplied RelativeActor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor", EditConditionHides))
	EComposableCameraActorInputSource RelativeActorSource { EComposableCameraActorInputSource::ExplicitActor };

	// Explicit relative actor when method is RelativeToActor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor && RelativeActorSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> RelativeActor;

	// Relative socket when method is RelativeToActor and the actor has a SkeletalMeshComponent. If no such component exists, will use RelativeActor's transform.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "Method == EComposableCameraRelativeFixedPoseMethod::RelativeToActor", EditConditionHides))
	FName RelativeSocket;

	// The target transform that is applied to the local specified space.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FTransform TargetTransform;

private:
	// Resolved at OnInitialize from RelativeActor's first SkeletalMeshComponent.
	// Stored as TWeakObjectPtr (not raw and not UPROPERTY): the actor / component
	// can be destroyed or re-spawned mid-run independently of this node, so a
	// raw cached pointer dangles. Tick / DrawNodeDebug must IsValid()-check
	// before each use, not just nullcheck.
	TWeakObjectPtr<USkeletalMeshComponent> SkeletalMeshComponentForRelativeActor;

	TWeakObjectPtr<AActor> LastResolvedRelativeActor;
};
