// Copyright 2026 Sulley. All Rights Reserved.

#include "MeshCamera/ComposableCameraMeshLayerPreviewViewExtension.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "RenderingThread.h"
#include "SceneView.h"

namespace UE::ComposableCamera::MeshEditor
{
	namespace
	{
		TAutoConsoleVariable<int32> StabilizeLandscapeLOD(
			TEXT("CCS.Editor.MeshLayers.StabilizeLandscapeLOD"), 1,
			TEXT("Keep Landscape at LOD 0 in Editor/PIE views with Show Mesh Layers. "
				"Prevents distant terrain morphing from hiding overlays; increases terrain draw cost. "
				"0 restores normal view LOD. Does not change r.ForceLOD or saved Landscape settings."));
		TSharedPtr<FMeshLayerPreviewViewExtension, ESPMode::ThreadSafe> PreviewViewExtension;
	}

	FMeshLayerPreviewViewExtension::FMeshLayerPreviewViewExtension(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister) {}

	void FMeshLayerPreviewViewExtension::SetPreviewRequested(bool bRequested)
	{
		check(IsInGameThread());
		bPreviewRequested = bRequested;
	}

	void FMeshLayerPreviewViewExtension::SetPIEEnding(bool bEnding)
	{
		check(IsInGameThread());
		bPIEEnding = bEnding;
	}

	void FMeshLayerPreviewViewExtension::TrackPreviewActor(AActor& Actor)
	{
		check(IsInGameThread());
		PreviewActors.RemoveAllSwap([](const auto& WeakActor)
		{
			const AActor* Preview = WeakActor.Get();
			return !Preview || Preview->IsActorBeingDestroyed() || !Preview->GetWorld()
				|| Preview->GetWorld()->IsBeingCleanedUp();
		});
		PreviewActors.AddUnique(TWeakObjectPtr<AActor>(&Actor));
	}

	void FMeshLayerPreviewViewExtension::UntrackPreviewActor(AActor& Actor)
	{
		check(IsInGameThread());
		PreviewActors.RemoveAllSwap([&Actor](const auto& WeakActor)
		{
			return !WeakActor.IsValid() || WeakActor.Get() == &Actor;
		});
	}

	bool FMeshLayerPreviewViewExtension::ShouldStabilizeWorld(const UWorld* World) const
	{
		check(IsInGameThread());
		if (!bPreviewRequested || StabilizeLandscapeLOD.GetValueOnGameThread() == 0
			|| !World || World->IsBeingCleanedUp()
			|| (World->WorldType != EWorldType::Editor && World->WorldType != EWorldType::PIE)
			|| (bPIEEnding && World->WorldType == EWorldType::PIE)) { return false; }
		for (const auto& WeakActor : PreviewActors)
		{
			const AActor* Actor = WeakActor.Get();
			if (Actor && !Actor->IsActorBeingDestroyed() && Actor->GetWorld() == World) { return true; }
		}
		return false;
	}

	bool FMeshLayerPreviewViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
	{
		return ShouldStabilizeWorld(Context.GetWorld());
	}

	void FMeshLayerPreviewViewExtension::BeginRenderViewFamily(FSceneViewFamily& ViewFamily)
	{
		if (!ViewFamily.Scene || !ShouldStabilizeWorld(ViewFamily.Scene->GetWorld()) || ViewFamily.Views.IsEmpty()) { return; }
		for (const FSceneView* View : ViewFamily.Views)
		{
			if (View->bIsSceneCapture || View->bIsReflectionCapture || View->bIsPlanarReflection) { return; }
		}
		// Each frame owns a fresh family. Turning Show off leaves subsequent families'
		// original override intact, without restoring any global or serialized value.
		ViewFamily.LandscapeLODOverride = 0;
	}

	void InitializePreviewViewExtension()
	{
		check(IsInGameThread());
		if (!PreviewViewExtension) { PreviewViewExtension = FSceneViewExtensions::NewExtension<FMeshLayerPreviewViewExtension>(); }
	}

	void SetPreviewViewRequested(bool bRequested)
	{
		if (PreviewViewExtension) { PreviewViewExtension->SetPreviewRequested(bRequested); }
	}

	void SetPreviewViewPIEEnding(bool bEnding)
	{
		if (PreviewViewExtension) { PreviewViewExtension->SetPIEEnding(bEnding); }
	}

	void TrackPreviewViewActor(AActor& Actor)
	{
		if (PreviewViewExtension) { PreviewViewExtension->TrackPreviewActor(Actor); }
	}

	void UntrackPreviewViewActor(AActor& Actor)
	{
		if (PreviewViewExtension) { PreviewViewExtension->UntrackPreviewActor(Actor); }
	}

	void ShutdownPreviewViewExtension()
	{
		check(IsInGameThread());
		if (PreviewViewExtension)
		{
			PreviewViewExtension->SetPreviewRequested(false);
			// Engine render commands retain active extensions for their in-flight family.
			// Wait only at module unload, never on Show toggles or PIE teardown.
			FlushRenderingCommands();
			PreviewViewExtension.Reset();
		}
	}
}
