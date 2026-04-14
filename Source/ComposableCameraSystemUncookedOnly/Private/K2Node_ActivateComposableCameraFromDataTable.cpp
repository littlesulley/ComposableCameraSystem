// Copyright Sulley. All rights reserved.

#include "K2Node_ActivateComposableCameraFromDataTable.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/DataTable.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "Textures/SlateIcon.h"

#include "DataAssets/ComposableCameraParameterTableRow.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

#define LOCTEXT_NAMESPACE "K2Node_ActivateComposableCameraFromDataTable"

const FName UK2Node_ActivateComposableCameraFromDataTable::DataTablePinName(TEXT("DataTable"));
const FName UK2Node_ActivateComposableCameraFromDataTable::RowNamePinName(TEXT("RowName"));

UK2Node_ActivateComposableCameraFromDataTable::UK2Node_ActivateComposableCameraFromDataTable(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Setting the function reference on the CDO is what makes the inherited
	// UK2Node_CallFunction machinery populate this node with the correct pins,
	// tooltip, and title without us having to override AllocateDefaultPins.
	FunctionReference.SetExternalMember(
		GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, ActivateComposableCameraFromDataTable),
		UComposableCameraBlueprintLibrary::StaticClass());
}

void UK2Node_ActivateComposableCameraFromDataTable::GetMenuActions(
	FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	// Because the wrapped BP library function is BlueprintInternalUseOnly,
	// the default UK2Node_CallFunction spawner skips it. Register this
	// subclass as the sole palette entry for the node.
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
		check(Spawner);
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

FText UK2Node_ActivateComposableCameraFromDataTable::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "ComposableCameraSystem|Camera");
}

FLinearColor UK2Node_ActivateComposableCameraFromDataTable::GetNodeTitleColor() const
{
	// Teal to match the Camera Type Asset color — this node activates a
	// camera from a DataTable row, so it belongs to the same visual family
	// as the plain "Activate Composable Camera" node.
	return FLinearColor::FromSRGBColor(FColor(20, 150, 140));
}

FSlateIcon UK2Node_ActivateComposableCameraFromDataTable::GetIconAndTint(FLinearColor& OutColor) const
{
	// Reuse the CameraComponent class icon the sibling K2 node uses, so
	// both camera-activation nodes share the same visual identity in the
	// graph and palette.
	OutColor = FLinearColor(.823f, .823f, .823f);
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.CameraComponent");
}

void UK2Node_ActivateComposableCameraFromDataTable::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);

	if (Pin && Pin->PinName == DataTablePinName)
	{
		// Revalidate the row name against the new DataTable's row set
		// BEFORE notifying the widget, so the widget observes the already-
		// cleared default value during its refresh.
		ClearRowNameIfInvalidForCurrentDataTable();

		// The DataTable literal changed — notify the row-name pin widget so
		// it can re-resolve and refresh its dropdown without waiting for a
		// reconstruct or save/reopen cycle.
		OnDataTablePinChangedDelegate.Broadcast();
	}
}

void UK2Node_ActivateComposableCameraFromDataTable::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	if (Pin && Pin->PinName == DataTablePinName)
	{
		// Linking/unlinking the DataTable pin can also change which rows
		// are reachable (or make the literal unresolvable entirely) — run
		// the same revalidation before broadcasting.
		ClearRowNameIfInvalidForCurrentDataTable();

		// Going from linked ↔ unlinked also flips whether the row-name
		// widget should show a dropdown or fall back to text entry, so it
		// needs the same refresh signal.
		OnDataTablePinChangedDelegate.Broadcast();
	}
}

void UK2Node_ActivateComposableCameraFromDataTable::ClearRowNameIfInvalidForCurrentDataTable()
{
	UEdGraphPin* RowNamePin = FindPin(RowNamePinName, EGPD_Input);
	if (!RowNamePin)
	{
		return;
	}

	// A linked row-name pin is driven by another node — we don't own its
	// value and shouldn't try to clear it.
	if (RowNamePin->LinkedTo.Num() > 0)
	{
		return;
	}

	const FString CurrentRowName = RowNamePin->GetDefaultAsString();
	if (CurrentRowName.IsEmpty() || CurrentRowName == FName(NAME_None).ToString())
	{
		// Already None — nothing to clear.
		return;
	}

	// If we can't resolve a literal DataTable (pin is linked, unset, or
	// points at a wrong-row-struct DataTable) we intentionally leave the
	// row name alone: the widget will fall back to a text box in that case
	// and the stale name is still meaningful as a hand-typed hint the
	// author might want to keep.
	const UDataTable* DataTable = ResolveLiteralDataTable();
	if (!DataTable)
	{
		return;
	}

	const TArray<FName> RowNames = DataTable->GetRowNames();
	if (RowNames.Contains(FName(*CurrentRowName)))
	{
		// Current selection is still valid in the new table — keep it.
		return;
	}

	// The row name is now stale. Clear it via the schema so the edit goes
	// through the canonical notification path (Modify(), default-value
	// changed events, and the active transaction scope wrapping the
	// DataTable change).
	const FString NoneString = FName(NAME_None).ToString();
	if (const UEdGraphSchema* Schema = RowNamePin->GetSchema())
	{
		Schema->TrySetDefaultValue(*RowNamePin, NoneString);
	}
	else if (UEdGraphNode* OwningNode = RowNamePin->GetOwningNodeUnchecked())
	{
		OwningNode->Modify();
		RowNamePin->DefaultValue = NoneString;
	}
}

UScriptStruct* UK2Node_ActivateComposableCameraFromDataTable::GetRequiredRowStruct()
{
	return FComposableCameraParameterTableRow::StaticStruct();
}

UDataTable* UK2Node_ActivateComposableCameraFromDataTable::ResolveLiteralDataTable() const
{
	const UEdGraphPin* DataTablePin = FindPin(DataTablePinName, EGPD_Input);
	if (!DataTablePin)
	{
		return nullptr;
	}

	// Only literal values are resolvable. Anything linked will be computed
	// at runtime and can't drive a static row-name dropdown.
	if (DataTablePin->LinkedTo.Num() > 0)
	{
		return nullptr;
	}

	UDataTable* DataTable = Cast<UDataTable>(DataTablePin->DefaultObject);
	if (!DataTable)
	{
		return nullptr;
	}

	// Defensive row-struct recheck. The asset picker should already reject
	// mismatched DataTables, but a stale reference from before the filter
	// was added — or a user editing the graph text directly — could still
	// produce a wrong-row-type literal. An IsChildOf check (rather than
	// exact equality) is forward-compatible with any future row-struct
	// subclasses that might appear.
	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct || !RowStruct->IsChildOf(GetRequiredRowStruct()))
	{
		return nullptr;
	}

	return DataTable;
}

#undef LOCTEXT_NAMESPACE
