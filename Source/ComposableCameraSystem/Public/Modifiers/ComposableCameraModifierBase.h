// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "ComposableCameraModifierBase.generated.h"

class FProperty;
class UComposableCameraCameraNodeBase;

/**
 * A node property override applied when a non-transient camera is constructed.
 *
 * Modifier-data-asset entries always use this class as a wrapper. Generic mode
 * selects a NodeTemplate and opts into individual editable properties. Custom
 * mode owns a user-authored subclass and delegates to its ApplyModifier event.
 */
UCLASS(Blueprintable, BlueprintType, DefaultToInstanced, EditInlineNew, ClassGroup = ComposableCameraSystem, CollapseCategories)
class COMPOSABLECAMERASYSTEM_API UComposableCameraModifierBase : public UObject
{
	GENERATED_BODY()

public:
	/** False = generic Node Type override. True = user-authored Modifier subclass. */
	UPROPERTY()
	bool bUseCustomModifierClass { false };

	/** User-authored modifier instance used only when bUseCustomModifierClass is true. */
	UPROPERTY(Instanced)
	TObjectPtr<UComposableCameraModifierBase> CustomModifier;

	/**
	 * Legacy target class for Blueprint modifiers that implement ApplyModifier.
	 * CustomModifier subclasses author this field; generic wrappers derive their
	 * target class from NodeTemplate instead.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifier", meta = (NoEditInline))
	TSubclassOf<UComposableCameraCameraNodeBase> NodeClass;

	/** A built-in or Blueprint camera-node instance holding authored override values. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "Modifier")
	TObjectPtr<UComposableCameraCameraNodeBase> NodeTemplate;

	/** Serialized names of NodeTemplate properties explicitly enabled by the author. */
	UPROPERTY()
	TSet<FName> OverrideProperties;

public:
	/** Returns the exact node class targeted by this modifier. */
	TSubclassOf<UComposableCameraCameraNodeBase> GetTargetNodeClass() const;

	/** True when the active branch copies a node template before initialization. */
	bool UsesNodeTemplateOverride() const;

	/** Applies checked NodeTemplate properties, or the legacy Blueprint callback. */
	void ApplyModifierToNode(UComposableCameraCameraNodeBase* Node);

	/** Shared runtime/editor eligibility rule for a node property. */
	static bool IsNodePropertyOverridable(const FProperty* Property);

	/**
	 * Legacy extension point. New modifier assets do not need a Blueprint class
	 * or this function: select a node type and check the properties to override.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "ComposableCameraSystem|Modifiers")
	void ApplyModifier(UComposableCameraCameraNodeBase* Node);
};
