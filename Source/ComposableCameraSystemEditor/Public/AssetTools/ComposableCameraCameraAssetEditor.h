// Copyright Sulley. All rights reserved.

#pragma once

#include "Tools/UAssetEditor.h"
#include "ComposableCameraCameraAssetEditor.generated.h"

class UComposableCameraCameraAsset;
class FBaseAssetToolkit;

UCLASS(Transient)
class UComposableCameraCameraAssetEditor 
	: public UAssetEditor
{
	GENERATED_BODY()

public:
	void Initialize(TObjectPtr<UComposableCameraCameraAsset> InCameraAsset);
	UComposableCameraCameraAsset* GetCameraAsset() const { return CameraAsset; }

	virtual void GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit) override;
	virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;

private:
	UPROPERTY()
	TObjectPtr<UComposableCameraCameraAsset> CameraAsset;
};
