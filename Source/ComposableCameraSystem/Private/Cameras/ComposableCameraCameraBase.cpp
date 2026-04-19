// Copyright Sulley. All rights reserved.

#include "Cameras/ComposableCameraCameraBase.h"

#include "Actions/ComposableCameraActionBase.h"
#include "Camera/CameraComponent.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraDebugSnapshot.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
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
		// Neither specified — fall back to a reasonable default so we never return garbage.
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
	if (PhysicalCameraBlendWeight <= 0.f)
	{
		return false;
	}

#define CCS_LERP_PP(SettingName, Value) \
	if (!InOutPostProcessSettings.bOverride_##SettingName || bOverwriteSettings) \
	{ \
		InOutPostProcessSettings.bOverride_##SettingName = true; \
		InOutPostProcessSettings.SettingName = FMath::Lerp(InOutPostProcessSettings.SettingName, static_cast<decltype(InOutPostProcessSettings.SettingName)>(Value), PhysicalCameraBlendWeight); \
	}

	// Auto-exposure inputs.
	CCS_LERP_PP(CameraISO, ISO);
	CCS_LERP_PP(CameraShutterSpeed, ShutterSpeed);

	// DoF inputs.
	CCS_LERP_PP(DepthOfFieldFstop, Aperture);
	CCS_LERP_PP(DepthOfFieldBladeCount, DiaphragmBladeCount);

	// Only apply focus distance if the pose has a valid one; otherwise leave whatever was already set.
	if (FocusDistance > 0.f)
	{
		CCS_LERP_PP(DepthOfFieldFocalDistance, FocusDistance);
	}

	const float EffectiveOverscan = 1.f + Overscan;
	CCS_LERP_PP(DepthOfFieldSensorWidth, SensorWidth * EffectiveOverscan);
	CCS_LERP_PP(DepthOfFieldSqueezeFactor, SqueezeFactor);

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
	// the blended pose is unambiguously in degrees mode — nothing downstream should
	// re-derive FOV from the (now-stale) FocalLength.

	const double FromFOV = GetEffectiveFieldOfView();
	const double ToFOV   = Other.GetEffectiveFieldOfView();
	FieldOfView = FMath::Lerp(FromFOV, ToFOV, static_cast<double>(OtherWeight));
	FocalLength = -1.f;

	// --- Physical camera numerics (always lerped; gated at apply-time by PhysicalCameraBlendWeight) ---

	SensorWidth   = FMath::Lerp(SensorWidth,  Other.SensorWidth,  OtherWeight);
	SensorHeight  = FMath::Lerp(SensorHeight, Other.SensorHeight, OtherWeight);
	Aperture      = FMath::Lerp(Aperture,     Other.Aperture,     OtherWeight);
	ShutterSpeed  = FMath::Lerp(ShutterSpeed, Other.ShutterSpeed, OtherWeight);
	ISO           = FMath::Lerp(ISO,          Other.ISO,          OtherWeight);
	SqueezeFactor = FMath::Lerp(SqueezeFactor,Other.SqueezeFactor,OtherWeight);
	Overscan      = FMath::Lerp(Overscan,     Other.Overscan,     OtherWeight);
	DiaphragmBladeCount = FMath::RoundToInt(FMath::Lerp(static_cast<float>(DiaphragmBladeCount), static_cast<float>(Other.DiaphragmBladeCount), OtherWeight));
	PhysicalCameraBlendWeight = FMath::Lerp(PhysicalCameraBlendWeight, Other.PhysicalCameraBlendWeight, OtherWeight);

	// Sentinel-aware: keep a valid focus distance alive across mixed blends.
	FocusDistance = LerpOptional(FocusDistance, Other.FocusDistance, OtherWeight);

	// --- Projection & aspect ---

	// Numerics lerp normally (only used when in their corresponding mode).
	OrthographicWidth  = FMath::Lerp(OrthographicWidth,  Other.OrthographicWidth,  OtherWeight);
	OrthoNearClipPlane = FMath::Lerp(OrthoNearClipPlane, Other.OrthoNearClipPlane, OtherWeight);
	OrthoFarClipPlane  = FMath::Lerp(OrthoFarClipPlane,  Other.OrthoFarClipPlane,  OtherWeight);

	// Booleans and enums can't be linearly interpolated — target wins immediately
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
	// Blend all post-process properties (including bOverride_* flags).
	// A default-constructed FPostProcessSettings has all overrides off, so blending
	// against a camera with no PostProcess node naturally fades overridden values
	// toward off. The bOverride_* booleans snap at 50% (integer lerp behavior).
	FPostProcessUtils::BlendPostProcessSettings(PostProcessSettings, Other.PostProcessSettings, OtherWeight);
}

// -------------------------------------------------------------------
// AComposableCameraCameraBase
// -------------------------------------------------------------------


AComposableCameraCameraBase::AComposableCameraCameraBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GetCameraComponent()->bConstrainAspectRatio = false;
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
}

void AComposableCameraCameraBase::Initialize(AComposableCameraPlayerCameraManager* Manager)
{
	CameraManager = Manager;

	// Per-node initialization is factored out so type-asset cameras can run it
	// later, once OnTypeAssetCameraConstructed has populated CameraNodes.
	InitializeNodes();

	Manager->BindCameraActionsForNewCamera(this);
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
	// OnPostTick — compute nodes run exactly once, from BeginPlayCamera, and
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

		for (const FComposableCameraExecEntry& Entry : ComputeFullExecChain)
		{
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
					|| Entry.VariableSlotSize <= 0)
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

FComposableCameraPose AComposableCameraCameraBase::TickCamera(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CCS_Camera_TickCamera);
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(*CameraTag.ToString());

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

		for (const FComposableCameraExecEntry& Entry : FullExecChain)
		{
			switch (Entry.EntryType)
			{
			case EComposableCameraExecEntryType::CameraNode:
			{
				if (CameraNodes.IsValidIndex(Entry.CameraNodeIndex))
				{
					UComposableCameraCameraNodeBase* Node = CameraNodes[Entry.CameraNodeIndex];
					if (Node)
					{
						Node->TickNode(DeltaTime, NewCameraPose, NewCameraPose);
					}
				}
				break;
			}
			case EComposableCameraExecEntryType::SetVariable:
			{
				// Copy the source camera node's output pin value into the
				// internal variable slot. Camera chain CameraNodeIndex
				// directly indexes NodeTemplates / CameraNodes (no offset).
				if (Entry.CameraNodeIndex == INDEX_NONE
					|| Entry.VariableName.IsNone()
					|| Entry.VariableSlotSize <= 0)
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
				Node->TickNode(DeltaTime, NewCameraPose, NewCameraPose);
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

// ─── Editor Debug Snapshot ────────────────────────────────────────────────────

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

	// ── Per-node entries ──────────────────────────────────────────────────

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
		Entry.NodeDisplayName = Node->GetClass()->GetDisplayNameText().ToString();
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

	// ── Exposed parameter values ──────────────────────────────────────────
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

	// ── Internal + Exposed variable values ────────────────────────────────
	//
	// InternalVariableOffsets holds both InternalVariables and ExposedVariables
	// (they share the same runtime map). Cross-reference both arrays on the
	// type asset for type info.

	if (OwnedRuntimeDataBlock && OwnedRuntimeDataBlock->IsValid() && TypeAsset)
	{
		// Build a quick name→(type, enum) lookup across both variable arrays.
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


