// Copyright 2026 Sulley. All Rights Reserved.
#include "Widgets/SShotEditorAuthoringPanel.h"
#include "Widgets/SShotEditorParameterPanel.h"
#include "Widgets/SShotEditorSubjectPicker.h"
#include "Widgets/SShotEditorSequenceDragButton.h"
#include "Widgets/ComposableCameraShotEditorStyle.h"
#include "Widgets/ComposableCameraShotEditorLayoutState.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "ComposableCameraSystemEditorModule.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "DataAssets/ComposableCameraShotTemplateLibrary.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "Editor.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Editors/ComposableCameraInstanceEditorSession.h"
#include "Editors/ComposableCameraShotSubjectLabel.h"
#include "Editors/ComposableCameraShotEditor.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISequencer.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieScene/ComposableCameraInstanceData.h"
#include "MovieScene/MovieSceneComposableCameraInstanceSection.h"
#include "MovieScene/MovieSceneComposableCameraInstanceTrack.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "Modules/ModuleManager.h"
#include "Misc/TransactionObjectEvent.h"
#include "Misc/MessageDialog.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Actor.h"
#include "PropertyCustomizationHelpers.h"
#include "Selection.h"
#include "Sequencer/ComposableCameraShotAuthoring.h"
#include "Sequencer/ComposableCameraInstanceTrackEditor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ShotEditorAuthoringPanel"
using namespace ComposableCameraSystem::ShotAuthoring;
namespace
{
TSharedRef<SWidget> ShotAuthoringPageScroll(TSharedRef<SWidget> Content, TSharedPtr<SScrollBox>* OutScroll = nullptr)
{
	const auto Scroll = SNew(SScrollBox) + SScrollBox::Slot().Padding(12.f)[Content];
	if (OutScroll) *OutScroll = Scroll;
	return Scroll;
}
}

void SShotEditorAuthoringPanel::Construct(const FArguments& Args)
{
	Session = Args._Session;
	AdvancedContent = Args._AdvancedContent;
	InstanceContent = Args._InstanceContent;
	NotifyHook = Args._NotifyHook;
	bPersistViewState = Args._PersistViewState;
	ViewStateIni = Args._ViewStateIni.IsEmpty() ? GEditorPerProjectIni : Args._ViewStateIni;
	if (bPersistViewState && GConfig)
	{
		const auto State = ComposableCameraSystem::ShotEditorLayout::ReadLayoutState(*GConfig, ViewStateIni);
		ActivePage = static_cast<EPage>(State.Page);
		ActiveEditSection = static_cast<EEditSection>(State.EditSection);
	}
	if (bPersistViewState && GConfig)
	{
		FString LibraryPath;
		GConfig->GetString(TEXT("ComposableCameraSystem.ShotEditor"), TEXT("TemplateLibrary"), LibraryPath, ViewStateIni);
		if (!LibraryPath.IsEmpty()) TemplateLibrary = TSoftObjectPtr<UComposableCameraShotTemplateLibrary>(FSoftObjectPath(LibraryPath)).LoadSynchronous();
	}
	TemplatePropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &SShotEditorAuthoringPanel::OnTemplatePropertyChanged);
	TemplateTransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this, &SShotEditorAuthoringPanel::OnTemplateTransacted);
	const auto Sequencers = FModuleManager::LoadModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor").GetLiveSequencers();
	if (Sequencers.Num()) Sequence = Cast<ULevelSequence>(Sequencers[0]->GetFocusedMovieSceneSequence());
	ChildSlot[SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Background).Padding(0.f)
		[SAssignNew(Body, SVerticalBox)]];
	UpdateGuideSelection();
	Rebuild();
}

SShotEditorAuthoringPanel::~SShotEditorAuthoringPanel()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(TemplatePropertyChangedHandle);
	FCoreUObjectDelegates::OnObjectTransacted.Remove(TemplateTransactedHandle);
}

void SShotEditorAuthoringPanel::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(TemplateLibrary);
	Collector.AddReferencedObject(SelectedType);
}

void SShotEditorAuthoringPanel::OnTemplatePropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	if (IsTemplateSource(Object)) bTemplateRefreshPending = true;
}

void SShotEditorAuthoringPanel::OnTemplateTransacted(UObject* Object, const FTransactionObjectEvent& Event)
{
	if (Event.GetEventType() == ETransactionObjectEventType::UndoRedo && IsTemplateSource(Object)) bTemplateRefreshPending = true;
}

bool SShotEditorAuthoringPanel::IsTemplateSource(const UObject* Object) const
{
	return Object && TemplateLibrary && (Object == TemplateLibrary || TemplateLibrary->Shots.ContainsByPredicate(
		[Object](const FComposableCameraShotTemplate& Entry) { return Entry.Shot == Object; }));
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::BuildNavigation()
{
	using namespace ComposableCameraSystem::ShotEditorStyle;
	const auto PageTab = [this](EPage Page, const FText& Label, const FName Icon) -> TSharedRef<SWidget>
	{
		return SNew(SBox).HeightOverride(28.f)
			.Visibility_Lambda([this, Page]() { return Session->IsGenericInstance() && (Page == EPage::Presets || Page == EPage::Advanced) ? EVisibility::Collapsed : EVisibility::Visible; })
			[SNew(SButton).ButtonStyle(TabStyle()).ContentPadding(FMargin(8.f, 2.f))
				.ButtonColorAndOpacity_Lambda([this, Page]() { return TabColor(ActivePage == Page); })
				.OnClicked_Lambda([this, Page]()
				{
					if (!Session->IsEditing() && (!GEditor || !GEditor->IsTransactionActive()))
					{
						ActivePage = Page;
						UpdateGuideSelection();
					}
					return FReply::Handled();
				})
				[SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
					[SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)
						[SNew(SImage).Image(FAppStyle::GetBrush(Icon)).ColorAndOpacity(FStyleColors::ForegroundHover)]]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[SNew(STextBlock).Text(Label).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FStyleColors::ForegroundHover)]]];
	};
	const auto EditTab = [this](EEditSection Section, const FText& Label) -> TSharedRef<SWidget>
	{
		return SNew(SBox).HeightOverride(28.f)
			.Visibility_Lambda([this, Section]() { return Session->IsGenericInstance() && Section != EEditSection::Follow && Section != EEditSection::Subjects ? EVisibility::Collapsed : EVisibility::Visible; })
			[SNew(SButton).ButtonStyle(SecondaryTabStyle()).ContentPadding(0.f).HAlign(HAlign_Fill).VAlign(VAlign_Fill)
				.OnClicked_Lambda([this, Section]()
				{
					if (!Session->IsEditing() && (!GEditor || !GEditor->IsTransactionActive()))
					{
						ActiveEditSection = Section;
						UpdateGuideSelection();
					}
					return FReply::Handled();
				})
				[SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Center).Padding(12.f, 0.f)
					[SNew(STextBlock).Text_Lambda([this, Section, Label]() { return Session->IsGenericInstance() && Section == EEditSection::Follow ? LOCTEXT("ParametersTab", "Parameters") : Label; })
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity_Lambda([this, Section]() { return ActiveEditSection == Section ? FStyleColors::ForegroundHover : FStyleColors::Foreground; })]
					+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 0.f)
					[SNew(SBox).HeightOverride(2.f)
						[SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).Padding(0.f)
							.BorderBackgroundColor_Lambda([this, Section]() { return ActiveEditSection == Section ? Accent() : FLinearColor::Transparent; })]]]];
	};
	const TSharedRef<SWidget> LevelPreview = SNew(SBox).WidthOverride(104.f).HeightOverride(24.f)
			.Visibility_Lambda([this]() { return Session->IsGenericInstance() ? EVisibility::Collapsed : EVisibility::Visible; })
			[SNew(SButton).ButtonStyle(TabStyle()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(0.f)
				.Tag(FName(TEXT("ShotEditor.LevelPreview")))
				.IsEnabled_Lambda([this]() { return !Session->IsGenericInstance(); })
				.ToolTipText(LOCTEXT("LevelPreviewTip", "Render the current level. Turn off for isolated preview."))
				.ButtonColorAndOpacity_Lambda([this]() { return TabColor(Session->bUseLevelWorld); })
				.OnClicked_Lambda([this]()
				{
					Session->bUseLevelWorld = !Session->bUseLevelWorld;
					Session->RequestPreview();
					return FReply::Handled();
				})
				[SNew(STextBlock).Text(LOCTEXT("LevelPreview", "Level Preview")).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.Justification(ETextJustify::Center).ColorAndOpacity(FStyleColors::ForegroundHover)]];
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.f, 4.f))
				+ SWrapBox::Slot()[PageTab(EPage::Create, LOCTEXT("CreatePage", "Create"), "Icons.PlusCircle")]
				+ SWrapBox::Slot()[PageTab(EPage::Edit, LOCTEXT("EditPage", "Edit"), "Icons.Edit")]
				+ SWrapBox::Slot()[PageTab(EPage::Sequence, LOCTEXT("SequencePage", "Sequence"), "Icons.Play")]
				+ SWrapBox::Slot()[PageTab(EPage::Presets, LOCTEXT("PresetsPage", "Presets"), "Icons.FolderOpen")]
				+ SWrapBox::Slot()[PageTab(EPage::Advanced, LOCTEXT("AdvancedPage", "Advanced"), "Icons.Settings")]]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
			[SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[LevelPreview]
				+ SVerticalBox::Slot().AutoHeight()
				[SNew(STextBlock).Text(LOCTEXT("LevelPreviewStatus", "Preview: Level"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)).ColorAndOpacity(FStyleColors::Foreground)
					.ToolTipText(LOCTEXT("GenericLevelPreviewTip", "This Camera Type previews in the focused Level Sequence's world."))
					.Visibility_Lambda([this]() { return Session->IsGenericInstance() ? EVisibility::Visible : EVisibility::Collapsed; })]]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FStyleColors::Panel).Padding(FMargin(8.f, 3.f, 8.f, 0.f))
			.Visibility_Lambda([this]() { return ActivePage == EPage::Edit ? EVisibility::Visible : EVisibility::Collapsed; })
			[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(2.f, 3.f))
				+ SWrapBox::Slot()[EditTab(EEditSection::Follow, LOCTEXT("FollowTab", "Follow"))]
				+ SWrapBox::Slot()[EditTab(EEditSection::LookAt, LOCTEXT("AimTab", "Aim"))]
				+ SWrapBox::Slot()[EditTab(EEditSection::Lens, LOCTEXT("LensTab", "Lens & Focus"))]
				+ SWrapBox::Slot()[EditTab(EEditSection::Motion, LOCTEXT("MotionTab", "Motion"))]
				+ SWrapBox::Slot()[EditTab(EEditSection::Subjects, LOCTEXT("SubjectsTab", "Subjects"))]]];
}

