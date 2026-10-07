// Copyright 2026 Sulley. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Utilities/ComposableCameraMeshLayerTool.h"
#include "MeshCamera/ComposableCameraMeshLayerEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerPreviewEdMode.h"
#include "MeshCamera/ComposableCameraMeshLayerPIEPreview.h"
#include "MeshCamera/ComposableCameraMeshSurfaceStorageActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "LevelEditorViewport.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshLayerPreviewModeResumeTest,
	"ComposableCameraSystem.Editor.MeshCamera.PreviewModeResumeAfterEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshLayerPreviewModeResumeTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->PlayWorld) { AddWarning(TEXT("Run in the Level Editor outside PIE.")); return true; }
	FEditorModeTools& Modes = GLevelEditorModeTools();
	using FTool = FComposableCameraMeshLayerTool;
	const FEditorModeID EditId = FComposableCameraMeshLayerEdMode::ModeId;
	const FEditorModeID PreviewId = FComposableCameraMeshLayerPreviewEdMode::ModeId;
	if (FTool::IsEditModeActive() || FTool::IsPreviewModeActive()
		|| !Modes.IsDefaultModeActive() || !Modes.GetToolkitHost().IsValid())
	{
		AddWarning(TEXT("Close authoring tools and disable Show Mesh Layers before this mode integration test."));
		return true;
	}
	struct FRestoreModes
	{
		FEditorModeTools& Tools;
		~FRestoreModes()
		{
			Tools.DestroyMode(FComposableCameraMeshLayerEdMode::ModeId);
			if (FComposableCameraMeshLayerTool::IsPreviewModeActive()) { FComposableCameraMeshLayerTool::TogglePreviewMode(); }
			Tools.DestroyMode(FComposableCameraMeshLayerPreviewEdMode::ModeId);
			Tools.ActivateDefaultMode();
		}
	} Restore{Modes};
	FTool::TogglePreviewMode();
	if (!TestTrue(TEXT("Show activates its actual rendering mode"), Modes.IsModeActive(PreviewId))) { return false; }
	FTool::ToggleEditMode();
	if (!TestTrue(TEXT("Edit menu activates the actual authoring mode"), Modes.IsModeActive(EditId))) { return false; }
	TestTrue(TEXT("Edit retains the checked Show intent"), FTool::IsPreviewModeActive());
	TestFalse(TEXT("Edit removes the read-only mode to avoid duplicate fill"), Modes.IsModeActive(PreviewId));
	// Complete the old preview's deferred Exit, as the editor's next mode Tick does.
	Modes.DestroyMode(PreviewId);
	FTool::UpdateEditorPreviewMode();
	TestFalse(TEXT("Show cannot displace an active Edit"), Modes.IsModeActive(PreviewId));
	auto* Edit = Modes.GetActiveModeTyped<FComposableCameraMeshLayerEdMode>(EditId);
	if (!TestNotNull(TEXT("Actual Edit mode is available"), Edit)) { return false; }
	Edit->RequestCloseFromToolkit();
	TestFalse(TEXT("Panel close deactivates Edit immediately"), Modes.IsModeActive(EditId));
	FTool::UpdateEditorPreviewMode();
	TestFalse(TEXT("Resume waits for deferred Edit cleanup/save prompt"), Modes.IsModeActive(PreviewId));
	Modes.DestroyMode(EditId); // Force the pending Exit deterministically; no document edits or save prompt.
	FTool::UpdateEditorPreviewMode();
	TestTrue(TEXT("Closing Edit restores the real Show rendering mode"), Modes.IsModeActive(PreviewId));
	TestTrue(TEXT("Restoration retains the checked Show intent"), FTool::IsPreviewModeActive());

	// Entering through the mode selector must also suspend/resume, bypassing the Edit menu.
	Modes.ActivateMode(EditId);
	if (!TestTrue(TEXT("Mode selector enters Edit"), Modes.IsModeActive(EditId))) { return false; }
	Modes.DestroyMode(PreviewId);
	FTool::UpdateEditorPreviewMode();
	TestFalse(TEXT("Mode selector also suspends Show"), Modes.IsModeActive(PreviewId));
	Modes.DeactivateMode(EditId); Modes.DestroyMode(EditId); FTool::UpdateEditorPreviewMode();
	TestTrue(TEXT("Mode selector exit also restores Show"), Modes.IsModeActive(PreviewId));

	FTool::ToggleEditMode(); Modes.DestroyMode(PreviewId);
	if (!TestTrue(TEXT("Edit reopens while Show remains requested"), Modes.IsModeActive(EditId))) { return false; }
	FTool::TogglePreviewMode();
	TestFalse(TEXT("Explicit Show off clears intent during Edit"), FTool::IsPreviewModeActive());
	TestTrue(TEXT("Show off does not close Edit"), Modes.IsModeActive(EditId));
	FTool::ToggleEditMode(); Modes.DestroyMode(EditId); FTool::UpdateEditorPreviewMode();
	TestFalse(TEXT("Closing Edit cannot resurrect explicitly disabled Show"), Modes.IsModeActive(PreviewId));

	// Preserve the existing one-click Edit -> Show-on behavior when Show started off.
	FTool::ToggleEditMode();
	if (!TestTrue(TEXT("Edit opens with Show off"), Modes.IsModeActive(EditId))) { return false; }
	FTool::TogglePreviewMode();
	TestFalse(TEXT("Show-on command closes Edit in one click"), Modes.IsModeActive(EditId));
	TestTrue(TEXT("Show-on command immediately records its intent"), FTool::IsPreviewModeActive());
	TestFalse(TEXT("Show-on also waits for deferred cleanup"), Modes.IsModeActive(PreviewId));
	Modes.DestroyMode(EditId); FTool::UpdateEditorPreviewMode();
	TestTrue(TEXT("Show-on publishes through the restored Preview mode"), Modes.IsModeActive(PreviewId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshStationaryPreviewTest,
	"ComposableCameraSystem.Editor.MeshCamera.StationaryPreviewPublication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshStationaryPreviewTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	if (!GEditor || GEditor->PlayWorld) { AddWarning(TEXT("Run in the Level Editor outside PIE.")); return true; }
	FEditorModeTools& Modes = GLevelEditorModeTools();
	if (FComposableCameraMeshLayerTool::IsPreviewModeActive()
		|| Modes.IsModeActive(FComposableCameraMeshLayerEdMode::ModeId)
		|| !Modes.IsDefaultModeActive() || !Modes.GetToolkitHost().IsValid())
	{
		AddWarning(TEXT("Disable Show and close authoring tools before stationary publication testing.")); return true;
	}
	UWorld* World = Modes.GetWorld();
	if (!World || World->WorldType != EWorldType::Editor) { AddWarning(TEXT("No editor world.")); return true; }
	struct FRestore
	{
		FEditorModeTools& Tools; TWeakObjectPtr<AActor> Storage; UPackage* Package; bool bDirty;
		~FRestore()
		{
			if (FComposableCameraMeshLayerTool::IsPreviewModeActive()) { FComposableCameraMeshLayerTool::TogglePreviewMode(); }
			Tools.DestroyMode(FComposableCameraMeshLayerPreviewEdMode::ModeId); Tools.ActivateDefaultMode();
			if (Storage.IsValid()) { Storage->Destroy(); }
			if (Package) { Package->SetDirtyFlag(bDirty); }
		}
	} Restore{Modes, {}, World->GetOutermost(), World->GetOutermost()->IsDirty()};
	FActorSpawnParameters Params; Params.ObjectFlags = RF_Transient; Params.bTemporaryEditorActor = true;
	auto* Storage = World->SpawnActor<AComposableCameraMeshSurfaceStorageActor>(Params); Restore.Storage = Storage;
	if (!TestNotNull(TEXT("Disposable source document"), Storage)) { return false; }
	FComposableCameraMeshLayerTool::TogglePreviewMode();
	auto* Mode = Modes.GetActiveModeTyped<FComposableCameraMeshLayerPreviewEdMode>(FComposableCameraMeshLayerPreviewEdMode::ModeId);
	if (!TestNotNull(TEXT("Actual Show mode"), Mode)) { return false; }
	TArray<FResolvedSurfaceLayerMesh> Source; Source.SetNum(1); Source[0].LayerIndex = 0;
	for (int32 Triangle = 0; Triangle < 10; ++Triangle)
	{
		const int32 Base = Source[0].LocalVertices.Num(); const double X = Triangle * 20.0;
		Source[0].LocalVertices.Append({FVector(X, 0, 2), FVector(X + 10, 0, 2), FVector(X, 10, 2)});
		Source[0].Indices.Append({Base, Base + 1, Base + 2});
	}
	const TWeakObjectPtr<AComposableCameraMeshSurfaceStorageActor> Key(Storage);
	auto& Cache = Mode->Previews.FindOrAdd(Key);
	BuildNativePreviewMeshes(Source, Cache.ReadyMeshes, 1);
	const auto& Viewports = GEditor->GetLevelViewportClients();
	FLevelEditorViewportClient* Viewport = Viewports.IsEmpty() ? nullptr : Viewports[0];
	const FVector CameraBefore = Viewport ? Viewport->GetViewLocation() : FVector::ZeroVector;
	// No viewport Tick, input or camera movement. This is the same entry used by
	// the core ticker; completed geometry must publish and invalidate static views.
	Mode->AdvancePreview();
	const int32 FirstProgress = Mode->Previews.FindChecked(Key).NextMesh;
	AActor* Preview = Mode->Previews.FindChecked(Key).PreviewActor.Get();
	if (!TestNotNull(TEXT("Stationary loading creates a visible preview actor"), Preview)) { return false; }
	TArray<UDynamicMeshComponent*> Components; Preview->GetComponents(Components);
	TestTrue(TEXT("Ready native geometry publishes without viewport ticks"), !Components.IsEmpty());
	TestTrue(TEXT("Completion requests follow-up static redraws"), Mode->PendingRedrawFrames > 0);
	if (Viewport)
	{
		TestTrue(TEXT("Stationary viewport is explicitly invalidated"), Viewport->bNeedsRedraw);
		TestTrue(TEXT("Publication does not move the camera"), Viewport->GetViewLocation().Equals(CameraBefore));
	}
	Mode->AdvancePreview();
	TestEqual(TEXT("Core and viewport callers share one frame budget"), Mode->Previews.FindChecked(Key).NextMesh, FirstProgress);
	// Other loaded user documents may also be building. Cancel only their
	// disposable test-session jobs so the final redraw check has a stable cache.
	for (auto& Pair : Mode->Previews)
	{
		if (Pair.Key != Key) { Pair.Value.Build.Reset(); Pair.Value.ReadyMeshes.Empty(); Pair.Value.NextMesh = 0; }
	}
	// Simulate subsequent frames without drawing/ticking a viewport.
	for (int32 Frame = 0; Frame < 12 && !Mode->Previews.FindChecked(Key).ReadyMeshes.IsEmpty(); ++Frame)
	{
		Mode->LastTickFrame = MAX_uint64; Mode->AdvancePreview();
	}
	Preview = Mode->Previews.FindChecked(Key).PreviewActor.Get(); Components.Reset();
	if (Preview) { Preview->GetComponents(Components); }
	TestEqual(TEXT("Every ready chunk drains while the camera stays still"), Components.Num(), 10);
	Mode->LastTickFrame = MAX_uint64; Mode->AdvancePreview();
	Mode->LastTickFrame = MAX_uint64; Mode->AdvancePreview();
	TestEqual(TEXT("Completed stable geometry stops requesting redraws"), Mode->PendingRedrawFrames, 0);
	// Audit regression: a live storage Actor can change after an empty document
	// has completed. Actor identity/lifetime alone must not keep its old fill alive.
	TArray<FComposableCameraMeshLayerDefinition> Layers; Layers.AddDefaulted(); Layers[0].LayerId = FGuid::NewGuid();
	FComposableCameraMeshSurfaceAuthoringData Updated;
	Updated.Vertices = {FVector3f(1000, 0, 0), FVector3f(1050, 0, 0), FVector3f(1000, 50, 0)};
	Updated.Indices = {0, 1, 2}; Updated.TriangleLayerIds.Add(Layers[0].LayerId);
	Storage->SetAuthoringData(Layers, Updated);
	bool bDisplaysUpdatedSource = false;
	const double Deadline = FPlatformTime::Seconds() + 3.0;
	while (!bDisplaysUpdatedSource && FPlatformTime::Seconds() < Deadline)
	{
		Mode->LastTickFrame = MAX_uint64; Mode->AdvancePreview();
		Preview = Mode->Previews.FindChecked(Key).PreviewActor.Get();
		bDisplaysUpdatedSource = Preview && SamplePIEPreviewActor(*Preview, FVector(1010, 10, 0)).Intersections > 0;
		if (!bDisplaysUpdatedSource) { FPlatformProcess::Sleep(0.001f); }
	}
	TestTrue(TEXT("Show publishes changed source on the same live storage Actor"), bDisplaysUpdatedSource);
	TestTrue(TEXT("Source replacement removes the old submitted fill"), !Preview
		|| SamplePIEPreviewActor(*Preview, FVector(2, 2, 0)).Intersections == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComposableCameraMeshPreviewBudgetTest,
	"ComposableCameraSystem.Editor.MeshCamera.PreviewPublicationBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FComposableCameraMeshPreviewBudgetTest::RunTest(const FString&)
{
	using namespace UE::ComposableCamera::MeshEditor;
	FPreviewPublicationBudget Fast(10.0); int32 Work = 0, Next = 0;
	RunPIEPreviewWorkRoundRobin(3, Next, [&](int32)
	{
		if (!Fast.CanAdvance(10.0 + Work * 0.00005)) { return EPIEPreviewWorkResult::BudgetExhausted; }
		Fast.Consume(); ++Work; return EPIEPreviewWorkResult::Advanced;
	});
	TestTrue(TEXT("Cheap publication uses spare time beyond the former two-chunk cap"), Work > 2);
	TestEqual(TEXT("Cheap loops retain a finite construction cap"), Work, PreviewPublicationChunkLimit);
	FPreviewPublicationBudget Slow(20.0); Work = 0;
	RunPIEPreviewWorkRoundRobin(3, Next, [&](int32)
	{
		if (!Slow.CanAdvance(20.0 + Work * 0.0011)) { return EPIEPreviewWorkResult::BudgetExhausted; }
		Slow.Consume(); ++Work; return EPIEPreviewWorkResult::Advanced;
	});
	TestEqual(TEXT("Expensive publication still stops at the existing soft time budget"), Work, 2);
	TestFalse(TEXT("An expired budget cannot start another upload"), Slow.CanAdvance(20.0 + PreviewPublicationSeconds));
	return true;
}

#endif
