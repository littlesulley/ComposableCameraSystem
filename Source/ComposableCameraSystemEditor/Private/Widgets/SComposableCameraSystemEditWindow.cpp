// Copyright 2026 Sulley. All Rights Reserved.

#include "Widgets/SComposableCameraSystemEditWindow.h"
#include "Widgets/SComposableCameraLiveEditPanel.h"
#include "Cameras/ComposableCameraCameraBase.h"

#include "Editors/ComposableCameraShotEditor.h"
#include "Utilities/ComposableCameraMeshLayerTool.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformProcess.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SComposableCameraSystemEditWindow"

namespace
{
	int32 GetCommonControlPriority(const FString& Name)
	{
		// Presentation order only; common controls keep their existing console semantics.
		static const TCHAR* Names[] = {
			TEXT("CCS.Debug.Viewport"),
			TEXT("CCS.Debug.Viewport.Nodes.All"),
			TEXT("CCS.Debug.Viewport.Transitions.All"),
			TEXT("CCS.Debug.Panel"),
			TEXT("CCS.Debug.Panel.PoseHistory"),
			TEXT("CCS.Debug.Panel.PoseHistory.Freeze")
		};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
		{
			if (Name == Names[Index]) return Index;
		}
		return MAX_int32;
	}
}

void SComposableCameraSystemEditWindow::Construct(const FArguments& /*InArgs*/)
{
	for (uint8 Index = 0; Index < static_cast<uint8>(EComposableCameraConsoleControlGroup::Count); ++Index)
	{
		CollapsedGroups.Add(static_cast<EComposableCameraConsoleControlGroup>(Index));
	}
	ChildSlot
	[
		SNew(SBorder).Padding(8).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4))
				+ SWrapBox::Slot()[BuildPageButton(LOCTEXT("Welcome", "Welcome"), 0)]
				+ SWrapBox::Slot()[BuildPageButton(LOCTEXT("Debugging", "Debugging"), 1)]
				+ SWrapBox::Slot()[BuildPageButton(LOCTEXT("LiveEditing", "Live Editing"), 2)]
			]
			+ SVerticalBox::Slot().FillHeight(1)
			[
				SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]() { return ActivePageIndex; })
				+ SWidgetSwitcher::Slot()[BuildWelcomePage()]
				+ SWidgetSwitcher::Slot()[BuildDebuggingPage()]
				+ SWidgetSwitcher::Slot()[SAssignNew(LiveEditingPanel, SComposableCameraLiveEditPanel)]
			]
		]
	];
}

TSharedRef<SWidget> SComposableCameraSystemEditWindow::BuildPageButton(const FText& Label, int32 PageIndex)
{
	return SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox").Padding(FMargin(10, 5))
		.IsChecked_Lambda([this, PageIndex]()
		{
			return ActivePageIndex == PageIndex ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this, PageIndex](ECheckBoxState State)
		{
			if (State == ECheckBoxState::Checked) SwitchPage(PageIndex);
		})
		[SNew(STextBlock).Text(Label)];
}

void SComposableCameraSystemEditWindow::SwitchPage(int32 PageIndex)
{
	if (PageIndex == 1 && ActivePageIndex != 1) RefreshControls();
	ActivePageIndex = PageIndex;
}

void SComposableCameraSystemEditWindow::OpenLiveEditing(AComposableCameraCameraBase* Camera)
{
	SwitchPage(2);
	LiveEditingPanel->SelectCamera(Camera);
}

