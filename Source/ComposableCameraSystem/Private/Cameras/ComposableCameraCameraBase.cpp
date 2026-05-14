// Copyright Sulley. All rights reserved.

#include "Cameras/ComposableCameraCameraBase.h"

#include "Actions/ComposableCameraActionBase.h"
#include "Camera/CameraComponent.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraDebugSnapshot.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/PostProcessUtils.h"
#include "Engine/Scene.h"
#include "Modifiers/ComposableCameraModifierBase.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "Utils/ComposableCameraDebugFormatUtils.h"

namespace ComposableCameraPosePrivate
{
	/** Default fallback focal length used when both FocalLength and FieldOfView are unset. */
	constexpr float DefaultFallbackFocalLengthMM = 35.f;

	/**
	 * Lerp a "sentinel-aware" float field where values <= 0 mean "unset / inherit".
	 * If both sides are valid, regular lerp. If only one side is valid, the valid side's value is used
	 * across the blend (no interpolation, to avoid dragging through the meaningless negative-sentinel range).
	 * If both are unset, the sentinel is preserved.
	 */
	FORCEINLINE float LerpOptional(float From, float To, float Factor)
	{
		const bool bFromValid = (From > 0.f);
		const bool bToValid   = (To   > 0.f); 

		if (bFromValid && bToValid) { return FMath::Lerp(From, To, Factor); }
		if (bToValid)               { return To; }
		if (bFromValid)             { return From; }
		return From;
	}
}

// -------------------------------------------------------------------
// FComposableCameraPose
// -------------------------------------------------------------------

double FComposableCameraPose::GetEffectiveFieldOfView() const
{
	const bool bValidFocalLength = (FocalLength > 0.f);
	const bool bValidFieldOfView = (FieldOfView > 0.0);

	float EffectiveFocalLength = FocalLength;
	if (!bValidFocalLength && !bValidFieldOfView)
	{
		// Neither specified. Fall back to a reasonable default so we never return garbage.
		EffectiveFocalLength = ComposableCameraPosePrivate::DefaultFallbackFocalLengthMM;
	}

	if (EffectiveFocalLength > 0.f && !bValidFieldOfView)
	{
		// Physical mode: compute FOV from focal length and sensor, matching UCineCameraComponent's approach.
		double CroppedSensorWidth = static_cast<double>(SensorWidth) * static_cast<double>(SqueezeFactor);
		const double AspectRatio = (SensorHeight > 0.f) ? (static_cast<double>(SensorWidth) / static_cast<double>(SensorHeight)) : 0.0;
		if (AspectRatio > 0.0)
		{
			const double DesqueezeAspectRatio = AspectRatio * static_cast<double>(SqueezeFactor);
			if (AspectRatio < DesqueezeAspectRatio)
			{
				CroppedSensorWidth *= AspectRatio / DesqueezeAspectRatio;
			}
		}

		const double EffectiveOverscan = 1.0 + static_cast<double>(Overscan);
		return FMath::RadiansToDegrees(2.0 * FMath::Atan(CroppedSensorWidth * EffectiveOverscan / (2.0 * static_cast<double>(EffectiveFocalLength))));
	}

	// Degrees mode: return stored FOV directly.
	return FieldOfView;
}

void FComposableCameraPose::SetFieldOfViewDegrees(double InFieldOfViewDegrees)
{
	FieldOfView = InFieldOfViewDegrees;
	// Clear the focal-length side of the dual-mode so GetEffectiveFieldOfView() returns our degrees value directly.
	FocalLength = -1.f;
}

