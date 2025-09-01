// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BaseAssetToolkit.h"
#include "ComposableCameraCameraAssetEditorToolkit.generated.h"

class FComposableCameraCameraAssetEditorToolkitBase;
class UComposableCameraCameraAsset;
class UAssetEditor;

class FComposableCameraCameraAssetEditorToolkit
	: public FBaseAssetToolkit
{
public:
	FComposableCameraCameraAssetEditorToolkit(UAssetEditor* InAssetEditor);
	~FComposableCameraCameraAssetEditorToolkit();

	void SetCameraAsset(UComposableCameraCameraAsset* InCameraAsset);

protected:
	// FBaseAssetToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void CreateWidgets() override;
	virtual void RegisterToolbar() override;
	virtual void InitToolMenuContext(FToolMenuContext& MenuContext) override;
	virtual void PostInitAssetEditor() override;
	virtual void PostRegenerateMenusAndToolbars() override;

	// IToolkit interface
	virtual FText GetBaseToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;

private:
	/* Base implementation */
	TSharedPtr<FComposableCameraCameraAssetEditorToolkitBase> Impl;
};

UCLASS(Experimental)
class UComposableCameraCameraAssetEditorMenuContext : public UObject
{
	GENERATED_BODY()

public:
	TWeakPtr<FComposableCameraCameraAssetEditorToolkit> Toolkit;
};