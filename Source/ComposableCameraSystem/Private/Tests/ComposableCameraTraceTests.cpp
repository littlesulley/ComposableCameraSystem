// Copyright 2026 Sulley. All Rights Reserved.

#include "Debug/ComposableCameraDebugDrawSink.h"
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
	Input.Add(FComposableCameraDebugPrimitive::MakePoint(
		FVector(7.0, 8.0, 9.0),
		FColor::Blue,
		3.0f,
		SDPG_World));
	Input.Add(FComposableCameraDebugPrimitive::MakeBox(
		FVector(11.0, 12.0, 13.0),
		FVector(14.0, 15.0, 16.0),
		FQuat::Identity,
		FColor::White,
		SDPG_Foreground));

	FComposableCameraTracePose FrustumPose;
	FrustumPose.Location = FVector(100.0, 200.0, 300.0);
	FrustumPose.Rotation = FRotator(10.0, 20.0, 30.0);
	FrustumPose.FieldOfView = 55.0f;
	FrustumPose.OrthoWidth = 777.0f;
	Input.Add(FComposableCameraDebugPrimitive::MakeCameraFrustum(
		FrustumPose,
		FColor::Yellow,
		SDPG_Foreground));

	TArray<uint8> Bytes;
	UTEST_TRUE("Serialize primitives", SerializeComposableCameraDebugPrimitives(Input, Bytes));

	TArray<FComposableCameraDebugPrimitive> Output;
	UTEST_TRUE("Deserialize primitives", DeserializeComposableCameraDebugPrimitives(Bytes, Output));

	UTEST_EQUAL("Primitive count survives", Output.Num(), 5);
	UTEST_EQUAL("First primitive kind survives", Output[0].Kind, EComposableCameraDebugPrimitiveKind::Line);
	UTEST_EQUAL("Line start survives", Output[0].A, FVector(1.0, 2.0, 3.0));
	UTEST_EQUAL("Line end survives", Output[0].B, FVector(4.0, 5.0, 6.0));
	UTEST_EQUAL("Line color survives", Output[0].Color, FColor::Red);
	UTEST_EQUAL("Sphere kind survives", Output[1].Kind, EComposableCameraDebugPrimitiveKind::SolidSphere);
	UTEST_EQUAL("Sphere radius survives", Output[1].Radius, 42.0f);
	UTEST_EQUAL("Sphere alpha survives", Output[1].Alpha, static_cast<uint8>(96));
	UTEST_EQUAL("Point kind survives", Output[2].Kind, EComposableCameraDebugPrimitiveKind::Point);
	UTEST_EQUAL("Point location survives", Output[2].A, FVector(7.0, 8.0, 9.0));
	UTEST_EQUAL("Point size survives", Output[2].Size, 3.0f);
	UTEST_EQUAL("Box kind survives", Output[3].Kind, EComposableCameraDebugPrimitiveKind::Box);
	UTEST_EQUAL("Box center survives", Output[3].A, FVector(11.0, 12.0, 13.0));
	UTEST_EQUAL("Box extent survives", Output[3].Extent, FVector(14.0, 15.0, 16.0));
	UTEST_EQUAL("Frustum kind survives", Output[4].Kind, EComposableCameraDebugPrimitiveKind::CameraFrustum);
	UTEST_EQUAL("Frustum location survives", Output[4].A, FVector(100.0, 200.0, 300.0));
	UTEST_EQUAL("Frustum rotation survives", Output[4].Rotation, FRotator(10.0, 20.0, 30.0));
	UTEST_EQUAL("Frustum FOV survives", Output[4].Radius, 55.0f);
	UTEST_EQUAL("Frustum ortho width survives", Output[4].Size, 777.0f);

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
	Output.Add(FComposableCameraDebugPrimitive::MakePoint(
		FVector::ZeroVector,
		FColor::White,
		1.0f,
		SDPG_World));

	UTEST_FALSE("Malformed primitive stream fails cleanly",
		DeserializeComposableCameraDebugPrimitives(Bytes, Output));
	UTEST_EQUAL("Malformed primitive stream produces no output", Output.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraTraceRejectsInvalidPrimitiveKindTest,
	"ComposableCameraSystem.RewindTrace.RejectsInvalidPrimitiveKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTraceRejectsInvalidPrimitiveKindTest::RunTest(const FString& Parameters)
{
	TArray<FComposableCameraDebugPrimitive> Input;
	Input.Add(FComposableCameraDebugPrimitive::MakeLine(
		FVector::ZeroVector,
		FVector::OneVector,
		FColor::Red,
		1.0f,
		SDPG_Foreground));

	TArray<uint8> Bytes;
	UTEST_TRUE("Serialize valid primitive", SerializeComposableCameraDebugPrimitives(Input, Bytes));

	constexpr int32 PrimitiveKindOffset = sizeof(uint8) + sizeof(uint8) + sizeof(int32);
	UTEST_TRUE("Serialized primitive has kind byte", Bytes.IsValidIndex(PrimitiveKindOffset));
	Bytes[PrimitiveKindOffset] = 255;

	TArray<FComposableCameraDebugPrimitive> Output;
	Output.Add(FComposableCameraDebugPrimitive::MakePoint(
		FVector::ZeroVector,
		FColor::White,
		1.0f,
		SDPG_World));

	UTEST_FALSE("Invalid primitive kind fails cleanly",
		DeserializeComposableCameraDebugPrimitives(Bytes, Output));
	UTEST_EQUAL("Invalid primitive kind produces no output", Output.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraTraceRejectsOversizedPrimitiveSerializeTest,
	"ComposableCameraSystem.RewindTrace.RejectsOversizedPrimitiveSerialize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTraceRejectsOversizedPrimitiveSerializeTest::RunTest(const FString& Parameters)
{
	TArray<FComposableCameraDebugPrimitive> Input;
	Input.SetNum(16385);

	TArray<uint8> Bytes;
	Bytes.Add(0xff);

	UTEST_FALSE("Oversized primitive stream serialize fails cleanly",
		SerializeComposableCameraDebugPrimitives(Input, Bytes));
	UTEST_EQUAL("Oversized primitive stream produces no bytes", Bytes.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraTraceRejectsTruncatedPrimitiveStreamTest,
	"ComposableCameraSystem.RewindTrace.RejectsTruncatedPrimitiveStream",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTraceRejectsTruncatedPrimitiveStreamTest::RunTest(const FString& Parameters)
{
	TArray<uint8> Bytes;
	Bytes.Add(0xcc);
	Bytes.Add(0x01);
	Bytes.Add(0x01);
	Bytes.Add(0x00);
	Bytes.Add(0x00);
	Bytes.Add(0x00);

	TArray<FComposableCameraDebugPrimitive> Output;
	Output.Add(FComposableCameraDebugPrimitive::MakePoint(
		FVector::ZeroVector,
		FColor::White,
		1.0f,
		SDPG_World));

	UTEST_FALSE("Truncated primitive stream fails cleanly",
		DeserializeComposableCameraDebugPrimitives(Bytes, Output));
	UTEST_EQUAL("Truncated primitive stream produces no output", Output.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraTraceCaptureSinkRecordsPrimitivesTest,
	"ComposableCameraSystem.RewindTrace.CaptureSinkRecordsPrimitives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraTraceCaptureSinkRecordsPrimitivesTest::RunTest(const FString& Parameters)
{
	TArray<FComposableCameraDebugPrimitive> Primitives;
	FComposableCameraPrimitiveCaptureSink Sink(Primitives);

	Sink.DrawLine(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), FColor::Blue, 3.0f, SDPG_Foreground);
	Sink.DrawPoint(FVector(2.0, 0.0, 0.0), FColor::Red, 5.0f, SDPG_World);
	Sink.DrawSphere(FVector(3.0, 0.0, 0.0), 9.0f, FColor::Green, 80, SDPG_Foreground, true);

	UTEST_EQUAL("Capture sink recorded three primitives", Primitives.Num(), 3);
	UTEST_EQUAL("First primitive line", Primitives[0].Kind, EComposableCameraDebugPrimitiveKind::Line);
	UTEST_EQUAL("Second primitive point", Primitives[1].Kind, EComposableCameraDebugPrimitiveKind::Point);
	UTEST_EQUAL("Third primitive solid sphere", Primitives[2].Kind, EComposableCameraDebugPrimitiveKind::SolidSphere);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
