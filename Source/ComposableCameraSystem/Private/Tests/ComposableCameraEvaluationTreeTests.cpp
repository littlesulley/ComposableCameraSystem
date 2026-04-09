// Copyright Sulley. All rights reserved.

#include "Core/ComposableCameraEvaluationTree.h"
#include "Core/ComposableCameraDirector.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Misc/AutomationTest.h"
#include "Tests/ComposableCameraTestObjects.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "ComposableCameraEvaluationTreeTests"

namespace ComposableCameraTest
{
	/**
	 * Helper to create a minimal test world for spawning actors.
	 * Returns the world and a cleanup lambda.
	 */
	struct FTestWorld
	{
		UWorld* World { nullptr };

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		AComposableCameraCameraBase* SpawnCamera(
			const FVector& Location = FVector::ZeroVector,
			bool bTransient = false,
			float LifeTime = -1.f)
		{
			FTransform Transform;
			Transform.SetLocation(Location);

			AComposableCameraCameraBase* Camera = World->SpawnActorDeferred<AComposableCameraCameraBase>(
				AComposableCameraCameraBase::StaticClass(), Transform);

			Camera->bIsTransient = bTransient;
			Camera->LifeTime = LifeTime;
			Camera->RemainingLifeTime = LifeTime;

			// Set the camera pose directly so we can verify evaluation results.
			Camera->CameraPose.Position = Location;
			Camera->LastFrameCameraPose.Position = Location;

			Camera->FinishSpawning(Transform);

			return Camera;
		}

		UComposableCameraTestTransition* CreateTransition(bool bStartFinished = false, float BlendFactor = 0.5f)
		{
			UComposableCameraTestTransition* Transition = NewObject<UComposableCameraTestTransition>();
			Transition->SetFinished(bStartFinished);
			Transition->BlendFactor = BlendFactor;
			return Transition;
		}

		UComposableCameraEvaluationTree* CreateTree()
		{
			return NewObject<UComposableCameraEvaluationTree>();
		}

		UComposableCameraDirector* CreateDirector()
		{
			return NewObject<UComposableCameraDirector>();
		}
	};
}

// ============================================================================
// Test: Tree node type identification
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEvalTreeNodeTypeTest,
	"System.Engine.ComposableCameraSystem.EvaluationTree.NodeTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEvalTreeNodeTypeTest::RunTest(const FString& Parameters)
{
	// Leaf node.
	{
		FComposableCameraEvaluationTreeNode Node;
		Node.Wrapper.Set<FComposableCameraEvaluationTreeLeafNodeWrapper>(FComposableCameraEvaluationTreeLeafNodeWrapper{});
		UTEST_TRUE("Leaf IsLeaf", Node.IsLeaf());
		UTEST_FALSE("Leaf IsInner", Node.IsInner());
		UTEST_FALSE("Leaf IsReferenceLeaf", Node.IsReferenceLeaf());
	}

	// Reference leaf node.
	{
		FComposableCameraEvaluationTreeNode Node;
		Node.Wrapper.Set<FComposableCameraEvaluationTreeReferenceLeafNodeWrapper>(FComposableCameraEvaluationTreeReferenceLeafNodeWrapper{});
		UTEST_TRUE("RefLeaf IsReferenceLeaf", Node.IsReferenceLeaf());
		UTEST_FALSE("RefLeaf IsLeaf", Node.IsLeaf());
		UTEST_FALSE("RefLeaf IsInner", Node.IsInner());
	}

	// Inner node.
	{
		FComposableCameraEvaluationTreeNode Node;
		Node.Wrapper.Set<FComposableCameraEvaluationTreeInnerNodeWrapper>(FComposableCameraEvaluationTreeInnerNodeWrapper{});
		UTEST_TRUE("Inner IsInner", Node.IsInner());
		UTEST_FALSE("Inner IsLeaf", Node.IsLeaf());
		UTEST_FALSE("Inner IsReferenceLeaf", Node.IsReferenceLeaf());
	}

	return true;
}

