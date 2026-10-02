// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class AComposableCameraCameraBase;
class UComposableCameraCameraNodeBase;
class UComposableCameraTypeAsset;
class FProperty;

/** Editor-only trial values. Runtime objects are weak; only authored-template proxies are GC roots. */
class FComposableCameraLiveEditSession : public FGCObject
{
public:
	bool BindCamera(AComposableCameraCameraBase* InCamera);
	bool SelectNode(int32 NodeIndex);
	UComposableCameraCameraNodeBase* GetEditingNode() const;
	UComposableCameraCameraNodeBase* GetEditingNode(int32 NodeIndex) const;
	int32 FindEditingNode(UObject* Object, FName& PropertyName) const;
	AComposableCameraCameraBase* GetCamera() const { return Camera.Get(); }
	int32 GetSelectedNodeIndex() const { return SelectedNodeIndex; }
	TArray<int32> GetNodeIndices() const;
	FText GetNodeLabel(int32 NodeIndex) const;
	bool HasChanges() const;
	bool HasOverrides() const;
	bool IsLive() const;
	bool CanEditProperty(FName PropertyName, FString& OutReason) const;
	bool ApplyProperty(FName PropertyName, FString& OutReason);
	bool ApplyToAsset(FString& OutReason);
	bool Reset();
	void DetachRuntime();
	FString DescribeProperties() const;
	static bool IsLiveEditProperty(const FProperty* Property);

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FComposableCameraLiveEditSession"); }

private:
	struct FNodeState
	{
		int32 RuntimeIndex = INDEX_NONE;
		TWeakObjectPtr<UComposableCameraCameraNodeBase> SourceTemplate;
		TWeakObjectPtr<UComposableCameraCameraNodeBase> RuntimeNode;
		TObjectPtr<UComposableCameraCameraNodeBase> EditingNode;
		TObjectPtr<UComposableCameraCameraNodeBase> BaselineNode;
		TObjectPtr<UComposableCameraCameraNodeBase> AcceptedNode;
		TMap<FName, FString> SourceDefaults;
		TMap<FName, FString> SourceValues;
		TMap<FName, FString> AcceptedValues;
		TSet<FName> ChangedProperties;
		TSet<FName> OverrideProperties;
	};
	FNodeState* FindSelectedState();
	const FNodeState* FindSelectedState() const;
	bool WriteRuntimeProperty(FNodeState& State, FName Name, UObject* Values, bool bRestoring);
	void CaptureSourceValues(FNodeState& State);

	TWeakObjectPtr<AComposableCameraCameraBase> Camera;
	TWeakObjectPtr<UComposableCameraTypeAsset> SourceAsset;
	TArray<TWeakObjectPtr<UComposableCameraCameraNodeBase>> SourceTemplates;
	TArray<TWeakObjectPtr<UComposableCameraCameraNodeBase>> RuntimeNodes;
	TArray<FNodeState> Nodes;
	TArray<int32> StartNodeOrder;
	int32 SelectedNodeIndex = INDEX_NONE;
};
