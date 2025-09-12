// Copyright Sulley. All rights reserved.

#include "K2Node_SetComposableCameraVariableCollection.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ExecutionSequence.h"
#include "KismetCompiler.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"
#include "Variables/ComposableCameraVariableCollection.h"


#define LOCTEXT_NAMESPACE "K2Node_SetComposableCameraVariableCollection"

class IAssetRegistry;

struct FK2Node_SetComposableCameraVariableCollectionPinNames
{
	static inline const FName CollectionPinName = "Collection";
};


void UK2Node_SetComposableCameraVariableCollection::Initialize(const FAssetData& UnloadedCollection, const FString& InCollectionName)
{
	UComposableCameraVariableCollection* LoadedCollection = Cast<UComposableCameraVariableCollection>(UnloadedCollection.GetAsset());
	if (ensure(LoadedCollection))
	{
		
	}
}

void UK2Node_SetComposableCameraVariableCollection::AllocateDefaultPins()
{
	bReconstructNode = false;
	
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	
	UEdGraphPin* InVariablePin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UComposableCameraVariableCollection::StaticClass(), FK2Node_SetComposableCameraVariableCollectionPinNames::CollectionPinName);
	K2Schema->ConstructBasicPinTooltip(*InVariablePin, LOCTEXT("InCollectionPinDescription", "The variable collection you want to set its values to."), InVariablePin->PinToolTip);

	for (int32 Idx = 0; Idx < NumPins; Idx++)
	{
		const FName PinName = *FString::Printf(TEXT("Variable %d"), Idx);
		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, PinName);
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

	UEdGraphPin* ExecPin = GetExecPin();
	UEdGraphPin* ThenPin = GetThenPin();
	UEdGraphPin* LastExecPin = ExecPin;
	
	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();
	// for (UEdGraphPin* VariableNamePin : Pins)
	// {
	// 	if (IsVariableNamePin(VariableNamePin))
	// 	{
	// 		if (UEdGraphPin* ValuePin = FindCorrespondingVariableValuePinFromVariableNamePin(Pin))
	// 		{
	// 			UK2Node_CallFunction* CallFunction = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	// 			CallFunction->SetFromFunction(UComposableCameraBlueprintLibrary::StaticClass()->FindFunctionByName("SetComposableCameraVariableRuntimeValue"));
	// 			CallFunction->AllocateDefaultPins();
	//
	// 			UEdGraphPin* FunctionExecPin = CallFunction->GetExecPin();
	// 			UEdGraphPin* FunctionThenPin = CallFunction->GetThenPin();
	// 			UEdGraphPin* FunctionVariablePin = CallFunction->FindPinChecked(TEXT("Variable"));
	// 			UEdGraphPin* FunctionValuePin = CallFunction->FindPinChecked(TEXT("NewRuntimeValue"));
	// 			FunctionValuePin->PinType = ValuePin->PinType;
	// 			
	// 			CompilerContext.MovePinLinksToIntermediate(*LastExecPin, *FunctionExecPin);
	// 			CompilerContext.MovePinLinksToIntermediate(*ThenPin, *FunctionThenPin);
	//
	// 			if (UComposableCameraVariable* Variable = )
	// 			if (VariablePin->LinkedTo.Num() == 1 || VariablePin->DefaultObject)
	// 			{
	// 				CompilerContext.MovePinLinksToIntermediate(*VariablePin, *FunctionVariablePin);
	// 				CompilerContext.MovePinLinksToIntermediate(*ValuePin, *FunctionValuePin);
	// 			}
	// 			else
	// 			{
	// 				CompilerContext.MessageLog.Error(
	// 					*(LOCTEXT("SetComposableCameraVariableValue_ExpandNode_Error_VariablePin", "Invalid variable.").ToString()));
	// 				BreakAllNodeLinks();
	// 				return;
	// 			}
	// 		}
	// 		
	// 	}
	// 	
	//
	// }
}

FText UK2Node_SetComposableCameraVariableCollection::GetTooltipText() const
{
	return LOCTEXT("K2Node_SetComposableCameraVariableCollection_NodeTitle", "Set Composable Camera Variables in a Composable Camera Variable Collection.");
}

FText UK2Node_SetComposableCameraVariableCollection::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("K2Node_SetComposableCameraVariableCollection_NodeTitle", "Set Composable Camera Variable Collection.");
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

void UK2Node_SetComposableCameraVariableCollection::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);

	if (IsCollectionPin(Pin))
	{
		OnCollectionPinChanged(Pin);
	}
	else if (IsVariableNamePin(Pin))
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

	if (IsCollectionPin(Pin))
	{
		OnCollectionPinChanged(Pin);
	}
	else if (IsVariableNamePin(Pin))
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
	Modify();
	NumPins++;

	// Let AllocateDefaultPins handle the actual addition via ReconstructNode.
	ReconstructNode();
}

