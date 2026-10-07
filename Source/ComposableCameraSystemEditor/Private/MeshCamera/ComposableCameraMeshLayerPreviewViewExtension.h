// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class AActor;
class UWorld;

namespace UE::ComposableCamera::MeshEditor
{
	/** Preview-only view policy. Never changes Landscape components, source heights or global LOD CVars. */
	class FMeshLayerPreviewViewExtension : public FSceneViewExtensionBase
	{
	public:
		explicit FMeshLayerPreviewViewExtension(const FAutoRegister& AutoRegister);
		void SetPreviewRequested(bool bRequested);
		void SetPIEEnding(bool bEnding);
		void TrackPreviewActor(AActor& Actor);
		void UntrackPreviewActor(AActor& Actor);
		virtual void BeginRenderViewFamily(FSceneViewFamily& ViewFamily) override;
		virtual int32 GetPriority() const override { return -100; }

	protected:
		virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

	private:
		bool ShouldStabilizeWorld(const UWorld* World) const;
		// Game-thread state; no UObject ownership and no per-view allocations.
		TArray<TWeakObjectPtr<AActor>> PreviewActors;
		bool bPreviewRequested = false;
		bool bPIEEnding = false;
	};

	void InitializePreviewViewExtension();
	void SetPreviewViewRequested(bool bRequested);
	void SetPreviewViewPIEEnding(bool bEnding);
	void TrackPreviewViewActor(AActor& Actor);
	void UntrackPreviewViewActor(AActor& Actor);
	/** Module unload only: drain in-flight view families before unloading extension code. */
	void ShutdownPreviewViewExtension();
}