bool SShotEditorAuthoringPanel::RevealDiagnostic(const FShotEditorDiagnostic& Diagnostic)
{
	if (!Diagnostic.HasIssue() || Session->IsEditing() || (GEditor && GEditor->IsTransactionActive())) return false;
	ActivePage = EPage::Edit;
	TSharedPtr<SShotEditorParameterPanel> Panel;
	switch (Diagnostic.Page)
	{
	case EShotEditorDiagnosticPage::Follow: ActiveEditSection = EEditSection::Follow; Panel = FollowPanel; break;
	case EShotEditorDiagnosticPage::Aim: ActiveEditSection = EEditSection::LookAt; Panel = LookAtPanel; break;
	case EShotEditorDiagnosticPage::Lens: ActiveEditSection = EEditSection::Lens; Panel = LensFocusPanel; break;
	case EShotEditorDiagnosticPage::Subjects: ActiveEditSection = EEditSection::Subjects; break;
	}
	UpdateGuideSelection();
	FString Path = Diagnostic.PropertyPath();
	if (Diagnostic.Page == EShotEditorDiagnosticPage::Subjects)
	{
		const int32 Index = Diagnostic.TargetIndex != INDEX_NONE ? Diagnostic.TargetIndex : 0;
		if (EditSubjectPanels.IsValidIndex(Index))
		{
			Panel = EditSubjectPanels[Index];
			Path = FString::Printf(TEXT("Targets[%d].BoundsGroup"), Index);
			const FString Group = FString::Printf(TEXT("ShotEditor.Edit.Targets[%d].SubjectGroup"), Index);
			if (const auto* Area = SubjectExpanders.Find(Group))
				if (const auto Pinned = Area->Pin()) Pinned->SetExpanded(true);
		}
	}
	if (Panel)
		if (const auto Row = Panel->RevealProperty(Path))
			EditScrolls[static_cast<int32>(ActiveEditSection)]->ScrollDescendantIntoView(Row, false);
	return true;
}

void SShotEditorAuthoringPanel::ShowSubjects()
{
	ActivePage = EPage::Edit; ActiveEditSection = EEditSection::Subjects;
	Message = FText::GetEmpty(); UpdateGuideSelection();
}

void SShotEditorAuthoringPanel::UpdateGuideSelection()
{
	Session->bShowLookAtGuide = !Session->IsGenericInstance() && ActivePage == EPage::Edit && ActiveEditSection == EEditSection::LookAt;
	Session->bShowFollowGuide = !Session->IsGenericInstance() && ActivePage == EPage::Edit && ActiveEditSection == EEditSection::Follow;
	Session->bShowSubjectGuide = !Session->IsGenericInstance() && ActivePage == EPage::Edit && ActiveEditSection == EEditSection::Subjects;
	if (bPersistViewState && GConfig)
	{
		using namespace ComposableCameraSystem::ShotEditorLayout;
		auto State = ReadLayoutState(*GConfig, ViewStateIni);
		State.Page = static_cast<int32>(ActivePage);
		State.EditSection = static_cast<int32>(ActiveEditSection);
		WriteLayoutState(*GConfig, ViewStateIni, State);
	}
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::Section(const FText& Label, TSharedRef<SWidget> Content)
{
	return SNew(SBox).WidthOverride_Lambda([this]()
		{
			return ComposableCameraSystem::ShotEditorStyle::ColumnWidth(static_cast<float>(GetCachedGeometry().GetLocalSize().X) - 40.f);
		})[ComposableCameraSystem::ShotEditorStyle::Group(Label, Content)];
}

void SShotEditorAuthoringPanel::Tick(const FGeometry& Geometry, double Time, float DeltaTime)
{
	SCompoundWidget::Tick(Geometry, Time, DeltaTime);
	if (bTemplateRefreshPending && !Session->IsEditing() && (!GEditor || !GEditor->IsTransactionActive())) RebuildTemplates();
	const FComposableCameraShot* Shot = Session->GetShot();
	if ((Shot ? Shot->Targets.Num() : 0) != DisplayedActors.Num()) bRefreshPending = true;
	if (!bRefreshPending)
		for (int32 Index = 0; Index < DisplayedActors.Num(); ++Index)
			if (DisplayedActors[Index].Get() != Session->ResolveTarget(Index)) { bRefreshPending = true; break; }
	if (bRefreshPending && !Session->IsEditing() && (!GEditor || !GEditor->IsTransactionActive())) Rebuild();
}

void SShotEditorAuthoringPanel::Rebuild()
{
	bRefreshPending = false;
	if (Session->IsGenericInstance())
	{
		if (ActivePage == EPage::Presets || ActivePage == EPage::Advanced) ActivePage = EPage::Edit;
		if (ActiveEditSection != EEditSection::Subjects) ActiveEditSection = EEditSection::Follow;
		UpdateGuideSelection();
	}
	DisplayedActors.Reset();
	if (const auto* Shot = Session->GetShot())
		for (int32 Index = 0; Index < Shot->Targets.Num(); ++Index)
			DisplayedActors.Add(Session->ResolveTarget(Index));

	if (bPagesBuilt)
	{
		for (const auto& Panel : { FollowPanel, LookAtPanel, LensFocusPanel, MotionPanel }) Panel->RefreshSource(true);
		SyncSubjects();
		SyncGenericSubjects();
		return;
	}
	bPagesBuilt = true;
#if WITH_DEV_AUTOMATION_TESTS
	++PageBuildCount;
#endif
	EditPageWidget = SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]() { return Session->IsGenericInstance() ? 1 : 0; })
		+ SWidgetSwitcher::Slot()[EditPage()]
		+ SWidgetSwitcher::Slot()[GenericEditPage()];
	CreatePageWidget = CreatePage();
	// Switchers keep page widgets alive. Navigation does not rebuild controls,
	// rebind the source or disturb a numeric gesture.
	Body->AddSlot().FillHeight(1.f)
	[SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]() { return static_cast<int32>(ActivePage); })
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(CreatePageWidget.ToSharedRef())]
		+ SWidgetSwitcher::Slot()[EditPageWidget.ToSharedRef()]
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(SequenceActions())]
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(PresetActions())]
		+ SWidgetSwitcher::Slot()
		[SNew(SBox).IsEnabled_Lambda([this]() { return Session->CanEdit(); })
			[AdvancedContent.IsValid() ? AdvancedContent.ToSharedRef() : SNullWidget::NullWidget]]];
	Body->AddSlot().AutoHeight()
	[SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(.08f, .08f, .08f)).Padding(10.f, 5.f)
		.Visibility_Lambda([this]() { return Message.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		[SNew(STextBlock).Text_Lambda([this]() { return Message; }).AutoWrapText(true)]];
}

