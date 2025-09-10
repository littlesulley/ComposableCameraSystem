// Copyright Sulley. All rights reserved.

#include "K2Node_SetComposableCameraVariableValue.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"
#include "Variables/ComposableCameraVariable.h"

#define LOCTEXT_NAMESPACE "K2Node_SetComposableCameraVariableValue"

struct FK2Node_SetComposableCameraVariableValuePinNames
{
	static inline const FName VariablePinName = "Variable";
	static inline const FName ValuePinName = "Value";
};

void UK2Node_SetComposableCameraVariableValue::BeginDestroy()
{
	Super::BeginDestroy();
}

void UK2Node_SetComposableCameraVariableValue::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	
	UEdGraphPin* InVariablePin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UComposableCameraVariable::StaticClass(), FK2Node_SetComposableCameraVariableValuePinNames::VariablePinName);
	K2Schema->ConstructBasicPinTooltip(*InVariablePin, LOCTEXT("InVariablePinDescription", "The variable you want to set its value to."), InVariablePin->PinToolTip);

	Super::AllocateDefaultPins();
}

void UK2Node_SetComposableCameraVariableValue::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);

	if (UComposableCameraVariable* CameraVariable = GetVariableFromPin(FindPinChecked(FK2Node_SetComposableCameraVariableValuePinNames::VariablePinName, EGPD_Input)); !CameraVariable)
	{
		const FText Messagetext = LOCTEXT("MissingVariable", "Missing variable inside node @@");
		MessageLog.Warning(*Messagetext.ToString(), this);
	}
}

void UK2Node_SetComposableCameraVariableValue::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);
	
	if (Pin && Pin->PinName == FK2Node_SetComposableCameraVariableValuePinNames::VariablePinName)
	{
		OnPinChanged();
	}
}

void UK2Node_SetComposableCameraVariableValue::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	
	if (Pin && Pin->PinName == FK2Node_SetComposableCameraVariableValuePinNames::VariablePinName)
	{
		OnPinChanged();
	}
}

FText UK2Node_SetComposableCameraVariableValue::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("K2Node_SetComposableCameraVariableValue_NodeTitle", "Set Composable Camera Variable");
}

FLinearColor UK2Node_SetComposableCameraVariableValue::GetNodeTitleColor() const
{
	return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("420039")));
}

FText UK2Node_SetComposableCameraVariableValue::GetTooltipText() const
{
	return LOCTEXT("K2Node_SetComposableCameraVariableValue_Tooltip", "Set the value of a composable camera variable.");
}

FSlateIcon UK2Node_SetComposableCameraVariableValue::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor(.823f, .823f, .823f);
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Kismet.AllClasses.VariableIcon");
}

void UK2Node_SetComposableCameraVariableValue::ExpandNode(FKismetCompilerContext& CompilerContext,
	UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UEdGraphPin* ValuePin = FindPinChecked(FK2Node_SetComposableCameraVariableValuePinNames::ValuePinName, EGPD_Input);
	UEdGraphPin* VariablePin = FindPinChecked(FK2Node_SetComposableCameraVariableValuePinNames::VariablePinName, EGPD_Input);
	UClass* VariableClass = GetInVariableClass(VariablePin);
	
	if (!VariableClass)
	{
		CompilerContext.MessageLog.Error(*(LOCTEXT("SetComposableCameraVariableValue_ExpandNode_Error_InValidVariablePin", "Node must have a valid input variable.").ToString()));
		BreakAllNodeLinks();
		return;
	}

	if (!ValuePin)
	{
		CompilerContext.MessageLog.Error(*(LOCTEXT("SetComposableCameraVariableValue_ExpandNode_Error_InValidValuePin", "Node must have a valid input value.").ToString()));
		BreakAllNodeLinks();
		return;
	}

	UK2Node_CallFunction* CallFunction = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallFunction->FunctionReference.SetExternalMember(TEXT("SetComposableCameraVariableRuntimeValue"), UComposableCameraBlueprintLibrary::StaticClass());
	CallFunction->AllocateDefaultPins();

	UEdGraphPin* FunctionExecPin = CallFunction->GetExecPin();
	UEdGraphPin* FunctionThenPin = CallFunction->GetThenPin();
	UEdGraphPin* FunctionVariablePin = CallFunction->FindPinChecked(TEXT("Variable"));
	UEdGraphPin* FunctionValuePin = CallFunction->FindPinChecked(TEXT("NewRuntimeValue"));
	FunctionValuePin->PinType = VariablePin->PinType;
	
	UEdGraphPin* ExecPin = GetExecPin();
	UEdGraphPin* ThenPin = GetThenPin();
	
	CompilerContext.MovePinLinksToIntermediate(*ExecPin, *FunctionExecPin);
	CompilerContext.MovePinLinksToIntermediate(*ThenPin, *FunctionThenPin);

	if (VariablePin->LinkedTo.Num() == 1 || VariablePin->DefaultObject)
	{
		CompilerContext.MovePinLinksToIntermediate(*VariablePin, *FunctionVariablePin);
		CompilerContext.MovePinLinksToIntermediate(*ValuePin, *FunctionValuePin);
	}
	else
	{
		CompilerContext.MessageLog.Error(
			*(LOCTEXT("SetComposableCameraVariableValue_ExpandNode_Error_VariablePin", "Invalid variable.").ToString()));
		BreakAllNodeLinks();
		return;
	}

	BreakAllNodeLinks();
}

