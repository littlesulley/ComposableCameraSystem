// Copyright Sulley. All rights reserved.

#include "Toolkits/ComposableCameraTypeAssetEditorToolkit.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Editors/ComposableCameraNodeGraph.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Editors/ComposableCameraVariableGraphNode.h"
#include "Editors/ComposableCameraNodeGraphSchema.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Transitions/ComposableCameraTransitionBase.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "ComposableCameraSystemEditorModule.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraDebugSnapshot.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphUtilities.h"
#include "Editor.h"
#include "HAL/PlatformApplicationMisc.h"
#include "SNodePanel.h"
#include "Framework/Docking/LayoutExtender.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"
#include "Styling/AppStyle.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ComposableCameraTypeAssetEditorToolkit)

#define LOCTEXT_NAMESPACE "ComposableCameraTypeAssetEditorToolkit"

// ─── Tab IDs ───────────────────────────────────────────────────────────────────

const FName FComposableCameraTypeAssetEditorToolkit::GraphEditorTabId(TEXT("ComposableCameraTypeAsset_GraphEditor"));
const FName FComposableCameraTypeAssetEditorToolkit::DetailsTabId(TEXT("ComposableCameraTypeAsset_Details"));
const FName FComposableCameraTypeAssetEditorToolkit::BuildMessagesTabId(TEXT("ComposableCameraTypeAsset_BuildMessages"));

// ─── Construction ──────────────────────────────────────────────────────────────

FComposableCameraTypeAssetEditorToolkit::FComposableCameraTypeAssetEditorToolkit(UAssetEditor* InAssetEditor)
	: FBaseAssetToolkit(InAssetEditor)
{
	// Override FBaseAssetToolkit's default layout with our custom 3-panel layout.
	// Must be set in the constructor — FBaseAssetToolkit reads this during InitAssetEditor().
	//
	// The layout version was bumped to _v2 when the Parameters tab was removed.
	// Users who had the old _v1 layout saved in their editor ini will get a
	// fresh _v2 layout on first open instead of loading a layout that still
	// references the now-nonexistent ParametersTabId (which would leave an
	// empty pane in the tab stack).
	StandaloneDefaultLayout = FTabManager::NewLayout("ComposableCameraTypeAssetEditor_Layout_v2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Horizontal)
				->SetSizeCoefficient(0.75f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.7f)
					->AddTab(GraphEditorTabId, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.3f)
					->AddTab(DetailsTabId, ETabState::OpenedTab)
				)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.15f)
				->AddTab(BuildMessagesTabId, ETabState::OpenedTab)
			)
		);
}

FComposableCameraTypeAssetEditorToolkit::~FComposableCameraTypeAssetEditorToolkit()
{
	// Cancel any deferred details-panel update so the ticker doesn't fire into
	// a destroyed toolkit (FTickerDelegate::CreateRaw holds a raw `this`).
	if (PendingSelectionTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PendingSelectionTickerHandle);
		PendingSelectionTickerHandle.Reset();
	}
	bHasPendingSelection = false;

	// ── Debug monitoring cleanup ─────────────────────────────────────────
	if (DebugTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DebugTickerHandle);
		DebugTickerHandle.Reset();
	}
	if (PIEStartedHandle.IsValid())
	{
		FEditorDelegates::PostPIEStarted.Remove(PIEStartedHandle);
		PIEStartedHandle.Reset();
	}
	if (PIEEndedHandle.IsValid())
	{
		FEditorDelegates::PrePIEEnded.Remove(PIEEndedHandle);
		PIEEndedHandle.Reset();
	}

	if (NodeGraph)
	{
		if (GraphChangedDelegateHandle.IsValid())
		{
			NodeGraph->RemoveOnGraphChangedHandler(GraphChangedDelegateHandle);
		}
		NodeGraph->RemoveFromRoot();
	}
}

void FComposableCameraTypeAssetEditorToolkit::SetTypeAsset(UComposableCameraTypeAsset* InTypeAsset)
{
	TypeAsset = InTypeAsset;
}

// ─── Tab Spawners ──────────────────────────────────────────────────────────────

void FComposableCameraTypeAssetEditorToolkit::RegisterTabSpawners(
	const TSharedRef<FTabManager>& InTabManager)
{
	// Skip FBaseAssetToolkit::RegisterTabSpawners — we don't want the default viewport tab.
	// Call FAssetEditorToolkit directly to get the base tab infrastructure.
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	const TSharedRef<FWorkspaceItem> LocalWorkspaceMenuCategory =
		InTabManager->AddLocalWorkspaceMenuCategory(
			LOCTEXT("WorkspaceMenu_TypeAssetEditor", "Camera Type Asset Editor"));

	InTabManager->RegisterTabSpawner(GraphEditorTabId,
		FOnSpawnTab::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::SpawnTab_GraphEditor))
		.SetDisplayName(LOCTEXT("GraphEditorTab", "Node Graph"))
		.SetGroup(LocalWorkspaceMenuCategory)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(DetailsTabId,
		FOnSpawnTab::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(LocalWorkspaceMenuCategory)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(BuildMessagesTabId,
		FOnSpawnTab::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::SpawnTab_BuildMessages))
		.SetDisplayName(LOCTEXT("BuildMessagesTab", "Build Messages"))
		.SetGroup(LocalWorkspaceMenuCategory)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.StatsViewer"));
}

