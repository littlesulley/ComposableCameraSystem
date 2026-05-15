// Copyright Sulley. All rights reserved.

#include "Nodes/ComposableCameraPivotLookAheadNode.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "Components/SceneComponent.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#define LOCTEXT_NAMESPACE "ComposableCameraPivotLookAheadNodeTests"

namespace
{
	void AddVectorOutputSlot(FComposableCameraRuntimeDataBlock& RuntimeData, int32 NodeIndex, FName PinName, int32 Offset)
	{
		RuntimeData.Storage.SetNumZeroed(Offset + static_cast<int32>(sizeof(FVector)));
		RuntimeData.TotalSize = RuntimeData.Storage.Num();
		RuntimeData.SlotShapes.Add(Offset, {
			EComposableCameraPinType::Vector3D,
			static_cast<int32>(sizeof(FVector)),
			nullptr
		});
		RuntimeData.OutputPinOffsets.Add(FComposableCameraPinKey{NodeIndex, PinName}, Offset);
	}

	AActor* SpawnVelocityActor(UWorld* World, const FVector& Location, const FVector& Velocity)
	{
		AActor* Actor = World ? World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity) : nullptr;
		if (!Actor)
		{
			return nullptr;
		}

		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("VelocityRoot"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		Root->ComponentVelocity = Velocity;
		return Actor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPivotLookAheadUsesActorVelocityTest,
	"System.Engine.ComposableCameraSystem.Nodes.PivotLookAhead.UsesActorVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPivotLookAheadUsesActorVelocityTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AActor* VelocityActor = SpawnVelocityActor(World, FVector::ZeroVector, FVector(200.f, 0.f, 0.f));

	FComposableCameraRuntimeDataBlock RuntimeData;
	AddVectorOutputSlot(RuntimeData, 0, TEXT("PivotPosition"), 0);

	UComposableCameraPivotLookAheadNode* Node = NewObject<UComposableCameraPivotLookAheadNode>();
	Node->PivotPosition = FVector(10.f, 20.f, 30.f);
	Node->VelocityActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->VelocityActor = VelocityActor;
	Node->LookAheadTime = 0.5f;
	Node->VelocityDampingTime = 0.f;
	Node->SetRuntimeDataBlock(&RuntimeData, 0);
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose Pose;
	Node->TickNode(0.016f, Pose, Pose);

	const FVector Result = RuntimeData.ReadOutputPin<FVector>(0, TEXT("PivotPosition"));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("Pivot is projected by actor velocity",
		Result.Equals(FVector(110.f, 20.f, 30.f), KINDA_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPivotLookAheadFallsBackToPivotDeltaTest,
	"System.Engine.ComposableCameraSystem.Nodes.PivotLookAhead.FallsBackToPivotDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPivotLookAheadFallsBackToPivotDeltaTest::RunTest(const FString& Parameters)
{
	FComposableCameraRuntimeDataBlock RuntimeData;
	AddVectorOutputSlot(RuntimeData, 0, TEXT("PivotPosition"), 0);

	UComposableCameraPivotLookAheadNode* Node = NewObject<UComposableCameraPivotLookAheadNode>();
	Node->VelocityActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->VelocityActor = nullptr;
	Node->LookAheadTime = 0.5f;
	Node->VelocityDampingTime = 0.f;
	Node->SetRuntimeDataBlock(&RuntimeData, 0);
	Node->Initialize(nullptr, nullptr);

	FComposableCameraPose Pose;

	Node->PivotPosition = FVector::ZeroVector;
	Node->TickNode(0.1f, Pose, Pose);

	Node->PivotPosition = FVector(100.f, 0.f, 0.f);
	Node->TickNode(0.1f, Pose, Pose);

	const FVector Result = RuntimeData.ReadOutputPin<FVector>(0, TEXT("PivotPosition"));

	UTEST_TRUE("Pivot is projected by frame-to-frame pivot velocity",
		Result.Equals(FVector(600.f, 0.f, 0.f), KINDA_SMALL_NUMBER));

	return true;
}

#undef LOCTEXT_NAMESPACE
