// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Utils/ComposableCameraActorInputSource.h"
#include "ComposableCameraPivotOffsetNode.generated.h"

UENUM(BlueprintType)
enum class ECameraPivotOffset : uint8
{
	WorldSpace,
	ActorLocalSpace,
	CameraSpace
};

/**
 * Node for adjusting the pivot position by applying an offset in world/camera/actor local space. \n
 * If using camera space, the CurrentCameraPose parameter in the Tick function will be used. \n
 * @ InputParameter PivotOffsetType: In which space you'd like to apply offset, can be world, camera, or actor local. \n
 * @ InputParameter ActorForLocalSpace: The actor determining the local space if you choose actor local space. \n
 * @ InputParameter PivotOffset: The offset. \n
 * This node runs every tick.
 */
UCLASS(NotBlueprintable, ClassGroup = ComposableCameraSystem, meta = (ToolTip = "Applies an offset to the pivot position in world, camera, or actor-local space."))
class COMPOSABLECAMERASYSTEM_API UComposableCameraPivotOffsetNode
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	UComposableCameraPivotOffsetNode() { PaletteCategory = TEXT("Pivot"); }

public:
	virtual void OnInitialize_Implementation() override;
	virtual void OnTickNode_Implementation(float DeltaTime, const FComposableCameraPose& CurrentCameraPose, FComposableCameraPose& OutCameraPose) override;
	virtual void GetPinDeclarations_Implementation(TArray<FComposableCameraNodePinDeclaration>& OutPins) const override;

#if !UE_BUILD_SHIPPING
	virtual void DrawNodeDebug(UWorld* World, bool bViewerIsOutsideCamera) const override;
#endif

protected:
	
public:
	// The pivot position to apply the offset to. Almost always driven by an upstream
	// pivot-producing node via wire (or a context parameter); kept as a UPROPERTY
	// so the Details panel renders a native FVector widget and an authored default
	// is available when unwired.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector PivotPosition { FVector::ZeroVector };

	// In which space you'd like to apply offset, can be world, camera, or actor local.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	ECameraPivotOffset PivotOffsetType = ECameraPivotOffset::WorldSpace;

	// Selects whether the local-space actor comes from the controller's controlled pawn or an explicit actor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "PivotOffsetType == ECameraPivotOffset::ActorLocalSpace", EditConditionHides))
	EComposableCameraActorInputSource ActorForLocalSpaceSource { EComposableCameraActorInputSource::ExplicitActor };

	// The explicit actor determining the local space if you choose actor local space.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters, meta = (EditCondition = "PivotOffsetType == ECameraPivotOffset::ActorLocalSpace && ActorForLocalSpaceSource == EComposableCameraActorInputSource::ExplicitActor", EditConditionHides))
	TObjectPtr<AActor> ActorForLocalSpace { nullptr };

	// The offset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = InputParameters)
	FVector PivotOffset = FVector::ZeroVector;

private:
	void UpdatePivotOffset(const FVector& InPivot, const FComposableCameraPose& CurrentCameraPose);

#if !UE_BUILD_SHIPPING
	/** Cache of the final post-offset pivot this frame, written in UpdatePivotOffset
	 *  and read by DrawNodeDebug. Output pins are not re-readable by name so we
	 *  keep a mirror; only present in non-shipping builds. */
	FVector LastComputedPivot { FVector::ZeroVector };
#endif
};