bool FComposableCameraPose::ApplyPhysicalCameraSettings(FPostProcessSettings& InOutPostProcessSettings, bool bOverwriteSettings) const
{
	const float DoFWeight = FMath::Clamp(PhysicalCameraBlendWeight, 0.f, 1.f);
	const float ExposureWeight = FMath::Clamp(ExposureBlendWeight, 0.f, 1.f);

	if (DoFWeight <= 0.f && ExposureWeight <= 0.f)
	{
		return false;
	}

#define CCS_LERP_PP(SettingName, Value, Weight) \
	if (!InOutPostProcessSettings.bOverride_##SettingName || bOverwriteSettings) \
	{ \
		InOutPostProcessSettings.bOverride_##SettingName = true; \
		InOutPostProcessSettings.SettingName = FMath::Lerp(InOutPostProcessSettings.SettingName, static_cast<decltype(InOutPostProcessSettings.SettingName)>(Value), Weight); \
	}

#define CCS_LERP_DOF_FOCAL_DISTANCE(Value, Weight) \
	if (!InOutPostProcessSettings.bOverride_DepthOfFieldFocalDistance || bOverwriteSettings) \
	{ \
		InOutPostProcessSettings.bOverride_DepthOfFieldFocalDistance = true; \
		if (InOutPostProcessSettings.DepthOfFieldFocalDistance == 0.f || (Value) == 0.f) \
		{ \
			InOutPostProcessSettings.DepthOfFieldFocalDistance = (Value); \
		} \
		else \
		{ \
			InOutPostProcessSettings.DepthOfFieldFocalDistance = FMath::Lerp(InOutPostProcessSettings.DepthOfFieldFocalDistance, (Value), (Weight)); \
		} \
	}

	// Exposure inputs.
	if (ExposureWeight > 0.f)
	{
		CCS_LERP_PP(CameraISO, ISO, ExposureWeight);
		CCS_LERP_PP(CameraShutterSpeed, ShutterSpeed, ExposureWeight);
	}

	// DoF inputs.
	if (DoFWeight > 0.f)
	{
		CCS_LERP_PP(DepthOfFieldFstop, Aperture, DoFWeight);
		CCS_LERP_PP(DepthOfFieldBladeCount, DiaphragmBladeCount, DoFWeight);

		// Only apply focus distance if the pose has a valid one; otherwise leave whatever was already set.
		if (FocusDistance > 0.f)
		{
			CCS_LERP_DOF_FOCAL_DISTANCE(FocusDistance, DoFWeight);
		}

		const float EffectiveOverscan = 1.f + Overscan;
		CCS_LERP_PP(DepthOfFieldScale, 1.f, DoFWeight);
		CCS_LERP_PP(DepthOfFieldSensorWidth, SensorWidth * EffectiveOverscan, DoFWeight);
		CCS_LERP_PP(DepthOfFieldSqueezeFactor, SqueezeFactor, DoFWeight);
	}

#undef CCS_LERP_DOF_FOCAL_DISTANCE
#undef CCS_LERP_PP

	return true;
}

void FComposableCameraPose::BlendBy(const FComposableCameraPose& Other, float OtherWeight)
{
	using namespace ComposableCameraPosePrivate;

	OtherWeight = FMath::Clamp(OtherWeight, 0.f, 1.f);

	// --- Transform ---

	Position = FMath::Lerp(Position, Other.Position, OtherWeight);

	const FRotator DeltaAng = (Other.Rotation - Rotation).GetNormalized();
	Rotation = (Rotation + OtherWeight * DeltaAng).GetNormalized();

	// --- FOV (resolved BEFORE blend; see DesignDoc "FOV resolution invariant") ---
	//
	// Each side's effective FOV is computed in its own mode (degrees or physical),
	// then the two degree values are lerped and stored. FocalLength is cleared so
	// the blended pose is unambiguously in degrees mode. Nothing downstream should
	// re-derive FOV from the (now-stale) FocalLength.

	const double FromFOV = GetEffectiveFieldOfView();
	const double ToFOV   = Other.GetEffectiveFieldOfView();
	FieldOfView = FMath::Lerp(FromFOV, ToFOV, static_cast<double>(OtherWeight));
	FocalLength = -1.f;

	// --- Physical camera numerics (always lerped; gated at apply-time by blend weights) ---

	SensorWidth   = FMath::Lerp(SensorWidth,  Other.SensorWidth,  OtherWeight);
	SensorHeight  = FMath::Lerp(SensorHeight, Other.SensorHeight, OtherWeight);
	Aperture      = FMath::Lerp(Aperture,     Other.Aperture,     OtherWeight);
	ShutterSpeed  = FMath::Lerp(ShutterSpeed, Other.ShutterSpeed, OtherWeight);
	ISO           = FMath::Lerp(ISO,          Other.ISO,          OtherWeight);
	SqueezeFactor = FMath::Lerp(SqueezeFactor,Other.SqueezeFactor,OtherWeight);
	Overscan      = FMath::Lerp(Overscan,     Other.Overscan,     OtherWeight);
	DiaphragmBladeCount = FMath::RoundToInt(FMath::Lerp(static_cast<float>(DiaphragmBladeCount), static_cast<float>(Other.DiaphragmBladeCount), OtherWeight));
	PhysicalCameraBlendWeight = FMath::Lerp(PhysicalCameraBlendWeight, Other.PhysicalCameraBlendWeight, OtherWeight);
	ExposureBlendWeight = FMath::Lerp(ExposureBlendWeight, Other.ExposureBlendWeight, OtherWeight);

	// Sentinel-aware: keep a valid focus distance alive across mixed blends.
	FocusDistance = LerpOptional(FocusDistance, Other.FocusDistance, OtherWeight);

	// --- Projection & aspect ---

	// Numerics lerp normally (only used when in their corresponding mode).
	OrthographicWidth  = FMath::Lerp(OrthographicWidth,  Other.OrthographicWidth,  OtherWeight);
	OrthoNearClipPlane = FMath::Lerp(OrthoNearClipPlane, Other.OrthoNearClipPlane, OtherWeight);
	OrthoFarClipPlane  = FMath::Lerp(OrthoFarClipPlane,  Other.OrthoFarClipPlane,  OtherWeight);

	// Booleans and enums can't be linearly interpolated. Target wins immediately
	// once the blend starts (OtherWeight > 0). This ensures that projection mode,
	// aspect ratio constraints, etc. take effect at the start of a transition into
	// a camera that sets them, rather than snapping mid-blend.
	if (OtherWeight > 0.f)
	{
		ProjectionMode                     = Other.ProjectionMode;
		ConstrainAspectRatio               = Other.ConstrainAspectRatio;
		OverrideAspectRatioAxisConstraint  = Other.OverrideAspectRatioAxisConstraint;
		AspectRatioAxisConstraint          = Other.AspectRatioAxisConstraint;
	}

	// --- Post-process ---
	//
	// Blend all post-process properties via the engine helper. Most fields use
	// `UE_LERP_PP` / `UE_SET_PP` macros that correctly flip `bOverride_X = true`
	// on the result whenever either side had the override. So a field that is
	// overridden only on one side fades in/out smoothly with its flag honoured.
	FPostProcessUtils::BlendPostProcessSettings(PostProcessSettings, Other.PostProcessSettings, OtherWeight);

	// Engine asymmetry workaround. Four fields in
	// `FPostProcessUtils::BlendPostProcessSettings` are hand-rolled outside the
	// `UE_LERP_PP` / `UE_SET_PP` macro path and *read* `OtherTo.bOverride_X`
	// without ever writing `ThisFrom.bOverride_X = true`. Consequences:
	//
	//   - `DepthOfFieldFocalDistance`. Most critical. When blending from a
	//     pose with no DoF override (e.g. a gameplay camera) to a pose that
	//     has one (e.g. `ViewTargetProxyNode` forwarding a `UCineCameraComponent`
	//     ->Sequencer Camera Cut Track to an LS Actor), the value snaps to the
	//     new focal distance on frame 0 but `bOverride_DepthOfFieldFocalDistance`
	//     stays at `false` for the whole blend. The renderer ignores the value,
	//     DoF doesn't draw, and only when `CollapseFinishedTransitions` replaces
	//     the blended pose with the proxy pose directly does `bOverride = true`
	//     reappear -DoF "snaps on" at the very end of the transition.
	//   - `DepthOfFieldMatteBoxFlags`, `LensFlareTints`, `MobileHQGaussian` -
	//     same pattern, less commonly hit but same bug.
	//
	// Fix: match the `UE_LERP_PP` "either-side-had-it" semantic by OR-ing the
	// source override into the blended result. For the first three (numeric /
	// array / linear-color) fields the VALUE was already written by the engine
	// helper and we only need to fix the flag.
	//
	// `MobileHQGaussian` is a bool. It follows the "target wins at OtherWeight
	// > 0" rule used above for `ProjectionMode` / `ConstrainAspectRatio` etc.
	// (see the Projection & aspect block). The engine helper snaps its value at
	// 50 %, which is exactly the mid-blend snap our convention rejects: a bool
	// has no meaningful intermediate state, so deferring the flip to the
	// midpoint produces a visible discontinuity instead of just moving the
	// discontinuity to the start of the blend where it is naturally hidden by
	// the Transition's accompanying numeric ramp. We therefore overwrite both
	// value AND flag at `OtherWeight > 0`.
	if (OtherWeight > 0.f)
	{
		PostProcessSettings.bOverride_DepthOfFieldFocalDistance |= Other.PostProcessSettings.bOverride_DepthOfFieldFocalDistance;
		PostProcessSettings.bOverride_DepthOfFieldMatteBoxFlags |= Other.PostProcessSettings.bOverride_DepthOfFieldMatteBoxFlags;
		PostProcessSettings.bOverride_LensFlareTints            |= Other.PostProcessSettings.bOverride_LensFlareTints;

		if (Other.PostProcessSettings.bOverride_MobileHQGaussian)
		{
			PostProcessSettings.bOverride_MobileHQGaussian = true;
			PostProcessSettings.bMobileHQGaussian          = Other.PostProcessSettings.bMobileHQGaussian;
		}
	}
}

// -------------------------------------------------------------------
// AComposableCameraCameraBase
// -------------------------------------------------------------------


AComposableCameraCameraBase::AComposableCameraCameraBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GetCameraComponent()->bConstrainAspectRatio = false;
}

