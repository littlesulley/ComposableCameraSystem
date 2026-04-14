// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DataAssets/ComposableCameraTransitionTableDataAsset.h"
#include "ComposableCameraProjectSettings.generated.h"

/**
 * Developer settings for composable camera system.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Composable Camera System"))
class COMPOSABLECAMERASYSTEM_API UComposableCameraProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Optional project-wide transition routing table.
	 *  Consulted when switching between camera types to resolve the
	 *  transition before falling back to per-camera-type defaults.
	 *  @see UComposableCameraTransitionTableDataAsset for the resolution chain. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Transition")
	TSoftObjectPtr<UComposableCameraTransitionTableDataAsset> TransitionTable;

	/**
	 * Named camera contexts that can be used with ActivateCamera.
	 * Each entry is just a name (e.g., "Gameplay", "UI", "LevelSequence").
	 * The first entry is treated as the base context — it is always present and cannot be popped.
	 * The context stack is strict LIFO: contexts push on top and pop from top.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Context Stack")
	TArray<FName> ContextNames;

	/** Returns true if the given name is a registered context. */
	bool IsValidContextName(FName ContextName) const
	{
		return ContextNames.Contains(ContextName);
	}

	/** Get all context names as a list (for dropdowns / GetOptions). */
	UFUNCTION()
	static TArray<FName> GetContextNames()
	{
		const UComposableCameraProjectSettings* Settings = GetDefault<UComposableCameraProjectSettings>();
		return Settings->ContextNames;
	}
};