// ============================================================================
// Test: Activate camera without transition (camera cut)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEvalTreeCameraCutTest,
	"System.Engine.ComposableCameraSystem.EvaluationTree.CameraCut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEvalTreeCameraCutTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FTestWorld TestWorld;
	UComposableCameraEvaluationTree* Tree = TestWorld.CreateTree();

	// No camera yet.
	UTEST_FALSE("No active camera initially", Tree->HasActiveCamera());

	// Activate first camera (camera cut — no transition).
	AComposableCameraCameraBase* CamA = TestWorld.SpawnCamera(FVector(100, 0, 0));
	Tree->OnActivateNewCamera(CamA, nullptr);

	UTEST_TRUE("Tree has active camera after activation", Tree->HasActiveCamera());
	UTEST_EQUAL("RunningCamera is CamA", Tree->GetRunningCamera(), CamA);

	// Activate second camera (camera cut — replaces CamA).
	AComposableCameraCameraBase* CamB = TestWorld.SpawnCamera(FVector(200, 0, 0));
	Tree->OnActivateNewCamera(CamB, nullptr);

	UTEST_EQUAL("RunningCamera is CamB after cut", Tree->GetRunningCamera(), CamB);

	return true;
}

// ============================================================================
// Test: Normal transition collapse (A → transition → B, transition finishes → B wins)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEvalTreeNormalCollapseTest,
	"System.Engine.ComposableCameraSystem.EvaluationTree.NormalCollapse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEvalTreeNormalCollapseTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FTestWorld TestWorld;
	UComposableCameraEvaluationTree* Tree = TestWorld.CreateTree();

	// Activate CamA.
	AComposableCameraCameraBase* CamA = TestWorld.SpawnCamera(FVector(100, 0, 0));
	Tree->OnActivateNewCamera(CamA, nullptr);

	// Activate CamB with a transition.
	UComposableCameraTestTransition* Transition = TestWorld.CreateTransition(false, 0.5f);
	AComposableCameraCameraBase* CamB = TestWorld.SpawnCamera(FVector(200, 0, 0));
	Tree->OnActivateNewCamera(CamB, Transition);

	UTEST_EQUAL("RunningCamera is CamB", Tree->GetRunningCamera(), CamB);

	// Evaluate while transition is in progress — tree should stay intact.
	(void)Tree->Evaluate(0.016f);
	UTEST_TRUE("Tree still has active camera during transition", Tree->HasActiveCamera());

	// Mark transition as finished and evaluate again — should collapse to CamB.
	Transition->SetFinished(true);
	(void)Tree->Evaluate(0.016f);

	UTEST_EQUAL("RunningCamera is still CamB after collapse", Tree->GetRunningCamera(), CamB);
	// CamA should have been destroyed by the collapse.
	UTEST_FALSE("CamA is destroyed", IsValid(CamA));

	return true;
}