void AComposableCameraCameraBase::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	AComposableCameraCameraBase* This = CastChecked<AComposableCameraCameraBase>(InThis);
	if (This->OwnedRuntimeDataBlock)
	{
		This->OwnedRuntimeDataBlock->AddReferencedObjects(Collector);
	}
	This->SourceParameterBlock.AddReferencedObjects(Collector);
	Super::AddReferencedObjects(InThis, Collector);
}

void AComposableCameraCameraBase::BeginPlay()
{
	Super::BeginPlay();

	BeginPlayCamera();
}

void AComposableCameraCameraBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	OnPreTick.Clear();
	OnPostTick.Clear();
	OnActionPreTick.Clear();
	OnActionPostTick.Clear();

	PreNodeTickActions.Reset();
	PostNodeTickActions.Reset();
}

void AComposableCameraCameraBase::Initialize(AComposableCameraPlayerCameraManager* Manager)
{
	CameraManager = Manager;

	// Cache the gameplay-tag string for per-tick Insights scope naming.
	// `FGameplayTag::ToString` allocates an FString; the previous code
	// regenerated it every TickCamera, paying one heap alloc per camera
	// per frame just for the dynamic trace label. CameraTag is
	// EditDefaultsOnly, so caching once here is safe.
	CameraTagTraceName = CameraTag.ToString();

	// Per-node initialization is factored out so type-asset cameras can run it
	// later, once OnTypeAssetCameraConstructed has populated CameraNodes.
	InitializeNodes();

	// PCM is optional: the Level Sequence component path drives cameras without
	// a PCM (evaluation happens inside UComposableCameraLevelSequenceComponent,
	// which owns the camera directly). Action binding is PCM-only. It hooks
	// the camera into the action system living on the PCM. So skip it when
	// there is no PCM to hook into.
	if (Manager)
	{
		Manager->BindCameraActionsForNewCamera(this);
	}
}

