// Copyright Sulley. All rights reserved.

#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraComputeNodeBase.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "ComposableCameraEdGraphPinTypeUtils.h"
#include "ComposableCameraEditorStyle.h"
#include "ComposableCameraSystemEditorModule.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "ComposableCameraNodeGraphNode"

// PN_ExecIn / PN_ExecOut are defined on UComposableCameraGraphNodeBase
// (see ComposableCameraGraphNodeBase.cpp). This subclass inherits them
// and references them by their unqualified names below.

void UComposableCameraNodeGraphNode::AllocateDefaultPins()
{
	if (!NodeTemplate)
	{
		return;
	}

	// Execution pins - every camera node has exec in and exec out. The
	// boilerplate (FEdGraphPinType + CreatePin + empty friendly name) lives
	// on the shared base class so all three pipeline node types stay in sync.
	CreateExecInPin();
	CreateExecOutPin();

	// Data pins from the node template's declarations. Per-instance pin
	// overrides (stored on the type asset, cached in RuntimePinOverrides)
	// can hide a declared pin entirely (bAsPin == false) or replace its
	// default-value string. Hidden pins don't get a UEdGraphPin at all - 
	// they become Details-only constants that the runtime reads from the
	// override entry.
	TArray<FComposableCameraNodePinDeclaration> Declarations;
	NodeTemplate->GatherAllPinDeclarations(Declarations);

	for (const FComposableCameraNodePinDeclaration& Decl: Declarations)
	{
		// Honor the per-instance "As Pin" toggle. A pin with bAsPin == false
		// never materializes on the graph node, which is why exposure and
		// wiring both become unreachable from the UI for it - the Details
		// panel is the only surface that still sees the value.
		if (Decl.Direction == EComposableCameraPinDirection::Input
			&& !GetEffectivePinAsPin(Decl.PinName))
		{
			continue;
		}

		UEdGraphPin* NewPin = CreatePinFromDeclaration(Decl);
		if (!NewPin)
		{
			continue;
		}

		// Replace the class-level default with the per-asset override if the
		// user has authored one. Output pins never carry a default value, so
		// the override only applies to inputs.
		if (Decl.Direction == EComposableCameraPinDirection::Input)
		{
			if (const FComposableCameraPinOverride* Override = FindPinOverride(Decl.PinName))
			{
				if (Override->bHasDefaultOverride)
				{
					NewPin->DefaultValue = Override->DefaultValueOverride;
				}
			}
		}

		// Issue 1: Mark exposed input pins with a visual indicator. Use the
		// label already on NewPin (set in CreatePinFromDeclaration) rather
		// than Decl.DisplayName, so the `*` suffix for required pins is
		// preserved - e.g. `ArmLength* (Exposed)` rather than dropping back
		// to `ArmLength (Exposed)`.
		if (Decl.Direction == EComposableCameraPinDirection::Input
			&& IsInputPinExposed(Decl.PinName))
		{
			NewPin->PinFriendlyName = FText::Format(LOCTEXT("ExposedPinLabel", "{0} (Exposed)"), NewPin->PinFriendlyName);
		}
	}

}

FText UComposableCameraNodeGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (NodeTemplate)
	{
		// Display-name resolution lives on the shared base so the title bar
		// here and the schema's "Camera Nodes" palette stay in lockstep - 
		// see UComposableCameraGraphNodeBase::GetCameraNodeDisplayNameForClass.
		return GetCameraNodeDisplayNameForClass(NodeTemplate->GetClass());
	}
	return LOCTEXT("UnknownNode", "Unknown Node");
}

FLinearColor UComposableCameraNodeGraphNode::GetNodeTitleColor() const
{
	// Compute nodes use warm amber - same family as the BeginPlay Start
	// sentinel but slightly lighter to distinguish the sentinel from its
	// payload nodes while keeping visual grouping clear. Regular camera
	// nodes use teal to match the type asset color. Palette lives in
	// FComposableCameraEditorColors (ComposableCameraEditorStyle.h).
	if (NodeTemplate && NodeTemplate->IsA<UComposableCameraComputeNodeBase>())
	{
		return FComposableCameraEditorColors::ComputeNodeTitle;
	}
	return FComposableCameraEditorColors::CameraNodeTitle;
}

