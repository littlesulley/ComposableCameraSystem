// Copyright 2026 Sulley. All Rights Reserved.

#include "K2Node_AddCameraModifier.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

#define LOCTEXT_NAMESPACE "K2Node_AddCameraModifier"

const FName UK2Node_AddCameraModifier::PN_PlayerCameraManager(TEXT("PlayerCameraManager"));
const FName UK2Node_AddCameraModifier::PN_ModifierAsset(TEXT("ModifierAsset"));

void UK2Node_AddCameraModifier::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	FEdGraphPinType ObjectType;
	ObjectType.PinCategory = UEdGraphSchema_K2::PC_Object;
	ObjectType.PinSubCategoryObject = AComposableCameraPlayerCameraManager::StaticClass();
	CreatePin(EGPD_Input, ObjectType, PN_PlayerCameraManager);
	ObjectType.PinSubCategoryObject = UComposableCameraNodeModifierDataAsset::StaticClass();
	CreatePin(EGPD_Input, ObjectType, PN_ModifierAsset);

	Super::AllocateDefaultPins();
}

void UK2Node_AddCameraModifier::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);
	if (Pin && Pin->PinName == PN_ModifierAsset && GetGraph())
	{
		GetGraph()->NotifyGraphChanged();
	}
}

void UK2Node_AddCameraModifier::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	if (Pin && Pin->PinName == PN_ModifierAsset && GetGraph())
	{
		GetGraph()->NotifyGraphChanged();
	}
}

FText UK2Node_AddCameraModifier::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "Add a Camera Node Modifier asset to the player camera manager using its authored settings.");
}

FText UK2Node_AddCameraModifier::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const UEdGraphPin* AssetPin = FindPin(PN_ModifierAsset);
	if (AssetPin && AssetPin->LinkedTo.IsEmpty()
		&& (TitleType == ENodeTitleType::FullTitle || TitleType == ENodeTitleType::EditableTitle))
	{
		if (const UComposableCameraNodeModifierDataAsset* Asset =
			Cast<UComposableCameraNodeModifierDataAsset>(AssetPin->DefaultObject))
		{
			return FText::Format(LOCTEXT("AssetTitle", "Add Camera Modifier\n{0}"),
				FText::FromString(Asset->GetName()));
		}
	}
	return LOCTEXT("Title", "Add Camera Modifier");
}

FLinearColor UK2Node_AddCameraModifier::GetNodeTitleColor() const
{
	// Match the Camera Node Modifier asset's Content Browser color.
	return FLinearColor(FColor(160, 90, 200));
}

FSlateIcon UK2Node_AddCameraModifier::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor(.823f, .823f, .823f);
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.CameraComponent");
}

void UK2Node_AddCameraModifier::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(ActionKey);
		Spawner->DefaultMenuSignature.Category = GetMenuCategory();
		Spawner->DefaultMenuSignature.MenuName = LOCTEXT("MenuName", "Add Camera Modifier");
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

FText UK2Node_AddCameraModifier::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "ComposableCameraSystem|Modifier");
}

void UK2Node_AddCameraModifier::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UK2Node_CallFunction* AddNode =
		CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	AddNode->SetFromFunction(UComposableCameraBlueprintLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, AddModifier)));
	AddNode->AllocateDefaultPins();

	// WorldContextObject is resolved by the function call's WorldContext metadata.
	// Moving the data pins also preserves literal asset defaults and variable links.
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_PlayerCameraManager),
		*AddNode->FindPinChecked(PN_PlayerCameraManager));
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_ModifierAsset),
		*AddNode->FindPinChecked(PN_ModifierAsset));
	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *AddNode->GetExecPin());
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(UEdGraphSchema_K2::PN_Then),
		*AddNode->GetThenPin());
	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE
