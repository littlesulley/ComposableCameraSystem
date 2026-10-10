// Copyright 2026 Sulley. All Rights Reserved.
#include "Widgets/SShotEditorAuthoringPanel.h"
#include "Widgets/SShotEditorParameterPanel.h"
#include "Widgets/ComposableCameraShotEditorStyle.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "ComposableCameraSystemEditorModule.h"
#include "DataAssets/ComposableCameraShotAsset.h"
#include "Editor.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "Editors/ComposableCameraShotEditor.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISequencer.h"
#include "LevelSequence.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "Modules/ModuleManager.h"
#include "GameFramework/Actor.h"
#include "PropertyCustomizationHelpers.h"
#include "Selection.h"
#include "Sequencer/ComposableCameraShotAuthoring.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ShotEditorAuthoringPanel"
using namespace ComposableCameraSystem::ShotAuthoring;
namespace
{
TSharedRef<SWidget> ShotAuthoringPageScroll(TSharedRef<SWidget> Content)
{
	return SNew(SScrollBox) + SScrollBox::Slot().Padding(12.f)[Content];
}
}

void SShotEditorAuthoringPanel::Construct(const FArguments& Args)
{
	Session = Args._Session;
	AdvancedContent = Args._AdvancedContent;
	NotifyHook = Args._NotifyHook;
	for (uint8 Index = 0; Index <= static_cast<uint8>(EComposableShotTemplate::Group); ++Index)
		Templates.Add(MakeShared<EComposableShotTemplate>(static_cast<EComposableShotTemplate>(Index)));
	const auto Sequencers = FModuleManager::LoadModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor").GetLiveSequencers();
	if (Sequencers.Num()) Sequence = Cast<ULevelSequence>(Sequencers[0]->GetFocusedMovieSceneSequence());
	ChildSlot[SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Background).Padding(0.f)
		[SAssignNew(Body, SVerticalBox)]];
	UpdateGuideSelection();
	Rebuild();
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::BuildNavigation()
{
	using namespace ComposableCameraSystem::ShotEditorStyle;
	const auto PageTab = [this](EPage Page, const FText& Label, const FName Icon) -> TSharedRef<SWidget>
	{
		return SNew(SBox).HeightOverride(28.f)
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
		return SNew(SBox).HeightOverride(26.f)
			[SNew(SButton).ButtonStyle(TabStyle()).ContentPadding(FMargin(12.f, 3.f))
				.ButtonColorAndOpacity_Lambda([this, Section]() { return SecondaryTabColor(ActiveEditSection == Section); })
				.OnClicked_Lambda([this, Section]()
				{
					if (!Session->IsEditing() && (!GEditor || !GEditor->IsTransactionActive()))
					{
						ActiveEditSection = Section;
						UpdateGuideSelection();
					}
					return FReply::Handled();
				})
				[SNew(STextBlock).Text(Label).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FStyleColors::ForegroundHover)]];
	};
	const auto PreviewToggle = [this](bool bLevel, const FText& Label, float Width) -> TSharedRef<SWidget>
	{
		return SNew(SBox).WidthOverride(Width).HeightOverride(24.f)
			[SNew(SButton).ButtonStyle(TabStyle()).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(0.f)
				.Tag(bLevel ? FName(TEXT("ShotEditor.LevelPreview")) : FName(TEXT("ShotEditor.FollowPlayhead")))
				.ToolTipText(bLevel ? LOCTEXT("LevelPreviewTip", "Render the current level. Turn off for isolated preview.") : LOCTEXT("FollowTip", "Follow the active Shot in Sequencer."))
				.ButtonColorAndOpacity_Lambda([this, bLevel]() { return TabColor(bLevel ? Session->bUseLevelWorld : Session->bFollowPlayhead); })
				.OnClicked_Lambda([this, bLevel]()
				{
					if (bLevel) { Session->bUseLevelWorld = !Session->bUseLevelWorld; Session->RequestPreview(); }
					else Session->bFollowPlayhead = !Session->bFollowPlayhead;
					return FReply::Handled();
				})
				[SNew(STextBlock).Text(Label).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.Justification(ETextJustify::Center).ColorAndOpacity(FStyleColors::ForegroundHover)]];
	};
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
			[PreviewToggle(true, LOCTEXT("LevelPreview", "Level Preview"), 104.f)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
			[PreviewToggle(false, LOCTEXT("Follow", "Follow playhead"), 112.f)]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[SNew(SBorder).BorderImage(NavigationBrush()).Padding(0.f)
			.Visibility_Lambda([this]() { return ActivePage == EPage::Edit ? EVisibility::Visible : EVisibility::Collapsed; })
			[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.f, 4.f))
				+ SWrapBox::Slot()[EditTab(EEditSection::Follow, LOCTEXT("FollowTab", "Follow"))]
				+ SWrapBox::Slot()[EditTab(EEditSection::LookAt, LOCTEXT("AimTab", "Aim"))]
				+ SWrapBox::Slot()[EditTab(EEditSection::Lens, LOCTEXT("LensTab", "Lens & Focus"))]
				+ SWrapBox::Slot()[EditTab(EEditSection::Motion, LOCTEXT("MotionTab", "Motion"))]
				+ SWrapBox::Slot()[EditTab(EEditSection::Subjects, LOCTEXT("SubjectsTab", "Subjects"))]]];
}

