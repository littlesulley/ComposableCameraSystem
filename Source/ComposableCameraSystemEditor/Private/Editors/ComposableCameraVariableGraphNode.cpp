// Copyright Sulley. All rights reserved.

#include "Editors/ComposableCameraVariableGraphNode.h"
#include "ComposableCameraEdGraphPinTypeUtils.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"

#define LOCTEXT_NAMESPACE "ComposableCameraVariableGraphNode"

const FName UComposableCameraVariableGraphNode::PN_Value(TEXT("Value"));
const FName UComposableCameraVariableGraphNode::PN_ExecIn(TEXT("ExecIn"));
const FName UComposableCameraVariableGraphNode::PN_ExecOut(TEXT("ExecOut"));

void UComposableCameraVariableGraphNode::AllocateDefaultPins()
{
	// Resolve the variable's type. If the variable has been renamed/removed, fall
	// back to a wildcard pin so the node is still visible and can be repaired.
	//
	// The enum→FEdGraphPinType switch lives in
	// ComposableCameraEdGraphPinTypeUtils (UncookedOnly module) as the single
	// source of truth shared with UComposableCameraNodeGraphNode and the K2
	// activation node. Any new EComposableCameraPinType case added there is
	// automatically picked up here.
	FEdGraphPinType PinType;
	if (const FComposableCameraInternalVariable* Variable = FindVariable())
	{
		PinType = ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType(
			Variable->VariableType, Variable->StructType, Variable->EnumType);
	}
	else
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
	}

	// Set nodes get explicit exec pins so they participate in the execution chain
	// alongside camera nodes. Get nodes are pure data conduits and have no exec pins.
	if (bIsSetter)
	{
		FEdGraphPinType ExecPinType;
		ExecPinType.PinCategory = UEdGraphSchema_K2::PC_Exec;

		if (UEdGraphPin* ExecIn = CreatePin(EGPD_Input, ExecPinType, PN_ExecIn))
		{
			ExecIn->PinFriendlyName = LOCTEXT("ExecInLabel", "");
		}
		if (UEdGraphPin* ExecOut = CreatePin(EGPD_Output, ExecPinType, PN_ExecOut))
		{
			ExecOut->PinFriendlyName = LOCTEXT("ExecOutLabel", "");
		}
	}

	const EEdGraphPinDirection Direction = bIsSetter ? EGPD_Input : EGPD_Output;
	UEdGraphPin* ValuePin = CreatePin(Direction, PinType, PN_Value);
	if (ValuePin)
	{
		ValuePin->PinFriendlyName = LOCTEXT("ValuePinLabel", "Value");
	}
}

FText UComposableCameraVariableGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const FText Prefix = bIsSetter
		? LOCTEXT("SetVariablePrefix", "Set")
		: LOCTEXT("GetVariablePrefix", "Get");

	if (const FComposableCameraInternalVariable* Variable = FindVariable())
	{
		// VariableName IS the display label — no separate DisplayName field.
		const FText Display = FText::FromName(Variable->VariableName);
		return FText::Format(LOCTEXT("VariableNodeTitleFmt", "{0} {1}"), Prefix, Display);
	}

	return FText::Format(LOCTEXT("VariableNodeTitleMissingFmt", "{0} {1} (missing)"),
		Prefix, FText::FromName(VariableName));
}

FLinearColor UComposableCameraVariableGraphNode::GetNodeTitleColor() const
{
	// Purple — distinct from camera nodes (teal) and Start/Output sentinels.
	return FLinearColor(0.45f, 0.25f, 0.6f);
}

FText UComposableCameraVariableGraphNode::GetTooltipText() const
{
	if (const FComposableCameraInternalVariable* Variable = FindVariable())
	{
		if (!Variable->Tooltip.IsEmpty())
		{
			return Variable->Tooltip;
		}
		return bIsSetter
			? FText::Format(LOCTEXT("SetVarTooltipFmt", "Writes to internal variable '{0}'."),
				FText::FromName(Variable->VariableName))
			: FText::Format(LOCTEXT("GetVarTooltipFmt", "Reads the current value of internal variable '{0}'."),
				FText::FromName(Variable->VariableName));
	}

	return FText::Format(LOCTEXT("VarTooltipMissingFmt",
		"Variable '{0}' no longer exists on this camera type."),
		FText::FromName(VariableName));
}

