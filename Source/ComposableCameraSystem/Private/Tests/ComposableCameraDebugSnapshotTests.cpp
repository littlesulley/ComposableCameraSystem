// Copyright 2026 Sulley. All Rights Reserved.

#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraDebugSnapshot.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "Core/ComposableCameraTypeAssetInstantiator.h"
#include "DataAssets/ComposableCameraTypeAsset.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Nodes/ComposableCameraFieldOfViewNode.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

namespace ComposableCameraDebugSnapshotTests
{
	struct FDebugSnapshotTestWorld
	{
		UWorld* World = nullptr;

		FDebugSnapshotTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FDebugSnapshotTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		AComposableCameraCameraBase* SpawnCamera() const
		{
			const FTransform Transform = FTransform::Identity;
			AComposableCameraCameraBase* Camera =
				World->SpawnActorDeferred<AComposableCameraCameraBase>(
					AComposableCameraCameraBase::StaticClass(), Transform);
			Camera->FinishSpawning(Transform);
			return Camera;
		}
	};

	const FComposableCameraNodeParameterDebugValue* FindParameter(
		const FComposableCameraNodeDebugEntry& Entry,
		FName ParameterName)
	{
		return Entry.ParameterValues.FindByPredicate(
			[ParameterName](const FComposableCameraNodeParameterDebugValue& Value)
			{
				return Value.ParameterName == ParameterName;
			});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraDebugSnapshotNodeParametersTest,
	"ComposableCameraSystem.Debug.Snapshot.NodeParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraDebugSnapshotNodeParametersTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ComposableCameraDebugSnapshotTests;

	FDebugSnapshotTestWorld TestWorld;
	UComposableCameraTypeAsset* TypeAsset =
		NewObject<UComposableCameraTypeAsset>(GetTransientPackage());
	UComposableCameraFieldOfViewNode* Template =
		NewObject<UComposableCameraFieldOfViewNode>(TypeAsset);
	Template->FieldOfView = 91.5f;
	Template->bDynamicFov = true;
	Template->MinFoV = 35.f;
	TypeAsset->NodeTemplates.Add(Template);

	AComposableCameraCameraBase* Camera = TestWorld.SpawnCamera();
	UE::ComposableCameras::ConstructCameraFromTypeAsset(
		Camera, TypeAsset, FComposableCameraParameterBlock());

	// Test worlds can start with GFrameCounter matching the camera's zero
	// sentinel. Force the first call down the real evaluation path.
	Camera->LastTickedFrameCounter = TNumericLimits<uint64>::Max();
	(void)Camera->TickCamera(1.f / 60.f);

	const FComposableCameraDebugSnapshot Snapshot = Camera->SnapshotDebugState();
	if (!TestTrue(TEXT("Snapshot is valid"), Snapshot.bIsValid)
		|| !TestEqual(TEXT("Snapshot has one node"), Snapshot.NodeEntries.Num(), 1))
	{
		return false;
	}

	const FComposableCameraNodeDebugEntry& Entry = Snapshot.NodeEntries[0];
	TestTrue(TEXT("Runtime node ticked"), Entry.bWasTicked);

	const FComposableCameraNodeParameterDebugValue* FieldOfView =
		FindParameter(Entry, GET_MEMBER_NAME_CHECKED(UComposableCameraFieldOfViewNode, FieldOfView));
	TestNotNull(TEXT("Declared FieldOfView parameter captured"), FieldOfView);
	if (FieldOfView)
	{
		TestTrue(TEXT("FieldOfView uses current runtime value"), FieldOfView->Value.Contains(TEXT("91.5")));
	}

	const FComposableCameraNodeParameterDebugValue* DynamicFov =
		FindParameter(Entry, GET_MEMBER_NAME_CHECKED(UComposableCameraFieldOfViewNode, bDynamicFov));
	TestNotNull(TEXT("Details-only declared parameter captured"), DynamicFov);
	if (DynamicFov)
	{
		TestTrue(TEXT("Boolean runtime value exported"), DynamicFov->Value.Equals(TEXT("True"), ESearchCase::IgnoreCase));
	}

	TestNotNull(TEXT("Non-pin editable array captured"),
		FindParameter(Entry, GET_MEMBER_NAME_CHECKED(UComposableCameraFieldOfViewNode, ActorsForDynamicFoV)));
	TestNull(TEXT("Node metadata excluded"), FindParameter(Entry, TEXT("PaletteCategory")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
