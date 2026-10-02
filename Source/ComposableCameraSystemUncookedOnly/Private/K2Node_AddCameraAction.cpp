// Copyright 2026 Sulley. All Rights Reserved.

#include "K2Node_AddCameraAction.h"

#include "Actions/ComposableCameraActionBase.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "ComposableCameraEdGraphPinTypeUtils.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "DataAssets/ComposableCameraActionTypeAsset.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_TemporaryVariable.h"
#include "KismetCompiler.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"

#define LOCTEXT_NAMESPACE "K2Node_AddCameraAction"

const FName UK2Node_AddCameraAction::PN_PlayerCameraManager(TEXT("PlayerCameraManager"));
const FName UK2Node_AddCameraAction::PN_ActionAsset(TEXT("ActionAsset"));
const FName UK2Node_AddCameraAction::PN_OnlyForCurrentCamera(TEXT("bOnlyForCurrentCamera"));
const FName UK2Node_AddCameraAction::PN_ReturnValue(TEXT("ReturnValue"));

void UK2Node_AddCameraAction::PostLoad()
{
	Super::PostLoad();
	SubscribeToAssetChanges();
}

void UK2Node_AddCameraAction::BeginDestroy()
{
	if (PropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
		PropertyChangedHandle.Reset();
	}
	Super::BeginDestroy();
}

void UK2Node_AddCameraAction::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();
	SubscribeToAssetChanges();
}

void UK2Node_AddCameraAction::SubscribeToAssetChanges()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && !PropertyChangedHandle.IsValid())
	{
		PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(
			this, &UK2Node_AddCameraAction::HandleObjectPropertyChanged);
	}
}

void UK2Node_AddCameraAction::HandleObjectPropertyChanged(
	UObject* Object, FPropertyChangedEvent& /*Event*/)
{
	if (bIsReconstructing || !CachedActionAsset || !GetGraph()
		|| (Object != CachedActionAsset
			&& Object != CachedActionAsset->ActionTemplate.Get()))
	{
		return;
	}
	ReconstructNode();
	if (UBlueprint* Blueprint = GetBlueprint())
	{
		if (!Blueprint->bBeingCompiled)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}
}

void UK2Node_AddCameraAction::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	FEdGraphPinType ObjectType;
	ObjectType.PinCategory = UEdGraphSchema_K2::PC_Object;
	ObjectType.PinSubCategoryObject = AComposableCameraPlayerCameraManager::StaticClass();
	CreatePin(EGPD_Input, ObjectType, PN_PlayerCameraManager);
	ObjectType.PinSubCategoryObject = UComposableCameraActionTypeAsset::StaticClass();
	CreatePin(EGPD_Input, ObjectType, PN_ActionAsset);
	UEdGraphPin* ScopePin = CreatePin(EGPD_Input,
		UEdGraphSchema_K2::PC_Boolean, PN_OnlyForCurrentCamera);
	ScopePin->DefaultValue = TEXT("false");
	ObjectType.PinSubCategoryObject = UComposableCameraActionBase::StaticClass();
	CreatePin(EGPD_Output, ObjectType, PN_ReturnValue);

	CreateDynamicParameterPins();
	Super::AllocateDefaultPins();
}

