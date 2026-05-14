// Copyright Sulley. All rights reserved.

// Regression tests for the visual-position-driven NodeTemplates ordering
// added when the Patch / Camera Type Asset editor was found to ignore
// canvas drag when computing the runtime `CameraNodes` array fallback
// order. The bug: a designer adding FocusPull then Lens and visually
// dragging Lens above FocusPull observed FocusPull ticking first at
// runtime regardless of the visual rearrangement, because
// `SyncPhase_CollectGraphNodes` sorted graph-node buckets by NodeIndex
// (creation order), `SyncPhase_RebuildNodeTemplatesAndPositions` wrote
// NodeTemplates in that order, and the per-frame TickCamera fallback
// walked CameraNodes linearly when no exec wires existed.
//
// Tests:
//  1. SyncToTypeAsset reorders NodeTemplates so the visually-higher
//     graph node (smaller NodePosY) sorts first, even though it was
//     created second. NodeIndex on the moved graph nodes is reassigned
//     to match the new array layout.
//  2. Build() emits a Warning per camera node template not reachable
//     from Start via FullExecChain when the chain is non-empty (the
//     wired-but-orphaned case). BuildStatus is SuccessWithWarnings.
//  3. Build() does NOT emit the unreachable-node warning when
//     FullExecChain is empty (the empty-chain case, which falls into
//     the runtime's linear-walk fallback path).

#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Editors/ComposableCameraNodeGraph.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Misc/AutomationTest.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraCameraOffsetNode.h"
#include "Nodes/ComposableCameraNodePinTypes.h"

#define LOCTEXT_NAMESPACE "ComposableCameraNodeGraphSyncTests"

#if WITH_DEV_AUTOMATION_TESTS

namespace ComposableCameraNodeGraphSyncTest
{
	// Build a transient TypeAsset + attached NodeGraph and add `Count` camera
	// graph nodes backed by fresh UComposableCameraCameraOffsetNode templates.
	// Each node is positioned at (X = Idx * 200, Y = Idx * 100) by default -
	// callers override individual positions after this returns. The templates
	// are also pre-registered on the asset's NodeTemplates array in creation
	// order so the sync's old -> new index map (phase 2) has something to walk.
	struct FFixture
	{
		UComposableCameraTypeAsset* Asset = nullptr;
		UComposableCameraNodeGraph* Graph = nullptr;
		TArray<UComposableCameraNodeGraphNode*> GraphNodes;
		TArray<UComposableCameraCameraNodeBase*> Templates;

		void Build(int32 Count)
		{
			Asset = NewObject<UComposableCameraTypeAsset>(GetTransientPackage());
			Graph = NewObject<UComposableCameraNodeGraph>(Asset);
			Graph->OwningTypeAsset = Asset;

			GraphNodes.Reset();
			Templates.Reset();
			for (int32 i = 0; i < Count; ++i)
			{
				UComposableCameraCameraOffsetNode* Template =
					NewObject<UComposableCameraCameraOffsetNode>(Asset);
				const int32 NewIndex = Asset->NodeTemplates.Add(Template);
				Templates.Add(Template);

				UComposableCameraNodeGraphNode* GraphNode =
					NewObject<UComposableCameraNodeGraphNode>(Graph);
				GraphNode->NodeTemplate = Template;
				GraphNode->NodeIndex = NewIndex;
				GraphNode->NodePosX = static_cast<float>(i * 200);
				GraphNode->NodePosY = static_cast<float>(i * 100);
				Graph->Nodes.Add(GraphNode);
				GraphNodes.Add(GraphNode);
			}
		}
	};
}

// Test 1. Visual-position-driven NodeTemplates ordering

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraNodeGraphSyncSortByVisualPositionTest,
	"ComposableCameraSystem.Editor.NodeGraphSync.SortByVisualPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraNodeGraphSyncSortByVisualPositionTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ComposableCameraNodeGraphSyncTest;

	FFixture Fixture;
	Fixture.Build(/*Count=*/2);

	// Simulate "add Node A first, then Node B, then visually drag B above A".
	// Creation order is [A, B] (NodeIndex 0 and 1, NodeTemplates [A, B]).
	// Y positions: B above A means smaller Y for B.
	Fixture.GraphNodes[0]->NodePosY = 100.0f; // Node A stays at Y = 100
	Fixture.GraphNodes[1]->NodePosY = 0.0f;   // Node B dragged to Y = 0
	UComposableCameraCameraNodeBase* TemplateA = Fixture.Templates[0];
	UComposableCameraCameraNodeBase* TemplateB = Fixture.Templates[1];

	Fixture.Graph->SyncToTypeAsset();

	if (!TestEqual(TEXT("NodeTemplates resized to 2 after sync"),
			Fixture.Asset->NodeTemplates.Num(), 2))
	{
		return false;
	}
	// Visually-higher node (B at Y=0) must land at NodeTemplates[0].
	TestTrue(TEXT("NodeTemplates[0] is the visually-higher node (B)"),
		Fixture.Asset->NodeTemplates[0] == TemplateB);
	TestTrue(TEXT("NodeTemplates[1] is the visually-lower node (A)"),
		Fixture.Asset->NodeTemplates[1] == TemplateA);

	// Graph nodes' NodeIndex has been reassigned to match the new array order.
	TestEqual(TEXT("Node B's NodeIndex is now 0"),
		Fixture.GraphNodes[1]->NodeIndex, 0);
	TestEqual(TEXT("Node A's NodeIndex is now 1"),
		Fixture.GraphNodes[0]->NodeIndex, 1);

	// Canvas positions survive the round-trip in the parallel array.
	if (TestEqual(TEXT("NodeTemplatePositions has 2 entries"),
			Fixture.Asset->NodeTemplatePositions.Num(), 2))
	{
		TestEqual(TEXT("Position[0] matches Node B's Y"),
			Fixture.Asset->NodeTemplatePositions[0].Y, 0.0);
		TestEqual(TEXT("Position[1] matches Node A's Y"),
			Fixture.Asset->NodeTemplatePositions[1].Y, 100.0);
	}

	return true;
}

