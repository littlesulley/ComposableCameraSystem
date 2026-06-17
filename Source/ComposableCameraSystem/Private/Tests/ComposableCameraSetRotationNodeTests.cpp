// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraSetRotationNode.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"
#include "Misc/AutomationTest.h"

#define LOCTEXT_NAMESPACE "ComposableCameraSetRotationNodeTests"

namespace
{
	AActor* SpawnSetRotationTestActor(UWorld* World, const FVector& Location)
	{
		AActor* Actor = World ? World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity) : nullptr;
		if (!Actor)
		{
			return nullptr;
		}

		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("SetRotationTestRoot"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSetRotationFromTwoActorsTest,
	"System.Engine.ComposableCameraSystem.Nodes.SetRotation.FromTwoActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSetRotationFromTwoActorsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AActor* FirstActor = SpawnSetRotationTestActor(World, FVector::ZeroVector);
	AActor* SecondActor = SpawnSetRotationTestActor(World, FVector(0.f, 100.f, 100.f));

	UComposableCameraSetRotationNode* Node = NewObject<UComposableCameraSetRotationNode>();
	Node->RotationSource = EComposableCameraSetRotationSource::FromTwoActors;
	Node->FirstActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->FirstActor = FirstActor;
	Node->SecondActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->SecondActor = SecondActor;
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose InPose;
	InPose.Rotation = FRotator::ZeroRotator;

	FComposableCameraPose OutPose = InPose;
	Node->TickNode(0.016f, InPose, OutPose);

	const FRotator Expected = UKismetMathLibrary::MakeRotFromX(
		SecondActor->GetActorLocation() - FirstActor->GetActorLocation());

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("SetRotation FromTwoActors uses direction from first actor to second actor",
		OutPose.Rotation.Equals(Expected, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSetRotationAppliesRotationOffsetTest,
	"System.Engine.ComposableCameraSystem.Nodes.SetRotation.AppliesRotationOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSetRotationAppliesRotationOffsetTest::RunTest(const FString& Parameters)
{
	UComposableCameraSetRotationNode* Node = NewObject<UComposableCameraSetRotationNode>();
	Node->RotationSource = EComposableCameraSetRotationSource::FromVector;
	Node->RotationVector = FVector::ForwardVector;
	Node->RotationOffset = FRotator(0.f, 90.f, 0.f);
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose InPose;
	InPose.Rotation = FRotator::ZeroRotator;

	FComposableCameraPose OutPose = InPose;
	Node->TickNode(0.016f, InPose, OutPose);

	const FRotator Expected = FRotator(0.f, 90.f, 0.f);
	UTEST_TRUE("SetRotation applies RotationOffset after resolving the base rotation",
		OutPose.Rotation.Equals(Expected, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSetRotationOffsetYawWorldPitchLocalTest,
	"System.Engine.ComposableCameraSystem.Nodes.SetRotation.OffsetYawWorldPitchLocal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSetRotationOffsetYawWorldPitchLocalTest::RunTest(const FString& Parameters)
{
	UComposableCameraSetRotationNode* Node = NewObject<UComposableCameraSetRotationNode>();
	Node->RotationSource = EComposableCameraSetRotationSource::FromRotator;
	Node->Rotation = FRotator(25.f, 70.f, 15.f);
	Node->RotationOffset = FRotator(20.f, 45.f, 0.f);
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose InPose;
	InPose.Rotation = FRotator::ZeroRotator;

	FComposableCameraPose OutPose = InPose;
	Node->TickNode(0.016f, InPose, OutPose);

	const FQuat WorldYawQuat = FRotator(0.f, Node->RotationOffset.Yaw, 0.f).Quaternion();
	const FQuat BaseQuat = Node->Rotation.Quaternion();
	const FQuat LocalPitchQuat = FRotator(Node->RotationOffset.Pitch, 0.f, 0.f).Quaternion();
	const FQuat ExpectedQuat = (WorldYawQuat * BaseQuat * LocalPitchQuat).GetNormalized();

	UTEST_TRUE("RotationOffset yaw uses world Z before local pitch",
		OutPose.Rotation.RotateVector(FVector::ForwardVector).Equals(
			ExpectedQuat.RotateVector(FVector::ForwardVector), 0.01f));
	UTEST_TRUE("RotationOffset pitch uses local camera right axis",
		OutPose.Rotation.RotateVector(FVector::UpVector).Equals(
			ExpectedQuat.RotateVector(FVector::UpVector), 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSetRotationAppliesRotationOffsetToActorSourceTest,
	"System.Engine.ComposableCameraSystem.Nodes.SetRotation.AppliesRotationOffsetToActorSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSetRotationAppliesRotationOffsetToActorSourceTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AActor* ReferenceActor = SpawnSetRotationTestActor(World, FVector::ZeroVector);
	ReferenceActor->SetActorRotation(FRotator::ZeroRotator);

	UComposableCameraSetRotationNode* Node = NewObject<UComposableCameraSetRotationNode>();
	Node->RotationSource = EComposableCameraSetRotationSource::FromActor;
	Node->RotationActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->RotationActor = ReferenceActor;
	Node->RotationOffset = FRotator(0.f, 90.f, 0.f);
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose InPose;
	InPose.Rotation = FRotator::ZeroRotator;

	FComposableCameraPose OutPose = InPose;
	Node->TickNode(0.016f, InPose, OutPose);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("SetRotation applies RotationOffset to FromActor base rotation",
		OutPose.Rotation.Equals(FRotator(0.f, 90.f, 0.f), 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBeginPlaySetRotationFromTwoActorsWithOffsetTest,
	"System.Engine.ComposableCameraSystem.Nodes.SetRotation.BeginPlayFromTwoActorsWithOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBeginPlaySetRotationFromTwoActorsWithOffsetTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	FTransform CameraTransform;
	CameraTransform.SetLocation(FVector::ZeroVector);
	AComposableCameraCameraBase* Camera = World->SpawnActorDeferred<AComposableCameraCameraBase>(
		AComposableCameraCameraBase::StaticClass(), CameraTransform);
	Camera->FinishSpawning(CameraTransform);
	Camera->CameraPose.Rotation = FRotator::ZeroRotator;
	Camera->LastFrameCameraPose.Rotation = FRotator::ZeroRotator;

	AActor* FirstActor = SpawnSetRotationTestActor(World, FVector::ZeroVector);
	AActor* SecondActor = SpawnSetRotationTestActor(World, FVector::ForwardVector * 100.f);

	UComposableCameraBeginPlaySetRotationNode* Node = NewObject<UComposableCameraBeginPlaySetRotationNode>();
	Node->RotationSource = EComposableCameraSetRotationSource::FromTwoActors;
	Node->FirstActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->FirstActor = FirstActor;
	Node->SecondActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->SecondActor = SecondActor;
	Node->RotationOffset = FRotator(0.f, 45.f, 0.f);
	Node->Initialize(Camera, nullptr);
	Node->ExecuteBeginPlay();

	const FRotator ResultRotation = Camera->CameraPose.Rotation;
	const FRotator ResultLastFrameRotation = Camera->LastFrameCameraPose.Rotation;

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("BeginPlay SetRotation writes FromTwoActors with offset",
		ResultRotation.Equals(FRotator(0.f, 45.f, 0.f), 0.01f));
	UTEST_TRUE("BeginPlay SetRotation seeds last-frame rotation with offset",
		ResultLastFrameRotation.Equals(ResultRotation, 0.01f));

	return true;
}

#undef LOCTEXT_NAMESPACE
