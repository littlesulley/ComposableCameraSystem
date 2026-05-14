// Copyright Sulley. All rights reserved.

#include "Editors/ComposableCameraVariableGraphNode.h"
#include "ComposableCameraEdGraphPinTypeUtils.h"
#include "ComposableCameraEditorStyle.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Editors/ComposableCameraNodeGraph.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "ToolMenus.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ComposableCameraVariableGraphNode"

const FName UComposableCameraVariableGraphNode::PN_Value(TEXT("Value"));
const FName UComposableCameraVariableGraphNode::PN_ExecIn(TEXT("ExecIn"));
const FName UComposableCameraVariableGraphNode::PN_ExecOut(TEXT("ExecOut"));

void UComposableCameraVariableGraphNode::AllocateDefaultPins()
{
	// Resolve the variable's type. If the variable has been renamed/removed, fall
	// back to a wildcard pin so the node is still visible and can be repaired.
	//
	// The enum -> FEdGraphPinType switch lives in
	// ComposableCameraEdGraphPinTypeUtils (UncookedOnly module) as the single
	// source of truth shared with UComposableCameraNodeGraphNode and the K2
	// activation node. Any new EComposableCameraPinType case added there is
	// automatically picked up here.
	FEdGraphPinType PinType;
	if (const FComposableCameraInternalVariable* Variable = FindVariable())
	{
		PinType = ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType(Variable->VariableType, Variable->StructType, Variable->EnumType);
	}
	else if (bHasValidCachedType)
	{
		// Variable is missing but we have cached metadata from a previous
		// FindVariable() success (e.g. cross-graph paste). Use the cached
		// type so the pin retains the correct type and existing wires stay
		// compatible, rather than falling through to wildcard.
		PinType = ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType(CachedVariableType, CachedStructType, CachedEnumType);
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

	const EEdGraphPinDirection Direction = bIsSetter ? EGPD_Input: EGPD_Output;
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
		// VariableName IS the display label - no separate DisplayName field.
		const FText Display = FText::FromName(Variable->VariableName);
		return FText::Format(LOCTEXT("VariableNodeTitleFmt", "{0} {1}"), Prefix, Display);
	}

	return FText::Format(LOCTEXT("VariableNodeTitleMissingFmt", "{0} {1} (missing)"),
		Prefix, FText::FromName(VariableName));
}