void FComposableCameraTypeAssetEditorToolkit::UnregisterTabSpawners(
	const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(GraphEditorTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	InTabManager->UnregisterTabSpawner(BuildMessagesTabId);

	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

// ─── Tab Content ───────────────────────────────────────────────────────────────

TSharedRef<SDockTab> FComposableCameraTypeAssetEditorToolkit::SpawnTab_GraphEditor(
	const FSpawnTabArgs& Args)
{
	EnsureEditorGraph();
	CreateGraphEditorCommands();

	// Build the graph editor widget.
	SGraphEditor::FGraphEditorEvents GraphEvents;
	GraphEvents.OnSelectionChanged =
		SGraphEditor::FOnSelectionChanged::CreateSP(
			this, &FComposableCameraTypeAssetEditorToolkit::OnGraphSelectionChanged);
	GraphEvents.OnNodeDoubleClicked =
		FSingleNodeEvent::CreateSP(
			this, &FComposableCameraTypeAssetEditorToolkit::OnGraphNodeDoubleClicked);

	GraphEditorWidget = SNew(SGraphEditor)
		.AdditionalCommands(GraphEditorCommands)
		.GraphToEdit(NodeGraph)
		.GraphEvents(GraphEvents)
		.IsEditable(true);

	// Subscribe to graph changes for syncing.
	if (NodeGraph)
	{
		GraphChangedDelegateHandle = NodeGraph->AddOnGraphChangedHandler(
			FOnGraphChanged::FDelegate::CreateSP(
				this, &FComposableCameraTypeAssetEditorToolkit::OnGraphChanged));
	}

	return SNew(SDockTab)
		.Label(LOCTEXT("GraphEditorTabLabel", "Node Graph"))
		[
			GraphEditorWidget.ToSharedRef()
		];
}

TSharedRef<SDockTab> FComposableCameraTypeAssetEditorToolkit::SpawnTab_Details(
	const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsTabLabel", "Details"))
		[
			NodeDetailsView.IsValid()
				? NodeDetailsView->AsShared()
				: SNullWidget::NullWidget
		];
}

TSharedRef<SDockTab> FComposableCameraTypeAssetEditorToolkit::SpawnTab_BuildMessages(
	const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("BuildMessagesTabLabel", "Build Messages"))
		[
			BuildBuildMessagesWidget()
		];
}

// ─── Widgets ───────────────────────────────────────────────────────────────────

void FComposableCameraTypeAssetEditorToolkit::CreateWidgets()
{
	// Skip FBaseAssetToolkit::CreateWidgets() — we don't need the default viewport tab.
	// But we MUST call RegisterToolbar() and set the base class's DetailsView member,
	// otherwise FBaseAssetToolkit crashes in SetObjectsToEdit.
	RegisterToolbar();
	LayoutExtender = MakeShared<FLayoutExtender>();

	// Create the details panel.
	FPropertyEditorModule& PropertyModule =
		FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bAllowSearch = true;

	NodeDetailsView = PropertyModule.CreateDetailView(DetailsArgs);

	// FBaseAssetToolkit requires DetailsView to be set, otherwise it crashes
	// when SetObjectsToEdit is called during initialization.
	DetailsView = NodeDetailsView;

	// Listen for property changes on whatever is currently shown in the details
	// panel. This is how we learn about InternalVariables edits on the type
	// asset so we can refresh any Get/Set variable graph nodes that reference
	// the affected variable (Issue 2 — variable rename/retype doesn't propagate
	// to existing variable graph nodes).
	NodeDetailsView->OnFinishedChangingProperties().AddRaw(
		this, &FComposableCameraTypeAssetEditorToolkit::OnTypeAssetPropertyChanged);

	// Set the type asset as the initial details object.
	if (TypeAsset)
	{
		NodeDetailsView->SetObject(TypeAsset);
		CurrentDetailsObject = TypeAsset;
	}
}

void FComposableCameraTypeAssetEditorToolkit::RegisterToolbar()
{
	const FName ToolbarName = GetToolMenuToolbarName();

	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu(ToolbarName);
	if (!ToolbarMenu)
	{
		return;
	}

	FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("CameraTypeAsset");

	// Debug instance picker — shown as a combo button in the toolbar.
	//
	// IMPORTANT: We use InitWidget + MakeWidget instead of InitComboButton so
	// that the widget factory receives the per-instance FToolMenuContext.
	// The UToolMenu system is GLOBAL — all editors of the same type share the
	// same UToolMenu. InitComboButton captures an FOnGetContent delegate at
	// registration time, so the last editor to call RegisterToolbar() would
	// overwrite the delegate for all instances, routing every Debug dropdown
	// to the wrong toolkit. MakeWidget is called at render time with the
	// correct context for the specific editor, avoiding this bug entirely.
	FToolMenuEntry DebugEntry = FToolMenuEntry::InitWidget(
		"DebugInstancePicker",
		SNullWidget::NullWidget,
		LOCTEXT("DebugInstanceLabel", "Debug")
	);
	DebugEntry.MakeCustomWidget.BindStatic(
		&FComposableCameraTypeAssetEditorToolkit::MakeDebugInstancePickerWidget);
	Section.AddEntry(DebugEntry);
}

void FComposableCameraTypeAssetEditorToolkit::InitToolMenuContext(FToolMenuContext& MenuContext)
{
	FBaseAssetToolkit::InitToolMenuContext(MenuContext);

	UComposableCameraTypeAssetEditorMenuContext* Context =
		NewObject<UComposableCameraTypeAssetEditorMenuContext>();
	Context->Toolkit = SharedThis(this);
	MenuContext.AddObject(Context);
}

void FComposableCameraTypeAssetEditorToolkit::PostInitAssetEditor()
{
	EnsureEditorGraph();

	// ── Subscribe to PIE lifecycle for runtime debug monitoring ──────────
	PIEStartedHandle = FEditorDelegates::PostPIEStarted.AddRaw(
		this, &FComposableCameraTypeAssetEditorToolkit::OnPIEStarted);
	PIEEndedHandle = FEditorDelegates::PrePIEEnded.AddRaw(
		this, &FComposableCameraTypeAssetEditorToolkit::OnPIEEnded);

	// If PIE is already running when this editor opens, catch up immediately
	// so the debug ticker starts and the instance picker works.
	if (GEditor && GEditor->PlayWorld)
	{
		OnPIEStarted(GEditor->bIsSimulatingInEditor);
	}
}

void FComposableCameraTypeAssetEditorToolkit::PostRegenerateMenusAndToolbars()
{
}

void FComposableCameraTypeAssetEditorToolkit::SaveAsset_Execute()
{
	// Flush the current visual graph state into the TypeAsset's durable
	// editor-only fields before the save writes the package. Node canvas
	// positions live on the (Transient) EdGraph and aren't serialized
	// directly — the snapshot in TypeAsset->NodeTemplatePositions /
	// StartNodePosition / OutputNodePosition is the authoritative on-disk
	// representation of layout. SGraphEditor does NOT fire NotifyGraphChanged
	// on node drags (only on structural changes), so without this explicit
	// sync a drag-then-save would quietly drop the new positions.
	if (NodeGraph)
	{
		NodeGraph->SyncToTypeAsset();
	}

	// Auto-build after syncing so the saved asset always has up-to-date
	// validation results. This replaces the need to manually click Build.
	OnBuild();

	FBaseAssetToolkit::SaveAsset_Execute();
}

void FComposableCameraTypeAssetEditorToolkit::SaveAssetAs_Execute()
{
	// Same pre-save sync as SaveAsset_Execute, so "Save As" also captures the
	// current layout into the duplicated asset's durable fields.
	if (NodeGraph)
	{
		NodeGraph->SyncToTypeAsset();
	}

	OnBuild();

	FBaseAssetToolkit::SaveAssetAs_Execute();
}

// ─── IToolkit Interface ────────────────────────────────────────────────────────

FText FComposableCameraTypeAssetEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Camera Type Asset Editor");
}

