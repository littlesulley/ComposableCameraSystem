// Copyright Sulley. All rights reserved.

#include "Debug/ComposableCameraViewportDebug.h"

#include "Cameras/ComposableCameraCameraBase.h"
#include "ComposableCameraSystemModule.h"
#include "Containers/Ticker.h"
#include "Core/ComposableCameraContextStack.h"
#include "Core/ComposableCameraDirector.h"
#include "Core/ComposableCameraEvaluationTree.h"
#include "Core/ComposableCameraPlayerCameraManager.h"
#include "Debug/DebugDrawService.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#if WITH_EDITOR
#include "Editor.h"   // GEditor, UEditorEngine::bIsSimulatingInEditor
#endif

#if UE_BUILD_SHIPPING
bool FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()
{
	return false;
}

bool FComposableCameraViewportDebug::ShouldShowAllTransitionGizmos()
{
	return false;
}
#endif
// ---------------------------------------------------------------------
// Viewport 3D debug. Private implementation.
//
// Design note: an earlier iteration used `UDebugDrawService::Register`
// on the "Game" show-flag channel. That hook works while the player is
// actively rendering through the game viewport, but does NOT fire when
// F8 ejects the player in PIE. The editor viewport takes over, and
// its draw path doesn't fan out to the game-channel debug delegates.
//
// We now drive the debug draw off `FTSTicker::GetCoreTicker()` instead:
// a single-shot ticker fires every frame regardless of which viewport
// owns the current render pass. The draw itself goes through
// `DrawDebugCamera`, which adds lines to the world's LineBatcher. And
// LineBatcher content is rendered by every viewport that draws that
// world, so the frustum is visible both in the game viewport (when
// possessed) and in the editor viewport (when F8-ejected or in
// Simulate mode).
//
// Auto-hide while possessed: gated on `GEditor->bIsSimulatingInEditor`
// (WITH_EDITOR only). That flag is true for both "Simulate in Editor"
// mode and the F8-ejected-from-PIE state, and false while the player
// is possessing the camera. Exactly the semantics we want. In
// non-editor builds the gate is skipped (draw always when CVar is on).
// ---------------------------------------------------------------------
namespace
{
	// ---- Console variables ---------------------------------------------
	static TAutoConsoleVariable<int32> CVarViewportDebugEnabled(
		TEXT("CCS.Debug.Viewport"),
		0,
		TEXT("Master toggle for in-world 3D debug draw.\n")
		TEXT("  0: everything disabled\n")
		TEXT("  1: enabled. Draws the camera FRUSTUM (auto-hidden while the player\n")
		TEXT("     is possessing the camera, shown after F8 eject / Simulate mode).\n")
		TEXT("     Per-node gizmos (pivot spheres, look-at lines, collision traces,\n")
		TEXT("     spline paths, etc.) DO NOT turn on just from this CVar. Each\n")
		TEXT("     node has its own `CCS.Debug.Viewport.<NodeName>` CVar that you\n")
		TEXT("     enable individually. Per-node gizmos show in BOTH possessed\n")
		TEXT("     play and F8 eject when their CVar is on."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarViewportDebugAlwaysShow(
		TEXT("CCS.Debug.Viewport.AlwaysShow"),
		0,
		TEXT("Bypass the frustum auto-hide-while-possessed gate. Set to 1 if you\n")
		TEXT("want the frustum pyramid drawn even while the player is actively\n")
		TEXT("viewing through the camera. Only affects the frustum; per-node\n")
		TEXT("gizmos already work in both states via their own CVars."),
		ECVF_Default);

	// "Enable all per-X gizmos" shortcut CVars. Still gated by the master
	// `CCS.Debug.Viewport 1`. When master is off nothing draws. OR logic
	// against each per-item CVar; no negative/except semantics.
	static TAutoConsoleVariable<int32> CVarViewportDebugNodesAll(
		TEXT("CCS.Debug.Viewport.Nodes.All"),
		0,
		TEXT("Enable every per-node gizmo at once (3D + 2D HUD overlays both).\n")
		TEXT("Saves typing 9+ individual `CCS.Debug.Viewport.<NodeName>` commands.\n")
		TEXT("Requires `CCS.Debug.Viewport 1`. OR-ed with each per-node CVar n")
		TEXT("turning this on does NOT turn the per-node CVars on; the node\n")
		TEXT("gizmo draws as long as EITHER this or its own CVar is non-zero."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarViewportDebugTransitionsAll(
		TEXT("CCS.Debug.Viewport.Transitions.All"),
		0,
		TEXT("Enable every per-transition gizmo at once. Saves typing 9 individual\n")
		TEXT("`CCS.Debug.Viewport.Transitions.<Name>` commands. Requires\n")
		TEXT("`CCS.Debug.Viewport 1`. OR-ed with each per-transition CVar."),
		ECVF_Default);


	// ---- Module state --------------------------------------------------
	static FTSTicker::FDelegateHandle GTickerHandle;
	// (FTSTicker::FDelegateHandle is defined in Containers/Ticker.h and is the
	// handle type AddTicker / RemoveTicker use; plain FDelegateHandle is NOT
	// interchangeable. The ticker has its own subclassed handle type.)

	// 2D HUD-pass hook for per-node Canvas overlays (safe-zone rectangles,
	// projected pivot markers, etc.). Registered on the "Game" channel of
	// UDebugDrawService. Fires from UGameViewportClient::Draw during PIE
	// possessed play and standalone, does NOT fire from the editor viewport
	// during F8 eject. That's the right gating: 2D overlays answer "is the
	// player's on-screen view correct", which only makes sense when the
	// player's view IS the render surface.
	static FDelegateHandle G2DHookHandle;

	// Cached master CVar values, refreshed once per frame at the top of
	// TickDraw / Draw2DHUD. Each per-node and per-transition override calls
	// ShouldShowAll*() before every atomic CVar read of its own CVar. With
	// ~8 nodes and ~9 transitions active that's ~34 atomic loads per frame
	// from the two master "All" CVars alone. Caching them into a module-scope
	// bool lets the helpers return a plain load instead. Default false so
	// early-out paths that never hit a refresh remain safe (the shortcut
	// just isn't honored that frame. Identical to the CVar being 0).
	static bool GAllNodeGizmosThisFrame = false;
	static bool GAllTransitionGizmosThisFrame = false;

#if !UE_BUILD_SHIPPING

	/** Decide whether the frustum pyramid should be drawn this frame.
	 *
	 *  The frustum is the ONE piece of debug draw that genuinely occludes the
	 *  scene when the player is looking through the camera. Its wireframe
	 *  ends up immediately in front of the near plane. So it stays gated on
	 *  `GEditor->bIsSimulatingInEditor` (true in F8 eject / SIE, false while
	 *  possessing). The `CCS.Debug.Viewport.AlwaysShow` CVar overrides the
	 *  gate for multi-viewport setups.
	 *
	 *  Per-node gizmos have their OWN per-CVar gating inside each
	 *  `UComposableCameraCameraNodeBase::DrawNodeDebug` override, so they are
	 *  invoked regardless of SIE state. Usable during live gameplay because
	 *  they rarely occlude the viewpoint (spheres out in the world, lines to
	 *  distant targets, etc.). */
	static bool ShouldDrawFrustumThisFrame()
	{
		if (CVarViewportDebugAlwaysShow.GetValueOnGameThread() != 0) { return true; }
#if WITH_EDITOR
		if (GEditor == nullptr)            { return false; }
		if (GEditor->PlayWorld == nullptr) { return false; } // no PIE / SIE running
		return GEditor->bIsSimulatingInEditor;
#else
		// Non-editor: no eject concept exists. Always show.
		return true;
#endif
	}

	/** Per-frame ticker entry. Finds the PIE / Game world's first PCM, pulls
	 *  the running camera, and calls `DrawCameraDebug` with a frustum flag
	 *  decided by `ShouldDrawFrustumThisFrame`. Per-node gizmos go through
	 *  each node's own CVar check inside `DrawNodeDebug`. */
	static bool TickDraw(float /*DeltaTime*/)
	{
		// Master CVar still gates the whole path. Zero cost when off.
		if (CVarViewportDebugEnabled.GetValueOnGameThread() == 0) { return true; }
		if (!GEngine)                                             { return true; }

		// Refresh the cached "show all" flags once per frame. Every node /
		// transition override that checks ShouldShowAll*() reads the cached
		// bool instead of paying an atomic CVar load.
		GAllNodeGizmosThisFrame       = CVarViewportDebugNodesAll.GetValueOnGameThread() != 0;
		GAllTransitionGizmosThisFrame = CVarViewportDebugTransitionsAll.GetValueOnGameThread() != 0;

		const bool bDrawFrustum = ShouldDrawFrustumThisFrame();

		static bool bLoggedFirstDraw = false;

		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game) { continue; }
			UWorld* World = Ctx.World();
			if (!World) { continue; }

			APlayerController* PC = World->GetFirstPlayerController();
			if (!PC) { continue; }

			auto* PCM = Cast<AComposableCameraPlayerCameraManager>(PC->PlayerCameraManager);
			if (!PCM) { continue; }

			AComposableCameraCameraBase* RunningCamera = PCM->GetRunningCamera();
			if (!IsValid(RunningCamera)) { continue; }

			if (!bLoggedFirstDraw)
			{
				bLoggedFirstDraw = true;
				UE_LOG(LogComposableCameraSystem, Log,
					TEXT("CCS.Debug.Viewport: ticker active (World=%s, Camera=%s, bDrawFrustum=%d)"),
					*World->GetName(), *RunningCamera->GetName(), bDrawFrustum ? 1 : 0);
			}

			RunningCamera->DrawCameraDebug(World, bDrawFrustum);

			// Per-transition gizmos. Walk the active director's evaluation
			// tree (recursing into reference leaves for inter-context blends)
			// and give each Inner's transition a chance to draw its gizmo.
			// Each transition's DrawTransitionDebug override self-gates on
			// its own `CCS.Debug.Viewport.Transitions.<Name>` CVar, so this
			// loop is near-free when no per-transition CVars are enabled.
			//
			// We pass `bDrawFrustum` through as `bViewerIsOutsideCamera` for
			// the exact same reason the per-camera draw does: it is the
			// "player is NOT viewing through the blended camera" signal,
			// and transition frustum pieces use it to gate themselves.
			if (const UComposableCameraContextStack* Stack = PCM->GetContextStack())
			{
				if (UComposableCameraDirector* ActiveDir = Stack->GetActiveDirector())
				{
					if (UComposableCameraEvaluationTree* Tree = ActiveDir->GetEvaluationTree())
					{
						Tree->DrawTransitionsDebug(World, /*bViewerIsOutsideCamera=*/bDrawFrustum);
					}
				}
			}
		}

		return true; // keep ticking
	}

	/** 2D HUD-pass entry. Fires from `UGameViewportClient::Draw` with a valid
	 *  UCanvas; PC usually comes through but is nullptr on some UE 5.6 paths
	 *  (e.g. split-screen teardown, editor preview callbacks), so we fall
	 *  back to a world-context scan mirroring the 3D ticker's resolution. */
	static void Draw2DHUD(UCanvas* Canvas, APlayerController* PC)
	{
		if (CVarViewportDebugEnabled.GetValueOnGameThread() == 0) { return; }
		if (!Canvas || Canvas->SizeX <= 0 || Canvas->SizeY <= 0) { return; }
		if (!GEngine) { return; }

		// Refresh the cached flags here too. The 2D hook fires from
		// UGameViewportClient::Draw, which runs after the ticker but we
		// don't want to depend on the exact ordering across frames.
		GAllNodeGizmosThisFrame       = CVarViewportDebugNodesAll.GetValueOnGameThread() != 0;
		GAllTransitionGizmosThisFrame = CVarViewportDebugTransitionsAll.GetValueOnGameThread() != 0;

		// First try the PC the delegate handed us.
		AComposableCameraPlayerCameraManager* PCM = nullptr;
		if (PC)
		{
			PCM = Cast<AComposableCameraPlayerCameraManager>(PC->PlayerCameraManager);
		}
		// Fallback: scan PIE/Game worlds for the first local PC with a CCS PCM.
		if (!PCM)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game) { continue; }
				UWorld* World = Ctx.World();
				if (!World) { continue; }
				APlayerController* LocalPC = World->GetFirstPlayerController();
				if (!LocalPC) { continue; }
				if (auto* CCSPCM = Cast<AComposableCameraPlayerCameraManager>(LocalPC->PlayerCameraManager))
				{
					PCM = CCSPCM;
					if (!PC) { PC = LocalPC; } // use this PC for ProjectWorldToScreen downstream
					break;
				}
			}
		}
		if (!PCM) { return; }