FLinearColor UComposableCameraVariableGraphNode::GetNodeTitleColor() const
{
	// Purple - distinct from camera nodes (teal) and Start/Output sentinels.
	// Palette lives in FComposableCameraEditorColors (ComposableCameraEditorStyle.h).
	return FComposableCameraEditorColors::VariableNodeTitle;
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

	// Variables can live in either InternalVariables or ExposedVariables - both
	// arrays share the same struct type and the same GUID/Name identity rules,
	// so this lookup iterates both uniformly. The GUID keyspace is enforced
	// disjoint by UComposableCameraTypeAsset::EnsureExposedVariableGuids /
	// EnsureInternalVariableGuids (each GUID is minted independently), and
	// name uniqueness across both arrays is enforced at Build() time.

	// Mutable self for cache updates - all mutations are editor-only display caches.
	UComposableCameraVariableGraphNode* MutableSelf =
		const_cast<UComposableCameraVariableGraphNode*>(this);

	auto SearchByGuid = [MutableSelf](const TArray<FComposableCameraInternalVariable>& Array)
		-> const FComposableCameraInternalVariable*
	{
		for (const FComposableCameraInternalVariable& Variable: Array)
		{
			if (Variable.VariableGuid == MutableSelf->VariableGuid)
			{
				MutableSelf->VariableName = Variable.VariableName;
				return &Variable;
			}
		}
		return nullptr;
	};

	auto SearchByName = [MutableSelf](const TArray<FComposableCameraInternalVariable>& Array)
		-> const FComposableCameraInternalVariable*
	{
		for (const FComposableCameraInternalVariable& Variable: Array)
		{
			if (Variable.VariableName == MutableSelf->VariableName)
			{
				if (Variable.VariableGuid.IsValid())
				{
					MutableSelf->VariableGuid = Variable.VariableGuid;
				}
				return &Variable;
			}
		}
		return nullptr;
	};

	// Helper: once a match is found, cache the variable's metadata so it
	// survives copy/paste into a graph where the variable doesn't exist.
	auto CacheVariableMetadata = [MutableSelf](const FComposableCameraInternalVariable* Found,
		bool bIsExposed)
	{
		MutableSelf->CachedVariableType = Found->VariableType;
		MutableSelf->CachedStructType = Found->StructType;
		MutableSelf->CachedEnumType = Found->EnumType;
		MutableSelf->bCachedIsExposed = bIsExposed;
		MutableSelf->bHasValidCachedType = true;
	};

	// Primary lookup: match by VariableGuid. This is the identity that survives
	// renames in the Details panel, which is the whole reason the GUID exists.
	if (VariableGuid.IsValid())
	{
		if (const FComposableCameraInternalVariable* Found = SearchByGuid(TypeAsset->InternalVariables))
		{
			CacheVariableMetadata(Found, /*bIsExposed=*/false);
			return Found;
		}
		if (const FComposableCameraInternalVariable* Found = SearchByGuid(TypeAsset->ExposedVariables))
		{
			CacheVariableMetadata(Found, /*bIsExposed=*/true);
			return Found;
		}

		// GUID was valid but not found on this asset - this is a pasted node
		// whose source variable doesn't exist here. Do NOT fall through to
		// the name-based search: rebinding by name would silently attach to
		// a same-name, different-type variable. PostPasteNode will handle
		// the missing case via TryAutoAssociateWithExistingVariable (which
		// checks both name AND type) or by offering the "Create Variable"
		// context menu.
		return nullptr;
	}

	// Legacy fallback: data saved before the GUID migration still references
	// variables by name only (VariableGuid is invalid). If we find a match
	// here, write the GUID back onto the node so subsequent lookups take the
	// fast GUID path above.
	if (!VariableName.IsNone())
	{
		if (const FComposableCameraInternalVariable* Found = SearchByName(TypeAsset->InternalVariables))
		{
			CacheVariableMetadata(Found, /*bIsExposed=*/false);
			return Found;
		}
		if (const FComposableCameraInternalVariable* Found = SearchByName(TypeAsset->ExposedVariables))
		{
			CacheVariableMetadata(Found, /*bIsExposed=*/true);
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

	if (FindVariable())
	{
		// Variable exists on the target asset (same GUID, or legacy name match).
		// Reconstruct pins to ensure the pin type reflects the target variable's
		// current type, not the cached type from the source graph.
		ReconstructPins();
		return;
	}

	// Name + type match - silently rebind.
	if (TryAutoAssociateWithExistingVariable())
	{
		return;
	}

	// Neither GUID nor name+type matched. If there is a same-name variable with
	// a different type, show the conflict resolution dialog so the user can
	// choose how to handle it.
	HandleVariableTypeConflictIfAny();
}

void UComposableCameraVariableGraphNode::ReconstructPins()
{
	Modify();

	// Preserve user wires through the rebuild. See the matching comment in
	// UComposableCameraNodeGraphNode::ReconstructPins - the pattern is the
	// same: snapshot, rebuild, then re-wire by (PinName, Direction).
	TArray<UEdGraphPin*> OldPins = MoveTemp(Pins);
	Pins.Reset();

	AllocateDefaultPins();

	for (UEdGraphPin* NewPin: Pins)
	{
		for (UEdGraphPin*& OldPin: OldPins)
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

	for (UEdGraphPin* OldPin: OldPins)
	{
		if (OldPin)
		{
			OldPin->BreakAllPinLinks();
			OldPin->MarkAsGarbage();
		}
	}
}

// Auto-Associate & Create Variable 

bool UComposableCameraVariableGraphNode::TryAutoAssociateWithExistingVariable()
{
	UComposableCameraTypeAsset* TypeAsset = GetOwningTypeAsset();
	if (!TypeAsset)
	{
		return false;
	}

	// Search both arrays for a variable whose name AND type match the cached
	// metadata from the source graph. If found, rebind the node's GUID to
	// point at the existing variable and reconstruct pins.
	auto TryMatch = [this](TArray<FComposableCameraInternalVariable>& Array, bool bIsExposed) -> bool
	{
		for (const FComposableCameraInternalVariable& Variable: Array)
		{
			if (Variable.VariableName == VariableName
				&& Variable.VariableType == CachedVariableType
				&& Variable.StructType == CachedStructType
				&& Variable.EnumType == CachedEnumType)
			{
				Modify();
				VariableGuid = Variable.VariableGuid;
				bCachedIsExposed = bIsExposed;
				ReconstructPins();
				return true;
			}
		}
		return false;
	};

	if (TryMatch(TypeAsset->InternalVariables, /*bIsExposed=*/false))
	{
		return true;
	}
	if (TryMatch(TypeAsset->ExposedVariables, /*bIsExposed=*/true))
	{
		return true;
	}

	return false;
}

void UComposableCameraVariableGraphNode::CreateVariableFromCachedInfo()
{
	UComposableCameraTypeAsset* TypeAsset = GetOwningTypeAsset();
	if (!TypeAsset)
	{
		return;
	}

	// Build the new variable from cached metadata.
	FComposableCameraInternalVariable NewVariable;
	NewVariable.VariableGuid = FGuid::NewGuid();
	NewVariable.VariableName = VariableName;
	NewVariable.VariableType = CachedVariableType;
	NewVariable.StructType = CachedStructType;
	NewVariable.EnumType = CachedEnumType;

	// Place in the correct array matching the source variable's kind.
	TypeAsset->Modify();
	if (bCachedIsExposed)
	{
		TypeAsset->ExposedVariables.Add(NewVariable);
	}
	else
	{
		TypeAsset->InternalVariables.Add(NewVariable);
	}

	// Rebind this node to the freshly created variable.
	Modify();
	VariableGuid = NewVariable.VariableGuid;
	ReconstructPins();

	// Sync the graph so the new variable is persisted and other editors see it.
	if (UComposableCameraNodeGraph* NodeGraph = Cast<UComposableCameraNodeGraph>(GetGraph()))
	{
		NodeGraph->SyncToTypeAsset();
		NodeGraph->NotifyGraphChanged();
	}
}

// Type Conflict Resolution 

namespace
{
	/** User-facing display name for a variable's type (e.g. "Float", "Struct (FMyPose)"). */
	FText VariableTypeDisplayName(EComposableCameraPinType Type,
		const UScriptStruct* StructType,
		const UEnum* EnumType)
	{
		switch (Type)
		{
		case EComposableCameraPinType::Struct:
			return StructType
				? FText::Format(LOCTEXT("StructTypeFmt", "Struct ({0})"),
					StructType->GetDisplayNameText())
				: LOCTEXT("UnknownStruct", "Struct");
		case EComposableCameraPinType::Enum:
			return EnumType
				? FText::Format(LOCTEXT("EnumTypeFmt", "Enum ({0})"),
					FText::FromString(EnumType->GetName()))
				: LOCTEXT("UnknownEnum", "Enum");
		default:
			if (const UEnum* PinTypeEnum = StaticEnum<EComposableCameraPinType>())
			{
				return PinTypeEnum->GetDisplayNameTextByValue(static_cast<int64>(Type));
			}
			return LOCTEXT("UnknownType", "Unknown");
		}
	}

	/**
	 * Generate a variable name that does not collide with any existing variable
	 * on the type asset. Appends _1, _2, until a unique name is found.
	 */
	FName GenerateUniqueVariableName(FName BaseName,
		const UComposableCameraTypeAsset* TypeAsset)
	{
		const FString BaseStr = BaseName.ToString();
		int32 Suffix = 1;

		auto NameExists = [TypeAsset](FName Candidate) -> bool
		{
			for (const FComposableCameraInternalVariable& Var: TypeAsset->InternalVariables)
			{
				if (Var.VariableName == Candidate) return true;
			}
			for (const FComposableCameraInternalVariable& Var: TypeAsset->ExposedVariables)
			{
				if (Var.VariableName == Candidate) return true;
			}
			return false;
		};

		FName Candidate;
		do
		{
			Candidate = FName(*FString::Printf(TEXT("%s_%d"), *BaseStr, Suffix++));
		} while (NameExists(Candidate));

		return Candidate;
	}
} // anonymous namespace

void UComposableCameraVariableGraphNode::HandleVariableTypeConflictIfAny()
{
	UComposableCameraTypeAsset* TypeAsset = GetOwningTypeAsset();
	if (!TypeAsset || VariableName.IsNone())
	{
		return;
	}

	// Find the variable with the same name. TryAutoAssociateWithExistingVariable
	// already proved name+type doesn't match, so any name match here is a type conflict.
	FComposableCameraInternalVariable* ConflictVar = nullptr;
	bool bConflictIsExposed = false;

	auto SearchByName = [this](TArray<FComposableCameraInternalVariable>& Array)
		->FComposableCameraInternalVariable*
	{
		for (FComposableCameraInternalVariable& Var: Array)
		{
			if (Var.VariableName == VariableName)
			{
				return &Var;
			}
		}
		return nullptr;
	};

	ConflictVar = SearchByName(TypeAsset->InternalVariables);
	if (ConflictVar)
	{
		bConflictIsExposed = false;
	}
	else
	{
		ConflictVar = SearchByName(TypeAsset->ExposedVariables);
		if (ConflictVar)
		{
			bConflictIsExposed = true;
		}
	}

	if (!ConflictVar)
	{
		// No name conflict - the variable is simply absent. The right-click
		// "Create Variable" context menu handles that case.
		return;
	}

	// Snapshot conflict info before any mutation.
	const FGuid ConflictGuid = ConflictVar->VariableGuid;
	const EComposableCameraPinType ConflictType = ConflictVar->VariableType;
	const TObjectPtr<UScriptStruct> ConflictStructType = ConflictVar->StructType;
	const TObjectPtr<UEnum> ConflictEnumType = ConflictVar->EnumType;

	const FText ExistingTypeName = VariableTypeDisplayName(ConflictType, ConflictStructType, ConflictEnumType);
	const FText PastedTypeName = VariableTypeDisplayName(CachedVariableType, CachedStructType, CachedEnumType);

	// Pre-compute the unique suffix name so the button label is accurate.
	const FName UniqueName = GenerateUniqueVariableName(VariableName, TypeAsset);

	// Build and show the conflict dialog 
	const FText ContentText = FText::Format(LOCTEXT("TypeConflictContent",
			"Variable '{0}' already exists on this camera type with type '{1}'.\n"
			"The pasted node expects type '{2}'.\n\n"
			"How would you like to resolve this?"),
		FText::FromName(VariableName),
		ExistingTypeName,
		PastedTypeName);

	int32 Choice = -1;
	TSharedPtr<SWindow> DialogWindow;

	DialogWindow = SNew(SWindow)
		.Title(LOCTEXT("TypeConflictTitle", "Variable Type Conflict"))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.IsTopmostWindow(true)
		[SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.Padding(FComposableCameraEditorPaddings::DialogLR,
				FComposableCameraEditorPaddings::DialogTB,
				FComposableCameraEditorPaddings::DialogLR,
				FComposableCameraEditorPaddings::DialogBetweenRowsTB)
			.AutoHeight()
			[SNew(STextBlock)
				.Text(ContentText)
				.AutoWrapText(true)
				.WrapTextAt(480.f)]

			+ SVerticalBox::Slot()
			.Padding(FComposableCameraEditorPaddings::DialogLR,
				0.f,
				FComposableCameraEditorPaddings::DialogLR,
				FComposableCameraEditorPaddings::DialogTB)
			.AutoHeight()
			.HAlign(HAlign_Right)
			[SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.f, 0.f, FComposableCameraEditorPaddings::InnerGap, 0.f)
				[SNew(SButton)
					.Text(FText::Format(LOCTEXT("AdoptExistingFmt", "Use Existing ({0})"), ExistingTypeName))
					.OnClicked_Lambda([&Choice, &DialogWindow]()
					{
						Choice = 0;
						if (DialogWindow.IsValid()) { DialogWindow->RequestDestroyWindow(); }
						return FReply::Handled();
					})]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FComposableCameraEditorPaddings::InnerGap, 0.f)
				[SNew(SButton)
					.Text(FText::Format(LOCTEXT("ChangeExistingFmt", "Change Existing to {0}"), PastedTypeName))
					.OnClicked_Lambda([&Choice, &DialogWindow]()
					{
						Choice = 1;
						if (DialogWindow.IsValid()) { DialogWindow->RequestDestroyWindow(); }
						return FReply::Handled();
					})]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FComposableCameraEditorPaddings::InnerGap, 0.f, 0.f, 0.f)
				[SNew(SButton)
					.Text(FText::Format(LOCTEXT("RenameFmt", "Create as '{0}'"), FText::FromName(UniqueName)))
					.OnClicked_Lambda([&Choice, &DialogWindow]()
					{
						Choice = 2;
						if (DialogWindow.IsValid()) { DialogWindow->RequestDestroyWindow(); }
						return FReply::Handled();
					})]]];

	FSlateApplication::Get().AddModalWindow(DialogWindow.ToSharedRef(), nullptr);

	if (Choice < 0 || Choice > 2)
	{
		// User closed the dialog without choosing - leave the node as "missing".
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ResolveTypeConflictTransaction", "Resolve Variable Type Conflict"));

	switch (Choice)
	{
	case 0: // Use existing type 
	{
		// Rebind this node to the existing variable. The pin type changes to
		// match the existing variable; incompatible wires break automatically
		// during ReconstructPins (it matches on PinType when re-wiring).
		Modify();
		VariableGuid = ConflictGuid;
		CachedVariableType = ConflictType;
		CachedStructType = ConflictStructType;
		CachedEnumType = ConflictEnumType;
		bCachedIsExposed = bConflictIsExposed;
		bHasValidCachedType = true;
		ReconstructPins();
		break;
	}
	case 1: // Change existing variable's type 
	{
		// Re-find the conflict variable by GUID (pointer may be stale after
		// Modify() calls, though in practice TArray doesn't reallocate on
		// in-place edits - but being defensive costs nothing).
		FComposableCameraInternalVariable* VarToChange = nullptr;
		auto FindByGuid = [&ConflictGuid](TArray<FComposableCameraInternalVariable>& Array)
			->FComposableCameraInternalVariable*
		{
			for (FComposableCameraInternalVariable& Var: Array)
			{
				if (Var.VariableGuid == ConflictGuid) return &Var;
			}
			return nullptr;
		};
		VarToChange = FindByGuid(TypeAsset->InternalVariables);
		if (!VarToChange) VarToChange = FindByGuid(TypeAsset->ExposedVariables);
		if (!VarToChange) break; // Should never happen.

		// Change the variable's type to match the pasted node's cached type.
		TypeAsset->Modify();
		VarToChange->VariableType = CachedVariableType;
		VarToChange->StructType = CachedStructType;
		VarToChange->EnumType = CachedEnumType;

		// Rebind this node to the (now type-matched) variable.
		Modify();
		VariableGuid = ConflictGuid;
		bCachedIsExposed = bConflictIsExposed;
		ReconstructPins();

		// Reconstruct all other variable nodes that reference the same
		// variable - their pin types need to update to the new type, and
		// any type-incompatible wires on those nodes will break cleanly.
		if (const UEdGraph* Graph = GetGraph())
		{
			for (UEdGraphNode* Node: Graph->Nodes)
			{
				UComposableCameraVariableGraphNode* VarNode =
					Cast<UComposableCameraVariableGraphNode>(Node);
				if (VarNode && VarNode != this && VarNode->VariableGuid == ConflictGuid)
				{
					VarNode->ReconstructPins();
				}
			}
		}

		// Sync so the type change is persisted and the Details panel refreshes.
		if (UComposableCameraNodeGraph* NodeGraph = Cast<UComposableCameraNodeGraph>(GetGraph()))
		{
			NodeGraph->SyncToTypeAsset();
			NodeGraph->NotifyGraphChanged();
		}
		break;
	}
	case 2: // Rename with suffix 
	{
		Modify();
		VariableName = UniqueName;
		CreateVariableFromCachedInfo();
		break;
	}
	default:
		break;
	}
}

void UComposableCameraVariableGraphNode::RenameWithUniqueSuffix()
{
	const UComposableCameraTypeAsset* TypeAsset = GetOwningTypeAsset();
	if (!TypeAsset || VariableName.IsNone())
	{
		return;
	}

	Modify();
	VariableName = GenerateUniqueVariableName(VariableName, TypeAsset);
	CreateVariableFromCachedInfo();
}

// Context Menu 

void UComposableCameraVariableGraphNode::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	if (!Menu)
	{
		return;
	}

	// Only show "Create Variable" when the variable is missing.
	if (FindVariable())
	{
		return;
	}

	FToolMenuSection& Section = Menu->AddSection("ComposableCameraVariableActions",
		LOCTEXT("VariableActionsSectionLabel", "Variable"));

	UComposableCameraVariableGraphNode* MutableSelf =
		const_cast<UComposableCameraVariableGraphNode*>(this);

	Section.AddMenuEntry(
		"CreateVariable",
		FText::Format(LOCTEXT("CreateVariableFmt", "Create Variable '{0}'"),
			FText::FromName(VariableName)),
		FText::Format(LOCTEXT("CreateVariableTooltipFmt",
			"Create a new {0} variable '{1}' on this camera type and associate this node with it."),
			bCachedIsExposed
				? LOCTEXT("ExposedLabel", "exposed")
				: LOCTEXT("InternalLabel", "internal"),
			FText::FromName(VariableName)),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([MutableSelf]()
			{
				const FScopedTransaction Transaction(LOCTEXT("CreateVariableTransaction", "Create Variable from Pasted Node"));
				MutableSelf->CreateVariableFromCachedInfo();
			})
		)
	);
}

#undef LOCTEXT_NAMESPACE