FName FComposableCameraTypeAssetEditorToolkit::GetToolkitFName() const
{
	static FName ToolkitName("ComposableCameraTypeAssetEditor");
	return ToolkitName;
}

FString FComposableCameraTypeAssetEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Camera Type ").ToString();
}

FLinearColor FComposableCameraTypeAssetEditorToolkit::GetWorldCentricTabColorScale() const
{
	// Teal to match the asset color.
	return FLinearColor(FColor(20, 150, 140));
}

// ─── Graph Management ──────────────────────────────────────────────────────────

void FComposableCameraTypeAssetEditorToolkit::EnsureEditorGraph()
{
	if (!TypeAsset)
	{
		return;
	}

#if WITH_EDITORONLY_DATA
	// The EditorGraph UPROPERTY is Transient — it is never serialized, so on
	// every editor session it starts null and we create + rebuild it from the
	// durable type-asset state. Creating a new UObject in the already-loaded
	// package (NewObject with TypeAsset as Outer) and running
	// RebuildFromTypeAsset both mark the package dirty as a side-effect.
	// That spurious dirty flag causes the asset to show an asterisk in the
	// Content Browser immediately after editor launch, even though nothing
	// actually changed. Snapshot the package's dirty state and restore it
	// after the transient graph is set up.
	UPackage* Package = TypeAsset->GetOutermost();
	const bool bWasDirty = Package && Package->IsDirty();

	if (!TypeAsset->EditorGraph)
	{
		UComposableCameraNodeGraph* NewGraph = NewObject<UComposableCameraNodeGraph>(
			TypeAsset, NAME_None, RF_Transactional);
		NewGraph->Schema = UComposableCameraNodeGraphSchema::StaticClass();
		NewGraph->OwningTypeAsset = TypeAsset;
		TypeAsset->EditorGraph = NewGraph;
	}

	NodeGraph = Cast<UComposableCameraNodeGraph>(TypeAsset->EditorGraph);
	if (NodeGraph)
	{
		NodeGraph->AddToRoot(); // Keep alive while editor is open.

		// Rebuild visual state from the serialized data.
		NodeGraph->OwningTypeAsset = TypeAsset;
		NodeGraph->RebuildFromTypeAsset();
	}

	// Restore the package's pre-existing dirty state. The graph creation and
	// rebuild are not user edits — they're internal reconstruction of
	// transient state from the durable serialized data.
	if (Package && !bWasDirty)
	{
		Package->SetDirtyFlag(false);
	}
#endif
}

void FComposableCameraTypeAssetEditorToolkit::OnGraphSelectionChanged(
	const TSet<UObject*>& SelectedNodes)
{
	if (!NodeDetailsView.IsValid())
	{
		return;
	}

	// We intentionally do NOT update the details panel synchronously here.
	//
	// SGraphEditor fires OnSelectionChanged BEFORE it calls the schema's
	// GetContextMenuActions on a right-click — which means that when the
	// user right-clicks a pin, we see a selection-change event that looks
	// identical to a real node selection. If we updated the details panel
	// immediately, the Details view would flip to the node's template
	// properties just from opening a pin context menu, which is jarring
	// and unwanted (Issue 3 in EditorDesignDoc).
	//
	// Instead we snapshot the selection and schedule an apply-pass on the
	// next tick. The schema's GetContextMenuActions (which runs AFTER this
	// callback on the same frame) will have called
	// UComposableCameraNodeGraph::MarkPinContextMenuRequested if a pin was
	// right-clicked, and ApplyPendingSelectionToDetails consumes that signal
	// via ConsumePinContextMenuRequested before touching NodeDetailsView.
	// This lets a plain click still update the details as expected, while a
	// pin right-click leaves them alone.

	PendingSelection.Reset();
	PendingSelection.Reserve(SelectedNodes.Num());
	for (UObject* Obj : SelectedNodes)
	{
		PendingSelection.Add(Obj);
	}

	if (!bHasPendingSelection)
	{
		bHasPendingSelection = true;
		PendingSelectionTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FComposableCameraTypeAssetEditorToolkit::ApplyPendingSelectionToDetails),
			0.0f);
	}
}

