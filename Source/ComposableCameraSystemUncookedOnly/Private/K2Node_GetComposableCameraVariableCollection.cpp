// Copyright Sulley. All rights reserved.

#include "K2Node_GetComposableCameraVariableCollection.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/DefaultValueHelper.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"
#include "Variables/ComposableCameraVariableCollection.h"

#define LOCTEXT_NAMESPACE "K2Node_SetComposableCameraVariableCollection"

void UK2Node_GetComposableCameraVariableCollection::AllocateDefaultPins()
{
	bReconstructNode = false;

	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	TArray<FName> VariablePinNames;
	VariablePinToValuePin.GetKeys(VariablePinNames);
	
	for (FName VariablePinName : VariablePinNames)
	{
		UEdGraphPin* Pin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, VariablePinName);
		Pin->DefaultValue = VariablePinNameToVariableName[VariablePinName];
		Pin->bNotConnectable = true;

		if (VariablePinToValuePin.Contains(VariablePinName))
		{
			FName ValuePinName = VariablePinToValuePin[VariablePinName];
			CreatePin(EGPD_Output, VariableNameToPinType[VariablePinNameToVariableName[VariablePinName]], ValuePinName);
		}
	}
	
	for (int32 Idx : UnusedPinIndex)
	{
		const FName PinName = *FString::Printf(TEXT("Variable %d"), Idx);
		UEdGraphPin* Pin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, PinName);
		Pin->bNotConnectable = true;
	}
	
	Super::AllocateDefaultPins();
}

FText UK2Node_GetComposableCameraVariableCollection::GetTooltipText() const
{
	return FText::Format(
			LOCTEXT("K2Node_GetComposableCameraVariableCollection_NodeTitle", "Gets the runtime variable values of variable collection {0}."),
			FText::FromString(GetNameSafe(Collection)));
}

FText UK2Node_GetComposableCameraVariableCollection::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::Format(LOCTEXT("K2Node_GetComposableCameraVariableCollection_NodeTitle", "GET Variable Collection {0}"),
		FText::FromString(GetNameSafe(Collection)));
}

void UK2Node_GetComposableCameraVariableCollection::NodeConnectionListChanged()
{
	Super::NodeConnectionListChanged();

	if (bReconstructNode)
	{
		ReconstructNode();

		UBlueprint* Blueprint = GetBlueprint();
		if(!Blueprint->bBeingCompiled)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}
}

void UK2Node_GetComposableCameraVariableCollection::GetNodeContextMenuActions(class UToolMenu* Menu,
                                                                              class UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	FToolMenuSection& SectionPin = Menu->FindOrAddSection(FName("EdGraphSchemaPinActions"));
	
	SectionPin.AddDynamicEntry("RemovePinDynamicEntry", FNewToolMenuSectionDelegate::CreateLambda([this, Context](FToolMenuSection& InSection)
	{
		FToolUIActionChoice RemovePin_Action(FExecuteAction::CreateLambda([Context]()
			{
				Cast<UK2Node_GetComposableCameraVariableCollection>(ConstCast(Context->Node))->RemoveVariablePin(Context->Pin);
			}));
	
		if (Context->Pin && IsVariableNamePin(Context->Pin))
		{
			InSection.AddEntry(FToolMenuEntry::InitMenuEntry(
				FName("RemovePin"),
				FText::FromString("Remove Pin"),
				FText::FromString(TEXT("Remove Pin")),
				FSlateIcon(FName("OGStyle"),
					FName("Icons.Remove")),
					RemovePin_Action));
		}
	}));
}

void UK2Node_GetComposableCameraVariableCollection::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);

	if (IsVariableNamePin(Pin))
	{
		OnVariableNamePinChanged(Pin);
	}
	else if (IsVariableValuePin(Pin))
	{
		OnVariableValuePinChanged(Pin);
	}
}

void UK2Node_GetComposableCameraVariableCollection::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	if (IsVariableNamePin(Pin))
	{
		OnVariableNamePinChanged(Pin);
	}
	else if (IsVariableValuePin(Pin))
	{
		OnVariableValuePinChanged(Pin);
	}
}

