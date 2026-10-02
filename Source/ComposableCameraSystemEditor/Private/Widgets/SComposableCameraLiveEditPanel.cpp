// Copyright 2026 Sulley. All Rights Reserved.

#include "Widgets/SComposableCameraLiveEditPanel.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Editors/ComposableCameraNodeGraph.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Editors/ComposableCameraNodeGraphSchema.h"
#include "Editors/ComposableCameraOutputGraphNode.h"
#include "Editors/ComposableCameraStartGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "PropertyEditorModule.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SComposableCameraLiveEditPanel"

void SComposableCameraLiveEditPanel::Construct(const FArguments& /*InArgs*/)
{
	constexpr float ActionButtonWidth = 132.f;
	constexpr float ActionButtonHeight = 28.f;
	const FMargin ActionButtonPadding(8, 4);
	CameraLabel = LOCTEXT("ChooseCamera", "Choose PIE camera");
	RuntimeParameterLabel = LOCTEXT("RuntimeParameters", "Runtime Parameters");
	NodeChainGraph = NewObject<UComposableCameraNodeGraph>(GetTransientPackage(), NAME_None, RF_Transient);
	NodeChainGraph->Schema = UComposableCameraNodeGraphSchema::StaticClass();
	NodeChainGraph->bEditable = false;
	SGraphEditor::FGraphEditorEvents GraphEvents;
	GraphEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &SComposableCameraLiveEditPanel::OnNodeSelectionChanged);
	FGraphAppearanceInfo GraphAppearance;
	GraphAppearance.InstructionText = LOCTEXT("EmptyChain", "Choose a PIE camera with a connected Start chain.");
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &SComposableCameraLiveEditPanel::OnObjectPropertyChanged);
	PIEEndingHandle = FEditorDelegates::PrePIEEnded.AddRaw(this, &SComposableCameraLiveEditPanel::OnPIEEnding);
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("Intro", "Select a node to tune its runtime parameters. Apply to Asset keeps edited defaults; save the asset afterward."))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0, 0, 12, 0)
			[
				SNew(SComboButton).IsEnabled_Lambda([this]() { return !Session.HasChanges(); })
				.ToolTipText(LOCTEXT("CameraScope", "Apply or Reset pending values before switching PIE cameras."))
				.OnGetMenuContent(this, &SComposableCameraLiveEditPanel::BuildCameraMenu)
				.ButtonContent()[SNew(STextBlock).Text_Lambda([this]() { return CameraLabel; })]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(SBox).WidthOverride(ActionButtonWidth).HeightOverride(ActionButtonHeight)
				[
				SNew(SButton).ButtonColorAndOpacity(FLinearColor(0.025f, 0.24f, 0.62f))
				.Tag(TEXT("LiveEdit.ApplyToAsset")).HAlign(HAlign_Center).VAlign(VAlign_Center)
				.ForegroundColor(FLinearColor::White).ContentPadding(ActionButtonPadding).Text(LOCTEXT("Apply", "Apply to Asset"))
				.ToolTipText(LOCTEXT("ApplyTip", "Commit all edited node defaults in one Undo transaction. Trial stays active until Reset."))
				.OnClicked_Lambda([this]()
				{
					FString Reason;
					Session.ApplyToAsset(Reason);
					Status = FText::FromString(Reason);
					RefreshDescriptions();
					return FReply::Handled();
				})
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(ActionButtonWidth).HeightOverride(ActionButtonHeight)
				[
				SNew(SButton).Text(LOCTEXT("Reset", "Reset Trial")).ContentPadding(ActionButtonPadding)
				.Tag(TEXT("LiveEdit.ResetTrial")).HAlign(HAlign_Center).VAlign(VAlign_Center)
				.IsEnabled_Lambda([this]() { return Session.HasOverrides(); })
				.OnClicked_Lambda([this]()
				{
					Status = Session.Reset() ? LOCTEXT("ResetDone", "Trial cleared. Current graph / Modifier drivers restored.")
						: LOCTEXT("ResetBlocked", "Runtime node unavailable. Pending values retained.");
					for (const auto& Panel : NodePanels) Panel->Details->ForceRefresh();
					RefreshDescriptions();
					return FReply::Handled();
				})
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1)
		[
			SNew(SSplitter).Orientation(Orient_Vertical).PhysicalSplitterHandleSize(4).HitDetectionSplitterHandleSize(6)
			+ SSplitter::Slot().Value(0.35f).MinSize(140)
			[
				SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(6)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(4, 2, 4, 6)
					[SNew(STextBlock).Font(FAppStyle::GetFontStyle("NormalFontBold")).Text(LOCTEXT("NodeChain", "Node Chain"))]
					+ SVerticalBox::Slot().FillHeight(1)
					[
						SAssignNew(NodeChainEditor, SGraphEditor).GraphToEdit(NodeChainGraph).GraphEvents(GraphEvents)
						.IsEditable(false).ShowGraphStateOverlay(false).Appearance(GraphAppearance)
					]
				]
			]
			+ SSplitter::Slot().Value(0.65f).MinSize(160)
			[
				SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(6)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(4, 2, 4, 6)
					[SNew(STextBlock).AutoWrapText(true).Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.ToolTipText_Lambda([this]() { return PropertyDescriptions; })
						.Text_Lambda([this]() { return RuntimeParameterLabel; })]
					+ SVerticalBox::Slot().FillHeight(1)[SAssignNew(ParameterHost, SBox)]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return Status; })]
	];
	RebuildNodePanels();
	RefreshDescriptions();
}

