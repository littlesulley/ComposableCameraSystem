// Copyright 2026 Sulley. All Rights Reserved.

#include "Nodes/ComposableCameraComputePositionBetweenActorsNode.h"

#include "Components/SceneComponent.h"
#include "Core/ComposableCameraRuntimeDataBlock.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#define LOCTEXT_NAMESPACE "ComposableCameraComputePositionBetweenActorsNodeTests"

namespace
{
	void AddVectorOutputSlot(FComposableCameraRuntimeDataBlock& RuntimeData, int32 NodeIndex, FName PinName)
	{
		RuntimeData.Storage.SetNumZeroed(static_cast<int32>(sizeof(FVector)));
		RuntimeData.TotalSize = RuntimeData.Storage.Num();
		RuntimeData.SlotShapes.Add(0, {
			EComposableCameraPinType::Vector3D,
			static_cast<int32>(sizeof(FVector)),
			nullptr
		});
		RuntimeData.OutputPinOffsets.Add(FComposableCameraPinKey{NodeIndex, PinName}, 0);
	}

	AActor* SpawnActorWithRoot(UWorld* World, const FVector& Location)
	{
		AActor* Actor = World ? World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity) : nullptr;
		if (!Actor)
		{
			return nullptr;
		}

		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("TestRoot"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComputePositionBetweenActorsInterpolatesAndAddsHeightTest,
	"System.Engine.ComposableCameraSystem.Nodes.ComputePositionBetweenActors.InterpolatesAndAddsHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComputePositionBetweenActorsInterpolatesAndAddsHeightTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AActor* FirstActor = SpawnActorWithRoot(World, FVector(10.f, 20.f, 30.f));
	AActor* SecondActor = SpawnActorWithRoot(World, FVector(110.f, 220.f, 330.f));

	FComposableCameraRuntimeDataBlock RuntimeData;
	AddVectorOutputSlot(RuntimeData, 0, TEXT("Position"));

	UComposableCameraComputePositionBetweenActorsNode* Node =
		NewObject<UComposableCameraComputePositionBetweenActorsNode>();
	Node->FirstActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->FirstActor = FirstActor;
	Node->SecondActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->SecondActor = SecondActor;
	Node->Alpha = 0.25f;
	Node->HeightOffset = 50.f;
	Node->SetRuntimeDataBlock(&RuntimeData, 0);
	Node->Initialize(nullptr, nullptr);
	Node->ExecuteBeginPlay();

	const FVector Result = RuntimeData.ReadOutputPin<FVector>(0, TEXT("Position"));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("Position lerps from first actor to second actor then adds world Z height",
		Result.Equals(FVector(35.f, 70.f, 155.f), KINDA_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComputePositionBetweenActorsClampsAlphaTest,
	"System.Engine.ComposableCameraSystem.Nodes.ComputePositionBetweenActors.ClampsAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComputePositionBetweenActorsClampsAlphaTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AActor* FirstActor = SpawnActorWithRoot(World, FVector::ZeroVector);
	AActor* SecondActor = SpawnActorWithRoot(World, FVector(100.f, 0.f, 0.f));

	FComposableCameraRuntimeDataBlock RuntimeData;
	AddVectorOutputSlot(RuntimeData, 0, TEXT("Position"));

	UComposableCameraComputePositionBetweenActorsNode* Node =
		NewObject<UComposableCameraComputePositionBetweenActorsNode>();
	Node->FirstActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->FirstActor = FirstActor;
	Node->SecondActorSource = EComposableCameraActorInputSource::ExplicitActor;
	Node->SecondActor = SecondActor;
	Node->Alpha = 2.f;
	Node->HeightOffset = 10.f;
	Node->SetRuntimeDataBlock(&RuntimeData, 0);
	Node->Initialize(nullptr, nullptr);
	Node->ExecuteBeginPlay();

	const FVector Result = RuntimeData.ReadOutputPin<FVector>(0, TEXT("Position"));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	UTEST_TRUE("Alpha above one clamps to the second actor",
		Result.Equals(FVector(100.f, 0.f, 10.f), KINDA_SMALL_NUMBER));

	return true;
}

#undef LOCTEXT_NAMESPACE
