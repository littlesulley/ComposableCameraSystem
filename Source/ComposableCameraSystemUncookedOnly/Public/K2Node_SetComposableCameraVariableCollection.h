// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_AddPinInterface.h"
#include "Variables/ComposableCameraVariable.h"
#include "K2Node_SetComposableCameraVariableCollection.generated.h"

class UComposableCameraVariable;
class UComposableCameraVariableCollection;

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
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void PinTypeChanged(UEdGraphPin* Pin) override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual void PostPasteNode() override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	//~ End UEdGraphNode Interface

	//~ Begin UK2Node Interface
	// virtual ERedirectType DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex) const override;
	// virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	// virtual void PostReconstructNode() override;
	// virtual void NotifyPinConnectionListChanged(UEdGraphPin* Pin) override;
	// virtual class FNodeHandlingFunctor* CreateNodeHandler(class FKismetCompilerContext& CompilerContext) const override;
	// virtual bool IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const override;
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

private:
	UPROPERTY()
	UComposableCameraVariableCollection* Collection;
	
	UPROPERTY()
	int32 NumPins = 0;

	UPROPERTY()
	TMap<FName, FName> PinNameToVariableName; 
	
	bool bReconstructionForPinTypeChanged = false;
	bool bReconstructNode = false;

private:
	void OnPinTypeChanged(UEdGraphPin* Pin);
	void OnCollectionPinChanged(UEdGraphPin* Pin);
	void OnVariableNamePinChanged(UEdGraphPin* Pin);
	void OnVariableValuePinChanged(UEdGraphPin* Pin);
	UEdGraphPin* GetCollectionPin() const;
	UEdGraphPin* FindCorrespondingVariableValuePinFromVariableNamePin(UEdGraphPin* VariableNamePin) const;
	UComposableCameraVariable* GetVariableFromVariableNamePin(UEdGraphPin* Pin) const;
	bool IsCollectionPin(UEdGraphPin* Pin) const;
	bool IsVariableNamePin(UEdGraphPin* Pin) const;
	bool IsVariableValuePin(UEdGraphPin* Pin) const;

private:
	void Initialize(const FAssetData& UnloadedCollection);
	void GetMenuAction(FBlueprintActionDatabaseRegistrar& ActionRegistrar, const FAssetData& CollectionAssetData) const;
	FEdGraphPinType MakePinTypeFromVariableType(EComposableCameraVariableType VariableType, const UObject* InPinSubCategoryObject = nullptr);
};
