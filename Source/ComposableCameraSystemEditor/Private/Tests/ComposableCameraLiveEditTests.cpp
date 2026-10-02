// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/ComposableCameraLiveEditSession.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraTypeAssetInstantiator.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Editors/ComposableCameraNodeGraph.h"
#include "Editors/ComposableCameraNodeGraphNode.h"
#include "Editors/ComposableCameraNodeGraphSchema.h"
#include "Editors/ComposableCameraOutputGraphNode.h"
#include "Editors/ComposableCameraStartGraphNode.h"
#include "Widgets/SComposableCameraLiveEditPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/Geometry.h"
#include "IDetailsView.h"
#include "Nodes/ComposableCameraHitchcockZoomNode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Nodes/ComposableCameraCameraOffsetNode.h"
#include "Nodes/ComposableCameraPivotDampingNode.h"
#include "Nodes/ComposableCameraImpulseResolutionNode.h"
#include "Interpolator/ComposableCameraInterpolatorBase.h"
#include "Interpolator/ComposableCameraIIRInterpolator.h"
#include "Components/SphereComponent.h"
#include "Curves/CurveFloat.h"
#include "Nodes/ComposableCameraFieldOfViewNode.h"
#include "Nodes/ComposableCameraPivotOffsetNode.h"
#include "Nodes/ComposableCameraSetRotationNode.h"
#include "UObject/GarbageCollection.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FLiveEditFixture : FGCObject
	{
		FLiveEditFixture(bool bDefaultSlot = false, bool bExposed = false)
		{
			World = UWorld::CreateWorld(EWorldType::PIE, false);
			Asset = NewObject<UComposableCameraTypeAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
			Template = NewObject<UComposableCameraFieldOfViewNode>(Asset, NAME_None, RF_Transactional);
			Asset->NodeTemplates.Add(Template);
			Asset->ExecutionOrder = { 0 };
			Asset->NodePinOverrides.SetNum(1);
			if (bDefaultSlot)
			{
				FComposableCameraPinOverride Override;
				Override.PinName = TEXT("FieldOfView");
				Override.bHasDefaultOverride = true;
				Override.DefaultValueOverride = TEXT("90");
				Asset->NodePinOverrides[0].Overrides.Add(Override);
			}
			FComposableCameraParameterBlock Parameters;
			if (bExposed)
			{
				FComposableCameraExposedParameter Parameter;
				Parameter.ParameterName = TEXT("FOV");
				Parameter.PinType = EComposableCameraPinType::Float;
				Parameter.TargetNodeIndex = 0;
				Parameter.TargetPinName = TEXT("FieldOfView");
				Asset->ExposedParameters.Add(Parameter);
				Parameters.Values.FindOrAdd(TEXT("FOV")).Set(EComposableCameraPinType::Float, 120.f);
			}
			Camera = World->SpawnActor<AComposableCameraCameraBase>();
			UE::ComposableCameras::ConstructCameraFromTypeAsset(Camera, Asset, Parameters);
			Node = CastChecked<UComposableCameraFieldOfViewNode>(Camera->CameraNodes[0]);
		}
		~FLiveEditFixture() { World->DestroyWorld(false); }
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override
		{
			Collector.AddReferencedObject(World);
			Collector.AddReferencedObject(Asset);
			Collector.AddReferencedObject(Template);
			Collector.AddReferencedObject(Camera);
			Collector.AddReferencedObject(Node);
		}
		virtual FString GetReferencerName() const override { return TEXT("FLiveEditFixture"); }
		TObjectPtr<UWorld> World;
		TObjectPtr<UComposableCameraTypeAsset> Asset;
		TObjectPtr<UComposableCameraFieldOfViewNode> Template;
		TObjectPtr<AComposableCameraCameraBase> Camera;
		TObjectPtr<UComposableCameraFieldOfViewNode> Node;
	};

	bool SetFov(FComposableCameraLiveEditSession& Session, float Value, FString& Reason)
	{
		CastChecked<UComposableCameraFieldOfViewNode>(Session.GetEditingNode())->FieldOfView = Value;
		return Session.ApplyProperty(TEXT("FieldOfView"), Reason);
	}

	float EvaluateFov(UComposableCameraFieldOfViewNode* Node)
	{
		FComposableCameraPose Pose;
		Node->TickNode(1.f / 60.f, Pose, Pose);
		return Pose.FieldOfView;
	}

	UComposableCameraNodeGraph* CreateAuthoringGraph(UComposableCameraTypeAsset* Asset)
	{
		auto* Graph = NewObject<UComposableCameraNodeGraph>(Asset, NAME_None, RF_Transactional | RF_Transient);
		Graph->Schema = UComposableCameraNodeGraphSchema::StaticClass();
		Graph->OwningTypeAsset = Asset;
		Asset->EditorGraph = Graph;
		// Exercise the same graph-notification -> sync callback as an open toolkit.
		const TWeakObjectPtr<UComposableCameraNodeGraph> WeakGraph(Graph);
		Graph->AddOnGraphChangedHandler(FOnGraphChanged::FDelegate::CreateLambda([WeakGraph](const FEdGraphEditAction&)
		{
			if (auto* Current = WeakGraph.Get()) Current->SyncToTypeAsset();
		}));
		Graph->RebuildFromTypeAsset();
		return Graph;
	}
	UComposableCameraNodeGraphNode* FindAuthoringNode(UComposableCameraNodeGraph* Graph, UComposableCameraCameraNodeBase* Template)
	{
		for (UEdGraphNode* Raw : Graph->Nodes)
		{
			auto* Node = Cast<UComposableCameraNodeGraphNode>(Raw);
			if (Node && Node->NodeTemplate == Template) return Node;
		}
		return nullptr;
	}
	FString UneditedAssetState(UComposableCameraTypeAsset* Asset)
	{
		FString Result;
		for (TFieldIterator<FProperty> It(Asset->GetClass()); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Transient) || It->GetFName() == TEXT("NodeTemplates") || It->GetFName() == TEXT("NodePinOverrides")) continue;
			FString Value;
			It->ExportTextItem_Direct(Value, It->ContainerPtrToValuePtr<void>(Asset), nullptr, Asset, PPF_None);
			Result += It->GetName() + TEXT("=") + Value + TEXT("\n");
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditDefaultsTest,
	"ComposableCameraSystem.Editor.LiveEdit.DefaultsAndSlotShapes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditDefaultsTest::RunTest(const FString& /*Parameters*/)
{
	for (bool bHasSlot : { false, true })
	{
		FLiveEditFixture Fixture(bHasSlot);
		FComposableCameraLiveEditSession Session;
		if (!TestTrue(TEXT("Bind PIE camera"), Session.BindCamera(Fixture.Camera))) return false;
		TestEqual(TEXT("Bound camera retains the selected runtime instance"), Session.GetCamera(), Fixture.Camera.Get());
		const TArray<int32> NodeIndices = Session.GetNodeIndices();
		if (!TestTrue(TEXT("Existing runtime node array supplies the selectable node"), NodeIndices.Num() == 1 && NodeIndices[0] == 0)) return false;
		if (!TestTrue(TEXT("Select duplicated node"), Session.SelectNode(0))) return false;
		FString Reason;
		TestTrue(TEXT("Apply trial"), SetFov(Session, 105.f, Reason));
		TestEqual(TEXT("Trial survives automatic pin refresh"), EvaluateFov(Fixture.Node), 105.f);
		TestEqual(TEXT("Template untouched by trial"), Fixture.Template->FieldOfView, 79.f);
		TestTrue(TEXT("Trial tracked"), Session.HasChanges());
		TestTrue(TEXT("Reset trial"), Session.Reset());
		TestEqual(TEXT("Reset restores original effective value"), EvaluateFov(Fixture.Node), bHasSlot ? 90.f : 79.f);
		if (bHasSlot)
		{
			auto& Block = *Fixture.Camera->OwnedRuntimeDataBlock;
			const int32 Offset = Block.DefaultValueOffsets[FComposableCameraPinKey{0, TEXT("FieldOfView")}];
			Block.SlotShapes[Offset].PinType = EComposableCameraPinType::Int32;
			TestTrue(TEXT("Independent trial accepts value without writing wrong-shaped slot"), SetFov(Session, 115.f, Reason));
			TestEqual(TEXT("Trial is visible despite invalid lower shape"), EvaluateFov(Fixture.Node), 115.f);
			Block.SlotShapes[Offset].PinType = EComposableCameraPinType::Float;
			TestEqual(TEXT("Lower slot untouched"), Block.ReadValue<float>(Offset), 90.f);
			Block.InputPinSourceOffsets.Add(FComposableCameraPinKey{0, TEXT("FieldOfView")}, Offset);
			TestTrue(TEXT("Trial overrides wired / variable input"), SetFov(Session, 115.f, Reason));
			TestEqual(TEXT("Wired trial is evaluated"), EvaluateFov(Fixture.Node), 115.f);
			Block.WriteValue<float>(Offset, 95.f);
			TestTrue(TEXT("Reset removes wire override"), Session.Reset());
			TestEqual(TEXT("Reset restores current wire rather than original baseline"), EvaluateFov(Fixture.Node), 95.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditOwnershipTest,
	"ComposableCameraSystem.Editor.LiveEdit.ParameterAndModifierOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditOwnershipTest::RunTest(const FString& /*Parameters*/)
{
	FLiveEditFixture Fixture(false, true);
	FComposableCameraLiveEditSession Session;
	Session.BindCamera(Fixture.Camera);
	Session.SelectNode(0);
	FString Reason;
	TestTrue(TEXT("Exposed parameter can be trialled"), SetFov(Session, 140.f, Reason));
	TestEqual(TEXT("Exposed slot survives tick"), EvaluateFov(Fixture.Node), 140.f);
	float OriginalCallerValue = 0;
	TestTrue(TEXT("Caller snapshot readable"), Fixture.Camera->SourceParameterBlock.Values[TEXT("FOV")].Get(OriginalCallerValue));
	TestEqual(TEXT("Trial preserves original caller parameters"), OriginalCallerValue, 120.f);
	FProperty* Property = FindFProperty<FProperty>(Fixture.Node->GetClass(), TEXT("FieldOfView"));
	Fixture.Node->RegisterModifierOverrideFieldOffset(Property->GetOffset_ForInternal());
	Fixture.Node->FieldOfView = 165.f;
	TestTrue(TEXT("Trial supersedes Modifier"), SetFov(Session, 80.f, Reason));
	TestEqual(TEXT("Trial wins during node evaluation"), EvaluateFov(Fixture.Node), 80.f);
	TestEqual(TEXT("Evaluation restores current Modifier storage"), Fixture.Node->FieldOfView, 165.f);
	TestTrue(TEXT("Reset removes trial while Modifier owns property"), Session.Reset());
	TestEqual(TEXT("Reset does not overwrite active Modifier"), Fixture.Node->FieldOfView, 165.f);
	Fixture.Node->UnregisterInPlaceModifierOverride(Property->GetOffset_ForInternal());
	TestEqual(TEXT("Modifier release sees original lower value"), EvaluateFov(Fixture.Node), 120.f);

	FLiveEditFixture Slotless;
	FComposableCameraLiveEditSession SlotlessSession;
	SlotlessSession.BindCamera(Slotless.Camera);
	SlotlessSession.SelectNode(0);
	SetFov(SlotlessSession, 100.f, Reason);
	Slotless.Node->RegisterModifierOverrideFieldOffset(Property->GetOffset_ForInternal());
	Slotless.Node->FieldOfView = 150.f;
	TestEqual(TEXT("Trial overrides newly acquired Modifier"), EvaluateFov(Slotless.Node), 100.f);
	TestTrue(TEXT("Slotless reset removes override immediately"), SlotlessSession.Reset());
	TestFalse(TEXT("Reset clears trial record"), SlotlessSession.HasChanges());
	TestEqual(TEXT("Reset leaves current Modifier untouched"), Slotless.Node->FieldOfView, 150.f);
	Slotless.Node->UnregisterInPlaceModifierOverride(Property->GetOffset_ForInternal());
	TestTrue(TEXT("Reset succeeds after release"), SlotlessSession.Reset());
	TestEqual(TEXT("Removing ownership does not resurrect a discarded trial"), EvaluateFov(Slotless.Node), 150.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditPersistenceTest,
	"ComposableCameraSystem.Editor.LiveEdit.PersistenceAndInstanceIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditPersistenceTest::RunTest(const FString& /*Parameters*/)
{
	FLiveEditFixture Fixture;
	AComposableCameraCameraBase* OtherCamera = Fixture.World->SpawnActor<AComposableCameraCameraBase>();
	UE::ComposableCameras::ConstructCameraFromTypeAsset(OtherCamera, Fixture.Asset, FComposableCameraParameterBlock());
	FComposableCameraLiveEditSession Session;
	Session.BindCamera(Fixture.Camera);
	Session.SelectNode(0);
	FString Reason;
	SetFov(Session, 111.f, Reason);
	TestEqual(TEXT("Other instance remains untouched"), EvaluateFov(CastChecked<UComposableCameraFieldOfViewNode>(OtherCamera->CameraNodes[0])), 79.f);
	TestFalse(TEXT("Camera switch cannot discard unsaved changes"), Session.BindCamera(OtherCamera));
	TestNull(TEXT("Proxy never copies PIE owning camera"), Session.GetEditingNode()->GetOwningCamera());
	const TWeakObjectPtr<UComposableCameraCameraNodeBase> WeakProxy(Session.GetEditingNode());
	CollectGarbage(RF_NoFlags);
	TestTrue(TEXT("Proxy survives GC through editor session"), WeakProxy.IsValid());
	Fixture.World->bIsTearingDown = true;
	TestFalse(TEXT("Tearing-down PIE world rejects writes before GC"), SetFov(Session, 115.f, Reason));
	Session.DetachRuntime();
	Fixture.World->bIsTearingDown = false;
	TestFalse(TEXT("Runtime detached on PIE end"), Session.IsLive());
	TestTrue(TEXT("PIE end keeps authored trial data"), Session.HasChanges());
	TestTrue(TEXT("Apply after PIE ends"), Session.ApplyToAsset(Reason));
	TestEqual(TEXT("Edited value committed to template"), Fixture.Template->FieldOfView, 111.f);
	TestEqual(TEXT("Only one pin override committed"), Fixture.Asset->NodePinOverrides[0].Overrides.Num(), 1);
	FComposableCameraRuntimeDataBlock Rebuilt = Fixture.Asset->BuildRuntimeDataLayout();
	float Value = 0;
	TestTrue(TEXT("Serialized default resolves after rebuild"), Rebuilt.TryResolveInputPin<float>(0, TEXT("FieldOfView"), Value));
	TestEqual(TEXT("Rebuilt value matches trial"), Value, 111.f);
	const auto* Graph = Cast<UComposableCameraNodeGraph>(Fixture.Asset->EditorGraph);
	TestNotNull(TEXT("Materialized authoring graph has schema"), Graph ? Graph->GetSchema() : nullptr);
	TestFalse(TEXT("Applied trial is no longer pending"), Session.HasChanges());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditIdentityTest,
	"ComposableCameraSystem.Editor.LiveEdit.NodeIdentityAndAtomicConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditIdentityTest::RunTest(const FString& /*Parameters*/)
{
	FLiveEditFixture Fixture;
	auto* Second = NewObject<UComposableCameraFieldOfViewNode>(Fixture.Asset, NAME_None, RF_Transactional);
	Second->FieldOfView = 60.f;
	Fixture.Asset->NodeTemplates.Add(Second);
	Fixture.Asset->ExecutionOrder = { 0, 1 };
	Fixture.Asset->NodePinOverrides.SetNum(2);
	Fixture.Camera = Fixture.World->SpawnActor<AComposableCameraCameraBase>();
	UE::ComposableCameras::ConstructCameraFromTypeAsset(Fixture.Camera, Fixture.Asset, FComposableCameraParameterBlock());
	FComposableCameraLiveEditSession Session;
	Session.BindCamera(Fixture.Camera);
	Session.SelectNode(0);
	FString Reason;
	SetFov(Session, 100.f, Reason);
	Session.SelectNode(1);
	SetFov(Session, 70.f, Reason);
	Second->FieldOfView = 65.f;
	TestFalse(TEXT("Concurrent source edit blocks entire commit"), Session.ApplyToAsset(Reason));
	TestEqual(TEXT("First node not partially committed"), Fixture.Template->FieldOfView, 79.f);
	TestTrue(TEXT("Both trial records preserved"), Session.HasChanges());
	Second->FieldOfView = 60.f;
	// Materialize graph, then reorder source nodes after the runtime instance was constructed.
	TestTrue(TEXT("Initial commit materializes graph"), Session.ApplyToAsset(Reason));
	Session.SelectNode(1);
	SetFov(Session, 75.f, Reason);
	auto* Graph = CastChecked<UComposableCameraNodeGraph>(Fixture.Asset->EditorGraph);
	int32 FirstGraphIndex = INDEX_NONE;
	int32 SecondGraphIndex = INDEX_NONE;
	FGuid SecondGuid;
	for (int32 Index = 0; Index < Graph->Nodes.Num(); ++Index)
	{
		auto* Node = Cast<UComposableCameraNodeGraphNode>(Graph->Nodes[Index]);
		if (!Node) continue;
		if (Node->NodeTemplate == Fixture.Template) FirstGraphIndex = Index;
		if (Node->NodeTemplate == Second) { SecondGraphIndex = Index; SecondGuid = Node->NodeGuid; }
	}
	if (!TestTrue(TEXT("Both graph nodes resolved"), FirstGraphIndex != INDEX_NONE && SecondGraphIndex != INDEX_NONE)) return false;
	Graph->Nodes.Swap(FirstGraphIndex, SecondGraphIndex);
	Graph->SyncToTypeAsset();
	TestTrue(TEXT("Apply follows template identity after reordering"), Session.ApplyToAsset(Reason));
	TestEqual(TEXT("Correct same-class node changed"), Second->FieldOfView, 75.f);
	TestEqual(TEXT("Other same-class node kept its value"), Fixture.Template->FieldOfView, 100.f);
	bool bGuidPreserved = false;
	for (const UEdGraphNode* Raw : Graph->Nodes)
	{
		const auto* Node = Cast<UComposableCameraNodeGraphNode>(Raw);
		if (Node && Node->NodeTemplate == Second) bGuidPreserved = Node->NodeGuid == SecondGuid;
	}
	TestTrue(TEXT("Applying trial preserves existing graph node GUID"), bGuidPreserved);
	Graph->RebuildFromTypeAsset();
	TestEqual(TEXT("Source template identity survives graph rebuild"), Second->FieldOfView, 75.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditMemoizationTest,
	"ComposableCameraSystem.Editor.LiveEdit.PreservesFrameMemoization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditMemoizationTest::RunTest(const FString& /*Parameters*/)
{
	FLiveEditFixture Fixture;
	FComposableCameraLiveEditSession Session;
	Session.BindCamera(Fixture.Camera);
	Session.SelectNode(0);
	const FComposableCameraPose Before = Fixture.Camera->TickCamera(1.f / 60.f);
	FString Reason;
	SetFov(Session, 115.f, Reason);
	const FComposableCameraPose SameFrame = Fixture.Camera->TickCamera(1.f / 60.f);
	TestEqual(TEXT("Editor write never causes a second DAG camera tick in same frame"), SameFrame.FieldOfView, Before.FieldOfView);
	TestEqual(TEXT("Next node evaluation reads trial"), EvaluateFov(Fixture.Node), 115.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditUndoTest,
	"ComposableCameraSystem.Editor.LiveEdit.AssetUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditUndoTest::RunTest(const FString& /*Parameters*/)
{
	if (!TestNotNull(TEXT("Editor transactor available"), GEditor ? GEditor->Trans.Get() : nullptr)) return false;
	FLiveEditFixture Fixture;
	FComposableCameraLiveEditSession Session;
	Session.BindCamera(Fixture.Camera);
	Session.SelectNode(0);
	FString Reason;
	SetFov(Session, 101.f, Reason);
	if (!TestTrue(TEXT("Apply transaction created"), Session.ApplyToAsset(Reason))) return false;
	TestTrue(TEXT("Undo source commit"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores template value"), Fixture.Template->FieldOfView, 79.f);
	TestEqual(TEXT("Undo restores pin override container"), Fixture.Asset->NodePinOverrides[0].Overrides.Num(), 0);
	auto* Graph = CastChecked<UComposableCameraNodeGraph>(Fixture.Asset->EditorGraph);
	auto* GraphNode = FindAuthoringNode(Graph, Fixture.Template);
	if (!TestNotNull(TEXT("Undo retains source graph node"), GraphNode)) return false;
	TestEqual(TEXT("Undo restores visible pin default"), FCString::Atof(*GraphNode->FindPin(TEXT("FieldOfView"), EGPD_Input)->DefaultValue), 79.f);
	TestTrue(TEXT("Redo source commit"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores trial default"), Fixture.Template->FieldOfView, 101.f);
	TestEqual(TEXT("Redo restores visible pin default"), FCString::Atof(*GraphNode->FindPin(TEXT("FieldOfView"), EGPD_Input)->DefaultValue), 101.f);
	TestTrue(TEXT("Undo again before new trial"), GEditor->UndoTransaction());
	TestTrue(TEXT("Reset rebases conflicts after Undo"), Session.Reset());
	TestTrue(TEXT("New trial after Undo accepted"), SetFov(Session, 112.f, Reason));
	TestTrue(TEXT("New trial applies against restored source"), Session.ApplyToAsset(Reason));
	TestEqual(TEXT("New commit saves new value"), Fixture.Template->FieldOfView, 112.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditValueTypesTest,
	"ComposableCameraSystem.Editor.LiveEdit.VectorRotatorAndEnumRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditValueTypesTest::RunTest(const FString& /*Parameters*/)
{
	FLiveEditFixture Fixture;
	auto* Asset = NewObject<UComposableCameraTypeAsset>();
	auto* Offset = NewObject<UComposableCameraCameraOffsetNode>(Asset);
	auto* Pivot = NewObject<UComposableCameraPivotOffsetNode>(Asset);
	auto* Rotation = NewObject<UComposableCameraSetRotationNode>(Asset);
	Asset->NodeTemplates = { Offset, Pivot, Rotation };
	Asset->ExecutionOrder = { 0, 1, 2 };
	Asset->NodePinOverrides.SetNum(3);
	auto AddDefault = [Asset](int32 Index, FName Name, const FString& Value)
	{
		FComposableCameraPinOverride Override;
		Override.PinName = Name;
		Override.bHasDefaultOverride = true;
		Override.DefaultValueOverride = Value;
		Asset->NodePinOverrides[Index].Overrides.Add(Override);
	};
	AddDefault(0, TEXT("CameraOffset"), TEXT("(X=-300.0,Y=0.0,Z=80.0)"));
	AddDefault(1, TEXT("PivotOffsetType"), TEXT("WorldSpace"));
	AddDefault(2, TEXT("RotationOffset"), TEXT("(Pitch=0.0,Yaw=0.0,Roll=0.0)"));
	AComposableCameraCameraBase* Camera = Fixture.World->SpawnActor<AComposableCameraCameraBase>();
	UE::ComposableCameras::ConstructCameraFromTypeAsset(Camera, Asset, FComposableCameraParameterBlock());
	FComposableCameraLiveEditSession Session;
	Session.BindCamera(Camera);
	FString Reason;
	Session.SelectNode(0);
	const FVector ExpectedOffset(-350., 20., 70.);
	CastChecked<UComposableCameraCameraOffsetNode>(Session.GetEditingNode())->CameraOffset = ExpectedOffset;
	TestTrue(TEXT("Vector trial"), Session.ApplyProperty(TEXT("CameraOffset"), Reason));
	Session.SelectNode(1);
	CastChecked<UComposableCameraPivotOffsetNode>(Session.GetEditingNode())->PivotOffsetType = ECameraPivotOffset::CameraSpace;
	TestTrue(TEXT("Enum trial"), Session.ApplyProperty(TEXT("PivotOffsetType"), Reason));
	Session.SelectNode(2);
	const FRotator ExpectedRotation(10., 30., 5.);
	CastChecked<UComposableCameraSetRotationNode>(Session.GetEditingNode())->RotationOffset = ExpectedRotation;
	TestTrue(TEXT("Rotator trial"), Session.ApplyProperty(TEXT("RotationOffset"), Reason));
	TestTrue(TEXT("Apply typed defaults"), Session.ApplyToAsset(Reason));
	FComposableCameraRuntimeDataBlock Block = Asset->BuildRuntimeDataLayout();
	FVector OffsetValue;
	FRotator RotationValue;
	int64 EnumValue = 0;
	TestTrue(TEXT("Vector default parses"), Block.TryResolveInputPin<FVector>(0, TEXT("CameraOffset"), OffsetValue));
	TestTrue(TEXT("Rotator default parses"), Block.TryResolveInputPin<FRotator>(2, TEXT("RotationOffset"), RotationValue));
	TestTrue(TEXT("Enum default parses to canonical int64"), Block.TryResolveInputPin<int64>(1, TEXT("PivotOffsetType"), EnumValue));
	TestEqual(TEXT("Vector round trip"), OffsetValue, ExpectedOffset);
	TestEqual(TEXT("Rotator round trip"), RotationValue, ExpectedRotation);
	TestEqual(TEXT("Enum round trip"), EnumValue, static_cast<int64>(ECameraPivotOffset::CameraSpace));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditAuthoringPropertiesTest,
	"ComposableCameraSystem.Editor.LiveEdit.AllAuthoringProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditAuthoringPropertiesTest::RunTest(const FString& /*Parameters*/)
{
	TestTrue(TEXT("FOV supports live edit"), FComposableCameraLiveEditSession::IsLiveEditProperty(FindFProperty<FProperty>(UComposableCameraFieldOfViewNode::StaticClass(), TEXT("FieldOfView"))));
	TestTrue(TEXT("Offset supports live edit"), FComposableCameraLiveEditSession::IsLiveEditProperty(FindFProperty<FProperty>(UComposableCameraCameraOffsetNode::StaticClass(), TEXT("CameraOffset"))));
	TestTrue(TEXT("Pivot enum supports live edit"), FComposableCameraLiveEditSession::IsLiveEditProperty(FindFProperty<FProperty>(UComposableCameraPivotOffsetNode::StaticClass(), TEXT("PivotOffsetType"))));
	TestTrue(TEXT("Curve automatically supported"), FComposableCameraLiveEditSession::IsLiveEditProperty(FindFProperty<FProperty>(UComposableCameraCameraOffsetNode::StaticClass(), TEXT("ForwardOffsetDeltaByPitchCurve"))));
	TestTrue(TEXT("Array automatically supported"), FComposableCameraLiveEditSession::IsLiveEditProperty(FindFProperty<FProperty>(UComposableCameraFieldOfViewNode::StaticClass(), TEXT("ActorsForDynamicFoV"))));
	TestTrue(TEXT("Instanced interpolator automatically supported"), FComposableCameraLiveEditSession::IsLiveEditProperty(FindFProperty<FProperty>(UComposableCameraPivotDampingNode::StaticClass(), TEXT("UpwardInterpolator"))));
	TestFalse(TEXT("Palette metadata excluded"), FComposableCameraLiveEditSession::IsLiveEditProperty(FindFProperty<FProperty>(UComposableCameraFieldOfViewNode::StaticClass(), TEXT("PaletteCategory"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditStartOrderTest,
	"ComposableCameraSystem.Editor.LiveEdit.StartChainOrderAndScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditStartOrderTest::RunTest(const FString& /*Parameters*/)
{
	FLiveEditFixture Fixture;
	Fixture.Asset->NodeTemplates.Add(NewObject<UComposableCameraFieldOfViewNode>(Fixture.Asset));
	Fixture.Asset->NodeTemplates.Add(NewObject<UComposableCameraCameraOffsetNode>(Fixture.Asset));
	Fixture.Asset->ComputeNodeTemplates.Add(NewObject<UComposableCameraBeginPlaySetRotationNode>(Fixture.Asset));
	Fixture.Asset->NodePinOverrides.SetNum(3);
	FComposableCameraExecEntry First, Set, Last;
	First.CameraNodeIndex = 2;
	Set.EntryType = EComposableCameraExecEntryType::SetVariable;
	Set.CameraNodeIndex = 1; // A data dependency is not an executed Start-chain node.
	Last.CameraNodeIndex = 0;
	Fixture.Asset->FullExecChain = { First, Set, Last };
	Fixture.Camera = Fixture.World->SpawnActor<AComposableCameraCameraBase>();
	UE::ComposableCameras::ConstructCameraFromTypeAsset(Fixture.Camera, Fixture.Asset, FComposableCameraParameterBlock());
	FComposableCameraLiveEditSession Session;
	TestTrue(TEXT("Bind execution-chain camera"), Session.BindCamera(Fixture.Camera));
	const TArray<int32> Indices = Session.GetNodeIndices();
	TestTrue(TEXT("Exec order differs from asset array order"), Indices.Num() == 2 && Indices[0] == 2 && Indices[1] == 0);
	TestNotNull(TEXT("First Start node proxy available without manual selection"), Session.GetEditingNode(2));
	TestNotNull(TEXT("Second Start node proxy available without manual selection"), Session.GetEditingNode(0));
	TestFalse(TEXT("Off-chain data dependency cannot be edited"), Session.SelectNode(1));
	TestNull(TEXT("BeginPlay compute node excluded"), Session.GetEditingNode(3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditChainPanelTest,
	"ComposableCameraSystem.Editor.LiveEdit.ChainPanelSelectionAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditChainPanelTest::RunTest(const FString& /*Parameters*/)
{
	FLiveEditFixture Fixture;
	Fixture.Asset->NodeTemplates.Add(NewObject<UComposableCameraFieldOfViewNode>(Fixture.Asset));
	Fixture.Asset->NodeTemplates.Add(NewObject<UComposableCameraCameraOffsetNode>(Fixture.Asset));
	Fixture.Asset->NodePinOverrides.SetNum(3);
	Fixture.Asset->ExecutionOrder = { 2, 0 }; // Node 1 is an off-chain dependency.
	Fixture.Camera = Fixture.World->SpawnActor<AComposableCameraCameraBase>();
	UE::ComposableCameras::ConstructCameraFromTypeAsset(Fixture.Camera, Fixture.Asset, FComposableCameraParameterBlock());
	const auto SourceTemplates = Fixture.Asset->NodeTemplates;
	const FString SourceState = UneditedAssetState(Fixture.Asset);
	const TSharedRef<SComposableCameraLiveEditPanel> Panel = SNew(SComposableCameraLiveEditPanel);
	Panel->SelectCamera(Fixture.Camera.Get());
	if (!TestEqual(TEXT("Start, two executed nodes, Output"), Panel->NodeChainGraph->Nodes.Num(), 4)) return false;
	TestTrue(TEXT("Presentation graph has no authoring owner"), !Panel->NodeChainGraph->OwningTypeAsset);
	TestFalse(TEXT("Chain topology cannot be authored in this window"), Panel->NodeChainGraph->bEditable);
	TestTrue(TEXT("Start and Output use existing graph node types"), Panel->NodeChainGraph->Nodes[0]->IsA<UComposableCameraStartGraphNode>()
		&& Panel->NodeChainGraph->Nodes.Last()->IsA<UComposableCameraOutputGraphNode>());
	auto* First = CastChecked<UComposableCameraNodeGraphNode>(Panel->NodeChainGraph->Nodes[1]);
	auto* Second = CastChecked<UComposableCameraNodeGraphNode>(Panel->NodeChainGraph->Nodes[2]);
	TestTrue(TEXT("Runtime order, not template order"), First->NodeIndex == 2 && Second->NodeIndex == 0);
	TestTrue(TEXT("Graph nodes reference independent editing proxies"), First->NodeTemplate == Panel->Session.GetEditingNode(2)
		&& Second->NodeTemplate == Panel->Session.GetEditingNode(0) && Second->NodeTemplate != Fixture.Template);
	for (int32 Index = 0; Index < Panel->NodeChainGraph->Nodes.Num() - 1; ++Index)
	{
		const auto* Current = Panel->NodeChainGraph->Nodes[Index].Get();
		const auto* Next = Panel->NodeChainGraph->Nodes[Index + 1].Get();
		UEdGraphPin* Output = Current->FindPin(UComposableCameraGraphNodeBase::PN_ExecOut, EGPD_Output);
		UEdGraphPin* Input = Next->FindPin(UComposableCameraGraphNodeBase::PN_ExecIn, EGPD_Input);
		TestTrue(TEXT("Exec links follow chain order in both directions"), Output && Input && Output->LinkedTo.Contains(Input) && Input->LinkedTo.Contains(Output));
		TestTrue(TEXT("Horizontal chain"), Current->NodePosX < Next->NodePosX && Current->NodePosY == Next->NodePosY);
	}
	FGraphPanelSelectionSet Selection;
	Selection.Add(Second);
	Panel->OnNodeSelectionChanged(Selection);
	TestEqual(TEXT("Clicked node chooses its runtime parameter view"), Panel->SelectedPanelIndex, 1);
	TestTrue(TEXT("Lower section hosts the selected Details view"), &Panel->ParameterHost->GetChildren()->GetChildAt(0).Get() == Panel->NodePanels[1]->Details.Get());
	Panel->RefreshDescriptions();
	TestEqual(TEXT("Editability refresh preserves selected runtime node"), Panel->Session.GetSelectedNodeIndex(), 0);
	FString Reason;
	TestTrue(TEXT("Selected node can create a trial"), SetFov(Panel->Session, 113.f, Reason));
	Selection.Reset();
	Selection.Add(First);
	Panel->OnNodeSelectionChanged(Selection);
	TestTrue(TEXT("Switching nodes retains pending trial"), Panel->Session.HasChanges() && Panel->Session.HasOverrides());
	TestTrue(TEXT("Lower section switches to the other node"), &Panel->ParameterHost->GetChildren()->GetChildAt(0).Get() == Panel->NodePanels[0]->Details.Get());
	TestEqual(TEXT("Trial remains in runtime after selection change"), EvaluateFov(CastChecked<UComposableCameraFieldOfViewNode>(Fixture.Camera->CameraNodes[0])), 113.f);
	TestNull(TEXT("Selection never creates an authoring graph"), Fixture.Asset->EditorGraph.Get());
	TestTrue(TEXT("Source templates remain unchanged"), Fixture.Asset->NodeTemplates == SourceTemplates && Fixture.Template->FieldOfView == 79.f);
	TestEqual(TEXT("Source layout and connections remain unchanged"), UneditedAssetState(Fixture.Asset), SourceState);
	Panel->OnPIEEnding(false);
	Selection.Reset();
	Selection.Add(Second);
	Panel->OnNodeSelectionChanged(Selection);
	TestEqual(TEXT("Pending node still selectable after PIE"), Panel->SelectedPanelIndex, 1);
	TestTrue(TEXT("Parameters become read-only after PIE"), Panel->NodePanels[1]->EditableProperties.IsEmpty());
	TestTrue(TEXT("Pending defaults survive PIE teardown"), Panel->Session.HasChanges());
	const TWeakObjectPtr<UComposableCameraNodeGraph> WeakGraph = Panel->NodeChainGraph.Get();
	const TWeakObjectPtr<UComposableCameraNodeGraphNode> WeakNode = Second;
	CollectGarbage(RF_NoFlags);
	TestTrue(TEXT("Window owns transient graph and node proxies across GC"), WeakGraph.IsValid() && WeakNode.IsValid()
		&& IsValid(WeakNode->NodeTemplate.Get()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditActionAlignmentTest,
	"ComposableCameraSystem.Editor.LiveEdit.ActionButtonTextAlignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditActionAlignmentTest::RunTest(const FString& /*Parameters*/)
{
	const TSharedRef<SComposableCameraLiveEditPanel> Panel = SNew(SComposableCameraLiveEditPanel);
	TFunction<TSharedPtr<SButton>(const TSharedRef<SWidget>&, FName)> FindButton;
	FindButton = [&FindButton](const TSharedRef<SWidget>& Widget, FName Tag) -> TSharedPtr<SButton>
	{
		if (Widget->GetTag() == Tag) return StaticCastSharedRef<SButton>(Widget);
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (auto Button = FindButton(Children->GetChildAt(Index), Tag)) return Button;
		}
		return nullptr;
	};
	for (const FName Tag : { FName(TEXT("LiveEdit.ApplyToAsset")), FName(TEXT("LiveEdit.ResetTrial")) })
	{
		const auto Button = FindButton(Panel, Tag);
		if (!TestNotNull(Tag.ToString(), Button.Get())) return false;
		for (bool bEnabled : { true, false })
		{
			Button->SetEnabled(bEnabled);
			for (float Scale : { 1.f, 1.5f })
			{
				Button->SlatePrepass(Scale);
				const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(132, 28), FSlateLayoutTransform(Scale));
				FArrangedChildren Children(EVisibility::All);
				Button->ArrangeChildren(Geometry, Children, true);
				if (!TestEqual(TEXT("Button has one text content widget"), Children.Num(), 1)) return false;
				const auto ButtonCenter = Geometry.GetAbsolutePositionAtCoordinates(FVector2f(0.5f, 0.5f));
				const auto TextCenter = Children[0].Geometry.GetAbsolutePositionAtCoordinates(FVector2f(0.5f, 0.5f));
				const FString Case = FString::Printf(TEXT("%s enabled=%d scale=%.1f"), *Tag.ToString(), bEnabled, Scale);
				TestTrue(Case + TEXT(" horizontally centered"), FMath::IsNearlyEqual(static_cast<double>(TextCenter.X), static_cast<double>(ButtonCenter.X), 0.01));
				TestTrue(Case + TEXT(" vertically centered"), FMath::IsNearlyEqual(static_cast<double>(TextCenter.Y), static_cast<double>(ButtonCenter.Y), 0.01));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditResourceTest,
	"ComposableCameraSystem.Editor.LiveEdit.RefreshReusesImpulseComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditResourceTest::RunTest(const FString& /*Parameters*/)
{
	FLiveEditFixture Fixture;
	auto* Impulse = NewObject<UComposableCameraImpulseResolutionNode>(Fixture.Asset);
	Fixture.Asset->NodeTemplates = { Impulse };
	Fixture.Camera = Fixture.World->SpawnActor<AComposableCameraCameraBase>();
	UE::ComposableCameras::ConstructCameraFromTypeAsset(Fixture.Camera, Fixture.Asset, FComposableCameraParameterBlock());
	TArray<USphereComponent*> Before, After;
	Fixture.Camera->GetComponents(Before);
	FComposableCameraLiveEditSession Session;
	Session.BindCamera(Fixture.Camera);
	FString Reason;
	CastChecked<UComposableCameraImpulseResolutionNode>(Session.GetEditingNode())->VelocityDamping = 3.f;
	TestTrue(TEXT("Trial refresh accepted"), Session.ApplyProperty(TEXT("VelocityDamping"), Reason));
	Session.Reset();
	Fixture.Camera->GetComponents(After);
	TestEqual(TEXT("Refresh does not duplicate the collision component"), After.Num(), Before.Num());
	TestTrue(TEXT("Initial component actually existed"), Before.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditComplexValuesTest,
	"ComposableCameraSystem.Editor.LiveEdit.NestedObjectsArraysAndCurveRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditComplexValuesTest::RunTest(const FString& /*Parameters*/)
{
	FString Reason;
	{
		FLiveEditFixture Fixture;
		FComposableCameraLiveEditSession Session;
		Session.BindCamera(Fixture.Camera);
		auto* Proxy = CastChecked<UComposableCameraFieldOfViewNode>(Session.GetEditingNode());
		Proxy->ActorsForDynamicFoV = { nullptr, nullptr };
		TestTrue(TEXT("Details-only array accepted"), Session.ApplyProperty(TEXT("ActorsForDynamicFoV"), Reason));
		EvaluateFov(Fixture.Node);
		TestEqual(TEXT("Trial evaluation preserves underlying array"), Fixture.Node->ActorsForDynamicFoV.Num(), 0);
		TestTrue(TEXT("Array saved"), Session.ApplyToAsset(Reason));
		TestEqual(TEXT("Array authoring round trip"), Fixture.Template->ActorsForDynamicFoV.Num(), 2);
		TestTrue(TEXT("Reset remains available after Apply"), Session.HasOverrides());
		Session.Reset();
		TestFalse(TEXT("Applied trial can still be removed"), Session.HasOverrides());
	}
	{
		FLiveEditFixture Fixture;
		auto* Template = NewObject<UComposableCameraPivotDampingNode>(Fixture.Asset, NAME_None, RF_Transactional);
		auto* Interpolator = NewObject<UComposableCameraIIRInterpolator>(Template, NAME_None, RF_Transactional);
		Interpolator->Speed = 2.f;
		Template->UpwardInterpolator = Interpolator;
		Fixture.Asset->NodeTemplates = { Template };
		Fixture.Camera = Fixture.World->SpawnActor<AComposableCameraCameraBase>();
		UE::ComposableCameras::ConstructCameraFromTypeAsset(Fixture.Camera, Fixture.Asset, FComposableCameraParameterBlock());
		FComposableCameraLiveEditSession Session;
		Session.BindCamera(Fixture.Camera);
		auto* Proxy = CastChecked<UComposableCameraPivotDampingNode>(Session.GetEditingNode());
		auto* Child = CastChecked<UComposableCameraIIRInterpolator>(Proxy->UpwardInterpolator);
		TestTrue(TEXT("Editable child independent of template"), Child != Interpolator);
		Child->Speed = 7.f;
		FName Root = TEXT("Speed");
		TestEqual(TEXT("Nested property event routes to owning node"), Session.FindEditingNode(Child, Root), 0);
		TestEqual(TEXT("Nested event identifies root parameter"), Root, FName(TEXT("UpwardInterpolator")));
		TestTrue(TEXT("Nested trial accepted"), Session.ApplyProperty(Root, Reason));
		auto* Runtime = CastChecked<UComposableCameraPivotDampingNode>(Fixture.Camera->CameraNodes[0]);
		TestEqual(TEXT("Explicit compound pin reader sees trial"), Runtime->GetInputPinValue<float>(TEXT("UpwardInterpolator.Speed")), 7.f);
		FComposableCameraPose Pose;
		Runtime->TickNode(1.f / 60.f, Pose, Pose);
		TestEqual(TEXT("Tick preserves trial child after cache refresh"), Runtime->GetInputPinValue<float>(TEXT("UpwardInterpolator.Speed")), 7.f);
		TestEqual(TEXT("Trial did not mutate source child"), Interpolator->Speed, 2.f);
		Child->Speed = 9.f;
		TestTrue(TEXT("Second nested trial accepted"), Session.ApplyProperty(Root, Reason));
		TestEqual(TEXT("Second edit copies latest child contents"), Runtime->GetInputPinValue<float>(TEXT("UpwardInterpolator.Speed")), 9.f);
		Fixture.World->bIsTearingDown = true;
		Child->Speed = 11.f;
		TestFalse(TEXT("Rejected nested edit restores accepted snapshot"), Session.ApplyProperty(Root, Reason));
		Fixture.World->bIsTearingDown = false;
		TestEqual(TEXT("Nested rollback preserves last accepted value"), CastChecked<UComposableCameraIIRInterpolator>(Proxy->UpwardInterpolator)->Speed, 9.f);
		TestTrue(TEXT("Nested default saved"), Session.ApplyToAsset(Reason));
		TestEqual(TEXT("Nested authoring value saved"), CastChecked<UComposableCameraIIRInterpolator>(Template->UpwardInterpolator)->Speed, 9.f);
		TestTrue(TEXT("Saved child belongs to source template"), Template->UpwardInterpolator->IsIn(Template));
		TestFalse(TEXT("Saved child does not inherit transient proxy flags"), Template->UpwardInterpolator->HasAnyFlags(RF_Transient));
		float Parsed = 0;
		FComposableCameraRuntimeDataBlock Rebuilt = Fixture.Asset->BuildRuntimeDataLayout();
		TestTrue(TEXT("Compound override parses after rebuild"), Rebuilt.TryResolveInputPin<float>(0, TEXT("UpwardInterpolator.Speed"), Parsed));
		TestEqual(TEXT("Compound default matches nested edit"), Parsed, 9.f);
	}
	{
		FLiveEditFixture Fixture;
		auto* Template = NewObject<UComposableCameraCameraOffsetNode>(Fixture.Asset, NAME_None, RF_Transactional);
		auto* Curve = NewObject<UCurveFloat>(Fixture.Asset, NAME_None, RF_Transactional);
		Fixture.Asset->NodeTemplates = { Template };
		Fixture.Camera = Fixture.World->SpawnActor<AComposableCameraCameraBase>();
		UE::ComposableCameras::ConstructCameraFromTypeAsset(Fixture.Camera, Fixture.Asset, FComposableCameraParameterBlock());
		FComposableCameraLiveEditSession Session;
		Session.BindCamera(Fixture.Camera);
		auto& EditedCurve = CastChecked<UComposableCameraCameraOffsetNode>(Session.GetEditingNode())->ForwardOffsetDeltaByPitchCurve;
		EditedCurve.ExternalCurve = Curve;
		EditedCurve.EditorCurveData.AddKey(0.f, 5.f);
		TestTrue(TEXT("Curve reference accepted"), Session.ApplyProperty(TEXT("ForwardOffsetDeltaByPitchCurve"), Reason));
		TestTrue(TEXT("Curve reference saved"), Session.ApplyToAsset(Reason));
		TestTrue(TEXT("Source stores chosen external curve"), Template->ForwardOffsetDeltaByPitchCurve.ExternalCurve == Curve);
		TestEqual(TEXT("Source stores inline curve keys"), Template->ForwardOffsetDeltaByPitchCurve.EditorCurveData.Eval(0.f), 5.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditWorldReferenceTest,
	"ComposableCameraSystem.Editor.LiveEdit.WorldReferenceScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditWorldReferenceTest::RunTest(const FString& /*Parameters*/)
{
	FLiveEditFixture Selected, Other;
	FComposableCameraLiveEditSession Session;
	if (!TestTrue(TEXT("Bind selected PIE world"), Session.BindCamera(Selected.Camera))) return false;
	auto* Proxy = CastChecked<UComposableCameraFieldOfViewNode>(Session.GetEditingNode());
	FString Reason;
	Proxy->ActorsForDynamicFoV = { Other.Camera.Get() };
	TestFalse(TEXT("Unmapped actor from another PIE world rejected"), Session.ApplyProperty(TEXT("ActorsForDynamicFoV"), Reason));
	TestEqual(TEXT("Rejected edit restores accepted authoring value"), Proxy->ActorsForDynamicFoV.Num(), 0);
	TestFalse(TEXT("Rejected reference never creates runtime override"), Session.HasOverrides());
	Proxy->ActorsForDynamicFoV = { Selected.Camera.Get() };
	TestTrue(TEXT("Spawned actor in selected PIE world can be trialled"), Session.ApplyProperty(TEXT("ActorsForDynamicFoV"), Reason));
	TestFalse(TEXT("PIE-only actor cannot become asset default"), Session.ApplyToAsset(Reason));
	TestTrue(TEXT("Failed save retains trial for correction"), Session.HasChanges());
	Session.DetachRuntime();
	TestTrue(TEXT("PIE end releases unmapped actor in proxy"), Proxy->ActorsForDynamicFoV.Num() == 1 && !Proxy->ActorsForDynamicFoV[0]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditReferenceScopeTest,
	"ComposableCameraSystem.Editor.LiveEdit.ChangedReferenceScopeAndAtomicSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditReferenceScopeTest::RunTest(const FString& /*Parameters*/)
{
	FLiveEditFixture Selected, Other;
	// Simulate a caller-populated runtime field unrelated to the edited scalar.
	Selected.Node->ActorsForDynamicFoV = { Selected.Camera.Get(), Other.Camera.Get() };
	FComposableCameraLiveEditSession Session;
	if (!TestTrue(TEXT("Bind caller references"), Session.BindCamera(Selected.Camera))) return false;
	auto* Proxy = CastChecked<UComposableCameraFieldOfViewNode>(Session.GetEditingNode());
	FString Reason;
	TestTrue(TEXT("Unchanged cross-world reference does not block scalar trial"), SetFov(Session, 107.f, Reason));
	TestTrue(TEXT("Unchanged PIE-only reference does not block scalar save"), Session.ApplyToAsset(Reason));
	TestEqual(TEXT("Only edited scalar saved"), Selected.Template->FieldOfView, 107.f);
	TestEqual(TEXT("Unedited actor array remains authored empty"), Selected.Template->ActorsForDynamicFoV.Num(), 0);
	TestTrue(TEXT("Caller references untouched"), Selected.Node->ActorsForDynamicFoV.Num() == 2 && Selected.Node->ActorsForDynamicFoV[1] == Other.Camera);
	Proxy->ActorsForDynamicFoV = { Selected.Camera.Get() };
	TestTrue(TEXT("Selected PIE-only actor can be trialled"), Session.ApplyProperty(TEXT("ActorsForDynamicFoV"), Reason));
	TestTrue(TEXT("Second scalar trial accepted"), SetFov(Session, 118.f, Reason));
	const FString Before = UneditedAssetState(Selected.Asset);
	TestFalse(TEXT("Unsavable edited reference rejects entire Apply"), Session.ApplyToAsset(Reason));
	TestEqual(TEXT("Rejected Apply leaves source scalar unchanged"), Selected.Template->FieldOfView, 107.f);
	TestEqual(TEXT("Rejected Apply leaves all other serialized data unchanged"), UneditedAssetState(Selected.Asset), Before);
	TestTrue(TEXT("Rejected Apply retains editing reference"), Proxy->ActorsForDynamicFoV.Num() == 1 && Proxy->ActorsForDynamicFoV[0] == Selected.Camera);
	TestEqual(TEXT("Rejected Apply retains runtime scalar trial"), EvaluateFov(Selected.Node), 118.f);
	TestTrue(TEXT("Rejected Apply retains pending changes"), Session.HasChanges());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditDrivenSchemaTest,
	"ComposableCameraSystem.Editor.LiveEdit.RejectsDrivenPinSchemaLoss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditDrivenSchemaTest::RunTest(const FString& /*Parameters*/)
{
	if (!TestNotNull(TEXT("Editor transactor available"), GEditor ? GEditor->Trans.Get() : nullptr)) return false;
	for (bool bGraphOpen : { false, true })
		for (bool bExposed : { false, true })
		{
			FLiveEditFixture Fixture;
			auto* Template = NewObject<UComposableCameraPivotDampingNode>(Fixture.Asset, NAME_None, RF_Transactional);
			auto* Interpolator = NewObject<UComposableCameraIIRInterpolator>(Template, NAME_None, RF_Transactional);
			Template->UpwardInterpolator = Interpolator;
			Fixture.Asset->NodeTemplates = { Template };
			if (bExposed)
			{
				FComposableCameraExposedParameter Parameter;
				Parameter.ParameterName = TEXT("DampingSpeed");
				Parameter.PinType = EComposableCameraPinType::Float;
				Parameter.TargetNodeIndex = 0;
				Parameter.TargetPinName = TEXT("UpwardInterpolator.Speed");
				Fixture.Asset->ExposedParameters.Add(Parameter);
			}
			else
			{
				FComposableCameraInternalVariable Variable;
				Variable.VariableGuid = FGuid::NewGuid();
				Variable.VariableName = TEXT("DampingDriver");
				Variable.InitialValueString = TEXT("3");
				Fixture.Asset->InternalVariables.Add(Variable);
				FComposableCameraVariableNodeRecord Record;
				Record.NodeGuid = FGuid::NewGuid();
				Record.VariableGuid = Variable.VariableGuid;
				Record.VariableName = Variable.VariableName;
				FComposableCameraVariablePinConnection Connection;
				Connection.CameraNodeIndex = 0;
				Connection.CameraPinName = TEXT("UpwardInterpolator.Speed");
				Record.Connections.Add(Connection);
				Fixture.Asset->VariableNodes.Add(Record);
			}
			auto* Graph = bGraphOpen ? CreateAuthoringGraph(Fixture.Asset) : nullptr;
			Fixture.Camera = Fixture.World->SpawnActor<AComposableCameraCameraBase>();
			UE::ComposableCameras::ConstructCameraFromTypeAsset(Fixture.Camera, Fixture.Asset, FComposableCameraParameterBlock());
			FComposableCameraLiveEditSession Session;
			if (!TestTrue(TEXT("Bind driven interpolator"), Session.BindCamera(Fixture.Camera))) return false;
			CastChecked<UComposableCameraPivotDampingNode>(Session.GetEditingNode())->UpwardInterpolator = nullptr;
			FString Reason;
			TestTrue(TEXT("Schema change can be trialled"), Session.ApplyProperty(TEXT("UpwardInterpolator"), Reason));
			const FString Before = UneditedAssetState(Fixture.Asset);
			TestFalse(TEXT("Apply refuses to delete wired or exposed compound pin"), Session.ApplyToAsset(Reason));
			TestTrue(TEXT("Source interpolator identity preserved"), Template->UpwardInterpolator == Interpolator);
			TestEqual(TEXT("Serialized driver identity and connections preserved"), UneditedAssetState(Fixture.Asset), Before);
			TestTrue(TEXT("Failure does not materialize a closed authoring graph"), Fixture.Asset->EditorGraph == Graph);
			if (Graph && !bExposed)
			{
				auto* Pin = FindAuthoringNode(Graph, Template)->FindPin(TEXT("UpwardInterpolator.Speed"), EGPD_Input);
				TestTrue(TEXT("Live variable wire retained on both endpoints"), Pin && Pin->LinkedTo.Num() == 1 && Pin->LinkedTo[0]->LinkedTo.Contains(Pin));
			}
			TestTrue(TEXT("Schema trial can be reset"), Session.Reset());
		}
	{
		FLiveEditFixture Fixture;
		auto* Template = NewObject<UComposableCameraPivotDampingNode>(Fixture.Asset, NAME_None, RF_Transactional);
		Template->UpwardInterpolator = NewObject<UComposableCameraIIRInterpolator>(Template, NAME_None, RF_Transactional);
		Fixture.Asset->NodeTemplates = { Template };
		FComposableCameraPinOverride Default;
		Default.PinName = TEXT("UpwardInterpolator.Speed");
		Default.bHasDefaultOverride = true;
		Default.DefaultValueOverride = TEXT("4");
		Fixture.Asset->NodePinOverrides[0].Overrides.Add(Default);
		auto* Graph = CreateAuthoringGraph(Fixture.Asset);
		Fixture.Camera = Fixture.World->SpawnActor<AComposableCameraCameraBase>();
		UE::ComposableCameras::ConstructCameraFromTypeAsset(Fixture.Camera, Fixture.Asset, FComposableCameraParameterBlock());
		FComposableCameraLiveEditSession Session;
		Session.BindCamera(Fixture.Camera);
		CastChecked<UComposableCameraPivotDampingNode>(Session.GetEditingNode())->UpwardInterpolator = nullptr;
		FString Reason;
		TestTrue(TEXT("Undriven schema change can be trialled"), Session.ApplyProperty(TEXT("UpwardInterpolator"), Reason));
		TestTrue(TEXT("Undriven schema change can be saved"), Session.ApplyToAsset(Reason));
		TestFalse(TEXT("Removed compound pin leaves no orphan override"), Fixture.Asset->NodePinOverrides[0].Overrides.ContainsByPredicate([](const auto& Override) { return Override.PinName == TEXT("UpwardInterpolator.Speed"); }));
		TestTrue(TEXT("Undo schema removal"), GEditor->UndoTransaction());
		TestNotNull(TEXT("Undo restores owned interpolator"), Template->UpwardInterpolator.Get());
		TestTrue(TEXT("Undo restores compound default entry"), Fixture.Asset->NodePinOverrides[0].Overrides.ContainsByPredicate([](const auto& Override) { return Override.PinName == TEXT("UpwardInterpolator.Speed") && Override.DefaultValueOverride == TEXT("4"); }));
		TestNotNull(TEXT("Undo restores visible compound pin"), FindAuthoringNode(Graph, Template)->FindPin(TEXT("UpwardInterpolator.Speed"), EGPD_Input));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditObjectDefaultsTest,
	"ComposableCameraSystem.Editor.LiveEdit.ObjectPinDefaultsUndoRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditObjectDefaultsTest::RunTest(const FString& /*Parameters*/)
{
	if (!TestNotNull(TEXT("Editor transactor available"), GEditor ? GEditor->Trans.Get() : nullptr)) return false;
	FLiveEditFixture Fixture;
	auto* Template = NewObject<UComposableCameraHitchcockZoomNode>(Fixture.Asset, NAME_None, RF_Transactional);
	auto* BeforeCurve = NewObject<UCurveFloat>(Fixture.Asset, NAME_None, RF_Transactional);
	auto* AfterCurve = NewObject<UCurveFloat>(Fixture.Asset, NAME_None, RF_Transactional);
	Template->FOVDeltaCurve = BeforeCurve;
	Fixture.Asset->NodeTemplates = { Template };
	FComposableCameraPinOverride CurveOverride;
	CurveOverride.PinName = TEXT("FOVDeltaCurve");
	CurveOverride.bAsPin = true;
	CurveOverride.bHasDefaultOverride = true;
	CurveOverride.DefaultValueOverride = BeforeCurve->GetPathName();
	Fixture.Asset->NodePinOverrides[0].Overrides.Add(CurveOverride);
	auto* Graph = CreateAuthoringGraph(Fixture.Asset);
	auto* GraphNode = FindAuthoringNode(Graph, Template);
	if (!TestNotNull(TEXT("Object pin source graph node"), GraphNode)) return false;
	TestTrue(TEXT("Loaded object default populates native picker"), GraphNode->FindPin(TEXT("FOVDeltaCurve"), EGPD_Input)->DefaultObject == BeforeCurve);
	Fixture.Camera = Fixture.World->SpawnActor<AComposableCameraCameraBase>();
	UE::ComposableCameras::ConstructCameraFromTypeAsset(Fixture.Camera, Fixture.Asset, FComposableCameraParameterBlock());
	FComposableCameraLiveEditSession Session;
	Session.BindCamera(Fixture.Camera);
	CastChecked<UComposableCameraHitchcockZoomNode>(Session.GetEditingNode())->FOVDeltaCurve = AfterCurve;
	FString Reason;
	TestTrue(TEXT("Curve pointer trial accepted"), Session.ApplyProperty(TEXT("FOVDeltaCurve"), Reason));
	if (!TestTrue(TEXT("Curve pointer saved"), Session.ApplyToAsset(Reason))) return false;
	TestTrue(TEXT("Source curve matches native picker after Apply"), Template->FOVDeltaCurve == AfterCurve && GraphNode->FindPin(TEXT("FOVDeltaCurve"), EGPD_Input)->DefaultObject == AfterCurve);
	TestTrue(TEXT("Undo object default"), GEditor->UndoTransaction());
	TestTrue(TEXT("Undo source and picker agree"), Template->FOVDeltaCurve == BeforeCurve && GraphNode->FindPin(TEXT("FOVDeltaCurve"), EGPD_Input)->DefaultObject == BeforeCurve);
	TestTrue(TEXT("Redo object default"), GEditor->RedoTransaction());
	TestTrue(TEXT("Redo source and picker agree"), Template->FOVDeltaCurve == AfterCurve && GraphNode->FindPin(TEXT("FOVDeltaCurve"), EGPD_Input)->DefaultObject == AfterCurve);
	Graph->RebuildFromTypeAsset();
	TestTrue(TEXT("Reopened graph picker matches saved object"), FindAuthoringNode(Graph, Template)->FindPin(TEXT("FOVDeltaCurve"), EGPD_Input)->DefaultObject == AfterCurve);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraLiveEditGraphStabilityTest,
	"ComposableCameraSystem.Editor.LiveEdit.PreservesAuthoringGraphAndDrivers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraLiveEditGraphStabilityTest::RunTest(const FString& /*Parameters*/)
{
	if (!TestNotNull(TEXT("Editor transactor available"), GEditor ? GEditor->Trans.Get() : nullptr)) return false;
	FLiveEditFixture Fixture;
	auto* Damping = NewObject<UComposableCameraPivotDampingNode>(Fixture.Asset, NAME_None, RF_Transactional);
	Damping->UpwardInterpolator = NewObject<UComposableCameraIIRInterpolator>(Damping, NAME_None, RF_Transactional);
	auto* Offset = NewObject<UComposableCameraCameraOffsetNode>(Fixture.Asset, NAME_None, RF_Transactional);
	Fixture.Asset->NodeTemplates = { Damping, Offset, Fixture.Template.Get() };
	Fixture.Asset->NodePinOverrides.SetNum(3);
	Fixture.Asset->ExecutionOrder = { 0, 1, 2 };
	FComposableCameraPinConnection Wire;
	Wire.SourceNodeIndex = 0;
	Wire.SourcePinName = TEXT("PivotPosition");
	Wire.TargetNodeIndex = 1;
	Wire.TargetPinName = TEXT("PivotPosition");
	Fixture.Asset->PinConnections.Add(Wire);
	FComposableCameraExposedParameter Parameter;
	Parameter.ParameterName = TEXT("FOV");
	Parameter.PinType = EComposableCameraPinType::Float;
	Parameter.TargetNodeIndex = 2;
	Parameter.TargetPinName = TEXT("FieldOfView");
	Fixture.Asset->ExposedParameters.Add(Parameter);
	FComposableCameraInternalVariable Variable;
	Variable.VariableGuid = FGuid::NewGuid();
	Variable.VariableName = TEXT("DampingDriver");
	Variable.InitialValueString = TEXT("3");
	Fixture.Asset->InternalVariables.Add(Variable);
	FComposableCameraVariableNodeRecord Record;
	Record.NodeGuid = FGuid::NewGuid();
	Record.VariableGuid = Variable.VariableGuid;
	Record.VariableName = Variable.VariableName;
	Record.Position = FVector2D(-150, 250);
	FComposableCameraVariablePinConnection VariableWire;
	VariableWire.CameraNodeIndex = 0;
	VariableWire.CameraPinName = TEXT("UpwardInterpolator.Speed");
	Record.Connections.Add(VariableWire);
	Fixture.Asset->VariableNodes.Add(Record);
	Fixture.Asset->ComputeNodeTemplates.Add(NewObject<UComposableCameraBeginPlaySetRotationNode>(Fixture.Asset, NAME_None, RF_Transactional));
	Fixture.Asset->ComputeNodePinOverrides.SetNum(1);
	Fixture.Asset->ComputeExecutionOrder = { 0 };
	auto* Graph = CreateAuthoringGraph(Fixture.Asset);
	Graph->SyncToTypeAsset(); // Compare canonical durable data on both sides.
	const FString Before = UneditedAssetState(Fixture.Asset);
	const auto TemplatesBefore = Fixture.Asset->NodeTemplates;
	TMap<FGuid, FVector2D> NodePositions;
	for (const UEdGraphNode* Node : Graph->Nodes) NodePositions.Add(Node->NodeGuid, FVector2D(Node->NodePosX, Node->NodePosY));
	FComposableCameraParameterBlock Parameters;
	Parameters.Values.FindOrAdd(TEXT("FOV")).Set(EComposableCameraPinType::Float, 120.f);
	Fixture.Camera = Fixture.World->SpawnActor<AComposableCameraCameraBase>();
	UE::ComposableCameras::ConstructCameraFromTypeAsset(Fixture.Camera, Fixture.Asset, Parameters);
	const auto InputOffsetsBefore = Fixture.Camera->OwnedRuntimeDataBlock->InputPinSourceOffsets;
	FComposableCameraLiveEditSession Session;
	if (!TestTrue(TEXT("Bind authored graph"), Session.BindCamera(Fixture.Camera))) return false;
	const int32 DampingIndex = Fixture.Asset->NodeTemplates.IndexOfByKey(Damping);
	const int32 FovIndex = Fixture.Asset->NodeTemplates.IndexOfByKey(Fixture.Template);
	Session.SelectNode(DampingIndex);
	CastChecked<UComposableCameraIIRInterpolator>(CastChecked<UComposableCameraPivotDampingNode>(Session.GetEditingNode())->UpwardInterpolator)->Speed = 8.f;
	FString Reason;
	TestTrue(TEXT("Trial overrides current variable driver"), Session.ApplyProperty(TEXT("UpwardInterpolator"), Reason));
	Session.SelectNode(FovIndex);
	TestTrue(TEXT("Trial overrides caller parameter"), SetFov(Session, 109.f, Reason));
	if (!TestTrue(TEXT("Apply defaults while preserving graph"), Session.ApplyToAsset(Reason))) return false;
	TestEqual(TEXT("Unedited durable graph state preserved"), UneditedAssetState(Fixture.Asset), Before);
	TestTrue(TEXT("Template identity/order preserved"), Fixture.Asset->NodeTemplates == TemplatesBefore);
	for (const UEdGraphNode* Node : Graph->Nodes)
		TestTrue(TEXT("Graph node GUID and canvas position preserved"), NodePositions.Contains(Node->NodeGuid) && NodePositions[Node->NodeGuid] == FVector2D(Node->NodePosX, Node->NodePosY));
	auto* Output = FindAuthoringNode(Graph, Damping)->FindPin(TEXT("PivotPosition"), EGPD_Output);
	auto* Input = FindAuthoringNode(Graph, Offset)->FindPin(TEXT("PivotPosition"), EGPD_Input);
	TestTrue(TEXT("Existing camera wire survives reconstruction in both directions"), Output->LinkedTo.Contains(Input) && Input->LinkedTo.Contains(Output));
	TestEqual(TEXT("Runtime driver binding count preserved"), Fixture.Camera->OwnedRuntimeDataBlock->InputPinSourceOffsets.Num(), InputOffsetsBefore.Num());
	for (const auto& Pair : InputOffsetsBefore)
		TestTrue(TEXT("Runtime source offset preserved"), Fixture.Camera->OwnedRuntimeDataBlock->InputPinSourceOffsets.FindRef(Pair.Key) == Pair.Value);
	float CallerFov = 0.f;
	TestTrue(TEXT("Original caller parameter readable"), Fixture.Camera->SourceParameterBlock.Values[TEXT("FOV")].Get(CallerFov));
	TestEqual(TEXT("Apply does not overwrite caller parameter"), CallerFov, 120.f);
	TestTrue(TEXT("Undo edited defaults"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo preserves unedited graph state"), UneditedAssetState(Fixture.Asset), Before);
	TestEqual(TEXT("Undo restores nested child contents"), CastChecked<UComposableCameraIIRInterpolator>(Damping->UpwardInterpolator)->Speed, 1.f);
	TestTrue(TEXT("Redo edited defaults"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores owned child contents"), CastChecked<UComposableCameraIIRInterpolator>(Damping->UpwardInterpolator)->Speed, 8.f);
	TestEqual(TEXT("Redo preserves unedited graph state"), UneditedAssetState(Fixture.Asset), Before);
	TestTrue(TEXT("Reset removes all trial bindings"), Session.Reset());
	TestEqual(TEXT("Reset exposes original caller parameter"), EvaluateFov(CastChecked<UComposableCameraFieldOfViewNode>(Fixture.Camera->CameraNodes[FovIndex])), 120.f);
	TestEqual(TEXT("Reset exposes current variable driver"), CastChecked<UComposableCameraPivotDampingNode>(Fixture.Camera->CameraNodes[DampingIndex])->GetInputPinValue<float>(TEXT("UpwardInterpolator.Speed")), 3.f);
	Graph->RebuildFromTypeAsset();
	Graph->SyncToTypeAsset();
	TestEqual(TEXT("Graph rebuild/sync preserves durable data outside edited defaults"), UneditedAssetState(Fixture.Asset), Before);
	return true;
}

#endif