// ============================================================================
// Test: Reference leaf evaluates source Director live
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEvalTreeReferenceLeafTest,
	"System.Engine.ComposableCameraSystem.EvaluationTree.ReferenceLeaf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEvalTreeReferenceLeafTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FTestWorld TestWorld;

	// Source Director — its internal tree is empty, so Evaluate() returns a default pose.
	// This is fine for testing the reference leaf wiring and collapse behavior.
	UComposableCameraDirector* SourceDirector = TestWorld.CreateDirector();

	// Target tree: CamB with a reference leaf pointing to SourceDirector.
	UComposableCameraEvaluationTree* TargetTree = TestWorld.CreateTree();
	AComposableCameraCameraBase* CamB = TestWorld.SpawnCamera(FVector(200, 0, 0));
	UComposableCameraTestTransition* Transition = TestWorld.CreateTransition(false, 0.5f);

	TargetTree->OnActivateNewCameraWithReferenceSource(CamB, Transition, SourceDirector);

	UTEST_EQUAL("Target tree's running camera is CamB", TargetTree->GetRunningCamera(), CamB);

	// Evaluate — reference leaf calls SourceDirector->Evaluate() (returns default pose).
	// Transition blends default pose with CamB's pose. No crash = reference leaf works.
	FComposableCameraPose Pose = TargetTree->Evaluate(0.016f);
	UTEST_TRUE("Tree still has active camera", TargetTree->HasActiveCamera());
	UTEST_EQUAL("Running camera unchanged", TargetTree->GetRunningCamera(), CamB);

	// Transition finishes → normal collapse to right, reference leaf is discarded.
	Transition->SetFinished(true);
	(void)TargetTree->Evaluate(0.016f);

	UTEST_EQUAL("RunningCamera is CamB after collapse", TargetTree->GetRunningCamera(), CamB);
	// Source Director is still alive — reference leaf doesn't own or destroy it.
	UTEST_TRUE("Source Director still valid", IsValid(SourceDirector));

	return true;
}

// ============================================================================
// Test: Reference leaf with camera cut (no transition)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEvalTreeReferenceLeafCutTest,
	"System.Engine.ComposableCameraSystem.EvaluationTree.ReferenceLeafCut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEvalTreeReferenceLeafCutTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FTestWorld TestWorld;

	UComposableCameraDirector* SourceDirector = TestWorld.CreateDirector();
	UComposableCameraEvaluationTree* TargetTree = TestWorld.CreateTree();
	AComposableCameraCameraBase* CamB = TestWorld.SpawnCamera(FVector(200, 0, 0));

	// Camera cut with reference source — no transition, should just place the leaf directly.
	TargetTree->OnActivateNewCameraWithReferenceSource(CamB, nullptr, SourceDirector);

	UTEST_EQUAL("RunningCamera is CamB after cut", TargetTree->GetRunningCamera(), CamB);

	FComposableCameraPose Pose = TargetTree->Evaluate(0.016f);
	UTEST_TRUE("Evaluation succeeds", TargetTree->HasActiveCamera());

	return true;
}

// ============================================================================
// Test: DestroyAll cleans up the tree
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEvalTreeDestroyAllTest,
	"System.Engine.ComposableCameraSystem.EvaluationTree.DestroyAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEvalTreeDestroyAllTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FTestWorld TestWorld;
	UComposableCameraEvaluationTree* Tree = TestWorld.CreateTree();

	AComposableCameraCameraBase* CamA = TestWorld.SpawnCamera(FVector(100, 0, 0));
	Tree->OnActivateNewCamera(CamA, nullptr);

	UComposableCameraTestTransition* Transition = TestWorld.CreateTransition(false);
	AComposableCameraCameraBase* CamB = TestWorld.SpawnCamera(FVector(200, 0, 0));
	Tree->OnActivateNewCamera(CamB, Transition);

	UTEST_TRUE("Tree has cameras before destroy", Tree->HasActiveCamera());

	Tree->DestroyAll();

	UTEST_FALSE("Tree has no cameras after destroy", Tree->HasActiveCamera());
	UTEST_EQUAL("RunningCamera is null after destroy", Tree->GetRunningCamera(), static_cast<AComposableCameraCameraBase*>(nullptr));
	UTEST_FALSE("CamA is destroyed", IsValid(CamA));
	UTEST_FALSE("CamB is destroyed", IsValid(CamB));

	return true;
}

