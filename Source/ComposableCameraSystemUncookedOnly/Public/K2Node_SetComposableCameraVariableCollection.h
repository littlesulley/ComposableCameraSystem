// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_AddPinInterface.h"
#include "Variables/ComposableCameraVariable.h"
#include "K2Node_SetComposableCameraVariableCollection.generated.h"

class UEdGraph;
class UEdGraphPin;
class UK2Node;
class UComposableCameraVariable;
class UComposableCameraVariableCollection;
class UK2Node_CallFunction;

/**
 * K2 Node to set the runtime values of a composable camera variable collection.
 */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API UK2Node_SetComposableCameraVariableCollection
	: public UK2Node, public IK2Node_AddPinInterface
{
	GENERATED_BODY()

public:
	//~ Begin UEdGraphNode Interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void NodeConnectionListChanged() override;
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	//~ End UEdGraphNode Interface

	//~ Begin UK2Node Interface
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual int32 GetNodeRefreshPriority() const override { return EBaseNodeRefreshPriority::Low_UsesDependentWildcard; }
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	//~ End UK2Node Interface

	//~ IK2Node_AddPinInterface
	virtual bool CanAddPin() const override { return true; }
	virtual void AddInputPin() override;
	virtual void RemoveInputPin(UEdGraphPin* Pin) override;
	//~ End IK2Node_AddPinInterface

	void RemoveVariablePin(const UEdGraphPin* Pin);

public:
	UComposableCameraVariableCollection* GetVariableCollection() const { return Collection; }
	TArray<FString> GetVariableNames() const;

private:
	UPROPERTY()
	UComposableCameraVariableCollection* Collection;

	UPROPERTY()
	TMap<FString, FEdGraphPinType> VariableNameToPinType;

	UPROPERTY()
	TSet<int32> UnusedPinIndex;

	UPROPERTY()
	TMap<FName, FName> VariablePinToValuePin;

	UPROPERTY()
	TMap<FName, FString> VariablePinNameToVariableName;
	
	bool bReconstructNode = false;

private:
	void OnVariableNamePinChanged(UEdGraphPin* Pin);
	void OnVariableValuePinChanged(UEdGraphPin* Pin);
	UEdGraphPin* FindCorrespondingVariableValuePinFromVariableNamePin(UEdGraphPin* VariableNamePin) const;
	void CreateCorrespondingVariableValuePin(UEdGraphPin* Pin);
	UComposableCameraVariable* FindVariableFromVariablePinName(FName VariablePinName) const;
	bool IsVariableNamePin(const UEdGraphPin* Pin) const;
	bool IsVariableValuePin(const UEdGraphPin* Pin) const;

private:
	TArray<int32> GetUsedPinIndex();
	int32 GetVariablePinIndex(const UEdGraphPin* Pin) const;
	bool ValidateCollectionBeforeExpandNode(FKismetCompilerContext& CompilerContext) const;
	UK2Node_CallFunction* MakeLiteralValueForPin(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UK2Node* SourceNode, UEdGraphPin* SourceValuePin);
	static UK2Node_CallFunction* CreateMakeLiteralNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UK2Node* SourceNode, UClass* FunctionLibraryClass, const TCHAR* FunctionName, UEdGraphPin* SourceValuePin);
	
	void Initialize(const FAssetData& UnloadedCollection);
	void GetMenuAction(FBlueprintActionDatabaseRegistrar& ActionRegistrar, const FAssetData& CollectionAssetData) const;
	FEdGraphPinType MakePinTypeFromVariableType(EComposableCameraVariableType VariableType, const UObject* InPinSubCategoryObject = nullptr);
};