FText UComposableCameraNodeGraphNode::GetTooltipText() const
{
	FString Tooltip;

	if (NodeTemplate)
	{
		const UClass* NodeClass = NodeTemplate->GetClass();

		// Prefer the UCLASS(meta = (ToolTip = "...")) description when available.
		// Falls back to the class DisplayName, then the raw class name.
		const FString& ClassToolTip = NodeClass->GetMetaData(TEXT("ToolTip"));
		Tooltip = !ClassToolTip.IsEmpty()
			? ClassToolTip: NodeClass->GetDisplayNameText().ToString();
	}

	// When the post-sync validation pass has flagged this node, surface the
	// accumulated messages in the tooltip so hovering the node (or its
	// inline warning / error badge) reveals what's wrong. The badge itself
	// is the visual cue; the tooltip carries the details.
	if (bHasCompilerMessage && !ErrorMsg.IsEmpty())
	{
		if (!Tooltip.IsEmpty())
		{
			Tooltip += TEXT("\n\n");
		}
		Tooltip += ErrorMsg;
	}

	return Tooltip.IsEmpty() ? FText::GetEmpty() : FText::FromString(Tooltip);
}

void UComposableCameraNodeGraphNode::PrepareForCopying()
{
	// Snapshot the Transient fields into their non-Transient copy-paste
	// counterparts so they survive the clipboard text round-trip. See the
	// header comment on CopyPasteNodeTemplate for why this is necessary.
	if (NodeTemplate)
	{
		CopyPasteNodeTemplate = DuplicateObject<UComposableCameraCameraNodeBase>(NodeTemplate, this);
	}
	else
	{
		CopyPasteNodeTemplate = nullptr;
	}
	CopyPastePinOverrides = RuntimePinOverrides;
}

void UComposableCameraNodeGraphNode::PostPasteNode()
{
	Super::PostPasteNode();

	// Adopt the copy-paste transport template as our live NodeTemplate.
	// PrepareForCopying duplicated the original template into
	// CopyPasteNodeTemplate (non-Transient, so it survived the clipboard
	// round-trip); the Transient NodeTemplate was null after import. Move
	// the transported template into NodeTemplate and reparent it under the
	// owning TypeAsset so SyncToTypeAsset picks it up as a new entry in
	// NodeTemplates (or ComputeNodeTemplates).
	if (CopyPasteNodeTemplate)
	{
		// Find the TypeAsset we need to reparent the template under. The
		// TypeAsset is the EdGraph's outer. If we can't resolve it, the
		// paste is in an invalid context - abandon the template so
		// SyncToTypeAsset skips this node (null templates are filtered out
		// in SyncPhase_CollectGraphNodes).
		UObject* TargetOuter = nullptr;
		if (UEdGraph* Graph = GetGraph())
		{
			TargetOuter = Graph->GetOuter();
		}

		if (TargetOuter)
		{
			NodeTemplate = CopyPasteNodeTemplate;
			CopyPasteNodeTemplate = nullptr;

			// Reparent the template under the TypeAsset so it joins the same
			// ownership pool as existing templates.
			NodeTemplate->Rename(nullptr, TargetOuter,
				REN_DontCreateRedirectors | REN_DoNotDirty);

			// Restore the pin overrides that came along for the ride.
			RuntimePinOverrides = MoveTemp(CopyPastePinOverrides);
		}
		else
		{
			UE_LOG(LogComposableCameraSystemEditor, Warning,
				TEXT("PostPasteNode: Could not resolve TypeAsset outer - "
					 "pasted node template will be discarded."));
			CopyPasteNodeTemplate = nullptr;
		}
		CopyPastePinOverrides.Empty();
	}

	// Pasted nodes are brand-new - they don't correspond to any existing
	// slot in NodeTemplates. SyncToTypeAsset will assign the real index.
	NodeIndex = INDEX_NONE;
}

void UComposableCameraNodeGraphNode::PostEditUndo()
{
	Super::PostEditUndo();

	// After an undo/redo, RuntimePinOverrides has been rolled back to its
	// previous serialized state, but the live Pins array may still reference
	// UEdGraphPin objects that ReconstructPins() trashed on the way in/out of
	// the transaction. Rebuild pins from scratch using the restored override
	// state so the on-screen pin set matches what the user expects. The
	// matching logic in ReconstructPins will carry any LinkedTo that the
	// transaction restored onto the freshly-created pins, so wires on the
	// other side reconnect at the same time.
	ReconstructPins();

	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}

