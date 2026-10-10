// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Editors/ComposableCameraShotTemplates.h"

class FComposableCameraShotAuthoringSession;
class ULevelSequence;
class UComposableCameraShotAsset;
class SVerticalBox;
class SBox;
class FNotifyHook;
class SShotEditorParameterPanel;

class SShotEditorAuthoringPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShotEditorAuthoringPanel) : _NotifyHook(nullptr) {}
		SLATE_ARGUMENT(TSharedPtr<FComposableCameraShotAuthoringSession>, Session)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, AdvancedContent)
		SLATE_ARGUMENT(FNotifyHook*, NotifyHook)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	TSharedRef<SWidget> BuildNavigation();
	void Refresh() { bRefreshPending = true; }
	virtual void Tick(const FGeometry& Geometry, double Time, float DeltaTime) override;
#if WITH_DEV_AUTOMATION_TESTS
	void SelectLookAtForTesting(bool bSelected) { ActivePage = EPage::Edit; ActiveEditSection = bSelected ? EEditSection::LookAt : EEditSection::Follow; UpdateGuideSelection(); }
	void SelectCreateForTesting() { ActivePage = EPage::Create; UpdateGuideSelection(); }
	void SelectSequenceForTesting() { ActivePage = EPage::Sequence; UpdateGuideSelection(); }
	void SelectPresetsForTesting() { ActivePage = EPage::Presets; UpdateGuideSelection(); }
	void SelectSubjectsForTesting() { ActivePage = EPage::Edit; ActiveEditSection = EEditSection::Subjects; UpdateGuideSelection(); }
	TSharedPtr<SWidget> GetEditSubjectsForTesting() const;
	TSharedPtr<SWidget> GetCreateSubjectsForTesting() const;
	TSharedPtr<SWidget> GetSubjectCardForTesting(int32 Index, bool bCreate = false) const
	{
		const auto& Cards = bCreate ? CreateSubjectCards : EditSubjectCards;
		return Cards.IsValidIndex(Index) ? Cards[Index] : nullptr;
	}
	TSharedPtr<SShotEditorParameterPanel> GetSubjectPanelForTesting(int32 Index, bool bCreate = false) const
	{
		const auto& Panels = bCreate ? CreateSubjectPanels : EditSubjectPanels;
		return Panels.IsValidIndex(Index) ? Panels[Index] : nullptr;
	}
	void GetEditParameterPanelsForTesting(TArray<TSharedPtr<SShotEditorParameterPanel>>& Panels) const { Panels = { FollowPanel, LookAtPanel, LensFocusPanel, MotionPanel }; }
	uint32 GetPageBuildCountForTesting() const { return PageBuildCount; }
	const SWidget* GetEditPageForTesting() const { return EditPageWidget.Get(); }
	TSharedPtr<SShotEditorParameterPanel> GetFollowPanelForTesting() const { return FollowPanel; }
#endif
private:
	enum class EPage : uint8 { Create, Edit, Sequence, Presets, Advanced };
	enum class EEditSection : uint8 { Follow, LookAt, Lens, Motion, Subjects };
	TSharedPtr<FComposableCameraShotAuthoringSession> Session;
	TSharedPtr<SWidget> AdvancedContent;
	TSharedPtr<SVerticalBox> Body;
	TSharedPtr<SBox> CreateSubjectsBox;
	TSharedPtr<SBox> EditSubjectsBox;
	TSharedPtr<SVerticalBox> CreateSubjectRows;
	TSharedPtr<SVerticalBox> EditSubjectRows;
	TSharedPtr<SWidget> EditPageWidget;
	TSharedPtr<SShotEditorParameterPanel> FollowPanel;
	TSharedPtr<SShotEditorParameterPanel> LookAtPanel;
	TSharedPtr<SShotEditorParameterPanel> LensFocusPanel;
	TSharedPtr<SShotEditorParameterPanel> MotionPanel;
	TArray<TSharedPtr<SShotEditorParameterPanel>> CreateSubjectPanels;
	TArray<TSharedPtr<SShotEditorParameterPanel>> EditSubjectPanels;
	TArray<TSharedPtr<SWidget>> CreateSubjectCards;
	TArray<TSharedPtr<SWidget>> EditSubjectCards;
	// View state belongs to the page and subject role/index, never to serialized Shot data.
	TMap<FString, bool> ExpandedSubjectGroups;
	FNotifyHook* NotifyHook = nullptr;
	TArray<TSharedPtr<EComposableShotTemplate>> Templates;
	TArray<TWeakObjectPtr<AActor>> DisplayedActors;
	TSoftObjectPtr<ULevelSequence> Sequence;
	TSoftObjectPtr<UComposableCameraShotAsset> Preset;
	EComposableShotTemplate SelectedTemplate = EComposableShotTemplate::Medium;
	EPage ActivePage = EPage::Edit;
	EEditSection ActiveEditSection = EEditSection::Follow;
	FText Message;
	float Duration = 5.f;
	bool bSequenceOwnedCamera = true;
	bool bSequenceOverride = false;
	bool bRefreshPending = true;
	bool bPagesBuilt = false;
#if WITH_DEV_AUTOMATION_TESTS
	uint32 PageBuildCount = 0;
#endif
	void Rebuild();
	void UpdateGuideSelection();
	TSharedRef<SWidget> Section(const FText& Label, TSharedRef<SWidget> Content);
	TSharedRef<SWidget> CreatePage();
	TSharedRef<SWidget> EditPage();
	TSharedRef<SWidget> PresetActions();
	TSharedRef<SWidget> Subjects(bool bCreatePage = false);
	void SyncSubjects(bool bCreatePage);
	TSharedRef<SWidget> SubjectCard(int32 Index, bool bCreatePage);
	TSharedRef<SWidget> ComponentPicker(int32 Index);
	TSharedRef<SWidget> BonePicker(int32 Index);
	TSharedRef<SWidget> SequenceActions();
	FReply UseSelection();
	FReply ApplyTemplate();
	FReply CreateSequenceShot(bool bDialogue);
	FReply DuplicateShot();
	FReply JumpToShot();
	void SetMessage(const FString& Reason);
};
