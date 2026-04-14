// Copyright Sulley. All rights reserved.

#include "AssetTools/ComposableCameraTypeAssetEditor.h"
#include "Toolkits/ComposableCameraTypeAssetEditorToolkit.h"
#include "DataAssets/ComposableCameraTypeAsset.h"

void UComposableCameraTypeAssetEditor::Initialize(TObjectPtr<UComposableCameraTypeAsset> InTypeAsset)
{
	TypeAsset = InTypeAsset;
	Super::Initialize();
}

void UComposableCameraTypeAssetEditor::GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit)
{
	InObjectsToEdit.Add(TypeAsset.Get());
}

TSharedPtr<FBaseAssetToolkit> UComposableCameraTypeAssetEditor::CreateToolkit()
{
	TSharedPtr<FComposableCameraTypeAssetEditorToolkit> Toolkit =
		MakeShared<FComposableCameraTypeAssetEditorToolkit>(this);
	Toolkit->SetTypeAsset(TypeAsset);
	return Toolkit;
}