void UComposableCameraNodeGraphNode::ReconstructPins()
{
	Modify();

	// Snapshot the old pins before rebuilding so we can preserve any wires the
	// user has already connected. A naive nuke-and-recreate would silently drop
	// every wire on this node, which is the wrong behavior when we only wanted
	// to refresh a single pin (e.g. flip a pin's "(Exposed)" label, swap a
	// variable's type). Callers that explicitly want to break a specific wire
	// are expected to call BreakAllPinLinks on that one pin before calling us.
	TArray<UEdGraphPin*> OldPins = MoveTemp(Pins);
	Pins.Reset();

	// Re-create from declarations.
	AllocateDefaultPins();

	// Re-wire: for each new pin, find a matching old pin (same PinName and
	// same direction) and move its persistent data (LinkedTo, DefaultValue,
	// DefaultObject, DefaultTextValue) onto the new pin. MovePersistentDataFromOldPin
	// takes care of updating the LinkedTo references on the other side of
	// each wire to point at NewPin, so the graph stays consistent.
	for (UEdGraphPin* NewPin: Pins)
	{
		for (UEdGraphPin*& OldPin: OldPins)
		{
			// Match on (PinName, Direction, PinType). If the type changed for a
			// pin with the same name, the existing wire is almost certainly no
			// longer valid, so we intentionally drop it rather than carry an
			// incompatible link into the new layout.
			if (OldPin
				&& OldPin->PinName == NewPin->PinName
				&& OldPin->Direction == NewPin->Direction
				&& OldPin->PinType == NewPin->PinType)
			{
				NewPin->MovePersistentDataFromOldPin(*OldPin);
				OldPin = nullptr; // consumed - don't match again
				break;
			}
		}
	}

	// Destroy any old pins that did not have a matching new pin (i.e. a pin
	// declaration was removed). BreakAllPinLinks clears stale LinkedTo entries
	// on the other side before we mark the pin as garbage.
	for (UEdGraphPin* OldPin: OldPins)
	{
		if (OldPin)
		{
			OldPin->BreakAllPinLinks();
			OldPin->MarkAsGarbage();
		}
	}
}

bool UComposableCameraNodeGraphNode::IsInputPinExposed(FName PinName) const
{
	// Walk up to the owning graph -> type asset and check ExposedParameters.
	if (UEdGraph* Graph = GetGraph())
	{
		if (UComposableCameraTypeAsset* TypeAsset = Cast<UComposableCameraTypeAsset>(Graph->GetOuter()))
		{
			for (const FComposableCameraExposedParameter& Param: TypeAsset->ExposedParameters)
			{
				if (Param.TargetNodeIndex == NodeIndex && Param.TargetPinName == PinName)
				{
					return true;
				}
			}
		}
	}
	return false;
}

