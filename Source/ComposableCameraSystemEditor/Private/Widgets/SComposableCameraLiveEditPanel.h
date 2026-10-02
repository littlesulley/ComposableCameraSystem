// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Editors/ComposableCameraLiveEditSession.h"
#include "GraphEditor.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class SBox;
class UComposableCameraNodeGraph;
struct FPropertyAndParent;

class SComposableCameraLiveEditPanel : public SCompoundWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SComposableCameraLiveEditPanel) {} SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);
	void SelectCamera(TWeakObjectPtr<AComposableCameraCameraBase> Camera);
	virtual ~SComposableCameraLiveEditPanel() override;
	virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SComposableCameraLiveEditPanel"); }

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FComposableCameraLiveEditChainPanelTest;
#endif
	TSharedRef<SWidget> BuildCameraMenu();
	void RebuildNodePanels();
	void OnNodeSelectionChanged(const FGraphPanelSelectionSet& Selection);
	void ShowNodeParameters(int32 PanelIndex);
	void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
	void OnPIEEnding(bool bSimulating);
	void RefreshDescriptions();
	static const FProperty* FindTopProperty(const FPropertyAndParent& Property);

	FComposableCameraLiveEditSession Session;
	struct FNodePanel
	{
		int32 Index = INDEX_NONE;
		FText Label;
		TSharedPtr<IDetailsView> Details;
		TSet<FName> EditableProperties;
	};
	TArray<TSharedPtr<FNodePanel>> NodePanels;
	TObjectPtr<UComposableCameraNodeGraph> NodeChainGraph;
	TSharedPtr<SGraphEditor> NodeChainEditor;
	TSharedPtr<SBox> ParameterHost;
	int32 SelectedPanelIndex = INDEX_NONE;
	bool bUpdatingNodeSelection = false;
	FDelegateHandle PropertyChangedHandle;
	FDelegateHandle PIEEndingHandle;
	FText CameraLabel;
	FText PropertyDescriptions;
	FText RuntimeParameterLabel;
	FText Status;
	double NextRefreshTime = 0;
};