FLinearColor UK2Node_GetComposableCameraVariableCollection::GetNodeTitleColor() const
{
	return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("420039")));
}

FSlateIcon UK2Node_GetComposableCameraVariableCollection::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor(.823f, .823f, .823f);
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Kismet.AllClasses.VariableIcon");
}

void UK2Node_GetComposableCameraVariableCollection::GetMenuActions(
	FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();

	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		FARFilter Filter;
		Filter.ClassPaths.Add(UComposableCameraVariableCollection::StaticClass()->GetClassPathName());

		TArray<FAssetData> CollectionAssets;
		IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
		AssetRegistry.GetAssets(Filter, CollectionAssets);

		for (const FAssetData& CollectionAssetData : CollectionAssets)
		{
			GetMenuAction(ActionRegistrar, CollectionAssetData);
		}
	}
	else if (const UComposableCameraVariableCollection* KeyFilter = Cast<const UComposableCameraVariableCollection>(ActionRegistrar.GetActionKeyFilter()))
	{
		const FAssetData AssetData(KeyFilter);
		GetMenuAction(ActionRegistrar, AssetData);
	}
}

FText UK2Node_GetComposableCameraVariableCollection::GetMenuCategory() const
{
	return LOCTEXT("K2Node_GetComposableCameraVariableCollection_MenuCategory", "ComposableCameraSystem|Variable");
}

void UK2Node_GetComposableCameraVariableCollection::ExpandNode(class FKismetCompilerContext& CompilerContext,
	UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	if (!ValidateCollectionBeforeExpandNode(CompilerContext))
	{
		BreakAllNodeLinks();
		return;
	}
	
	UEdGraphPin* ExecPin = GetExecPin();
	UEdGraphPin* ThenPin = GetThenPin();
	UEdGraphPin* FunctionChainStartExecPin = nullptr;
	UEdGraphPin* FunctionChainEndThenPin = nullptr;
	
	TArray<FName> VariablePinNames;
	VariablePinToValuePin.GetKeys(VariablePinNames);
	
	for (FName VariablePinName : VariablePinNames)
	{
		UEdGraphPin* VariablePin = FindPin(VariablePinName);

		if (!VariablePin)
		{
			continue;
		}
		
		if (UEdGraphPin* ValuePin = FindPin(VariablePinToValuePin[VariablePinName]))
		{
			UK2Node_CallFunction* CallFunction = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			CallFunction->SetFromFunction(UComposableCameraBlueprintLibrary::StaticClass()->FindFunctionByName("GetComposableCameraVariableRuntimeValue"));
			CallFunction->AllocateDefaultPins();

			UEdGraphPin* FunctionExecPin = CallFunction->GetExecPin();
			UEdGraphPin* FunctionThenPin = CallFunction->GetThenPin();
			UEdGraphPin* FunctionVariablePin = CallFunction->FindPinChecked(TEXT("Variable"));
			UEdGraphPin* FunctionValuePin = CallFunction->FindPinChecked(TEXT("ReturnValue"));
			FunctionValuePin->PinType = ValuePin->PinType;
			
			if (UComposableCameraVariable* Variable = FindVariableFromVariablePinName(VariablePinName))
			{
				if (!FunctionChainStartExecPin)
				{
					FunctionChainStartExecPin = FunctionExecPin;
				}
				if (FunctionChainEndThenPin)
				{
					FunctionChainEndThenPin->MakeLinkTo(FunctionExecPin);
				}
				FunctionChainEndThenPin = FunctionThenPin;

				FunctionVariablePin->DefaultObject = Variable;

				for (UEdGraphPin* ValueLinkedPin : ValuePin->LinkedTo)
				{
					FunctionValuePin->MakeLinkTo(ValueLinkedPin);
				}
			}
			else
			{
				FText ErrorMessage = FText::Format(
					LOCTEXT("SetComposableCameraVariableCollection_ExpandNode_Error_Variable", "Invalid variable from variable pin {0}."),
					FText::FromString(VariablePinName.ToString()));
				CompilerContext.MessageLog.Error(*ErrorMessage.ToString());
				BreakAllNodeLinks();
				return;
			}
		}
	}

	CompilerContext.MovePinLinksToIntermediate(*ExecPin, *FunctionChainStartExecPin);
	CompilerContext.MovePinLinksToIntermediate(*ThenPin, *FunctionChainEndThenPin);
	
	BreakAllNodeLinks();
}

