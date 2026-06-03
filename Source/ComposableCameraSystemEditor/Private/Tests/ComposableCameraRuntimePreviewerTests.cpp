// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/ComposableCameraRuntimePreviewerViewportClient.h"
#include "Misc/AutomationTest.h"

#define LOCTEXT_NAMESPACE "ComposableCameraRuntimePreviewerTests"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimePreviewerMakeSubjectRelativeTransformTranslationTest,
	"ComposableCameraSystem.RuntimePreviewer.MakeSubjectRelativeTransform.Translation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimePreviewerMakeSubjectRelativeTransformTranslationTest::RunTest(
	const FString& /*Parameters*/)
{
	using ComposableCameraSystem::RuntimePreviewer::MakeSubjectRelativeTransform;

	const FTransform SubjectWorld(FRotator::ZeroRotator, FVector(100.0f, 50.0f, 0.0f));
	const FTransform SourceWorld(FRotator::ZeroRotator, FVector(130.0f, 70.0f, 10.0f));
	const FTransform Relative = MakeSubjectRelativeTransform(SourceWorld, SubjectWorld);

	TestEqual(TEXT("Relative X"), Relative.GetLocation().X, 30.0);
	TestEqual(TEXT("Relative Y"), Relative.GetLocation().Y, 20.0);
	TestEqual(TEXT("Relative Z"), Relative.GetLocation().Z, 10.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimePreviewerMakeSubjectRelativeTransformRotationTest,
	"ComposableCameraSystem.RuntimePreviewer.MakeSubjectRelativeTransform.Rotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimePreviewerMakeSubjectRelativeTransformRotationTest::RunTest(
	const FString& /*Parameters*/)
{
	using ComposableCameraSystem::RuntimePreviewer::MakeSubjectRelativeTransform;

	const FTransform SubjectWorld(FRotator(0.0, 90.0, 0.0), FVector::ZeroVector);
	const FVector ExpectedLocal(50.0, 0.0, 10.0);
	const FVector SourceWorldLocation = SubjectWorld.TransformPosition(ExpectedLocal);
	const FTransform SourceWorld(SubjectWorld.GetRotation(), SourceWorldLocation);
	const FTransform Relative = MakeSubjectRelativeTransform(SourceWorld, SubjectWorld);

	TestEqual(TEXT("Relative rotated X"), Relative.GetLocation().X, ExpectedLocal.X);
	TestEqual(TEXT("Relative rotated Y"), Relative.GetLocation().Y, ExpectedLocal.Y);
	TestEqual(TEXT("Relative rotated Z"), Relative.GetLocation().Z, ExpectedLocal.Z);
	TestTrue(TEXT("Source with same rotation as subject becomes local identity rotation"),
		Relative.GetRotation().Equals(FQuat::Identity, KINDA_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimePreviewerVisualSubjectReferenceKeepsCameraOrbitTest,
	"ComposableCameraSystem.RuntimePreviewer.VisualSubjectReferenceKeepsCameraOrbit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimePreviewerVisualSubjectReferenceKeepsCameraOrbitTest::RunTest(
	const FString& /*Parameters*/)
{
	using ComposableCameraSystem::RuntimePreviewer::MakeSubjectRelativeTransform;

	const FTransform RotatedPawnRoot(FRotator(0.0, 90.0, 0.0), FVector::ZeroVector);
	const FTransform StableVisualSubject(FRotator::ZeroRotator, FVector::ZeroVector);
	const FTransform CameraWorld(FRotator::ZeroRotator, FVector(0.0, -300.0, 80.0));

	const FTransform VisualRelative =
		MakeSubjectRelativeTransform(CameraWorld, StableVisualSubject);
	const FTransform RootRelative =
		MakeSubjectRelativeTransform(CameraWorld, RotatedPawnRoot);

	TestEqual(TEXT("Visual subject keeps camera orbit location unchanged"),
		VisualRelative.GetLocation(), CameraWorld.GetLocation());
	TestFalse(TEXT("Pawn root yaw would cancel the visible orbit"),
		RootRelative.GetLocation().Equals(VisualRelative.GetLocation(), KINDA_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimePreviewerSkeletalRootYawIsRemovedFromProxyTest,
	"ComposableCameraSystem.RuntimePreviewer.SkeletalRootYawIsRemovedFromProxy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimePreviewerSkeletalRootYawIsRemovedFromProxyTest::RunTest(
	const FString& /*Parameters*/)
{
	using ComposableCameraSystem::RuntimePreviewer::MakeSkeletalSubjectWorldTransform;
	using ComposableCameraSystem::RuntimePreviewer::MakeSubjectRelativeTransform;

	const FTransform ComponentWorld(FRotator::ZeroRotator, FVector::ZeroVector);
	TArray<FTransform> ComponentSpaceTransforms;
	ComponentSpaceTransforms.Add(FTransform(FRotator(0.0, 90.0, 0.0), FVector::ZeroVector));

	const FTransform SubjectWorld =
		MakeSkeletalSubjectWorldTransform(ComponentWorld, ComponentSpaceTransforms);
	const FTransform ProxyComponentRelative =
		MakeSubjectRelativeTransform(ComponentWorld, SubjectWorld);
	const FTransform PreviewRoot =
		ComponentSpaceTransforms[0] * ProxyComponentRelative;

	TestTrue(TEXT("Root bone yaw is removed by subject anchoring"),
		PreviewRoot.GetRotation().Equals(FQuat::Identity, KINDA_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimePreviewerStatusToTextTest,
	"ComposableCameraSystem.RuntimePreviewer.StatusToText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimePreviewerStatusToTextTest::RunTest(const FString& /*Parameters*/)
{
	const ERuntimePreviewerStatus Cases[] = {
		ERuntimePreviewerStatus::NoPIE,
		ERuntimePreviewerStatus::NoCamera,
		ERuntimePreviewerStatus::NoPawn,
		ERuntimePreviewerStatus::Live,
	};

	for (ERuntimePreviewerStatus Status: Cases)
	{
		TestFalse(FString::Printf(TEXT("Status %d text is non-empty"),
				static_cast<int32>(Status)),
			RuntimePreviewerStatusToText(Status).IsEmpty());
	}

	TestTrue(TEXT("Live status mentions Live"),
		RuntimePreviewerStatusToText(ERuntimePreviewerStatus::Live)
			.ToString()
			.Contains(TEXT("Live")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimePreviewerComputeFloorOffsetForBoundsTest,
	"ComposableCameraSystem.RuntimePreviewer.ComputeFloorOffsetForBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRuntimePreviewerComputeFloorOffsetForBoundsTest::RunTest(
	const FString& /*Parameters*/)
{
	using ComposableCameraSystem::RuntimePreviewer::ComputeFloorOffsetForBounds;

	const FBox BelowFloorBounds(FVector(-20.0, -20.0, -88.0),
		FVector(20.0, 20.0, 92.0));
	TestEqual(TEXT("Floor offset moves down to proxy bottom"),
		ComputeFloorOffsetForBounds(BelowFloorBounds), 88.0f);

	const FBox AboveFloorBounds(FVector(-20.0, -20.0, 2.0),
		FVector(20.0, 20.0, 180.0));
	TestEqual(TEXT("Floor offset stays at zero when proxy is above floor"),
		ComputeFloorOffsetForBounds(AboveFloorBounds), 0.0f);

	const FBox InvalidBounds(ForceInit);
	TestEqual(TEXT("Invalid bounds returns zero offset"),
		ComputeFloorOffsetForBounds(InvalidBounds), 0.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

#undef LOCTEXT_NAMESPACE