bool FComposableCameraTypeAssetEditorToolkit::ApplyPendingSelectionToDetails(float /*DeltaTime*/)
{
	// Always reset the pending-flag and handle first, so an early-out doesn't
	// leave the toolkit in a state where future selections fail to enqueue.
	bHasPendingSelection = false;
	PendingSelectionTickerHandle.Reset();

	if (!NodeDetailsView.IsValid())
	{
		return false; // don't reschedule
	}

	// If the schema set the "pin context menu" flag since we were queued, the
	// selection change was incidental — caused by SGraphEditor auto-selecting
	// the pin's owning node to show a right-click menu — and we must NOT move
	// the details panel. ConsumePinContextMenuRequested returns true exactly
	// once per MarkPinContextMenuRequested call and clears the flag as a side
	// effect, so we don't touch the field directly.
	if (NodeGraph && NodeGraph->ConsumePinContextMenuRequested())
	{
		PendingSelection.Reset();
		return false;
	}

	const int32 NumSelected = PendingSelection.Num();

	// When all nodes are deselected, show the type asset so its
	// InternalVariables / ExposedVariables / ExposedParameters remain editable.
	if (NumSelected == 0)
	{
		if (TypeAsset && CurrentDetailsObject.Get() != TypeAsset)
		{
			NodeDetailsView->SetObject(TypeAsset);
			CurrentDetailsObject = TypeAsset;
		}
		PendingSelection.Reset();
		return false;
	}

	// ── Single selection ────────────────────────────────────────────────
	//
	// For a single camera graph node, show the GRAPH NODE itself — not the
	// underlying NodeTemplate. The GraphNode is where per-instance pin
	// overrides (bAsPin, DefaultValueOverride) live via the transient
	// RuntimePinOverrides cache, and FComposableCameraNodeGraphNodeDetails is
	// registered as an IDetailCustomization on UComposableCameraNodeGraphNode
	// specifically so it can synthesize per-pin rows from the GraphNode's
	// accessors. The customization surfaces the NodeTemplate's class-level
	// UPROPERTYs in the same unified "Properties" category via
	// AddExternalObjectProperty, so designers retain access to legacy
	// class-level fields (e.g. FieldOfView on
	// UComposableCameraFieldOfViewNode) despite the subject swap.
	//
	// For any OTHER selected UEdGraphNode (Begin / Tick / Output / Set / Get /
	// variable-bind nodes), push the raw node object into the details view so
	// the default property grid renders its own UPROPs. Previously this branch
	// only fired for camera graph nodes, so clicking a Start node or a Get
	// node left the details pinned to the last camera node the user looked at
	// — visually confusing because the details no longer match the graph
	// selection highlight. Treating "single selection" as "push whatever
	// UObject is selected" makes the details track selection for every node
	// type uniformly.
	if (NumSelected == 1)
	{
		// PendingSelection is a TSet<TWeakObjectPtr<UObject>> — there's no
		// indexed access, so grab the sole element via a range-for and break.
		UObject* Obj = nullptr;
		for (const TWeakObjectPtr<UObject>& WeakObj : PendingSelection)
		{
			Obj = WeakObj.Get();
			break;
		}
		if (Obj && CurrentDetailsObject.Get() != Obj)
		{
			NodeDetailsView->SetObject(Obj);
			CurrentDetailsObject = Obj;
		}
	}
	// ── Multi-selection ─────────────────────────────────────────────────
	//
	// Two or more nodes selected at once. We don't yet support a proper
	// multi-object details experience for camera graph nodes — the pin-row
	// customization is strictly single-object because different nodes have
	// different pin declarations and per-pin state isn't meaningful across a
	// heterogeneous selection. Previously this case was a silent no-op, which
	// meant ctrl-clicking a second node left the panel showing the first
	// node's info — visually stale and confusing.
	//
	// Fall back to the type asset instead. The type asset's details view is
	// always a coherent target: ExposedParameters, InternalVariables, and
	// ExposedVariables stay editable, and the panel clearly no longer claims
	// to represent a specific node. Once we want to support multi-node editing
	// (e.g. bulk-toggling As Pin across nodes of the same class) this branch
	// is the place to add a SetObjects(...) path instead of the fallback.
	else
	{
		if (TypeAsset && CurrentDetailsObject.Get() != TypeAsset)
		{
			NodeDetailsView->SetObject(TypeAsset);
			CurrentDetailsObject = TypeAsset;
		}
	}

	PendingSelection.Reset();
	return false;
}

void FComposableCameraTypeAssetEditorToolkit::OnGraphNodeDoubleClicked(UEdGraphNode* Node)
{
	// Could open a sub-editor or focus the details panel in the future.
}

void FComposableCameraTypeAssetEditorToolkit::OnGraphChanged(const FEdGraphEditAction& Action)
{
	// Sync graph state back to the type asset whenever the graph changes.
	if (NodeGraph)
	{
		NodeGraph->SyncToTypeAsset();
	}
}

void FComposableCameraTypeAssetEditorToolkit::OnTypeAssetPropertyChanged(
	const FPropertyChangedEvent& PropertyChangedEvent)
{
	// We only care about edits to the type asset itself (not to a node template
	// that happens to be in the details view). Bail out if the current object
	// is not the type asset.
	if (CurrentDetailsObject.Get() != TypeAsset)
	{
		return;
	}

	if (!TypeAsset || !NodeGraph)
	{
		return;
	}

	const FName MemberName = PropertyChangedEvent.MemberProperty
		? PropertyChangedEvent.MemberProperty->GetFName()
		: NAME_None;

	// Issue 2: When InternalVariables or ExposedVariables changes (name, type,
	// etc.), refresh every Get/Set variable graph node that references a
	// variable from either array. Identity is GUID-based, so renames survive
	// and the node's title / pin type will pick up the new values via
	// FindVariable() — which now searches both arrays uniformly.
	const bool bInternalVarsEdit = (MemberName == GET_MEMBER_NAME_CHECKED(UComposableCameraTypeAsset, InternalVariables));
	const bool bExposedVarsEdit = (MemberName == GET_MEMBER_NAME_CHECKED(UComposableCameraTypeAsset, ExposedVariables));
	if (bInternalVarsEdit || bExposedVarsEdit)
	{
		for (UEdGraphNode* GraphNode : NodeGraph->Nodes)
		{
			if (UComposableCameraVariableGraphNode* VarNode = Cast<UComposableCameraVariableGraphNode>(GraphNode))
			{
				// ReconstructPins is wire-preserving (see the matching fix for
				// Issue 4), so wires survive unless the variable's type actually
				// changed — in which case we deliberately drop the incompatible
				// link.
				VarNode->ReconstructPins();
			}
		}

		// Persist the refreshed layout back to the type asset. This is a no-op
		// for the variable records themselves (names/types live on
		// InternalVariables / ExposedVariables, not on the records) but it
		// keeps the editor in sync with any side-effects of the reconstruct.
		NodeGraph->SyncToTypeAsset();
		NodeGraph->NotifyGraphChanged();
	}
}

// ─── Graph Editor Commands ────────────────────────────────────────────────────

