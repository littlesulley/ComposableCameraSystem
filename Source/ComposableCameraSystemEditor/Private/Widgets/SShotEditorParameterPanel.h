// Copyright 2026 Sulley. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FComposableCameraShotAuthoringSession;
class FNotifyHook;
class IDetailTreeNode;
class IPropertyRowGenerator;
class IPropertyHandle;
class SVerticalBox;

enum class EShotEditorParameterSection : uint8 { Follow, LookAt, LensFocus, Motion, Subject };

/** Complete native Shot fields in retained parameter-page groups. */
class SShotEditorParameterPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShotEditorParameterPanel) : _NotifyHook(nullptr), _Section(EShotEditorParameterSection::Follow), _SubjectIndex(INDEX_NONE) {}
		SLATE_ARGUMENT(TSharedPtr<FComposableCameraShotAuthoringSession>, Session)
		SLATE_ARGUMENT(FNotifyHook*, NotifyHook)
		SLATE_ARGUMENT(EShotEditorParameterSection, Section)
		SLATE_ARGUMENT(int32, SubjectIndex)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	/** Rebind handles; collection updates can replace their controls atomically in the same Slate tick. */
	void RefreshSource(bool bImmediate = false);
	virtual ~SShotEditorParameterPanel() override;
	virtual void Tick(const FGeometry& Geometry, double Time, float DeltaTime) override;
#if WITH_DEV_AUTOMATION_TESTS
	void RefreshForTesting();
	void GetVisiblePropertyNamesForTesting(TSet<FName>& Names) const;
	void GetRenderedPropertyPathsForTesting(TSet<FString>& Paths) const;
	bool SetValueForTesting(const FString& Path, bool Value);
	bool SetValueForTesting(const FString& Path, uint8 Value);
	bool SetSubjectFloatForTesting(FName Name, float Value);
	bool SetFloatForTesting(FName Name, float Value);
	uint32 GetRebuildCountForTesting() const { return RebuildCount; }
#endif
private:
	TSharedPtr<FComposableCameraShotAuthoringSession> Session;
	TSharedPtr<IPropertyRowGenerator> Generator;
	TSharedPtr<SVerticalBox> Body;
	EShotEditorParameterSection Section = EShotEditorParameterSection::Follow;
	int32 SubjectIndex = INDEX_NONE;
	bool bRebuildPending = true;
	void RequestRebuild();
	void Rebuild();
	TSharedPtr<IDetailTreeNode> FindRoot(FName Name) const;
	TSharedPtr<IDetailTreeNode> FindSubject() const;
	void AddLayerRows(FName RootName, TSharedRef<SVerticalBox> Rows);
	void BuildSubject();
	void BuildMotion();
	TSharedRef<SWidget> SubjectGroup(const FText& Label, const FString& Path, TSharedRef<SWidget> Content, bool bExpanded);
	TSharedRef<SWidget> NodeWidget(TSharedRef<IDetailTreeNode> Node, const FString& Path);
	TSharedRef<SWidget> Group(const FText& Label, TSharedRef<SWidget> Content);
	TMap<FString, bool> ExpandedNodes;
#if WITH_DEV_AUTOMATION_TESTS
	uint32 RebuildCount = 0;
	TMap<FString, TAttribute<EVisibility>> RenderedPropertyVisibility;
	TMap<FString, TSharedPtr<IPropertyHandle>> RenderedPropertyHandles;
#endif
};
