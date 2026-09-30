// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ComposableCameraActionTypeAsset.generated.h"

class UComposableCameraActionBase;
struct FComposableCameraParameterBlock;
class FProperty;

/** An authored Action template. Each activation duplicates the template so
 * Blueprint execution state and caller-supplied values remain per Action. */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraActionTypeAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Action")
	TObjectPtr<UComposableCameraActionBase> ActionTemplate;

	/** Duplicate the template and apply caller values before PCM registration. */
	UComposableCameraActionBase* CreateAction(UObject* Outer,
		const FComposableCameraParameterBlock& Parameters) const;

	/** Compatible editable Blueprint-visible fields declared below ActionBase
	 * become dynamic K2 inputs. Base lifecycle fields stay asset-owned. */
	static bool IsExposableProperty(const FProperty* Property);
};
