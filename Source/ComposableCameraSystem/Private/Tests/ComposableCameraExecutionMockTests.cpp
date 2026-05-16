// Copyright Sulley. All rights reserved.

// Execution mock tests: simulate realistic multi-frame camera scenarios.
// These tests exercise the full evaluation + collapse pipeline over time.

#include "Core/ComposableCameraEvaluationTree.h"
#include "Core/ComposableCameraDirector.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Misc/AutomationTest.h"
#include "Tests/ComposableCameraTestObjects.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "ComposableCameraExecutionMockTests"

namespace ComposableCameraTest
{
	// Reuse FTestWorld from EvaluationTreeTests (same translation unit linkage).
	// Redeclare here for self-contained compilation.

	struct FExecTestWorld
	{
		UWorld* World { nullptr };

		FExecTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FExecTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		AComposableCameraCameraBase* SpawnCamera(
			const FVector& Location = FVector::ZeroVector,
			float FOV = 75.f)
		{
			FTransform Transform;
			Transform.SetLocation(Location);

			AComposableCameraCameraBase* Camera = World->SpawnActorDeferred<AComposableCameraCameraBase>(
				AComposableCameraCameraBase::StaticClass(), Transform);

			Camera->CameraPose.Position = Location;
			Camera->CameraPose.SetFieldOfViewDegrees(FOV);
			Camera->LastFrameCameraPose.Position = Location;
			Camera->LastFrameCameraPose.SetFieldOfViewDegrees(FOV);

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
// Mock Scenario 1: "Gameplay->Cutscene -> Back to Gameplay"
// Simulates a typical flow: gameplay camera active, cutscene camera overrides
// with a transition, cutscene transition finishes, then cutscene ends and
// goes back to gameplay with another transition.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FExecMockGameplayCutsceneFlowTest,
	"System.Engine.ComposableCameraSystem.Execution.GameplayCutsceneFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExecMockGameplayCutsceneFlowTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FExecTestWorld TestWorld;
	UComposableCameraEvaluationTree* Tree = TestWorld.CreateTree();

	const float DT = 0.016f; // ~60fps

	// Frame 0: Gameplay camera activated (camera cut).
	AComposableCameraCameraBase* GameplayCam = TestWorld.SpawnCamera(FVector(0, 0, 100), 90.f);
	Tree->OnActivateNewCamera(GameplayCam, nullptr);
	UTEST_EQUAL("Running is GameplayCam", Tree->GetRunningCamera(), GameplayCam);

	// Frames 1-10: gameplay camera running alone.
	for (int i = 0; i < 10; ++i)
	{
		FComposableCameraPose Pose = Tree->Evaluate(DT);
		UTEST_TRUE("Pose from gameplay cam", FMath::IsNearlyEqual(Pose.Position.Z, 100.0, 1.0));
	}

	// Frame 11: Cutscene camera activates with blend transition (blend = 0.0 ->full source).
	UComposableCameraTestTransition* ToCutsceneTrans = TestWorld.CreateTransition(false, 0.0f);
	AComposableCameraCameraBase* CutsceneCam = TestWorld.SpawnCamera(FVector(500, 500, 50), 60.f);
	Tree->OnActivateNewCamera(CutsceneCam, ToCutsceneTrans);
	UTEST_EQUAL("Running is CutsceneCam", Tree->GetRunningCamera(), CutsceneCam);

	// Frame 12: Evaluate during transition. At blend=0, output should be full source (GameplayCam).
	{
		FComposableCameraPose Pose = Tree->Evaluate(DT);
		// BlendFactor 0.0 ->Lerp(source, target, 0) = source.
		UTEST_TRUE("During transition at blend=0: position from GameplayCam",
			FMath::IsNearlyEqual(Pose.Position.Z, 100.0, 1.0));
	}

	// Frame 13: Mid-transition. Change blend to 0.5.
	ToCutsceneTrans->BlendFactor = 0.5f;
	{
		FComposableCameraPose Pose = Tree->Evaluate(DT);
		// Lerp(100, 50, 0.5) = 75 for Z.
		UTEST_TRUE("During transition at blend=0.5: Z position is 75",
			FMath::IsNearlyEqual(Pose.Position.Z, 75.0, 1.0));
	}

	// Frame 14: Transition finishes ->collapse to CutsceneCam.
	ToCutsceneTrans->SetFinished(true);
	ToCutsceneTrans->BlendFactor = 1.0f;
	{
		FComposableCameraPose Pose = Tree->Evaluate(DT);
		// After collapse, tree is just CutsceneCam. GameplayCam destroyed.
		UTEST_FALSE("GameplayCam destroyed after transition", IsValid(GameplayCam));
	}

	// Frames 15-20: Cutscene running.
	for (int i = 0; i < 6; ++i)
	{
		FComposableCameraPose Pose = Tree->Evaluate(DT);
		UTEST_TRUE("Cutscene pose Z", FMath::IsNearlyEqual(Pose.Position.Z, 50.0, 1.0));
	}

	// Frame 21: Cutscene ends, new gameplay camera takes over with transition.
	UComposableCameraTestTransition* BackToGameplay = TestWorld.CreateTransition(false, 0.0f);
	AComposableCameraCameraBase* GameplayCam2 = TestWorld.SpawnCamera(FVector(0, 0, 100), 90.f);
	Tree->OnActivateNewCamera(GameplayCam2, BackToGameplay);

	// Frame 22: Transition immediately finishes (fast cut).
	BackToGameplay->SetFinished(true);
	BackToGameplay->BlendFactor = 1.0f;
	{
		FComposableCameraPose Pose = Tree->Evaluate(DT);
		UTEST_FALSE("CutsceneCam destroyed", IsValid(CutsceneCam));
		UTEST_EQUAL("Running is GameplayCam2", Tree->GetRunningCamera(), GameplayCam2);
	}

	return true;
}

// ============================================================================
// Mock Scenario 2: "Rapid camera switches during combat"
// Simulates rapid target-lock switches: A->B ->C -> D with overlapping
// transitions. Each new activation creates a deeper tree, then transitions
// finish in order.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FExecMockRapidSwitchTest,
	"System.Engine.ComposableCameraSystem.Execution.RapidCameraSwitches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExecMockRapidSwitchTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FExecTestWorld TestWorld;
	UComposableCameraEvaluationTree* Tree = TestWorld.CreateTree();