void UK2Node_AddCameraAction::CreateDynamicParameterPins()
{
	DynamicParameterPinNames.Reset();
	if (!CachedActionAsset || !CachedActionAsset->ActionTemplate)
	{
		AdvancedPinDisplay = ENodeAdvancedPins::NoPins;
		return;
	}
	for (TFieldIterator<FProperty> It(CachedActionAsset->ActionTemplate->GetClass()); It; ++It)
	{
		FProperty* Property = *It;
		if (!UComposableCameraActionTypeAsset::IsExposableProperty(Property))
		{
			continue;
		}
		EComposableCameraPinType PinType;
		UScriptStruct* StructType = nullptr;
		UEnum* EnumType = nullptr;
		UFunction* SignatureFunction = nullptr;
		if (!TryMapPropertyToPinType(Property, PinType, StructType, EnumType,
			&SignatureFunction))
		{
			continue;
		}
		const FName Name = Property->GetFName();
		if (FindPin(Name))
		{
			continue;
		}
		FEdGraphPinType GraphType =
			ComposableCameraEdGraphPinTypeUtils::MakeEdGraphPinTypeFromCameraPinType(
				PinType, StructType, EnumType, SignatureFunction);
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			GraphType.PinSubCategoryObject = ObjectProperty->PropertyClass;
		}
		UEdGraphPin* Pin = CreatePin(EGPD_Input, GraphType, Name);
		Pin->PinFriendlyName = Property->GetDisplayNameText();
		Pin->PinToolTip = Property->GetToolTipText().ToString();
		Pin->bAdvancedView = true;
		Pin->bDefaultValueIsIgnored = true;
		DynamicParameterPinNames.Add(Name);
	}
	AdvancedPinDisplay = DynamicParameterPinNames.IsEmpty()
		? ENodeAdvancedPins::NoPins : ENodeAdvancedPins::Hidden;
}

void UK2Node_AddCameraAction::ReallocatePinsDuringReconstruction(
	TArray<UEdGraphPin*>& OldPins)
{
	TGuardValue<bool> Guard(bIsReconstructing, true);
	for (const UEdGraphPin* Pin : OldPins)
	{
		if (!Pin || Pin->PinName != PN_ActionAsset)
		{
			continue;
		}
		if (!Pin->LinkedTo.IsEmpty())
		{
			CachedActionAsset = nullptr;
		}
		else if (UComposableCameraActionTypeAsset* Resolved =
			Cast<UComposableCameraActionTypeAsset>(Pin->DefaultObject))
		{
			CachedActionAsset = Resolved;
		}
		else if (!Pin->DefaultValue.IsEmpty())
		{
			FSoftObjectPath Path(Pin->DefaultValue);
			CachedActionAsset = Cast<UComposableCameraActionTypeAsset>(Path.TryLoad());
		}
		if (CachedActionAsset)
		{
			CachedActionAsset->ConditionalPostLoad();
		}
		break;
	}
	Super::ReallocatePinsDuringReconstruction(OldPins);
}

void UK2Node_AddCameraAction::RefreshAsset()
{
	UEdGraphPin* Pin = FindPin(PN_ActionAsset);
	UComposableCameraActionTypeAsset* NewAsset = Pin && Pin->LinkedTo.IsEmpty()
		? Cast<UComposableCameraActionTypeAsset>(Pin->DefaultObject) : nullptr;
	if (NewAsset == CachedActionAsset)
	{
		return;
	}
	CachedActionAsset = NewAsset;
	ReconstructNode();
}

void UK2Node_AddCameraAction::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);
	if (!bIsReconstructing && Pin && Pin->PinName == PN_ActionAsset)
	{
		RefreshAsset();
	}
}

void UK2Node_AddCameraAction::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	if (!bIsReconstructing && Pin && Pin->PinName == PN_ActionAsset)
	{
		RefreshAsset();
	}
}

FText UK2Node_AddCameraAction::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "Add a fresh Action from a Composable Camera Action asset. Connected parameter pins override its authored defaults.");
}

FText UK2Node_AddCameraAction::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return CachedActionAsset && TitleType == ENodeTitleType::FullTitle
		? FText::Format(LOCTEXT("AssetTitle", "Add Camera Action\n{0}"),
			FText::FromString(CachedActionAsset->GetName()))
		: LOCTEXT("Title", "Add Camera Action");
}

FLinearColor UK2Node_AddCameraAction::GetNodeTitleColor() const
{
	return FLinearColor(FColor(102, 90, 229));
}

FSlateIcon UK2Node_AddCameraAction::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor(.823f, .823f, .823f);
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.CameraComponent");
}

void UK2Node_AddCameraAction::GetMenuActions(
	FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	if (ActionRegistrar.IsOpenForRegistration(GetClass()))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
		Spawner->DefaultMenuSignature.Category = GetMenuCategory();
		Spawner->DefaultMenuSignature.MenuName = LOCTEXT("MenuName", "Add Camera Action from Asset");
		ActionRegistrar.AddBlueprintAction(GetClass(), Spawner);
	}
}