void UK2Node_GetComposableCameraVariableCollection::AddInputPin()
{
	TArray<int32> UsedPinIndex = GetUsedPinIndex();
	
	for (int32 PinIndex = 0; ; PinIndex++)
	{
		if (!UnusedPinIndex.Contains(PinIndex) && !UsedPinIndex.Contains(PinIndex))
		{
			UnusedPinIndex.Add(PinIndex);
			break;
		}
	}
	
	Modify();
	ReconstructNode();
}

void UK2Node_GetComposableCameraVariableCollection::RemoveInputPin(UEdGraphPin* Pin)
{
	ReconstructNode();
}

void UK2Node_GetComposableCameraVariableCollection::RemoveVariablePin(const UEdGraphPin* VariablePin)
{
	Modify();
		
	int32 PinIndex = GetVariablePinIndex(VariablePin);
	UnusedPinIndex.Remove(PinIndex);
		
	FName VariablePinName = VariablePin->PinName;
	FName VariableValuePinName = FName(VariablePin->PinName.ToString().Replace(TEXT("Variable"), TEXT("Value")));

	if (UEdGraphPin* ValuePin = FindPin(VariableValuePinName))
	{
		ValuePin->BreakAllPinLinks();
		Pins.Remove(ValuePin);
	}
	if (VariablePinToValuePin.Contains(VariablePinName))
	{
		VariablePinToValuePin.Remove(VariablePinName);
	}
	if (VariablePinNameToVariableName.Contains(VariablePinName))
	{
		VariablePinNameToVariableName.Remove(VariablePinName);
	}

	Pins.Remove(const_cast<UEdGraphPin*>(VariablePin));
	ReconstructNode();
}

TArray<FString> UK2Node_GetComposableCameraVariableCollection::GetVariableNames() const
{
	TArray<FString> Names;
	
	if (Collection)
	{
		for (UComposableCameraVariable* Variable : Collection->Variables)
		{
			if (Variable)
			{
				Names.Add(Variable->DisplayName);
			}
		}
	}

	return Names;
}

void UK2Node_GetComposableCameraVariableCollection::OnVariableNamePinChanged(UEdGraphPin* Pin)
{
	UEdGraphPin* CorrespondingValuePin = FindCorrespondingVariableValuePinFromVariableNamePin(Pin);

	if (!CorrespondingValuePin)
	{
		CreateCorrespondingVariableValuePin(Pin);
	}
	else
	{
		Modify();
		CorrespondingValuePin->BreakAllPinLinks();
		CorrespondingValuePin->PinType = VariableNameToPinType[Pin->DefaultValue];

		VariablePinNameToVariableName[Pin->PinName] = Pin->DefaultValue;
		
		GetGraph()->NotifyGraphChanged();
		FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
	}
}

void UK2Node_GetComposableCameraVariableCollection::OnVariableValuePinChanged(UEdGraphPin* Pin)
{
}

UEdGraphPin* UK2Node_GetComposableCameraVariableCollection::FindCorrespondingVariableValuePinFromVariableNamePin(
	UEdGraphPin* VariableNamePin) const
{
	UEdGraphPin* VariableValuePin = nullptr;

	if (VariableNamePin)
	{
		FString VariableValuePinName = VariableNamePin->PinName.ToString().Replace(TEXT("Variable"), TEXT("Value"));
		VariableValuePin = FindPin(VariableValuePinName);
	}

	return VariableValuePin;
}

