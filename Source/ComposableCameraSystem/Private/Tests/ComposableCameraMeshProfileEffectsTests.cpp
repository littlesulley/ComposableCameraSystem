// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Actions/ComposableCameraMoveToAction.h"
#include "Core/ComposableCameraContextStack.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Core/ComposableCameraParameterBlock.h"
#include "DataAssets/ComposableCameraActionTypeAsset.h"
#include "DataAssets/ComposableCameraMeshProfile.h"
#include "DataAssets/ComposableCameraModifierDataAsset.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "MeshCamera/ComposableCameraMeshWorldSubsystem.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "Patches/ComposableCameraPatchHandle.h"
#include "Patches/ComposableCameraPatchManager.h"
#include "Tests/ComposableCameraTestObjects.h"
#include "Utils/ComposableCameraProjectSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FMeshProfileTestWorld
	{
		// CreateWorld roots the world until DestroyWorld; its subsystem survives the GC test.
		TObjectPtr<UWorld> World = UWorld::CreateWorld(EWorldType::Game, false);
		FMeshProfileTestWorld()
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}
		~FMeshProfileTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshProfileMigrationTest,
	"ComposableCameraSystem.MeshCamera.ProfileTypeMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshProfileMigrationTest::RunTest(const FString&)
{
	UComposableCameraMeshProfile* Profile = NewObject<UComposableCameraMeshProfile>();
	UComposableCameraNodeModifierDataAsset* Modifier = NewObject<UComposableCameraNodeModifierDataAsset>();
	Profile->ModifierAssets.Add(Modifier);
	Profile->MigrateLegacyProfile();
	TestTrue(TEXT("Modifier-only legacy Profile selects Modifier"), Profile->Type == EComposableCameraMeshProfileType::Modifier);
	TestFalse(TEXT("Unambiguous legacy Profile is ready"), Profile->NeedsTypeSelection());
	UComposableCameraTypeAsset* Camera = NewObject<UComposableCameraTypeAsset>();
	Profile->Camera.CameraType = Camera;
	Profile->MigrateLegacyProfile();
	TestTrue(TEXT("Mixed legacy Profile requires explicit selection"), Profile->NeedsTypeSelection());
	TestTrue(TEXT("Migration retains both configurations"), Profile->Camera.CameraType.Get() == Camera && Profile->ModifierAssets[0] == Modifier);
	Profile->Type = EComposableCameraMeshProfileType::Modifier;
	Profile->ConfirmTypeSelection();
	TestFalse(TEXT("Confirmation enables the selected family"), Profile->NeedsTypeSelection());
	TestTrue(TEXT("Confirmation preserves inactive Camera configuration"), Profile->Camera.CameraType.Get() == Camera);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshActionParametersTest,
	"ComposableCameraSystem.MeshCamera.ActionProfileParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshActionParametersTest::RunTest(const FString&)
{
	FMeshProfileTestWorld TestWorld;
	AComposableCameraMeshBindingTestController* PC = TestWorld.World->SpawnActor<AComposableCameraMeshBindingTestController>();
	AComposableCameraPlayerCameraManager* PCM = TestWorld.World->SpawnActor<AComposableCameraPlayerCameraManager>();
	if (!TestTrue(TEXT("Binding test player exists"), PC && PCM)) { return false; }
	UComposableCameraActionTypeAsset* Asset = NewObject<UComposableCameraActionTypeAsset>();
	UComposableCameraMoveToAction* Template = NewObject<UComposableCameraMoveToAction>(Asset);
	Asset->ActionTemplate = Template;
	Template->MoveSpeed = 12.f;
	FComposableCameraMeshActionConfig Config;
	Config.Parameters.Values.Add(TEXT("TargetPosition"), TEXT("(X=42,Y=8,Z=3)"));
	Config.Parameters.Values.Add(TEXT("Duration"), TEXT("99")); // Base lifecycle is not a K2-exposed input.
	FComposableCameraParameterBlock Block;
	Config.BuildParameterBlock(*Asset, PC, PCM, nullptr, Block);
	UComposableCameraMoveToAction* Instance = Cast<UComposableCameraMoveToAction>(Asset->CreateAction(PCM, Block));
	if (!TestNotNull(TEXT("Configured Action instance exists"), Instance)) { return false; }
	TestTrue(TEXT("Typed vector override reaches the instance"), Instance->TargetPosition.Equals(FVector(42, 8, 3)));
	TestEqual(TEXT("Unspecified property keeps template default"), Instance->MoveSpeed, 12.f);
	TestEqual(TEXT("Base lifecycle is not overridden"), Instance->Duration, Template->Duration);
	TestFalse(TEXT("Profile override does not mutate shared template"), Template->TargetPosition.Equals(Instance->TargetPosition));

	UComposableCameraActionAssetTestAction* BindingTemplate = NewObject<UComposableCameraActionAssetTestAction>(Asset);
	BindingTemplate->TargetActor = PCM;
	Asset->ActionTemplate = BindingTemplate;
	Config.Bindings.FindOrAdd(TEXT("TargetActor")).Source = EComposableCameraMeshParameterSource::PlayerController;
	FComposableCameraMeshParameterBinding& Callback = Config.Bindings.FindOrAdd(TEXT("Callback"));
	Callback.Source = EComposableCameraMeshParameterSource::PlayerController;
	Callback.FunctionName = GET_FUNCTION_NAME_CHECKED(AComposableCameraMeshBindingTestController, NotifyMeshAction);
	Config.BuildParameterBlock(*Asset, PC, PCM, nullptr, Block);
	UComposableCameraActionAssetTestAction* BindingInstance = Cast<UComposableCameraActionAssetTestAction>(Asset->CreateAction(PCM, Block));
	if (!TestNotNull(TEXT("Runtime-binding Action exists"), BindingInstance)) { return false; }
	TestTrue(TEXT("Actor source resolves for this player"), BindingInstance->TargetActor.Get() == PC);
	BindingInstance->Callback.ExecuteIfBound();
	TestEqual(TEXT("Signature-compatible Delegate is bound"), PC->CallbackCount, 1);
	TestFalse(TEXT("Asset template Delegate remains unbound"), BindingTemplate->Callback.IsBound());
	TestTrue(TEXT("Actor binding does not change template default"), BindingTemplate->TargetActor.Get() == PCM);
	Config.Bindings.FindOrAdd(TEXT("TargetActor")).Source = EComposableCameraMeshParameterSource::None;
	Callback.Source = EComposableCameraMeshParameterSource::None;
	Config.BuildParameterBlock(*Asset, PC, PCM, nullptr, Block);
	BindingInstance = Cast<UComposableCameraActionAssetTestAction>(Asset->CreateAction(PCM, Block));
	TestTrue(TEXT("Explicit None clears the Actor and Delegate"), BindingInstance && !BindingInstance->TargetActor && !BindingInstance->Callback.IsBound());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshProfileDispatchTest,
	"ComposableCameraSystem.MeshCamera.ExclusiveProfileDispatchAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshProfileDispatchTest::RunTest(const FString&)
{
	TGuardValue<TArray<FName>> Contexts(GetMutableDefault<UComposableCameraProjectSettings>()->ContextNames,
		TArray<FName>{TEXT("MeshProfileTest"), TEXT("DifferentContext")});
	FMeshProfileTestWorld TestWorld;
	APlayerController* PC = TestWorld.World->SpawnActor<APlayerController>();
	AComposableCameraPlayerCameraManager* PCM = TestWorld.World->SpawnActor<AComposableCameraPlayerCameraManager>();
	if (!TestTrue(TEXT("Dispatch test player exists"), PC && PCM)) { return false; }
	AComposableCameraCameraBase* Gameplay = PCM->ActivateNewCamera(AComposableCameraCameraBase::StaticClass(),
		static_cast<UComposableCameraTransitionDataAsset*>(nullptr), FComposableCameraActivateParams{},
		FOnCameraFinishConstructed{}, TEXT("MeshProfileTest"));
	UComposableCameraMeshWorldSubsystem* Subsystem = TestWorld.World->GetSubsystem<UComposableCameraMeshWorldSubsystem>();
	if (!TestTrue(TEXT("Test setup succeeds"), PC && PCM && Gameplay && Subsystem)) { return false; }
	TestEqual(TEXT("Named activation establishes the base Context"), PCM->GetActiveContextName(), FName(TEXT("MeshProfileTest")));
	TStrongObjectPtr<UComposableCameraMeshProfile> Profile(NewObject<UComposableCameraMeshProfile>());
	UComposableCameraActionTypeAsset* Action = NewObject<UComposableCameraActionTypeAsset>(Profile.Get());
	Action->ActionTemplate = NewObject<UComposableCameraActionBase>(Action);
	Action->ActionTemplate->ExpirationType = static_cast<uint8>(EComposableCameraActionExpirationType::Manual);
	Profile->Action.ActionAsset = Action;
	UComposableCameraPatchTypeAsset* Patch = NewObject<UComposableCameraPatchTypeAsset>(Profile.Get());
	Profile->Patch.PatchAsset = Patch;
	Profile->Patch.ActivationParams.bOverrideExpirationType = true;
	Profile->Patch.ActivationParams.ExpirationType = static_cast<uint8>(EComposableCameraPatchExpirationType::Manual);
	Profile->Patch.ActivationParams.bOverrideEnterDuration = true;
	Profile->Patch.ActivationParams.EnterDuration = 0.f;
	Profile->Patch.ActivationParams.bOverrideExitDuration = true;
	Profile->Patch.ActivationParams.ExitDuration = 0.1f;
	Profile->ModifierAssets.Add(NewObject<UComposableCameraNodeModifierDataAsset>(Profile.Get()));
	UComposableCameraTypeAsset* Camera = NewObject<UComposableCameraTypeAsset>(Profile.Get());
	Camera->NodeTemplates.Add(NewObject<UComposableCameraModifierTestNode>(Camera));
	Profile->Camera.CameraType = Camera;
	AComposableCameraMeshSurfaceStorageActor* Storage = TestWorld.World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
	FComposableCameraMeshLayerQueryResult Layer;
	Layer.StorageActor = Storage;
	Layer.LayerId = FGuid::NewGuid();
	Layer.Profile = Profile.Get();
	Subsystem->PlayerStates.AddDefaulted();
	UComposableCameraMeshWorldSubsystem::FPlayerLayerState& State = Subsystem->PlayerStates.Last();
	State.PlayerController = PC;
	State.CameraManager = PCM;
	State.LastSeenFrame = MAX_uint64;

	for (EComposableCameraMeshProfileType Type : {EComposableCameraMeshProfileType::CameraType,
		EComposableCameraMeshProfileType::Modifier, EComposableCameraMeshProfileType::Action, EComposableCameraMeshProfileType::Patch})
	{
		Profile->Type = Type;
		Subsystem->EnterLayer(State, PCM, Layer);
		UComposableCameraMeshWorldSubsystem::FActiveLayerState& Active = State.ActiveLayers.Last();
		TestEqual(TEXT("Only Camera type creates a Context"), !Active.OwnedCameraContextName.IsNone(), Type == EComposableCameraMeshProfileType::CameraType);
		TestEqual(TEXT("Only Modifier type installs candidates"), !Active.ModifierInstances.IsEmpty(), Type == EComposableCameraMeshProfileType::Modifier);
		TestEqual(TEXT("Only Action type registers an Action"), Active.ActionInstance.IsValid(), Type == EComposableCameraMeshProfileType::Action);
		TestEqual(TEXT("Only Patch type registers a Patch"), Active.PatchHandle != nullptr, Type == EComposableCameraMeshProfileType::Patch);
		const FName ContextBeforeUpdate = Active.OwnedCameraContextName;
		const TWeakObjectPtr<UComposableCameraActionBase> ActionBeforeUpdate = Active.ActionInstance;
		const TWeakObjectPtr<UComposableCameraPatchHandle> PatchBeforeUpdate = Active.PatchHandle.Get();
		Subsystem->UpdatePlayerLayers(State, PC, PCM, MakeArrayView(&Layer, 1));
		TestEqual(TEXT("Unchanged membership keeps one active Layer"), State.ActiveLayers.Num(), 1);
		TestEqual(TEXT("Unchanged membership preserves Context"), Active.OwnedCameraContextName, ContextBeforeUpdate);
		TestTrue(TEXT("Unchanged membership preserves Action instance"), Active.ActionInstance == ActionBeforeUpdate);
		TestTrue(TEXT("Unchanged membership preserves Patch instance"), Active.PatchHandle.Get() == PatchBeforeUpdate.Get());
		TWeakObjectPtr<UComposableCameraPatchHandle> Handle = Active.PatchHandle.Get();
		UComposableCameraPatchManager* Manager = Active.PatchManager.Get();
		UComposableCameraActionBase* OwnedAction = Active.ActionInstance.Get();
		UComposableCameraActionBase* OtherAction = nullptr;
		TStrongObjectPtr<UComposableCameraPatchHandle> OtherPatch;
		if (OwnedAction)
		{
			OtherAction = PCM->AddCameraActionFromAsset(Action, FComposableCameraParameterBlock{}, false);
		}
		if (Handle.IsValid())
		{
			// The world subsystem's manual GC hook must retain this weakly-owned Patch handle.
			CollectGarbage(RF_NoFlags);
			TestTrue(TEXT("Layer-owned Patch handle survives GC"), Handle.IsValid());
			OtherPatch.Reset(Manager->AddPatch(Patch, Profile->Patch.ActivationParams, FComposableCameraParameterBlock{}));
			(void)Manager->Apply(0.f, FComposableCameraPose{});
			AComposableCameraCameraBase* OtherContextCamera = PCM->ActivateNewCamera(AComposableCameraCameraBase::StaticClass(),
				static_cast<UComposableCameraTransitionDataAsset*>(nullptr), FComposableCameraActivateParams{},
				FOnCameraFinishConstructed{}, TEXT("DifferentContext"));
			TestNotNull(TEXT("Named activation creates the second Context camera"), OtherContextCamera);
			TestEqual(TEXT("Patch cleanup runs after an actual Context switch"), PCM->GetActiveContextName(), FName(TEXT("DifferentContext")));
		}
		Subsystem->ExitLayer(Active, PCM);
		State.ActiveLayers.Reset();
		if (OwnedAction)
		{
			TestFalse(TEXT("Exit removes exact Layer Action"), PCM->GetCameraActions().Contains(OwnedAction));
			TestTrue(TEXT("Exit preserves same-class external Action"), PCM->GetCameraActions().Contains(OtherAction));
			PCM->RemoveCameraAction(OtherAction);
			Subsystem->EnterLayer(State, PCM, Layer);
			PCM->RemoveCameraAction(State.ActiveLayers.Last().ActionInstance.Get());
			Subsystem->UpdatePlayerLayers(State, PC, PCM, MakeArrayView(&Layer, 1));
			TestEqual(TEXT("Removed Action does not restart within unchanged Layer"), PCM->GetCameraActions().Num(), 0);
			Subsystem->ExitLayer(State.ActiveLayers.Last(), PCM);
			State.ActiveLayers.Reset();
		}
		if (Handle.IsValid())
		{
			TestTrue(TEXT("Exit expires Patch on original Director after Context switch"), Handle->GetPhase() == EComposableCameraPatchPhase::Exiting);
			(void)Manager->Apply(0.2f, FComposableCameraPose{});
			TestEqual(TEXT("Patch exit preserves external instance of same asset"), Manager->GetActivePatchCount(), 1);
			Manager->ExpirePatch(OtherPatch.Get());
			(void)Manager->Apply(0.2f, FComposableCameraPose{});
			TestEqual(TEXT("Patch cleanup completes after exit envelope"), Manager->GetActivePatchCount(), 0);
			PCM->PopCameraContext(TEXT("DifferentContext"));
			TestTrue(TEXT("Popping the second Context resumes gameplay"), PCM->GetRunningCamera() == Gameplay);
		}
		if (Type == EComposableCameraMeshProfileType::CameraType)
		{
			TestTrue(TEXT("Camera exit restores original gameplay instance"), PCM->GetRunningCamera() == Gameplay);
		}
	}
	Subsystem->PlayerStates.Reset();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshProfileSerializationTest,
	"ComposableCameraSystem.MeshCamera.ProfileSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshProfileSerializationTest::RunTest(const FString&)
{
	UComposableCameraMeshProfile* Source = NewObject<UComposableCameraMeshProfile>();
	Source->Type = EComposableCameraMeshProfileType::Action;
	Source->Camera.Parameters.Values.Add(TEXT("CameraDistance"), TEXT("420"));
	Source->Action.Parameters.Values.Add(TEXT("MoveSpeed"), TEXT("12"));
	Source->Action.Bindings.FindOrAdd(TEXT("TargetActor")).Source = EComposableCameraMeshParameterSource::PlayerController;
	Source->Patch.Parameters.Values.Add(TEXT("PatchStrength"), TEXT("0.5"));
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	FObjectAndNameAsStringProxyArchive Save(Writer, false);
	Source->Serialize(Save);
	UComposableCameraMeshProfile* Restored = NewObject<UComposableCameraMeshProfile>();
	FMemoryReader Reader(Bytes);
	FObjectAndNameAsStringProxyArchive Load(Reader, false);
	Load.SetCustomVersions(Save.GetCustomVersions());
	Restored->Serialize(Load);
	TestTrue(TEXT("Selected family survives serialization"), Restored->Type == Source->Type);
	TestEqual(TEXT("Camera overrides survive while inactive"), Restored->Camera.Parameters.Values.FindRef(TEXT("CameraDistance")), FString(TEXT("420")));
	TestEqual(TEXT("Action overrides survive serialization"), Restored->Action.Parameters.Values.FindRef(TEXT("MoveSpeed")), FString(TEXT("12")));
	TestTrue(TEXT("Actor binding source survives serialization"), Restored->Action.Bindings.FindRef(TEXT("TargetActor")).Source == EComposableCameraMeshParameterSource::PlayerController);
	TestEqual(TEXT("Patch overrides survive while inactive"), Restored->Patch.Parameters.Values.FindRef(TEXT("PatchStrength")), FString(TEXT("0.5")));
	return true;
}

#endif
