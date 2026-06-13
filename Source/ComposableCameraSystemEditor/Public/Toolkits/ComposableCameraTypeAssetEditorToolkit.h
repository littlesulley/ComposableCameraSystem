// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BaseAssetToolkit.h"
#include "GraphEditor.h"
#include "Containers/Ticker.h"
#include "ComposableCameraTypeAssetEditorToolkit.generated.h"

class UComposableCameraTypeAsset;
class UComposableCameraNodeGraph;
class UAssetEditor;
class AComposableCameraCameraBase;
class AActor;
class SComposableCameraRuntimePreviewer;
struct FComposableCameraDebugSnapshot;
enum class ERuntimePreviewerStatus : uint8;

/**
 * FBaseAssetToolkit subclass that provides the visual node graph editor
 * for Camera Type Assets.
 *
 * Layout:
 * 
 * Toolbar [Build] 
 * 
 * 
 * Details Panel 
 * Node Graph (selected node properties - 
 * falls back to the type asset 
 * itself when nothing is selected,
 * so Exposed Parameters, Internal 
 * Variables, and Default 
 * Transition all remain editable 
 * in-place) 
 * 
 * 
 * Build Messages (validation log) 
 * 
 *
 * Note: the editor previously had a dedicated "Parameters" tab that showed a
 * read-only summary of exposed parameters, internal variables, and the default
 * transition. That tab was removed because its contents are all editable
 * directly on the type asset through the main Details panel - clicking on an
 * empty part of the graph (or opening the asset with nothing selected) makes
 * the Details panel show the type asset and surfaces every category the old
 * Parameters tab used to display.
 */
class FComposableCameraTypeAssetEditorToolkit: public FBaseAssetToolkit
{
public:
	FComposableCameraTypeAssetEditorToolkit(UAssetEditor* InAssetEditor);
	~FComposableCameraTypeAssetEditorToolkit();

	void SetTypeAsset(UComposableCameraTypeAsset* InTypeAsset);

protected:
	// FBaseAssetToolkit Interface 
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void CreateWidgets() override;
	virtual void RegisterToolbar() override;
	virtual void InitToolMenuContext(FToolMenuContext& MenuContext) override;
	virtual void PostInitAssetEditor() override;
	virtual void PostRegenerateMenusAndToolbars() override;

	// IToolkit Interface 
	virtual FText GetBaseToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;

	// FAssetEditorToolkit Save Hook 
	//
	// Override the save path so the node graph's in-memory visual state
	// (especially node canvas positions, which the graph doesn't notify on
	// during drags) is flushed into the TypeAsset's durable editor-only
	// fields before the package is written to disk. Without this, a user
	// drags some nodes, saves, reopens, and the drag is lost - because
	// EditorGraph is Transient so NodePosX/NodePosY aren't serialized
	// directly, only the snapshot inside TypeAsset->NodeTemplatePositions is.
	virtual void SaveAsset_Execute() override;
	virtual void SaveAssetAs_Execute() override;

private:
	// Graph Management 

	/** Ensure the EdGraph exists on the type asset and rebuild it if needed. */
	void EnsureEditorGraph();

	/** Callback when graph selection changes - schedules a deferred details panel update. */
	void OnGraphSelectionChanged(const TSet<UObject*>& SelectedNodes);

	/** Deferred tick callback that actually applies the pending selection to the details panel.
	 * Runs one frame after OnGraphSelectionChanged so we can cancel the update when the
	 * schema flags the selection change as incidental (e.g. a pin right-click that SGraphEditor
	 * auto-selected the owning node for). See UComposableCameraNodeGraph::MarkPinContextMenuRequested /
	 * ConsumePinContextMenuRequested. */
	bool ApplyPendingSelectionToDetails(float DeltaTime);

	/** Callback when variable/parameter properties on the type asset change in the main details view.
	 * Used to refresh variable graph nodes when InternalVariables is edited. */
	void OnTypeAssetPropertyChanged(const FPropertyChangedEvent& PropertyChangedEvent);

	/** Callback when a node is double-clicked. */
	void OnGraphNodeDoubleClicked(UEdGraphNode* Node);

