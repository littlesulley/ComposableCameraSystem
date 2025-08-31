// Copyright Sulley. All rights reserved.

#include "AssetTools/ComposableCameraCameraAssetEditor.h"
#include "Toolkits/ComposableCameraCameraAssetEditorToolkit.h"
#include "Core/ComposableCameraCameraAsset.h"

void UComposableCameraCameraAssetEditor::Initialize(TObjectPtr<UComposableCameraCameraAsset> InCameraAsset)
{
	CameraAsset = InCameraAsset;
	Super::Initialize();
}

void UComposableCameraCameraAssetEditor::GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit)
{
	InObjectsToEdit.Add(CameraAsset.Get());
}

TSharedPtr<FBaseAssetToolkit> UComposableCameraCameraAssetEditor::CreateToolkit()
{
	TSharedPtr<FComposableCameraCameraAssetEditorToolkit> Toolkit = MakeShared<FComposableCameraCameraAssetEditorToolkit>(this);
	Toolkit->SetCameraAsset(CameraAsset);
	return Toolkit;
}