void AComposableCameraCameraBase::InitializeNodes()
{
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		if (Node)
		{
			Node->Initialize(this, CameraManager);

			// Register node delegates.
			OnPreTick.AddUObject(Node, &UComposableCameraCameraNodeBase::OnPreTick);
			OnPostTick.AddUObject(Node, &UComposableCameraCameraNodeBase::OnPostTick);
		}
	}

	// Initialize compute nodes. They inherit from UComposableCameraCameraNodeBase
	// so they reuse the same Initialize() path (which wires OwningCamera /
	// OwningPlayerCameraManager / RuntimeDataBlock and fires OnInitialize).
	// Unlike camera nodes, they are deliberately NOT registered for OnPreTick /
	// OnPostTick. Compute nodes run exactly once, from BeginPlayCamera, and
	// must not consume hot-path time every frame.
	for (UComposableCameraComputeNodeBase* ComputeNode : ComputeNodes)
	{
		if (ComputeNode)
		{
			ComputeNode->Initialize(this, CameraManager);
		}
	}
}

void AComposableCameraCameraBase::ApplyModifiers(const T_NodeModifier& Modifiers)
{
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		if (!Node)
		{
			continue;
		}
		
		if (const FModifierEntry* Modifier = Modifiers.Find(Node->GetClass()))
		{
			if (Modifier->Modifier)
			{
				Modifier->Modifier->ApplyModifier(Node);
			}
		}
	}
}

void AComposableCameraCameraBase::BeginPlayCamera()
{
	// Walk the BeginPlay compute chain.
	//
	// By the time this runs, InitializeNodes has already fired per-node
	// Initialize on every entry in both CameraNodes and ComputeNodes, so each
	// compute node has its OwningCamera / OwningPlayerCameraManager /
	// RuntimeDataBlock wired and is free to use the pin system and internal
	// variables from inside ExecuteBeginPlay.
	//
	// When ComputeFullExecChain is available, walk it to interleave
	// compute-node execution with scratch-variable writes. Otherwise fall
	// back to the linear walk of ComputeNodes (legacy type-asset cameras
	// saved before ComputeFullExecChain existed).

	if (ComputeFullExecChain.Num() > 0 && OwnedRuntimeDataBlock && OwnedRuntimeDataBlock->IsValid())
	{
		FComposableCameraRuntimeDataBlock& DataBlock = *OwnedRuntimeDataBlock;

		// The compute chain's CameraNodeIndex is in ComputeNodeTemplates
		// space. The data block's output pin offsets use the unified index
		// (NodeTemplates.Num() + ComputeIdx). We need the base offset.
		// TypeAssetNodeTemplateCount is set during OnTypeAssetCameraConstructed
		// to the exact value of TypeAsset->NodeTemplates.Num(), matching what
		// BuildRuntimeDataLayout used to allocate the pin key offsets.
		const int32 ComputeNodeIndexBase = TypeAssetNodeTemplateCount;

		// Indexed walk so SetVariable can cross-reference DataBlock's
		// InvalidSetVariableComputeExecEntries set (see camera-chain comment
		// above for the type-validation rationale).
		for (int32 EntryIdx = 0; EntryIdx < ComputeFullExecChain.Num(); ++EntryIdx)
		{
			const FComposableCameraExecEntry& Entry = ComputeFullExecChain[EntryIdx];
			switch (Entry.EntryType)
			{
			case EComposableCameraExecEntryType::CameraNode:
			{
				if (ComputeNodes.IsValidIndex(Entry.CameraNodeIndex))
				{
					UComposableCameraComputeNodeBase* ComputeNode = ComputeNodes[Entry.CameraNodeIndex];
					if (ComputeNode)
					{
						ComputeNode->ExecuteBeginPlay();
					}
				}
				break;
			}
			case EComposableCameraExecEntryType::SetVariable:
			{
				// Copy the source compute node's output pin value into the
				// internal variable slot. All the metadata (VariableName,
				// SourcePinName, VariableSlotSize) was pre-resolved by the
				// editor's SyncPhase_RebuildComputeExecutionChain.
				if (Entry.CameraNodeIndex == INDEX_NONE
					|| Entry.VariableName.IsNone()
					|| Entry.VariableSlotSize <= 0
					|| DataBlock.InvalidSetVariableComputeExecEntries.Contains(EntryIdx))
				{
					break;
				}

				const int32 RuntimeSourceIdx = ComputeNodeIndexBase + Entry.CameraNodeIndex;
				const FComposableCameraPinKey SourceKey{ RuntimeSourceIdx, Entry.SourcePinName };
				const int32* SourceOffset = DataBlock.OutputPinOffsets.Find(SourceKey);
				const int32* VarOffset = DataBlock.InternalVariableOffsets.Find(Entry.VariableName);
				if (SourceOffset && VarOffset)
				{
					DataBlock.CopySlot(*SourceOffset, *VarOffset, Entry.VariableSlotSize);
				}
				break;
			}
			}
		}
	}
	else
	{
		// Fallback: walk ComputeNodes linearly (legacy type-asset data).
		for (UComposableCameraComputeNodeBase* ComputeNode : ComputeNodes)
		{
			if (ComputeNode)
			{
				ComputeNode->ExecuteBeginPlay();
			}
		}
	}
}

