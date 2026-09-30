// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_AddCameraAction.generated.h"

class UComposableCameraActionTypeAsset;
class FCompilerResultsLog;
class FKismetCompilerContext;
struct FPropertyChangedEvent;

/** Adds an Action asset. Linked pins override fields on a fresh template copy;
 * unlinked pins retain the values authored on the asset. */
UCLASS(ClassGroup = ComposableCameraSystem)
class COMPOSABLECAMERASYSTEMUNCOOKEDONLY_API UK2Node_AddCameraAction : public UK2Node
{
	GENERATED_BODY()

public:
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	virtual void PostPlacedNewNode() override;
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
	virtual bool IsNodePure() const override { return false; }
	virtual bool NodeCausesStructuralBlueprintChange() const override { return true; }

private:
	void CreateDynamicParameterPins();
	void RefreshAsset();
	void SubscribeToAssetChanges();
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);

	UPROPERTY()
	TObjectPtr<UComposableCameraActionTypeAsset> CachedActionAsset;

	UPROPERTY()
	TArray<FName> DynamicParameterPinNames;

	FDelegateHandle PropertyChangedHandle;
	bool bIsReconstructing = false;

	static const FName PN_PlayerCameraManager;
	static const FName PN_ActionAsset;
	static const FName PN_OnlyForCurrentCamera;
	static const FName PN_ReturnValue;
};