void FComposableCameraTypeAssetEditorToolkit::CreateGraphEditorCommands()
{
	if (GraphEditorCommands.IsValid())
	{
		return;
	}

	GraphEditorCommands = MakeShared<FUICommandList>();

	GraphEditorCommands->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::DeleteSelectedNodes),
		FCanExecuteAction::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::CanDeleteSelectedNodes));

	GraphEditorCommands->MapAction(
		FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::CopySelectedNodes),
		FCanExecuteAction::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::CanCopySelectedNodes));

	GraphEditorCommands->MapAction(
		FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::PasteNodes),
		FCanExecuteAction::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::CanPasteNodes));

	GraphEditorCommands->MapAction(
		FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::CutSelectedNodes),
		FCanExecuteAction::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::CanCutSelectedNodes));

	GraphEditorCommands->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::DuplicateSelectedNodes),
		FCanExecuteAction::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::CanCopySelectedNodes));

	GraphEditorCommands->MapAction(
		FGenericCommands::Get().SelectAll,
		FExecuteAction::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::SelectAllNodes),
		FCanExecuteAction::CreateSP(this, &FComposableCameraTypeAssetEditorToolkit::CanSelectAllNodes));
}

void FComposableCameraTypeAssetEditorToolkit::DeleteSelectedNodes()
{
	if (!GraphEditorWidget.IsValid() || !NodeGraph)
	{
		return;
	}

	const FScopedTransaction Transaction(
		LOCTEXT("DeleteSelectedNodes", "Delete Selected Camera Nodes"));
	NodeGraph->Modify();

	const FGraphPanelSelectionSet SelectedNodes = GraphEditorWidget->GetSelectedNodes();
	GraphEditorWidget->ClearSelectionSet();

	bool bAnyDeleted = false;
	for (UObject* NodeObj : SelectedNodes)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(NodeObj);
		if (!Node || !Node->CanUserDeleteNode())
		{
			continue;
		}

		Node->Modify();
		Node->DestroyNode();
		bAnyDeleted = true;
	}

	if (bAnyDeleted)
	{
		// SyncToTypeAsset will run via OnGraphChanged, but we explicitly invoke
		// it here so NodeTemplates / indices are rebuilt before any UI refresh.
		NodeGraph->SyncToTypeAsset();
		NodeGraph->NotifyGraphChanged();
	}
}

bool FComposableCameraTypeAssetEditorToolkit::CanDeleteSelectedNodes() const
{
	if (!GraphEditorWidget.IsValid())
	{
		return false;
	}

	const FGraphPanelSelectionSet SelectedNodes = GraphEditorWidget->GetSelectedNodes();
	for (UObject* NodeObj : SelectedNodes)
	{
		if (const UEdGraphNode* Node = Cast<UEdGraphNode>(NodeObj))
		{
			if (Node->CanUserDeleteNode())
			{
				return true;
			}
		}
	}
	return false;
}

void FComposableCameraTypeAssetEditorToolkit::CopySelectedNodes()
{
	if (!GraphEditorWidget.IsValid())
	{
		return;
	}

	// Collect the copiable subset of the selection. PrepareForCopying
	// snapshots Transient state (NodeTemplate, RuntimePinOverrides) into
	// the non-Transient copy-paste transport fields before the export
	// serializes the graph nodes to text.
	FGraphPanelSelectionSet SelectedNodes = GraphEditorWidget->GetSelectedNodes();
	for (FGraphPanelSelectionSet::TIterator It(SelectedNodes); It; ++It)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(*It);
		if (!Node || !Node->CanDuplicateNode())
		{
			It.RemoveCurrent();
			continue;
		}
		Node->PrepareForCopying();
	}

	FString ExportedText;
	FEdGraphUtilities::ExportNodesToText(SelectedNodes, /*out*/ ExportedText);
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool FComposableCameraTypeAssetEditorToolkit::CanCopySelectedNodes() const
{
	if (!GraphEditorWidget.IsValid())
	{
		return false;
	}

	const FGraphPanelSelectionSet SelectedNodes = GraphEditorWidget->GetSelectedNodes();
	for (UObject* NodeObj : SelectedNodes)
	{
		if (const UEdGraphNode* Node = Cast<UEdGraphNode>(NodeObj))
		{
			if (Node->CanDuplicateNode())
			{
				return true;
			}
		}
	}
	return false;
}

void FComposableCameraTypeAssetEditorToolkit::PasteNodes()
{
	if (!GraphEditorWidget.IsValid() || !NodeGraph)
	{
		return;
	}

	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);

	const FScopedTransaction Transaction(
		LOCTEXT("PasteNodes", "Paste Camera Nodes"));
	NodeGraph->Modify();

	// Import the serialized graph nodes into the graph. This reconstructs
	// the UEdGraphNode subclass instances (with their non-Transient fields)
	// and any inter-node pin connections that existed at copy time.
	TSet<UEdGraphNode*> PastedNodes;
	FEdGraphUtilities::ImportNodesFromText(NodeGraph, ClipboardText, /*out*/ PastedNodes);

	// Offset pasted nodes so they don't overlap the originals.
	constexpr int32 PasteOffsetX = 50;
	constexpr int32 PasteOffsetY = 50;
	for (UEdGraphNode* Node : PastedNodes)
	{
		Node->NodePosX += PasteOffsetX;
		Node->NodePosY += PasteOffsetY;
	}

	// Run post-paste fixup on each node. For camera graph nodes this
	// adopts the copy-paste transport template as the live NodeTemplate
	// and reparents it under the TypeAsset (see
	// UComposableCameraNodeGraphNode::PostPasteNode).
	for (UEdGraphNode* Node : PastedNodes)
	{
		Node->PostPasteNode();
		Node->SnapToGrid(SNodePanel::GetSnapGridSize());
	}

	// Sync the updated graph back to the type asset so the new nodes
	// get slots in NodeTemplates / PinConnections / VariableNodes.
	NodeGraph->SyncToTypeAsset();
	NodeGraph->NotifyGraphChanged();

	// Select only the pasted nodes so the user can immediately drag them.
	GraphEditorWidget->ClearSelectionSet();
	for (UEdGraphNode* Node : PastedNodes)
	{
		GraphEditorWidget->SetNodeSelection(Node, true);
	}
}