namespace
{
	// Fire every node-scoped action whose TargetNodeClass matches Node's class.
	// Pose is passed as the same in/out slot (matching TickNode's convention -
	// actions mutate the pose in place and the next node/action sees the update).
	//
	// `Action->OnExecute` is allowed to call `PCM->AddCameraAction(...)` /
	// `RemoveCameraAction(...)`, which routes to
	// `AComposableCameraCameraBase::RegisterNodeAction` /
	// `UnregisterNodeAction` and mutates the very `PreNodeTickActions` /
	// `PostNodeTickActions` array we'd be iterating. The PCM-level
	// `bIsUpdatingActions` reentrancy gate covers the PCM `CameraActions`
	// TSet but does NOT cover the camera-side TArrays -Register/Unregister
	// happen unconditionally inside the public Add/Remove paths. Iterating
	// a stable snapshot decouples "what fires this broadcast" from "what
	// gets registered for next broadcast"; AddUnique-induced reallocation
	// or RemoveSingleSwap-induced re-ordering during OnExecute can no
	// longer invalidate the iteration. New registrations made during
	// OnExecute take effect on the NEXT broadcast (matches the PCM
	// pending-add semantics for symmetry).
	//
	// Snapshot stores `TWeakObjectPtr<UComposableCameraActionBase>` -NOT
	// raw pointers. `Action->OnExecute` is a `BlueprintNativeEvent`, so
	// the body can run arbitrary BP that triggers GC mid-loop (sync
	// `LoadObject`, async-load completion, BP exception unwind, slow BP
	// that yields to the engine for a tick). A re-entrant
	// `RemoveCameraAction(SiblingAction)` from inside one OnExecute drops
	// the sibling from `CameraActions` TSet AND from this NodeActions
	// array. So the next GC pass legitimately reclaims the sibling and
	// any later `Action->TargetNodeClass` deref against our raw snapshot
	// reads freed memory. Weak ptr survives the reclaim cleanly; the
	// per-iteration `Pin() + IsValid` check skips reclaimed entries.
	//
	// `TInlineAllocator<8>` keeps the snapshot on the stack for the
	// typical "0- actions targeting this node class" case; oversized
	// cases spill once.
	FORCEINLINE void BroadcastNodeActions(
		const TArray<UComposableCameraActionBase*>& NodeActions,
		UComposableCameraCameraNodeBase* Node,
		float DeltaTime,
		FComposableCameraPose& InOutPose)
	{
		if (NodeActions.Num() == 0)
		{
			return;
		}

		UClass* NodeClass = Node->GetClass();

		// Build a weak-ptr snapshot. Convert from the live raw-pointer
		// array up-front so the loop body can iterate without touching
		// `NodeActions` (which `OnExecute` may mutate).
		TArray<TWeakObjectPtr<UComposableCameraActionBase>, TInlineAllocator<8>> Snapshot;
		Snapshot.Reserve(NodeActions.Num());
		for (UComposableCameraActionBase* Action : NodeActions)
		{
			Snapshot.Emplace(Action);
		}

		for (const TWeakObjectPtr<UComposableCameraActionBase>& Weak : Snapshot)
		{
			UComposableCameraActionBase* Action = Weak.Get();
			if (!IsValid(Action))
			{
				// Reclaimed mid-broadcast (re-entrant Remove + GC, or
				// the action was destroyed by an unrelated path).
				continue;
			}
			if (Action->TargetNodeClass == NodeClass)
			{
				Action->OnExecute(DeltaTime, InOutPose, InOutPose);
			}
		}
	}
}