void UK2Node_SetComposableCameraVariableCollection::OnPinTypeChanged(UEdGraphPin* Pin)
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	if (Pin == GetCollectionPin())
	{
		
	}
	else
	{
		
	}
}

void UK2Node_SetComposableCameraVariableCollection::OnCollectionPinChanged(UEdGraphPin* Pin)
{
}

void UK2Node_SetComposableCameraVariableCollection::OnVariableNamePinChanged(UEdGraphPin* Pin)
{
}

void UK2Node_SetComposableCameraVariableCollection::OnVariableValuePinChanged(UEdGraphPin* Pin)
{
}

UEdGraphPin* UK2Node_SetComposableCameraVariableCollection::GetCollectionPin() const
{
	UEdGraphPin* Pin = FindPin(FK2Node_SetComposableCameraVariableCollectionPinNames::CollectionPinName);
	check(Pin != NULL);
	return Pin;
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

UComposableCameraVariable* UK2Node_SetComposableCameraVariableCollection::GetVariableFromVariableNamePin(
	UEdGraphPin* Pin) const
{
	UComposableCameraVariable* Variable = nullptr;

	return Variable;
}

bool UK2Node_SetComposableCameraVariableCollection::IsCollectionPin(UEdGraphPin* Pin) const
{
	return Pin->PinName == FK2Node_SetComposableCameraVariableCollectionPinNames::CollectionPinName;
}

bool UK2Node_SetComposableCameraVariableCollection::IsVariableNamePin(UEdGraphPin* Pin) const
{
	return Pin->PinName.ToString().StartsWith(TEXT("Variable"));
}

bool UK2Node_SetComposableCameraVariableCollection::IsVariableValuePin(UEdGraphPin* Pin) const
{
	return Pin->PinName.ToString().StartsWith(TEXT("Value"));
}

void UK2Node_SetComposableCameraVariableCollection::GetMenuAction(FBlueprintActionDatabaseRegistrar& ActionRegistrar,
	const FAssetData& CollectionAssetData) const
{
	const FText BaseCategoryString = GetMenuCategory();

	for (TPair<FName, FAssetTagValueRef> It : CollectionAssetData.TagsAndValues)
	{
		const FName ParameterName = It.Key;

		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		NodeSpawner->DefaultMenuSignature.Category = FText::Join(
				FText::FromString(TEXT("|")), BaseCategoryString, FText::FromName(CollectionAssetData.AssetName));
		NodeSpawner->DefaultMenuSignature.MenuName = FText::Format(
				LOCTEXT("SetComposableCameraVariableCollectionActionMenuName", "Set {0}"),
				FText::FromName(ParameterName));
		NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda(
				[ParameterName, CollectionAssetData](UEdGraphNode* NewNode, bool bIsTemplateNode)
				{
					UK2Node_SetComposableCameraVariableCollection* NewSetter = CastChecked<UK2Node_SetComposableCameraVariableCollection>(NewNode);
					NewSetter->Initialize(CollectionAssetData, ParameterName.ToString());
				});

		ActionRegistrar.AddBlueprintAction(CollectionAssetData, NodeSpawner);
	}
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

void UK2Node_SetComposableCameraVariableCollection::PinTypeChanged(UEdGraphPin* Pin)
{
	bReconstructionForPinTypeChanged = true;
	OnPinTypeChanged(Pin);
}

void UK2Node_SetComposableCameraVariableCollection::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (FromPin)
	{
		const UEdGraphSchema_K2* K2Schema = CastChecked<UEdGraphSchema_K2>(GetSchema());
		UEdGraphPin* CollectionPin = GetCollectionPin();
		ECanCreateConnectionResponse ConnectResponse = K2Schema->CanCreateConnection(FromPin, CollectionPin).Response;
		if (ConnectResponse == ECanCreateConnectionResponse::CONNECT_RESPONSE_MAKE)
		{
			if (K2Schema->TryCreateConnection(FromPin, CollectionPin))
			{
				FromPin->GetOwningNode()->NodeConnectionListChanged();
				this->NodeConnectionListChanged();
				return;
			}
		}
	}
	
	Super::AutowireNewNode(FromPin);
}

void UK2Node_SetComposableCameraVariableCollection::PostPasteNode()
{
	Super::PostPasteNode();

	if (UEdGraphPin* CollectionPin = GetCollectionPin())
	{
		FString OldDefaultValue = CollectionPin->DefaultValue;
		OnPinTypeChanged(CollectionPin);
		CollectionPin->DefaultValue = OldDefaultValue;
	}
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