bool FComposableCameraTypeAssetEditorToolkit::CanPasteNodes() const
{
	if (!GraphEditorWidget.IsValid() || !NodeGraph)
	{
		return false;
	}

	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);
	if (ClipboardText.IsEmpty())
	{
		return false;
	}

	return FEdGraphUtilities::CanImportNodesFromText(NodeGraph, ClipboardText);
}

void FComposableCameraTypeAssetEditorToolkit::CutSelectedNodes()
{
	CopySelectedNodes();
	DeleteSelectedNodes();
}

bool FComposableCameraTypeAssetEditorToolkit::CanCutSelectedNodes() const
{
	return CanCopySelectedNodes() && CanDeleteSelectedNodes();
}

void FComposableCameraTypeAssetEditorToolkit::DuplicateSelectedNodes()
{
	CopySelectedNodes();
	PasteNodes();
}

void FComposableCameraTypeAssetEditorToolkit::SelectAllNodes()
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->SelectAllNodes();
	}
}

bool FComposableCameraTypeAssetEditorToolkit::CanSelectAllNodes() const
{
	return GraphEditorWidget.IsValid();
}

// ─── Build ─────────────────────────────────────────────────────────────────────

void FComposableCameraTypeAssetEditorToolkit::OnBuild()
{
#if WITH_EDITOR
	if (TypeAsset)
	{
		// Sync the latest graph state before building.
		if (NodeGraph)
		{
			NodeGraph->SyncToTypeAsset();
		}

		TypeAsset->Build();

		// Refresh the build messages tab. Exposed parameters, internal
		// variables, and the default transition now live in the main Details
		// panel (surfaced when the type asset itself is the details subject),
		// so they repaint automatically on the next property-change tick and
		// don't need an explicit refresh pass here.
		if (TabManager.IsValid())
		{
			TSharedPtr<SDockTab> BuildTab = TabManager->FindExistingLiveTab(BuildMessagesTabId);
			if (BuildTab.IsValid())
			{
				BuildTab->SetContent(BuildBuildMessagesWidget());
			}
		}
	}
#endif
}

// ─── Widget Builders ───────────────────────────────────────────────────────────

TSharedRef<SWidget> FComposableCameraTypeAssetEditorToolkit::BuildBuildMessagesWidget()
{
	TSharedRef<SVerticalBox> VBox = SNew(SVerticalBox);

#if WITH_EDITORONLY_DATA
	if (!TypeAsset)
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("NoBuildData", "No type asset loaded."))
			.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"));
	}

	// ── Status header with icon ──────────────────────────────────────────

	FText StatusText;
	FSlateColor StatusColor;
	const FSlateBrush* StatusIcon = nullptr;
	switch (TypeAsset->BuildStatus)
	{
	case EComposableCameraBuildStatus::NotBuilt:
		StatusText = LOCTEXT("StatusNotBuilt", "Not Built");
		StatusColor = FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f));
		StatusIcon = FAppStyle::GetBrush("Icons.Help");
		break;
	case EComposableCameraBuildStatus::Success:
		StatusText = FText::Format(
			LOCTEXT("StatusSuccessFmt", "Build Succeeded  ({0} nodes, {1} connections)"),
			FText::AsNumber(TypeAsset->NodeTemplates.Num()),
			FText::AsNumber(TypeAsset->PinConnections.Num()));
		StatusColor = FSlateColor(FLinearColor(0.0f, 0.8f, 0.2f));
		StatusIcon = FAppStyle::GetBrush("Icons.SuccessWithColor");
		break;
	case EComposableCameraBuildStatus::SuccessWithWarnings:
		StatusText = FText::Format(
			LOCTEXT("StatusWarningsFmt", "Build Succeeded with {0} Warning(s)"),
			FText::AsNumber(TypeAsset->BuildMessages.Num()));
		StatusColor = FSlateColor(FLinearColor(1.0f, 0.8f, 0.0f));
		StatusIcon = FAppStyle::GetBrush("Icons.WarningWithColor");
		break;
	case EComposableCameraBuildStatus::Failed:
	{
		int32 ErrorCount = 0;
		for (const FComposableCameraBuildMessage& Msg : TypeAsset->BuildMessages)
		{
			if (Msg.Severity >= 2) { ++ErrorCount; }
		}
		StatusText = FText::Format(
			LOCTEXT("StatusFailedFmt", "Build Failed  ({0} Error(s))"),
			FText::AsNumber(ErrorCount));
		StatusColor = FSlateColor(FLinearColor(1.0f, 0.15f, 0.15f));
		StatusIcon = FAppStyle::GetBrush("Icons.ErrorWithColor");
		break;
	}
	}

	VBox->AddSlot()
	.AutoHeight()
	.Padding(6.0f, 4.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(SImage)
			.Image(StatusIcon)
			.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(StatusText)
			.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			.ColorAndOpacity(StatusColor)
		]
	];

	// ── Separator ────────────────────────────────────────────────────────

	if (TypeAsset->BuildMessages.Num() > 0)
	{
		VBox->AddSlot()
		.AutoHeight()
		.Padding(6.0f, 0.0f)
		[
			SNew(SSeparator)
		];
	}

	// ── Messages ─────────────────────────────────────────────────────────

	if (TypeAsset->BuildMessages.Num() == 0 && TypeAsset->BuildStatus != EComposableCameraBuildStatus::NotBuilt)
	{
		VBox->AddSlot()
		.AutoHeight()
		.Padding(10.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoMessages", "No issues found."))
			.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		];
	}
	else
	{
		for (const FComposableCameraBuildMessage& Msg : TypeAsset->BuildMessages)
		{
			FSlateColor MsgColor;
			const FSlateBrush* MsgIcon = nullptr;
			switch (Msg.Severity)
			{
			case 0:
				MsgColor = FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f));
				MsgIcon = FAppStyle::GetBrush("Icons.Info");
				break;
			case 1:
				MsgColor = FSlateColor(FLinearColor(1.0f, 0.85f, 0.0f));
				MsgIcon = FAppStyle::GetBrush("Icons.Warning");
				break;
			default:
				MsgColor = FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f));
				MsgIcon = FAppStyle::GetBrush("Icons.Error");
				break;
			}

			VBox->AddSlot()
			.AutoHeight()
			.Padding(10.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Top)
				.Padding(0.0f, 1.0f, 6.0f, 0.0f)
				[
					SNew(SImage)
					.Image(MsgIcon)
					.DesiredSizeOverride(FVector2D(14.0f, 14.0f))
					.ColorAndOpacity(MsgColor)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Msg.Message)
					.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
					.ColorAndOpacity(MsgColor)
					.AutoWrapText(true)
				]
			];
		}
	}