void AComposableCameraCameraBase::RegisterNodeAction(UComposableCameraActionBase* Action)
{
	if (!Action)
	{
		return;
	}

	// Ignore actions without a target class. Matches the "ignored" ergonomic
	// documented on EComposableCameraActionExecutionType::PreNodeTick/PostNodeTick.
	if (!Action->TargetNodeClass)
	{
		return;
	}

	if (Action->ExecutionType == EComposableCameraActionExecutionType::PreNodeTick)
	{
		PreNodeTickActions.AddUnique(Action);
	}
	else if (Action->ExecutionType == EComposableCameraActionExecutionType::PostNodeTick)
	{
		PostNodeTickActions.AddUnique(Action);
	}
}

void AComposableCameraCameraBase::UnregisterNodeAction(UComposableCameraActionBase* Action)
{
	if (!Action)
	{
		return;
	}
	PreNodeTickActions.RemoveSingleSwap(Action);
	PostNodeTickActions.RemoveSingleSwap(Action);
}

DECLARE_CYCLE_STAT(TEXT("Camera TickCamera"), STAT_CCS_Camera_TickCamera, STATGROUP_CCS);

FComposableCameraPose AComposableCameraCameraBase::TickWithInputPose(
	float DeltaTime, const FComposableCameraPose& InputPose)
{
	// Patch evaluators are driven by their PatchManager via this entry point;
	// the PCM-driven main camera path uses TickCamera directly. Seeding
	// CameraPose with the upstream pose makes the first node in the chain read
	// it as the starting state -TickCamera's existing per-node loop then
	// mutates it the same way it does for non-Patch cameras. TickCamera's
	// end-of-tick `LastFrameCameraPose = CameraPose` snapshot then captures
	// THIS frame's upstream pose for the next frame's delta-style nodes
	// (PivotDamping etc.) to consume. See PatchSystemProposal Section 6.2 / Section 16.7.
	CameraPose = InputPose;
	return TickCamera(DeltaTime);
}

FComposableCameraPose AComposableCameraCameraBase::TickCamera(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_CCS_Camera_TickCamera);
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_Camera_TickCamera);
	// Read the cached trace name from Initialize. Never allocate a fresh
	// FString here on the per-tick hot path.
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(*CameraTagTraceName);

	// Per-frame memoization. Under the snapshot-DAG evaluation topology,
	// a single camera can be reached via multiple paths in one frame
	// (two RefLeaves in different branches both ultimately reach the
	// same underlying leaf). Ticking twice would double-advance per-node
	// state (damping, interpolator `bStartFrame`, spline progress, etc.)
	// AND double-decrement RemainingLifeTime for transient cameras. Both
	// observable bugs. The first call in a given frame does the real
	// tick and stores the result; any subsequent same-frame call returns
	// the cached CameraPose verbatim. GFrameCounter monotonically
	// increases, so the counter value never collides across frames.
	const uint64 CurrentFrame = GFrameCounter;
	if (LastTickedFrameCounter == CurrentFrame)
	{
		return CameraPose;
	}

#if WITH_EDITOR
	ClearNodeDebugFlags();
