// Copyright Sulley. All rights reserved.

#include "AssetTools/ComposableCameraVariableCollectionEditor.h"
#include "Variables/ComposableCameraVariableCollection.h"

void UComposableCameraVariableCollectionEditor::Initialize(TObjectPtr<UComposableCameraVariableCollection> InCollection)
{
	Collection = InCollection;
	Super::Initialize();
}

void UComposableCameraVariableCollectionEditor::GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit)
{
	InObjectsToEdit.Add(Collection.Get());
}

TSharedPtr<FBaseAssetToolkit> UComposableCameraVariableCollectionEditor::CreateToolkit()
{
	return MakeShared<FBaseAssetToolkit>(this);
}