#if WITH_DEV_AUTOMATION_TESTS
TSharedPtr<SWidget> SShotEditorAuthoringPanel::GetEditSubjectsForTesting() const
{
	return EditSubjectsBox ? EditSubjectsBox->GetChildren()->GetChildAt(0).ToSharedPtr() : nullptr;
}

#endif

TSharedRef<SWidget> SShotEditorAuthoringPanel::CreatePage()
{
	using namespace ComposableCameraSystem::ShotEditorStyle;
	const auto Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[SNew(STextBlock).Text(LOCTEXT("TemplateLibrary", "Template Library"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[SNew(SObjectPropertyEntryBox).Tag(FName(TEXT("ShotEditor.TemplateLibrary")))
			.AllowedClass(UComposableCameraShotTemplateLibrary::StaticClass())
			.ObjectPath_Lambda([this]() { return TemplateLibrary ? TemplateLibrary->GetPathName() : FString(); })
			.OnObjectChanged_Lambda([this](const FAssetData& Asset) { SetTemplateLibrary(Cast<UComposableCameraShotTemplateLibrary>(Asset.GetAsset())); })]
		+ SVerticalBox::Slot().AutoHeight()
		[SAssignNew(TemplateCombo, SComboButton).Tag(FName(TEXT("ShotEditor.TemplateChoice")))
			.IsEnabled_Lambda([this]() { return TemplateLibrary && !TemplateLibrary->Shots.IsEmpty() && !bTemplateRefreshPending
				&& !Session->IsEditing() && (!GEditor || !GEditor->IsTransactionActive()); })
			.OnGetMenuContent(this, &SShotEditorAuthoringPanel::BuildTemplateMenu)
			.ButtonContent()[SNew(STextBlock).Text_Lambda([this]() { return SelectedTemplateText; })]];
	RebuildTemplates();
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[Section(LOCTEXT("TemplateSection", "Shot template"), Content)]
		// Match the Camera Type group's eight-unit content inset and shared action style.
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(8.f, 8.f, 0.f, 0.f)
		[Action(SNew(SButton).Tag(FName(TEXT("ShotEditor.Create.NewDraft"))).Text(LOCTEXT("NewDraft", "Create Empty Shot"))
			.IsEnabled_Lambda([this]() { return !Session->IsEditing() && (!GEditor || !GEditor->IsTransactionActive()); })
			.ToolTipText(LOCTEXT("EmptyDraftTip", "Create an empty Shot with no Subjects, without using a template."))
			.OnClicked(this, &SShotEditorAuthoringPanel::NewSequenceDraft))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
		[Section(LOCTEXT("CameraTypeSection", "Camera Type"), SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[SNew(SObjectPropertyEntryBox).Tag(FName(TEXT("ShotEditor.CameraType")))
				.AllowedClass(UComposableCameraTypeAsset::StaticClass())
				.OnShouldFilterAsset(FOnShouldFilterAsset::CreateLambda([](const FAssetData& Asset)
					{ return Asset.GetClass() && Asset.GetClass()->IsChildOf(UComposableCameraPatchTypeAsset::StaticClass()); }))
				.ObjectPath_Lambda([this]() { return SelectedType ? SelectedType->GetPathName() : FString(); })
				.OnObjectChanged_Lambda([this](const FAssetData& Asset) { SelectedType = Cast<UComposableCameraTypeAsset>(Asset.GetAsset()); })]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
			[Action(SNew(SButton).Tag(FName(TEXT("ShotEditor.Create.TypeDraft"))).Text(LOCTEXT("CreateTypeDraft", "Create Draft"))
				.IsEnabled_Lambda([this]() { return SelectedType && !Session->IsEditing() && (!GEditor || !GEditor->IsTransactionActive()); })
				.OnClicked(this, &SShotEditorAuthoringPanel::CreateTypeDraft))])];
}

bool SShotEditorAuthoringPanel::ConfirmReplaceDraft() const
{
	return !Session->HasModifiedDraft() || FMessageDialog::Open(EAppMsgType::YesNo,
		LOCTEXT("DiscardDraft", "Discard this modified draft? It has not been inserted into Level Sequence.")) == EAppReturnType::Yes;
}

FReply SShotEditorAuthoringPanel::CreateTypeDraft()
{
	if (!ConfirmReplaceDraft()) return FReply::Handled();
	auto& Module = FModuleManager::LoadModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor");
	auto Seq = Session->GetSequencer();
	if (!Seq && !Session->GetSequence())
	{
		const auto Live = Module.GetLiveSequencers();
		if (Live.Num()) Seq = Live[0];
	}
	FString Reason;
	if (Session->StartTypeDraft(SelectedType, Seq, Reason))
	{
		Sequence = Cast<ULevelSequence>(Session->GetSequence());
		ActivePage = EPage::Edit;
		ActiveEditSection = Session->GetInstanceValues()->Subjects.IsEmpty() ? EEditSection::Follow : EEditSection::Subjects;
		UpdateGuideSelection(); Refresh();
	}
	SetMessage(Reason);
	return FReply::Handled();
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::GenericEditPage()
{
	SAssignNew(GenericSubjectRows, SVerticalBox);
	SyncGenericSubjects();
	return SNew(SWidgetSwitcher).Tag(FName(TEXT("ShotEditor.GenericEdit")))
		.WidgetIndex_Lambda([this]() { return ActiveEditSection == EEditSection::Subjects ? 1 : 0; })
		+ SWidgetSwitcher::Slot()[InstanceContent ? InstanceContent.ToSharedRef() : SNullWidget::NullWidget]
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(GenericSubjectRows.ToSharedRef())];
}

void SShotEditorAuthoringPanel::SyncGenericSubjects()
{
	if (!GenericSubjectRows) return;
	TArray<FName> Inputs;
	TArray<FText> Labels, Descriptions;
	TArray<const UClass*> ActorClasses;
	TArray<bool> RequiredInputs;
	TArray<FString> Presentation;
	if (auto* Values = Session->GetInstanceValues(); Values && !Values->bComposition)
	{
		TArray<FComposableCameraInstanceField> Fields; Values->GatherFields(Fields);
		for (const auto& Field : Fields)
			if (const auto* Property = CastField<FObjectPropertyBase>(Field.Property); Property && Property->PropertyClass->IsChildOf(AActor::StaticClass()))
			{
				Inputs.Add(Field.Path);
				ActorClasses.Add(Property->PropertyClass.Get()); RequiredInputs.Add(Field.bRequiredActor);
				Labels.Add(ComposableCameraSystem::CameraInstanceAuthoring::ParameterLabel(*Values, Field.Path));
				Descriptions.Add(FText::Format(LOCTEXT("ActorInputMetadata", "{0} | {1}"),
					Property->PropertyClass->GetDisplayNameText(), Field.bRequiredActor ? LOCTEXT("RequiredInput", "Required") : LOCTEXT("OptionalInput", "Optional")));
				Presentation.Add(Labels.Last().ToString() + TEXT("\n") + Descriptions.Last().ToString() + TEXT("\n") + Property->PropertyClass->GetPathName());
			}
	}
	if (Inputs == GenericInputs && Presentation == GenericInputPresentation && GenericSubjectRows->GetChildren()->Num() > 0) return;
	GenericInputs = Inputs;
	GenericInputPresentation = MoveTemp(Presentation);
	GenericSubjectRows->ClearChildren();
	if (Inputs.IsEmpty())
		GenericSubjectRows->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("NoActorInputs", "This Camera Type exposes no Actor inputs.")).AutoWrapText(true)];
	if (!Inputs.IsEmpty())
		GenericSubjectRows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
		[SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("ActorInputGuide", "Bind the Actor inputs exposed by this Camera Type. Level actors are referenced through Sequence bindings; the actors are not duplicated."))];
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const FName Input = Inputs[Index];
		GenericSubjectRows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
		[SNew(SBox).ToolTipText(FText::FromName(Input))
		[ComposableCameraSystem::ShotEditorStyle::Group(Labels[Index], SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[SNew(STextBlock).Text(Descriptions[Index]).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))]
			+ SVerticalBox::Slot().AutoHeight()
			[SNew(SShotEditorSubjectPicker).Session(Session).InputPath(Input)
				.ActorClass(ActorClasses[Index]).RequiredInput(RequiredInputs[Index])
				.OnError_Lambda([this](const FString& Reason) { SetMessage(Reason); })])]];
	}
}