	const float DT = 0.016f;

	// Setup: 4 cameras activated in rapid succession with transitions.
	AComposableCameraCameraBase* CamA = TestWorld.SpawnCamera(FVector(100, 0, 0));
	Tree->OnActivateNewCamera(CamA, nullptr);

	UComposableCameraTestTransition* TransAB = TestWorld.CreateTransition(false, 0.5f);
	AComposableCameraCameraBase* CamB = TestWorld.SpawnCamera(FVector(200, 0, 0));
	Tree->OnActivateNewCamera(CamB, TransAB);

	UComposableCameraTestTransition* TransBC = TestWorld.CreateTransition(false, 0.5f);
	AComposableCameraCameraBase* CamC = TestWorld.SpawnCamera(FVector(300, 0, 0));
	Tree->OnActivateNewCamera(CamC, TransBC);

	UComposableCameraTestTransition* TransCD = TestWorld.CreateTransition(false, 0.5f);
	AComposableCameraCameraBase* CamD = TestWorld.SpawnCamera(FVector(400, 0, 0));
	Tree->OnActivateNewCamera(CamD, TransCD);

	// Tree structure: ((A -TransAB->B) -TransBC->C) -TransCD->D
	UTEST_EQUAL("Running is CamD", Tree->GetRunningCamera(), CamD);

	// Simulate a few frames. All transitions active.
	for (int i = 0; i < 3; ++i)
	{
		(void)Tree->Evaluate(DT);
	}
	UTEST_TRUE("All cameras valid during transitions", IsValid(CamA) && IsValid(CamB) && IsValid(CamC) && IsValid(CamD));

	// TransAB finishes first (innermost left subtree).
	TransAB->SetFinished(true);
	(void)Tree->Evaluate(DT);
	UTEST_FALSE("CamA destroyed after TransAB collapse", IsValid(CamA));
	UTEST_TRUE("CamB still valid (promoted)", IsValid(CamB));
	UTEST_TRUE("CamC still valid", IsValid(CamC));
	UTEST_TRUE("CamD still valid", IsValid(CamD));

	// TransBC finishes next.
	TransBC->SetFinished(true);
	(void)Tree->Evaluate(DT);
	UTEST_FALSE("CamB destroyed after TransBC collapse", IsValid(CamB));
	UTEST_TRUE("CamC still valid (promoted)", IsValid(CamC));
	UTEST_TRUE("CamD still valid", IsValid(CamD));