void SShotEditorAuthoringPanel::UpdateGuideSelection()
{
	Session->bShowLookAtGuide = ActivePage == EPage::Edit && ActiveEditSection == EEditSection::LookAt;
	Session->bShowOrbitGuide = ActivePage == EPage::Edit && ActiveEditSection == EEditSection::Follow;
	Session->bShowSubjectGuide = ActivePage == EPage::Create || (ActivePage == EPage::Edit && ActiveEditSection == EEditSection::Subjects);
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
	DisplayedActors.Reset();
	if (const auto* Shot = Session->GetShot())
		for (int32 Index = 0; Index < Shot->Targets.Num(); ++Index)
			DisplayedActors.Add(Session->ResolveTarget(Index));

	if (bPagesBuilt)
	{
		for (const auto& Panel : { FollowPanel, LookAtPanel, LensFocusPanel, MotionPanel }) Panel->RefreshSource(true);
		SyncSubjects(true);
		SyncSubjects(false);
		return;
	}
	bPagesBuilt = true;
#if WITH_DEV_AUTOMATION_TESTS
	++PageBuildCount;
#endif
	EditPageWidget = EditPage();
	// Switchers keep page widgets alive. Navigation does not rebuild controls,
	// rebind the source or disturb a numeric gesture.
	Body->AddSlot().FillHeight(1.f)
	[SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]() { return static_cast<int32>(ActivePage); })
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(CreatePage())]
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

TSharedPtr<SWidget> SShotEditorAuthoringPanel::GetCreateSubjectsForTesting() const
{
	return CreateSubjectsBox ? CreateSubjectsBox->GetChildren()->GetChildAt(0).ToSharedPtr() : nullptr;
}
#endif

