// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "ComposableCameraModifierDataAsset.generated.h"

class UComposableCameraTransitionBase;
class UComposableCameraModifierTransitionBase;
class UComposableCameraModifierBase;

/** How runtime changes to this Modifier asset affect the current camera. */
UENUM(BlueprintType)
enum class EComposableCameraModifierApplyMode : uint8
{
	/** Existing behavior. Reconstruct the current camera and pose-blend to it. */
	ReactivateCamera = 0,

	/** Keep the camera instance and transition only the checked node values. */
	ModifyExistingInstance
};

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
	/**
	 * Existing assets deserialize to ReactivateCamera because it is value zero.
	 * ModifyExistingInstance is opt-in and never inserts nodes into the
	 * Evaluation Tree.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier")
	EComposableCameraModifierApplyMode ApplyMode =
		EComposableCameraModifierApplyMode::ReactivateCamera;

	// Modifier wrappers. Each entry selects Node Type or Custom Modifier Class mode.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Modifier")
	TArray<TObjectPtr<UComposableCameraModifierBase>> Modifiers;

	// Transition when this group of modifiers is applied to current running camera. If this is not set, the camera's default transition will be used.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Modifier",
		meta = (EditCondition = "ApplyMode==EComposableCameraModifierApplyMode::ReactivateCamera", EditConditionHides))
	TObjectPtr<UComposableCameraTransitionBase> OverrideEnterTransition;

	// Transition when this group of modifiers is removed from current running camera. If this is not set, the camera's default transition will be used.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Modifier",
		meta = (EditCondition = "ApplyMode==EComposableCameraModifierApplyMode::ReactivateCamera", EditConditionHides))
	TObjectPtr<UComposableCameraTransitionBase> OverrideExitTransition;

	/** Value transition for properties owned only by this newly effective
	 *  in-place Modifier. Null means immediate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Modifier",
		meta = (EditCondition = "ApplyMode==EComposableCameraModifierApplyMode::ModifyExistingInstance", EditConditionHides))
	TObjectPtr<UComposableCameraModifierTransitionBase> OverrideEnterValueTransition;

	/**
	 * Value transition used when a property is overridden by both the old and
	 * new effective in-place Modifiers. Null preserves legacy selection:
	 * desired Enter when its priority is at least the previous priority,
	 * otherwise previous Exit. Use a zero-duration transition for an explicitly
	 * immediate replacement.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Modifier",
		meta = (EditCondition = "ApplyMode==EComposableCameraModifierApplyMode::ModifyExistingInstance", EditConditionHides))
	TObjectPtr<UComposableCameraModifierTransitionBase> OverrideReplaceValueTransition;

	/** Value transition for properties owned only by this previous in-place
	 *  Modifier. Null means immediate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Modifier",
		meta = (EditCondition = "ApplyMode==EComposableCameraModifierApplyMode::ModifyExistingInstance", EditConditionHides))
	TObjectPtr<UComposableCameraModifierTransitionBase> OverrideExitValueTransition;
	
	/** Boolean gameplay-tag expression selecting cameras for this modifier group.
	 *  Empty means all cameras. Supports nested ALL / ANY / NONE expressions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier")
	FGameplayTagQuery CameraTagQuery;

	/** Legacy exact-match list. Migrated to an ANY query during PostLoad. */
	UPROPERTY(BlueprintReadOnly, Category = "Modifier",
		meta = (DeprecatedProperty, DeprecationMessage = "Use CameraTagQuery instead."))
	FGameplayTagContainer CameraTags;

	// Priority for this asset's property claims. Higher priority wins when
	// multiple Node Type entries target the same exact node-class property.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier", meta = (ClampMin = "0"))
	int32 Priority { 0 };

	/** Empty queries match every camera. */
	bool MatchesCameraTags(const FGameplayTagContainer& InCameraTags) const;

	virtual void PostLoad() override;
};
