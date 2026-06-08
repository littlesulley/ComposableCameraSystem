// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraCameraOffsetNode.h"

#include "Misc/AutomationTest.h"

#define LOCTEXT_NAMESPACE "ComposableCameraCameraOffsetNodeTests"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCameraOffsetAddsForwardDeltaByPitchCurveTest,
	"System.Engine.ComposableCameraSystem.Nodes.CameraOffset.AddsForwardDeltaByPitchCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCameraOffsetAddsForwardDeltaByPitchCurveTest::RunTest(const FString& Parameters)
{
	UComposableCameraCameraOffsetNode* Node = NewObject<UComposableCameraCameraOffsetNode>();
	Node->PivotPosition = FVector::ZeroVector;
	Node->CameraOffset = FVector(100.f, 0.f, 0.f);
	Node->ForwardOffsetDeltaByPitchCurve.GetRichCurve()->AddKey(30.f, 40.f);
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose InPose;
	InPose.Rotation = FRotator(30.f, 0.f, 0.f);

	FComposableCameraPose OutPose = InPose;
	Node->TickNode(0.016f, InPose, OutPose);

	const FVector ExpectedPosition = InPose.Rotation.Vector() * 140.f;
	UTEST_TRUE("CameraOffset.X includes additive forward delta sampled by pitch",
		OutPose.Position.Equals(ExpectedPosition, KINDA_SMALL_NUMBER));

	return true;
}

#undef LOCTEXT_NAMESPACE