	/** Callback when graph changes structurally. */
	void OnGraphChanged(const FEdGraphEditAction& Action);

	/** Create the command list bound to the graph editor (Delete, Cut, Copy, Paste, Duplicate, SelectAll). */
	void CreateGraphEditorCommands();

	/** Delete every currently-selected node in the graph editor (respects CanUserDeleteNode). */
	void DeleteSelectedNodes();

	/** Whether at least one selected node is user-deletable. */
	bool CanDeleteSelectedNodes() const;

	/** Copy every currently-selected copiable node to the system clipboard. */
	void CopySelectedNodes();

	/** Whether at least one selected node supports duplication. */
	bool CanCopySelectedNodes() const;

	/** Paste graph nodes from the system clipboard. */
	void PasteNodes();

	/** Whether the clipboard contains a valid node text that can be pasted. */
	bool CanPasteNodes() const;

	/** Cut = Copy + Delete. */
	void CutSelectedNodes();

	/** Can cut if we can both copy and delete at least one node. */
	bool CanCutSelectedNodes() const;

	/** Duplicate = Copy + Paste (in-place). */
	void DuplicateSelectedNodes();

	/** Select every node in the graph editor. */
	void SelectAllNodes();

	/** Whether Select All is currently meaningful. */
	bool CanSelectAllNodes() const;

	// Tab Spawners 