#endif

	FComposableCameraPose NewCameraPose = CameraPose;

	// Execute pre-tick actions.
	OnActionPreTick.Broadcast(DeltaTime, NewCameraPose, NewCameraPose);

	// Do something before camera tick begins.
	OnPreTick.Broadcast(DeltaTime, NewCameraPose, NewCameraPose);

	// Execute camera nodes, interleaving scratch-variable writes when the
	// FullExecChain is available. Otherwise fall back to the linear walk of
	// CameraNodes (legacy type-asset cameras saved before FullExecChain).
	if (FullExecChain.Num() > 0 && OwnedRuntimeDataBlock && OwnedRuntimeDataBlock->IsValid())
	{
		FComposableCameraRuntimeDataBlock& DataBlock = *OwnedRuntimeDataBlock;

		// Indexed walk so SetVariable can cross-reference DataBlock's
		// InvalidSetVariableExecEntries set, populated by activation-time
		// type validation in BuildRuntimeDataLayout. Range-based form would
		// lose the EntryIdx that the set is keyed on.
		for (int32 EntryIdx = 0; EntryIdx < FullExecChain.Num(); ++EntryIdx)
		{
			const FComposableCameraExecEntry& Entry = FullExecChain[EntryIdx];
			switch (Entry.EntryType)
			{
			case EComposableCameraExecEntryType::CameraNode:
			{
				if (CameraNodes.IsValidIndex(Entry.CameraNodeIndex))
				{
					UComposableCameraCameraNodeBase* Node = CameraNodes[Entry.CameraNodeIndex];
					if (Node)
					{
						BroadcastNodeActions(PreNodeTickActions, Node, DeltaTime, NewCameraPose);
						Node->TickNode(DeltaTime, NewCameraPose, NewCameraPose);
						BroadcastNodeActions(PostNodeTickActions, Node, DeltaTime, NewCameraPose);
					}
				}
				break;
			}
			case EComposableCameraExecEntryType::SetVariable:
			{
				// Copy the source camera node's output pin value into the
				// internal variable slot. Camera chain CameraNodeIndex
				// directly indexes NodeTemplates / CameraNodes (no offset).
				//
				// Skip entries whose source-pin / variable type pair was
				// flagged as incompatible at activation time. Without this
				// gate, a stale entry would cross-read bytes between
				// mismatched-shape slots (Float source -> Actor variable would
				// memcpy 4 bytes past the float slot then have RefreshReferenceSlot
				// hand a garbage pointer to the GC mirror).
				if (Entry.CameraNodeIndex == INDEX_NONE
					|| Entry.VariableName.IsNone()
					|| Entry.VariableSlotSize <= 0
					|| DataBlock.InvalidSetVariableExecEntries.Contains(EntryIdx))
				{
					break;
				}

				const FComposableCameraPinKey SourceKey{ Entry.CameraNodeIndex, Entry.SourcePinName };
				const int32* SourceOffset = DataBlock.OutputPinOffsets.Find(SourceKey);
				const int32* VarOffset = DataBlock.InternalVariableOffsets.Find(Entry.VariableName);
				if (SourceOffset && VarOffset)
				{
					DataBlock.CopySlot(*SourceOffset, *VarOffset, Entry.VariableSlotSize);
				}
				break;
			}
			}
		}
	}
	else
	{
		// Fallback: walk CameraNodes linearly (legacy type-asset data).
		for (int32 LegIdx = 0; LegIdx < CameraNodes.Num(); ++LegIdx)
		{
			UComposableCameraCameraNodeBase* Node = CameraNodes[LegIdx];
			if (Node)
			{
				BroadcastNodeActions(PreNodeTickActions, Node, DeltaTime, NewCameraPose);
				Node->TickNode(DeltaTime, NewCameraPose, NewCameraPose);
				BroadcastNodeActions(PostNodeTickActions, Node, DeltaTime, NewCameraPose);
			}
		}
	}

	// Do something when camera tick finishes.
	OnPostTick.Broadcast(DeltaTime, NewCameraPose, NewCameraPose);

	// Execute post-tick actions.
	OnActionPostTick.Broadcast(DeltaTime, NewCameraPose, NewCameraPose);

	// Cache camera pose.
	LastFrameCameraPose = CameraPose;
	CameraPose = NewCameraPose;

	// Update remaining life time if transient.
	if (bIsTransient)
	{
		RemainingLifeTime = FMath::Max(0.f, RemainingLifeTime - DeltaTime);
	}

	// Memoization stamp. See note at top of function.
	LastTickedFrameCounter = CurrentFrame;

	return CameraPose;
}

UComposableCameraCameraNodeBase* AComposableCameraCameraBase::GetNodeByClass(
	TSubclassOf<UComposableCameraCameraNodeBase> NodeClass)
{
	UComposableCameraCameraNodeBase* Node = nullptr;

	for (UComposableCameraCameraNodeBase* OwningNode : CameraNodes)
	{
		if (OwningNode && OwningNode->GetClass() == NodeClass)
		{
			Node = OwningNode;
			break;
		}
	}

	return Node;
}

// --- Editor Debug Snapshot ----------------------------------------------------

#if WITH_EDITOR

void AComposableCameraCameraBase::ClearNodeDebugFlags()
{
	for (UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		if (Node)
		{
			Node->bDebugWasTickedThisFrame = false;
		}
	}
}