TSharedRef<SWidget> SShotEditorAuthoringPanel::CreatePage()
{
	const TSharedRef<SWidget> TemplateControls = ComposableCameraSystem::ShotEditorStyle::Group(LOCTEXT("TemplateSection", "Shot template"), SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[SNew(SComboBox<TSharedPtr<EComposableShotTemplate>>).Tag(FName(TEXT("ShotEditor.Template"))).OptionsSource(&Templates)
				.OnGenerateWidget_Lambda([](TSharedPtr<EComposableShotTemplate> Value) { return SNew(STextBlock).Text(TemplateLabel(*Value)); })
				.OnSelectionChanged_Lambda([this](TSharedPtr<EComposableShotTemplate> Value, ESelectInfo::Type) { if (Value) SelectedTemplate = *Value; })
				[SNew(STextBlock).Text_Lambda([this]() { return TemplateLabel(SelectedTemplate); })]]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f).HAlign(HAlign_Left)
			[ComposableCameraSystem::ShotEditorStyle::Action(SNew(SButton).Text(LOCTEXT("Selection", "Use Selected Actors"))
				.ToolTipText(LOCTEXT("SelectionTip", "Assign the actors selected in the level and apply this template."))
				.OnClicked(this, &SShotEditorAuthoringPanel::UseSelection))]
			+ SVerticalBox::Slot().AutoHeight()
			[ComposableCameraSystem::ShotEditorStyle::Action(SNew(SButton).Text(LOCTEXT("Apply", "Apply Template"))
				.IsEnabled_Lambda([this]() { return Session->CanEdit(); })
				.ToolTipText(LOCTEXT("ApplyTip", "Apply this template to the current subjects. Existing composition values will change."))
				.OnClicked(this, &SShotEditorAuthoringPanel::ApplyTemplate))]);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)[TemplateControls]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[ComposableCameraSystem::ShotEditorStyle::GroupHeader(LOCTEXT("Subjects", "Subjects"))]
		+ SVerticalBox::Slot().AutoHeight()[SAssignNew(CreateSubjectsBox, SBox)[Subjects(true)]];
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::EditPage()
{
	if (!FollowPanel) SAssignNew(FollowPanel, SShotEditorParameterPanel).Session(Session).NotifyHook(NotifyHook).Section(EShotEditorParameterSection::Follow);
	if (!LookAtPanel) SAssignNew(LookAtPanel, SShotEditorParameterPanel).Session(Session).NotifyHook(NotifyHook).Section(EShotEditorParameterSection::LookAt);
	if (!LensFocusPanel) SAssignNew(LensFocusPanel, SShotEditorParameterPanel).Session(Session).NotifyHook(NotifyHook).Section(EShotEditorParameterSection::LensFocus);

	if (!MotionPanel) SAssignNew(MotionPanel, SShotEditorParameterPanel).Session(Session).NotifyHook(NotifyHook).Section(EShotEditorParameterSection::Motion);

	return SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]() { return static_cast<int32>(ActiveEditSection); })
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(FollowPanel.ToSharedRef())]
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(LookAtPanel.ToSharedRef())]
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(LensFocusPanel.ToSharedRef())]
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(MotionPanel.ToSharedRef())]
		+ SWidgetSwitcher::Slot()[ShotAuthoringPageScroll(SAssignNew(EditSubjectsBox, SBox)[Subjects()])];
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::Subjects(bool bCreatePage)
{
	auto& Rows = bCreatePage ? CreateSubjectRows : EditSubjectRows;
	Rows = SNew(SVerticalBox);
	Rows->SetEnabled(TAttribute<bool>::CreateLambda([this]() { return Session->CanEdit(); }));
	const FString Prefix = bCreatePage ? TEXT("ShotEditor.Create") : TEXT("ShotEditor.Edit");
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
	SyncSubjects(bCreatePage);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[Actions]
		+ SVerticalBox::Slot().AutoHeight()
		[SNew(STextBlock).Text(LOCTEXT("Pick", "Add a subject, then choose an actor or preview model.")).AutoWrapText(true)
			.Visibility_Lambda([this]() { const auto* Shot = Session->GetShot(); return !Shot || Shot->Targets.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })]
		+ SVerticalBox::Slot().AutoHeight()[Rows.ToSharedRef()];
}

void SShotEditorAuthoringPanel::SyncSubjects(bool bCreatePage)
{
	auto& Rows = bCreatePage ? CreateSubjectRows : EditSubjectRows;
	auto& Cards = bCreatePage ? CreateSubjectCards : EditSubjectCards;
	auto& Panels = bCreatePage ? CreateSubjectPanels : EditSubjectPanels;
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
			.Session(Session).NotifyHook(NotifyHook).Section(EShotEditorParameterSection::Subject).SubjectIndex(Index);
		else Panels[Index]->RefreshSource(true);
		if (Index >= Cards.Num())
		{
			const auto Card = SubjectCard(Index, bCreatePage);
			Cards.Add(Card);
			Rows->AddSlot().AutoHeight().HAlign(HAlign_Fill).Padding(0.f, 0.f, 0.f, 8.f)[Card];
		}
	}
}

