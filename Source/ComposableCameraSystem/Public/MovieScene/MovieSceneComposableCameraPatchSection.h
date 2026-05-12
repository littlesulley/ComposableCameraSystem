// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "MovieSceneObjectBindingID.h"
#include "Patches/ComposableCameraPatchTypes.h"
#include "Sections/MovieSceneParameterSection.h"
#include "StructUtils/PropertyBag.h"
#include "MovieSceneComposableCameraPatchSection.generated.h"

class UComposableCameraPatchTypeAsset;
struct FComposableCameraParameterBlock;

/**
 * One section on a UMovieSceneComposableCameraPatchTrack — represents a single
 * Patch activation window in the timeline, **bound to a specific
 * AComposableCameraLevelSequenceActor** via TargetActorBinding.
 *
 * The section IS the addressing artifact:
 *   - WHEN the patch is active           → the section's TrueRange.
 *   - WHO it applies to                  → the LS Actor at TargetActorBinding.
 *   - WHAT enter/exit envelope it uses   → Params (asset defaults + per-section
 *                                          overrides; section easing folds in).
 *   - WHAT parameter values it carries   → Parameters / Variables bags
 *                                          (static defaults) + per-name
 *                                          UMovieSceneParameterSection channels
 *                                          (overridden when keyed).
 *
 * Per-frame the TrackInstance:
 *   1. Resolves TargetActorBinding → bound LS Actor → its
 *      UComposableCameraLevelSequenceComponent.
 *   2. Samples parameter block from channels (priority) + bag (fallback).
 *   3. Computes envelope alpha statelessly via
 *      `PatchEnvelope::ComputeStatelessAlpha(playhead, range, durations, …)`.
 *   4. Calls `LSComp->SetSequencerPatchOverlay(this, params, alpha)`.
 *
 * The LS Component then applies all registered overlays in its own TickComponent
 * (after InternalCamera tick, before projection) — sorted by `Params.LayerIndex`,
 * each overlay ticks its cached evaluator with the running pose as input and
 * blends by alpha. Final pose's Position + Rotation + FOV land on the CineCamera.
 *
 * Same code path runs in BOTH editor preview (Sequencer scrub in the editor
 * viewport) and PIE (Camera Cut Track targets the LS Actor). Spawn Tracks own
 * LS Actor lifetime, so patches apply only while the host component exists
 * and ticks.
 *
 * **Patches added via the BP library (`AddCameraPatch(PlayerIndex, ContextName, ...)`)
 * are a separate path** that lives on the gameplay PCM/Director, not here.
 * The two surfaces are intentionally orthogonal:
 *   - Sequencer Section → LS Actor's CineCamera (cinematic overlay).
 *   - BP `AddCameraPatch` → gameplay Director's RunningCamera (gameplay overlay).
 *
 * Inherits from UMovieSceneParameterSection so each ExposedParameter can be
 * promoted to a keyable channel inside the section (Scalar / Bool / Vector2D
 * / Vector3D / Vector4-as-Color named curves with auto channel-proxy
 * reconstruction — same pattern UMaterialParameterCollection /
 * UMovieSceneCustomPrimitiveDataSection use).
 *
 * Parameter resolution order (per ExposedParameter, every frame):
 *
 *   1. Channel exists (user promoted via right-click "Camera Parameters → X")
 *      → sample channel at the current frame.
 *   2. Else → bag default (Parameters / Variables FInstancedPropertyBag — set
 *      in the section's Details panel by the designer).
 *   3. Else → asset's authored default (handled at Patch construction time
 *      by ConstructCameraFromTypeAsset).
 */
UCLASS(MinimalAPI)
class UMovieSceneComposableCameraPatchSection : public UMovieSceneParameterSection
{
	GENERATED_BODY()

public:
	UMovieSceneComposableCameraPatchSection(const FObjectInitializer& ObjectInitializer);

	// UObject.
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// IMovieSceneEntityProvider — overridden to emit our own TrackInstance
	// dispatch entity instead of the parameter-section base's per-curve entities.
	virtual void ImportEntityImpl(
		UMovieSceneEntitySystemLinker* EntityLinker,
		const UE::MovieScene::FEntityImportParams& ImportParams,
		UE::MovieScene::FImportedEntity* OutImportedEntity) override;

