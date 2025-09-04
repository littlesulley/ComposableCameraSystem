// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/UAssetEditor.h"
#include "Tools/BaseAssetToolkit.h"
#include "ComposableCameraVariableCollectionEditor.generated.h"

class UComposableCameraVariableCollection;

UCLASS(Transient)
class COMPOSABLECAMERASYSTEMEDITOR_API UComposableCameraVariableCollectionEditor
	: public UAssetEditor
{
	GENERATED_BODY()

public:
	void Initialize(TObjectPtr<UComposableCameraVariableCollection> InCollection);
	UComposableCameraVariableCollection* GetCollection() const { return Collection; }

	virtual void GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit) override;
	virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;

private:
	UPROPERTY()
	TObjectPtr<UComposableCameraVariableCollection> Collection;
};