FComposableCameraDebugSnapshot AComposableCameraCameraBase::SnapshotDebugState() const
{
	FComposableCameraDebugSnapshot Snapshot;
	Snapshot.FinalPose = CameraPose;

	// -- Per-node entries --------------------------------------------------

	Snapshot.NodeEntries.Reserve(CameraNodes.Num());

	for (int32 i = 0; i < CameraNodes.Num(); ++i)
	{
		const UComposableCameraCameraNodeBase* Node = CameraNodes[i];
		if (!Node)
		{
			continue;
		}

		FComposableCameraNodeDebugEntry Entry;
		Entry.NodeIndex = i;
		Entry.NodeDisplayName = Node->GetClass()->GetName();
		Entry.bWasTicked = Node->bDebugWasTickedThisFrame;
		Entry.PoseAfterNode = Node->DebugPoseAfterTick;

		// Read output pin values from the data block.
		if (OwnedRuntimeDataBlock && OwnedRuntimeDataBlock->IsValid())
		{
			TArray<FComposableCameraNodePinDeclaration> Pins;
			const_cast<UComposableCameraCameraNodeBase*>(Node)->GatherAllPinDeclarations(Pins);

			for (const FComposableCameraNodePinDeclaration& Pin : Pins)
			{
				if (Pin.Direction == EComposableCameraPinDirection::Output)
				{
					FString Formatted = ComposableCameraDebug::FormatOutputPinValue(
						*OwnedRuntimeDataBlock, i, Pin.PinName, Pin.PinType, Pin.EnumType);
					Entry.OutputPinValues.Emplace(Pin.PinName, MoveTemp(Formatted));
				}
			}
		}

		Snapshot.NodeEntries.Add(MoveTemp(Entry));
	}

	// -- Exposed parameter values ------------------------------------------
	//
	// Cross-reference the type asset's ExposedParameters for pin-type info so
	// we can format the actual values instead of a generic "(set)".

	const UComposableCameraTypeAsset* TypeAsset = SourceTypeAsset.Get();

	if (OwnedRuntimeDataBlock && OwnedRuntimeDataBlock->IsValid() && TypeAsset)
	{
		for (const auto& Pair : OwnedRuntimeDataBlock->ExposedParameterOffsets)
		{
			EComposableCameraPinType ParamType = EComposableCameraPinType::Float;
			const UEnum* ParamEnum = nullptr;
			for (const FComposableCameraExposedParameter& Param : TypeAsset->ExposedParameters)
			{
				if (Param.ParameterName == Pair.Key)
				{
					ParamType = Param.PinType;
					ParamEnum = Param.EnumType;
					break;
				}
			}
			Snapshot.ExposedParameterValues.Emplace(
				Pair.Key,
				ComposableCameraDebug::FormatTypedValue(*OwnedRuntimeDataBlock, Pair.Value, ParamType, ParamEnum));
		}
	}

	// -- Internal + Exposed variable values --------------------------------
	//
	// InternalVariableOffsets holds both InternalVariables and ExposedVariables
	// (they share the same runtime map). Cross-reference both arrays on the
	// type asset for type info.

	if (OwnedRuntimeDataBlock && OwnedRuntimeDataBlock->IsValid() && TypeAsset)
	{
		// Build a quick name->type, enum) lookup across both variable arrays.
		// EnumType is meaningful only for the Enum pin type but keeping it in
		// the same map avoids a second walk over the variable arrays.
		struct FVarTypeInfo { EComposableCameraPinType PinType; const UEnum* EnumType; };
		TMap<FName, FVarTypeInfo> VarTypes;
		for (const FComposableCameraInternalVariable& Var : TypeAsset->InternalVariables)
		{
			VarTypes.Add(Var.VariableName, { Var.VariableType, Var.EnumType });
		}
		for (const FComposableCameraInternalVariable& Var : TypeAsset->ExposedVariables)
		{
			VarTypes.Add(Var.VariableName, { Var.VariableType, Var.EnumType });
		}

		for (const auto& Pair : OwnedRuntimeDataBlock->InternalVariableOffsets)
		{
			EComposableCameraPinType VarType = EComposableCameraPinType::Float;
			const UEnum* VarEnum = nullptr;
			if (const FVarTypeInfo* Found = VarTypes.Find(Pair.Key))
			{
				VarType = Found->PinType;
				VarEnum = Found->EnumType;
			}
			Snapshot.InternalVariableValues.Emplace(
				Pair.Key,
				ComposableCameraDebug::FormatTypedValue(*OwnedRuntimeDataBlock, Pair.Value, VarType, VarEnum));
		}
	}

	Snapshot.bIsValid = true;
	return Snapshot;
}

#endif // WITH_EDITOR

#if !UE_BUILD_SHIPPING
void AComposableCameraCameraBase::DrawCameraDebug(UWorld* World, bool bDrawFrustum) const
{
	if (!World)
	{
		return;
	}

	if (bDrawFrustum)
	{
		// Camera frustum at the pose this camera evaluated to this frame.
		// `CameraPose` is the leaf-local pose. During a transition it may
		// differ from the PCM's blended output pose, which is what the user
		// wants to see (source vs target contributions). Only invoked when
		// the caller determined the player isn't looking through this camera.
		const float FOV = CameraPose.GetEffectiveFieldOfView();
		DrawDebugCamera(
			World,
			CameraPose.Position,
			CameraPose.Rotation,
			FOV,
			/*Scale=*/1.f,
			/*Color=*/FColor::Yellow,
			/*bPersistentLines=*/false,
			/*LifeTime=*/-1.f,
			/*DepthPriority=*/0);
	}

	// Per-node gizmos are always walked. Each override consults its own
	// `CCS.Debug.Viewport.<NodeName>` CVar and early-outs when zero, so
	// node-level gizmos show in both possessed play and F8 eject.
	// `bDrawFrustum` doubles as "viewer is outside the camera": nodes that
	// have a gizmo sitting at the camera's own position (e.g. the self-
	// collision sphere on CollisionPushNode) use this to skip their
	// occluding parts during live gameplay.
	for (const UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		if (Node)
		{
			Node->DrawNodeDebug(World, /*bViewerIsOutsideCamera=*/bDrawFrustum);
		}
	}
}

void AComposableCameraCameraBase::DrawCameraDebug2D(UCanvas* Canvas, APlayerController* PC) const
{
	if (!Canvas) { return; }
	for (const UComposableCameraCameraNodeBase* Node : CameraNodes)
	{
		if (Node)
		{
			Node->DrawNodeDebug2D(Canvas, PC);
		}
	}
}
#endif // !UE_BUILD_SHIPPING

