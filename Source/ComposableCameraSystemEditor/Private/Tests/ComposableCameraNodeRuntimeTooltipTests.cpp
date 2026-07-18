// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Editors/ComposableCameraNodeGraph.h"
#include "Editors/SComposableCameraGraphNode.h"
#include "ComposableCameraEditorStyle.h"
#include "Misc/AutomationTest.h"
#include "Nodes/ComposableCameraFieldOfViewNode.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/IToolTip.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraNodeRuntimeTooltipTest,
	"ComposableCameraSystem.Editor.Debug.NodeRuntimeTooltip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraNodeRuntimeTooltipTest::RunTest(const FString& /*Parameters*/)
{
	UComposableCameraNodeGraph* Graph =
		NewObject<UComposableCameraNodeGraph>(GetTransientPackage());
	UComposableCameraNodeGraphNode* GraphNode =
		NewObject<UComposableCameraNodeGraphNode>(Graph);
	GraphNode->NodeTemplate =
		NewObject<UComposableCameraFieldOfViewNode>(GraphNode);
	GraphNode->AllocateDefaultPins();
	Graph->Nodes.Add(GraphNode);
	GraphNode->DebugState.bHasRuntimeData = true;
	GraphNode->DebugState.ParameterDisplayValues.Emplace(TEXT("Field Of View"), TEXT("91.50"));
	GraphNode->DebugState.ParameterDisplayValues.Emplace(TEXT("Dynamic FOV"), TEXT("true"));

	const FString RuntimeTooltip = GraphNode->GetTooltipText().ToString();
	TestTrue(TEXT("Tooltip includes runtime section"), RuntimeTooltip.Contains(TEXT("Runtime Parameters")));
	TestTrue(TEXT("Tooltip includes first live value"), RuntimeTooltip.Contains(TEXT("Field Of View: 91.50")));
	TestTrue(TEXT("Tooltip includes second live value"), RuntimeTooltip.Contains(TEXT("Dynamic FOV: true")));

	const TSharedRef<FComposableCameraEditorStyle> Style = FComposableCameraEditorStyle::Get();
	TestNotNull(TEXT("Dark tooltip background brush is registered"),
		Style->GetOptionalBrush(TEXT("DebugTooltip.Background"), nullptr, nullptr));
	TestNotNull(TEXT("Tooltip parameter row brush is registered"),
		Style->GetOptionalBrush(TEXT("DebugTooltip.Row"), nullptr, nullptr));
	TestNotNull(TEXT("Tooltip active badge brush is registered"),
		Style->GetOptionalBrush(TEXT("DebugTooltip.ActiveBadge"), nullptr, nullptr));

	const TSharedRef<SComposableCameraGraphNode> SlateNode =
		SNew(SComposableCameraGraphNode, GraphNode);
	const TSharedPtr<IToolTip> DebugToolTip = SlateNode->GetToolTip();
	TestTrue(TEXT("Runtime debug tooltip is created"), DebugToolTip.IsValid());
	if (DebugToolTip.IsValid())
	{
		TestTrue(TEXT("Runtime debug tooltip is interactive for scrolling"), DebugToolTip->IsInteractive());
	}
	TestFalse(TEXT("Source hover keeps the interactive tooltip open"),
		SComposableCameraGraphNode::ShouldCloseRuntimeDebugToolTipForTesting(
			true,
			false,
			1.0));
	TestFalse(TEXT("Tooltip hover keeps the interactive tooltip open"),
		SComposableCameraGraphNode::ShouldCloseRuntimeDebugToolTipForTesting(
			false,
			true,
			1.0));
	TestFalse(TEXT("Leave grace prevents closing while moving into the card"),
		SComposableCameraGraphNode::ShouldCloseRuntimeDebugToolTipForTesting(
			false,
			false,
			0.05));
	TestTrue(TEXT("Leaving both source and card closes the tooltip"),
		SComposableCameraGraphNode::ShouldCloseRuntimeDebugToolTipForTesting(
			false,
			false,
			1.0));

	if (FSlateApplication::IsInitialized())
	{
		SlateNode->PinRuntimeDebugWindow();
		TestTrue(TEXT("Runtime debug tooltip can be pinned into a persistent window"),
			SlateNode->IsRuntimeDebugWindowPinned());
		TestTrue(TEXT("Pinning promotes the existing hover card instead of rebuilding it"),
			SlateNode->DidLastPinReuseHoverCardForTesting());
		TestTrue(TEXT("Pinning preserves the captured physical screen position"),
			SlateNode->DidLastPinPreserveScreenPositionForTesting());
		SlateNode->PinRuntimeDebugWindow();
		TestTrue(TEXT("Pinning twice reuses the existing window"),
			SlateNode->IsRuntimeDebugWindowPinned());
		SlateNode->ClosePinnedRuntimeDebugWindow();
		TestFalse(TEXT("Pinned runtime debug window can be closed"),
			SlateNode->IsRuntimeDebugWindowPinned());
	}

	GraphNode->DebugState.Reset();
	SlateNode->OnToolTipClosing();
	SlateNode->PinRuntimeDebugWindow();
	TestFalse(TEXT("Authoring mode cannot create a runtime debug window"),
		SlateNode->IsRuntimeDebugWindowPinned());
	const TSharedPtr<IToolTip> AuthoringToolTip = SlateNode->GetToolTip();
	TestTrue(TEXT("Authoring mode falls back to standard graph tooltip"), AuthoringToolTip.IsValid());
	if (AuthoringToolTip.IsValid())
	{
		TestFalse(TEXT("Standard authoring tooltip remains non-interactive"), AuthoringToolTip->IsInteractive());
	}

	const FString ResetTooltip = GraphNode->GetTooltipText().ToString();
	TestFalse(TEXT("Runtime section disappears after debug reset"), ResetTooltip.Contains(TEXT("Runtime Parameters")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