		AComposableCameraCameraBase* RunningCamera = PCM->GetRunningCamera();
		if (!IsValid(RunningCamera)) { return; }

		APlayerController* EffectivePC = PC ? PC : PCM->GetOwningPlayerController();
		RunningCamera->DrawCameraDebug2D(Canvas, EffectivePC);
	}

#endif // !UE_BUILD_SHIPPING
} // anonymous namespace

// ---------------------------------------------------------------------
// Public lifecycle.
// ---------------------------------------------------------------------
void FComposableCameraViewportDebug::Initialize()
{
#if !UE_BUILD_SHIPPING
	if (!GTickerHandle.IsValid())
	{
		GTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&TickDraw),
			/*InDelay=*/0.f);
	}
	if (!G2DHookHandle.IsValid())
	{
		G2DHookHandle = UDebugDrawService::Register(
			TEXT("Game"),
			FDebugDrawDelegate::CreateStatic(&Draw2DHUD));
	}
#endif
}

void FComposableCameraViewportDebug::Shutdown()
{
#if !UE_BUILD_SHIPPING
	if (GTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GTickerHandle);
		GTickerHandle.Reset();
	}
	if (G2DHookHandle.IsValid())
	{
		UDebugDrawService::Unregister(G2DHookHandle);
		G2DHookHandle.Reset();
	}
