// Copyright 2026 Sulley. All Rights Reserved.

#include "Core/ComposableCameraContextStack.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Misc/AutomationTest.h"
#include "Tests/ComposableCameraTestObjects.h"
#include "Utils/ComposableCameraProjectSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "ComposableCameraContextStackTests"

namespace ComposableCameraTest
{
	/**
	 * RAII helper that sets up test context names in project settings
	 * and restores the original names on destruction.
	 */
	struct FTestContextSetup
	{
		TArray<FName> OriginalContextNames;
		FName GameplayName;
		FName UIName;
		FName LevelSequenceName;

		FTestContextSetup()
		{
			GameplayName = FName("Gameplay");
			UIName = FName("UI");
			LevelSequenceName = FName("LevelSequence");

			// Save original settings and install test context names.
			UComposableCameraProjectSettings* Settings =
				GetMutableDefault<UComposableCameraProjectSettings>();
			OriginalContextNames = Settings->ContextNames;

			Settings->ContextNames.Empty();
			Settings->ContextNames.Add(GameplayName);
			Settings->ContextNames.Add(UIName);
			Settings->ContextNames.Add(LevelSequenceName);
		}

		~FTestContextSetup()
		{
			// Restore original settings.
			UComposableCameraProjectSettings* Settings =
				GetMutableDefault<UComposableCameraProjectSettings>();
			Settings->ContextNames = OriginalContextNames;
		}
	};

	/** Minimal world helper reused from EvaluationTree tests, extended for context stack. */
	struct FContextTestWorld
	{
		UWorld* World { nullptr };
		UComposableCameraContextStack* ContextStack { nullptr };

		FContextTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();

			ContextStack = NewObject<UComposableCameraContextStack>();
		}

		~FContextTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

// ============================================================================
// Test: EnsureContext creates a new context and returns its Director
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContextStackEnsureTest,
	"System.Engine.ComposableCameraSystem.ContextStack.EnsureContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContextStackEnsureTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FTestContextSetup Setup;
	ComposableCameraTest::FContextTestWorld TestWorld;

	UTEST_EQUAL("Stack empty initially", TestWorld.ContextStack->GetStackDepth(), 0);

	// Ensure the gameplay context.
	UComposableCameraDirector* GameplayDirector = TestWorld.ContextStack->EnsureContext(nullptr, Setup.GameplayName);
	UTEST_NOT_NULL("Gameplay Director created", GameplayDirector);
	UTEST_EQUAL("Stack depth is 1", TestWorld.ContextStack->GetStackDepth(), 1);
	UTEST_TRUE("Active context is Gameplay", TestWorld.ContextStack->GetActiveContextName() == Setup.GameplayName);

	// Ensure again. Should return the same Director, not create a new one.
	UComposableCameraDirector* SameDirector = TestWorld.ContextStack->EnsureContext(nullptr, Setup.GameplayName);
	UTEST_EQUAL("Same Director returned on second ensure", SameDirector, GameplayDirector);
	UTEST_EQUAL("Stack depth unchanged", TestWorld.ContextStack->GetStackDepth(), 1);

	return true;
}

// ============================================================================
// Test: LIFO stack ordering. Last pushed is active
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContextStackLIFOTest,
	"System.Engine.ComposableCameraSystem.ContextStack.LIFOOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContextStackLIFOTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FTestContextSetup Setup;
	ComposableCameraTest::FContextTestWorld TestWorld;

	// Push gameplay first.
	TestWorld.ContextStack->EnsureContext(nullptr, Setup.GameplayName);
	UTEST_TRUE("Active is Gameplay", TestWorld.ContextStack->GetActiveContextName() == Setup.GameplayName);

	// Push UI. Should become active (last in, first out).
	TestWorld.ContextStack->EnsureContext(nullptr, Setup.UIName);
	UTEST_EQUAL("Stack depth is 2", TestWorld.ContextStack->GetStackDepth(), 2);
	UTEST_TRUE("Active is UI (last pushed)", TestWorld.ContextStack->GetActiveContextName() == Setup.UIName);

	// Push LevelSequence. Should become active.
	TestWorld.ContextStack->EnsureContext(nullptr, Setup.LevelSequenceName);
	UTEST_EQUAL("Stack depth is 3", TestWorld.ContextStack->GetStackDepth(), 3);
	UTEST_TRUE("Active is LevelSequence (last pushed)", TestWorld.ContextStack->GetActiveContextName() == Setup.LevelSequenceName);

	// Pop LevelSequence -UI should become active.
	TestWorld.ContextStack->PopActiveContext();
	UTEST_EQUAL("Stack depth is 2 after pop", TestWorld.ContextStack->GetStackDepth(), 2);
	UTEST_TRUE("Active is UI after pop", TestWorld.ContextStack->GetActiveContextName() == Setup.UIName);

	// EnsureContext on a buried context moves it to top.
	// Stack is [Gameplay, UI]. Ensure Gameplay ->stack becomes [UI, Gameplay].
	UComposableCameraDirector* GameplayDir = TestWorld.ContextStack->EnsureContext(nullptr, Setup.GameplayName);
	UTEST_NOT_NULL("Gameplay Director returned", GameplayDir);
	UTEST_EQUAL("Stack depth unchanged", TestWorld.ContextStack->GetStackDepth(), 2);
	UTEST_TRUE("Active is Gameplay (moved to top)", TestWorld.ContextStack->GetActiveContextName() == Setup.GameplayName);

	// EnsureContext on the already-active context is a no-op.
	UComposableCameraDirector* SameDir = TestWorld.ContextStack->EnsureContext(nullptr, Setup.GameplayName);
	UTEST_EQUAL("Same Director on re-ensure", SameDir, GameplayDir);
	UTEST_TRUE("Active still Gameplay", TestWorld.ContextStack->GetActiveContextName() == Setup.GameplayName);

	return true;
}