FText UK2Node_SetComposableCameraVariableValue::GetMenuCategory() const
{
	return LOCTEXT("K2Node_SetComposableCameraVariableValue_MenuCategory", "ComposableCameraSystem|Variable");
}

void UK2Node_SetComposableCameraVariableValue::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();

	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

UClass* UK2Node_SetComposableCameraVariableValue::GetInVariableClass(UEdGraphPin* VariablePin)
{
	UClass* VariableClass = nullptr;
	
	// Use a default object not a linked pin.
	if (VariablePin->DefaultObject && VariablePin->LinkedTo.Num() == 0)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(VariablePin->DefaultObject))
		{
			VariableClass = Cast<UClass>(Blueprint->GeneratedClass);
		}
		else
		{
			VariableClass = VariablePin->DefaultObject->GetClass();
		}
	}
	else if (VariablePin->LinkedTo.Num())
	{
		UEdGraphPin* VariableSourcePin = VariablePin->LinkedTo[0];
		VariableClass = VariableSourcePin ? Cast<UClass>(VariableSourcePin->PinType.PinSubCategoryObject.Get()) : nullptr;
	}

	return VariableClass;
}

UComposableCameraVariable* UK2Node_SetComposableCameraVariableValue::GetVariableFromPin(UEdGraphPin* Pin) const
{
	UComposableCameraVariable* CameraVariable = nullptr;

	if (Pin)
	{
		if (Pin->DefaultObject && Pin->LinkedTo.Num() == 0)
		{
			CameraVariable = Cast<UComposableCameraVariable>(Pin->DefaultObject);
		}
		else if (Pin->LinkedTo.Num())
		{
			UEdGraphPin* VariableSourcePin = Pin->LinkedTo[0];
			CameraVariable = Cast<UComposableCameraVariable>(VariableSourcePin->DefaultObject);
		}
	}

	return CameraVariable;
}

FProperty* UK2Node_SetComposableCameraVariableValue::FindRuntimeValueProperty(UClass* Class)
{
	FProperty* Property = nullptr;
	
	for (TFieldIterator<FProperty> PropertyIt(Class, EFieldIterationFlags::Default); PropertyIt; ++PropertyIt)
	{
		FProperty* ThisProperty = *PropertyIt;

		if (ThisProperty->GetFName().ToString().Equals("RuntimeValue"))
		{
			Property = ThisProperty;
			break;
		}
	}

	return Property;
}

void UK2Node_SetComposableCameraVariableValue::CreateValuePin(UClass* Class)
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	if (FProperty* Property = FindRuntimeValueProperty(Class))
	{
		FEdGraphPinType PinType;
		K2Schema->ConvertPropertyToPinType(Property, PinType);
		CreatePin(EGPD_Input, PinType, TEXT("Value"));
	}
}

bool UK2Node_SetComposableCameraVariableValue::ShouldCreateNewValuePin(UEdGraphPin* VariablePin, UEdGraphPin* ValuePin)
{
	if (!ValuePin || !GetInVariableClass(VariablePin))
	{
		return true;
	}
	
	if (FProperty* Property = FindRuntimeValueProperty(GetInVariableClass(VariablePin)))
	{
		FEdGraphPinType PinType;
		GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, PinType);
		return PinType != ValuePin->PinType;
	}
	else
	{
		return true;
	}
}

void UK2Node_SetComposableCameraVariableValue::OnPinChanged()
{
	UEdGraphPin* VariablePin = FindPin(FK2Node_SetComposableCameraVariableValuePinNames::VariablePinName, EGPD_Input);
	UEdGraphPin* ValuePin = FindPin(FK2Node_SetComposableCameraVariableValuePinNames::ValuePinName, EGPD_Input);

	if (ShouldCreateNewValuePin(VariablePin, ValuePin))
	{
		if (ValuePin)
		{
			ValuePin->BreakAllPinLinks();
		}

		Modify();
		Pins.Remove(ValuePin);

		if (UClass* VariableClass = GetInVariableClass(VariablePin))
		{
			CreateValuePin(VariableClass);
		}

		GetGraph()->NotifyGraphChanged();
		FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
	}
}

#undef  LOCTEXT_NAMESPACE