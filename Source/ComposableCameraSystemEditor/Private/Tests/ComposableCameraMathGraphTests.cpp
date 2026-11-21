// Copyright 2026 Sulley. All Rights Reserved.
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Editors/ComposableCameraNodeGraph.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Editors/ComposableCameraNodeGraphSchema.h"
#include "Editors/ComposableCameraOutputGraphNode.h"
#include "Editors/ComposableCameraStartGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Nodes/ComposableCameraFieldOfViewNode.h"
#include "Nodes/ComposableCameraMathNodes.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace ComposableCameraMathGraphTests
{
	UComposableCameraNodeGraph* MakeGraph(UComposableCameraTypeAsset*& Asset)
	{
		Asset = NewObject<UComposableCameraTypeAsset>();
		auto* Graph = NewObject<UComposableCameraNodeGraph>(Asset);
		Graph->OwningTypeAsset = Asset;
		Graph->Schema = UComposableCameraNodeGraphSchema::StaticClass();
		Asset->EditorGraph = Graph;
		GetDefault<UComposableCameraNodeGraphSchema>()->CreateDefaultNodesForGraph(*Graph);
		return Graph;
	}

	UComposableCameraNodeGraphNode* AddNode(UComposableCameraNodeGraph* Graph, UClass* Class, float X)
	{
		return Cast<UComposableCameraNodeGraphNode>(UComposableCameraNodeGraphSchema::AddNodeToGraph(Graph, Class, FVector2D(X, 0)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCCSMathGraphNumericCompatibilityTest,
	"ComposableCameraSystem.Editor.MathGraph.NumericCompatibility", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCCSMathGraphNumericCompatibilityTest::RunTest(const FString&)
{
	using namespace ComposableCameraMathGraphTests;
	UComposableCameraTypeAsset* Asset = nullptr;
	auto* Graph = MakeGraph(Asset);
	auto* Source = AddNode(Graph, UComposableCameraClampNode::StaticClass(), 200.f);
	auto* Widen = AddNode(Graph, UComposableCameraFloatToDoubleNode::StaticClass(), 400.f);
	auto* Narrow = AddNode(Graph, UComposableCameraDoubleToFloatNode::StaticClass(), 600.f);
	if (!TestNotNull(TEXT("Source"), Source) || !TestNotNull(TEXT("Widen"), Widen) || !TestNotNull(TEXT("Narrow"), Narrow)) return false;
	const auto* Schema = GetDefault<UComposableCameraNodeGraphSchema>();
	const auto* FloatOut = Source->FindPin(TEXT("Result"), EGPD_Output);
	const auto* FloatIn = Widen->FindPin(TEXT("Value"), EGPD_Input);
	const auto* DoubleOut = Widen->FindPin(TEXT("Result"), EGPD_Output);
	const auto* DoubleIn = Narrow->FindPin(TEXT("Value"), EGPD_Input);
	if (!TestNotNull(TEXT("Float output"), FloatOut) || !TestNotNull(TEXT("Float input"), FloatIn)
		|| !TestNotNull(TEXT("Double output"), DoubleOut) || !TestNotNull(TEXT("Double input"), DoubleIn)) return false;
	TestTrue(TEXT("Float-to-double alias is rejected at authoring time"), Schema->CanCreateConnection(FloatOut, DoubleIn).Response == CONNECT_RESPONSE_DISALLOW);
	TestTrue(TEXT("Reverse drag also rejects incompatible storage"), Schema->CanCreateConnection(DoubleIn, FloatOut).Response == CONNECT_RESPONSE_DISALLOW);
	TestTrue(TEXT("Conversion accepts float source"), Schema->CanCreateConnection(FloatOut, FloatIn).Response != CONNECT_RESPONSE_DISALLOW);
	TestTrue(TEXT("Conversion exposes a real double output"), Schema->CanCreateConnection(DoubleOut, DoubleIn).Response != CONNECT_RESPONSE_DISALLOW);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCCSMathGraphRoundTripTest,
	"ComposableCameraSystem.Editor.MathGraph.RoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCCSMathGraphRoundTripTest::RunTest(const FString&)
{
	using namespace ComposableCameraMathGraphTests;
	UComposableCameraTypeAsset* Asset = nullptr;
	auto* Graph = MakeGraph(Asset);
	auto* Remap = AddNode(Graph, UComposableCameraRemapNode::StaticClass(), 200.f);
	auto* FOV = AddNode(Graph, UComposableCameraFieldOfViewNode::StaticClass(), 500.f);
	UComposableCameraStartGraphNode* Start = nullptr;
	UComposableCameraOutputGraphNode* Output = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (auto* Candidate = Cast<UComposableCameraStartGraphNode>(Node)) Start = Candidate;
		if (auto* Candidate = Cast<UComposableCameraOutputGraphNode>(Node)) Output = Candidate;
	}
	if (!TestNotNull(TEXT("Remap"), Remap) || !TestNotNull(TEXT("FOV"), FOV)
		|| !TestNotNull(TEXT("Start"), Start) || !TestNotNull(TEXT("Output"), Output)) return false;
	const auto* Schema = GetDefault<UComposableCameraNodeGraphSchema>();
	const auto Link = [&](UEdGraphNode* From, FName OutName, UEdGraphNode* To, FName InName)
	{
		UEdGraphPin* OutPin = From->FindPin(OutName, EGPD_Output);
		UEdGraphPin* InPin = To->FindPin(InName, EGPD_Input);
		return OutPin && InPin && Schema->TryCreateConnection(OutPin, InPin);
	};
	TestTrue(TEXT("Start executes mapping"), Link(Start, UComposableCameraGraphNodeBase::PN_ExecOut, Remap, UComposableCameraGraphNodeBase::PN_ExecIn));
	TestTrue(TEXT("Mapping precedes consumer"), Link(Remap, UComposableCameraGraphNodeBase::PN_ExecOut, FOV, UComposableCameraGraphNodeBase::PN_ExecIn));
	TestTrue(TEXT("Consumer reaches Output"), Link(FOV, UComposableCameraGraphNodeBase::PN_ExecOut, Output, UComposableCameraGraphNodeBase::PN_ExecIn));
	TestTrue(TEXT("Mapping drives FOV"), Link(Remap, TEXT("Result"), FOV, TEXT("FieldOfView")));
	Graph->SyncToTypeAsset();
	TestEqual(TEXT("Math remains on main chain"), Asset->FullExecChain.Num(), 2);
	TestEqual(TEXT("Math never moves to BeginPlay"), Asset->ComputeNodeTemplates.Num(), 0);
	TestEqual(TEXT("Wire is persisted"), Asset->PinConnections.Num(), 1);
	const auto* RemapTemplate = Remap->NodeTemplate.Get();
	const auto* FOVTemplate = FOV->NodeTemplate.Get();
	Graph->RebuildFromTypeAsset();
	UComposableCameraNodeGraphNode* RebuiltRemap = nullptr;
	UComposableCameraNodeGraphNode* RebuiltFOV = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (auto* CameraNode = Cast<UComposableCameraNodeGraphNode>(Node))
		{
			if (CameraNode->NodeTemplate == RemapTemplate) RebuiltRemap = CameraNode;
			if (CameraNode->NodeTemplate == FOVTemplate) RebuiltFOV = CameraNode;
		}
	}
	if (!TestNotNull(TEXT("Original mapping template retained"), RebuiltRemap)
		|| !TestNotNull(TEXT("Original consumer template retained"), RebuiltFOV)) return false;
	const auto* ResultPin = RebuiltRemap->FindPin(TEXT("Result"), EGPD_Output);
	const auto* FOVPin = RebuiltFOV->FindPin(TEXT("FieldOfView"), EGPD_Input);
	TestTrue(TEXT("Data connection survives graph reconstruction"), ResultPin && FOVPin && ResultPin->LinkedTo.Contains(FOVPin));
	Graph->SyncToTypeAsset();
	FDataValidationContext Context;
	TestTrue(TEXT("Round-tripped math graph passes native validation"), Asset->IsDataValid(Context) == EDataValidationResult::Valid);
	return true;
}
#endif
