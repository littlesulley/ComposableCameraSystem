// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

class UComposableCameraCameraAsset;

/**
 * Implementation for ComposableCameraCameraAssetEditorToolkit.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API FComposableCameraCameraAssetEditorToolkitBase
	: public FGCObject
	, public FNotifyHook
	, public TSharedFromThis<FComposableCameraCameraAssetEditorToolkitBase>
{
	public:

	FComposableCameraCameraAssetEditorToolkitBase(FName InLayoutName);
	~FComposableCameraCameraAssetEditorToolkitBase();

	UComposableCameraCameraAsset* GetCameraAsset() const { return CameraAsset; }
	void SetCameraAsset(UComposableCameraCameraAsset* InCameraAsset) { CameraAsset = InCameraAsset; }

	TSharedPtr<IDetailsView> GetDetailsView() const { return DetailsView; }

	void RegisterTabSpawners(TSharedRef<FTabManager> InTabManager, TSharedPtr<FWorkspaceItem> InAssetEditorTabsCategory);
	void UnregisterTabSpawners(TSharedRef<FTabManager> InTabManager);
	void CreateWidgets();
	void BuildToolbarMenu(UToolMenu* ToolbarMenu);
	void BindCommands(TSharedRef<FUICommandList> CommandList);

protected:

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;

	// FNotifyHook interface
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;

private:
	/** The asset being edited */
	TObjectPtr<UComposableCameraCameraAsset> CameraAsset;

	/** The details view */
	TSharedPtr<IDetailsView> DetailsView;

};