	/**
	 * Override the parameter-section base's PopulateEvaluationFieldImpl —
	 * which returns `true` (i.e. "I handle entity-field setup, skip the
	 * standard ImportEntityImpl path"; the base expects its outer track to
	 * call `ExternalPopulateEvaluationField` to inject per-curve entities
	 * into a material/parameter blender). We don't use that pattern — we
	 * want our own per-section TrackInstance dispatch via ImportEntityImpl,
	 * not parameter-blender entities. Returning `false` falls back to the
	 * standard import path so ImportEntityImpl is actually called per
	 * Sequencer evaluation. **Without this override, OnAnimate never fires.**
	 */
	virtual bool PopulateEvaluationFieldImpl(
		const TRange<FFrameNumber>& EffectiveRange,
		const FMovieSceneEvaluationFieldEntityMetaData& InMetaData,
		FMovieSceneEntityComponentFieldBuilder* OutFieldBuilder) override
	{
		return false;
	}


	/** Sync Parameters / Variables bag layouts to the current PatchAsset's
	 *  exposed surface. Preserves values for entries whose name + type survive
	 *  via FInstancedPropertyBag::MigrateToNewBagStruct. Resets both bags empty
	 *  when PatchAsset is null.
	 *
	 *  Note: this only syncs the BAG (static defaults). Channel curves on the
	 *  parent class are NOT auto-created — they're added on demand when the
	 *  designer right-clicks a parameter in the section context menu and chooses
	 *  "Add Keyable Channel" (which calls AddScalarParameterKey / etc. with the
	 *  bag value as the seed key). */
	void RebuildBagsFromPatchAsset();

	/** Build a parameter block at the given frame time. Channel-keyed params
	 *  are sampled from their curves; un-keyed params fall back to bag values.
	 *
	 *  @param CurrentFrame  Frame position to sample channels at. Pass the
	 *                       section start frame for OnInputAdded; pass the
	 *                       sequence instance's current context time for
	 *                       OnAnimate's per-frame re-sync.
	 *  @param OutBlock      Parameter block to populate. */
	void BuildParameterBlock(FFrameNumber CurrentFrame, FComposableCameraParameterBlock& OutBlock) const;

public:
	/** The Patch asset to add when the section enters its range. Required —
	 *  null asset → section is a no-op. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	TObjectPtr<UComposableCameraPatchTypeAsset> PatchAsset;

	/** Per-section envelope / lifetime / composition overrides. Each `bOverride*`
	 *  defaults false → the asset's default field is used. Section easing folds
	 *  in too: if `bOverrideEnterDuration` is unset, `Easing.GetEaseInDuration()`
	 *  is used (same for exit), so dragging the section's ease handles directly
	 *  reshapes the patch envelope.
	 *
	 *  Note: `bExpireOnCameraChange` and the schedule channels (Duration /
	 *  Manual / Condition) inside this struct are designed for the BP-driven
	 *  PCM path — for Sequencer-driven patches the section's TrueRange is the
	 *  authoritative lifetime, and these schedule fields are advisory at best.
	 *  Leaving them at defaults is recommended. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch", meta = (ShowOnlyInnerProperties))
	FComposableCameraPatchActivateParams Params;

	/** Static default values for ExposedParameters. One bag entry per exposed
	 *  parameter, typed by its PinType. Layout rebuilt on PatchAsset change.
	 *  Used as fallback values when no channel curve exists for the parameter. */
	UPROPERTY(EditAnywhere, Category = "Patch", meta = (FixedLayout = true))
	FInstancedPropertyBag Parameters;

	/** Static default values for ExposedVariables. Same shape and lifetime as
	 *  Parameters; carried separately so the track editor's parameter menu
	 *  splits "Camera Parameters" vs. "Camera Variables" into distinct sections. */
	UPROPERTY(EditAnywhere, Category = "Patch", meta = (FixedLayout = true))
	FInstancedPropertyBag Variables;

	/**
	 * Editor-preview binding — when set, the patch's effect is applied onto
	 * the bound `AComposableCameraLevelSequenceActor`'s CineCamera while the
	 * Sequencer scrubber is in this section's range, so designers see the
	 * patch live in the editor viewport without entering PIE. The runtime
	 * (PIE / Game) path is unaffected and continues to drive patches through
	 * the active Director resolved via `PlayerIndex` / `ContextName` above.
	 *
	 * Bind to a `AComposableCameraLevelSequenceActor` Spawnable / Possessable
	 * in the same Sequencer (drag the binding row onto this field, or use
	 * the picker dropdown). If the bound object isn't an LS Actor or doesn't
	 * have a `UComposableCameraLevelSequenceComponent`, editor preview is a
	 * silent no-op.
	 *
	 * Leave unset (NAME_None GUID) to disable editor preview entirely — the
	 * patch will only show when running in PIE.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Preview")
	FMovieSceneObjectBindingID TargetActorBinding;
};