void UK2Node_GetComposableCameraVariableCollection::CreateCorrespondingVariableValuePin(UEdGraphPin* Pin)
{
	if (Pin && Collection && FName(Pin->DefaultValue) != FName())
	{
		Modify();
		
		int32 PinIndex = GetVariablePinIndex(Pin);
		UnusedPinIndex.Remove(PinIndex);
		
		FName VariablePinName = Pin->PinName;
		FName VariableValuePinName = FName(Pin->PinName.ToString().Replace(TEXT("Variable"), TEXT("Value")));
		UEdGraphPin* VariableValuePin = CreatePin(EGPD_Output, VariableNameToPinType[Pin->DefaultValue], FName(VariableValuePinName));
		VariablePinToValuePin.Add(VariablePinName, VariableValuePinName);
		VariablePinNameToVariableName.Add(VariablePinName, Pin->DefaultValue);
		
		GetGraph()->NotifyGraphChanged();
		FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
	}
}

UComposableCameraVariable* UK2Node_GetComposableCameraVariableCollection::FindVariableFromVariablePinName(
	FName VariablePinName) const
{
	FString VariableName;
	
	if (VariablePinNameToVariableName.Contains(VariablePinName))
	{
		VariableName = VariablePinNameToVariableName[VariablePinName];
	}

	for (UComposableCameraVariable* Variable : Collection->Variables)
	{
		if (VariableName == Variable->DisplayName)
		{
			return Variable;
		}
	}

	return nullptr;
}

bool UK2Node_GetComposableCameraVariableCollection::IsVariableNamePin(const UEdGraphPin* Pin) const
{
	return Pin->PinName.ToString().StartsWith(TEXT("Variable"));
}

bool UK2Node_GetComposableCameraVariableCollection::IsVariableValuePin(const UEdGraphPin* Pin) const
{
	return Pin->PinName.ToString().StartsWith(TEXT("Value"));
}

TArray<int32> UK2Node_GetComposableCameraVariableCollection::GetUsedPinIndex()
{
	TArray<FName> PinNames;
	VariablePinToValuePin.GetKeys(PinNames);

	TArray<int32> PinIndex;
	for (FName PinName : PinNames)
	{
		FString Name =  PinName.ToString();
		Name.RemoveFromStart("Variable ");
		
		int32 Index;
		FDefaultValueHelper::ParseInt(Name, Index);
		
		PinIndex.Add(Index);
	}

	return PinIndex;
}

int32 UK2Node_GetComposableCameraVariableCollection::GetVariablePinIndex(const UEdGraphPin* Pin) const
{
	FString PinName = Pin->PinName.ToString();
	PinName.RemoveFromStart("Variable ");
		
	int32 PinIndex;
	FDefaultValueHelper::ParseInt(PinName, PinIndex);
	return PinIndex;
}

bool UK2Node_GetComposableCameraVariableCollection::ValidateCollectionBeforeExpandNode(
	FKismetCompilerContext& CompilerContext) const
{
	if (!Collection)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("ErrorVariableCollection", "GetComposableCameraVariableCollection node @@ doesn't have a valid variable collection.").ToString(), this);
		return false;
	}
	return true;
}