void UComposableCameraNodeGraphNode::ExposePinAsParameter(FName PinName)
{
	// Find the pin declaration.
	if (!NodeTemplate)
	{
		return;
	}

	TArray<FComposableCameraNodePinDeclaration> Declarations;
	NodeTemplate->GatherAllPinDeclarations(Declarations);

	const FComposableCameraNodePinDeclaration* FoundDecl = nullptr;
	for (const FComposableCameraNodePinDeclaration& Decl: Declarations)
	{
		if (Decl.PinName == PinName && Decl.Direction == EComposableCameraPinDirection::Input)
		{
			FoundDecl = &Decl;
			break;
		}
	}

	if (!FoundDecl)
	{
		return;
	}

	// Find the type asset.
	if (UEdGraph* Graph = GetGraph())
	{
		if (UComposableCameraTypeAsset* TypeAsset = Cast<UComposableCameraTypeAsset>(Graph->GetOuter()))
		{
			// Capture pre-change snapshots of BOTH this GraphNode and the
			// owning type asset so the surrounding transaction can roll back:
			// - ExposedParameters / PinConnections on TypeAsset
			// - the Pins array on this GraphNode (ReconstructPins below
			// replaces Pins wholesale with a new "(Exposed)"-labeled set)
			//
			// Without the self Modify(), the Pins array is never snapshotted
			// and Ctrl+Z leaves the pin labels in their post-expose state
			// even though TypeAsset rolled back. PostEditUndo on this node
			// also won't fire unless the node participated in the transaction.
			//
			// Without a surrounding FScopedTransaction at all (caller side),
			// both Modify() calls are no-ops - see the lambdas in
			// ComposableCameraNodeGraphSchema::BuildPinContextMenuActions and
			// OnAsPinCheckChanged in ComposableCameraNodeGraphNodeDetails.cpp.
			Modify();
			TypeAsset->Modify();

			// Break any existing wire to this pin.
			if (UEdGraphPin* Pin = FindPin(PinName, EGPD_Input))
			{
				Pin->BreakAllPinLinks();
			}

			// Remove from PinConnections.
			TypeAsset->PinConnections.RemoveAll([this, PinName](const FComposableCameraPinConnection& Conn)
			{
				return Conn.TargetNodeIndex == NodeIndex && Conn.TargetPinName == PinName;
			});

			// Also clear any VariableNodes connections that target this pin.
			// VariableNodes records are normally rebuilt by SyncToTypeAsset,
			// but the wire we just broke won't be reflected until the next
			// sync. Stale connections here would cause BuildRuntimeDataLayout
			// to create InputPinSourceOffsets entries that shadow the new
			// ExposedParameter via TryResolveInputPin's priority ordering.
			for (FComposableCameraVariableNodeRecord& VarRecord: TypeAsset->VariableNodes)
			{
				if (VarRecord.bIsSetter)
				{
					continue;
				}
				VarRecord.Connections.RemoveAll(
					[this, PinName](const FComposableCameraVariablePinConnection& VarConn)
					{
						return VarConn.CameraNodeIndex == NodeIndex
							&& VarConn.CameraPinName == PinName;
					});
			}

			// Resolve the final parameter name. If another exposed parameter
			// (or any variable, since the cross-set name space is shared) is
			// already using PinName, MakeUniqueExposedName returns a suffixed
			// variant - e.g. "Strength" -> "Strength_2". This is the only place
			// new ExposedParameters are created from pin exposes, so the
			// uniqueness invariant only needs to be enforced here at the
			// authoring boundary; legacy assets get healed in PostLoad via
			// DeduplicateExposedNames().
			const FName UniqueParameterName = TypeAsset->MakeUniqueExposedName(PinName);
			const bool bWasRenamed = (UniqueParameterName != PinName);

			// Create the exposed parameter.
			FComposableCameraExposedParameter NewParam;
			NewParam.ParameterName = UniqueParameterName;
			NewParam.DisplayName = FoundDecl->DisplayName;
			NewParam.PinType = FoundDecl->PinType;
			NewParam.StructType = FoundDecl->StructType;
			NewParam.EnumType = FoundDecl->EnumType;
			NewParam.SignatureFunction = FoundDecl->SignatureFunction;
			NewParam.TargetNodeIndex = NodeIndex;
			// TargetNodeIndex / TargetPinName still point at the underlying
			// camera node pin - only ParameterName is suffixed. The pin
			// declaration on the camera node itself is unchanged.
			NewParam.TargetPinName = PinName;
			NewParam.bRequired = FoundDecl->bRequired;
			NewParam.Tooltip = FoundDecl->Tooltip;
			TypeAsset->ExposedParameters.Add(NewParam);

			TypeAsset->MarkPackageDirty();

			// Tell the user when we had to suffix the name. Without the
			// notification the rename would happen silently and the user
			// would have no idea why the new entry in the Details panel
			// reads "Strength_2" instead of "Strength".
			if (bWasRenamed)
			{
				FNotificationInfo Info(FText::Format(LOCTEXT("ExposeParameterRenamed",
						"'{0}' is already in use - exposed as '{1}' instead."),
					FText::FromName(PinName),
					FText::FromName(UniqueParameterName)));
				Info.ExpireDuration = 5.0f;
				Info.bFireAndForget = true;
				FSlateNotificationManager::Get().AddNotification(Info);

				UE_LOG(LogComposableCameraSystemEditor, Log,
					TEXT("[%s] Exposed parameter '%s' renamed to '%s' to avoid collision with an existing entry."),
					*TypeAsset->GetName(), *PinName.ToString(), *UniqueParameterName.ToString());
			}
		}
	}

	// Rebuild pins so the "(Exposed)" label shows, then tell the graph editor to refresh.
	ReconstructPins();
	if (UEdGraph* OwningGraph = GetGraph())
	{
		OwningGraph->NotifyGraphChanged();
	}
}

