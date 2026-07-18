// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/ComposableCameraNodeGraph.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Misc/AutomationTest.h"
#include "Nodes/ComposableCameraFieldOfViewNode.h"
#include "Widgets/SComposableCameraRuntimeDebugPanel.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraRuntimeDebugPanelTest,
	"ComposableCameraSystem.Editor.Debug.RuntimeDebugPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraRuntimeDebugPanelTest::RunTest(const FString& /*Parameters*/)
{
	UComposableCameraNodeGraph* Graph =
		NewObject<UComposableCameraNodeGraph>(GetTransientPackage());

	auto AddNode = [Graph](int32 NodeIndex, bool bActive)
	{
		UComposableCameraNodeGraphNode* Node =
			NewObject<UComposableCameraNodeGraphNode>(Graph);
		Node->NodeTemplate = NewObject<UComposableCameraFieldOfViewNode>(Node);
		Node->NodeIndex = NodeIndex;
		Node->CreateNewGuid();
		Node->AllocateDefaultPins();
		Node->DebugState.bHasRuntimeData = true;
		Node->DebugState.bIsActive = bActive;
		Graph->Nodes.Add(Node);
		return Node;
	};

	UComposableCameraNodeGraphNode* ActiveNode = AddNode(0, true);
	ActiveNode->DebugState.ParameterDisplayValues.Emplace(
		TEXT("Field Of View"),
		TEXT("91.50"));
	UComposableCameraNodeGraphNode* InactiveNode = AddNode(1, false);
	InactiveNode->DebugState.ParameterDisplayValues.Emplace(
		TEXT("Hidden Inactive"),
		TEXT("123"));

	const TSharedRef<SComposableCameraRuntimeDebugPanel> Panel =
		SNew(SComposableCameraRuntimeDebugPanel)
		.NodeGraph(Graph);

	TestEqual(TEXT("Panel includes only active camera nodes"),
		Panel->GetVisibleNodeCount(),
		1);
	TestTrue(TEXT("Initial active rows schedule one post-rebuild layout refresh"),
		Panel->IsPostRebuildLayoutRefreshPendingForTesting());
	Panel->HandleItemsRebuiltForTesting();
	TestFalse(TEXT("Post-rebuild layout refresh is consumed exactly once"),
		Panel->IsPostRebuildLayoutRefreshPendingForTesting());
	Panel->SetNodeExpanded(ActiveNode, false);
	TestFalse(TEXT("Active item can be collapsed"),
		Panel->IsNodeExpanded(ActiveNode));
	TestTrue(TEXT("Collapsing a variable-height row schedules remeasurement"),
		Panel->IsPostRebuildLayoutRefreshPendingForTesting());
	Panel->HandleItemsRebuiltForTesting();
	TestFalse(TEXT("Collapse remeasurement is consumed exactly once"),
		Panel->IsPostRebuildLayoutRefreshPendingForTesting());
	Panel->SetSearchText(FText::FromString(TEXT("Hidden Inactive")));
	TestEqual(TEXT("Search never reveals inactive nodes"),
		Panel->GetVisibleNodeCount(),
		0);
	Panel->SetSearchText(FText::FromString(TEXT("Field Of View")));
	TestEqual(TEXT("Search matches active node parameter labels"),
		Panel->GetVisibleNodeCount(),
		1);

	Panel->SetSearchText(FText::FromString(TEXT("Hidden Inactive")));
	TestTrue(TEXT("Focus succeeds for an active node"),
		Panel->FocusNode(ActiveNode));
	TestTrue(TEXT("Focus expands the target item"),
		Panel->IsNodeExpanded(ActiveNode));
	TestTrue(TEXT("Focus starts a whole-item color transition"),
		Panel->IsNodeFocusTransitionActive(ActiveNode));
	TestTrue(TEXT("Focus starts with a clearly visible whole-item overlay"),
		Panel->GetNodeFocusOverlayTintForTesting(ActiveNode).A >= 0.25f);
	TestEqual(TEXT("Focus does not create a persistent list selection"),
		Panel->GetSelectedNodeCount(),
		0);
	TestEqual(TEXT("Focus clears a filter that could hide the target"),
		Panel->GetVisibleNodeCount(),
		1);
	TestFalse(TEXT("Focus rejects inactive nodes"),
		Panel->FocusNode(InactiveNode));

	UComposableCameraNodeGraphNode* RequestedNode = nullptr;
	const FDelegateHandle RequestHandle = Graph->OnRequestShowRuntimeDebug().AddLambda(
		[&RequestedNode](UComposableCameraNodeGraphNode* Node)
		{
			RequestedNode = Node;
		});
	Graph->RequestShowRuntimeDebug(ActiveNode);
	TestTrue(TEXT("Graph request bridge forwards the requested node"),
		RequestedNode == ActiveNode);
	Graph->OnRequestShowRuntimeDebug().Remove(RequestHandle);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