TSharedRef<SWidget> SComposableCameraSystemEditWindow::BuildWelcomePage()
{
	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 8, 4, 6)
			[
				SNew(STextBlock).AutoWrapText(true).Font(FAppStyle::GetFontStyle("HeadingMedium"))
				.Text(LOCTEXT("WelcomeTitle", "Welcome to Composable Camera System"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 14)
			[
				SNew(STextBlock).AutoWrapText(true)
				.Text(LOCTEXT("Introduction", "Build reusable cameras by composing behaviors. Author shots, blend camera modes, and inspect your results in Unreal Engine."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 6)
			[SNew(STextBlock).Font(FAppStyle::GetFontStyle("BoldFont")).Text(LOCTEXT("Resources", "Learn & explore"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				BuildResourceLink(LOCTEXT("Documentation", "Documentation"),
					LOCTEXT("DocumentationDescription", "Start here for setup, concepts, authoring guides and API reference."),
					TEXT("https://sulley.cc/ComposableCameraSystem-Docs/"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				BuildResourceLink(LOCTEXT("GitHub", "GitHub"),
					LOCTEXT("GitHubDescription", "Explore the plugin source and project repository."),
					TEXT("https://github.com/littlesulley/ComposableCameraSystem"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				BuildResourceLink(LOCTEXT("YouTube", "YouTube Tutorial"),
					LOCTEXT("YouTubeDescription", "Watch the video tutorial on YouTube."),
					TEXT("https://www.youtube.com/watch?v=yAWaHS36mmw"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				BuildResourceLink(LOCTEXT("Bilibili", "Bilibili Tutorial"),
					LOCTEXT("BilibiliDescription", "Watch the video tutorial on Bilibili."),
					TEXT("https://www.bilibili.com/video/BV1s8EF6tEZp/"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 0, 4, 8)
			[
				SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("WelcomeNext", "Open Shot Editor to compose a shot. Choose Debugging above for live camera HUD, gizmos and inspection tools."))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(4, 0, 4, 4)
			[
				SNew(SButton).Text(LOCTEXT("ShotEditor", "Open Shot Editor"))
				.OnClicked_Lambda([]()
				{
					FGlobalTabmanager::Get()->TryInvokeTab(FTabId(FComposableCameraShotEditor::TabId));
					return FReply::Handled();
				})
			]
		];
}

TSharedRef<SWidget> SComposableCameraSystemEditWindow::BuildResourceLink(
	const FText& Label, const FText& Description, const FString& Url)
{
	return SNew(SBorder).Padding(10).BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
			[
				SNew(SHyperlink).Text(Label).ToolTipText(FText::FromString(Url))
				.OnNavigate_Lambda([Url]() { FPlatformProcess::LaunchURL(*Url, nullptr, nullptr); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[SNew(STextBlock).AutoWrapText(true).Text(Description).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
		];
}

TSharedRef<SWidget> SComposableCameraSystemEditWindow::BuildDebuggingPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("Scope", "Switches apply across all PIE worlds. Expand a section to inspect its controls."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SSearchBox).HintText(LOCTEXT("SearchHint", "Search controls, command names or help"))
			.OnTextChanged_Lambda([this](const FText& Text) { SearchText = Text.ToString(); RebuildGroups(); })
		]
		+ SVerticalBox::Slot().FillHeight(1)
		[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(GroupsBox, SVerticalBox)]]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return Status; })]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton).Text(LOCTEXT("OutputLog", "Output Log"))
				.OnClicked_Lambda([]()
				{
					FGlobalTabmanager::Get()->TryInvokeTab(FTabId(TEXT("OutputLog")));
					return FReply::Handled();
				})
			]
		];
}

void SComposableCameraSystemEditWindow::RefreshControls()
{
	Controls = FComposableCameraConsoleControls::Discover();
	Controls.Sort([](const FComposableCameraConsoleControl& A, const FComposableCameraConsoleControl& B)
	{
		if (A.Group != B.Group) return A.Group < B.Group;
		const int32 PriorityA = GetCommonControlPriority(A.Name);
		const int32 PriorityB = GetCommonControlPriority(B.Name);
		return PriorityA == PriorityB ? A.Name < B.Name : PriorityA < PriorityB;
	});
	Status = FText::Format(LOCTEXT("Discovered", "{0} controls"), FText::AsNumber(Controls.Num() + 1));
	RebuildGroups();
}

void SComposableCameraSystemEditWindow::RebuildGroups()
{
	if (!GroupsBox.IsValid()) return;
	GroupsBox->ClearChildren();
	int32 VisibleCount = 0;
	for (uint8 Index = 0; Index < static_cast<uint8>(EComposableCameraConsoleControlGroup::Count); ++Index)
	{
		const EComposableCameraConsoleControlGroup Group = static_cast<EComposableCameraConsoleControlGroup>(Index);
		TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		if (Group == EComposableCameraConsoleControlGroup::RuntimeCommands)
		{
			Rows->AddSlot().AutoHeight().Padding(6, 3)
			[
				SNew(SComboButton).OnGetMenuContent(this, &SComposableCameraSystemEditWindow::BuildWorldMenu)
				.ToolTipText(LOCTEXT("WorldScope", "Target world for runtime inspection commands only."))
				.Visibility_Lambda([this]() { return ShouldShowWorldSelector() ? EVisibility::Visible : EVisibility::Collapsed; })
				.ButtonContent()[SNew(STextBlock).Text(this, &SComposableCameraSystemEditWindow::GetWorldLabel)]
			];
			Rows->AddSlot().AutoHeight().Padding(6, 3)
			[
				SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Visibility_Lambda([this]() { return ShouldShowWorldSelector() ? EVisibility::Collapsed : EVisibility::Visible; })
				.Text_Lambda([this]()
				{
					const UWorld* World = ResolveWorld();
					return World && World->IsGameWorld()
						? FText::Format(LOCTEXT("RuntimeTarget", "Target: {0}"), FText::FromString(World->GetName()))
						: LOCTEXT("StartPIE", "Start PIE to inspect a running CCS camera.");
				})
			];
		}
		if (Group == EComposableCameraConsoleControlGroup::Nodes || Group == EComposableCameraConsoleControlGroup::Transitions)
		{
			const FString AllName = Group == EComposableCameraConsoleControlGroup::Nodes
				? TEXT("CCS.Debug.Viewport.Nodes.All") : TEXT("CCS.Debug.Viewport.Transitions.All");
			Rows->AddSlot().AutoHeight().Padding(6, 3)
			[
				SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Visibility_Lambda([AllName]()
				{
					return !FComposableCameraConsoleControls::ReadToggle(TEXT("CCS.Debug.Viewport")).Get(false)
						|| FComposableCameraConsoleControls::ReadToggle(AllName).Get(false)
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				.Text_Lambda([AllName]()
				{
					if (!FComposableCameraConsoleControls::ReadToggle(TEXT("CCS.Debug.Viewport")).Get(false))
					{
						return LOCTEXT("GizmosHidden", "3D viewport debug is off. Enable it to see these gizmos.");
					}
					return FComposableCameraConsoleControls::ReadToggle(AllName).Get(false)
						? LOCTEXT("AllOverrides", "All gizmos is on. Individual Off switches cannot hide gizmos until All gizmos is off.")
						: FText::GetEmpty();
				})
			];
		}
		int32 Count = 0;
		for (const FComposableCameraConsoleControl& Control : Controls)
		{
			if (Control.Group != Group || (!SearchText.IsEmpty() && !Control.Name.Contains(SearchText)
				&& !Control.Label.Contains(SearchText) && !Control.Help.Contains(SearchText))) continue;
			Rows->AddSlot().AutoHeight().Padding(0, 1)[BuildControl(Control)];
			++Count;
		}
		if (Group == EComposableCameraConsoleControlGroup::Viewport)
		{
			const FText Label = LOCTEXT("ShowMeshLayers", "Show Mesh Layers");
			const FText Help = LOCTEXT("MeshLayersTooltip", "Toggle read-only mesh camera layer visualization in the editor and all PIE worlds. Shares the Tools menu switch and works independently of 3D viewport debug.");
			if (SearchText.IsEmpty() || FString(TEXT("ShowMeshLayers")).Contains(SearchText)
				|| Label.ToString().Contains(SearchText) || Help.ToString().Contains(SearchText))
			{
				Rows->AddSlot().AutoHeight().Padding(0, 1)
				[
					SNew(SBorder).Padding(FMargin(6, 3)).BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.ToolTipText(Help)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0, 0, 8, 0)
						[SNew(STextBlock).AutoWrapText(true).Text(Label).ColorAndOpacity(FLinearColor::White)
							.Font(FAppStyle::GetFontStyle("NormalFont"))]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SCheckBox)
							.IsChecked_Lambda([]()
							{
								return FComposableCameraMeshLayerTool::IsPreviewModeActive() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([](ECheckBoxState State)
							{
								if ((State == ECheckBoxState::Checked) != FComposableCameraMeshLayerTool::IsPreviewModeActive())
								{
									FComposableCameraMeshLayerTool::TogglePreviewMode();
								}
							})
							[SNew(STextBlock).Text_Lambda([]()
							{
								return FComposableCameraMeshLayerTool::IsPreviewModeActive() ? LOCTEXT("On", "On") : LOCTEXT("Off", "Off");
							})]
						]
					]
				];
				++Count;
			}
		}
		if (Count == 0) continue;
		VisibleCount += Count;
		GroupsBox->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(SExpandableArea).InitiallyCollapsed(SearchText.IsEmpty() && CollapsedGroups.Contains(Group))
			.HeaderPadding(FMargin(6, 4)).Padding(FMargin(2, 1)).AllowAnimatedTransition(false)
			.OnAreaExpansionChanged_Lambda([this, Group](bool bExpanded)
			{
				if (!SearchText.IsEmpty()) return;
				if (bExpanded) CollapsedGroups.Remove(Group); else CollapsedGroups.Add(Group);
			})
			.HeaderContent()
			[
				SNew(STextBlock).Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(FText::Format(LOCTEXT("GroupCount", "{0} ({1})"),
					FComposableCameraConsoleControls::GetGroupLabel(Group), FText::AsNumber(Count)))
			]
			.BodyContent()[Rows]
		];
	}
	if (VisibleCount == 0)
	{
		GroupsBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("NoMatches", "No matching CCS controls."))];
	}
}

TSharedRef<SWidget> SComposableCameraSystemEditWindow::BuildControl(const FComposableCameraConsoleControl& Control)
{
	const FString Name = Control.Name;
	const bool bCommand = Control.Kind == EComposableCameraConsoleControlKind::Command;
	TSharedPtr<SWrapBox> CommandInputs;
	TSharedRef<SWidget> Input = SNullWidget::NullWidget;
	switch (Control.Kind)
	{
	case EComposableCameraConsoleControlKind::Toggle:
		Input = SNew(SCheckBox)
			.IsChecked_Lambda([Name]()
			{
				const TOptional<bool> Value = FComposableCameraConsoleControls::ReadToggle(Name);
				return !Value.IsSet() ? ECheckBoxState::Undetermined : (Value.GetValue() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
			})
			.OnCheckStateChanged_Lambda([this, Name](ECheckBoxState State) { SetValue(Name, State == ECheckBoxState::Checked ? TEXT("1") : TEXT("0")); })
			[SNew(STextBlock).Text_Lambda([Name]()
			{
				const TOptional<bool> Value = FComposableCameraConsoleControls::ReadToggle(Name);
				return !Value.IsSet() ? LOCTEXT("Unavailable", "Unavailable") : (Value.GetValue() ? LOCTEXT("On", "On") : LOCTEXT("Off", "Off"));
			})];
		break;
	case EComposableCameraConsoleControlKind::Integer:
		Input = SNew(SNumericEntryBox<int32>).MinDesiredValueWidth(100).AllowSpin(true)
			.MinValue(Name == TEXT("CCS.Debug.Panel.Page") ? TOptional<int32>(0) : TOptional<int32>())
			.Value_Lambda([Name]() -> TOptional<int32>
			{
				const TOptional<FString> Value = FComposableCameraConsoleControls::ReadValue(Name);
				return Value.IsSet() ? TOptional<int32>(FCString::Atoi(*Value.GetValue())) : TOptional<int32>();
			})
			.OnValueChanged_Lambda([this, Name](int32 Value) { SetValue(Name, FString::FromInt(Value)); })
			.OnValueCommitted_Lambda([this, Name](int32 Value, ETextCommit::Type) { SetValue(Name, FString::FromInt(Value)); });
		break;
	case EComposableCameraConsoleControlKind::Float:
	{
		const bool bPanelWidth = Name == TEXT("CCS.Debug.Panel.Width");
		const bool bHistoryWidth = Name == TEXT("CCS.Debug.Panel.PoseHistory.Width");
		Input = SNew(SNumericEntryBox<float>).MinDesiredValueWidth(100).AllowSpin(true)
			.MinValue(bPanelWidth || bHistoryWidth ? TOptional<float>(0.15f) : TOptional<float>())
			.MaxValue(bPanelWidth ? TOptional<float>(0.6f) : (bHistoryWidth ? TOptional<float>(0.5f) : TOptional<float>()))
			.Value_Lambda([Name]() -> TOptional<float>
			{
				const TOptional<FString> Value = FComposableCameraConsoleControls::ReadValue(Name);
				return Value.IsSet() ? TOptional<float>(FCString::Atof(*Value.GetValue())) : TOptional<float>();
			})
			.OnValueChanged_Lambda([this, Name](float Value) { SetValue(Name, FString::SanitizeFloat(Value)); })
			.OnValueCommitted_Lambda([this, Name](float Value, ETextCommit::Type) { SetValue(Name, FString::SanitizeFloat(Value)); });
		break;
	}
	case EComposableCameraConsoleControlKind::Text:
		Input = SNew(SEditableTextBox).MinDesiredWidth(160)
			.Text_Lambda([Name]() { return FText::FromString(FComposableCameraConsoleControls::ReadValue(Name).Get(FString())); })
			.OnTextCommitted_Lambda([this, Name](const FText& Value, ETextCommit::Type) { SetValue(Name, Value.ToString()); });
		break;
	case EComposableCameraConsoleControlKind::Command:
		CommandInputs = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 2));
		CommandInputs->AddSlot()
		[
			SNew(SEditableTextBox).MinDesiredWidth(160).HintText(LOCTEXT("Arguments", "Arguments (optional)"))
			.Text(FText::FromString(CommandArguments.FindRef(Name)))
			.OnTextChanged_Lambda([this, Name](const FText& Text) { CommandArguments.FindOrAdd(Name) = Text.ToString(); })
		];
		Input = SNew(SButton).Text(LOCTEXT("Run", "Run"))
			.OnClicked_Lambda([this, Control]() { return ExecuteCommand(Control); });
		break;
	}
	Input->SetEnabled(TAttribute<bool>::CreateLambda([this, Control, bCommand]()
	{
		return bCommand ? FComposableCameraConsoleControls::CanExecute(Control, ResolveWorld())
			: FComposableCameraConsoleControls::CanSetValue(Control.Name);
	}));
	const FText Tooltip = FText::FromString(Name + TEXT("\n") + Control.Help
		+ (Control.bRequiresGameWorld ? TEXT("\nRequires a PIE/game world.") : TEXT("")));
	if (bCommand)
	{
		CommandInputs->AddSlot()[Input];
		return SNew(SBorder).Padding(FMargin(6, 4)).BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.ToolTipText(Tooltip)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
			[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Control.Label)).ColorAndOpacity(Control.Color)]
			+ SVerticalBox::Slot().AutoHeight()[CommandInputs.ToSharedRef()]
		];
	}
	const bool bCommon = GetCommonControlPriority(Name) != MAX_int32;
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0, 0, 8, 0)
		[
			SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Control.Label)).ColorAndOpacity(Control.Color)
			.Font(FAppStyle::GetFontStyle(bCommon ? "NormalFontBold" : "NormalFont"))
		];
	if (bCommon)
	{
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
		[
			SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(4, 1))
			.ToolTipText(LOCTEXT("CommonControlTooltip", "Frequently used debugging control."))
			[
				SNew(STextBlock).Text(LOCTEXT("CommonControl", "Common"))
				.Font(FAppStyle::GetFontStyle("SmallFont")).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
	}
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[Input];
	return SNew(SBorder).Padding(FMargin(6, 3)).BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.ToolTipText(Tooltip)[Row];
}

