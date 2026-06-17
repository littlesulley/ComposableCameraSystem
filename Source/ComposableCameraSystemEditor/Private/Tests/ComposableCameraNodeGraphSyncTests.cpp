// Copyright 2026 Sulley. All Rights Reserved.

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
#include "Editors/ComposableCameraBeginPlayStartGraphNode.h"
#include "Editors/ComposableCameraGraphNodeBase.h"
#include "Editors/ComposableCameraNodeGraph.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Editors/ComposableCameraVariableGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/AutomationTest.h"
#include "Nodes/ComposableCameraCameraNodeBase.h"
#include "Nodes/ComposableCameraCameraOffsetNode.h"
#include "Nodes/ComposableCameraComputeDistanceToActorNode.h"
#include "Nodes/ComposableCameraComputePositionBetweenActorsNode.h"
#include "Nodes/ComposableCameraNodePinTypes.h"
#include "Nodes/ComposableCameraPivotOffsetNode.h"
#include "Nodes/ComposableCameraSetRotationNode.h"

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

	UComposableCameraNodeGraphNode* AddCameraNode(
		UComposableCameraTypeAsset* Asset,
		UComposableCameraNodeGraph* Graph,
		UComposableCameraCameraNodeBase* Template,
		const FVector2D& Position)
	{
		const int32 NewIndex = Asset->NodeTemplates.Add(Template);
		UComposableCameraNodeGraphNode* GraphNode =
			NewObject<UComposableCameraNodeGraphNode>(Graph);
		GraphNode->NodeTemplate = Template;
		GraphNode->NodeIndex = NewIndex;
		GraphNode->NodePosX = Position.X;
		GraphNode->NodePosY = Position.Y;
		GraphNode->AllocateDefaultPins();
		Graph->AddNode(GraphNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
		return GraphNode;
	}

	UComposableCameraNodeGraphNode* AddComputeNode(
		UComposableCameraTypeAsset* Asset,
		UComposableCameraNodeGraph* Graph,
		UComposableCameraComputeNodeBase* Template,
		const FVector2D& Position)
	{
		const int32 NewIndex = Asset->ComputeNodeTemplates.Add(Template);
		UComposableCameraNodeGraphNode* GraphNode =
			NewObject<UComposableCameraNodeGraphNode>(Graph);
		GraphNode->NodeTemplate = Template;
		GraphNode->NodeIndex = NewIndex;
		GraphNode->NodePosX = Position.X;
		GraphNode->NodePosY = Position.Y;
		GraphNode->AllocateDefaultPins();
		Graph->AddNode(GraphNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
		return GraphNode;
	}

	UComposableCameraVariableGraphNode* AddGetVariableNode(
		UComposableCameraNodeGraph* Graph,
		const FComposableCameraInternalVariable& Variable,
		const FVector2D& Position)
	{
		UComposableCameraVariableGraphNode* VarNode =
			NewObject<UComposableCameraVariableGraphNode>(Graph);
		VarNode->VariableGuid = Variable.VariableGuid;
		VarNode->VariableName = Variable.VariableName;
		VarNode->bIsSetter = false;
		VarNode->NodePosX = Position.X;
		VarNode->NodePosY = Position.Y;
		VarNode->CreateNewGuid();
		VarNode->AllocateDefaultPins();
		Graph->AddNode(VarNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
		return VarNode;
	}

	UComposableCameraVariableGraphNode* AddSetVariableNode(
		UComposableCameraNodeGraph* Graph,
		const FComposableCameraInternalVariable& Variable,
		const FVector2D& Position)
	{
		UComposableCameraVariableGraphNode* VarNode =
			NewObject<UComposableCameraVariableGraphNode>(Graph);
		VarNode->VariableGuid = Variable.VariableGuid;
		VarNode->VariableName = Variable.VariableName;
		VarNode->bIsSetter = true;
		VarNode->NodePosX = Position.X;
		VarNode->NodePosY = Position.Y;
		VarNode->CreateNewGuid();
		VarNode->AllocateDefaultPins();
		Graph->AddNode(VarNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
		return VarNode;
	}

	bool ArePinsLinked(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin)
	{
		return OutputPin && InputPin && OutputPin->LinkedTo.Contains(InputPin);
	}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraVariableGetCameraAndComputeFanOutRoundTripTest,
	"ComposableCameraSystem.Editor.NodeGraphSync.VariableGetCameraAndComputeFanOutRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraVariableGetCameraAndComputeFanOutRoundTripTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ComposableCameraNodeGraphSyncTest;

	UComposableCameraTypeAsset* Asset =
		NewObject<UComposableCameraTypeAsset>(GetTransientPackage());
	UComposableCameraNodeGraph* Graph = NewObject<UComposableCameraNodeGraph>(Asset);
	Graph->OwningTypeAsset = Asset;

	FComposableCameraInternalVariable PivotActorVariable;
	PivotActorVariable.VariableGuid = FGuid::NewGuid();
	PivotActorVariable.VariableName = TEXT("PivotActor");
	PivotActorVariable.VariableType = EComposableCameraPinType::Actor;
	Asset->ExposedVariables.Add(PivotActorVariable);

	TArray<UComposableCameraNodeGraphNode*> SetRotationGraphNodes;
	for (int32 CameraIdx = 0; CameraIdx < 2; ++CameraIdx)
	{
		UComposableCameraSetRotationNode* SetRotationTemplate =
			NewObject<UComposableCameraSetRotationNode>(Asset);
		SetRotationTemplate->RotationSource = EComposableCameraSetRotationSource::FromActor;
		UComposableCameraNodeGraphNode* SetRotationGraphNode = AddCameraNode(
			Asset, Graph, SetRotationTemplate, FVector2D(0.0, CameraIdx * 100.0));
		SetRotationGraphNode->SetPinAsPin(TEXT("RotationActor"), true);
		SetRotationGraphNode->ReconstructPins();
		SetRotationGraphNodes.Add(SetRotationGraphNode);
	}

	TArray<UComposableCameraNodeGraphNode*> DistanceGraphNodes;
	for (int32 ComputeIdx = 0; ComputeIdx < 2; ++ComputeIdx)
	{
		UComposableCameraComputeDistanceToActorNode* DistanceTemplate =
			NewObject<UComposableCameraComputeDistanceToActorNode>(Asset);
		UComposableCameraNodeGraphNode* DistanceGraphNode = AddComputeNode(
			Asset, Graph, DistanceTemplate, FVector2D(300.0, ComputeIdx * 100.0));
		DistanceGraphNode->SetPinAsPin(TEXT("ActorA"), true);
		DistanceGraphNode->ReconstructPins();
		DistanceGraphNodes.Add(DistanceGraphNode);
	}

	UComposableCameraVariableGraphNode* SharedGetNode = AddGetVariableNode(
		Graph, PivotActorVariable, FVector2D(-250.0, 150.0));

	UEdGraphPin* SharedGetValue =
		SharedGetNode->FindPin(UComposableCameraVariableGraphNode::PN_Value, EGPD_Output);
	if (!TestNotNull(TEXT("Shared get value pin exists"), SharedGetValue))
	{
		return false;
	}

	for (UComposableCameraNodeGraphNode* SetRotationGraphNode : SetRotationGraphNodes)
	{
		UEdGraphPin* RotationActorPin =
			SetRotationGraphNode->FindPin(TEXT("RotationActor"), EGPD_Input);
		if (!TestNotNull(TEXT("SetRotation RotationActor pin exists"), RotationActorPin))
		{
			return false;
		}
		SharedGetValue->MakeLinkTo(RotationActorPin);
	}
	for (UComposableCameraNodeGraphNode* DistanceGraphNode : DistanceGraphNodes)
	{
		UEdGraphPin* ActorAPin =
			DistanceGraphNode->FindPin(TEXT("ActorA"), EGPD_Input);
		if (!TestNotNull(TEXT("DistanceToActor ActorA pin exists"), ActorAPin))
		{
			return false;
		}
		SharedGetValue->MakeLinkTo(ActorAPin);
	}

	Graph->SyncToTypeAsset();

	const FComposableCameraVariableNodeRecord* GetRecord = nullptr;
	for (const FComposableCameraVariableNodeRecord& Record : Asset->VariableNodes)
	{
		if (!Record.bIsSetter && Record.VariableGuid == PivotActorVariable.VariableGuid)
		{
			GetRecord = &Record;
			break;
		}
	}

	if (!TestNotNull(TEXT("Shared Get variable record was serialized"), GetRecord))
	{
		return false;
	}
	TestEqual(TEXT("Single Get variable record stores every fan-out connection"),
		GetRecord->Connections.Num(), 4);

	int32 CameraConnectionCount = 0;
	int32 ComputeConnectionCount = 0;
	for (const FComposableCameraVariablePinConnection& Conn : GetRecord->Connections)
	{
		if (Conn.bIsComputeChain)
		{
			++ComputeConnectionCount;
		}
		else
		{
			++CameraConnectionCount;
		}
	}
	TestEqual(TEXT("Shared Get keeps two camera-chain connections"),
		CameraConnectionCount, 2);
	TestEqual(TEXT("Shared Get keeps two compute-chain connections"),
		ComputeConnectionCount, 2);

	const FComposableCameraRuntimeDataBlock RuntimeData = Asset->BuildRuntimeDataLayout();
	for (int32 CameraIdx = 0; CameraIdx < 2; ++CameraIdx)
	{
		TestTrue(TEXT("Runtime layout routes camera Get variable to SetRotation"),
			RuntimeData.InputPinSourceOffsets.Contains(
				FComposableCameraPinKey{CameraIdx, TEXT("RotationActor")}));
	}
	for (int32 ComputeIdx = 0; ComputeIdx < 2; ++ComputeIdx)
	{
		TestTrue(TEXT("Runtime layout routes compute Get variable to DistanceToActor"),
			RuntimeData.InputPinSourceOffsets.Contains(
				FComposableCameraPinKey{Asset->NodeTemplates.Num() + ComputeIdx, TEXT("ActorA")}));
	}

	Graph->RebuildFromTypeAsset();

	TArray<UComposableCameraNodeGraphNode*> RebuiltSetRotationNodes;
	TArray<UComposableCameraNodeGraphNode*> RebuiltDistanceNodes;
	UComposableCameraVariableGraphNode* RebuiltSharedGetNode = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UComposableCameraVariableGraphNode* VarNode = Cast<UComposableCameraVariableGraphNode>(Node))
		{
			if (!VarNode->bIsSetter && VarNode->VariableGuid == PivotActorVariable.VariableGuid)
			{
				RebuiltSharedGetNode = VarNode;
			}
			continue;
		}

		UComposableCameraNodeGraphNode* GraphNode = Cast<UComposableCameraNodeGraphNode>(Node);
		if (!GraphNode || !GraphNode->NodeTemplate)
		{
			continue;
		}

		if (GraphNode->NodeTemplate->IsA<UComposableCameraSetRotationNode>())
		{
			RebuiltSetRotationNodes.Add(GraphNode);
		}
		else if (GraphNode->NodeTemplate->IsA<UComposableCameraComputeDistanceToActorNode>())
		{
			RebuiltDistanceNodes.Add(GraphNode);
		}
	}

	if (!TestNotNull(TEXT("Shared Get node rebuilt"), RebuiltSharedGetNode))
	{
		return false;
	}
	TestEqual(TEXT("Two SetRotation nodes rebuilt"),
		RebuiltSetRotationNodes.Num(), 2);
	TestEqual(TEXT("Two DistanceToActor nodes rebuilt"),
		RebuiltDistanceNodes.Num(), 2);

	UEdGraphPin* RebuiltSharedGetValue =
		RebuiltSharedGetNode->FindPin(UComposableCameraVariableGraphNode::PN_Value, EGPD_Output);
	if (!TestNotNull(TEXT("Rebuilt shared Get value pin exists"), RebuiltSharedGetValue))
	{
		return false;
	}
	TestEqual(TEXT("Shared Get output keeps all fan-out wires after rebuild"),
		RebuiltSharedGetValue->LinkedTo.Num(), 4);

	for (UComposableCameraNodeGraphNode* RebuiltSetRotationNode : RebuiltSetRotationNodes)
	{
		UEdGraphPin* RebuiltRotationActorPin =
			RebuiltSetRotationNode->FindPin(TEXT("RotationActor"), EGPD_Input);
		if (!TestNotNull(TEXT("Rebuilt SetRotation RotationActor pin exists"), RebuiltRotationActorPin))
		{
			return false;
		}
		TestEqual(TEXT("SetRotation RotationActor keeps its variable wire after rebuild"),
			RebuiltRotationActorPin->LinkedTo.Num(), 1);
	}
	for (UComposableCameraNodeGraphNode* RebuiltDistanceNode : RebuiltDistanceNodes)
	{
		UEdGraphPin* RebuiltActorAPin =
			RebuiltDistanceNode->FindPin(TEXT("ActorA"), EGPD_Input);
		if (!TestNotNull(TEXT("Rebuilt DistanceToActor ActorA pin exists"), RebuiltActorAPin))
		{
			return false;
		}
		TestEqual(TEXT("DistanceToActor ActorA keeps its variable wire after rebuild"),
			RebuiltActorAPin->LinkedTo.Num(), 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraBuildMessagesUseChainLocalNodeIndicesTest,
	"ComposableCameraSystem.Editor.NodeGraphSync.BuildMessagesUseChainLocalNodeIndices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraBuildMessagesUseChainLocalNodeIndicesTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ComposableCameraNodeGraphSyncTest;

	UComposableCameraTypeAsset* Asset =
		NewObject<UComposableCameraTypeAsset>(GetTransientPackage());
	UComposableCameraNodeGraph* Graph = NewObject<UComposableCameraNodeGraph>(Asset);
	Graph->OwningTypeAsset = Asset;

	UComposableCameraNodeGraphNode* CameraGraphNode = AddCameraNode(
		Asset, Graph, NewObject<UComposableCameraCameraOffsetNode>(Asset), FVector2D(0.0, 0.0));
	UComposableCameraNodeGraphNode* ComputeGraphNode = AddComputeNode(
		Asset, Graph, NewObject<UComposableCameraComputeDistanceToActorNode>(Asset), FVector2D(300.0, 0.0));

	TestEqual(TEXT("Camera node index starts at zero"),
		CameraGraphNode->NodeIndex, 0);
	TestEqual(TEXT("Compute node index also starts at zero"),
		ComputeGraphNode->NodeIndex, 0);

	FComposableCameraBuildMessage CameraMessage;
	CameraMessage.Severity = 1;
	CameraMessage.NodeIndex = 0;
	CameraMessage.Message = FText::FromString(TEXT("camera warning"));
	Asset->BuildMessages.Add(CameraMessage);

	FComposableCameraBuildMessage ComputeMessage;
	ComputeMessage.Severity = 1;
	ComputeMessage.NodeIndex = 0;
	ComputeMessage.bIsComputeChain = true;
	ComputeMessage.Message = FText::FromString(TEXT("compute warning"));
	Asset->BuildMessages.Add(ComputeMessage);

	Graph->ApplyBuildMessagesToGraphNodes();

	TestTrue(TEXT("Camera node receives camera-chain message"),
		CameraGraphNode->ErrorMsg.Contains(TEXT("camera warning")));
	TestFalse(TEXT("Camera node does not receive compute-chain message"),
		CameraGraphNode->ErrorMsg.Contains(TEXT("compute warning")));
	TestTrue(TEXT("Compute node receives compute-chain message"),
		ComputeGraphNode->ErrorMsg.Contains(TEXT("compute warning")));
	TestFalse(TEXT("Compute node does not receive camera-chain message"),
		ComputeGraphNode->ErrorMsg.Contains(TEXT("camera warning")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraComputePinsAreNotExposedParametersTest,
	"ComposableCameraSystem.Editor.NodeGraphSync.ComputePinsAreNotExposedParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraComputePinsAreNotExposedParametersTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ComposableCameraNodeGraphSyncTest;

	UComposableCameraTypeAsset* Asset =
		NewObject<UComposableCameraTypeAsset>(GetTransientPackage());
	UComposableCameraNodeGraph* Graph = NewObject<UComposableCameraNodeGraph>(Asset);
	Graph->OwningTypeAsset = Asset;

	UComposableCameraNodeGraphNode* ComputeGraphNode = AddComputeNode(
		Asset, Graph, NewObject<UComposableCameraComputeDistanceToActorNode>(Asset), FVector2D(0.0, 0.0));

	FComposableCameraExposedParameter CameraParam;
	CameraParam.ParameterName = TEXT("ActorA");
	CameraParam.TargetNodeIndex = ComputeGraphNode->NodeIndex;
	CameraParam.TargetPinName = TEXT("ActorA");
	Asset->ExposedParameters.Add(CameraParam);

	TestFalse(TEXT("Compute node with overlapping NodeIndex is not treated as exposed"),
		ComputeGraphNode->IsInputPinExposed(TEXT("ActorA")));

	const int32 ExposedCountBefore = Asset->ExposedParameters.Num();
	ComputeGraphNode->ExposePinAsParameter(TEXT("ActorB"));
	TestEqual(TEXT("ExposePinAsParameter ignores compute nodes"),
		Asset->ExposedParameters.Num(), ExposedCountBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraComputeRequiredInputsValidatedTest,
	"ComposableCameraSystem.Editor.TypeAssetBuild.ComputeRequiredInputsValidated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraComputeRequiredInputsValidatedTest::RunTest(const FString& /*Parameters*/)
{
	UComposableCameraTypeAsset* Asset =
		NewObject<UComposableCameraTypeAsset>(GetTransientPackage());

	Asset->NodeTemplates.Add(NewObject<UComposableCameraCameraOffsetNode>(Asset));

	FComposableCameraInternalVariable PivotActorVariable;
	PivotActorVariable.VariableGuid = FGuid::NewGuid();
	PivotActorVariable.VariableName = TEXT("PivotActor");
	PivotActorVariable.VariableType = EComposableCameraPinType::Actor;
	Asset->ExposedVariables.Add(PivotActorVariable);

	UComposableCameraComputeDistanceToActorNode* DistanceNode =
		NewObject<UComposableCameraComputeDistanceToActorNode>(Asset);
	Asset->ComputeNodeTemplates.Add(DistanceNode);

	FComposableCameraVariableNodeRecord GetRecord;
	GetRecord.VariableGuid = PivotActorVariable.VariableGuid;
	GetRecord.VariableName = PivotActorVariable.VariableName;
	GetRecord.bIsSetter = false;

	FComposableCameraVariablePinConnection ActorAConnection;
	ActorAConnection.CameraNodeIndex = 0;
	ActorAConnection.CameraPinName = TEXT("ActorA");
	ActorAConnection.bIsComputeChain = true;
	GetRecord.Connections.Add(ActorAConnection);
	Asset->VariableNodes.Add(GetRecord);

	Asset->Build(/*bLogResult=*/false);

	bool bActorAMissing = false;
	bool bActorBMissing = false;
	for (const FComposableCameraBuildMessage& Msg : Asset->BuildMessages)
	{
		if (Msg.Severity < 2 || !Msg.bIsComputeChain || Msg.NodeIndex != 0)
		{
			continue;
		}
		if (Msg.PinName == TEXT("ActorA"))
		{
			bActorAMissing = true;
		}
		else if (Msg.PinName == TEXT("ActorB"))
		{
			bActorBMissing = true;
		}
	}

	TestFalse(TEXT("Compute variable Get resolves ActorA required input"),
		bActorAMissing);
	TestTrue(TEXT("Compute build validation flags unresolved ActorB required input"),
		bActorBMissing);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraComputeSetVariableRejectsCameraSourceTest,
	"ComposableCameraSystem.Editor.NodeGraphSync.ComputeSetVariableRejectsCameraSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraComputeSetVariableRejectsCameraSourceTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ComposableCameraNodeGraphSyncTest;

	UComposableCameraTypeAsset* Asset =
		NewObject<UComposableCameraTypeAsset>(GetTransientPackage());
	UComposableCameraNodeGraph* Graph = NewObject<UComposableCameraNodeGraph>(Asset);
	Graph->OwningTypeAsset = Asset;

	FComposableCameraInternalVariable PivotVariable;
	PivotVariable.VariableGuid = FGuid::NewGuid();
	PivotVariable.VariableName = TEXT("PivotVector");
	PivotVariable.VariableType = EComposableCameraPinType::Vector3D;
	Asset->InternalVariables.Add(PivotVariable);

	UComposableCameraNodeGraphNode* CameraSourceNode = AddCameraNode(
		Asset, Graph, NewObject<UComposableCameraPivotOffsetNode>(Asset), FVector2D(0.0, 0.0));
	UComposableCameraVariableGraphNode* SetNode = AddSetVariableNode(
		Graph, PivotVariable, FVector2D(300.0, 0.0));

	UComposableCameraBeginPlayStartGraphNode* BeginPlayStart =
		NewObject<UComposableCameraBeginPlayStartGraphNode>(Graph);
	BeginPlayStart->CreateNewGuid();
	BeginPlayStart->AllocateDefaultPins();
	Graph->AddNode(BeginPlayStart, /*bFromUI=*/false, /*bSelectNewNode=*/false);

	UEdGraphPin* CameraOutput =
		CameraSourceNode->FindPin(TEXT("PivotPosition"), EGPD_Output);
	UEdGraphPin* SetValue =
		SetNode->FindPin(UComposableCameraVariableGraphNode::PN_Value, EGPD_Input);
	UEdGraphPin* BeginExecOut =
		BeginPlayStart->FindPin(UComposableCameraGraphNodeBase::PN_ExecOut, EGPD_Output);
	UEdGraphPin* SetExecIn =
		SetNode->FindPin(UComposableCameraVariableGraphNode::PN_ExecIn, EGPD_Input);

	if (!TestNotNull(TEXT("Camera PivotPosition output exists"), CameraOutput) ||
		!TestNotNull(TEXT("Set Value input exists"), SetValue) ||
		!TestNotNull(TEXT("BeginPlay ExecOut exists"), BeginExecOut) ||
		!TestNotNull(TEXT("Set ExecIn exists"), SetExecIn))
	{
		return false;
	}

	CameraOutput->MakeLinkTo(SetValue);
	BeginExecOut->MakeLinkTo(SetExecIn);

	Graph->SyncToTypeAsset();

	const FComposableCameraVariableNodeRecord* SetRecord = nullptr;
	for (const FComposableCameraVariableNodeRecord& Record : Asset->VariableNodes)
	{
		if (Record.bIsSetter && Record.VariableGuid == PivotVariable.VariableGuid)
		{
			SetRecord = &Record;
			break;
		}
	}
	if (!TestNotNull(TEXT("Set variable record was serialized"), SetRecord))
	{
		return false;
	}
	TestEqual(TEXT("Compute SetVariable record drops cross-chain value wire"),
		SetRecord->Connections.Num(), 0);

	if (!TestEqual(TEXT("Compute chain serialized one SetVariable entry"),
			Asset->ComputeFullExecChain.Num(), 1))
	{
		return false;
	}

	const FComposableCameraExecEntry& Entry = Asset->ComputeFullExecChain[0];
	TestTrue(TEXT("Entry is SetVariable"),
		Entry.EntryType == EComposableCameraExecEntryType::SetVariable);
	TestEqual(TEXT("Compute SetVariable does not serialize a camera-space source index"),
		Entry.CameraNodeIndex, INDEX_NONE);
	TestTrue(TEXT("Compute SetVariable keeps source pin empty when source is cross-chain"),
		Entry.SourcePinName.IsNone());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraBeginPlaySetVariableExecRoundTripWithGetNodeTest,
	"ComposableCameraSystem.Editor.NodeGraphSync.BeginPlaySetVariableExecRoundTripWithGetNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraBeginPlaySetVariableExecRoundTripWithGetNodeTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ComposableCameraNodeGraphSyncTest;

	UComposableCameraTypeAsset* Asset =
		NewObject<UComposableCameraTypeAsset>(GetTransientPackage());
	UComposableCameraNodeGraph* Graph = NewObject<UComposableCameraNodeGraph>(Asset);
	Graph->OwningTypeAsset = Asset;

	FComposableCameraInternalVariable PivotVariable;
	PivotVariable.VariableGuid = FGuid::NewGuid();
	PivotVariable.VariableName = TEXT("PivotPosition");
	PivotVariable.VariableType = EComposableCameraPinType::Vector3D;
	Asset->InternalVariables.Add(PivotVariable);

	UComposableCameraBeginPlayStartGraphNode* BeginPlayStart =
		NewObject<UComposableCameraBeginPlayStartGraphNode>(Graph);
	BeginPlayStart->CreateNewGuid();
	BeginPlayStart->AllocateDefaultPins();
	Graph->AddNode(BeginPlayStart, /*bFromUI=*/false, /*bSelectNewNode=*/false);

	UComposableCameraNodeGraphNode* PositionNode = AddComputeNode(
		Asset, Graph, NewObject<UComposableCameraComputePositionBetweenActorsNode>(Asset), FVector2D(300.0, 400.0));
	UComposableCameraVariableGraphNode* SetPivotNode = AddSetVariableNode(
		Graph, PivotVariable, FVector2D(600.0, 400.0));
	UComposableCameraVariableGraphNode* GetPivotNode = AddGetVariableNode(
		Graph, PivotVariable, FVector2D(600.0, 520.0));
	UComposableCameraNodeGraphNode* RotationNode = AddComputeNode(
		Asset, Graph, NewObject<UComposableCameraBeginPlaySetRotationNode>(Asset), FVector2D(900.0, 400.0));

	UEdGraphPin* BeginExecOut =
		BeginPlayStart->FindPin(UComposableCameraGraphNodeBase::PN_ExecOut, EGPD_Output);
	UEdGraphPin* PositionExecIn =
		PositionNode->FindPin(UComposableCameraNodeGraphNode::PN_ExecIn, EGPD_Input);
	UEdGraphPin* PositionExecOut =
		PositionNode->FindPin(UComposableCameraNodeGraphNode::PN_ExecOut, EGPD_Output);
	UEdGraphPin* PositionOutput =
		PositionNode->FindPin(TEXT("Position"), EGPD_Output);
	UEdGraphPin* SetExecIn =
		SetPivotNode->FindPin(UComposableCameraVariableGraphNode::PN_ExecIn, EGPD_Input);
	UEdGraphPin* SetExecOut =
		SetPivotNode->FindPin(UComposableCameraVariableGraphNode::PN_ExecOut, EGPD_Output);
	UEdGraphPin* SetValue =
		SetPivotNode->FindPin(UComposableCameraVariableGraphNode::PN_Value, EGPD_Input);
	UEdGraphPin* RotationExecIn =
		RotationNode->FindPin(UComposableCameraNodeGraphNode::PN_ExecIn, EGPD_Input);

	if (!TestNotNull(TEXT("BeginPlay ExecOut exists"), BeginExecOut) ||
		!TestNotNull(TEXT("Position ExecIn exists"), PositionExecIn) ||
		!TestNotNull(TEXT("Position ExecOut exists"), PositionExecOut) ||
		!TestNotNull(TEXT("Position output exists"), PositionOutput) ||
		!TestNotNull(TEXT("Set ExecIn exists"), SetExecIn) ||
		!TestNotNull(TEXT("Set ExecOut exists"), SetExecOut) ||
		!TestNotNull(TEXT("Set Value exists"), SetValue) ||
		!TestNotNull(TEXT("Rotation ExecIn exists"), RotationExecIn))
	{
		return false;
	}

	BeginExecOut->MakeLinkTo(PositionExecIn);
	PositionExecOut->MakeLinkTo(SetExecIn);
	SetExecOut->MakeLinkTo(RotationExecIn);
	PositionOutput->MakeLinkTo(SetValue);

	Graph->SyncToTypeAsset();

	if (!TestEqual(TEXT("BeginPlay chain serialized compute, set, compute"),
			Asset->ComputeFullExecChain.Num(), 3))
	{
		return false;
	}
	TestTrue(TEXT("Set entry keeps the exact Set node identity"),
		Asset->ComputeFullExecChain[1].VariableNodeGuid == SetPivotNode->NodeGuid);

	Graph->RebuildFromTypeAsset();

	UComposableCameraBeginPlayStartGraphNode* RebuiltBeginPlayStart = nullptr;
	UComposableCameraNodeGraphNode* RebuiltPositionNode = nullptr;
	UComposableCameraVariableGraphNode* RebuiltSetPivotNode = nullptr;
	UComposableCameraVariableGraphNode* RebuiltGetPivotNode = nullptr;
	UComposableCameraNodeGraphNode* RebuiltRotationNode = nullptr;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UComposableCameraBeginPlayStartGraphNode* BeginPlayCandidate =
			Cast<UComposableCameraBeginPlayStartGraphNode>(Node))
		{
			RebuiltBeginPlayStart = BeginPlayCandidate;
		}
		else if (UComposableCameraVariableGraphNode* VariableCandidate =
			Cast<UComposableCameraVariableGraphNode>(Node))
		{
			if (VariableCandidate->NodeGuid == SetPivotNode->NodeGuid)
			{
				RebuiltSetPivotNode = VariableCandidate;
			}
			else if (VariableCandidate->NodeGuid == GetPivotNode->NodeGuid)
			{
				RebuiltGetPivotNode = VariableCandidate;
			}
		}
		else if (UComposableCameraNodeGraphNode* GraphNodeCandidate =
			Cast<UComposableCameraNodeGraphNode>(Node))
		{
			if (GraphNodeCandidate->NodeTemplate &&
				GraphNodeCandidate->NodeTemplate->IsA<UComposableCameraComputePositionBetweenActorsNode>())
			{
				RebuiltPositionNode = GraphNodeCandidate;
			}
			else if (GraphNodeCandidate->NodeTemplate &&
				GraphNodeCandidate->NodeTemplate->IsA<UComposableCameraBeginPlaySetRotationNode>())
			{
				RebuiltRotationNode = GraphNodeCandidate;
			}
		}
	}

	if (!TestNotNull(TEXT("Rebuilt BeginPlay start exists"), RebuiltBeginPlayStart) ||
		!TestNotNull(TEXT("Rebuilt position compute node exists"), RebuiltPositionNode) ||
		!TestNotNull(TEXT("Rebuilt Set PivotPosition node exists"), RebuiltSetPivotNode) ||
		!TestNotNull(TEXT("Rebuilt Get PivotPosition node exists"), RebuiltGetPivotNode) ||
		!TestNotNull(TEXT("Rebuilt rotation compute node exists"), RebuiltRotationNode))
	{
		return false;
	}

	TestTrue(TEXT("BeginPlay reconnects to first compute node"),
		ArePinsLinked(
			RebuiltBeginPlayStart->FindPin(UComposableCameraGraphNodeBase::PN_ExecOut, EGPD_Output),
			RebuiltPositionNode->FindPin(UComposableCameraNodeGraphNode::PN_ExecIn, EGPD_Input)));
	TestTrue(TEXT("First compute node reconnects to exact Set node, not the same-variable Get node"),
		ArePinsLinked(
			RebuiltPositionNode->FindPin(UComposableCameraNodeGraphNode::PN_ExecOut, EGPD_Output),
			RebuiltSetPivotNode->FindPin(UComposableCameraVariableGraphNode::PN_ExecIn, EGPD_Input)));
	TestTrue(TEXT("Set node reconnects to next BeginPlay compute node"),
		ArePinsLinked(
			RebuiltSetPivotNode->FindPin(UComposableCameraVariableGraphNode::PN_ExecOut, EGPD_Output),
			RebuiltRotationNode->FindPin(UComposableCameraNodeGraphNode::PN_ExecIn, EGPD_Input)));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

#undef LOCTEXT_NAMESPACE