void SShotEditorAuthoringPanel::SetTemplateLibrary(UComposableCameraShotTemplateLibrary* Library)
{
	TemplateLibrary = Library;
	SelectedTemplateIndex = INDEX_NONE;
	if (bPersistViewState && GConfig)
		GConfig->SetString(TEXT("ComposableCameraSystem.ShotEditor"), TEXT("TemplateLibrary"),
			Library ? *Library->GetPathName() : TEXT(""), ViewStateIni);
	bTemplateRefreshPending = true;
}

void SShotEditorAuthoringPanel::RebuildTemplates()
{
	if (TemplateCombo) TemplateCombo->SetIsOpen(false);
	++TemplateMenuRevision;
	bTemplateRefreshPending = false;
	if (!TemplateLibrary || !TemplateLibrary->Shots.IsValidIndex(SelectedTemplateIndex)) SelectedTemplateIndex = INDEX_NONE;
	SelectedTemplateText = SelectedTemplateIndex != INDEX_NONE ? TemplateEntryLabel(SelectedTemplateIndex)
		: !TemplateLibrary ? LOCTEXT("SelectTemplateLibrary", "Choose a template library first")
		: TemplateLibrary->Shots.IsEmpty() ? LOCTEXT("NoTemplateShots", "No Shots in this library")
		: LOCTEXT("SelectTemplateShot", "Choose a Shot...");
}

FText SShotEditorAuthoringPanel::TemplateEntryLabel(int32 Index) const
{
	const auto& Entry = TemplateLibrary->Shots[Index];
	return IsValid(Entry.Shot.Get())
		? FText::Format(LOCTEXT("TemplateEntry", "{0} ({1} Subjects)"), FText::FromName(Entry.Name), Entry.Shot->Shot.Targets.Num())
		: FText::Format(LOCTEXT("MissingTemplateShot", "{0} (Missing Shot)"), FText::FromName(Entry.Name));
}

bool SShotEditorAuthoringPanel::CanChooseTemplate(int32 Index, uint32 Revision) const
{
	return Revision == TemplateMenuRevision && !bTemplateRefreshPending && TemplateLibrary && TemplateLibrary->Shots.IsValidIndex(Index)
		&& !TemplateLibrary->Shots[Index].Name.IsNone() && IsValid(TemplateLibrary->Shots[Index].Shot.Get())
		&& !Session->IsEditing() && (!GEditor || !GEditor->IsTransactionActive());
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::BuildTemplateMenu()
{
	FMenuBuilder Menu(true, nullptr);
	PopulateTemplateMenu(Menu);
	return Menu.MakeWidget();
}

void SShotEditorAuthoringPanel::PopulateTemplateMenu(FMenuBuilder& Menu)
{
	if (!TemplateLibrary) return;
	const TWeakPtr<SShotEditorAuthoringPanel> WeakPanel = SharedThis(this);
	const uint32 Revision = TemplateMenuRevision;
	for (int32 Index = 0; Index < TemplateLibrary->Shots.Num(); ++Index)
	{
		Menu.AddMenuEntry(TemplateEntryLabel(Index),
			IsValid(TemplateLibrary->Shots[Index].Shot.Get())
				? LOCTEXT("CreateFromTemplateTip", "Create an independent draft from this Shot, then bind its Subjects in Edit.")
				: LOCTEXT("MissingTemplateShotTip", "Assign an existing Shot asset to this entry in the template library."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakPanel, Index, Revision]()
				{ if (const auto Panel = WeakPanel.Pin(); Panel && Panel->CanChooseTemplate(Index, Revision)) Panel->CreateTemplateDraft(Index); }),
				FCanExecuteAction::CreateLambda([WeakPanel, Index, Revision]()
				{ const auto Panel = WeakPanel.Pin(); return Panel && Panel->CanChooseTemplate(Index, Revision); }),
				FIsActionChecked::CreateLambda([WeakPanel, Index]()
				{ const auto Panel = WeakPanel.Pin(); return Panel && Panel->SelectedTemplateIndex == Index; })),
			NAME_None, EUserInterfaceActionType::RadioButton);
	}
}

FReply SShotEditorAuthoringPanel::CreateTemplateDraft(int32 Index)
{
	if (bTemplateRefreshPending || !TemplateLibrary || !TemplateLibrary->Shots.IsValidIndex(Index)) return FReply::Handled();
	const auto& Entry = TemplateLibrary->Shots[Index];
	if (Entry.Name.IsNone()) { SetMessage(TEXT("Name this Shot in its template library first.")); return FReply::Handled(); }
	if (!IsValid(Entry.Shot.Get())) { SetMessage(TEXT("Assign an existing Shot asset to this template first.")); return FReply::Handled(); }
	UObject* PreviousHost = Session->GetHost();
	CreateDraft(Entry.Shot->Shot, FText::FromName(Entry.Name));
	if (Session->GetHost() != PreviousHost)
	{
		SelectedTemplateIndex = Index;
		RebuildTemplates();
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::EditPage()
{
	if (!FollowPanel) SAssignNew(FollowPanel, SShotEditorParameterPanel).Session(Session).NotifyHook(NotifyHook).Section(EShotEditorParameterSection::Follow).PersistViewState(bPersistViewState).ViewStateIni(ViewStateIni);
	if (!LookAtPanel) SAssignNew(LookAtPanel, SShotEditorParameterPanel).Session(Session).NotifyHook(NotifyHook).Section(EShotEditorParameterSection::LookAt).PersistViewState(bPersistViewState).ViewStateIni(ViewStateIni);
	if (!LensFocusPanel) SAssignNew(LensFocusPanel, SShotEditorParameterPanel).Session(Session).NotifyHook(NotifyHook).Section(EShotEditorParameterSection::LensFocus).PersistViewState(bPersistViewState).ViewStateIni(ViewStateIni);

	if (!MotionPanel) SAssignNew(MotionPanel, SShotEditorParameterPanel).Session(Session).NotifyHook(NotifyHook).Section(EShotEditorParameterSection::Motion).PersistViewState(bPersistViewState).ViewStateIni(ViewStateIni);

	return SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]() { return static_cast<int32>(ActiveEditSection); })
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(FollowPanel.ToSharedRef(), &EditScrolls[0])]
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(LookAtPanel.ToSharedRef(), &EditScrolls[1])]
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(LensFocusPanel.ToSharedRef(), &EditScrolls[2])]
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(MotionPanel.ToSharedRef(), &EditScrolls[3])]
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(SAssignNew(EditSubjectsBox, SBox)[Subjects()], &EditScrolls[4])];
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::Subjects()
{
	auto& Rows = EditSubjectRows;
	Rows = SNew(SVerticalBox);
	Rows->SetEnabled(TAttribute<bool>::CreateLambda([this]() { return Session->CanEdit(); }));
	const FString Prefix = TEXT("ShotEditor.Edit");
	const TSharedRef<SWidget> Actions = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f, 8.f))
		+ SWrapBox::Slot()
		[ComposableCameraSystem::ShotEditorStyle::Action(SNew(SButton).Tag(FName(*(Prefix + TEXT(".AddSubject"))))
			.Text(LOCTEXT("AddSubject", "+ Add Subject"))
			.IsEnabled_Lambda([this]() { return Session->CanAddTargets(); })
			.ToolTipText(LOCTEXT("AddSubjectTip", "Append an empty subject, then choose an actor or preview model."))
			.OnClicked_Lambda([this]()
			{
				FString Reason; if (!Session->AddTarget(nullptr, Reason)) SetMessage(Reason);
				else Message = FText::GetEmpty();
				return FReply::Handled();
			}))]
		+ SWrapBox::Slot()
		[ComposableCameraSystem::ShotEditorStyle::Action(SNew(SButton).Tag(FName(*(Prefix + TEXT(".AddSelectedSubjects"))))
			.Text(LOCTEXT("AppendSelectedSubjects", "Add Selected Actors"))
			.IsEnabled_Lambda([this]() { return Session->CanAddTargets() && GEditor && GEditor->GetSelectedActors()->Num() > 0; })
			.ToolTipText(LOCTEXT("AppendSelectedSubjectsTip", "Append the selected actors. Existing subjects and composition are kept."))
			.OnClicked_Lambda([this]()
			{
				FString Reason; if (!Session->AddSelectedTargets(Reason)) SetMessage(Reason);
				else Message = FText::GetEmpty();
				return FReply::Handled();
			}))];
	SyncSubjects();
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[Actions]
		+ SVerticalBox::Slot().AutoHeight()
		[SNew(STextBlock).Text(LOCTEXT("Pick", "Add a subject, then choose an actor or preview model.")).AutoWrapText(true)
			.Visibility_Lambda([this]() { const auto* Shot = Session->GetShot(); return !Shot || Shot->Targets.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })]
		+ SVerticalBox::Slot().AutoHeight()[Rows.ToSharedRef()];
}