SComposableCameraLiveEditPanel::~SComposableCameraLiveEditPanel()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	FEditorDelegates::PrePIEEnded.Remove(PIEEndingHandle);
	for (const auto& Panel : NodePanels)
	{
		Panel->Details->SetIsPropertyVisibleDelegate(FIsPropertyVisible());
		Panel->Details->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly());
		Panel->Details->SetObject(nullptr);
	}
}

void SComposableCameraLiveEditPanel::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(NodeChainGraph);
}

const FProperty* SComposableCameraLiveEditPanel::FindTopProperty(const FPropertyAndParent& Property)
{
	for (int32 Index = Property.ParentProperties.Num() - 1; Index >= 0; --Index)
		if (FComposableCameraLiveEditSession::IsLiveEditProperty(Property.ParentProperties[Index])) return Property.ParentProperties[Index];
	return &Property.Property;
}

TSharedRef<SWidget> SComposableCameraLiveEditPanel::BuildCameraMenu()
{
	FMenuBuilder Menu(true, nullptr);
	int32 Count = 0;
	if (GEngine)
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || World->WorldType != EWorldType::PIE || World->bIsTearingDown) continue;
			Menu.BeginSection(NAME_None, FText::FromString(World->GetName()));
			for (TActorIterator<AComposableCameraCameraBase> It(World); It; ++It)
			{
				if (!IsValid(*It) || !It->SourceTypeAsset) continue;
				const TWeakObjectPtr<AComposableCameraCameraBase> WeakCamera(*It);
				Menu.AddMenuEntry(FText::FromString(It->GetActorLabel()), FText::FromString(It->SourceTypeAsset->GetPathName()), FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &SComposableCameraLiveEditPanel::SelectCamera, WeakCamera)));
				++Count;
			}
			Menu.EndSection();
		}
	if (!Count) Menu.AddMenuEntry(LOCTEXT("NoCamera", "Start PIE and activate a camera"), FText(), FSlateIcon(), FUIAction());
	return Menu.MakeWidget();
}

void SComposableCameraLiveEditPanel::SelectCamera(TWeakObjectPtr<AComposableCameraCameraBase> Camera)
{
	if (Camera.IsValid() && Session.GetCamera() == Camera.Get()) return;
	if (!Session.BindCamera(Camera.Get())) { Status = LOCTEXT("CannotSwitch", "Camera unavailable, or pending defaults need Apply / Reset before switching."); return; }
	CameraLabel = FText::FromString(Camera->GetWorld()->GetName() + TEXT(" / ") + Camera->GetActorLabel());
	Status = FText();
	RebuildNodePanels();
	RefreshDescriptions();
}