// ============================================================================
// Test: Chained collapse — D→B, E→B, B→A, C→A
// When B finishes before A, B collapses to E. Then when A finishes, A collapses to C.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEvalTreeChainedCollapseTest,
	"System.Engine.ComposableCameraSystem.EvaluationTree.ChainedCollapse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEvalTreeChainedCollapseTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FTestWorld TestWorld;
	UComposableCameraEvaluationTree* Tree = TestWorld.CreateTree();

	// Build the tree: D→B, E→B, B→A, C→A
	// Step 1: Activate CamD (leaf root).
	AComposableCameraCameraBase* CamD = TestWorld.SpawnCamera(FVector(100, 0, 0));
	Tree->OnActivateNewCamera(CamD, nullptr);

	// Step 2: Activate CamE with transition B (inner node B: left=D, right=E).
	UComposableCameraTestTransition* TransB = TestWorld.CreateTransition(false, 0.5f);
	AComposableCameraCameraBase* CamE = TestWorld.SpawnCamera(FVector(200, 0, 0));
	Tree->OnActivateNewCamera(CamE, TransB);

	// Step 3: Activate CamC with transition A (inner node A: left=B, right=C).
	UComposableCameraTestTransition* TransA = TestWorld.CreateTransition(false, 0.5f);
	AComposableCameraCameraBase* CamC = TestWorld.SpawnCamera(FVector(300, 0, 0));
	Tree->OnActivateNewCamera(CamC, TransA);

	UTEST_EQUAL("RunningCamera is CamC", Tree->GetRunningCamera(), CamC);

	// Evaluate — both transitions active, full tree intact.
	(void)Tree->Evaluate(0.016f);
	UTEST_TRUE("CamD valid during both transitions", IsValid(CamD));
	UTEST_TRUE("CamE valid during both transitions", IsValid(CamE));
	UTEST_TRUE("CamC valid during both transitions", IsValid(CamC));

	// B finishes (inner transition between D and E).
	// After collapse: A's left becomes leaf E, A's right is leaf C.
	TransB->SetFinished(true);
	(void)Tree->Evaluate(0.016f);

	UTEST_FALSE("CamD destroyed after B collapses", IsValid(CamD));
	UTEST_TRUE("CamE still valid (promoted from B)", IsValid(CamE));
	UTEST_TRUE("CamC still valid (A still active)", IsValid(CamC));
	UTEST_EQUAL("RunningCamera is still CamC", Tree->GetRunningCamera(), CamC);

	// A finishes (inner transition between E and C).
	// After collapse: tree is just leaf C. E is destroyed.
	TransA->SetFinished(true);
	(void)Tree->Evaluate(0.016f);

	UTEST_FALSE("CamE destroyed after A collapses", IsValid(CamE));
	UTEST_TRUE("CamC still valid (final camera)", IsValid(CamC));
	UTEST_EQUAL("RunningCamera is CamC after full collapse", Tree->GetRunningCamera(), CamC);

	return true;
}

// ============================================================================
// Test: Source destroyed mid-transition triggers collapse
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEvalTreeSourceDestroyedCollapseTest,
	"System.Engine.ComposableCameraSystem.EvaluationTree.SourceDestroyedCollapse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEvalTreeSourceDestroyedCollapseTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FTestWorld TestWorld;
	UComposableCameraEvaluationTree* Tree = TestWorld.CreateTree();

	// Activate CamA, then CamB with transition.
	AComposableCameraCameraBase* CamA = TestWorld.SpawnCamera(FVector(100, 0, 0));
	Tree->OnActivateNewCamera(CamA, nullptr);

	UComposableCameraTestTransition* Trans = TestWorld.CreateTransition(false, 0.5f);
	AComposableCameraCameraBase* CamB = TestWorld.SpawnCamera(FVector(200, 0, 0));
	Tree->OnActivateNewCamera(CamB, Trans);

	// Externally destroy CamA (source) while transition is still active.
	CamA->Destroy();

	// Evaluate — should detect source destroyed and collapse to CamB.
	(void)Tree->Evaluate(0.016f);

	UTEST_TRUE("CamB still valid after source destroyed", IsValid(CamB));
	UTEST_EQUAL("RunningCamera is CamB", Tree->GetRunningCamera(), CamB);

	return true;
}

#undef LOCTEXT_NAMESPACE