FText UK2Node_AddCameraAction::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "ComposableCameraSystem|Action");
}

void UK2Node_AddCameraAction::ValidateNodeDuringCompilation(
	FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);
	const UEdGraphPin* AssetPin = FindPin(PN_ActionAsset);
	if (AssetPin && AssetPin->LinkedTo.IsEmpty() && CachedActionAsset
		&& !CachedActionAsset->ActionTemplate)
	{
		MessageLog.Error(TEXT("Add Camera Action node @@ has an asset without an Action Template."), this);
	}
}

void UK2Node_AddCameraAction::ExpandNode(
	FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);
	UK2Node_TemporaryVariable* BlockNode =
		CompilerContext.SpawnIntermediateNode<UK2Node_TemporaryVariable>(this, SourceGraph);
	BlockNode->VariableType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	BlockNode->VariableType.PinSubCategoryObject = FComposableCameraParameterBlock::StaticStruct();
	BlockNode->AllocateDefaultPins();
	UEdGraphPin* BlockPin = BlockNode->GetVariablePin();

	UEdGraphPin* FirstSetterExec = nullptr;
	UEdGraphPin* PreviousThen = nullptr;
	for (FName Name : DynamicParameterPinNames)
	{
		UEdGraphPin* Input = FindPin(Name);
		if (!Input || Input->LinkedTo.IsEmpty())
		{
			continue;
		}
		const FName SetterName =
			ComposableCameraEdGraphPinTypeUtils::ResolveTypedSetterFunctionName(Input->PinType);
		UFunction* Setter = UComposableCameraBlueprintLibrary::StaticClass()->FindFunctionByName(SetterName);
		if (!Setter)
		{
			CompilerContext.MessageLog.Error(TEXT("Add Camera Action node @@ has an unsupported parameter pin."), this);
			BreakAllNodeLinks();
			return;
		}
		UK2Node_CallFunction* SetterNode =
			CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		SetterNode->SetFromFunction(Setter);
		SetterNode->AllocateDefaultPins();
		BlockPin->MakeLinkTo(SetterNode->FindPinChecked(TEXT("ParameterBlock")));
		SetterNode->FindPinChecked(TEXT("ParameterName"))->DefaultValue = Name.ToString();
		UEdGraphPin* ValuePin = SetterNode->FindPinChecked(TEXT("Value"));
		ValuePin->PinType = Input->PinType;
		CompilerContext.MovePinLinksToIntermediate(*Input, *ValuePin);
		if (!FirstSetterExec)
		{
			FirstSetterExec = SetterNode->GetExecPin();
		}
		if (PreviousThen)
		{
			PreviousThen->MakeLinkTo(SetterNode->GetExecPin());
		}
		PreviousThen = SetterNode->GetThenPin();
	}

	UK2Node_CallFunction* AddNode =
		CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	AddNode->SetFromFunction(UComposableCameraBlueprintLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UComposableCameraBlueprintLibrary, AddActionFromAsset)));
	AddNode->AllocateDefaultPins();
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_PlayerCameraManager),
		*AddNode->FindPinChecked(PN_PlayerCameraManager));
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_ActionAsset),
		*AddNode->FindPinChecked(PN_ActionAsset));
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_OnlyForCurrentCamera),
		*AddNode->FindPinChecked(PN_OnlyForCurrentCamera));
	BlockPin->MakeLinkTo(AddNode->FindPinChecked(TEXT("Parameters")));
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_ReturnValue),
		*AddNode->GetReturnValuePin());
	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(),
		*(FirstSetterExec ? FirstSetterExec : AddNode->GetExecPin()));
	if (PreviousThen)
	{
		PreviousThen->MakeLinkTo(AddNode->GetExecPin());
	}
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(UEdGraphSchema_K2::PN_Then),
		*AddNode->GetThenPin());
	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE
