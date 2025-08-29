// Copyright Sulley. All rights reserved.

#include "AssetTools/ComposableCameraCameraAssetEditor.h"
#include "AssetTools/ComposableCameraCameraAssetEditorToolkit.h"
#include "Core/ComposableCameraCameraAsset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ComposableCameraCameraAsset)

void UComposableCameraCameraAssetEditor::Initialize(TObjectPtr<UComposableCameraCameraAsset> InCameraAsset)
{
	CameraAsset = InCameraAsset;
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
