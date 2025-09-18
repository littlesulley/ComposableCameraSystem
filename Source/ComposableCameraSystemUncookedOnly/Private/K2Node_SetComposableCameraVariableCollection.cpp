// Copyright Sulley. All rights reserved.

#include "K2Node_SetComposableCameraVariableCollection.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintEditor.h"
#include "BlueprintNodeSpawner.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ExecutionSequence.h"
#include "KismetCompiler.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/DefaultValueHelper.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"
#include "Variables/ComposableCameraVariableCollection.h"


#define LOCTEXT_NAMESPACE "K2Node_SetComposableCameraVariableCollection"

class IAssetRegistry;

void UK2Node_SetComposableCameraVariableCollection::Initialize(const FAssetData& UnloadedCollection)
{
	UComposableCameraVariableCollection* LoadedCollection = Cast<UComposableCameraVariableCollection>(UnloadedCollection.GetAsset());
	if (ensure(LoadedCollection))
	{
		Collection = LoadedCollection;

		for (UComposableCameraVariable* Variable : Collection->Variables)
		{
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

void UK2Node_SetComposableCameraVariableCollection::AllocateDefaultPins()
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
			CreatePin(EGPD_Input, VariableNameToPinType[VariablePinNameToVariableName[VariablePinName]], ValuePinName);
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


void UK2Node_SetComposableCameraVariableCollection::GetMenuActions(
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

FText UK2Node_SetComposableCameraVariableCollection::GetMenuCategory() const
{
	return LOCTEXT("K2Node_SetComposableCameraVariableCollection_MenuCategory", "ComposableCameraSystem|Variable");
}

void UK2Node_SetComposableCameraVariableCollection::ExpandNode(class FKismetCompilerContext& CompilerContext,
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
	UEdGraphPin* LastExecPin = ExecPin;
	
	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

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
			CallFunction->SetFromFunction(UComposableCameraBlueprintLibrary::StaticClass()->FindFunctionByName("SetComposableCameraVariableRuntimeValue"));
			CallFunction->AllocateDefaultPins();

			UEdGraphPin* FunctionExecPin = CallFunction->GetExecPin();
			UEdGraphPin* FunctionThenPin = CallFunction->GetThenPin();
			UEdGraphPin* FunctionVariablePin = CallFunction->FindPinChecked(TEXT("Variable"));
			UEdGraphPin* FunctionValuePin = CallFunction->FindPinChecked(TEXT("NewRuntimeValue"));
			FunctionValuePin->PinType = ValuePin->PinType;
			
			if (UComposableCameraVariable* Variable = FindVariableFromVariablePinName(VariablePinName))
			{
				CompilerContext.MovePinLinksToIntermediate(*LastExecPin, *FunctionExecPin);
				CompilerContext.MovePinLinksToIntermediate(*ThenPin, *FunctionThenPin);
				FunctionVariablePin->DefaultObject = Variable;

				if (ValuePin->LinkedTo.Num() > 0)
				{
					CompilerContext.MovePinLinksToIntermediate(*ValuePin, *FunctionValuePin);
				}
				else if (UK2Node_CallFunction* MakeLiteral = MakeLiteralValueForPin(CompilerContext, SourceGraph, this, ValuePin))
				{
					MakeLiteral->GetReturnValuePin()->MakeLinkTo(FunctionValuePin);
				}
				else
				{
					FText MissingParameterConnectionMsg = FText::Format(
							LOCTEXT(
								"ErrorRequiresLiteral", 
								"SetComposableCameraVariableCollection node @@ parameter value pin '{0}' must have an input connected into it "
								"(try connecting a MakeStruct, EnumLiteral, or appropriate node)."),
							FText::FromString(ValuePin->PinName.ToString()));
					CompilerContext.MessageLog.Error(*MissingParameterConnectionMsg.ToString(), this);
				}

				LastExecPin = FunctionThenPin;
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

	BreakAllNodeLinks();
}

FText UK2Node_SetComposableCameraVariableCollection::GetTooltipText() const
{
	return FText::Format(
			LOCTEXT("K2Node_SetComposableCameraVariableCollection_NodeTitle", "Sets the variable values of variable collection {0}."),
			FText::FromString(GetNameSafe(Collection)));
}

FText UK2Node_SetComposableCameraVariableCollection::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::Format(LOCTEXT("K2Node_SetComposableCameraVariableCollection_NodeTitle", "SET Variable Collection {0}"),
		FText::FromString(GetNameSafe(Collection)));
}

void UK2Node_SetComposableCameraVariableCollection::NodeConnectionListChanged()
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

void UK2Node_SetComposableCameraVariableCollection::GetNodeContextMenuActions(class UToolMenu* Menu,
	class UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	FToolMenuSection& SectionPin = Menu->FindOrAddSection(FName("EdGraphSchemaPinActions"));
	
	SectionPin.AddDynamicEntry("RemovePinDynamicEntry", FNewToolMenuSectionDelegate::CreateLambda([this, Context](FToolMenuSection& InSection)
	{
		FToolUIActionChoice RemovePin_Action(FExecuteAction::CreateLambda([Context]()
			{
				Cast<UK2Node_SetComposableCameraVariableCollection>(ConstCast(Context->Node))->RemoveVariablePin(Context->Pin);
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

void UK2Node_SetComposableCameraVariableCollection::PinDefaultValueChanged(UEdGraphPin* Pin)
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

void UK2Node_SetComposableCameraVariableCollection::PinConnectionListChanged(UEdGraphPin* Pin)
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


void UK2Node_SetComposableCameraVariableCollection::AddInputPin()
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

void UK2Node_SetComposableCameraVariableCollection::RemoveInputPin(UEdGraphPin* Pin)
{
	ReconstructNode();
}

void UK2Node_SetComposableCameraVariableCollection::RemoveVariablePin(const UEdGraphPin* VariablePin)
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

TArray<FString> UK2Node_SetComposableCameraVariableCollection::GetVariableNames() const
{
	TArray<FString> Names;
	
	if (Collection)
	{
		for (UComposableCameraVariable* Variable : Collection->Variables)
		{
			Names.Add(Variable->DisplayName);
		}
	}

	return Names;
}

void UK2Node_SetComposableCameraVariableCollection::OnVariableNamePinChanged(UEdGraphPin* Pin)
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

void UK2Node_SetComposableCameraVariableCollection::OnVariableValuePinChanged(UEdGraphPin* Pin)
{
}

UEdGraphPin* UK2Node_SetComposableCameraVariableCollection::FindCorrespondingVariableValuePinFromVariableNamePin(
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

void UK2Node_SetComposableCameraVariableCollection::CreateCorrespondingVariableValuePin(UEdGraphPin* Pin)
{
	if (Pin && FName(Pin->DefaultValue) != FName())
	{
		Modify();
		
		int32 PinIndex = GetVariablePinIndex(Pin);
		UnusedPinIndex.Remove(PinIndex);
		
		FName VariablePinName = Pin->PinName;
		FName VariableValuePinName = FName(Pin->PinName.ToString().Replace(TEXT("Variable"), TEXT("Value")));
		UEdGraphPin* VariableValuePin = CreatePin(EGPD_Input, VariableNameToPinType[Pin->DefaultValue], FName(VariableValuePinName));
		VariablePinToValuePin.Add(VariablePinName, VariableValuePinName);
		VariablePinNameToVariableName.Add(VariablePinName, Pin->DefaultValue);
		
		GetGraph()->NotifyGraphChanged();
		FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
	}
}

int32 UK2Node_SetComposableCameraVariableCollection::GetVariablePinIndex(const UEdGraphPin* Pin) const
{
	FString PinName = Pin->PinName.ToString();
	PinName.RemoveFromStart("Variable ");
		
	int32 PinIndex;
	FDefaultValueHelper::ParseInt(PinName, PinIndex);
	return PinIndex;
}

UComposableCameraVariable* UK2Node_SetComposableCameraVariableCollection::FindVariableFromVariablePinName(
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

bool UK2Node_SetComposableCameraVariableCollection::IsVariableNamePin(const UEdGraphPin* Pin) const
{
	return Pin->PinName.ToString().StartsWith(TEXT("Variable"));
}

bool UK2Node_SetComposableCameraVariableCollection::IsVariableValuePin(const UEdGraphPin* Pin) const
{
	return Pin->PinName.ToString().StartsWith(TEXT("Value"));
}

TArray<int32> UK2Node_SetComposableCameraVariableCollection::GetUsedPinIndex()
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

bool UK2Node_SetComposableCameraVariableCollection::ValidateCollectionBeforeExpandNode(
	FKismetCompilerContext& CompilerContext) const
{
	if (!Collection)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("ErrorVariableCollection", "SetComposableCameraVariableCollection node @@ doesn't have a valid variable collection.").ToString(), this);
		return false;
	}
	return true;
}

UK2Node_CallFunction* UK2Node_SetComposableCameraVariableCollection::MakeLiteralValueForPin(
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

UK2Node_CallFunction* UK2Node_SetComposableCameraVariableCollection::CreateMakeLiteralNode(
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

void UK2Node_SetComposableCameraVariableCollection::GetMenuAction(FBlueprintActionDatabaseRegistrar& ActionRegistrar,
                                                                  const FAssetData& CollectionAssetData) const
{
	const FText BaseCategoryString = GetMenuCategory();

	UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
	NodeSpawner->DefaultMenuSignature.Category = BaseCategoryString;
	NodeSpawner->DefaultMenuSignature.MenuName = FText::Format(
			LOCTEXT("SetComposableCameraVariableCollectionActionMenuName", "Set Collection {0}"),
			FText::FromString(CollectionAssetData.AssetName.ToString()));
	NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda(
			[CollectionAssetData](UEdGraphNode* NewNode, bool bIsTemplateNode)
			{
				UK2Node_SetComposableCameraVariableCollection* NewSetter = CastChecked<UK2Node_SetComposableCameraVariableCollection>(NewNode);
				NewSetter->Initialize(CollectionAssetData);
			});

	ActionRegistrar.AddBlueprintAction(CollectionAssetData, NodeSpawner);
}

FEdGraphPinType UK2Node_SetComposableCameraVariableCollection::MakePinTypeFromVariableType(
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


FLinearColor UK2Node_SetComposableCameraVariableCollection::GetNodeTitleColor() const
{
	return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("420039")));
}

FSlateIcon UK2Node_SetComposableCameraVariableCollection::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor(.823f, .823f, .823f);
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Kismet.AllClasses.VariableIcon");
}


#undef LOCTEXT_NAMESPACE