#endif

	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			VBox
		];
}

// ─── Runtime Debug Monitoring ─────────────────────────────────────────────────

void FComposableCameraTypeAssetEditorToolkit::OnPIEStarted(bool bIsSimulating)
{
	bIsPIEActive = true;

	// Start the debug ticker — polls every frame while PIE is active.
	DebugTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FComposableCameraTypeAssetEditorToolkit::DebugTick));

	// Auto-bind if exactly one matching camera instance is found.
	TArray<TWeakObjectPtr<AComposableCameraCameraBase>> Instances = FindMatchingCameraInstances();
	if (Instances.Num() == 1 && Instances[0].IsValid())
	{
		BindToCamera(Instances[0].Get());
	}
}

void FComposableCameraTypeAssetEditorToolkit::OnPIEEnded(bool bIsSimulating)
{
	bIsPIEActive = false;

	// Stop the debug ticker.
	if (DebugTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DebugTickerHandle);
		DebugTickerHandle.Reset();
	}

	DebuggedCamera.Reset();
	ClearGraphNodeDebugState();
}

bool FComposableCameraTypeAssetEditorToolkit::DebugTick(float DeltaTime)
{
	if (!GEditor || !GEditor->PlayWorld)
	{
		ClearGraphNodeDebugState();
		return false; // Unregister ticker.
	}

	// If the debugged camera was destroyed mid-PIE, clear and keep ticking.
	if (!DebuggedCamera.IsValid())
	{
		ClearGraphNodeDebugState();
		return true;
	}

	// Pull a debug snapshot from the runtime camera.
	FComposableCameraDebugSnapshot Snapshot = DebuggedCamera->SnapshotDebugState();
	if (!Snapshot.bIsValid)
	{
		ClearGraphNodeDebugState();
		return true;
	}

	// Push snapshot data into graph nodes by correlating NodeIndex.
	// Also push variable values into Get/Set variable graph nodes.
	if (NodeGraph)
	{
		// Build a quick name→value lookup for internal + exposed variable values.
		TMap<FName, FString> VarValueMap;
		VarValueMap.Reserve(Snapshot.InternalVariableValues.Num());
		for (const TPair<FName, FString>& Pair : Snapshot.InternalVariableValues)
		{
			VarValueMap.Add(Pair.Key, Pair.Value);
		}

		for (UEdGraphNode* RawNode : NodeGraph->Nodes)
		{
			// ── Camera nodes: per-node pose + output pin values ──────────
			if (UComposableCameraNodeGraphNode* GraphNode = Cast<UComposableCameraNodeGraphNode>(RawNode))
			{
				const FComposableCameraNodeDebugEntry* MatchingEntry = nullptr;
				for (const FComposableCameraNodeDebugEntry& Entry : Snapshot.NodeEntries)
				{
					if (Entry.NodeIndex == GraphNode->NodeIndex)
					{
						MatchingEntry = &Entry;
						break;
					}
				}

				if (MatchingEntry)
				{
					GraphNode->DebugState.bIsActive = MatchingEntry->bWasTicked;
					GraphNode->DebugState.PoseAfterNode = MatchingEntry->PoseAfterNode;
					GraphNode->DebugState.OutputPinDisplayValues.Reset();
					for (const TPair<FName, FString>& PinValue : MatchingEntry->OutputPinValues)
					{
						GraphNode->DebugState.OutputPinDisplayValues.Add(PinValue.Key, PinValue.Value);
					}
				}
				else
				{
					GraphNode->DebugState.Reset();
				}
				continue;
			}

			// ── Variable nodes: current variable value ───────────────────
			if (UComposableCameraVariableGraphNode* VarNode = Cast<UComposableCameraVariableGraphNode>(RawNode))
			{
				if (const FString* Value = VarValueMap.Find(VarNode->VariableName))
				{
					VarNode->DebugState.bIsActive = true;
					VarNode->DebugState.DisplayValue = *Value;
				}
				else
				{
					VarNode->DebugState.Reset();
				}
				continue;
			}
		}
	}

	return true; // Keep ticking.
}

TArray<TWeakObjectPtr<AComposableCameraCameraBase>> FComposableCameraTypeAssetEditorToolkit::FindMatchingCameraInstances() const
{
	TArray<TWeakObjectPtr<AComposableCameraCameraBase>> Result;

	if (!TypeAsset || !GEditor)
	{
		UE_LOG(LogComposableCameraSystemEditor, Verbose,
			TEXT("[DebugPicker] Early-out: TypeAsset=%p  GEditor=%p"),
			TypeAsset, GEditor);
		return Result;
	}

	UE_LOG(LogComposableCameraSystemEditor, Verbose,
		TEXT("[DebugPicker] Searching for cameras with SourceTypeAsset == '%s' (%p)"),
		*TypeAsset->GetName(), TypeAsset);

	// Collect all PIE/Game worlds. GEditor->PlayWorld is the primary PIE world;
	// we also walk GEngine->GetWorldContexts() to catch multiple-client PIE.
	TArray<UWorld*, TInlineAllocator<4>> PIEWorlds;
	if (GEditor->PlayWorld)
	{
		PIEWorlds.AddUnique(GEditor->PlayWorld);
	}
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (World && (WorldContext.WorldType == EWorldType::PIE || WorldContext.WorldType == EWorldType::Game))
		{
			PIEWorlds.AddUnique(World);
		}
	}

	UE_LOG(LogComposableCameraSystemEditor, Verbose,
		TEXT("[DebugPicker] Found %d PIE world(s), PlayWorld=%p"),
		PIEWorlds.Num(), GEditor->PlayWorld.Get());

	for (UWorld* World : PIEWorlds)
	{
		int32 TotalCamerasInWorld = 0;
		for (TActorIterator<AComposableCameraCameraBase> It(World); It; ++It)
		{
			AComposableCameraCameraBase* Camera = *It;
			++TotalCamerasInWorld;

			UComposableCameraTypeAsset* CamSource = Camera ? Camera->SourceTypeAsset.Get() : nullptr;
			UE_LOG(LogComposableCameraSystemEditor, Verbose,
				TEXT("[DebugPicker]   Camera '%s' — SourceTypeAsset='%s' (%p), match=%s"),
				Camera ? *Camera->GetName() : TEXT("null"),
				CamSource ? *CamSource->GetName() : TEXT("null"),
				CamSource,
				(CamSource == TypeAsset) ? TEXT("YES") : TEXT("NO"));

			if (Camera && CamSource == TypeAsset)
			{
				Result.Add(Camera);
			}
		}

		UE_LOG(LogComposableCameraSystemEditor, Verbose,
			TEXT("[DebugPicker]   World '%s': %d total camera(s), %d matched"),
			*World->GetName(), TotalCamerasInWorld, Result.Num());
	}

	return Result;
}

