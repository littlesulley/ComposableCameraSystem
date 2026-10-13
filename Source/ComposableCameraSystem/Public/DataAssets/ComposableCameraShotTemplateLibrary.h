// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ComposableCameraShotTemplateLibrary.generated.h"

class UComposableCameraShotAsset;

/** A named choice pointing to an existing Shot asset. */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraShotTemplate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Template")
	FName Name = TEXT("New Shot");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Template")
	TObjectPtr<UComposableCameraShotAsset> Shot = nullptr;
};

/** User-authored template choices. Draft creation copies the referenced asset's Shot by value. */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem, meta = (DisplayName = "Composable Camera Shot Template Library"))
class COMPOSABLECAMERASYSTEM_API UComposableCameraShotTemplateLibrary : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Templates", meta = (TitleProperty = "Name"))
	TArray<FComposableCameraShotTemplate> Shots;
};
