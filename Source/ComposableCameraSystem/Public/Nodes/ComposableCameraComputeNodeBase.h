// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "ComposableCameraComputeNodeBase.generated.h"

/**
 * Base class for one-shot compute nodes that run on the BeginPlay chain.
 *
 * Compute nodes are NOT camera nodes in the per-frame-evaluated sense. They
 * run exactly once, between per-node Initialize and the first TickCamera, and
 * their job is to perform arbitrary math / data shaping in C++ and publish
 * the result to camera-level internal variables (or to output pins that are
 * then plumbed through the graph).
 *
 * Why a dedicated base class instead of "just use a camera node"?
 * --------------------------------------------------------------
 *   - Camera nodes are walked per-frame by TickCamera and multicast through
 *     OnPreTick / OnPostTick. A compute node must not touch any of that. It
 *     would otherwise burn hot-path time on values that never change.
 *   - Compute nodes still need the pin system (GetInputPinValue /
 *     SetOutputPinValue / Set/GetInternalVariable), which only lives on
 *     UComposableCameraCameraNodeBase. Inheriting from it is the cheapest
 *     way to get that plumbing without duplicating the RuntimeDataBlock
 *     wiring.
 *   - The editor-side graph hosts these on a separate BeginPlay chain rooted
 *     at UComposableCameraBeginPlayStartGraphNode, parallel to how regular
 *     camera nodes wire off the main Start sentinel. The sync/rebuild phases
 *     classify a UComposableCameraNodeGraphNode as camera-chain vs
 *     compute-chain by testing IsA<UComposableCameraComputeNodeBase> on its
 *     underlying NodeTemplate, which is exactly this base class. So the
 *     runtime discriminator "this belongs on the BeginPlay chain" is the
 *     same type check the editor uses.
 *
 * Why a dedicated class instead of UObject?
 * -----------------------------------------
 *   - K2 math graph nodes from the BlueprintGraph module do not work in our
 *     custom UEdGraph without significant engineering. The pragmatic Option
 *     B is: author a compute node in C++, use FMath / FVector / FQuat /
 *     UKismetMathLibrary directly, publish the result to internal variables,
 *     and let downstream camera nodes consume them.
 *   - Subclasses describe their inputs and outputs with the same pin
 *     declaration system as camera nodes (GetPinDeclarations), which keeps
 *     editor palette / pin rendering / type-asset authoring uniform.
 *
 * Lifecycle
 * ---------
 *   1. Camera activation fires AComposableCameraCameraBase::InitializeNodes.
 *   2. Each compute node has OnInitialize_Implementation invoked via the
 *      inherited Initialize() path (same machinery camera nodes use). This
 *      is where one-time setup, ref caching, and exposed-parameter reads
 *      belong.
 *   3. AActor::BeginPlay fires, which calls
 *      AComposableCameraCameraBase::BeginPlayCamera(), which walks the
 *      ComputeNodes array in order and calls ExecuteBeginPlay() on each.
 *   4. First TickCamera then runs with whatever internal variables / output
 *      pins the compute chain published.
 *
 * Compute nodes must NOT register for OnPreTick / OnPostTick and must NOT
 * override OnTickNode_Implementation. They are not per-frame nodes. The
 * camera's InitializeNodes loop intentionally skips tick-delegate wiring
 * for compute nodes for exactly this reason.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, BlueprintType, Blueprintable, CollapseCategories, ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEM_API UComposableCameraComputeNodeBase
	: public UComposableCameraCameraNodeBase
{
	GENERATED_BODY()

public:
	// Compute-tree default category. Lifts subclasses out of "Misc" without
	// needing each one to set the field explicitly. Concrete compute nodes
	// can still override in their own ctor when a more specific bucket fits
	// (e.g. a future "Compute Nodes|Probe" / "Compute Nodes|IO" group).
	// Must be public so subclasses can call this base ctor; UE's
	// GENERATED_BODY() leaves the access mode at the surrounding default
	// (private), so we restate it here explicitly.
	UComposableCameraComputeNodeBase() { PaletteCategory = TEXT("Math"); }

public:
	/**
	 * Execute this compute node's one-shot work.
	 *
	 * Called from AComposableCameraCameraBase::BeginPlayCamera, after every
	 * node on the camera (both camera nodes and compute nodes) has already
	 * had Initialize() / OnInitialize_Implementation() run. By the time this
	 * fires, OwningCamera / OwningPlayerCameraManager / RuntimeDataBlock are
	 * all wired, so GetInputPinValue / SetOutputPinValue /
	 * Get/SetInternalVariable are all safe to use.
	 *
	 * The outgoing camera pose (the pose the previous camera was evaluating
	 * before this one became active) is available via
	 * OwningPlayerCameraManager->GetCurrentCameraPose(). This is the same
	 * value AActor::BeginPlay used to pass into BeginPlayCamera as a
	 * parameter before Step 4a removed that argument.
	 *
	 * Plain virtual (not a BlueprintNativeEvent) for 4a. If Blueprint
	 * authoring of compute nodes becomes a requirement later, promote this
	 * to a BlueprintNativeEvent following the same
	 * OnFoo / OnFoo_Implementation pattern used by OnInitialize and
	 * OnTickNode on the parent class.
	 */
	virtual void ExecuteBeginPlay() {}

	// Compute nodes are never evaluated in the Level Sequence path -LS skips
	// the entire BeginPlay compute chain because scrubbing a timeline with
	// one-shot initialization logic is ambiguous. Values the compute chain
	// would publish must instead be re-sourced as exposed parameters. The
	// Details-panel customization uses this to show an informational warning
	// when a TypeAsset containing compute nodes is assigned to an LS actor.
	virtual EComposableCameraNodeLevelSequenceCompatibility GetLevelSequenceCompatibility_Implementation() const override
	{
		return EComposableCameraNodeLevelSequenceCompatibility::ComputeOnly;
	}
};