void UComposableCameraNodeGraphNode::UnexposePinParameter(FName PinName)
{
	if (UEdGraph* Graph = GetGraph())
	{
		if (UComposableCameraTypeAsset* TypeAsset = Cast<UComposableCameraTypeAsset>(Graph->GetOuter()))
		{
			// Mirror the pattern in ExposePinAsParameter: snapshot both this
			// GraphNode (Pins array is about to be rebuilt) and the TypeAsset
			// (ExposedParameters is about to be mutated). See the detailed
			// comment in ExposePinAsParameter for why both sides are required.
			Modify();
			TypeAsset->Modify();

			TypeAsset->ExposedParameters.RemoveAll([this, PinName](const FComposableCameraExposedParameter& Param)
			{
				return Param.TargetNodeIndex == NodeIndex && Param.TargetPinName == PinName;
			});

			TypeAsset->MarkPackageDirty();
		}
	}

	// Rebuild pins so the "(Exposed)" label disappears, then refresh the graph widget.
	ReconstructPins();
	if (UEdGraph* OwningGraph = GetGraph())
	{
		OwningGraph->NotifyGraphChanged();
	}
}

const FComposableCameraPinOverride* UComposableCameraNodeGraphNode::FindPinOverride(FName PinName) const
{
	for (const FComposableCameraPinOverride& Override: RuntimePinOverrides)
	{
		if (Override.PinName == PinName)
		{
			return &Override;
		}
	}
	return nullptr;
}

FComposableCameraPinOverride* UComposableCameraNodeGraphNode::FindPinOverride(FName PinName)
{
	for (FComposableCameraPinOverride& Override: RuntimePinOverrides)
	{
		if (Override.PinName == PinName)
		{
			return &Override;
		}
	}
	return nullptr;
}

FString UComposableCameraNodeGraphNode::GetEffectivePinDefault(const FComposableCameraNodePinDeclaration& Declaration) const
{
	if (const FComposableCameraPinOverride* Override = FindPinOverride(Declaration.PinName))
	{
		if (Override->bHasDefaultOverride)
		{
			return Override->DefaultValueOverride;
		}
	}
	return Declaration.DefaultValueString;
}

bool UComposableCameraNodeGraphNode::GetEffectivePinAsPin(FName PinName) const
{
	if (const FComposableCameraPinOverride* Override = FindPinOverride(PinName))
	{
		return Override->bAsPin;
	}
	// No per-instance override: fall back to the C++ declaration's class-level
	// default. This lets node authors mark a pin as Details-only by default
	// (bDefaultAsPin = false on FComposableCameraNodePinDeclaration) without
	// every fresh placement having to flip the toggle by hand. A missing or
	// unresolvable declaration falls back to the historical default of true.
	if (NodeTemplate)
	{
		TArray<FComposableCameraNodePinDeclaration> Declarations;
		NodeTemplate->GatherAllPinDeclarations(Declarations);
		for (const FComposableCameraNodePinDeclaration& Decl: Declarations)
		{
			if (Decl.PinName == PinName)
			{
				return Decl.bDefaultAsPin;
			}
		}
	}
	return true;
}

void UComposableCameraNodeGraphNode::SetPinDefaultOverride(FName PinName, const FString& NewDefault)
{
	Modify();

	FComposableCameraPinOverride* Override = FindPinOverride(PinName);
	if (!Override)
	{
		// Lazily create a sparse entry. Seed bAsPin from the *current
		// effective* state (which already consults Decl.bDefaultAsPin) so
		// that authoring a default value on a pin that was hidden by class
		// default doesn't accidentally surface it as a wire.
		FComposableCameraPinOverride NewOverride;
		NewOverride.PinName = PinName;
		NewOverride.bAsPin = GetEffectivePinAsPin(PinName);
		NewOverride.bHasDefaultOverride = true;
		NewOverride.DefaultValueOverride = NewDefault;
		RuntimePinOverrides.Add(NewOverride);
	}
	else
	{
		Override->bHasDefaultOverride = true;
		Override->DefaultValueOverride = NewDefault;
	}

	// The actual live UEdGraphPin value needs to be updated too so the graph
	// widget shows the new value without waiting for a full ReconstructPins.
	if (UEdGraphPin* Pin = FindPin(PinName, EGPD_Input))
	{
		Pin->DefaultValue = NewDefault;
	}

	if (UEdGraph* OwningGraph = GetGraph())
	{
		if (UComposableCameraTypeAsset* TypeAsset = Cast<UComposableCameraTypeAsset>(OwningGraph->GetOuter()))
		{
			TypeAsset->MarkPackageDirty();
		}
	}
}