// ============================================================================
// Test: Pop context by name
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContextStackPopByNameTest,
	"System.Engine.ComposableCameraSystem.ContextStack.PopByName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContextStackPopByNameTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FTestContextSetup Setup;
	ComposableCameraTest::FContextTestWorld TestWorld;

	TestWorld.ContextStack->EnsureContext(nullptr, Setup.GameplayName);
	TestWorld.ContextStack->EnsureContext(nullptr, Setup.UIName);
	TestWorld.ContextStack->EnsureContext(nullptr, Setup.LevelSequenceName);
	UTEST_EQUAL("Stack depth is 3", TestWorld.ContextStack->GetStackDepth(), 3);

	// Pop UI (middle of the stack, not the top).
	TestWorld.ContextStack->PopContext(Setup.UIName);
	UTEST_EQUAL("Stack depth is 2", TestWorld.ContextStack->GetStackDepth(), 2);
	UTEST_TRUE("Active is still LevelSequence", TestWorld.ContextStack->GetActiveContextName() == Setup.LevelSequenceName);

	// UI Director should no longer be findable.
	UComposableCameraDirector* UIDirector = TestWorld.ContextStack->GetDirectorForContext(Setup.UIName);
	UTEST_EQUAL("UI Director is null", UIDirector, static_cast<UComposableCameraDirector*>(nullptr));

	return true;
}

// ============================================================================
// Test: Cannot pop the last remaining context
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContextStackCannotPopBaseTest,
	"System.Engine.ComposableCameraSystem.ContextStack.CannotPopBase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContextStackCannotPopBaseTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FTestContextSetup Setup;
	ComposableCameraTest::FContextTestWorld TestWorld;

	TestWorld.ContextStack->EnsureContext(nullptr, Setup.GameplayName);
	UTEST_EQUAL("Stack depth is 1", TestWorld.ContextStack->GetStackDepth(), 1);

	// Try to pop the only context. Should be rejected.
	TestWorld.ContextStack->PopContext(Setup.GameplayName);
	UTEST_EQUAL("Stack depth still 1 after attempted pop", TestWorld.ContextStack->GetStackDepth(), 1);
	UTEST_TRUE("Gameplay still active", TestWorld.ContextStack->GetActiveContextName() == Setup.GameplayName);

	// Also try PopActiveContext. Same result.
	TestWorld.ContextStack->PopActiveContext();
	UTEST_EQUAL("Stack depth still 1 after PopActiveContext", TestWorld.ContextStack->GetStackDepth(), 1);

	return true;
}

// ============================================================================
// Test: Undefined context name returns nullptr
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContextStackUndefinedNameTest,
	"System.Engine.ComposableCameraSystem.ContextStack.UndefinedName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContextStackUndefinedNameTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FTestContextSetup Setup;
	ComposableCameraTest::FContextTestWorld TestWorld;

	// Try to ensure a name that's not in project settings.
	FName UnknownName = FName("Unknown");
	UComposableCameraDirector* Director = TestWorld.ContextStack->EnsureContext(nullptr, UnknownName);
	UTEST_EQUAL("Unknown name returns nullptr", Director, static_cast<UComposableCameraDirector*>(nullptr));
	UTEST_EQUAL("Stack depth unchanged", TestWorld.ContextStack->GetStackDepth(), 0);

	return true;
}

// ============================================================================
// Test: GetDirectorForContext finds contexts by name
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContextStackGetDirectorTest,
	"System.Engine.ComposableCameraSystem.ContextStack.GetDirectorForContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContextStackGetDirectorTest::RunTest(const FString& Parameters)
{
	ComposableCameraTest::FTestContextSetup Setup;
	ComposableCameraTest::FContextTestWorld TestWorld;

	UComposableCameraDirector* GameplayDir = TestWorld.ContextStack->EnsureContext(nullptr, Setup.GameplayName);
	UComposableCameraDirector* UIDir = TestWorld.ContextStack->EnsureContext(nullptr, Setup.UIName);

	UTEST_EQUAL("GetDirectorForContext(Gameplay) matches",
		TestWorld.ContextStack->GetDirectorForContext(Setup.GameplayName), GameplayDir);
	UTEST_EQUAL("GetDirectorForContext(UI) matches",
		TestWorld.ContextStack->GetDirectorForContext(Setup.UIName), UIDir);
	UTEST_EQUAL("GetDirectorForContext(LS) is null",
		TestWorld.ContextStack->GetDirectorForContext(Setup.LevelSequenceName),
		static_cast<UComposableCameraDirector*>(nullptr));

	return true;
}

#undef LOCTEXT_NAMESPACE