UK2Node_CallFunction* UK2Node_GetComposableCameraVariableCollection::MakeLiteralValueForPin(
	FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UK2Node* SourceNode, UEdGraphPin* SourceValuePin)
{
#define COMPOSABLECAMERASYSTEM_NEW_MAKE_LITERAL_NODE(FunctionLibraryClassName, MakeLiteralFunctionName)\
	CreateMakeLiteralNode(\
			CompilerContext, SourceGraph, SourceNode,\
			FunctionLibraryClassName::StaticClass(), TEXT(#MakeLiteralFunctionName),\
			SourceValuePin);

#define COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_CATEGORY(PinCategoryName, MakeLiteralFunctionName)\
	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PinCategoryName)\
	{\
		return COMPOSABLECAMERASYSTEM_NEW_MAKE_LITERAL_NODE(UKismetSystemLibrary, MakeLiteralFunctionName);\
	}

#define COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_SUB_CATEGORY_OBJECT(SubCategoryObject, MakeLiteralFunctionName)\
	if (SourceValuePin->PinType.PinSubCategoryObject == SubCategoryObject)\
	{\
		return COMPOSABLECAMERASYSTEM_NEW_MAKE_LITERAL_NODE(UComposableCameraBlueprintLibrary, MakeLiteralFunctionName);\
	}

	// Support for all types with and inline widget for editing the default value.
	// Start with those supported by the built-in FNodeFactory class.
	COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_CATEGORY(PC_Boolean, MakeLiteralBool)
	COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_CATEGORY(PC_Text, MakeLiteralText)
	COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_CATEGORY(PC_Int, MakeLiteralInt)
	COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_CATEGORY(PC_Int64, MakeLiteralInt64)
	COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_CATEGORY(PC_Name, MakeLiteralName)
	COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_CATEGORY(PC_String, MakeLiteralString)
	COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_CATEGORY(PC_Text, MakeLiteralText)

	COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_CATEGORY(PC_Float, MakeLiteralFloat)
	COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_CATEGORY(PC_Double, MakeLiteralDouble)
	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real && 
			SourceValuePin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float)
	{
		return COMPOSABLECAMERASYSTEM_NEW_MAKE_LITERAL_NODE(UKismetSystemLibrary, MakeLiteralFloat);
	}
	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real && 
			SourceValuePin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double)
	{
		return COMPOSABLECAMERASYSTEM_NEW_MAKE_LITERAL_NODE(UKismetSystemLibrary, MakeLiteralDouble);
	}

	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte &&
		SourceValuePin->PinType.PinSubCategoryObject == nullptr)  // Not an enum
	{
		return COMPOSABLECAMERASYSTEM_NEW_MAKE_LITERAL_NODE(UKismetSystemLibrary, MakeLiteralByte);
	}

	if (SourceValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_SUB_CATEGORY_OBJECT(TBaseStructure<FVector>::Get(), MakeLiteralVector)
		COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_SUB_CATEGORY_OBJECT(TBaseStructure<FVector4>::Get(), MakeLiteralVector4)
		COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_SUB_CATEGORY_OBJECT(TBaseStructure<FVector2D>::Get(), MakeLiteralVector2D)
		COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_SUB_CATEGORY_OBJECT(TBaseStructure<FRotator>::Get(), MakeLiteralRotator)
		COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_SUB_CATEGORY_OBJECT(TBaseStructure<FTransform>::Get(), MakeLiteralTransform)
	}

	return nullptr;

#undef COMPOSABLECAMERASYSTEM_NEW_MAKE_LITERAL_NODE
#undef COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_CATEGORY
#undef COMPOSABLECAMERASYSTEM_CREATE_MAKE_LITERAL_NODE_IF_PIN_SUB_CATEGORY_OBJECT
}

UK2Node_CallFunction* UK2Node_GetComposableCameraVariableCollection::CreateMakeLiteralNode(
	FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UK2Node* SourceNode, UClass* FunctionLibraryClass,
	const TCHAR* FunctionName, UEdGraphPin* SourceValuePin)
{
	UK2Node_CallFunction* MakeLiteralNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(SourceNode, SourceGraph);
	MakeLiteralNode->FunctionReference.SetExternalMember(FunctionName, FunctionLibraryClass);
	MakeLiteralNode->AllocateDefaultPins();
	CompilerContext.MessageLog.NotifyIntermediateObjectCreation(MakeLiteralNode, SourceGraph);

	UEdGraphPin* LiteralValuePin = MakeLiteralNode->FindPinChecked(TEXT("Value"));
	LiteralValuePin->DefaultValue = SourceValuePin->DefaultValue;
	LiteralValuePin->DefaultTextValue = SourceValuePin->DefaultTextValue;
	LiteralValuePin->AutogeneratedDefaultValue = SourceValuePin->AutogeneratedDefaultValue;
	LiteralValuePin->DefaultObject = SourceValuePin->DefaultObject;

	return MakeLiteralNode;
}