	// TransCD finishes last.
	TransCD->SetFinished(true);
	(void)Tree->Evaluate(DT);
	UTEST_FALSE("CamC destroyed after TransCD collapse", IsValid(CamC));
	UTEST_TRUE("CamD still valid (final camera)", IsValid(CamD));
	UTEST_EQUAL("Running is still CamD", Tree->GetRunningCamera(), CamD);

	// Tree should now be a single leaf. Evaluate clean.
	FComposableCameraPose FinalPose = Tree->Evaluate(DT);
	UTEST_TRUE("Final pose from CamD", FMath::IsNearlyEqual(FinalPose.Position.X, 400.0, 1.0));

	return true;
}

// ============================================================================
// Mock Scenario 3: "Camera cut interrupts active transition"
// During an A->A transition, a camera cut to C occurs. The entire old tree
// (A and B) should be destroyed, and C becomes the sole camera.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FExecMockCutInterruptsTransitionTest,
	"System.Engine.ComposableCameraSystem.Execution.CutInterruptsTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExecMockCutInterruptsTransitionTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FExecTestWorld TestWorld;
	UComposableCameraEvaluationTree* Tree = TestWorld.CreateTree();

	const float DT = 0.016f;

	AComposableCameraCameraBase* CamA = TestWorld.SpawnCamera(FVector(100, 0, 0));
	Tree->OnActivateNewCamera(CamA, nullptr);

	UComposableCameraTestTransition* TransAB = TestWorld.CreateTransition(false, 0.5f);
	AComposableCameraCameraBase* CamB = TestWorld.SpawnCamera(FVector(200, 0, 0));
	Tree->OnActivateNewCamera(CamB, TransAB);

	// Evaluate a frame. Transition active.
	(void)Tree->Evaluate(DT);
	UTEST_TRUE("A valid during transition", IsValid(CamA));
	UTEST_TRUE("B valid during transition", IsValid(CamB));

	// Camera CUT to C (nullptr transition). Should destroy entire old tree.
	AComposableCameraCameraBase* CamC = TestWorld.SpawnCamera(FVector(300, 0, 0));
	Tree->OnActivateNewCamera(CamC, nullptr);

	UTEST_FALSE("CamA destroyed by camera cut", IsValid(CamA));
	UTEST_FALSE("CamB destroyed by camera cut", IsValid(CamB));
	UTEST_EQUAL("Running is CamC", Tree->GetRunningCamera(), CamC);

	// Clean evaluation post-cut.
	FComposableCameraPose Pose = Tree->Evaluate(DT);
	UTEST_TRUE("Pose from CamC", FMath::IsNearlyEqual(Pose.Position.X, 300.0, 1.0));

	return true;
}

// ============================================================================
// Mock Scenario 4: "Transition over a transition"
// A->A transition active, then B transition starts while A->A is still running.
// Both transitions active simultaneously, forming a 3-level tree.
// The outer (A->A) finishes first, then (B) finishes.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FExecMockNestedTransitionTest,
	"System.Engine.ComposableCameraSystem.Execution.NestedTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExecMockNestedTransitionTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FExecTestWorld TestWorld;
	UComposableCameraEvaluationTree* Tree = TestWorld.CreateTree();

	const float DT = 0.016f;

	// Initial camera.
	AComposableCameraCameraBase* CamA = TestWorld.SpawnCamera(FVector(0, 0, 0));
	Tree->OnActivateNewCamera(CamA, nullptr);

	// Start A->A transition.
	UComposableCameraTestTransition* TransAB = TestWorld.CreateTransition(false, 0.3f);
	AComposableCameraCameraBase* CamB = TestWorld.SpawnCamera(FVector(100, 0, 0));
	Tree->OnActivateNewCamera(CamB, TransAB);

	// Evaluate a couple frames mid-transition.
	(void)Tree->Evaluate(DT);
	(void)Tree->Evaluate(DT);

	// Start B transition while A->A is still running.
	// Tree becomes: (A -TransAB->B) -TransBC->C
	UComposableCameraTestTransition* TransBC = TestWorld.CreateTransition(false, 0.7f);
	AComposableCameraCameraBase* CamC = TestWorld.SpawnCamera(FVector(200, 0, 0));
	Tree->OnActivateNewCamera(CamC, TransBC);

	UTEST_EQUAL("Running is CamC", Tree->GetRunningCamera(), CamC);
	UTEST_TRUE("CamA valid (nested)", IsValid(CamA));
	UTEST_TRUE("CamB valid (nested)", IsValid(CamB));

	// Inner transition TransAB finishes while TransBC still active.
	TransAB->SetFinished(true);
	(void)Tree->Evaluate(DT);

	UTEST_FALSE("CamA destroyed (inner collapsed)", IsValid(CamA));
	UTEST_TRUE("CamB valid (promoted to leaf)", IsValid(CamB));
	UTEST_TRUE("CamC valid (target of outer)", IsValid(CamC));

	// Continue evaluation. Now tree is B -TransBC->C.
	(void)Tree->Evaluate(DT);

	// TransBC finishes.
	TransBC->SetFinished(true);
	(void)Tree->Evaluate(DT);

	UTEST_FALSE("CamB destroyed (outer collapsed)", IsValid(CamB));
	UTEST_TRUE("CamC is the final camera", IsValid(CamC));
	UTEST_EQUAL("Running is CamC", Tree->GetRunningCamera(), CamC);

	return true;
}

