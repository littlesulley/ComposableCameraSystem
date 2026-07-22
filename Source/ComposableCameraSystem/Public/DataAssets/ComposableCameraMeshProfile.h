// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/ComposableCameraParameterTableRow.h"
#include "Engine/DataAsset.h"
#include "ComposableCameraMeshProfile.generated.h"

class UComposableCameraNodeModifierDataAsset;

/**
 * Camera behavior invoked when a player enters a mesh surface layer.
 *
 * Camera uses the same activation schema and exposed-parameter authoring as a
 * FComposableCameraParameterTableRow. Modifiers keep their existing
 * camera-tag-filtered behavior. Action and Patch are reserved editor sections
 * until their runtime contracts are defined.
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraMeshProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Optional Camera Type activation performed once when this Profile becomes active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ShowOnlyInnerProperties))
	FComposableCameraParameterTableRow Camera;

	/** Modifier templates installed for the lifetime of this active Profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier")
	TArray<TObjectPtr<UComposableCameraNodeModifierDataAsset>> ModifierAssets;
};