UWorld* SComposableCameraSystemEditWindow::ResolveWorld() const
{
	if (!bAutoWorld)
	{
		UWorld* World = SelectedWorld.Get();
		return World && !World->bIsTearingDown && GEngine && GEngine->GetWorldContextFromWorld(World) ? World : nullptr;
	}
	UWorld* EditorWorld = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!IsValid(World) || World->bIsTearingDown) continue;
			if (World->IsGameWorld()) return World;
			if (World->WorldType == EWorldType::Editor) EditorWorld = World;
		}
	}
	return EditorWorld;
}

FText SComposableCameraSystemEditWindow::GetWorldLabel() const
{
	const UWorld* World = ResolveWorld();
	return FText::Format(LOCTEXT("WorldLabel", "{0}: {1}"),
		bAutoWorld ? LOCTEXT("Auto", "Auto world") : LOCTEXT("World", "World"),
		World ? FText::FromString(World->GetName()) : LOCTEXT("NoWorld", "Unavailable"));
}

bool SComposableCameraSystemEditWindow::ShouldShowWorldSelector() const
{
	if (!bAutoWorld) return true; // Keep ended explicit selections visible so the user can return to Auto.
	int32 GameWorldCount = 0;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			const UWorld* World = Context.World();
			if (IsValid(World) && World->IsGameWorld() && !World->bIsTearingDown && ++GameWorldCount > 1) return true;
		}
	}
	return false;
}