// ============================================================================
// Mock Scenario 5: "Multi-frame blend verification"
// Verify that blend weights produce correct interpolated poses over time.
// Simulates a transition where the blend factor changes each frame.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FExecMockBlendVerificationTest,
	"System.Engine.ComposableCameraSystem.Execution.BlendVerification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExecMockBlendVerificationTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FExecTestWorld TestWorld;
	UComposableCameraEvaluationTree* Tree = TestWorld.CreateTree();

	const float DT = 0.016f;

	// Source at X=0, Target at X=1000.
	AComposableCameraCameraBase* Source = TestWorld.SpawnCamera(FVector(0, 0, 0), 60.f);
	Tree->OnActivateNewCamera(Source, nullptr);

	UComposableCameraTestTransition* Trans = TestWorld.CreateTransition(false, 0.0f);
	AComposableCameraCameraBase* Target = TestWorld.SpawnCamera(FVector(1000, 0, 0), 120.f);
	Tree->OnActivateNewCamera(Target, Trans);

	// Simulate 10 frames where blend progresses from 0.0 to 1.0.
	for (int i = 0; i <= 10; ++i)
	{
		float Alpha = static_cast<float>(i) / 10.f;
		Trans->BlendFactor = Alpha;

		FComposableCameraPose Pose = Tree->Evaluate(DT);

		float ExpectedX = FMath::Lerp(0.f, 1000.f, Alpha);
		float ExpectedFOV = FMath::Lerp(60.f, 120.f, Alpha);

		UTEST_TRUE(*FString::Printf(TEXT("Frame %d: X position ~%.0f"), i, ExpectedX),
			FMath::IsNearlyEqual(Pose.Position.X, static_cast<double>(ExpectedX), 1.0));
		UTEST_TRUE(*FString::Printf(TEXT("Frame %d: FOV ~%.0f"), i, ExpectedFOV),
			FMath::IsNearlyEqual(Pose.GetEffectiveFieldOfView(), static_cast<double>(ExpectedFOV), 1.0));
	}

	// Mark finished and collapse.
	Trans->SetFinished(true);
	Trans->BlendFactor = 1.0f;
	(void)Tree->Evaluate(DT);

	UTEST_FALSE("Source destroyed after transition", IsValid(Source));
	UTEST_EQUAL("Running is Target", Tree->GetRunningCamera(), Target);

	// Post-collapse, pose should be pure target.
	FComposableCameraPose FinalPose = Tree->Evaluate(DT);
	UTEST_TRUE("Final X is 1000", FMath::IsNearlyEqual(FinalPose.Position.X, 1000.0, 1.0));
	UTEST_TRUE("Final FOV is 120", FMath::IsNearlyEqual(FinalPose.GetEffectiveFieldOfView(), 120.0, 1.0));

	return true;
}

