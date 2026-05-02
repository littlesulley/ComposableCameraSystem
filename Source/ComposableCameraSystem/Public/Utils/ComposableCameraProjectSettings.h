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

	// ─── Composition Solver — AnchorAtScreen Picard tuning ────────────────
	//
	// Parameters for the joint position+rotation Picard iteration in
	// `SolveAnchorAtScreen` (spec §4.3, TechDoc §3.25). Defaults are
	// hand-tuned for typical character-scale framing (Distance 50-2000 cm,
	// off-center ScreenPosition); projects with unusual scale ranges
	// (architectural fly-throughs, micro-scale tabletop) may need to
	// retune. Re-reads happen once per `SolveShot` call into locals — no
	// per-iteration `GetDefault<>` cost.

	/** Maximum Picard iterations before giving up. Hard fail returns
	 *  `bValid=false`; caller (CompositionFramingNode / Shot Editor preview)
	 *  preserves the upstream pose. Higher = more stress-geometry tolerance
	 *  at a per-frame cost ceiling; lower = quicker fail-out on truly
	 *  unsolvable inputs. Typical convergence is 3-6 iters; the cap matters
	 *  only for stress / edge cases. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Composition Solver|Picard",
		meta = (ClampMin = "1", ClampMax = "256"))
	int32 PicardMaxIterations = 16;

	/** Convergence tolerance in cm. The iteration stops when the un-damped
	 *  step's `||Candidate - PrevCamPos||` drops below this. Smaller =
	 *  tighter convergence (more iters, more stable framing under solver
	 *  noise); larger = looser (fewer iters, possible visible jitter on
	 *  off-center geometry). 0.01 cm matches what the eye can't see at
	 *  typical character scale (~200 cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Composition Solver|Picard",
		meta = (ClampMin = "0.0001", ClampMax = "10.0", Units = "cm"))
	float PicardConvergenceTolerance = 0.01f;

	/** Damping factor α ∈ (0, 1] for the Picard step:
	 *  `OutCamPos = Lerp(PrevCamPos, Candidate, α)`. 1.0 = un-damped (fastest
	 *  when stable, prone to oscillation on off-center / short-Distance
	 *  geometry); 0.7 = moderate damping (default, suppresses oscillation
	 *  with mild iter-count cost); lower = more damping, even more
	 *  oscillation suppression but slower convergence. The convergence
	 *  test runs on the un-damped residual so damping doesn't affect the
	 *  fixed point — only path length to it. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Composition Solver|Picard",
		meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float PicardRelaxation = 0.7f;

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