void FComposableCameraTypeAssetEditorToolkit::BindToCamera(AComposableCameraCameraBase* Camera)
{
	DebuggedCamera = Camera;
}

void FComposableCameraTypeAssetEditorToolkit::ClearGraphNodeDebugState()
{
	if (!NodeGraph)
	{
		return;
	}

	for (UEdGraphNode* RawNode : NodeGraph->Nodes)
	{
		if (UComposableCameraNodeGraphNode* GraphNode = Cast<UComposableCameraNodeGraphNode>(RawNode))
		{
			GraphNode->DebugState.Reset();
		}
		else if (UComposableCameraVariableGraphNode* VarNode = Cast<UComposableCameraVariableGraphNode>(RawNode))
		{
			VarNode->DebugState.Reset();
		}
	}

	// No explicit repaint needed — the custom SGraphNode OnPaint overlays are
	// re-evaluated by Slate on the next paint pass automatically.
}

TSharedRef<SWidget> FComposableCameraTypeAssetEditorToolkit::MakeDebugInstancePickerWidget(
	const FToolMenuContext& Context, const FToolMenuCustomWidgetContext& /*WidgetContext*/)
{
	UComposableCameraTypeAssetEditorMenuContext* Ctx =
		Context.FindContext<UComposableCameraTypeAssetEditorMenuContext>();
	if (!Ctx)
	{
		return SNullWidget::NullWidget;
	}

	TSharedPtr<FComposableCameraTypeAssetEditorToolkit> Toolkit = Ctx->Toolkit.Pin();
	if (!Toolkit.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	// Use a weak pointer so the combo button doesn't prevent toolkit destruction.
	TWeakPtr<FComposableCameraTypeAssetEditorToolkit> WeakToolkit = Toolkit;

	return SNew(SComboButton)
		.OnGetMenuContent_Lambda([WeakToolkit]() -> TSharedRef<SWidget>
		{
			TSharedPtr<FComposableCameraTypeAssetEditorToolkit> Pinned = WeakToolkit.Pin();
			if (Pinned.IsValid())
			{
				return Pinned->BuildDebugInstancePickerWidget();
			}
			return SNullWidget::NullWidget;
		})
		.ToolTipText(LOCTEXT("DebugInstanceTooltip",
			"Select a running camera instance to debug. Active during PIE."))
		.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
		.ContentPadding(FMargin(4.0f, 2.0f))
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("PlayWorld.Simulate"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DebugLabel", "Debug"))
			]
		];
}

TSharedRef<SWidget> FComposableCameraTypeAssetEditorToolkit::BuildDebugInstancePickerWidget()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/ true, nullptr);

	// Query PIE state directly from GEditor rather than relying on bIsPIEActive,
	// which can be stale if the editor was opened after PIE started.
	const bool bCurrentlyInPIE = GEditor && (GEditor->PlayWorld.Get() != nullptr);

	if (!bCurrentlyInPIE)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("DebugNotInPIE", "Not in PIE"),
			LOCTEXT("DebugNotInPIETooltip", "Start a Play-In-Editor session to debug camera instances."),
			FSlateIcon(),
			FUIAction(),
			NAME_None,
			EUserInterfaceActionType::None
		);
		return MenuBuilder.MakeWidget();
	}

	TArray<TWeakObjectPtr<AComposableCameraCameraBase>> Instances = FindMatchingCameraInstances();

	if (Instances.Num() == 0)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("DebugNoInstances", "No instances found"),
			LOCTEXT("DebugNoInstancesTooltip", "No camera instances matching this type asset were found in the current PIE session."),
			FSlateIcon(),
			FUIAction(),
			NAME_None,
			EUserInterfaceActionType::None
		);
	}
	else
	{
		// "None" entry to unbind.
		MenuBuilder.AddMenuEntry(
			LOCTEXT("DebugNone", "None"),
			LOCTEXT("DebugNoneTooltip", "Stop debugging any camera instance."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this]()
			{
				DebuggedCamera.Reset();
				ClearGraphNodeDebugState();
			}))
		);

		MenuBuilder.AddSeparator();

		for (int32 i = 0; i < Instances.Num(); ++i)
		{
			if (!Instances[i].IsValid())
			{
				continue;
			}

			AComposableCameraCameraBase* Camera = Instances[i].Get();
			const FText Label = FText::FromString(Camera->GetName());
			const bool bIsCurrent = (DebuggedCamera == Camera);

			MenuBuilder.AddMenuEntry(
				Label,
				FText::Format(LOCTEXT("DebugInstanceTooltipFmt", "Debug {0}"), Label),
				bIsCurrent
					? FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Check")
					: FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, WeakCamera = Instances[i]]()
				{
					if (WeakCamera.IsValid())
					{
						BindToCamera(WeakCamera.Get());
					}
				}))
			);
		}
	}

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
