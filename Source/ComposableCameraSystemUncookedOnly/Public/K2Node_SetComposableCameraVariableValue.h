// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_SetComposableCameraVariableValue.generated.h"

class UComposableCameraVariable;

/**
 * K2 Node to set a composable camera's variable runtime value.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API UK2Node_SetComposableCameraVariableValue
	: public UK2Node
{
	GENERATED_BODY()

public:
	// UEdGraphNode interface.
	virtual void AllocateDefaultPins() override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins);
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	
	// UK2Node interface.
	virtual FText GetMenuCategory() const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;

protected:
	UClass* GetInVariableClass(UEdGraphPin* VariablePin);
	
private:
	UComposableCameraVariable* GetVariableFromPin(UEdGraphPin* Pin) const;
	FProperty* FindRuntimeValueProperty(UClass* Class);
	void CreateValuePin(UClass* Class);
	bool ShouldCreateNewValuePin(UEdGraphPin* VariablePin, UEdGraphPin* ValuePin);
	void OnPinChanged();
};