TSharedRef<SWidget> SShotEditorAuthoringPanel::SubjectCard(int32 Index, bool bCreatePage)
{
	const auto& Panels = bCreatePage ? CreateSubjectPanels : EditSubjectPanels;
	const auto ExpandableGroup = [this](const FText& Label, const FString& Path, TSharedRef<SWidget> Content,
		bool bExpanded, bool bPrimary, TSharedPtr<SWidget> Header = nullptr) -> TSharedRef<SWidget>
	{
		ExpandedSubjectGroups.FindOrAdd(Path, bExpanded);
		return SNew(SExpandableArea).Tag(FName(*Path)).InitiallyCollapsed(!ExpandedSubjectGroups.FindRef(Path)).Padding(0.f)
			.BorderImage(bPrimary ? FAppStyle::GetBrush("WhiteBrush") : FCoreStyle::Get().GetBrush("ExpandableArea.Border"))
			.BorderBackgroundColor(bPrimary ? ComposableCameraSystem::ShotEditorStyle::HeaderColor() : FLinearColor::White)
			.BodyBorderImage(FAppStyle::GetBrush("NoBorder")).BodyBorderBackgroundColor(FLinearColor::Transparent)
			.HeaderPadding(bPrimary ? FMargin(0.f) : FMargin(4.f, 2.f))
			.OnAreaExpansionChanged_Lambda([this, Path](bool bOpen) { ExpandedSubjectGroups.Add(Path, bOpen); })
			.HeaderContent()[Header ? Header.ToSharedRef() : bPrimary ? ComposableCameraSystem::ShotEditorStyle::GroupHeader(Label) : ComposableCameraSystem::ShotEditorStyle::SubsectionHeader(Label)]
			.BodyContent()[Content];
	};
	const FText Label = Index == 0 ? LOCTEXT("A", "Subject A") : Index == 1 ? LOCTEXT("B", "Subject B")
		: FText::Format(LOCTEXT("N", "Subject {0}"), Index + 1);
	const FString Prefix = FString::Printf(TEXT("ShotEditor.%s.Targets[%d]"), bCreatePage ? TEXT("Create") : TEXT("Edit"), Index);
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
		[SNew(SObjectPropertyEntryBox).AllowedClass(AActor::StaticClass())
			.ObjectPath_Lambda([this, Index]() { AActor* Actor = Session->ResolveTarget(Index); return Actor ? Actor->GetPathName() : FString(); })
			.OnObjectChanged_Lambda([this, Index](const FAssetData& Asset)
			{ FString Reason; if (!Session->SetTargetActor(Index, Cast<AActor>(Asset.GetAsset()), Reason)) SetMessage(Reason); })]
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
	const TSharedRef<SWidget> DestinationControls = Section(LOCTEXT("DestinationSection", "Destination / duration"), SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[SNew(STextBlock).Text(LOCTEXT("SequenceAsset", "Level Sequence"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[SNew(SObjectPropertyEntryBox).Tag(FName(TEXT("ShotEditor.Destination"))).AllowedClass(ULevelSequence::StaticClass())
				.ObjectPath_Lambda([this]() { return !bSequenceOverride && Session->GetSequence() ? Session->GetSequence()->GetPathName() : Sequence.ToSoftObjectPath().ToString(); })
				.OnObjectChanged_Lambda([this](const FAssetData& Asset) { Sequence = Cast<ULevelSequence>(Asset.GetAsset()); bSequenceOverride = true; })]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("Duration", "Clip duration (s)"))]
				+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SNumericEntryBox<float>).MinDesiredValueWidth(90.f).Value_Lambda([this]() { return TOptional<float>(Duration); })
					.MinValue(.1f).MaxValue(3600.f).OnValueCommitted_Lambda([this](float Value, ETextCommit::Type) { Duration = FMath::Clamp(Value, .1f, 3600.f); })]]
			+ SVerticalBox::Slot().AutoHeight()
			[SNew(SCheckBox).IsChecked_Lambda([this]() { return bSequenceOwnedCamera ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState Value) { bSequenceOwnedCamera = Value == ECheckBoxState::Checked; })
				.ToolTipText(LOCTEXT("OwnedTip", "Create a camera owned by the sequence. Turn off to create a persistent level camera."))
				[SNew(STextBlock).Text(LOCTEXT("Owned", "Sequence-owned camera"))]]);
	return SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(24.f, 16.f))
		+ SWrapBox::Slot()[DestinationControls]
		+ SWrapBox::Slot()
		[Section(LOCTEXT("SequenceCreateSection", "Create clips"), SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f).HAlign(HAlign_Left)
			[ComposableCameraSystem::ShotEditorStyle::Action(SNew(SButton).Text(LOCTEXT("CreateSequence", "Add Shot to Sequence"))
				.IsEnabled_Lambda([this]() { return Session->GetShot() != nullptr; })
				.OnClicked_Lambda([this]() { return CreateSequenceShot(false); }))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f).HAlign(HAlign_Left)
			[ComposableCameraSystem::ShotEditorStyle::Action(SNew(SButton).Text(LOCTEXT("Dialogue", "Create Dialogue Set"))
				.IsEnabled_Lambda([this]() { const auto* Shot = Session->GetShot(); return Shot && Shot->Targets.Num() == 2; })
				.ToolTipText(LOCTEXT("DialogueTip", "Requires two subjects. Creates a two-shot and two reverse shoulder clips."))
				.OnClicked_Lambda([this]() { return CreateSequenceShot(true); }))]
			+ SVerticalBox::Slot().AutoHeight()
			[SNew(STextBlock).Text(LOCTEXT("SequenceGuide", "Creates camera, Shot Track, subject bindings and Camera Cuts. Current Shot: append after it.")).AutoWrapText(true)])]
		+ SWrapBox::Slot()
		[Section(LOCTEXT("CurrentClipSection", "Current clip"), SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f).HAlign(HAlign_Left)
			[ComposableCameraSystem::ShotEditorStyle::Action(SNew(SButton).Text(LOCTEXT("Duplicate", "Duplicate after current"))
				.IsEnabled_Lambda([this]() { return Session->GetSection() && Session->CanEdit() && Session->GetSequencer().IsValid(); })
				.OnClicked(this, &SShotEditorAuthoringPanel::DuplicateShot))]
			+ SVerticalBox::Slot().AutoHeight()
			[ComposableCameraSystem::ShotEditorStyle::Action(SNew(SButton).Text(LOCTEXT("Jump", "Jump to Shot"))
				.IsEnabled_Lambda([this]() { return Session->GetSection() && Session->GetSection()->HasStartFrame() && Session->GetSequencer().IsValid(); })
				.OnClicked(this, &SShotEditorAuthoringPanel::JumpToShot))])];
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
FReply SShotEditorAuthoringPanel::UseSelection()
{
	FString Reason; if (!Session->UseSelectedActors(SelectedTemplate, Reason)) SetMessage(Reason);
	else { ActivePage = EPage::Edit; UpdateGuideSelection(); Message = LOCTEXT("Selected", "Subjects assigned. Adjust composition, then add to Sequence."); }
	return FReply::Handled();
}
FReply SShotEditorAuthoringPanel::ApplyTemplate()
{
	FString Reason; if (!Session->ApplyTemplate(SelectedTemplate, Reason)) SetMessage(Reason);
	else { ActivePage = EPage::Edit; UpdateGuideSelection(); Message = FText::GetEmpty(); }
	return FReply::Handled();
}
FReply SShotEditorAuthoringPanel::JumpToShot()
{
	if (auto Sequencer = Session->GetSequencer(); Sequencer && Session->GetSection() && Session->GetSection()->HasStartFrame())
		{ Sequencer->SetLocalTimeDirectly(Session->GetSection()->GetInclusiveStartFrame()); Session->RequestPreview(); }
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
	ULevelSequence* TargetSequence = bSequenceOverride ? Sequence.LoadSynchronous() : Cast<ULevelSequence>(Session->GetSequence());
	if (!TargetSequence) TargetSequence = Sequence.LoadSynchronous();
	if (!TargetSequence || !Session->GetShot()) { SetMessage(TEXT("Choose a Level Sequence and subjects first.")); return FReply::Handled(); }
	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(TargetSequence);
	auto Sequencer = FModuleManager::LoadModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor").FindOpenSequencerForSequence(TargetSequence);
	if (!Sequencer) { SetMessage(TEXT("The selected sequence is not open.")); return FReply::Handled(); }
	if (Sequencer->GetFocusedMovieSceneSequence() != TargetSequence) { SetMessage(TEXT("Focus the selected sequence in Sequencer before adding a shot.")); return FReply::Handled(); }
	FComposableCameraShot Shot = *Session->GetShot();
	for (int32 Index = 0; Index < Shot.Targets.Num(); ++Index) Shot.Targets[Index].Target.Actor = Session->ResolveTarget(Index);
	FString Reason;
	if (auto* Section = CreateInSequence(Sequencer.ToSharedRef(), Shot, TemplateLabel(SelectedTemplate), Duration, bDialogue, bSequenceOwnedCamera, Session->GetSection(), Reason))
		{ FComposableCameraShotEditor::OpenForShotSection(Section); Message = LOCTEXT("Created", "Created and bound. Scrub or play to inspect the shot."); }
	else SetMessage(Reason);
	return FReply::Handled();
}
#undef LOCTEXT_NAMESPACE
