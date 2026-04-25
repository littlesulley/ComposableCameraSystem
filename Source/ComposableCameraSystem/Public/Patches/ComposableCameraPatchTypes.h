// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComposableCameraPatchTypes.generated.h"

/**
 * Bitmask of expiration channels that may individually fire to retire a Patch.
 *
 * Mirrors the spirit of EComposableCameraActionExpirationType but is tailored to
 * Patch's "always has an enter/exit envelope" model — there is no Instant variant
 * because every Patch ramps in and out via its envelope.
 *
 * A Patch's effective expiration is the OR of its enabled channels: the first
 * channel to fire flips Phase to Exiting. Bits are independent and stack
 * additively — e.g. (Duration | Manual) means "expires after Duration seconds
 * OR when ExpirePatch is called, whichever comes first".
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EComposableCameraPatchExpirationType : uint8
{
	None = 0 UMETA(Hidden),

	/** Expires after Duration seconds, counted from the moment the envelope reaches Active (alpha == 1). */
	Duration = 1 << 0,

	/** Expires only when ExpirePatch(handle) is called. */
	Manual = 1 << 1,

	/** Expires when the Patch asset's CanRemain() override returns false. */
	Condition = 1 << 2
};
ENUM_CLASS_FLAGS(EComposableCameraPatchExpirationType)


/**
 * Easing curve applied symmetrically to the enter and exit alpha ramps.
 *
 * Asset-only in V1 — there is no per-AddPatch override (an enum has no natural
 * sentinel value, and adding a parallel bool is worse than asset-only). If a
 * future case requires a runtime override, add a sixth `Custom` member with a
 * companion `FRuntimeFloatCurve` pin (see PatchSystemProposal §8.1).
 */
UENUM(BlueprintType)
enum class EComposableCameraPatchEase : uint8
{
	Linear,
	EaseIn,
	EaseOut,
	EaseInOut,
	Smooth
};


/**
 * Lifecycle phase of a Patch instance.
 *
 *   Entering : alpha ramping 0 → 1 over EnterDuration. Patch evaluator already ticks at full fidelity.
 *   Active   : alpha == 1, expiration channels are evaluated each frame.
 *   Exiting  : alpha ramping 1 → 0 over ExitDuration.
 *   Expired  : terminal; instance is removed at the end of PatchManager::Apply.
 */
UENUM(BlueprintType)
enum class EComposableCameraPatchPhase : uint8
{
	Entering,
	Active,
	Exiting,
	Expired
};


/**
 * Caller-provided activation parameters for AddPatch.
 *
 * Each "overridable" field is paired with a `bOverride*` bool tagged
 * `InlineEditConditionToggle` and gated by `EditCondition` on the value
 * field — the same idiom as `FPostProcessSettings::bOverride_*`.
 *
 * Two surfaces, two distinct workflows:
 *
 *   • Details panel (asset details, struct customization): the bool collapses
 *     into an inline checkbox next to the value field. Unchecked → use asset
 *     default; checked → caller value wins. Standard.
 *
 *   • BP `Make FComposableCameraPatchActivateParams` node: UE's
 *     MakeStructHandler treats `InlineEditConditionToggle` bools as *implicit*
 *     override flags. The bool's runtime value is forced TRUE for every value
 *     pin whose `bShowPin` flag is true on the MakeStruct node, and FALSE for
 *     pins whose `bShowPin` is false. **Important UI subtlety**: `bShowPin`
 *     is controlled ONLY by the node's details-panel "Show Pin For …"
 *     checkboxes — NOT by the per-pin eye icon visible on the node body. The
 *     eye icon toggles a different state (advanced/visual collapse) and does
 *     NOT propagate to `bShowPin`, so clicking it leaves `bOverride*=true`
 *     even though the pin appears collapsed. Authoring rule: **to use asset
 *     defaults for a field, uncheck "Show Pin For [FieldName]" in the
 *     MakeStruct node's details panel** (selecting the node shows the list).
 *     (See `K2Node_MakeStruct.cpp:117` `CanBeExposed` returning false for
 *     `InlineEditConditionToggle` properties, and `MakeStructHandler.cpp:189`
 *     where `KCST_Assignment` of `bool = true` is injected only for properties
 *     whose `PropertyEntry.bShowPin` is true.)
 *
 * The paired-bool design (over float-zero sentinels) is what lets a caller
 * legitimately request a literal `0` — e.g. `EnterDuration = 0` for "no
 * fade-in" — without having that confused with "fall back to asset".
 *
 * Fields without an asset-side default — `bExpireOnCameraChange` is the only
 * one — have no override toggle; the per-call value is always used.
 *
 * Exposed parameter / exposed variable values are passed separately as a
 * FComposableCameraParameterBlock argument to AddPatch — mirroring the shape of
 * FComposableCameraActivateParams + ActivateComposableCameraFromTypeAsset, and
 * letting UK2Node_AddCameraPatch generate typed pins for each exposed name
 * without having to set-fields-in-struct on this struct.
 */
USTRUCT(BlueprintType)
struct COMPOSABLECAMERASYSTEM_API FComposableCameraPatchActivateParams
{
	GENERATED_BODY()

	/** When true, EnterDuration overrides the asset's DefaultEnterDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch", meta = (InlineEditConditionToggle))
	bool bOverrideEnterDuration = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch",
		meta = (ClampMin = "0.0", EditCondition = "bOverrideEnterDuration"))
	float EnterDuration = 0.f;

	/** When true, ExitDuration overrides the asset's DefaultExitDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch", meta = (InlineEditConditionToggle))
	bool bOverrideExitDuration = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch",
		meta = (ClampMin = "0.0", EditCondition = "bOverrideExitDuration"))
	float ExitDuration = 0.f;

	/** When true, ExpirationType overrides the asset's DefaultExpirationType. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch", meta = (InlineEditConditionToggle))
	bool bOverrideExpirationType = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch",
		meta = (Bitmask, BitmaskEnum = EComposableCameraPatchExpirationType, EditCondition = "bOverrideExpirationType"))
	uint8 ExpirationType = 0;

	/** When true, Duration overrides the asset's DefaultDuration. Only consulted
	 *  when the Duration channel is enabled (either via the per-call ExpirationType
	 *  override or via the asset default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch", meta = (InlineEditConditionToggle))
	bool bOverrideDuration = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch",
		meta = (ClampMin = "0.0", EditCondition = "bOverrideDuration"))
	float Duration = 0.f;

	/** If true, the Patch flips to Exiting when the owning Director's RunningCamera
	 *  changes. Stacks additively with the ExpirationType channels. No asset-side
	 *  default — always uses this per-call value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bExpireOnCameraChange = false;

	/** When true, LayerIndex below overrides the asset's DefaultLayerIndex. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch", meta = (InlineEditConditionToggle))
	bool bOverrideLayerIndex = false;

	/** Composition order. Lower runs earlier (matches GameplayCameras' StackOrder).
	 *  Only consulted when bOverrideLayerIndex is true; otherwise the asset's
	 *  DefaultLayerIndex wins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch", meta = (EditCondition = "bOverrideLayerIndex"))
	int32 LayerIndex = 0;
};