const FComposableCameraInternalVariable* UComposableCameraVariableGraphNode::FindVariable() const
{
	const UComposableCameraTypeAsset* TypeAsset = GetOwningTypeAsset();
	if (!TypeAsset)
	{
		return nullptr;
	}

	// Variables can live in either InternalVariables or ExposedVariables — both
	// arrays share the same struct type and the same GUID/Name identity rules,
	// so this lookup iterates both uniformly. The GUID keyspace is enforced
	// disjoint by UComposableCameraTypeAsset::EnsureExposedVariableGuids /
	// EnsureInternalVariableGuids (each GUID is minted independently), and
	// name uniqueness across both arrays is enforced at Build() time.
	auto SearchByGuid = [this](const TArray<FComposableCameraInternalVariable>& Array)
		-> const FComposableCameraInternalVariable*
	{
		for (const FComposableCameraInternalVariable& Variable : Array)
		{
			if (Variable.VariableGuid == VariableGuid)
			{
				// Opportunistically refresh the cached name so the "(missing)"
				// label stays accurate if the variable is later deleted. This
				// is const-ok because VariableName is a display cache, not
				// identity. const_cast is acceptable here because we own the
				// node and mutation is editor-only.
				const_cast<UComposableCameraVariableGraphNode*>(this)->VariableName
					= Variable.VariableName;
				return &Variable;
			}
		}
		return nullptr;
	};

	auto SearchByName = [this](const TArray<FComposableCameraInternalVariable>& Array)
		-> const FComposableCameraInternalVariable*
	{
		for (const FComposableCameraInternalVariable& Variable : Array)
		{
			if (Variable.VariableName == VariableName)
			{
				if (Variable.VariableGuid.IsValid())
				{
					const_cast<UComposableCameraVariableGraphNode*>(this)->VariableGuid
						= Variable.VariableGuid;
				}
				return &Variable;
			}
		}
		return nullptr;
	};

	// Primary lookup: match by VariableGuid. This is the identity that survives
	// renames in the Details panel, which is the whole reason the GUID exists.
	if (VariableGuid.IsValid())
	{
		if (const FComposableCameraInternalVariable* Found = SearchByGuid(TypeAsset->InternalVariables))
		{
			return Found;
		}
		if (const FComposableCameraInternalVariable* Found = SearchByGuid(TypeAsset->ExposedVariables))
		{
			return Found;
		}
	}

	// Legacy fallback: data saved before the GUID migration still references
	// variables by name. If we find a match here, write the GUID back onto the
	// node so subsequent lookups take the fast path above.
	if (!VariableName.IsNone())
	{
		if (const FComposableCameraInternalVariable* Found = SearchByName(TypeAsset->InternalVariables))
		{
			return Found;
		}
		if (const FComposableCameraInternalVariable* Found = SearchByName(TypeAsset->ExposedVariables))
		{
			return Found;
		}
	}

	return nullptr;
}

UComposableCameraTypeAsset* UComposableCameraVariableGraphNode::GetOwningTypeAsset() const
{
	if (const UEdGraph* Graph = GetGraph())
	{
		return Cast<UComposableCameraTypeAsset>(Graph->GetOuter());
	}
	return nullptr;
}

void UComposableCameraVariableGraphNode::PostPasteNode()
{
	Super::PostPasteNode();

	// VariableGuid, VariableName, and bIsSetter are non-Transient and
	// survived the clipboard round-trip. Pins were also serialized during
	// copy and deserialized on import. Nothing special to reconstruct —
	// the pasted node points at the same variable (by GUID) and carries
	// the right pin layout. SyncToTypeAsset (triggered by OnGraphChanged
	// after the paste operation) will capture this node as a new entry in
	// the VariableNodes array.
}

void UComposableCameraVariableGraphNode::ReconstructPins()
{
	Modify();

	// Preserve user wires through the rebuild. See the matching comment in
	// UComposableCameraNodeGraphNode::ReconstructPins — the pattern is the
	// same: snapshot, rebuild, then re-wire by (PinName, Direction).
	TArray<UEdGraphPin*> OldPins = MoveTemp(Pins);
	Pins.Reset();

	AllocateDefaultPins();

	for (UEdGraphPin* NewPin : Pins)
	{
		for (UEdGraphPin*& OldPin : OldPins)
		{
			// Match on (PinName, Direction, PinType). If a variable's declared
			// type changes, the new Value pin has a different PinType, so we
			// drop the old wire rather than carry a type-incompatible link.
			if (OldPin
				&& OldPin->PinName == NewPin->PinName
				&& OldPin->Direction == NewPin->Direction
				&& OldPin->PinType == NewPin->PinType)
			{
				NewPin->MovePersistentDataFromOldPin(*OldPin);
				OldPin = nullptr;
				break;
			}
		}
	}

	for (UEdGraphPin* OldPin : OldPins)
	{
		if (OldPin)
		{
			OldPin->BreakAllPinLinks();
			OldPin->MarkAsGarbage();
		}
	}
}

#undef LOCTEXT_NAMESPACE
