// Copyright 2026 Sulley. All Rights Reserved.

#include "Actions/ComposableCameraActionBase.h"
#include "Actions/ComposableCameraMoveToAction.h"
#include "Cameras/ComposableCameraCameraBase.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "DataAssets/ComposableCameraActionTypeAsset.h"
#include "Tests/ComposableCameraTestObjects.h"
#include "Utils/ComposableCameraBlueprintLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraActionNonPositiveDurationTest,
	"System.Engine.ComposableCameraSystem.Actions.NonPositiveDurationRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraActionNonPositiveDurationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AComposableCameraPlayerCameraManager* PCM =
		World->SpawnActor<AComposableCameraPlayerCameraManager>(
			AComposableCameraPlayerCameraManager::StaticClass(), FTransform::Identity);
	if (!PCM)
	{
		AddError(TEXT("Action duration test setup failed."));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	const UComposableCameraTestZeroDurationAction* ZeroFixture =
		NewObject<UComposableCameraTestZeroDurationAction>(PCM);
	const UComposableCameraTestNegativeDurationAction* NegativeFixture =
		NewObject<UComposableCameraTestNegativeDurationAction>(PCM);
	TestTrue(TEXT("Zero-duration fixture has authored defaults"),
		ZeroFixture && ZeroFixture->Duration == 0.f
			&& (ZeroFixture->ExpirationType
				& static_cast<uint8>(EComposableCameraActionExpirationType::Duration)) != 0);
	TestTrue(TEXT("Negative-duration fixture has authored defaults"),
		NegativeFixture && NegativeFixture->Duration == -1.f
			&& (NegativeFixture->ExpirationType
				& static_cast<uint8>(EComposableCameraActionExpirationType::Duration)) != 0);

	TestTrue(TEXT("Zero-duration action is not added"),
		PCM->AddCameraAction(UComposableCameraTestZeroDurationAction::StaticClass(), false) == nullptr);
	TestTrue(TEXT("Negative-duration action is not added"),
		PCM->AddCameraAction(UComposableCameraTestNegativeDurationAction::StaticClass(), false) == nullptr);
	TestTrue(TEXT("Rejected actions are absent from PCM"), PCM->GetCameraActions().IsEmpty());
	TestNotNull(TEXT("Positive-duration action is added"),
		PCM->AddCameraAction(UComposableCameraActionBase::StaticClass(), false));
	TestNotNull(TEXT("Zero duration is allowed without Duration expiration"),
		PCM->AddCameraAction(UComposableCameraTestConditionOnlyZeroDurationAction::StaticClass(), false));

	UComposableCameraActionBase* RuntimeAction = NewObject<UComposableCameraActionBase>(PCM);
	RuntimeAction->ExpirationType = static_cast<uint8>(EComposableCameraActionExpirationType::Duration);
	RuntimeAction->Duration = 0.f;
	TestFalse(TEXT("Invalid runtime duration cannot execute"),
		RuntimeAction->OnCanExecute(0.016f, FComposableCameraPose{}));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraActionLocalPoseConditionTest,
	"System.Engine.ComposableCameraSystem.Actions.ConditionUsesLocalPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraActionLocalPoseConditionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AComposableCameraPlayerCameraManager* PCM =
		World->SpawnActor<AComposableCameraPlayerCameraManager>(
			AComposableCameraPlayerCameraManager::StaticClass(), FTransform::Identity);
	AComposableCameraCameraBase* Camera =
		World->SpawnActor<AComposableCameraCameraBase>(
			AComposableCameraCameraBase::StaticClass(), FTransform::Identity);
	if (!PCM || !Camera)
	{
		AddError(TEXT("Action local-pose test setup failed."));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	PCM->RunningCamera = Camera;
	PCM->CurrentCameraPose.Position = FVector::ZeroVector;
	Camera->CameraPose.Position = FVector(50.f, 0.f, 0.f);
	UComposableCameraMoveToAction* Action = Cast<UComposableCameraMoveToAction>(
		PCM->AddCameraAction(UComposableCameraMoveToAction::StaticClass(), false));
	if (!Action)
	{
		AddError(TEXT("MoveTo action was not added."));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Action->TargetPosition = FVector::ZeroVector;
	Action->MoveSpeed = 30.f;
	AComposableCameraCameraBase* SourceCamera =
		World->SpawnActor<AComposableCameraCameraBase>(
			AComposableCameraCameraBase::StaticClass(), FTransform::Identity);
	if (!SourceCamera)
	{
		AddError(TEXT("Source camera was not spawned."));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	SourceCamera->Initialize(PCM); // Persistent Action binds to both cameras.
	SourceCamera->CameraPose.Position = FVector::ZeroVector;

	const float DeltaTime = 1.f / 60.f;
	TestTrue(TEXT("Blended PCM pose at target does not expire action"),
		Action->OnCanExecute(DeltaTime, PCM->CurrentCameraPose));
	(void)SourceCamera->TickCamera(DeltaTime);
	TestTrue(TEXT("Source camera at target cannot expire running camera action"),
		PCM->GetCameraActions().Contains(Action));
	const FComposableCameraPose LocalOutput = Camera->TickCamera(DeltaTime);
	TestTrue(TEXT("MoveTo runs using local camera pose"),
		LocalOutput.Position.X > 0.f && LocalOutput.Position.X < 50.f);
	TestTrue(TEXT("Action remains registered while local camera is short of target"),
		PCM->GetCameraActions().Contains(Action));

	TestTrue(TEXT("Action advances into next PCM update"),
		Action->OnCanExecute(DeltaTime, PCM->CurrentCameraPose));
	Camera->CameraPose.Position = FVector::ZeroVector;
	Camera->InvalidateTickCache();
	(void)Camera->TickCamera(DeltaTime);
	TestFalse(TEXT("Action expires when local camera pose reaches target"),
		PCM->GetCameraActions().Contains(Action));
	SourceCamera->CameraPose.Position = FVector(100.f, 0.f, 0.f);
	SourceCamera->InvalidateTickCache();
	const FComposableCameraPose SourceAfterRemoval = SourceCamera->TickCamera(DeltaTime);
	TestTrue(TEXT("Removed action does not execute on transition source"),
		SourceAfterRemoval.Position.Equals(FVector(100.f, 0.f, 0.f)));

	PCM->RunningCamera = SourceCamera;
	UComposableCameraMoveToAction* CurrentCameraOnlyAction =
		Cast<UComposableCameraMoveToAction>(PCM->AddCameraAction(
			UComposableCameraMoveToAction::StaticClass(), true));
	if (!CurrentCameraOnlyAction)
	{
		AddError(TEXT("Current-camera-only MoveTo action was not added."));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	CurrentCameraOnlyAction->TargetPosition = FVector::ZeroVector;
	SourceCamera->CameraPose.Position = FVector::ZeroVector;
	PCM->RunningCamera = Camera; // Former running camera remains a blend source.
	TestTrue(TEXT("Current-camera-only action advances into PCM update"),
		CurrentCameraOnlyAction->OnCanExecute(DeltaTime, PCM->CurrentCameraPose));
	SourceCamera->InvalidateTickCache();
	(void)SourceCamera->TickCamera(DeltaTime);
	TestFalse(TEXT("Current-camera-only action expires on its bound source camera"),
		PCM->GetCameraActions().Contains(CurrentCameraOnlyAction));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraActionAssetParametersTest,
	"System.Engine.ComposableCameraSystem.Actions.AssetParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraActionAssetParametersTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	AComposableCameraPlayerCameraManager* PCM =
		World->SpawnActor<AComposableCameraPlayerCameraManager>();
	AActor* Target = World->SpawnActor<AActor>();
	AActor* DefaultTarget = World->SpawnActor<AActor>();
	if (!PCM || !Target || !DefaultTarget)
	{
		AddError(TEXT("Action asset parameter test setup failed."));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	UComposableCameraActionTypeAsset* ActorAsset =
		NewObject<UComposableCameraActionTypeAsset>(GetTransientPackage());
	UComposableCameraActionAssetTestAction* ActorTemplate =
		NewObject<UComposableCameraActionAssetTestAction>(ActorAsset);
	ActorAsset->ActionTemplate = ActorTemplate;
	ActorTemplate->TargetActor = DefaultTarget;
	FComposableCameraParameterBlock ActorParameters;
	ActorParameters.SetActor(TEXT("TargetActor"), Target);
	UComposableCameraActionAssetTestAction* ActorInstance =
		Cast<UComposableCameraActionAssetTestAction>(
			PCM->AddCameraActionFromAsset(ActorAsset, ActorParameters, true));
	TestNotNull(TEXT("Actor Action created from template"), ActorInstance);
	if (ActorInstance)
	{
		TestTrue(TEXT("Live Actor input reaches Action instance"),
			ActorInstance->TargetActor.Get() == Target);
		TestTrue(TEXT("Template is not the registered Action"),
			ActorInstance != ActorTemplate);
		TestTrue(TEXT("Caller scope is applied"), ActorInstance->bOnlyForCurrentCamera);
	}
	TestTrue(TEXT("Template retains its default Actor"),
		ActorTemplate->TargetActor.Get() == DefaultTarget);
	FComposableCameraParameterBlock WrongTypeParameters;
	WrongTypeParameters.SetFloat(TEXT("TargetActor"), 1.f);
	UComposableCameraActionAssetTestAction* WrongTypeInstance =
		Cast<UComposableCameraActionAssetTestAction>(
			ActorAsset->CreateAction(PCM, WrongTypeParameters));
	TestTrue(TEXT("Wrong parameter type leaves template default intact"),
		WrongTypeInstance && WrongTypeInstance->TargetActor.Get() == DefaultTarget);

	UComposableCameraActionTypeAsset* MoveAsset =
		NewObject<UComposableCameraActionTypeAsset>(GetTransientPackage());
	UComposableCameraMoveToAction* MoveTemplate =
		NewObject<UComposableCameraMoveToAction>(MoveAsset);
	MoveAsset->ActionTemplate = MoveTemplate;
	MoveTemplate->TargetPosition = FVector(25.f, 0.f, 0.f);
	MoveTemplate->MoveSpeed = 2.f;
	FComposableCameraParameterBlock MoveParameters;
	MoveParameters.SetVector(TEXT("TargetPosition"), FVector(100.f, 0.f, 0.f));
	UComposableCameraMoveToAction* FirstMove =
		Cast<UComposableCameraMoveToAction>(
			PCM->AddCameraActionFromAsset(MoveAsset, MoveParameters, false));
	UComposableCameraMoveToAction* SecondMove =
		Cast<UComposableCameraMoveToAction>(
			PCM->AddCameraActionFromAsset(MoveAsset, FComposableCameraParameterBlock{}, false));
	TestNotNull(TEXT("First MoveTo created"), FirstMove);
	TestNotNull(TEXT("Second MoveTo created"), SecondMove);
	if (FirstMove && SecondMove)
	{
		TestTrue(TEXT("Caller value overrides first instance"),
			FirstMove->TargetPosition.Equals(FVector(100.f, 0.f, 0.f)));
		TestTrue(TEXT("Unprovided value keeps second instance default"),
			SecondMove->TargetPosition.Equals(FVector(25.f, 0.f, 0.f)));
		TestEqual(TEXT("Template speed survives without override"),
			FirstMove->MoveSpeed, 2.f);
		TestTrue(TEXT("Each activation gets independent state"), FirstMove != SecondMove);
	}
	TestTrue(TEXT("Asset template stays unchanged"),
		MoveTemplate->TargetPosition.Equals(FVector(25.f, 0.f, 0.f)));
	MoveTemplate->ExpirationType =
		static_cast<uint8>(EComposableCameraActionExpirationType::Duration);
	MoveTemplate->Duration = 0.f;
	TestNull(TEXT("Asset path also rejects invalid Duration"),
		PCM->AddCameraActionFromAsset(MoveAsset, FComposableCameraParameterBlock{}, false));
	UComposableCameraBlueprintLibrary::RemoveActionInstance(SecondMove);
	TestTrue(TEXT("Removing one same-class Action keeps the other"),
		FirstMove && PCM->GetCameraActions().Contains(FirstMove));
	TestFalse(TEXT("Removed Action handle leaves PCM"),
		SecondMove && PCM->GetCameraActions().Contains(SecondMove));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}