TSharedRef<SWidget> SComposableCameraSystemEditWindow::BuildWorldMenu()
{
	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(LOCTEXT("AutoWorld", "Auto (PIE first)"), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SComposableCameraSystemEditWindow::SelectWorld, TWeakObjectPtr<UWorld>(), true)));
	// An ended explicit PIE selection stays unavailable until the user changes it.
	Menu.AddMenuSeparator();
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!IsValid(World) || World->bIsTearingDown || !World->IsGameWorld()) continue;
			const TWeakObjectPtr<UWorld> WeakWorld(World);
			const FString Label = FString::Printf(TEXT("%s | PIE %d"), *World->GetName(), Context.PIEInstance);
			Menu.AddMenuEntry(FText::FromString(Label), FText::GetEmpty(), FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SComposableCameraSystemEditWindow::SelectWorld, WeakWorld, false)));
		}
	}
	return Menu.MakeWidget();
}

void SComposableCameraSystemEditWindow::SelectWorld(TWeakObjectPtr<UWorld> World, bool bAutomatic)
{
	bAutoWorld = bAutomatic;
	SelectedWorld = World;
}

void SComposableCameraSystemEditWindow::SetValue(const FString& Name, const FString& Value)
{
	if (!FComposableCameraConsoleControls::SetValue(Name, Value))
	{
		Status = LOCTEXT("CannotWrite", "Control unavailable or read-only. Reopen the Debugging page to refresh.");
	}
}

FReply SComposableCameraSystemEditWindow::ExecuteCommand(const FComposableCameraConsoleControl& Control)
{
	const bool bDispatched = GLog && FComposableCameraConsoleControls::Execute(Control,
		CommandArguments.FindRef(Control.Name), ResolveWorld(), *GLog);
	Status = bDispatched
		? FText::Format(LOCTEXT("Invoked", "Invoked {0}. See Output Log for results."), FText::FromString(Control.Name))
		: LOCTEXT("DispatchFailed", "Command unavailable, or its required PIE/game world has ended.");
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
