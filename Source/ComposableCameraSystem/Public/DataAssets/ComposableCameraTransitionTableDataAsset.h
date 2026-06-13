// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include "ComposableCameraTransitionTableDataAsset.generated.h"

class UComposableCameraTransitionBase;
class UComposableCameraTypeAsset;

/**
 * One entry in the transition routing table.
 *
 * Defines which transition to use when switching from a camera built from
 * SourceTypeAsset to one built from TargetTypeAsset. Both fields are
 * required. The table performs exact-match lookups only. Wildcard /
 * fallback behavior is handled by per-camera ExitTransition and
 * EnterTransition fields on UComposableCameraTypeAsset, which sit
 * below the table in the resolution chain.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraTransitionTableEntry
{
	GENERATED_BODY()

	/** Source camera type asset (required). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition Table")
	TSoftObjectPtr<UComposableCameraTypeAsset> SourceTypeAsset;

	/** Target camera type asset (required). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition Table")
	TSoftObjectPtr<UComposableCameraTypeAsset> TargetTypeAsset;

	/** Transition to use for this (Source->Target) pair. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Transition Table")
	TObjectPtr<UComposableCameraTransitionBase> Transition;

	/** Computed display string for the array header (e.g. "ThirdPerson -> FirstPerson"). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Transition Table")
	FString DisplayTitle;

	/** Rebuild DisplayTitle from current Source/Target. */
	void UpdateDisplayTitle();
};

/**
 * Data asset holding a transition routing table.
 *
 * Provides a centralized, project-level definition of which transition to use
 * when switching between camera type pairs. Referenced from
 * UComposableCameraProjectSettings::TransitionTable.
 *
 * Resolution order when switching from camera A to camera B:
 *
 *   1. Caller-supplied override (TransitionOverride parameter)
 *   2. Table lookup by (A, B) pair . This asset
 *   3. A's ExitTransition          . Source camera declares how to leave
 *   4. B's EnterTransition           . Target camera declares how to enter
 *   5. Hard cut (no transition)
 *
 * Steps 3 and 4 are per-camera-type-asset fields; step 2 is what this table
 * provides. Together they cover both project-wide gameplay routing and
 * per-camera self-contained transitions (puzzle, UI, cinematic cameras).
 *
 * @see UComposableCameraProjectSettings::TransitionTable
 * @see UComposableCameraTypeAsset::EnterTransition
 * @see UComposableCameraTypeAsset::ExitTransition
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraTransitionTableDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** The transition routing entries. Exact-match by (Source, Target) pair;
	 *  first matching entry in declaration order wins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition Table",
		meta = (TitleProperty = "DisplayTitle"))
	TArray<FComposableCameraTransitionTableEntry> Entries;

	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
	virtual void PostLoad() override;
	//~ End UObject Interface

	/**
	 * Look up the transition for an exact (Source, Target) pair.
	 *
	 * @param Source  The currently-active camera's type asset. Returns nullptr if null.
	 * @param Target  The camera type asset being activated. Returns nullptr if null.
	 * @return The matched transition, or nullptr if no entry matches.
	 */
	UComposableCameraTransitionBase* FindTransition(
		const UComposableCameraTypeAsset* Source,
		const UComposableCameraTypeAsset* Target) const;
};