void SShotEditorAuthoringPanel::SyncSubjects()
{
	auto& Rows = EditSubjectRows;
	auto& Cards = EditSubjectCards;
	auto& Panels = EditSubjectPanels;
	const auto* Shot = Session->GetShot();
	const int32 Count = Shot ? Shot->Targets.Num() : 0;
	while (Cards.Num() > Count)
	{
		Rows->RemoveSlot(Cards.Last().ToSharedRef());
		Cards.Pop();
	}
	Panels.SetNum(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (!Panels[Index]) SAssignNew(Panels[Index], SShotEditorParameterPanel)
			.Session(Session).NotifyHook(NotifyHook).Section(EShotEditorParameterSection::Subject).SubjectIndex(Index)
			.PersistViewState(bPersistViewState).ViewStateIni(ViewStateIni).ViewKey(FString::Printf(TEXT("ShotEditor.Edit.Targets[%d]"), Index));
		else Panels[Index]->RefreshSource(true);
		if (Index >= Cards.Num())
		{
			const auto Card = SubjectCard(Index);
			Cards.Add(Card);
			Rows->AddSlot().AutoHeight().HAlign(HAlign_Fill).Padding(0.f, 0.f, 0.f, 8.f)[Card];
		}
	}
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::SubjectCard(int32 Index)
{
	const auto& Panels = EditSubjectPanels;
	const auto ExpandableGroup = [this](const FText& Label, const FString& Path, TSharedRef<SWidget> Content,
		bool bExpanded, bool bPrimary, TSharedPtr<SWidget> Header = nullptr) -> TSharedRef<SWidget>
	{
		ExpandedSubjectGroups.FindOrAdd(Path, bPersistViewState ? ComposableCameraSystem::ShotEditorLayout::ReadFold(Path, bExpanded, ViewStateIni) : bExpanded);
		const auto Area = SNew(SExpandableArea).Tag(FName(*Path)).InitiallyCollapsed(!ExpandedSubjectGroups.FindRef(Path)).Padding(0.f)
			.BorderImage(bPrimary ? FComposableCameraEditorStyle::Get()->GetBrush(TEXT("DebugTooltip.Header")) : FCoreStyle::Get().GetBrush("ExpandableArea.Border"))
			.BodyBorderImage(bPrimary ? FComposableCameraEditorStyle::Get()->GetBrush(TEXT("DebugTooltip.Background")) : FAppStyle::GetBrush("NoBorder"))
			.HeaderPadding(bPrimary ? FMargin(0.f) : FMargin(4.f, 2.f))
			.OnAreaExpansionChanged_Lambda([this, Path](bool bOpen)
			{
				ExpandedSubjectGroups.Add(Path, bOpen);
				if (bPersistViewState) ComposableCameraSystem::ShotEditorLayout::WriteFold(Path, bOpen, ViewStateIni);
			})
			.HeaderContent()[Header ? Header.ToSharedRef() : bPrimary ? ComposableCameraSystem::ShotEditorStyle::GroupHeader(Label) : ComposableCameraSystem::ShotEditorStyle::SubsectionHeader(Label)]
			.BodyContent()[Content];
		SubjectExpanders.Add(Path, Area);
		return Area;
	};
	const FText Label = ComposableCameraSystem::ShotSubjectLabels::ToText(Index);
	const FString Prefix = FString::Printf(TEXT("ShotEditor.Edit.Targets[%d]"), Index);
	const TSharedRef<SWidget> Header = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)[ComposableCameraSystem::ShotEditorStyle::GroupHeader(Label)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 6.f, 0.f)
		[SNew(SBox).WidthOverride(28.f).HeightOverride(24.f)
			[SNew(SButton).Tag(FName(*(Prefix + TEXT(".RemoveSubject"))))
				.HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(FMargin(4.f))
				.NormalPaddingOverride(FMargin(0.f)).PressedPaddingOverride(FMargin(0.f))
				.IsEnabled_Lambda([this, Index]() { return Session->CanRemoveTarget(Index); })
				.ToolTipText(LOCTEXT("RemoveSubjectTip", "Delete this subject from the Shot."))
				.OnClicked_Lambda([this, Index]()
				{
					FString Reason; if (!Session->RemoveTarget(Index, Reason)) SetMessage(Reason);
					else Message = FText::GetEmpty();
					return FReply::Handled();
				})
				[SNew(SImage).Image(FAppStyle::GetBrush("Icons.Delete"))]]];
	return ExpandableGroup(Label, Prefix + TEXT(".SubjectGroup"), SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 6.f)
		[SNew(SShotEditorSubjectPicker).Session(Session).SubjectIndex(Index)
			.OnError_Lambda([WeakPanel = TWeakPtr<SShotEditorAuthoringPanel>(SharedThis(this))](const FString& Reason)
			{ if (const auto Panel = WeakPanel.Pin()) Panel->SetMessage(Reason); })]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(8.f, 0.f, 8.f, 6.f)
		[ExpandableGroup(LOCTEXT("SubjectPivot", "Component / pivot"), Prefix + TEXT(".ComponentGroup"), SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)[ComponentPicker(Index)]
			+ SVerticalBox::Slot().AutoHeight()[BonePicker(Index)], false, false)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(8.f, 0.f)
		[Panels[Index].ToSharedRef()], true, true, Header);
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::ComponentPicker(int32 Index)
{
	return SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text_Lambda([this, Index]()
	{
		const auto* Shot = Session->GetShot();
		const FName Name = Shot && Shot->Targets.IsValidIndex(Index) ? Shot->Targets[Index].Target.ComponentName : NAME_None;
		return Name.IsNone() ? LOCTEXT("AutoComponent", "Component: Actor / Auto mesh") : FText::FromName(Name);
	})].OnGetMenuContent_Lambda([this, Index]()
	{
		FMenuBuilder Menu(true, nullptr);
		TArray<FName> Names; Names.Add(NAME_None);
		if (AActor* Actor = Session->ResolveTarget(Index))
			for (UActorComponent* Component : Actor->GetComponents())
				if (Component && Component->IsA<USceneComponent>()) Names.Add(Component->GetFName());
		for (FName Name : Names)
			Menu.AddMenuEntry(Name.IsNone() ? LOCTEXT("ActorPivot", "Actor / Auto mesh") : FText::FromName(Name), FText(), FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, Index, Name]()
				{
					if (auto* Shot = Session->GetShot(); Shot && Session->CanEdit() && Shot->Targets.IsValidIndex(Index))
					{
						Session->BeginEdit(LOCTEXT("Component", "Set Subject Component"));
						auto& Target = Shot->Targets[Index]; Target.Target.ComponentName = Name;
						Target.Target.Offset = FVector::ZeroVector; Target.Target.BoneName = NAME_None; Target.Target.bUseBoneAsPivot = false;
						Target.CachedBoundsMeshComponent.Reset(); Target.RefreshAutoBoundsCache(); Session->Changed(); Session->EndEdit();
					}
				})));
		return Menu.MakeWidget();
	});
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::BonePicker(int32 Index)
{
	return SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text_Lambda([this, Index]()
	{
		const auto* Shot = Session->GetShot();
		const FName Name = Shot && Shot->Targets.IsValidIndex(Index) && Shot->Targets[Index].Target.bUseBoneAsPivot ? Shot->Targets[Index].Target.BoneName : NAME_None;
		return Name.IsNone() ? LOCTEXT("NoBone", "Pivot: Actor / Component") : FText::FromName(Name);
	})].OnGetMenuContent_Lambda([this, Index]()
	{
		FMenuBuilder Menu(true, nullptr);
		TArray<FName> Names; Names.Add(NAME_None);
		const auto* Shot = Session->GetShot();
		if (Shot && Shot->Targets.IsValidIndex(Index))
		{
			if (AActor* Actor = Session->ResolveTarget(Index))
			{
				for (UActorComponent* Component : Actor->GetComponents())
					if (auto* Mesh = Cast<USkeletalMeshComponent>(Component); Mesh && (Shot->Targets[Index].Target.ComponentName.IsNone() || Mesh->GetFName() == Shot->Targets[Index].Target.ComponentName))
						{ for (FName Bone : Mesh->GetAllSocketNames()) Names.AddUnique(Bone); }
			}
			else if (USkeletalMesh* Mesh = Shot->Targets[Index].Target.EditorPreviewMesh.LoadSynchronous())
				for (int32 Bone = 0; Bone < Mesh->GetRefSkeleton().GetNum(); ++Bone) Names.Add(Mesh->GetRefSkeleton().GetBoneName(Bone));
		}
		for (FName Name : Names)
			Menu.AddMenuEntry(Name.IsNone() ? LOCTEXT("ClearBone", "Actor / Component pivot") : FText::FromName(Name), FText(), FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, Index, Name]()
				{
					if (auto* Data = Session->GetShot(); Data && Session->CanEdit() && Data->Targets.IsValidIndex(Index))
					{
						Session->BeginEdit(LOCTEXT("Bone", "Set Subject Bone"));
						auto& Target = Data->Targets[Index].Target; Target.BoneName = Name; Target.bUseBoneAsPivot = !Name.IsNone(); Target.Offset = FVector::ZeroVector;
						Session->Changed(); Session->EndEdit();
					}
				})));
		return SNew(SBox).MaxDesiredHeight(320.f)[Menu.MakeWidget()];
	});
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::SequenceActions()
{
	using namespace ComposableCameraSystem::ShotEditorStyle;
	const auto Metric = [](const FText& Label, const FText& Tooltip, TSharedRef<SWidget> Value) -> TSharedRef<SWidget>
	{
		return SNew(SBorder).BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
			.Padding(8.f).ToolTipText(Tooltip).ForegroundColor(FStyleColors::ForegroundHeader)
			[SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[SNew(STextBlock).Text(Label).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FStyleColors::Foreground).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
				+ SVerticalBox::Slot().AutoHeight()[Value]];
	};
	const TSharedRef<SWidget> DestinationControls = Section(LOCTEXT("SequenceSummarySection", "Sequence"), SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f, 4.f, 2.f, 12.f)
			[SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
				[SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
					[SNew(SImage).Image(FAppStyle::GetBrush("ClassIcon.LevelSequence"))]]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[SNew(STextBlock).Tag(FName(TEXT("ShotEditor.Destination")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11)).ColorAndOpacity(FStyleColors::ForegroundHeader).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Text_Lambda([this]() { const auto* Target = Session->GetSequence() ? Session->GetSequence() : Sequence.Get(); return Target ? FText::FromString(Target->GetName()) : LOCTEXT("NoSequenceDestination", "No Level Sequence"); })
					.ToolTipText_Lambda([this]() { const auto* Target = Session->GetSequence() ? Session->GetSequence() : Sequence.Get(); return Target ? FText::FromString(Target->GetPathName()) : LOCTEXT("NoDestination", "Open a Level Sequence to insert a clip."); })]]
			+ SVerticalBox::Slot().AutoHeight()
			[SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(.55f).Padding(0.f, 0.f, 8.f, 0.f)
				[Metric(LOCTEXT("ClipFrameRangeLabel", "Frame range"),
					LOCTEXT("ClipFrameRangeTooltip", "Sequence display frames. Start included; end excluded. A draft gets its start when dropped on the timeline or added at the playhead."),
					SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Text_Lambda([this]()
					{
						const auto* Clip = GetBoundClip();
						const auto* Scene = Clip ? Clip->GetTypedOuter<UMovieScene>() : nullptr;
						if (!Scene) return Session->IsSequenceDraft() ? LOCTEXT("DraftRangeOnInsert", "On insert") : LOCTEXT("SequenceMetricUnavailable", "-");
						if (!Clip->HasStartFrame() || !Clip->HasEndFrame()) return LOCTEXT("ClipRangeUnbounded", "Unbounded");
						const FFrameRate Tick = Scene->GetTickResolution(), Display = Scene->GetDisplayRate();
						return FText::Format(LOCTEXT("ClipDisplayRangeValue", "{0} - {1}"),
							FText::AsNumber(FFrameRate::TransformTime(Clip->GetInclusiveStartFrame(), Tick, Display).AsDecimal()),
							FText::AsNumber(FFrameRate::TransformTime(Clip->GetExclusiveEndFrame(), Tick, Display).AsDecimal()));
					}))]
				+ SHorizontalBox::Slot().FillWidth(.45f)
				[Metric(LOCTEXT("ClipDurationLabel", "Duration"), LOCTEXT("ClipDurationTooltip", "Clip duration in seconds."),
					SNew(STextBlock).Tag(FName(TEXT("ShotEditor.ClipDuration")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Text_Lambda([this]()
					{
						const auto* Clip = GetBoundClip();
						if (!Session->IsSequenceDraft() && (Clip ? !Clip->HasStartFrame() || !Clip->HasEndFrame() : Session->GetShot() == nullptr))
							return LOCTEXT("SequenceMetricUnavailable", "-");
						return FText::Format(LOCTEXT("ClipDurationValue", "{0} s"), FText::AsNumber(GetClipDuration()));
					}))]]);
	return SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(24.f, 16.f))
		+ SWrapBox::Slot()[DestinationControls]
		+ SWrapBox::Slot()
		[Section(LOCTEXT("ClipSection", "Clip"), SNew(SVerticalBox).Tag(FName(TEXT("ShotEditor.ClipActions")))
			+ SVerticalBox::Slot().AutoHeight()
			[SNew(SVerticalBox).Tag(FName(TEXT("ShotEditor.CreateActions")))
				.Visibility_Lambda([this]() { return Session->IsSequenceDraft() || !Session->GetInstanceValues() ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[SubsectionHeader(LOCTEXT("ClipCreateActions", "Create"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[SNew(SBox).Visibility_Lambda([this]() { return Session->IsGenericInstance() ? EVisibility::Collapsed : EVisibility::Visible; })
				[SequenceAction(LOCTEXT("NewDraft", "Create Empty Shot"), SNew(SButton).Tag(FName(TEXT("ShotEditor.NewDraft")))
					.OnClicked(this, &SShotEditorAuthoringPanel::NewSequenceDraft))]]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[SNew(SVerticalBox).Tag(FName(TEXT("ShotEditor.DragRow")))
				.Visibility_Lambda([this]() { return Session->IsSequenceDraft() ? EVisibility::Visible : EVisibility::Collapsed; })
				.IsEnabled_Lambda([this]() { return Session->IsSequenceDraft() && Session->CanEdit() && !Session->IsEditing(); })
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
				[SNew(SShotEditorSequenceDragButton).Tag(FName(TEXT("ShotEditor.DragIntoSequence"))).Session(Session)
					.Duration_Lambda([this]() { return static_cast<double>(GetClipDuration()); })
					.OnError_Lambda([this](const FText& Error) { Message = Error; })]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("DragIntoSequenceHint",
					"Or drag this button onto the timeline. The new Camera Cut replaces overlapping cuts."))]]
			+ SVerticalBox::Slot().AutoHeight()
			[SNew(SBox).Visibility_Lambda([this]() { return Session->GetInstanceValues() ? EVisibility::Collapsed : EVisibility::Visible; })
			[SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
				[SequenceAction(LOCTEXT("CreateSequence", "Add Shot to Sequence"), SNew(SButton)
					.IsEnabled_Lambda([this]() { return Session->GetShot() != nullptr; })
					.OnClicked_Lambda([this]() { return CreateSequenceShot(false); }))]
				+ SVerticalBox::Slot().AutoHeight()
				[SequenceAction(LOCTEXT("Dialogue", "Create Dialogue Set"), SNew(SButton)
					.IsEnabled_Lambda([this]() { const auto* Shot = Session->GetShot(); return Shot && Shot->Targets.Num() == 2; })
					.ToolTipText(LOCTEXT("DialogueTip", "Requires two subjects. Creates a two-shot and two reverse shoulder clips."))
					.OnClicked_Lambda([this]() { return CreateSequenceShot(true); }))]]]]
			+ SVerticalBox::Slot().AutoHeight()
			[SNew(SVerticalBox).Tag(FName(TEXT("ShotEditor.BoundActions")))
				.Visibility_Lambda([this]() { return GetBoundClip() ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[SubsectionHeader(LOCTEXT("ClipExistingActions", "Navigate / Duplicate"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[SequenceAction(LOCTEXT("GoToClip", "Go to Clip"), SNew(SButton).Tag(FName(TEXT("ShotEditor.GoToClip")))
				.ToolTipText(LOCTEXT("GoToClipTip", "Select this clip and frame its time range in Level Sequence."))
				.IsEnabled_Lambda([this]() { return GetBoundClip() != nullptr && !Session->IsEditing(); })
				.OnClicked(this, &SShotEditorAuthoringPanel::GoToClip))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[SNew(SBox).Visibility_Lambda([this]() { return Session->GetInstanceTrack() ? EVisibility::Visible : EVisibility::Collapsed; })
				[SequenceAction(LOCTEXT("SetCameraCut", "Set Camera Cut"), SNew(SButton).Tag(FName(TEXT("ShotEditor.SetCameraCut")))
					.IsEnabled_Lambda([this]()
					{
						const auto Seq = Session->GetSequencer(); const auto* Owner = Session->GetInstanceTrack(); const auto* Clip = GetBoundClip(); FString Reason;
						return !Session->IsEditing() && Seq && Owner && Clip && ComposableCameraSystem::CameraInstanceAuthoring::CanSetCameraCut(Seq.ToSharedRef(), *Owner, *Clip, Reason);
					})
					.ToolTipText_Lambda([this]()
					{
						const auto Seq = Session->GetSequencer(); const auto* Owner = Session->GetInstanceTrack(); const auto* Clip = GetBoundClip(); FString Reason;
						if (Seq && Owner && Clip && !ComposableCameraSystem::CameraInstanceAuthoring::CanSetCameraCut(Seq.ToSharedRef(), *Owner, *Clip, Reason)) return FText::FromString(Reason);
						return LOCTEXT("SetCameraCutTip", "Set this instance's camera over the frame range shown above. Replaces overlapping Cuts, preserves their outside ranges. One Undo; no new instance or playhead change.");
					})
					.OnClicked(this, &SShotEditorAuthoringPanel::SetCameraCut))]]
			+ SVerticalBox::Slot().AutoHeight()
			[SequenceAction(LOCTEXT("DuplicateAtPlayhead", "Duplicate At Playhead"), SNew(SButton).Tag(FName(TEXT("ShotEditor.DuplicateAtPlayhead")))
				.ToolTipText(LOCTEXT("DuplicateAtPlayheadTip", "Duplicate this clip's configuration, Subjects and animation at the current playhead."))
				.IsEnabled_Lambda([this]()
				{
					const auto* Clip = GetBoundClip();
					const auto* Scene = Clip ? Clip->GetTypedOuter<UMovieScene>() : nullptr;
					const auto Seq = Session->GetSequencer();
					return Clip && Clip->HasStartFrame() && Clip->HasEndFrame() && !Clip->IsLocked()
						&& Scene && !Scene->IsReadOnly() && (!Seq || !Seq->IsReadOnly()) && !Session->IsEditing()
						&& GEditor && !GEditor->IsTransactionActive();
				})
				.OnClicked(this, &SShotEditorAuthoringPanel::DuplicateAtPlayhead))]
			+ SVerticalBox::Slot().AutoHeight()
			[SNew(SBox).Padding(0.f, 8.f, 0.f, 0.f)
				.Visibility_Lambda([this]() { return Session->GetSection() ? EVisibility::Visible : EVisibility::Collapsed; })
				[SequenceAction(LOCTEXT("Duplicate", "Duplicate after current"), SNew(SButton)
					.IsEnabled_Lambda([this]() { return Session->GetSection() && Session->CanEdit() && Session->GetSequencer().IsValid(); })
					.OnClicked(this, &SShotEditorAuthoringPanel::DuplicateShot))]]])];
}

UMovieSceneSection* SShotEditorAuthoringPanel::GetBoundClip() const
{
	if (Session->IsSequenceDraft()) return nullptr;
	UMovieSceneSection* Clip = Session->GetSection();
	if (Clip)
	{
		const auto* Track = Clip->GetTypedOuter<UMovieSceneTrack>();
		const auto* Scene = Clip->GetTypedOuter<UMovieScene>();
		const auto* Binding = Scene && Track ? Scene->FindBinding(Track->FindObjectBindingGuid()) : nullptr;
		return Track && Track->HasSection(*Clip) && Scene && (Scene->GetTracks().Contains(Track)
			|| (Binding && Binding->GetTracks().Contains(Track))) ? Clip : nullptr;
	}
	if (const auto* Track = Session->GetInstanceTrack(); Track && Track->IsInMovieScene())
	{
		if (const auto Sequencer = Session->GetSequencer())
			Clip = UMovieSceneComposableCameraInstanceTrack::FindActiveSection(*Track, Sequencer->GetLocalTime().Time);
		// Outside all activation intervals, retain the instance's first clip duration.
		if (!Clip && !Track->GetAllSections().IsEmpty()) Clip = Track->GetAllSections()[0];
	}
	return Clip;
}

float SShotEditorAuthoringPanel::GetClipDuration() const
{
	const auto* Clip = GetBoundClip();
	const auto* Scene = Clip ? Clip->GetTypedOuter<UMovieScene>() : nullptr;
	if (Scene && Clip->HasStartFrame() && Clip->HasEndFrame())
	{
		const int64 Frames = static_cast<int64>(Clip->GetExclusiveEndFrame().Value) - Clip->GetInclusiveStartFrame().Value;
		if (Frames > 0) return static_cast<float>(Frames / Scene->GetTickResolution().AsDecimal());
	}
	return 5.f;
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::PresetActions()
{
	const TSharedRef<SWidget> ApplyControls = Section(LOCTEXT("ApplyPresetSection", "Apply / restore"), SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[SNew(STextBlock).Text(LOCTEXT("Preset", "Reusable shot preset"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[SNew(SObjectPropertyEntryBox).Tag(FName(TEXT("ShotEditor.Preset"))).AllowedClass(UComposableCameraShotAsset::StaticClass())
				.IsEnabled_Lambda([this]() { return Session->CanEdit(); })
				.ObjectPath_Lambda([this]() { return Preset.ToSoftObjectPath().ToString(); })
				.ToolTipText(LOCTEXT("PresetTip", "Choosing a preset applies its composition to the current subjects."))
				.OnObjectChanged_Lambda([this](const FAssetData& Asset) { Preset = Cast<UComposableCameraShotAsset>(Asset.GetAsset()); if (Preset.IsValid()) { FString Reason; Session->ApplyPreset(Preset.Get(), Reason); SetMessage(Reason); } })]
			+ SVerticalBox::Slot().AutoHeight()
			[ComposableCameraSystem::ShotEditorStyle::Action(SNew(SButton).Text(LOCTEXT("RestorePreset", "Restore Preset"))
				.IsEnabled_Lambda([this]() { return Session->CanEdit() && !Preset.IsNull(); })
				.OnClicked_Lambda([this]() { FString Reason; Session->ApplyPreset(Preset.LoadSynchronous(), Reason); SetMessage(Reason); return FReply::Handled(); }))]);
	return SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(24.f, 16.f))
		+ SWrapBox::Slot()[ApplyControls]
		+ SWrapBox::Slot()
		[Section(LOCTEXT("SavePresetSection", "Save composition"), SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f).HAlign(HAlign_Left)
			[ComposableCameraSystem::ShotEditorStyle::Action(SNew(SButton).Text(LOCTEXT("SavePreset", "Save as Preset"))
				.IsEnabled_Lambda([this]() { return Session->GetShot() != nullptr; })
				.OnClicked_Lambda([this]() { if (auto* Saved = Session->SavePreset()) Preset = Saved; return FReply::Handled(); }))]
			+ SVerticalBox::Slot().AutoHeight()
			[SNew(STextBlock).Text(LOCTEXT("PresetGuide", "Keep composition and preview meshes for reuse. Applying a preset preserves your current subjects.")).AutoWrapText(true)])];
}

void SShotEditorAuthoringPanel::SetMessage(const FString& Reason) { Message = FText::FromString(Reason); }
FReply SShotEditorAuthoringPanel::NewSequenceDraft()
{
	UObject* PreviousHost = Session->GetHost();
	CreateDraft(FComposableCameraShot(), LOCTEXT("EmptyDraft", "Shot Draft"));
	if (Session->GetHost() != PreviousHost)
	{
		SelectedTemplateIndex = INDEX_NONE;
		RebuildTemplates();
	}
	return FReply::Handled();
}

FReply SShotEditorAuthoringPanel::CreateDraft(const FComposableCameraShot& Shot, const FText& Name)
{
	if (!ConfirmReplaceDraft()) return FReply::Handled();
	auto& Module = FModuleManager::LoadModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor");
	ULevelSequence* Target = Session->GetSequence() ? Cast<ULevelSequence>(Session->GetSequence()) : Sequence.LoadSynchronous();
	if (!Target)
	{
		const auto Live = Module.GetLiveSequencers();
		if (Live.Num()) Target = Cast<ULevelSequence>(Live[0]->GetFocusedMovieSceneSequence());
	}
	auto Seq = Target ? Module.FindOpenSequencerForSequence(Target) : nullptr;
	if (Target && !Seq && GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Target);
		Seq = Module.FindOpenSequencerForSequence(Target);
	}
	FString Reason;
	if (Target && (!Seq || Seq->GetFocusedMovieSceneSequence() != Target)) Reason = TEXT("Open and focus the selected Level Sequence first.");
	else if (Session->StartTemplateDraft(Shot, Name, Seq, Reason))
	{
		Sequence = Target;
		ShowSubjects();
	}
	SetMessage(Reason);
	return FReply::Handled();
}
TSharedPtr<ISequencer> SShotEditorAuthoringPanel::OpenClipSequencer()
{
	if (!GetBoundClip() || Session->IsEditing()) return nullptr;
	auto* Source = Session->GetSequence();
	auto Sequencer = Session->GetSequencer();
	if (!Sequencer && GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Source);
		Sequencer = Session->GetSequencer();
	}
	if (!Sequencer || Sequencer->GetFocusedMovieSceneSequence() != Source)
	{ SetMessage(TEXT("Open and focus this clip's Level Sequence first.")); return nullptr; }
	return Sequencer;
}

FReply SShotEditorAuthoringPanel::GoToClip()
{
	const auto Sequencer = OpenClipSequencer();
	auto* Clip = GetBoundClip();
	if (!Sequencer || !Clip) return FReply::Handled();
	Sequencer->EmptySelection();
	Sequencer->SelectTrack(Clip->GetTypedOuter<UMovieSceneTrack>());
	Sequencer->SelectSection(Clip);
	Sequencer->ThrobSectionSelection();
	if (Clip->HasStartFrame() && Clip->HasEndFrame())
	{
		const auto Rate = Sequencer->GetFocusedTickResolution();
		const double Start = Rate.AsSeconds(Clip->GetInclusiveStartFrame());
		const double End = Rate.AsSeconds(Clip->GetExclusiveEndFrame());
		const double Margin = FMath::Max(.1, (End - Start) * .1);
		if (End > Start) Sequencer->SetViewRange(TRange<double>(Start - Margin, End + Margin), EViewRangeInterpolation::Immediate);
	}
	Message = FText::GetEmpty();
	return FReply::Handled();
}

FReply SShotEditorAuthoringPanel::SetCameraCut()
{
	const auto Seq = OpenClipSequencer(); auto* Owner = Session->GetInstanceTrack(); auto* Clip = GetBoundClip();
	if (!Seq || !Owner || !Clip || Session->IsEditing()) return FReply::Handled();
	FString Reason;
	if (ComposableCameraSystem::CameraInstanceAuthoring::SetCameraCut(Seq.ToSharedRef(), *Owner, *Clip, Reason))
		Message = LOCTEXT("CameraCutSet", "Camera Cut now covers this clip's displayed range.");
	else SetMessage(Reason);
	return FReply::Handled();
}

FReply SShotEditorAuthoringPanel::DuplicateAtPlayhead()
{
	const auto Sequencer = OpenClipSequencer();
	auto* Clip = GetBoundClip();
	if (!Sequencer || !Clip) return FReply::Handled();
	FString Reason;
	const FFrameNumber Start = Sequencer->GetLocalTime().Time.FloorToFrame();
	if (auto* Track = Session->GetInstanceTrack())
	{
		if (auto* Copy = ComposableCameraSystem::CameraInstanceAuthoring::DuplicateAtTime(Sequencer.ToSharedRef(), *Track, *Clip, Start, Reason))
		{
			Sequencer->ForceEvaluate();
			Session->Bind(Copy->Instance->bComposition ? &Copy->Instance->Shot : nullptr, Copy->Instance);
		}
	}
	else if (auto* Legacy = Session->GetSection())
	{
		if (auto* Copy = DuplicateInSequence(Sequencer.ToSharedRef(), *Legacy, Reason, Start))
			Session->Bind(Copy->ResolveShotEditorShot(), Copy);
	}
	SetMessage(Reason);
	return FReply::Handled();
}
FReply SShotEditorAuthoringPanel::DuplicateShot()
{
	if (auto Sequencer = Session->GetSequencer(); Sequencer && Session->GetSection())
	{
		FString Reason;
		if (auto* Section = DuplicateInSequence(Sequencer.ToSharedRef(), *Session->GetSection(), Reason)) FComposableCameraShotEditor::OpenForShotSection(Section);
		else SetMessage(Reason);
	}
	return FReply::Handled();
}
FReply SShotEditorAuthoringPanel::CreateSequenceShot(bool bDialogue)
{
	ULevelSequence* TargetSequence = Cast<ULevelSequence>(Session->GetSequence());
	if (!TargetSequence) TargetSequence = Sequence.LoadSynchronous();
	if (!TargetSequence || !Session->GetShot()) { SetMessage(TEXT("Open a Level Sequence and assign subjects first.")); return FReply::Handled(); }
	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(TargetSequence);
	auto Sequencer = FModuleManager::LoadModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor").FindOpenSequencerForSequence(TargetSequence);
	if (!Sequencer) { SetMessage(TEXT("The selected sequence is not open.")); return FReply::Handled(); }
	if (Sequencer->GetFocusedMovieSceneSequence() != TargetSequence) { SetMessage(TEXT("Focus the selected sequence in Sequencer before adding a shot.")); return FReply::Handled(); }
	FComposableCameraShot Shot = *Session->GetShot();
	for (int32 Index = 0; Index < Shot.Targets.Num(); ++Index) Shot.Targets[Index].Target.Actor = Session->ResolveTarget(Index);
	FString Reason;
	if (auto* Section = CreateInSequence(Sequencer.ToSharedRef(), Shot, LOCTEXT("LegacyShotName", "Camera Shot"), GetClipDuration(), bDialogue, true, Session->GetSection(), Reason))
		{ FComposableCameraShotEditor::OpenForShotSection(Section); Message = LOCTEXT("Created", "Created and bound. Scrub or play to inspect the shot."); }
	else SetMessage(Reason);
	return FReply::Handled();
}
#undef LOCTEXT_NAMESPACE