// Test 2. Build warning for camera nodes orphaned from FullExecChain

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraTypeAssetBuildOrphanCameraNodeWarningTest,
	"ComposableCameraSystem.Editor.TypeAssetBuild.OrphanCameraNodeWarning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTypeAssetBuildOrphanCameraNodeWarningTest::RunTest(const FString& /*Parameters*/)
{
	UComposableCameraTypeAsset* Asset =
		NewObject<UComposableCameraTypeAsset>(GetTransientPackage());

	// Two camera node templates, one reachable from Start, one orphaned.
	UComposableCameraCameraOffsetNode* TemplateA =
		NewObject<UComposableCameraCameraOffsetNode>(Asset);
	UComposableCameraCameraOffsetNode* TemplateB =
		NewObject<UComposableCameraCameraOffsetNode>(Asset);
	Asset->NodeTemplates.Add(TemplateA);
	Asset->NodeTemplates.Add(TemplateB);

	// Hand-craft a FullExecChain that references TemplateA (index 0) only.
	// TemplateB at index 1 is the orphan we expect the warning for.
	FComposableCameraExecEntry Entry;
	Entry.EntryType = EComposableCameraExecEntryType::CameraNode;
	Entry.CameraNodeIndex = 0;
	Asset->FullExecChain.Add(Entry);

	Asset->Build(/*bLogResult=*/false);

	// Walk BuildMessages looking for the orphan warning targeting NodeIndex == 1.
	bool bFoundOrphanWarningForNode1 = false;
	bool bFoundOrphanWarningForNode0 = false;
	for (const FComposableCameraBuildMessage& Msg : Asset->BuildMessages)
	{
		if (Msg.Severity != 1) // Warnings only.
		{
			continue;
		}
		if (!Msg.Message.ToString().Contains(TEXT("not wired into the execution chain")))
		{
			continue;
		}
		if (Msg.NodeIndex == 1)
		{
			bFoundOrphanWarningForNode1 = true;
		}
		else if (Msg.NodeIndex == 0)
		{
			bFoundOrphanWarningForNode0 = true;
		}
	}

	TestTrue(TEXT("Orphan warning emitted for unwired node (index 1)"),
		bFoundOrphanWarningForNode1);
	TestFalse(TEXT("No orphan warning emitted for wired node (index 0)"),
		bFoundOrphanWarningForNode0);

	// BuildStatus rolls to SuccessWithWarnings (never Failed - the orphan
	// case is a non-fatal authoring oversight, not a build break).
	TestTrue(TEXT("BuildStatus is SuccessWithWarnings or stricter (warnings present)"),
		Asset->BuildStatus == EComposableCameraBuildStatus::SuccessWithWarnings
		|| Asset->BuildStatus == EComposableCameraBuildStatus::Failed);

	return true;
}

// Test 3. Empty FullExecChain does NOT trigger the orphan warning

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraTypeAssetBuildEmptyExecChainSilentTest,
	"ComposableCameraSystem.Editor.TypeAssetBuild.EmptyExecChainSilent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTypeAssetBuildEmptyExecChainSilentTest::RunTest(const FString& /*Parameters*/)
{
	UComposableCameraTypeAsset* Asset =
		NewObject<UComposableCameraTypeAsset>(GetTransientPackage());

	UComposableCameraCameraOffsetNode* TemplateA =
		NewObject<UComposableCameraCameraOffsetNode>(Asset);
	UComposableCameraCameraOffsetNode* TemplateB =
		NewObject<UComposableCameraCameraOffsetNode>(Asset);
	Asset->NodeTemplates.Add(TemplateA);
	Asset->NodeTemplates.Add(TemplateB);

	// FullExecChain is intentionally left empty - the "no exec wires"
	// case that falls through to the runtime's linear-walk fallback.
	// The orphan warning must NOT fire here; the runtime gracefully ticks
	// every node in NodeTemplates order in this regime.
	Asset->Build(/*bLogResult=*/false);

	int32 OrphanWarningCount = 0;
	for (const FComposableCameraBuildMessage& Msg : Asset->BuildMessages)
	{
		if (Msg.Severity == 1
			&& Msg.Message.ToString().Contains(TEXT("not wired into the execution chain")))
		{
			++OrphanWarningCount;
		}
	}

	TestEqual(TEXT("No orphan warning when FullExecChain is empty"),
		OrphanWarningCount, 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

#undef LOCTEXT_NAMESPACE
