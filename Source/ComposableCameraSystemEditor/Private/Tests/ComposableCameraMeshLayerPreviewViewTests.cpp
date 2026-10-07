// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MeshCamera/ComposableCameraMeshLayerPreviewViewExtension.h"
#include "MeshCamera/ComposableCameraMeshLayerPIEPreview.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "SceneView.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshLayerPreviewLandscapeLODTest,
	"ComposableCameraSystem.Editor.MeshCamera.PreviewLandscapeLODStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerPreviewLandscapeLODTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	IConsoleVariable* Stabilize = IConsoleManager::Get().FindConsoleVariable(TEXT("CCS.Editor.MeshLayers.StabilizeLandscapeLOD"));
	IConsoleVariable* ForceLOD = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ForceLOD"));
	if (!TestNotNull(TEXT("Preview LOD control is registered"), Stabilize)
		|| !TestNotNull(TEXT("Engine is available"), GEngine)
		|| !TestNotNull(TEXT("Engine diagnostic LOD control exists"), ForceLOD)) { return false; }
	struct FRestoreStability
	{
		IConsoleVariable* Variable;
		int32 Value;
		~FRestoreStability() { Variable->SetWithCurrentPriority(Value); }
	} Restore{Stabilize, Stabilize->GetInt()};
	const int32 OriginalForceLOD = ForceLOD->GetInt();
	Stabilize->SetWithCurrentPriority(1);
	UWorld* EditorWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	UWorld* PIEWorld = UWorld::CreateWorld(EWorldType::PIE, false);
	UWorld* UnrelatedWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	UWorld* AssetWorld = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	UWorld* GameWorld = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::PIE).SetCurrentWorld(PIEWorld);
	const bool bResult = [&, this]()
	{
		TArray<FResolvedSurfaceLayerMesh> Source;
		Source.SetNum(1);
		Source[0].LocalVertices = {FVector(0, 0, VisualizationSurfaceOffset),
			FVector(10, 0, VisualizationSurfaceOffset), FVector(0, 10, VisualizationSurfaceOffset)};
		Source[0].Indices = {0, 1, 2};
		auto* EditorStorage = EditorWorld->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
		auto* PIEStorage = PIEWorld->SpawnActor<AComposableCameraMeshSurfaceStorageActor>();
		if (!TestNotNull(TEXT("Editor fixture storage exists"), EditorStorage)
			|| !TestNotNull(TEXT("PIE fixture storage exists"), PIEStorage)) { return false; }
		TArray<FNativePreviewLayerMesh> Native;
		BuildNativePreviewMeshes(Source, Native);
		AActor* EditorPreview = AppendNativePreviewMeshes(*EditorStorage, nullptr, Native, true);
		AActor* PIEPreview = CreatePIEPreviewActor(*PIEStorage, Source);
		if (!TestNotNull(TEXT("Editor fixture publishes real preview geometry"), EditorPreview)
			|| !TestNotNull(TEXT("PIE fixture publishes real preview geometry"), PIEPreview)) { return false; }
		auto Extension = FSceneViewExtensions::NewExtension<FMeshLayerPreviewViewExtension>();
		Extension->TrackPreviewActor(*EditorPreview);
		Extension->TrackPreviewActor(*PIEPreview);
		AActor* AssetPreview = AssetWorld->SpawnActor<AActor>();
		AActor* GamePreview = GameWorld->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("Asset-world routing fixture exists"), AssetPreview)
			|| !TestNotNull(TEXT("Game-world routing fixture exists"), GamePreview)) { return false; }
		Extension->TrackPreviewActor(*AssetPreview);
		Extension->TrackPreviewActor(*GamePreview);
		EditorWorld->GetPackage()->SetDirtyFlag(false);
		PIEWorld->GetPackage()->SetDirtyFlag(false);
		auto Apply = [&Extension](UWorld& World, int32 ExistingLOD = -1, double Distance = 100.0,
			bool bGameView = false, int32 CaptureKind = 0)
		{
			FEngineShowFlags Flags(bGameView || World.WorldType == EWorldType::PIE ? ESFIM_Game : ESFIM_Editor);
			FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(nullptr, World.Scene, Flags).SetTime(FGameTime()));
			Family.LandscapeLODOverride = static_cast<int8>(ExistingLOD);
			FSceneViewInitOptions Options;
			Options.ViewFamily = &Family;
			Options.ViewOrigin = FVector(Distance, 0, 500);
			Options.SetViewRectangle(FIntRect(0, 0, 64, 64));
			// FamilyContext owns and deletes its views.
			FSceneView* View = new FSceneView(Options);
			View->bIsSceneCapture = CaptureKind == 1;
			View->bIsReflectionCapture = CaptureKind == 2;
			View->bIsPlanarReflection = CaptureKind == 3;
			Family.Views.Add(View);
			Extension->BeginRenderViewFamily(Family);
			return int32(Family.LandscapeLODOverride);
		};
		TestEqual(TEXT("Show off preserves an existing view override"), Apply(*EditorWorld, 3), 3);
		Extension->SetPreviewRequested(true);
		TestTrue(TEXT("Published editor world activates the real view extension"),
			Extension->IsActiveThisFrame(FSceneViewExtensionContext(EditorWorld->Scene)));
		TestFalse(TEXT("A world without a preview does not activate the extension"),
			Extension->IsActiveThisFrame(FSceneViewExtensionContext(UnrelatedWorld->Scene)));
		TestEqual(TEXT("Near editor view uses stable Landscape LOD"), Apply(*EditorWorld), 0);
		TestEqual(TEXT("Far editor view keeps the same Landscape LOD"), Apply(*EditorWorld, -1, 100000.0), 0);
		TestEqual(TEXT("Editor Game View uses the same stable terrain policy"), Apply(*EditorWorld, 3, 100000.0, true), 0);
		TestEqual(TEXT("PIE near view uses stable Landscape LOD"), Apply(*PIEWorld), 0);
		TestEqual(TEXT("PIE far view keeps stable LOD without moving mesh vertices"), Apply(*PIEWorld, -1, 100000.0), 0);
		TestEqual(TEXT("Unrelated editor world preserves its view policy"), Apply(*UnrelatedWorld, 3), 3);
		TestEqual(TEXT("Asset preview worlds preserve their view policy"), Apply(*AssetWorld, 3), 3);
		TestEqual(TEXT("Packaged Game world routing is excluded"), Apply(*GameWorld, 3), 3);
		for (int32 CaptureKind = 1; CaptureKind <= 3; ++CaptureKind)
		{
			TestEqual(TEXT("Scene/reflection/planar captures preserve their own view LOD"),
				Apply(*EditorWorld, 3, 100.0, false, CaptureKind), 3);
		}
		Stabilize->SetWithCurrentPriority(0);
		TestEqual(TEXT("Explicit opt-out restores automatic LOD while Show stays on"), Apply(*EditorWorld), -1);
		TestEqual(TEXT("Opt-out also preserves PIE view overrides"), Apply(*PIEWorld, 3), 3);
		Stabilize->SetWithCurrentPriority(1);
		Extension->SetPIEEnding(true);
		TestEqual(TEXT("PIE teardown preserves the view's original LOD"), Apply(*PIEWorld, 3), 3);
		TestEqual(TEXT("PIE teardown does not disable the editor preview"), Apply(*EditorWorld), 0);
		Extension->SetPIEEnding(false);
		TestEqual(TEXT("A subsequent PIE session can stabilize again"), Apply(*PIEWorld), 0);
		Extension->SetPreviewRequested(false);
		TestEqual(TEXT("Show off restores automatic LOD in the next editor family"), Apply(*EditorWorld), -1);
		TestEqual(TEXT("Show off restores automatic LOD in the next PIE family"), Apply(*PIEWorld), -1);
		Extension->SetPreviewRequested(true);
		Extension->UntrackPreviewActor(*EditorPreview);
		TestEqual(TEXT("Removing the last editor preview stops its override"), Apply(*EditorWorld, 3), 3);
		TestEqual(TEXT("Removing one world's preview preserves another world's policy"), Apply(*PIEWorld), 0);
		Extension->TrackPreviewActor(*EditorPreview);
		TestFalse(TEXT("View policy does not dirty editor Level data"), EditorWorld->GetPackage()->IsDirty());
		TestFalse(TEXT("View policy does not dirty PIE Level data"), PIEWorld->GetPackage()->IsDirty());
		const auto Sample = SamplePIEPreviewActor(*PIEPreview, FVector(2, 2, 0));
		TestTrue(TEXT("Near/far view policy leaves the published mesh at its original height"),
			Sample.Intersections > 0 && FMath::IsNearlyEqual(Sample.NearestPosition.Z, VisualizationSurfaceOffset));
		TestEqual(TEXT("Global r.ForceLOD remains untouched"), ForceLOD->GetInt(), OriginalForceLOD);
		DestroyPIEPreviewActor(EditorPreview);
		TestEqual(TEXT("Destroyed editor actors cannot keep LOD pinned"), Apply(*EditorWorld, 3), 3);
		DestroyPIEPreviewActor(PIEPreview);
		TestEqual(TEXT("Destroyed PIE actors cannot keep LOD pinned"), Apply(*PIEWorld, 3), 3);
		Extension->SetPreviewRequested(false);
		return true;
	}();
	GEngine->DestroyWorldContext(PIEWorld);
	for (UWorld* World : {EditorWorld, PIEWorld, UnrelatedWorld, AssetWorld, GameWorld}) { World->DestroyWorld(false); }
	return bResult;
}

#endif