#endif
}

#if !UE_BUILD_SHIPPING
bool FComposableCameraViewportDebug::ShouldShowAllNodeGizmos()
{
	return GAllNodeGizmosThisFrame;
}

bool FComposableCameraViewportDebug::ShouldShowAllTransitionGizmos()
{
	return GAllTransitionGizmosThisFrame;
}

void FComposableCameraViewportDebug::DrawSolidDebugSphere(
	UWorld* World,
	const FVector& Center,
	float Radius,
	const FColor& Color,
	uint8 Alpha,
	int32 Segments,
	uint8 DepthPriority)
{
	if (!World)  { return; }
	if (Radius <= 0.f) { return; }

	// NOTE: Despite the name, this now renders a TRANSLUCENT-WIREFRAME
	// sphere (not a solid mesh). The name is kept for API compatibility
	// with the 16+ callsites that adopted the helper; renaming would be
	// busywork.
	//
	// Implementation history (short version):
	//   1. Wireframe `DrawDebugSphere`. Too lattice-y, looked "ugly".
	//   2. Solid mesh via `DrawDebugMesh`. Looked nice unoccluded, but
	//      engine's hardcoded `DebugMeshMaterial` depth-tests regardless
	//      of `DepthPriority`, so the fill got clipped by character
	//      meshes (pivot on a chest, target on a bone socket, etc.).
	//      `SDPG_Foreground` only bypasses depth for LINE primitives,
	//      not for the mesh material path.
	//   3. Hybrid solid+wireframe. The double layer read as two stacked
	//      gizmos rather than one coherent marker; the user preferred
	//      the wireframe layer alone.
	//   4. CURRENT: just the wireframe layer (line primitive, Alpha
	//      applied, `SDPG_Foreground` for proper through-wall draw).
	//      Looks cleaner than (1) because of: lower segment count (8
	//      instead of 12 = sparser lattice), `Thickness=0` (1-pixel
	//      lines instead of thicker), and alpha blending. Retains (2)'s
	//      visual niceness minus the occlusion issue.
	//
	// The `Segments` parameter still clamps into a sensible range in
	// case callers pass something exotic. Default 12 gives a smooth-
	// enough silhouette without line clutter.
	Segments = FMath::Clamp(Segments, 4, 32);

	// Alpha comes from the helper param (overrides whatever was in
	// Color.A) so callsites keep passing their usual `FColor::X` and
	// get consistent translucency without baking it into the color.
	const FColor Final(Color.R, Color.G, Color.B, Alpha);

	// `SDPG_Foreground` is the key. For line primitives the line batcher
	// draws in a foreground pass that ignores depth test, so the sphere
	// outline renders THROUGH opaque meshes (character in the way, etc.).
	// If a caller explicitly passes SDPG_World we honor that, but the
	// default at every callsite is SDPG_Foreground because "marker that
	// gets hidden by meshes" is almost never what debug viz wants.
	DrawDebugSphere(World, Center, Radius,
		/*Segments=*/Segments, Final,
		/*bPersistentLines=*/false, /*LifeTime=*/-1.f,
		/*DepthPriority=*/DepthPriority, /*Thickness=*/0.f);
}
#endif
