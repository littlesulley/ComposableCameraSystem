// Copyright Sulley. All rights reserved.

#include "Cameras/ComposableCameraCameraBase.h"

#include "Actions/ComposableCameraActionBase.h"
#include "Camera/CameraComponent.h"
#include "ComposableCameraSystemModule.h"
#include "Core/ComposableCameraDebugSnapshot.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Modifiers/ComposableCameraModifierBase.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "Utils/ComposableCameraDebugFormatUtils.h"

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
						*OwnedRuntimeDataBlock, i, Pin.PinName, Pin.PinType);
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
			for (const FComposableCameraExposedParameter& Param : TypeAsset->ExposedParameters)
			{
				if (Param.ParameterName == Pair.Key)
				{
					ParamType = Param.PinType;
					break;
				}
			}
			Snapshot.ExposedParameterValues.Emplace(
				Pair.Key,
				ComposableCameraDebug::FormatTypedValue(*OwnedRuntimeDataBlock, Pair.Value, ParamType));
		}
	}

	// ── Internal + Exposed variable values ────────────────────────────────
	//
	// InternalVariableOffsets holds both InternalVariables and ExposedVariables
	// (they share the same runtime map). Cross-reference both arrays on the
	// type asset for type info.

	if (OwnedRuntimeDataBlock && OwnedRuntimeDataBlock->IsValid() && TypeAsset)
	{
		// Build a quick name→type lookup across both variable arrays.
		TMap<FName, EComposableCameraPinType> VarTypes;
		for (const FComposableCameraInternalVariable& Var : TypeAsset->InternalVariables)
		{
			VarTypes.Add(Var.VariableName, Var.VariableType);
		}
		for (const FComposableCameraInternalVariable& Var : TypeAsset->ExposedVariables)
		{
			VarTypes.Add(Var.VariableName, Var.VariableType);
		}

		for (const auto& Pair : OwnedRuntimeDataBlock->InternalVariableOffsets)
		{
			EComposableCameraPinType VarType = EComposableCameraPinType::Float;
			if (const EComposableCameraPinType* Found = VarTypes.Find(Pair.Key))
			{
				VarType = *Found;
			}
			Snapshot.InternalVariableValues.Emplace(
				Pair.Key,
				ComposableCameraDebug::FormatTypedValue(*OwnedRuntimeDataBlock, Pair.Value, VarType));
		}
	}

	Snapshot.bIsValid = true;
	return Snapshot;
}

#endif // WITH_EDITOR


