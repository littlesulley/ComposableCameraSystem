// Copyright 2026 Sulley. All Rights Reserved.

#include "Cameras/ComposableCameraCameraBase.h"
#include "DataAssets/ComposableCameraPatchTypeAsset.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Patches/ComposableCameraPatchHandle.h"
#include "Patches/ComposableCameraPatchInstance.h"
#include "Patches/ComposableCameraPatchManager.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FComposableCameraPatchExpireAllWithoutHandleTest,
	"ComposableCameraSystem.Patches.ExpireAllWithoutRetainedHandle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraPatchExpireAllWithoutHandleTest::RunTest(const FString& /*Parameters*/)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AComposableCameraCameraBase* Owner = World->SpawnActor<AComposableCameraCameraBase>(
		AComposableCameraCameraBase::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("Patch test owner exists"), Owner))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	TStrongObjectPtr<UComposableCameraPatchManager> Manager(
		NewObject<UComposableCameraPatchManager>(Owner));
	const bool bResult = [this, &Manager]()
	{
		UComposableCameraPatchTypeAsset* Asset = NewObject<UComposableCameraPatchTypeAsset>();
		FComposableCameraPatchActivateParams Params;
		Params.bOverrideExpirationType = true;
		Params.ExpirationType = static_cast<uint8>(EComposableCameraPatchExpirationType::Manual);
		Params.bOverrideEnterDuration = true;
		Params.EnterDuration = 0.f;
		Params.bOverrideExitDuration = true;
		Params.ExitDuration = 0.2f;

		TStrongObjectPtr<UComposableCameraPatchHandle> RetainedHandle(
			Manager->AddPatch(Asset, Params, FComposableCameraParameterBlock{}));
		TWeakObjectPtr<UComposableCameraPatchHandle> ReleasedHandle(
			Manager->AddPatch(Asset, Params, FComposableCameraParameterBlock{}));
		if (!TestTrue(TEXT("Both patches were added"),
			RetainedHandle.IsValid() && ReleasedHandle.IsValid()))
		{
			return false;
		}

		TWeakObjectPtr<UComposableCameraPatchInstance> ReleasedInstance = ReleasedHandle->GetInstance();
		TWeakObjectPtr<AComposableCameraCameraBase> ReleasedEvaluator = ReleasedInstance->Evaluator.Get();
		(void)Manager->Apply(0.016f, FComposableCameraPose{});

		// Only the manager and the first handle have strong owners. The second
		// instance must keep running after its caller discards the opaque handle.
		CollectGarbage(RF_NoFlags);
		TestFalse(TEXT("Unretained handle was collected"), ReleasedHandle.IsValid());
		if (!TestTrue(TEXT("Manager still owns the handle-free patch"), ReleasedInstance.IsValid()))
		{
			return false;
		}
		TestEqual(TEXT("Both patches remain registered before bulk expiration"),
			Manager->GetActivePatchCount(), 2);

		Manager->ExpireAll(0.4f);
		TestTrue(TEXT("Retained-handle patch starts fading out"),
			RetainedHandle->GetPhase() == EComposableCameraPatchPhase::Exiting);
		TestTrue(TEXT("Handle-free patch also starts fading out"),
			ReleasedInstance->Phase == EComposableCameraPatchPhase::Exiting);
		TestEqual(TEXT("Handle-free patch receives the duration override"), ReleasedInstance->ExitDuration, 0.4f);
		(void)Manager->Apply(0.2f, FComposableCameraPose{});
		TestEqual(TEXT("Both patches survive the first half of the exit"), Manager->GetActivePatchCount(), 2);
		Manager->ExpireAll(0.f);
		TestEqual(TEXT("Repeated bulk expiration preserves the running exit duration"), ReleasedInstance->ExitDuration, 0.4f);
		TestEqual(TEXT("Repeated bulk expiration does not reset the exit clock"), ReleasedInstance->ElapsedInPhase, 0.2f);
		(void)Manager->Apply(0.25f, FComposableCameraPose{});
		TestEqual(TEXT("Bulk expiration removes both patches after the exit envelope"),
			Manager->GetActivePatchCount(), 0);
		TestFalse(TEXT("Handle-free patch evaluator is destroyed"), ReleasedEvaluator.IsValid());

		Params.EnterDuration = 0.4f;
		TStrongObjectPtr<UComposableCameraPatchHandle> EnteringHandle(
			Manager->AddPatch(Asset, Params, FComposableCameraParameterBlock{}));
		if (!TestTrue(TEXT("Entering patch exists"), EnteringHandle.IsValid())) return false;
		(void)Manager->Apply(0.1f, FComposableCameraPose{});
		UComposableCameraPatchInstance* EnteringInstance = EnteringHandle->GetInstance();
		const float EnterAlpha = EnteringInstance->CurrentAlpha;
		Manager->ExpireAll();
		TestTrue(TEXT("Partially entered patch starts its exit"), EnteringInstance->Phase == EComposableCameraPatchPhase::Exiting);
		TestEqual(TEXT("Exit starts at the partial enter alpha"), EnteringInstance->ExitStartAlpha, EnterAlpha);
		(void)Manager->Apply(0.25f, FComposableCameraPose{});
		TestEqual(TEXT("Partially entered patch is removed after its exit"), Manager->GetActivePatchCount(), 0);

		Params.EnterDuration = 0.f;
		TStrongObjectPtr<UComposableCameraPatchHandle> ImmediateHandle(
			Manager->AddPatch(Asset, Params, FComposableCameraParameterBlock{}));
		if (!TestTrue(TEXT("Immediate-cut patch exists"), ImmediateHandle.IsValid())) return false;
		(void)Manager->Apply(0.f, FComposableCameraPose{});
		Manager->ExpireAll(0.f);
		TestTrue(TEXT("Zero exit override expires immediately"), ImmediateHandle->GetPhase() == EComposableCameraPatchPhase::Expired);
		(void)Manager->Apply(0.f, FComposableCameraPose{});
		TestEqual(TEXT("Immediate cut is swept normally"), Manager->GetActivePatchCount(), 0);
		return true;
	}();

	// Also tear down the surviving patch when running against the known bug.
	Manager->DestroyAll();
	Manager.Reset();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return bResult;
}

#endif
