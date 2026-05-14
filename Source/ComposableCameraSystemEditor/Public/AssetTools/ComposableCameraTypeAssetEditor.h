// Copyright Sulley. All rights reserved.

#pragma once

#include "Tools/UAssetEditor.h"
#include "ComposableCameraTypeAssetEditor.generated.h"

class UComposableCameraTypeAsset;
class FBaseAssetToolkit;

/**
 * UAssetEditor bridge for the Camera Type Asset editor.
 * Created by the editor module and manages the lifecycle of the toolkit.
 */
UCLASS(Transient, Experimental)
class UComposableCameraTypeAssetEditor: public UAssetEditor
{
	GENERATED_BODY()

public:
	void Initialize(TObjectPtr<UComposableCameraTypeAsset> InTypeAsset);
	UComposableCameraTypeAsset* GetTypeAsset() const { return TypeAsset; }

	virtual void GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit) override;
	virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;

private:
	UPROPERTY()
	TObjectPtr<UComposableCameraTypeAsset> TypeAsset;
};