// ============================================================================
// Mock Scenario 5b: "Transition keeps its initial rotation path while endpoints move"
// The source camera can keep receiving control rotation input during a blend.
// Rotation blending must keep the path chosen at transition start, then fade the
// source endpoint's live offset out instead of recalculating the shortest path
// from the newly-rotated source to the fixed target every frame.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FExecMockTransitionKeepsInitialRotationPathWithMovingSourceTest,
	"System.Engine.ComposableCameraSystem.Execution.RotationBlend.KeepsInitialPathWithMovingSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExecMockTransitionKeepsInitialRotationPathWithMovingSourceTest::RunTest(const FString& Parameters)
{
	constexpr float DT = 0.016f;

	UComposableCameraTestTransition* Transition = NewObject<UComposableCameraTestTransition>();
	Transition->BlendFactor = 0.5f;

	FComposableCameraPose SourcePose;
	SourcePose.Rotation = FRotator(0.f, 170.f, 0.f);

	FComposableCameraPose TargetPose;
	TargetPose.Rotation = FRotator(0.f, -170.f, 0.f);

	FComposableCameraTransitionInitParams InitParams;
	InitParams.CurrentSourcePose = SourcePose;
	InitParams.PreviousSourcePose = SourcePose;
	InitParams.DeltaTime = DT;
	Transition->TransitionEnabled(InitParams);
	Transition->SetTransitionTime(10.f);
	Transition->ResetTransitionState();

	(void)Transition->Evaluate(DT, SourcePose, TargetPose);

	const float SourceYawFrames[] = { 130.f, 90.f, 50.f, 10.f, -30.f };
	FComposableCameraPose ResultPose;
	for (const float SourceYaw : SourceYawFrames)
	{
		SourcePose.Rotation = FRotator(0.f, SourceYaw, 0.f);
		ResultPose = Transition->Evaluate(DT, SourcePose, TargetPose);
	}

	// Initial path: 170 -> -170 goes forward by +20 deg, so alpha 0.5 is 180.
	// Source then moves by -200 deg over several frames; at alpha 0.5 that
	// source-side offset contributes -100 deg, producing yaw 80. Recomputing
	// the shortest path from the live source (-30) to the target (-170) would
	// instead produce -100, a 180-degree flip.
	UTEST_TRUE("Moving source endpoint is applied as a faded live offset on the initial path",
		FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(ResultPose.Rotation.Yaw, 80.f), 0.f, 0.01f));

	return true;
}

// ============================================================================
// Mock Scenario 5c: "Nested transitions apply endpoint offsets per node"
// Tree shape is (A->B)->C. A and C keep rotating while B stays fixed. Each
// transition node should preserve its own initial path, so A's live offset fades
// through the inner transition and then through the outer source side, while C's
// live offset fades in through the outer target side.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FExecMockNestedTransitionKeepsRotationPathWithMovingEndpointsTest,
	"System.Engine.ComposableCameraSystem.Execution.RotationBlend.NestedKeepsInitialPathsWithMovingEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExecMockNestedTransitionKeepsRotationPathWithMovingEndpointsTest::RunTest(const FString& Parameters)
{
	constexpr float DT = 0.016f;

	UComposableCameraTestTransition* InnerTransition = NewObject<UComposableCameraTestTransition>();
	InnerTransition->BlendFactor = 0.5f;
	InnerTransition->SetTransitionTime(10.f);

	UComposableCameraTestTransition* OuterTransition = NewObject<UComposableCameraTestTransition>();
	OuterTransition->BlendFactor = 0.5f;
	OuterTransition->SetTransitionTime(10.f);

	FComposableCameraPose APose;
	APose.Rotation = FRotator(0.f, 170.f, 0.f);

	FComposableCameraPose BPose;
	BPose.Rotation = FRotator(0.f, -170.f, 0.f);

	FComposableCameraTransitionInitParams InnerInitParams;
	InnerInitParams.CurrentSourcePose = APose;
	InnerInitParams.PreviousSourcePose = APose;
	InnerInitParams.DeltaTime = DT;
	InnerTransition->TransitionEnabled(InnerInitParams);
	InnerTransition->ResetTransitionState();

	FComposableCameraPose ABPose = InnerTransition->Evaluate(DT, APose, BPose);

	FComposableCameraPose CPose;
	CPose.Rotation = FRotator(0.f, -170.f, 0.f);

	FComposableCameraTransitionInitParams OuterInitParams;
	OuterInitParams.CurrentSourcePose = ABPose;
	OuterInitParams.PreviousSourcePose = ABPose;
	OuterInitParams.DeltaTime = DT;
	OuterTransition->TransitionEnabled(OuterInitParams);
	OuterTransition->ResetTransitionState();

	FComposableCameraPose ResultPose = OuterTransition->Evaluate(DT, ABPose, CPose);

	const float AYawFrames[] = { 130.f, 90.f, 50.f, 10.f, -30.f };
	const float CYawFrames[] = { -150.f, -130.f, -110.f, -90.f, -70.f };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(AYawFrames); ++Index)
	{
		APose.Rotation = FRotator(0.f, AYawFrames[Index], 0.f);
		CPose.Rotation = FRotator(0.f, CYawFrames[Index], 0.f);

		ABPose = InnerTransition->Evaluate(DT, APose, BPose);
		ResultPose = OuterTransition->Evaluate(DT, ABPose, CPose);
	}

	// Inner A->B starts at yaw 180 and A contributes -200 * 0.5 = -100,
	// so the live inner output reaches yaw 80. The outer transition started
	// at source 180 -> target -170 (fixed midpoint 185/-175). Its source
	// live offset is -100 at 0.5 weight and its target live offset is +100
	// at 0.5 weight, so they cancel and the outer midpoint remains -175.
	UTEST_TRUE("Nested transition nodes preserve their own rotation paths",
		FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(ResultPose.Rotation.Yaw, -175.f), 0.f, 0.01f));

	return true;
}