void SComposableCameraLiveEditPanel::RebuildNodePanels()
{
	TGuardValue<bool> SelectionGuard(bUpdatingNodeSelection, true);
	NodeChainEditor->ClearSelectionSet();
	ParameterHost->SetContent(SNullWidget::NullWidget);
	for (const auto& Panel : NodePanels)
	{
		Panel->Details->SetIsPropertyVisibleDelegate(FIsPropertyVisible());
		Panel->Details->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly());
		Panel->Details->SetObject(nullptr);
	}
	NodePanels.Reset();
	SelectedPanelIndex = INDEX_NONE;
	NodeChainGraph->Nodes.Reset();
	const TArray<int32> NodeIndices = Session.GetNodeIndices();
	UEdGraphPin* PreviousOutput = nullptr;
	int32 NodeX = 40;
	const auto AddChainNode = [this, &NodeX](UComposableCameraGraphNodeBase* Node)
	{
		Node->CreateNewGuid();
		Node->NodePosX = NodeX;
		Node->NodePosY = 32;
		NodeChainGraph->AddNode(Node, false, false);
		const FSlateFontInfo& TitleFont = FAppStyle::GetWidgetStyle<FTextBlockStyle>("Graph.Node.NodeTitle").Font;
		const auto TitleWidth = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Node->GetNodeTitle(ENodeTitleType::FullTitle), TitleFont).X;
		NodeX += FMath::Max(220, FMath::CeilToInt(TitleWidth + 100)) + 40;
	};
	if (!NodeIndices.IsEmpty())
	{
		auto* Start = NewObject<UComposableCameraStartGraphNode>(NodeChainGraph, NAME_None, RF_Transient);
		Start->AllocateDefaultPins();
		AddChainNode(Start);
		PreviousOutput = Start->FindPin(UComposableCameraGraphNodeBase::PN_ExecOut, EGPD_Output);
	}
	for (int32 Index : NodeIndices)
	{
		auto* EditingNode = Session.GetEditingNode(Index);
		if (!EditingNode) continue;
		auto Panel = MakeShared<FNodePanel>();
		Panel->Index = Index;
		Panel->Label = UComposableCameraGraphNodeBase::GetCameraNodeDisplayNameForClass(EditingNode->GetClass());
		FDetailsViewArgs Args;
		Args.bAllowSearch = true;
		Args.bUpdatesFromSelection = false;
		Args.bLockable = false;
		Args.bHideSelectionTip = true;
		Args.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Show;
		Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		Panel->Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
		Panel->Details->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& Property)
		{ return FComposableCameraLiveEditSession::IsLiveEditProperty(FindTopProperty(Property)); }));
		const TWeakPtr<FNodePanel> WeakPanel = Panel;
		Panel->Details->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateLambda([WeakPanel](const FPropertyAndParent& Property)
		{
			auto Row = WeakPanel.Pin();
			return !Row || !Row->EditableProperties.Contains(FindTopProperty(Property)->GetFName());
		}));
		NodePanels.Add(Panel);
		Session.SelectNode(Index);
		for (TFieldIterator<FProperty> It(EditingNode->GetClass()); It; ++It)
		{
			FString Reason;
			if (Session.CanEditProperty(It->GetFName(), Reason)) Panel->EditableProperties.Add(It->GetFName());
		}
		Panel->Details->SetObject(EditingNode, true);
		auto* ChainNode = NewObject<UComposableCameraNodeGraphNode>(NodeChainGraph, NAME_None, RF_Transient);
		ChainNode->NodeTemplate = EditingNode;
		ChainNode->NodeIndex = Index;
		// This graph is an exec-only presentation. Parameters live in the lower section.
		FEdGraphPinType ExecType;
		ExecType.PinCategory = UEdGraphSchema_K2::PC_Exec;
		UEdGraphPin* Input = ChainNode->CreatePin(EGPD_Input, ExecType, UComposableCameraGraphNodeBase::PN_ExecIn);
		UEdGraphPin* Output = ChainNode->CreatePin(EGPD_Output, ExecType, UComposableCameraGraphNodeBase::PN_ExecOut);
		Input->PinFriendlyName = Output->PinFriendlyName = LOCTEXT("ChainExecLabel", "");
		if (PreviousOutput) PreviousOutput->MakeLinkTo(Input);
		PreviousOutput = Output;
		AddChainNode(ChainNode);
	}
	if (PreviousOutput)
	{
		auto* Output = NewObject<UComposableCameraOutputGraphNode>(NodeChainGraph, NAME_None, RF_Transient);
		Output->AllocateDefaultPins();
		PreviousOutput->MakeLinkTo(Output->FindPin(UComposableCameraGraphNodeBase::PN_ExecIn, EGPD_Input));
		AddChainNode(Output);
	}
	NodeChainGraph->NotifyGraphChanged();
	NodeChainEditor->SetViewLocation(FVector2f::ZeroVector, 1.f);
	if (!NodePanels.IsEmpty())
	{
		NodeChainEditor->SetNodeSelection(NodeChainGraph->Nodes[1], true);
		ShowNodeParameters(0);
	}
	else ShowNodeParameters(INDEX_NONE);
}

