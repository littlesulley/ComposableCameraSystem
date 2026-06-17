// Copyright 2026 Sulley. All Rights Reserved.

#include "Debug/ComposableCameraTraceTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraTracePrimitiveRoundTripTest,
	"ComposableCameraSystem.RewindTrace.PrimitiveRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTracePrimitiveRoundTripTest::RunTest(const FString& Parameters)
{
	TArray<FComposableCameraDebugPrimitive> Input;
	Input.Add(FComposableCameraDebugPrimitive::MakeLine(
		FVector(1.0, 2.0, 3.0),
		FVector(4.0, 5.0, 6.0),
		FColor::Red,
		2.0f,
		SDPG_Foreground));
	Input.Add(FComposableCameraDebugPrimitive::MakeSphere(
		FVector(10.0, 20.0, 30.0),
		42.0f,
		FColor::Green,
		96,
		SDPG_Foreground,
		/*bSolid=*/true));

	TArray<uint8> Bytes;
	UTEST_TRUE("Serialize primitives", SerializeComposableCameraDebugPrimitives(Input, Bytes));

	TArray<FComposableCameraDebugPrimitive> Output;
	UTEST_TRUE("Deserialize primitives", DeserializeComposableCameraDebugPrimitives(Bytes, Output));

	UTEST_EQUAL("Primitive count survives", Output.Num(), 2);
	UTEST_EQUAL("First primitive kind survives", Output[0].Kind, EComposableCameraDebugPrimitiveKind::Line);
	UTEST_EQUAL("Line start survives", Output[0].A, FVector(1.0, 2.0, 3.0));
	UTEST_EQUAL("Line end survives", Output[0].B, FVector(4.0, 5.0, 6.0));
	UTEST_EQUAL("Line color survives", Output[0].Color, FColor::Red);
	UTEST_EQUAL("Sphere kind survives", Output[1].Kind, EComposableCameraDebugPrimitiveKind::SolidSphere);
	UTEST_EQUAL("Sphere radius survives", Output[1].Radius, 42.0f);
	UTEST_EQUAL("Sphere alpha survives", Output[1].Alpha, static_cast<uint8>(96));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraTraceMatchGameplayPCMTest,
	"ComposableCameraSystem.RewindTrace.MatchGameplayPCM",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTraceMatchGameplayPCMTest::RunTest(const FString& Parameters)
{
	FComposableCameraActiveTraceFrame Active;
	Active.SourceKind = EComposableCameraTraceSourceKind::CCS_PCM;
	Active.PlayerCameraManagerId = 101;
	Active.FrameCycle = 700;

	FComposableCameraEvaluationTraceFrame Eval;
	Eval.SourceKind = EComposableCameraTraceSourceKind::CCS_PCM;
	Eval.SourceObjectId = 101;
	Eval.FrameCycle = 700;

	UTEST_TRUE("Gameplay PCM frame matches same PCM id and cycle",
		DoesComposableCameraEvaluationMatchActiveFrame(Active, Eval));

	Eval.FrameCycle = 701;
	UTEST_FALSE("Gameplay PCM frame rejects different cycle",
		DoesComposableCameraEvaluationMatchActiveFrame(Active, Eval));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraTraceMatchLevelSequenceTest,
	"ComposableCameraSystem.RewindTrace.MatchLevelSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTraceMatchLevelSequenceTest::RunTest(const FString& Parameters)
{
	FComposableCameraActiveTraceFrame Active;
	Active.SourceKind = EComposableCameraTraceSourceKind::CCS_LevelSequence;
	Active.ViewTargetActorId = 202;

	FComposableCameraEvaluationTraceFrame Eval;
	Eval.SourceKind = EComposableCameraTraceSourceKind::CCS_LevelSequence;
	Eval.ViewTargetActorId = 202;

	UTEST_TRUE("Level Sequence frame matches same view target actor",
		DoesComposableCameraEvaluationMatchActiveFrame(Active, Eval));

	Eval.ViewTargetActorId = 303;
	UTEST_FALSE("Level Sequence frame rejects different view target actor",
		DoesComposableCameraEvaluationMatchActiveFrame(Active, Eval));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraTraceRejectsMalformedPrimitiveStreamTest,
	"ComposableCameraSystem.RewindTrace.RejectsMalformedPrimitiveStream",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTraceRejectsMalformedPrimitiveStreamTest::RunTest(const FString& Parameters)
{
	TArray<uint8> Bytes;
	Bytes.Add(0x7f);
	Bytes.Add(0x01);
	Bytes.Add(0x02);

	TArray<FComposableCameraDebugPrimitive> Output;
	UTEST_FALSE("Malformed primitive stream fails cleanly",
		DeserializeComposableCameraDebugPrimitives(Bytes, Output));
	UTEST_EQUAL("Malformed primitive stream produces no output", Output.Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