// ============================================================================
// Mock Scenario 6: "Reference leaf inter-context transition with evaluation"
// Simulates context pop with a transition: the new active context's tree
// contains a reference leaf pointing to the popped context's Director.
// The reference leaf evaluates the source Director each frame.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FExecMockInterContextTransitionTest,
	"System.Engine.ComposableCameraSystem.Execution.InterContextTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExecMockInterContextTransitionTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FExecTestWorld TestWorld;

	const float DT = 0.016f;

	// Source context's Director. Has its own camera.
	UComposableCameraDirector* SourceDirector = TestWorld.CreateDirector();
	AComposableCameraCameraBase* SourceCam = TestWorld.SpawnCamera(FVector(0, 0, 0));
	SourceDirector->ResumeCamera(SourceCam, nullptr, FTransform::Identity);

	// Target context's tree. Activate a camera with reference source.
	UComposableCameraEvaluationTree* TargetTree = TestWorld.CreateTree();
	AComposableCameraCameraBase* TargetCam = TestWorld.SpawnCamera(FVector(500, 0, 0));
	UComposableCameraTestTransition* Trans = TestWorld.CreateTransition(false, 0.5f);

	TargetTree->OnActivateNewCameraWithReferenceSource(TargetCam, Trans, SourceDirector);

	UTEST_EQUAL("Running camera is TargetCam", TargetTree->GetRunningCamera(), TargetCam);

	// Evaluate several frames. Reference leaf should evaluate source Director live.
	for (int i = 0; i < 5; ++i)
	{
		FComposableCameraPose Pose = TargetTree->Evaluate(DT);
		// At blend 0.5: Lerp(source, target, 0.5).
		// Source Director evaluates SourceCam ->pose at (0,0,0).
		// Target is at (500,0,0).
		// Expected X: Lerp(0, 500, 0.5) = 250.
		UTEST_TRUE(*FString::Printf(TEXT("Frame %d: blended X ~250"), i),
			FMath::IsNearlyEqual(Pose.Position.X, 250.0, 5.0));
	}

	// Transition finishes. Reference leaf discarded, target promoted.
	Trans->SetFinished(true);
	(void)TargetTree->Evaluate(DT);

	UTEST_EQUAL("Running is TargetCam after collapse", TargetTree->GetRunningCamera(), TargetCam);
	// Source Director NOT destroyed (reference leaf doesn't own it).
	UTEST_TRUE("Source Director still valid", IsValid(SourceDirector));

	// Post-collapse evaluation. Pure target.
	FComposableCameraPose FinalPose = TargetTree->Evaluate(DT);
	UTEST_TRUE("Final X is 500", FMath::IsNearlyEqual(FinalPose.Position.X, 500.0, 1.0));

	return true;
}

// ============================================================================
// Mock Scenario 7: "Empty tree evaluation safety"
// Evaluate a tree with no cameras. Should return default pose, not crash.
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FExecMockEmptyTreeTest,
	"System.Engine.ComposableCameraSystem.Execution.EmptyTreeSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExecMockEmptyTreeTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FExecTestWorld TestWorld;
	UComposableCameraEvaluationTree* Tree = TestWorld.CreateTree();

	UTEST_FALSE("Empty tree has no active camera", Tree->HasActiveCamera());
	UTEST_EQUAL("RunningCamera is null", Tree->GetRunningCamera(), static_cast<AComposableCameraCameraBase*>(nullptr));

	// Evaluate empty tree. Should not crash.
	FComposableCameraPose Pose = Tree->Evaluate(0.016f);
	UTEST_TRUE("Default pose position is zero", Pose.Position.IsNearlyZero());

	return true;
}

#undef LOCTEXT_NAMESPACE