void SComposableCameraLiveEditPanel::OnNodeSelectionChanged(const FGraphPanelSelectionSet& Selection)
{
	if (bUpdatingNodeSelection) return;
	int32 PanelIndex = INDEX_NONE;
	UComposableCameraNodeGraphNode* SelectedNode = nullptr;
	for (UObject* Object : Selection)
	{
		auto* Node = Cast<UComposableCameraNodeGraphNode>(Object);
		if (!Node || Node->GetGraph() != NodeChainGraph) continue;
		const int32 Candidate = NodePanels.IndexOfByPredicate([Node](const auto& Panel) { return Panel->Index == Node->NodeIndex; });
		if (Candidate == INDEX_NONE) continue;
		PanelIndex = Candidate;
		SelectedNode = Node;
		if (Candidate != SelectedPanelIndex) break;
	}
	if (SelectedNode && Selection.Num() > 1)
	{
		TGuardValue<bool> SelectionGuard(bUpdatingNodeSelection, true);
		NodeChainEditor->ClearSelectionSet();
		NodeChainEditor->SetNodeSelection(SelectedNode, true);
	}
	ShowNodeParameters(PanelIndex);
}

void SComposableCameraLiveEditPanel::ShowNodeParameters(int32 PanelIndex)
{
	SelectedPanelIndex = NodePanels.IsValidIndex(PanelIndex) ? PanelIndex : INDEX_NONE;
	if (SelectedPanelIndex == INDEX_NONE)
	{
		RuntimeParameterLabel = LOCTEXT("RuntimeParameters", "Runtime Parameters");
		ParameterHost->SetContent(SNew(STextBlock).AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("SelectNode", "Select a camera node in the chain to inspect its parameters.")));
		return;
	}
	const auto& Panel = NodePanels[SelectedPanelIndex];
	if (Session.IsLive()) Session.SelectNode(Panel->Index);
	RuntimeParameterLabel = Panel->Label;
	ParameterHost->SetContent(Panel->Details.ToSharedRef());
	RefreshDescriptions();
}

void SComposableCameraLiveEditPanel::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	const FProperty* Property = Event.MemberProperty ? Event.MemberProperty : Event.Property;
	if (!Property) return;
	FName Name = Property->GetFName();
	const int32 Index = Session.FindEditingNode(Object, Name);
	if (Index == INDEX_NONE || !Session.SelectNode(Index)) return;
	FString Reason;
	Status = Session.ApplyProperty(Name, Reason) ? LOCTEXT("TrialApplied", "Trial active. Apply to Asset keeps defaults; Reset resumes current drivers.") : FText::FromString(Reason);
}

void SComposableCameraLiveEditPanel::OnPIEEnding(bool /*bSimulating*/)
{
	Session.DetachRuntime();
	Status = Session.HasChanges() ? LOCTEXT("PendingAfterPIE", "PIE ended. Pending defaults remain available for Apply to Asset.") : LOCTEXT("PIEEnded", "PIE ended. Choose a camera in the next session.");
	RefreshDescriptions();
}

void SComposableCameraLiveEditPanel::RefreshDescriptions()
{
	PropertyDescriptions = FText::FromString(Session.DescribeProperties());
	const bool bLive = Session.IsLive();
	for (const auto& Panel : NodePanels)
	{
		TSet<FName> Editable;
		if (bLive && Session.SelectNode(Panel->Index))
		{
			for (TFieldIterator<FProperty> It(Session.GetEditingNode(Panel->Index)->GetClass()); It; ++It)
			{
				FString Reason;
				if (Session.CanEditProperty(It->GetFName(), Reason)) Editable.Add(It->GetFName());
			}
		}
		if (Panel->EditableProperties.Difference(Editable).Num() || Editable.Difference(Panel->EditableProperties).Num())
		{
			Panel->EditableProperties = MoveTemp(Editable);
			Panel->Details->ForceRefresh();
		}
	}
	if (bLive && NodePanels.IsValidIndex(SelectedPanelIndex)) Session.SelectNode(NodePanels[SelectedPanelIndex]->Index);
}

void SComposableCameraLiveEditPanel::Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime)
{
	SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
	if (CurrentTime >= NextRefreshTime) { RefreshDescriptions(); NextRefreshTime = CurrentTime + 0.25; }
}
#undef LOCTEXT_NAMESPACE