	TSharedRef<SDockTab> SpawnTab_GraphEditor(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Details(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_BuildMessages(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_RuntimePreviewer(const FSpawnTabArgs& Args);

	// Toolbar Actions 

	void OnBuild();

	// Widget Builders 

	TSharedRef<SWidget> BuildBuildMessagesWidget();

private:
	/** The type asset being edited. */
	UComposableCameraTypeAsset* TypeAsset = nullptr;

	/** The node graph owned by the type asset. */
	UComposableCameraNodeGraph* NodeGraph = nullptr;

	/** The graph editor widget. */
	TSharedPtr<SGraphEditor> GraphEditorWidget;

	/** Command list bound to the graph editor (Delete, Copy, Paste, Cut, Duplicate, Select All). */
	TSharedPtr<FUICommandList> GraphEditorCommands;

	/** Details panel for selected node properties. */
	TSharedPtr<IDetailsView> NodeDetailsView;

	/** Handle to `NodeDetailsView->OnFinishedChangingProperties().AddRaw(this, )`.
	 * The details view is a Slate widget held by the property tree;
	 * Slate's deferred deletion can keep it alive at least one tick past
	 * the toolkit's destruction (tab close -> toolkit dtor runs while
	 * the widget is still scheduled for deletion). A property edit on
	 * that surviving widget would then fire the delegate against a
	 * freed `this`. The destructor calls
	 * `NodeDetailsView->OnFinishedChangingProperties().Remove(handle)`
	 * to break the binding before `this` goes away. */
	FDelegateHandle NodeDetailsPropertyChangedHandle;

	/** Delegate handle for graph change notification cleanup. */
	FDelegateHandle GraphChangedDelegateHandle;

	/** The object currently shown in the details panel (tracked to avoid redundant updates). */
	TWeakObjectPtr<UObject> CurrentDetailsObject;

	/** Most recent graph selection set captured by OnGraphSelectionChanged and
	 * waiting to be applied to the details panel by the ticker. */
	TSet<TWeakObjectPtr<UObject>> PendingSelection;

	/** Whether a deferred ApplyPendingSelectionToDetails is queued on the core ticker. */
	bool bHasPendingSelection = false;

	/** Handle for the deferred ApplyPendingSelectionToDetails tick. */
	FTSTicker::FDelegateHandle PendingSelectionTickerHandle;

	// Runtime Debug Monitoring 

	/** Called when PIE starts. Begins watching for camera instances. */
	void OnPIEStarted(bool bIsSimulating);

	/** Called when PIE ends. Clears all debug state. */
	void OnPIEEnded(bool bIsSimulating);

	/** Debug ticker: runs every frame during PIE when a camera is bound.
	 * Polls the debugged camera's state and pushes it into graph nodes. */
	bool DebugTick(float DeltaTime);

	/** Scan the PIE world for cameras spawned from our TypeAsset. */
	TArray<TWeakObjectPtr<AComposableCameraCameraBase>> FindMatchingCameraInstances() const;

	/** Bind the debugger to a specific camera instance. */
	void BindToCamera(AComposableCameraCameraBase* Camera);

	/** Clear debug state from all graph nodes. */
	void ClearGraphNodeDebugState();

	/** Push the selected runtime camera + controlled pawn relation into the previewer tab. */
	void PushRuntimePreviewData(const FComposableCameraDebugSnapshot& Snapshot);

	/** Clear the previewer tab while preserving its local observer camera. */
	void ClearRuntimePreviewer(ERuntimePreviewerStatus Status);

	/** Resolve the pawn controlled by the debugged camera's owning player controller. */
	AActor* ResolveDebuggedControlledPawn() const;

	/** Build the instance picker dropdown content for the toolbar. */
	TSharedRef<SWidget> BuildDebugInstancePickerWidget();

	/** Static factory for the debug combo button widget.
	 * Resolves the owning toolkit from FToolMenuContext so each editor
	 * instance gets its own button - avoids the global UToolMenu dispatch
	 * bug where the last editor to register overwrites all others. */
	static TSharedRef<SWidget> MakeDebugInstancePickerWidget(const FToolMenuContext& Context, const FToolMenuCustomWidgetContext& WidgetContext);

	// Shot Editor toolbar entry.
	//
	// Adjacent to the Debug button. Routes to the Shot Editor (in the
	// editor module) for the currently selected `UComposableCameraCompositionFramingNode`
	// in the graph editor. Disabled when no such node is selected.
	//
	// Static handlers receive `FToolMenuContext`, resolve the per-instance
	// toolkit via `UComposableCameraTypeAssetEditorMenuContext`, and forward
	// to the member methods below - same pattern as `MakeDebugInstancePickerWidget`,
	// avoids the "last-registered-toolkit-wins" bug for global UToolMenu entries.

	/** FToolMenuExecuteAction entry. Resolves toolkit + invokes
	 * OpenShotEditorForSelectedNode(). */
	static void StaticOnOpenShotEditorClicked(const FToolMenuContext& Context);

	/** FToolMenuCanExecuteAction entry. Returns true iff a CompositionFraming
	 * node is selected in the toolkit's graph editor. */
	static bool StaticCanOpenShotEditor(const FToolMenuContext& Context);

	/** Look up the currently selected CompositionFraming graph-node template
	 * (returns nullptr if 0 or non-Composition selected). */
	class UComposableCameraCompositionFramingNode* GetSelectedCompositionFramingNode() const;

	/** If a CompositionFraming node is selected, route through the runtime
	 * hook (`FOpenShotEditor::Open`) to spawn / focus the Shot Editor tab
	 * with that node's `Shot` UPROPERTY as the active context. No-op when
	 * CanOpenShotEditor would return false. */
	void OpenShotEditorForSelectedNode();

	/** The camera instance currently being debugged. */
	TWeakObjectPtr<AComposableCameraCameraBase> DebuggedCamera;

	/** Optional Runtime Previewer dock tab content. Created only when the user opens it. */
	TSharedPtr<SComposableCameraRuntimePreviewer> RuntimePreviewerWidget;

	/** Whether we're actively in a PIE/SIE session. */
	bool bIsPIEActive = false;

	/** Handle for the PIE-started delegate. */
	FDelegateHandle PIEStartedHandle;

	/** Handle for the PIE-ended delegate. */
	FDelegateHandle PIEEndedHandle;

	/** Handle for the debug ticker. */
	FTSTicker::FDelegateHandle DebugTickerHandle;

	/** Tab IDs. */
	static const FName GraphEditorTabId;
	static const FName DetailsTabId;
	static const FName BuildMessagesTabId;
	static const FName RuntimePreviewerTabId;
};

UCLASS(Experimental)
class UComposableCameraTypeAssetEditorMenuContext: public UObject
{
	GENERATED_BODY()

public:
	TWeakPtr<FComposableCameraTypeAssetEditorToolkit> Toolkit;
};
