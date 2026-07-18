// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "ComposableCameraModifierDataAsset.generated.h"

class UComposableCameraTransitionBase;
class UComposableCameraModifierBase;

/**
 * Data asset for node modifiers. Every entry is a fixed base wrapper selecting
 * either a generic node-property override or a custom Modifier subclass.
 * Modifiers can only be applied to non-transient cameras.
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraNodeModifierDataAsset
	: public UDataAsset
{
	GENERATED_BODY()

public:
	// Modifier wrappers. Each entry selects Node Type or Custom Modifier Class mode.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Modifier")
	TArray<TObjectPtr<UComposableCameraModifierBase>> Modifiers;

	// Transition when this group of modifiers is applied to current running camera. If this is not set, the camera's default transition will be used.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Modifier")
	TObjectPtr<UComposableCameraTransitionBase> OverrideEnterTransition;

	// Transition when this group of modifiers is removed from current running camera. If this is not set, the camera's default transition will be used.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Modifier")
	TObjectPtr<UComposableCameraTransitionBase> OverrideExitTransition;
	
	/** Boolean gameplay-tag expression selecting cameras for this modifier group.
	 *  Empty means all cameras. Supports nested ALL / ANY / NONE expressions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier")
	FGameplayTagQuery CameraTagQuery;

	/** Legacy exact-match list. Migrated to an ANY query during PostLoad. */
	UPROPERTY(BlueprintReadOnly, Category = "Modifier",
		meta = (DeprecatedProperty, DeprecationMessage = "Use CameraTagQuery instead."))
	FGameplayTagContainer CameraTags;

	// Priority for this group of modifiers. If there are other modifiers of the same type, the ones with higher priority will be chosen.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier", meta = (ClampMin = "0"))
	int32 Priority { 0 };

	/** Empty queries match every camera. */
	bool MatchesCameraTags(const FGameplayTagContainer& InCameraTags) const;

	virtual void PostLoad() override;
};