void UComposableCameraNodeGraphNode::SetPinAsPin(FName PinName, bool bNewAsPin)
{
	// Short-circuit no-op toggles so we don't spuriously dirty the package
	// on every Details panel repaint.
	if (GetEffectivePinAsPin(PinName) == bNewAsPin)
	{
		return;
	}

	Modify();

	FComposableCameraPinOverride* Override = FindPinOverride(PinName);
	if (!Override)
	{
		FComposableCameraPinOverride NewOverride;
		NewOverride.PinName = PinName;
		NewOverride.bAsPin = bNewAsPin;
		RuntimePinOverrides.Add(NewOverride);
	}
	else
	{
		Override->bAsPin = bNewAsPin;
	}

	// When flipping bAsPin OFF on a currently-live pin, two side effects need
	// to happen atomically with the toggle:
	//
	// 1. If the pin is exposed as a camera parameter, auto-unexpose it - 
	// there's no way to edit the exposure from the Details panel once the
	// pin has vanished from the graph, so leaving the exposure in place
	// would strand it.
	//
	// 2. If the pin carries any wires, break them. We run this under the
	// surrounding Transaction (ExposePinAsParameter / UnexposePinParameter
	// and the property-change handler wrap Modify calls around us), so
	// Ctrl+Z restores the wire cleanly. We don't warn the user here - the
	// toggle itself is the signal that the user accepted the consequence.
	if (!bNewAsPin)
	{
		if (IsInputPinExposed(PinName))
		{
			UnexposePinParameter(PinName);
		}

		if (UEdGraphPin* Pin = FindPin(PinName, EGPD_Input))
		{
			if (Pin->LinkedTo.Num() > 0)
			{
				Pin->BreakAllPinLinks();
			}
		}
	}

	if (UEdGraph* OwningGraph = GetGraph())
	{
		if (UComposableCameraTypeAsset* TypeAsset = Cast<UComposableCameraTypeAsset>(OwningGraph->GetOuter()))
		{
			TypeAsset->MarkPackageDirty();
		}
	}
}

UEdGraphPin* UComposableCameraNodeGraphNode::CreatePinFromDeclaration(const FComposableCameraNodePinDeclaration& Declaration)
{
	const EEdGraphPinDirection Direction = (Declaration.Direction == EComposableCameraPinDirection::Input)
		? EGPD_Input: EGPD_Output;

	// The enum-to-FEdGraphPinType conversion is shared with
	// UK2Node_ActivateComposableCamera so adding a new pin type only requires
	// editing one switch. See ComposableCameraEdGraphPinTypeUtils.h for the
	// rationale and the location of the single source of truth.
	FEdGraphPinType PinType = ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType(Declaration.PinType, Declaration.StructType, Declaration.EnumType, Declaration.SignatureFunction);

	UEdGraphPin* NewPin = CreatePin(Direction, PinType, Declaration.PinName);
	if (NewPin)
	{
		// Required input pins get a trailing `*` on their display label and an
		// explicit "(Required)" line appended to the tooltip so authors know
		// before they see a validation error that the pin has to be
		// satisfied somehow (wire, Expose-as-Parameter, or a default). Output
		// and optional input pins render as-is. Exposed-pin decoration is
		// applied on top of this in AllocateDefaultPins so the `*` survives
		// the `(Exposed)` suffix rewrite.
		const bool bIsRequiredInput =
			(Direction == EGPD_Input) && Declaration.bRequired;
		NewPin->PinFriendlyName = bIsRequiredInput
			? FText::Format(LOCTEXT("RequiredPinLabel", "{0}*"), Declaration.DisplayName)
			: Declaration.DisplayName;

		FString TooltipText = Declaration.Tooltip.ToString();
		if (bIsRequiredInput)
		{
			if (!TooltipText.IsEmpty())
			{
				TooltipText += TEXT("\n");
			}
			TooltipText += LOCTEXT("RequiredPinTooltipSuffix", "(Required)").ToString();
		}
		NewPin->PinToolTip = TooltipText;

		if (!Declaration.DefaultValueString.IsEmpty() && Direction == EGPD_Input
			&& Declaration.PinType != EComposableCameraPinType::Delegate)
		{
			NewPin->DefaultValue = Declaration.DefaultValueString;
		}
	}

	return NewPin;
}

#undef LOCTEXT_NAMESPACE
