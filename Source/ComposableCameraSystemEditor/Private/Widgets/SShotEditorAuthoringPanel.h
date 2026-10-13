// Copyright 2026 Sulley. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/GCObject.h"
#include "DataAssets/ComposableCameraShot.h"
#include "Editors/ComposableCameraShotEditorDiagnostics.h"

class FComposableCameraShotAuthoringSession;
class ULevelSequence;
class UMovieSceneSection;
class ISequencer;
class UComposableCameraShotAsset;
class UComposableCameraShotTemplateLibrary;
class SVerticalBox;
class SBox;
class SScrollBox;
class SExpandableArea;
class SComboButton;
class FMenuBuilder;
class FNotifyHook;
class FTransactionObjectEvent;
struct FPropertyChangedEvent;
class SShotEditorParameterPanel;

class SShotEditorAuthoringPanel : public SCompoundWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SShotEditorAuthoringPanel) : _PersistViewState(false), _NotifyHook(nullptr) {}
		SLATE_ARGUMENT(bool, PersistViewState)
		SLATE_ARGUMENT(FString, ViewStateIni)
		SLATE_ARGUMENT(TSharedPtr<FComposableCameraShotAuthoringSession>, Session)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, AdvancedContent)
		SLATE_ARGUMENT(FNotifyHook*, NotifyHook)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual ~SShotEditorAuthoringPanel() override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SShotEditorAuthoringPanel"); }
	TSharedRef<SWidget> BuildNavigation();
	void ShowSubjects();
	bool RevealDiagnostic(const FShotEditorDiagnostic& Diagnostic);
	void Refresh() { bRefreshPending = true; }
	virtual void Tick(const FGeometry& Geometry, double Time, float DeltaTime) override;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FCameraInstanceClipActionsTest;
	friend class FShotEditorFollowGuideSelectionTest;
	friend class FShotEditorTemplateLibraryTest;
	void SelectLookAtForTesting(bool bSelected) { ActivePage = EPage::Edit; ActiveEditSection = bSelected ? EEditSection::LookAt : EEditSection::Follow; UpdateGuideSelection(); }
	void SelectCreateForTesting() { ActivePage = EPage::Create; UpdateGuideSelection(); }
	void SelectSequenceForTesting() { ActivePage = EPage::Sequence; UpdateGuideSelection(); }
	void SelectPresetsForTesting() { ActivePage = EPage::Presets; UpdateGuideSelection(); }
	void SelectSubjectsForTesting() { ActivePage = EPage::Edit; ActiveEditSection = EEditSection::Subjects; UpdateGuideSelection(); }
	TSharedPtr<SWidget> GetEditSubjectsForTesting() const;
	TSharedPtr<SWidget> GetCreatePageForTesting() const { return CreatePageWidget; }
	TSharedPtr<SWidget> GetSubjectCardForTesting(int32 Index) const
	{
		const auto& Cards = EditSubjectCards;
		return Cards.IsValidIndex(Index) ? Cards[Index] : nullptr;
	}
	TSharedPtr<SShotEditorParameterPanel> GetSubjectPanelForTesting(int32 Index) const
	{
		const auto& Panels = EditSubjectPanels;
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
	TSharedPtr<SBox> EditSubjectsBox;
	TSharedPtr<SVerticalBox> EditSubjectRows;
	TSharedPtr<SWidget> CreatePageWidget;
	TSharedPtr<SComboButton> TemplateCombo;
	FText SelectedTemplateText;
	int32 SelectedTemplateIndex = INDEX_NONE;
	uint32 TemplateMenuRevision = 0;
	TObjectPtr<UComposableCameraShotTemplateLibrary> TemplateLibrary = nullptr;
	FDelegateHandle TemplatePropertyChangedHandle;
	FDelegateHandle TemplateTransactedHandle;
	bool bTemplateRefreshPending = false;
	TSharedPtr<SWidget> EditPageWidget;
	TSharedPtr<SShotEditorParameterPanel> FollowPanel;
	TSharedPtr<SShotEditorParameterPanel> LookAtPanel;
	TSharedPtr<SShotEditorParameterPanel> LensFocusPanel;
	TSharedPtr<SShotEditorParameterPanel> MotionPanel;
	TArray<TSharedPtr<SShotEditorParameterPanel>> EditSubjectPanels;
	TArray<TSharedPtr<SWidget>> EditSubjectCards;
	// View state belongs to the page and subject role/index, never to serialized Shot data.
	TMap<FString, bool> ExpandedSubjectGroups;
	TMap<FString, TWeakPtr<SExpandableArea>> SubjectExpanders;
	TSharedPtr<SScrollBox> EditScrolls[5];
	bool bPersistViewState = false;
	FString ViewStateIni;
	FNotifyHook* NotifyHook = nullptr;
	TArray<TWeakObjectPtr<AActor>> DisplayedActors;
	TSoftObjectPtr<ULevelSequence> Sequence;
	TSoftObjectPtr<UComposableCameraShotAsset> Preset;
	EPage ActivePage = EPage::Edit;
	EEditSection ActiveEditSection = EEditSection::Follow;
	FText Message;
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
	TSharedRef<SWidget> Subjects();
	void SyncSubjects();
	TSharedRef<SWidget> SubjectCard(int32 Index);
	void SetTemplateLibrary(UComposableCameraShotTemplateLibrary* Library);
	void RebuildTemplates();
	FText TemplateEntryLabel(int32 Index) const;
	TSharedRef<SWidget> BuildTemplateMenu();
	void PopulateTemplateMenu(FMenuBuilder& Menu);
	bool CanChooseTemplate(int32 Index, uint32 Revision) const;
	bool IsTemplateSource(const UObject* Object) const;
	void OnTemplatePropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
	void OnTemplateTransacted(UObject* Object, const FTransactionObjectEvent& Event);
	FReply CreateTemplateDraft(int32 Index);
	FReply CreateDraft(const FComposableCameraShot& Shot, const FText& Name);
	TSharedRef<SWidget> ComponentPicker(int32 Index);
	TSharedRef<SWidget> BonePicker(int32 Index);
	TSharedRef<SWidget> SequenceActions();
	UMovieSceneSection* GetBoundClip() const;
	TSharedPtr<ISequencer> OpenClipSequencer();
	float GetClipDuration() const;
	FReply NewSequenceDraft();
	FReply CreateSequenceShot(bool bDialogue);
	FReply DuplicateShot();
	FReply GoToClip();
	FReply DuplicateAtPlayhead();
	void SetMessage(const FString& Reason);
};