void UK2Node_GetComposableCameraVariableCollection::Initialize(const FAssetData& UnloadedCollection)
{
	UComposableCameraVariableCollection* LoadedCollection = Cast<UComposableCameraVariableCollection>(UnloadedCollection.GetAsset());
	if (ensure(LoadedCollection))
	{
		Collection = LoadedCollection;

		for (UComposableCameraVariable* Variable : Collection->Variables)
		{
			if (!Variable)
			{
				continue;
			}
			
			UObject* PinSubCategoryObject = nullptr;
			EComposableCameraVariableType Type = Variable->GetVariableType();

			if (Type == EComposableCameraVariableType::Actor)
			{
				PinSubCategoryObject = AActor::StaticClass();
			}
			
			VariableNameToPinType.Add(Variable->DisplayName,
				MakePinTypeFromVariableType(Type, PinSubCategoryObject));
		}
	}
}

void UK2Node_GetComposableCameraVariableCollection::GetMenuAction(FBlueprintActionDatabaseRegistrar& ActionRegistrar,
                                                                  const FAssetData& CollectionAssetData) const
{
	const FText BaseCategoryString = GetMenuCategory();

	UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
	NodeSpawner->DefaultMenuSignature.Category = BaseCategoryString;
	NodeSpawner->DefaultMenuSignature.MenuName = FText::Format(
			LOCTEXT("GetComposableCameraVariableCollectionActionMenuName", "Get Collection {0}"),
			FText::FromString(CollectionAssetData.AssetName.ToString()));
	NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda(
			[CollectionAssetData](UEdGraphNode* NewNode, bool bIsTemplateNode)
			{
				UK2Node_GetComposableCameraVariableCollection* NewSetter = CastChecked<UK2Node_GetComposableCameraVariableCollection>(NewNode);
				NewSetter->Initialize(CollectionAssetData);
			});

	ActionRegistrar.AddBlueprintAction(CollectionAssetData, NodeSpawner);
}

FEdGraphPinType UK2Node_GetComposableCameraVariableCollection::MakePinTypeFromVariableType(
	EComposableCameraVariableType VariableType, const UObject* InPinSubCategoryObject)
{
	FName PinCategory;
	FName PinSubCategory;
	UObject* PinSubCategoryObject = const_cast<UObject*>(InPinSubCategoryObject);
	
	switch (VariableType)
	{
	case EComposableCameraVariableType::Actor:
		PinCategory = UEdGraphSchema_K2::PC_Object;
		break;
	case EComposableCameraVariableType::Float:
		PinCategory = UEdGraphSchema_K2::PC_Real;
		PinSubCategory = UEdGraphSchema_K2::PC_Float;
		break;
	case EComposableCameraVariableType::Integer32:
		PinCategory = UEdGraphSchema_K2::PC_Int;
		break;
	case EComposableCameraVariableType::Boolean:
		PinCategory = UEdGraphSchema_K2::PC_Boolean;
		break;
	case EComposableCameraVariableType::Double:
		PinCategory = UEdGraphSchema_K2::PC_Real;
		PinSubCategory = UEdGraphSchema_K2::PC_Double;
		break;
	case EComposableCameraVariableType::Vector2d:
		PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
		break;
	case EComposableCameraVariableType::Vector3d:
		PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinSubCategoryObject = TBaseStructure<FVector>::Get();
		break;
	case EComposableCameraVariableType::Vector4d:
		PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinSubCategoryObject = TBaseStructure<FVector4>::Get();
		break;
	case EComposableCameraVariableType::Rotator3d:
		PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinSubCategoryObject = TBaseStructure<FRotator>::Get();
		break;
	case EComposableCameraVariableType::Transform3d:
		PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinSubCategoryObject = TBaseStructure<FTransform>::Get();
		break;
	case EComposableCameraVariableType::BlendableStruct:
		PinCategory = UEdGraphSchema_K2::PC_Struct;
		break;
	}

	FEdGraphPinType PinType;
	PinType.PinCategory = PinCategory;
	PinType.PinSubCategory = PinSubCategory;
	PinType.PinSubCategoryObject = PinSubCategoryObject;
	return PinType;
}

#undef LOCTEXT_NAMESPACE
