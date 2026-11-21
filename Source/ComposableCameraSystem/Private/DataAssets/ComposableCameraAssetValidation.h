// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

#if WITH_EDITOR
class UComposableCameraTypeAsset;
class FDataValidationContext;
struct FComposableCameraBuildMessage;
struct FComposableCameraShot;

namespace ComposableCameraAssetValidation
{
	void CollectTypeMessages(const UComposableCameraTypeAsset& Asset, TArray<FComposableCameraBuildMessage>& Messages);
	void ValidateShot(const FComposableCameraShot& Shot, FDataValidationContext& Context);
	/** Checks asset metadata without loading referenced packages or resolving level actors. */
	void ValidateSoftAsset(const FSoftObjectPath& Reference, const UClass* ExpectedClass,
		FDataValidationContext& Context, const FString& FieldPath);
}
#endif
