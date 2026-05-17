// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Patches/ComposableCameraPatchTypes.h"
#include "ComposableCameraPatchTypeAsset.generated.h"

/**
 * Data asset describing a Camera Patch type. A small, time-bounded, additively-
 * composable overlay that reads upstream pose, applies a node graph, writes a
 * modified pose. Authored in the same visual editor as UComposableCameraTypeAsset,
 * with no schema change (PatchSystemProposal Section 5 / Section 16.8).
 *
 * Subclasses UComposableCameraTypeAsset for type-safe API (AddPatch only accepts
 * UComposableCameraPatchTypeAsset*) and a separate Content Browser factory. The
 * graph schema, pin system, parameter / variable system, and runtime data-block
 * layout are all inherited unchanged.
 *
 * Patch-incompatible nodes (those that ignore InPose and synthesize pose from
 * scratch. E.g. RelativeFixedPose, MixingCamera, ViewTargetProxy) will be
 * caught by the upcoming GetPatchCompatibility() node enum + a yellow-banner
 * editor warning. That enum is introduced in a later staging step (see
 * PatchSystemProposal Section 11 / Section 19); until then, any node may be wired into a
 * Patch graph without surface-level guard rails.
 */
UCLASS(BlueprintType, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraPatchTypeAsset : public UComposableCameraTypeAsset
{
	GENERATED_BODY()

public:
	/** Default fade-in duration. Used when AddPatch's EnterDuration sentinel (== 0) is supplied. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Patch|Envelope", meta = (ClampMin = "0.0"))
	float DefaultEnterDuration = 0.25f;

	/** Default fade-out duration. Used when AddPatch's ExitDuration sentinel (== 0) is supplied. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Patch|Envelope", meta = (ClampMin = "0.0"))
	float DefaultExitDuration = 0.25f;

	/** Easing curve applied symmetrically to BOTH the enter and the exit ramp.
	 *  Asset-only. See EComposableCameraPatchEase doc comment for the rationale
	 *  (no natural sentinel for an enum). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Patch|Envelope")
	EComposableCameraPatchEase DefaultEaseType = EComposableCameraPatchEase::EaseInOut;

	/** Default composition order. Lower runs earlier (matches GameplayCameras' StackOrder).
	 *  Per-AddPatch override available via FComposableCameraPatchActivateParams::bOverrideLayerIndex
	 *  + LayerIndex. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Patch|Composition")
	int32 DefaultLayerIndex = 0;

	/** Default expiration channels. Bitmask of EComposableCameraPatchExpirationType.
	 *  Per-AddPatch override always wins when non-zero (no sentinel. Bitmask of 0 from
	 *  the caller is treated as "use asset default"). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Patch|Lifetime",
		meta = (Bitmask, BitmaskEnum = "/Script/ComposableCameraSystem.EComposableCameraPatchExpirationType"))
	uint8 DefaultExpirationType = static_cast<uint8>(EComposableCameraPatchExpirationType::Duration);

	/** Default duration in seconds for the Duration expiration channel. Used when
	 *  AddPatch's Duration sentinel (== 0) is supplied AND the Duration channel
	 *  is enabled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Patch|Lifetime", meta = (ClampMin = "0.0"))
	float DefaultDuration = 0.f;

public:
	/**
	 * Condition expiration hook. Override in a Blueprint subclass to decide per-frame
	 * whether the Patch may remain active. Called ONLY when the Patch is in Active
	 * phase AND the Condition bit of ExpirationType is enabled. Otherwise it is
	 * not consulted. Returning false flips the Patch to Exiting via the standard
	 * envelope ramp (same path as Duration expiration and manual ExpirePatch).
	 *
	 * Signature mirrors UComposableCameraActionBase::CanExecute (PatchSystemProposal
	 * Section 16.10). Same param shape, same BlueprintNativeEvent idiom. UpstreamPose is
	 * the pose this Patch would act on (output of the tree evaluation and all
	 * lower-layer Patches this frame); inspect it to write conditions like "stop
	 * when the player is looking below the horizon" or "stop when FOV drops below 30 deg".
	 *
	 * Default implementation returns true (Patch stays). Override in BP to add real gating.
	 */
	UFUNCTION(BlueprintNativeEvent, DisplayName = "CanRemain", Category = "ComposableCameraSystem|Patch")
	bool CanRemain(float DeltaTime, const FComposableCameraPose& UpstreamPose);
	virtual bool CanRemain_Implementation(float DeltaTime, const FComposableCameraPose& UpstreamPose) { return true; }

#if WITH_EDITOR
	/**
	 * Walk NodeTemplates and emit a build message for any node whose
	 * GetPatchCompatibility() is Incompatible (error) or CompatibleWithCaveat
	 * (warning). Messages flow through BuildMessages so the existing per-node
	 * inline badge infrastructure displays them on the graph node.
	 */
	virtual void ValidateAdditional(TArray<FComposableCameraBuildMessage>& OutMessages) const override;
#endif
};
