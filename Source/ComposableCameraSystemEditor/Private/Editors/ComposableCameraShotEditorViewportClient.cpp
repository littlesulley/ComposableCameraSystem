// Copyright 2026 Sulley. All Rights Reserved.

#include "Editors/ComposableCameraShotEditorViewportClient.h"
#include "Widgets/ComposableCameraShotViewportDisplayUtils.h"
#include "Widgets/ComposableCameraShotViewportOverlayUtils.h"
#include "Widgets/ComposableCameraShotSubjectGizmoUtils.h"
#include "ComposableCameraEditorStyle.h"

#include "Animation/SkeletalMeshActor.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "Editors/ComposableCameraShotAuthoringSession.h"
#include "ComposableCameraSystemEditorModule.h" // LogComposableCameraSystemEditor
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DataAssets/ComposableCameraShot.h"
#include "DataAssets/ComposableCameraShotTarget.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "ISequencer.h"
#include "Modules/ModuleManager.h"
#include "MovieScene/MovieSceneComposableCameraShotSection.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneSequence.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "ComposableCameraShotEditorViewportClient"
#include "Engine/Font.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/ComposableCameraMath.h"
#include "Math/ComposableCameraShotSolver.h"
#include "PreviewScene.h"
#include "SceneManagement.h" // DrawWireBox, FPrimitiveDrawInterface
#include "SEditorViewport.h"
#include "Widgets/ComposableCameraShotViewportCanvasUtils.h"

namespace
{
	void DrawOverlayFill(FCanvas& Canvas, const FBox2D& Rect, FLinearColor Color)
	{
		FCanvasTileItem Tile(Rect.Min, Rect.GetSize(), Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas.DrawItem(Tile);
	}

	void DrawOverlayText(FCanvas& Canvas, UFont* Font, FVector2D Position, const TCHAR* Text,
		FLinearColor Color, float MaxWidth, float TextScale = 1.f)
	{
		if (!Font || MaxWidth <= 0.f) return;
		FCanvasTextStringViewItem Item(Position, FStringView(Text), Font, Color);
		const float Scale = FMath::Min(TextScale, MaxWidth / FMath::Max(1, Font->GetStringSize(Text)));
		Item.Scale = FVector2D(Scale, Scale);
		Canvas.DrawItem(Item); // Draw consumes the view synchronously, while its stack buffer lives.
	}

	struct FShotHudCard
	{
		FCanvas& Canvas;
		TObjectPtr<UFont> Font; // Borrowed for this draw; GEngine owns the font.
		FBox2D Rect;
		const ComposableCameraSystem::ShotViewportOverlay::FHudLayout& Layout;
		int32 Row = 0;
		FShotHudCard(FCanvas& InCanvas, UFont* InFont, const ComposableCameraSystem::ShotViewportOverlay::FHudLayout& InLayout,
			FBox2D InRect, const TCHAR* Title)
			: Canvas(InCanvas), Font(InFont), Rect(InRect), Layout(InLayout)
		{
			if (!Rect.bIsValid) return;
			const float Scale = Layout.Scale;
			DrawOverlayFill(Canvas, Rect, FLinearColor(.012f, .015f, .019f, .88f));
			DrawOverlayFill(Canvas, FBox2D(Rect.Min, FVector2D(Rect.Max.X, Rect.Min.Y + Layout.HeaderHeight * Scale)), FLinearColor(.07f, .08f, .095f, .95f));
			DrawOverlayFill(Canvas, FBox2D(Rect.Min, Rect.Min + FVector2D(2.f, Layout.HeaderHeight) * Scale), FComposableCameraEditorColors::CameraNodeTitle);
			DrawOverlayText(Canvas, Font.Get(), Rect.Min + FVector2D(10.f, 5.f) * Scale, Title, FLinearColor::White, Rect.GetSize().X - 20.f * Scale, Scale);
		}
		void Add(const TCHAR* Label, const TCHAR* Value, FLinearColor Color = FLinearColor(.9f, .92f, .95f))
		{
			using namespace ComposableCameraSystem::ShotViewportOverlay;
			if (Row >= Layout.Rows(Rect)) return;
			const float Scale = Layout.Scale;
			const double Y = Rect.Min.Y + (FHudLayout::HeaderHeight + Row++ * FHudLayout::RowHeight) * Scale;
			if ((Row & 1) == 0) DrawOverlayFill(Canvas, FBox2D(FVector2D(Rect.Min.X, Y), FVector2D(Rect.Max.X, Y + FHudLayout::RowHeight * Scale)), FLinearColor(.1f, .11f, .12f, .15f));
			DrawOverlayText(Canvas, Font.Get(), FVector2D(Rect.Min.X + 10.f * Scale, Y + 2.f * Scale), Label, FLinearColor(.55f, .6f, .65f), 66.f * Scale, Scale);
			DrawOverlayText(Canvas, Font.Get(), FVector2D(Rect.Min.X + 84.f * Scale, Y + 2.f * Scale), Value, Color, Rect.GetSize().X - 94.f * Scale, Scale);
		}
	};

	template <typename T> T* ResolveSourceMesh(AActor* Actor, FName Name)
	{
		if (!Actor) return nullptr;
		if (Name.IsNone()) return Actor->FindComponentByClass<T>();
		for (UActorComponent* Component : Actor->GetComponents())
			if (Component && Component->GetFName() == Name) return Cast<T>(Component);
		return nullptr;
	}
	/** UE's "BasicShapes" cylinder is 100uu x 100uu (XY x Z). To approximate
	 * a 1.7m x 0.34m character capsule, scale (0.7, 0.7, 1.8) -> 70uu wide,
	 * 180uu tall. Close enough as a stand-in for Targets that aren't backed
	 * by a SkeletalMesh / StaticMesh source. */
	const FVector kCapsuleFallbackScale(0.7f, 0.7f, 1.8f);
	const TCHAR* const kCapsuleFallbackMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	// Handle visuals: placement anchor and aim anchor.
	constexpr float kHandleAnchorRadius = 10.f;
	constexpr float kHandleHoverRadiusBoost = 1.3f;
	constexpr float kHandleHitRadius = 14.f;
	const FLinearColor kHandlePlacementColor (1.f, 0.85f, 0.2f, 1.f); // yellow - Placement
	const FLinearColor kHandleAimColor (0.4f, 0.8f, 1.f, 1.f); // cyan - Aim
	const FLinearColor kHandleDisabledColor (0.45f, 0.45f, 0.5f, 0.6f); // greyed out
	const FLinearColor kHandleCrossColor (0.05f, 0.05f, 0.05f, 1.f); // dark cross overlay

	// Zone overlay visuals: Cinemachine-style framing zones.
	//
	// Each enabled `FShotScreenZones` paints two nested rectangles
	// centered on the anchor's authored `ScreenPosition` (NOT on the
	// anchor's projected pixel - the anchor floats inside the zone and
	// its drift relative to ScreenPosition is exactly the visual diagnosis
	// the zone overlay is meant to surface). Rendering uses translucent
	// fills (no border lines): a solid-fill inner rect for the dead zone,
	// plus four soft-zone "ring" tiles between dead and soft.
	//
	// Edges of both rects are LMB drag targets for single-side padding
	// edits. `kZoneEdgeHitThickness` is the hit-grab region perpendicular
	// to the edge - generous so designers can grab a thin edge without
	// pixel-precision aim. Hovered / actively-dragged edges get a thin
	// highlight line as visual feedback (the only line drawn in the zone
	// overlay; the resting state is fills only).
	constexpr float kZoneEdgeHitThickness = 8.f;
	constexpr float kZoneEdgeHighlightThickness = 2.5f;

	const FLinearColor kZoneDeadFillPlacement (1.f, 0.85f, 0.2f, 0.18f); // yellow ~18% - Placement
	const FLinearColor kZoneSoftFillPlacement (1.f, 0.85f, 0.2f, 0.08f); // yellow ~8%
	const FLinearColor kZoneDeadFillAim (0.4f, 0.8f, 1.f, 0.18f); // cyan ~18% - Aim
	const FLinearColor kZoneSoftFillAim (0.4f, 0.8f, 1.f, 0.08f); // cyan ~8%
	const FLinearColor kZoneEdgeHighlightColor(1.f, 1.f, 1.f, 0.85f); // white-ish on hover/drag

	FProperty* ResolveShotEditorProperty(UObject* Host, FComposableCameraShot* Shot)
	{
		if (!Host)
		{
			return nullptr;
		}
		if (UMovieSceneComposableCameraShotSection* Section =
				Cast<UMovieSceneComposableCameraShotSection>(Host))
		{
			if (Shot == &Section->InlineShot)
			{
				return Section->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, InlineShot));
			}
			if (Shot == &Section->ShotOverrides)
			{
				return Section->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMovieSceneComposableCameraShotSection, ShotOverrides));
			}
		}
		return Host->GetClass()->FindPropertyByName(TEXT("Shot"));
	}
}

FComposableCameraShotEditorViewportClient::FComposableCameraShotEditorViewportClient(FPreviewScene* InPreviewScene,
	const TSharedRef<SEditorViewport>& InEditorViewportWidget)
	: FEditorViewportClient(/*InModeTools=*/nullptr, InPreviewScene, InEditorViewportWidget)
	, PreviewScene(InPreviewScene)
{
	// Render camera output without editor-only collision shapes, selection or component helpers.
	SetViewMode(VMI_Lit);
	SetGameView(true);
	ComposableCameraSystem::ShotViewportDisplay::ConfigurePreviewFlags(EngineShowFlags, bUseLevelWorld);

	// Realtime: solver-driven camera needs a tick every frame even without input.
	SetRealtime(true);
	CachedHandles.Reserve(19); // Two anchors, eight edges each, plus one orbit control.

	// Force horizontal-FOV-locked aspect handling so the renderer's projection
	// stays in lockstep with the solver's math. Without this, the renderer
	// reads `ULevelEditorViewportSettings::AspectRatioAxisConstraint` (a global
	// editor preference, often `AspectRatio_MajorAxisFOV` by default) which
	// FLIPS the interpretation of `ViewFOV` between H-FOV and V-FOV when the
	// viewport's aspect crosses 1.0 (taller than wide -> treats ViewFOV as
	// V-FOV; wider than tall->H-FOV). Our solver always treats `ViewFOV` as
	// H-FOV, so the flip causes pivot drift on viewport resize.
	//
	// The override is only honored when `bUseControllingActorViewInfo == true`
	// (per `EditorViewportClient.cpp:1043-1046`), so we have to opt into the
	// "controlling actor" code path. That path uses `ControllingActorViewInfo`
	// (an FMinimalViewInfo) for the projection matrix; ViewLocation / Rotation
	// still flow from `SetViewLocation` / `SetViewRotation` via the shared
	// `ViewTransform` member, so we keep using those. FOV however must be set
	// on `ControllingActorViewInfo.FOV` to participate (see Tick).
	bUseControllingActorViewInfo = true;
	ControllingActorViewInfo.bConstrainAspectRatio = true;
	ControllingActorViewInfo.AspectRatio = GetCameraAspectRatio();
	ControllingActorAspectRatioAxisConstraint = AspectRatio_MaintainXFOV;

	// Post-process injection path. With bUseControllingActorViewInfo=true,
	// the engine routes post-process through
	// `ControllingActorViewInfo.PostProcessSettings` at weight
	// `ControllingActorViewInfo.PostProcessBlendWeight` (default 0 - i.e.,
	// no contribution unless we opt in). The virtual `OverridePostProcessSettings`
	// hook is the OTHER branch (only fires when bUseControllingActorViewInfo
	// is false), so we can't rely on it here. Set blend weight to 1 so our
	// per-frame DoF writes (RunSolverAndDriveCamera) actually take effect.
	ControllingActorViewInfo.PostProcessBlendWeight = 1.f;

	// Sane initial pose so the viewport isn't staring at the world origin
	// before any Shot is bound. Looks at the origin from a 3/4 angle.
	SetViewLocation(FVector(-400.f, -400.f, 200.f));
	SetViewRotation(FRotator(-15.f, 45.f, 0.f));

	// Default FOV - overridden by solver once a Shot is bound. Set both
	// `ViewFOV` (used by stat widgets / culling) and `ControllingActorViewInfo.FOV`
	// (used by the projection matrix because of `bUseControllingActorViewInfo`).
	ViewFOV = 79.f;
	ControllingActorViewInfo.FOV = 79.f;

	// Listener position is meaningless for a non-game preview.
	bSetListenerPosition = false;
}

FComposableCameraShotEditorViewportClient::~FComposableCameraShotEditorViewportClient()
{
	// Note: NOT clearing `Viewport` here - the SShotEditorViewport widget does
	// it in its own destructor BEFORE this destructor fires (see research Q7).
	//
	// Note: NOT calling DestroyProxies() here either - by the time this
	// destructor runs, the owning widget's PreviewScene member may already
	// be dead. The widget's destructor calls ReleaseSceneResources() while
	// the scene is still alive. Reaching this destructor with proxies still
	// in ProxyActors would mean someone built a client without the standard
	// widget host - defensive no-op rather than risk Proxy->Destroy() on a
	// dead world.
	ProxyActors.Reset();
}

void FComposableCameraShotEditorViewportClient::ReleaseSceneResources()
{
	DestroyProxies();
	// Drop ActiveShot too so any straggler tick before the destructor runs
	// (Slate paint between widget destructor and base destructor) can't try
	// to drive a now-orphaned scene.
	ActiveShot = nullptr;
	ActiveHost = nullptr;
	LastRebuiltHost = nullptr;
	bCachedDoFValid = false;

	// D.4: close any in-flight drag transaction. Idempotent - the destructor
	// handles cleanup if the editor was torn down mid-drag.
	DragTransaction.Reset();
	ActiveDragHandleType = EHandleType::None;
	bOrbitDragChanged = false;
	bSubjectDragChanged = false;
	ActiveSubjectHandle = {};
	bActiveDragIsZoneEdge = false;
	ActiveDragZoneIsSoft = false;
	ActiveDragZoneEdgeIndex = -1;
	bHoveredIsZoneEdge = false;
	HoveredZoneIsSoft = false;
	HoveredZoneEdgeIndex = -1;
	CachedHandles.Reset();

	// Same cleanup for any in-flight Alt+RMB Roll drag.
	RollTransaction.Reset();
	bRollDragActive = false;
}

void FComposableCameraShotEditorViewportClient::SetMode(EShotEditorMode InMode)
{
	if (CurrentMode != InMode) { EndDrag(); EndRollDrag(); }
	CurrentMode = InMode;
}

void FComposableCameraShotEditorViewportClient::SetShowDiagnosticHud(bool bInShowDiagnosticHud)
{
	bShowDiagnosticHud = bInShowDiagnosticHud;
	Invalidate(false, false);
}

void FComposableCameraShotEditorViewportClient::SetShowCompositionGuides(bool bInShowCompositionGuides)
{
	bShowCompositionGuides = bInShowCompositionGuides;
	if (!bShowCompositionGuides)
	{
		EndDrag();
		CachedHandles.Reset();
		HoveredHandleType = EHandleType::None;
		bHoveredIsZoneEdge = false;
		HoveredZoneIsSoft = false;
		HoveredZoneEdgeIndex = -1;
	}
	Invalidate(false, false);
}

void FComposableCameraShotEditorViewportClient::SetActiveShot(FComposableCameraShot* InShot, UObject* InHost)
{
	if (ActiveShot != InShot || ActiveHost.Get() != InHost)
	{
		EndDrag();
		EndRollDrag();
		CachedHandles.Reset();
	}
	LastRebuiltHost = nullptr;
	bEffectiveShotCacheValid = false;
	ActiveShot = InShot;
	ActiveHost = InHost;
	if (ActiveShot) CachedHandles.Reserve(19 + 9 * ActiveShot->Targets.Num());

	// Drop the framing-zone prior-pose cache when the bound Shot changes -
	// projecting the new shot's anchors through the previous shot's pose
	// would either NaN the zone math (anchor behind camera) or produce a
	// visible one-frame glitch. The next valid solve hard-seeds a fresh
	// prior. Cleared unconditionally because we have no cheap way to tell
	// "is this the same Shot pointer pointing at semantically-identical
	// data" - a swap-host call with the same backing pointer is the
	// degenerate case where the clear is harmless (next solve re-seeds
	// to the same value).
	bHasCachedPriorPose = false;

	// Don't rebuild here synchronously - Tick() will detect the host change
	// and rebuild on next frame. Avoids reentrancy if SetActiveShot is called
	// from a Slate paint pass.
}

void FComposableCameraShotEditorViewportClient::SetAuthoringSession(TSharedPtr<FComposableCameraShotAuthoringSession> Session)
{
	AuthoringSession = Session;
}

UWorld* FComposableCameraShotEditorViewportClient::GetWorld() const
{
	if (bUseLevelWorld)
	{
		if (const UCineCameraComponent* Camera = OutputCamera.Get()) return Camera->GetWorld();
		if (GEditor) return GEditor->GetEditorWorldContext().World();
	}
	return FEditorViewportClient::GetWorld();
}

float FComposableCameraShotEditorViewportClient::GetCameraAspectRatio() const
{
	if (const auto Session = AuthoringSession.Pin()) return Session->GetPreviewAspectRatio();
	return ComposableCameraSystem::ShotViewportDisplay::CameraAspectRatio(OutputCamera.Get());
}

FIntRect FComposableCameraShotEditorViewportClient::GetRenderRect() const
{
	if (!Viewport) return FIntRect(0, 0, 16, 9);
	const FIntPoint Size = Viewport->GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0) return FIntRect(0, 0, 16, 9);
	return Viewport->CalculateViewExtents(GetCameraAspectRatio(), FIntRect(FIntPoint::ZeroValue, Size));
}

FIntPoint FComposableCameraShotEditorViewportClient::GetRenderSize() const
{
	return GetRenderRect().Size();
}

bool FComposableCameraShotEditorViewportClient::ShowLookAtHandle() const
{
	const auto Session = AuthoringSession.Pin();
	return ActiveShot && ComposableCameraSystem::ShotViewportDisplay::ShowLookAtHandle(*ActiveShot,
		Session && Session->bShowLookAtGuide);
}

bool FComposableCameraShotEditorViewportClient::ShowOrbitControl() const
{
	const auto Session = AuthoringSession.Pin();
	return ActiveShot && ActiveHost.IsValid() && ComposableCameraSystem::ShotViewportDisplay::ShowOrbitControl(*ActiveShot,
		Session && Session->bShowOrbitGuide);
}

bool FComposableCameraShotEditorViewportClient::IsEditingGesture() const
{
	return ActiveDragHandleType != EHandleType::None || bRollDragActive;
}

void FComposableCameraShotEditorViewportClient::NotifyInteractiveEdit()
{
	if (TSharedPtr<FComposableCameraShotAuthoringSession> Session = AuthoringSession.Pin()) Session->Changed();
	bEffectiveShotCacheValid = false;
}

void FComposableCameraShotEditorViewportClient::NotifyCommittedEdit()
{
	bEffectiveShotCacheValid = false;
	const auto Session = AuthoringSession.Pin();
	if (Session && Session->GetShot() == ActiveShot && Session->GetHost() == ActiveHost.Get())
	{
		Session->NotifyViewportValueCommit();
		return;
	}
	// Legacy direct viewport binding still receives the outer property event.
	if (UObject* Host = ActiveHost.Get())
	{
		if (FProperty* Property = ResolveShotEditorProperty(Host, ActiveShot))
		{
			FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
			Host->PostEditChangeProperty(Event);
		}
		Host->MarkPackageDirty();
	}
}

void FComposableCameraShotEditorViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);
	if (TSharedPtr<FComposableCameraShotAuthoringSession> Session = AuthoringSession.Pin())
	{
		OutputCamera = Session->GetOutputCamera();
		if (bUseLevelWorld != Session->bUseLevelWorld)
		{
			bUseLevelWorld = Session->bUseLevelWorld;
			ComposableCameraSystem::ShotViewportDisplay::ConfigurePreviewFlags(EngineShowFlags, bUseLevelWorld);
			DestroyProxies();
			LastRebuiltHost = nullptr;
		}
	}

	// Polish P.2 - invalidate the per-frame `BuildEffectiveShotForPreview`
	// cache at the start of each tick. Subsequent paint-time callers (HUD,
	// 3D wire BBs, handles, hover tooltip, etc.) within this frame share
	// a single override-resolution pass through the cache.
	bEffectiveShotCacheValid = false;
	bEffectiveShotCacheBuiltOk = false;

	// First-frame guard (research Q7) - Viewport is null until after the
	// SEditorViewport::Construct returns and the FSceneViewport is created.
	if (Viewport == nullptr)
	{
		return;
	}

	// Liveness guard: host UObject was destroyed (Type Asset closed, GC swept).
	// Drop the raw Shot pointer to avoid dangling reads.
	if (ActiveShot && !ActiveHost.IsValid())
	{
		if (ActiveDragHandleType != EHandleType::None)
		{
			if (DragTransaction) DragTransaction->Cancel();
			DragTransaction.Reset();
			ActiveDragHandleType = EHandleType::None;
			bOrbitDragChanged = false;
			bSubjectDragChanged = false;
			ActiveSubjectHandle = {};
		}
		if (RollTransaction) RollTransaction->Cancel();
		RollTransaction.Reset();
		bRollDragActive = false;
		ActiveShot = nullptr;
		DestroyProxies();
		LastRebuiltHost = nullptr;
		bCachedDoFValid = false;
		return;
	}

	if (!ActiveShot)
	{
		// No Shot to drive camera with - leave camera where the user (or
		// the default ctor pose) put it. Proxies were cleared on detach.
		bCachedDoFValid = false;
		return;
	}
	if (const auto Session = AuthoringSession.Pin(); Session && !Session->CanEdit())
	{
		// Preserve values authored before locking, but stop every captured writer.
		EndDrag();
		EndRollDrag();
	}
	if (ActiveDragHandleType == EHandleType::OrbitDirection)
	{
		const auto Session = AuthoringSession.Pin();
		if (!ShowOrbitControl() || !bShowCompositionGuides || CurrentMode != EShotEditorMode::Drag || (Session && !Session->CanEdit())) EndDrag();
	}
	if (ActiveDragHandleType == EHandleType::SubjectOffsetAxis || ActiveDragHandleType == EHandleType::SubjectBoundsFace)
	{
		const auto Session = AuthoringSession.Pin();
		if (!IsSubjectHandleValid(ActiveSubjectHandle) || !bShowCompositionGuides || CurrentMode != EShotEditorMode::Drag || (Session && !Session->CanEdit())) EndDrag();
	}
	// Capacity grows only after a structural subject-count change; painting never reallocates the hit cache.
	if (CachedHandles.Max() < 19 + 9 * ActiveShot->Targets.Num()) CachedHandles.Reserve(19 + 9 * ActiveShot->Targets.Num());
	if (bUseLevelWorld)
	{
		RunSolverAndDriveCamera(DeltaSeconds);
		return;
	}

	// Rebuild proxies on host change OR Targets count drift OR per-target
	// source actor / preview mesh identity change. The third check catches the
	// "designer just picked an Actor for an existing Target" path - count
	// is unchanged and host is unchanged, but the cylinder fallback proxy
	// from the null-actor moment needs to upgrade to SkelMesh / StaticMesh.
	const bool bHostChanged = (LastRebuiltHost != ActiveHost);
	const bool bCountDrift = (ProxyActors.Num() != ActiveShot->Targets.Num());
	bool bSourceDrift = false;
	if (!bHostChanged && !bCountDrift)
	{
		const int32 N = ActiveShot->Targets.Num();
		if (LastResolvedSources.Num() != N || LastResolvedPreviewMeshes.Num() != N || LastResolvedComponentNames.Num() != N)
		{
			bSourceDrift = true;
		}
		else
		{
			for (int32 i = 0; i < N; ++i)
			{
				const AActor* Now = ResolveSourceActorForTargetIndex(i);
				const AActor* Then = LastResolvedSources[i].Get();
				const USkeletalMesh* PreviewNow = ResolvePreviewMeshForTargetIndex(i);
				const USkeletalMesh* PreviewThen = LastResolvedPreviewMeshes[i].Get();
				if (Now != Then || PreviewNow != PreviewThen || LastResolvedComponentNames[i] != ActiveShot->Targets[i].Target.ComponentName)
				{
					bSourceDrift = true;
					break;
				}
			}
		}
	}
	if (bHostChanged || bCountDrift || bSourceDrift)
	{
		RebuildProxies();
	}

	SyncProxyTransforms();

	// Solver runs in all modes - lens parameters (FOV / Aperture /
	// FocusDistance) are always Shot-data-driven so designer's lens edits in
	// the Details panel take effect regardless of mode. Camera pose is mode-
	// specific: Drag honors solver pose; Free preserves user camera pose.
	RunSolverAndDriveCamera(DeltaSeconds);
}

void FComposableCameraShotEditorViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);

	if (!ActiveShot || !ActiveHost.IsValid())
	{
		CachedHandles.Reset();
		return;
	}
	if (bShowDiagnosticHud) DrawDiagnosticHud(Canvas);

	// D.4: draw screen-position handles on top of the HUD overlay.
	if (bShowCompositionGuides)
	{
		DrawHandles(InViewport, Canvas);
		DrawSubjectHandles(Canvas);
		if (ShowOrbitControl()) DrawOrbitControl(Canvas);
	}
	else
	{
		CachedHandles.Reset();
	}
}

void FComposableCameraShotEditorViewportClient::DrawDiagnosticHud(FCanvas& Canvas)
{
	using namespace ComposableCameraSystem::ShotViewportOverlay;
	using namespace ComposableCameraSystem::ShotEditorCanvas;
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font) return;
	const FIntRect RenderRect = GetRenderRect();
	const float Scale = ValidScale(Canvas.GetDPIScale());
	const FBox2D Image(ViewportToCanvas(FVector2D(RenderRect.Min), Scale), ViewportToCanvas(FVector2D(RenderRect.Max), Scale));
	const FHudLayout Layout = HudLayout(Image);
	if (!Layout.Camera.bIsValid) return;

	FShotHudCard Camera(Canvas, Font, Layout, Layout.Camera, TEXT("CAMERA"));
	FShotHudCard Composition(Canvas, Font, Layout, Layout.Composition, TEXT("COMPOSITION"));
	TCHAR Value[160];
	const FVector Position = GetViewLocation();
	const FRotator Rotation = GetViewRotation();
	Camera.Add(TEXT("View"), CurrentMode == EShotEditorMode::Drag ? TEXT("Compose") : TEXT("Inspect"));
	FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("%d x %d  |  %.3f"), RenderRect.Width(), RenderRect.Height(), GetCameraAspectRatio());
	Camera.Add(TEXT("Frame"), Value);
	FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("%.1f, %.1f, %.1f cm"), Position.X, Position.Y, Position.Z);
	Camera.Add(TEXT("Position"), Value);
	FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("P %.1f  Y %.1f  R %.1f"), Rotation.Pitch, Rotation.Yaw, Rotation.Roll);
	Camera.Add(TEXT("Rotation"), Value);
	if (ActiveShot->Lens.FOVMode == EShotFOVMode::Manual && bHasCachedPriorPose && !FMath::IsNearlyEqual(ActiveShot->Lens.ManualFOV, ViewFOV, .05f))
		FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("%.1f -> %.1f deg"), ActiveShot->Lens.ManualFOV, ViewFOV);
	else FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("%.1f deg"), ViewFOV);
	Camera.Add(TEXT("FOV"), Value);
	FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("%.1f cm"), CachedFocusDistance);
	Camera.Add(TEXT("Focus"), bCachedDoFValid ? Value : TEXT("Not driven"));
	FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("f/%.2f"), CachedAperture);
	Camera.Add(TEXT("Aperture"), bCachedDoFValid ? Value : TEXT("Not driven"));
	if (bHasCachedPriorPose && CachedPriorRoll != TNumericLimits<float>::Max() && FMath::Abs(FRotator::NormalizeAxis(ActiveShot->Roll - CachedPriorRoll)) >= .05f)
		FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("%.1f -> %.1f deg"), ActiveShot->Roll, CachedPriorRoll);
	else FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("%.1f deg"), ActiveShot->Roll);
	Camera.Add(TEXT("Roll"), Value);

	const FShotPlacement& Placement = ActiveShot->Placement;
	Composition.Add(TEXT("Follow"), Placement.Mode == EShotPlacementMode::AnchorOrbit ? TEXT("Anchor Orbit") : Placement.Mode == EShotPlacementMode::AnchorAtScreen ? TEXT("Anchor At Screen") : TEXT("Fixed World Position"));
	if (bHasCachedPriorPose && CachedPriorDistance > 0.f && !FMath::IsNearlyEqual(Placement.Distance, CachedPriorDistance, .05f))
		FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("%.1f -> %.1f cm"), Placement.Distance, CachedPriorDistance);
	else FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("%.1f cm"), Placement.Distance);
	Composition.Add(TEXT("Distance"), Placement.Mode == EShotPlacementMode::FixedWorldPosition ? TEXT("Unused") : Value);

	FVector AnchorWorld = FVector::ZeroVector;
	const bool bAimUsed = ActiveShot->Aim.Mode == EShotAimMode::LookAtAnchor;
	const bool bAnchorOK = bAimUsed && EnsureEffectiveShotCache()
		&& CachedEffectiveShot.Aim.AimAnchor.ResolveWorldPosition(CachedEffectiveShot.Targets, AnchorWorld);
	FVector2D Projected = FVector2D::ZeroVector;
	const bool bInFront = bAnchorOK && ComposableCameraSystem::ProjectWorldPointToScreen(AnchorWorld, Position, Rotation,
		FMath::Tan(FMath::DegreesToRadians(ViewFOV * .5f)), GetCameraAspectRatio(), Projected);
	const FLinearColor StateColor = !bAimUsed || bInFront ? FComposableCameraEditorColors::CameraNodeTitle : FLinearColor(1.f, .45f, .25f);
	Composition.Add(TEXT("Aim"), !bAimUsed ? TEXT("Disabled") : !bAnchorOK ? TEXT("Unresolved") : !bInFront ? TEXT("Behind camera") : TEXT("Resolved"), StateColor);
	FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("%.3f, %.3f"), ActiveShot->Aim.ScreenPosition.X, ActiveShot->Aim.ScreenPosition.Y);
	Composition.Add(TEXT("Target"), bAimUsed ? Value : TEXT("Unused"));
	FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("%.3f, %.3f"), Projected.X, Projected.Y);
	Composition.Add(TEXT("Projected"), bInFront ? Value : TEXT("--"));
	const FVector2D Drift = Projected - ActiveShot->Aim.ScreenPosition;
	FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("%.3f, %.3f"), Drift.X, Drift.Y);
	Composition.Add(TEXT("Drift"), bInFront ? Value : TEXT("--"));
	Composition.Add(TEXT("Basis"), Placement.Mode != EShotPlacementMode::AnchorOrbit ? TEXT("Unused")
		: Placement.BasisFrame == EShotPlacementBasisFrame::World ? TEXT("World")
		: Placement.BasisFrame == EShotPlacementBasisFrame::InheritFromActor ? TEXT("Subject") : TEXT("Two Subjects"));
	FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("Y %.1f  P %.1f deg"), Placement.LocalCameraDirection.X, Placement.LocalCameraDirection.Y);
	Composition.Add(TEXT("Orbit"), Placement.Mode == EShotPlacementMode::AnchorOrbit ? Value : TEXT("Unused"));
	FCString::Snprintf(Value, UE_ARRAY_COUNT(Value), TEXT("%.1f, %.1f, %.1f cm"), AnchorWorld.X, AnchorWorld.Y, AnchorWorld.Z);
	Composition.Add(TEXT("Aim World"), bAnchorOK ? Value : TEXT("--"));
}

void FComposableCameraShotEditorViewportClient::DrawOrbitControl(FCanvas& Canvas)
{
	using namespace ComposableCameraSystem::ShotViewportOverlay;
	using namespace ComposableCameraSystem::ShotEditorCanvas;
	const FIntRect RenderRect = GetRenderRect();
	const float Scale = ValidScale(Canvas.GetDPIScale());
	const FBox2D Image(ViewportToCanvas(FVector2D(RenderRect.Min), Scale), ViewportToCanvas(FVector2D(RenderRect.Max), Scale));
	const FOrbitControlLayout Layout = OrbitLayout(Image);
	if (!Layout.IsVisible()) return;
	OrbitControlDpiScale = Scale;
	const auto Session = AuthoringSession.Pin();
	const bool bInteractive = CurrentMode == EShotEditorMode::Drag && (!Session || Session->CanEdit());
	const bool bHighlighted = bInteractive && (HoveredHandleType == EHandleType::OrbitDirection || ActiveDragHandleType == EHandleType::OrbitDirection);
	const FLinearColor Accent = bInteractive ? FComposableCameraEditorColors::CameraNodeTitle : FLinearColor(.4f, .43f, .46f);
	DrawOverlayFill(Canvas, Layout.Panel, FLinearColor(.015f, .02f, .025f, .85f));
	DrawOverlayFill(Canvas, FBox2D(Layout.Panel.Min, FVector2D(Layout.Panel.Max.X, Layout.Panel.Min.Y + 22.f)), FLinearColor(.07f, .08f, .095f, .95f));
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	DrawOverlayText(Canvas, Font, Layout.Panel.Min + FVector2D(10.f, 4.f), TEXT("ORBIT  /  LOCAL BASIS"), FLinearColor(.8f, .84f, .88f), 144.f);
	FCanvasNGonItem Disc(Layout.Center, FVector2D(Layout.Radius, Layout.Radius), 36, FLinearColor(.04f, .05f, .065f, 1.f));
	Canvas.DrawItem(Disc);
	const auto Project = [&](const FVector& Point) { return Layout.Center + FVector2D(Point.Y, -Point.Z) * Layout.Radius; };
	const auto GridSegment = [&](const FVector& A, const FVector& B)
	{
		FCanvasLineItem Line(Project(A), Project(B));
		const bool bFront = A.X + B.X >= 0.f;
		Line.SetColor(bFront ? FLinearColor(.4f, .46f, .52f, bHighlighted ? .9f : .65f) : FLinearColor(.2f, .24f, .28f, .35f));
		Line.LineThickness = bFront ? 1.f : .5f;
		Canvas.DrawItem(Line);
	};
	// Latitude circles and longitude great circles. Fixed segment counts, no transient arrays.
	for (int32 Latitude = -60; Latitude <= 60; Latitude += 30)
	{
		FVector Previous = OrbitGlobePoint(FVector2D(0.f, Latitude));
		for (int32 Step = 1; Step <= 24; ++Step)
		{
			const FVector Point = OrbitGlobePoint(FVector2D(Step * 15.f, Latitude));
			GridSegment(Previous, Point); Previous = Point;
		}
	}
	for (int32 Longitude = 0; Longitude < 180; Longitude += 30)
	{
		FVector Previous = OrbitGlobePoint(FVector2D(Longitude, 0.f));
		for (int32 Step = 1; Step <= 24; ++Step)
		{
			const FVector Point = OrbitGlobePoint(FVector2D(Longitude, Step * 15.f));
			GridSegment(Previous, Point); Previous = Point;
		}
	}
	FVector2D Previous = Layout.Center + FVector2D(Layout.Radius, 0.f);
	for (int32 Step = 1; Step <= 36; ++Step)
	{
		const float Angle = Step * (2.f * PI / 36.f);
		const FVector2D Point = Layout.Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Layout.Radius;
		FCanvasLineItem Rim(Previous, Point); Rim.SetColor(Accent); Rim.LineThickness = bHighlighted ? 2.f : 1.f;
		Canvas.DrawItem(Rim); Previous = Point;
	}
	const FVector CameraPoint = OrbitGlobePoint(ActiveShot->Placement.LocalCameraDirection);
	const FVector2D Marker = Project(CameraPoint);
	FCanvasLineItem Radial(Layout.Center, Marker); Radial.SetColor(Accent); Radial.LineThickness = 1.5f; Canvas.DrawItem(Radial);
	FCanvasNGonItem Dot(Marker, FVector2D(4.f, 4.f), 12, CameraPoint.X >= 0.f ? Accent : FLinearColor(.6f, .64f, .68f));
	Canvas.DrawItem(Dot);
	TCHAR Angles[80];
	FCString::Snprintf(Angles, UE_ARRAY_COUNT(Angles), TEXT("Y %.1f   P %.1f"), ActiveShot->Placement.LocalCameraDirection.X, ActiveShot->Placement.LocalCameraDirection.Y);
	DrawOverlayText(Canvas, Font, Layout.Panel.Min + FVector2D(12.f, 113.f), Angles, FLinearColor(.9f, .93f, .95f), 140.f);
	const TCHAR* Hint = bInteractive ? TEXT("Drag orbit  |  Wheel distance")
		: Session && !Session->CanEdit() ? TEXT("Read-only source") : TEXT("Compose mode to edit");
	DrawOverlayText(Canvas, Font, Layout.Panel.Min + FVector2D(12.f, 128.f), Hint, FLinearColor(.5f, .56f, .61f), 140.f);
	if (bInteractive)
	{
		FHandleScreenPosCache Hit;
		Hit.Type = EHandleType::OrbitDirection;
		Hit.PixelPos = CanvasToViewport(Layout.Center, Scale);
		Hit.HitArea = CanvasHitAreaToViewport(FBox2D(Layout.Center - FVector2D(Layout.Radius, Layout.Radius), Layout.Center + FVector2D(Layout.Radius, Layout.Radius)), Scale);
		CachedHandles.Add(Hit);
	}
}

bool FComposableCameraShotEditorViewportClient::ShowSubjectGuides() const
{
	const auto Session = AuthoringSession.Pin();
	return ActiveShot && ActiveHost.IsValid() && Session && Session->bShowSubjectGuide;
}

bool FComposableCameraShotEditorViewportClient::IsSubjectHandleValid(const FHandleScreenPosCache& H) const
{
	if (!ShowSubjectGuides() || !ActiveShot->Targets.IsValidIndex(H.SubjectIndex)
		|| H.SubjectCount != ActiveShot->Targets.Num() || H.AxisIndex < 0 || H.AxisIndex > 2
		|| H.SubjectRenderRect != GetRenderRect()
		|| !ComposableCameraSystem::ShotSubjectGizmo::CanDrag(H.PixelsPerUnit)) return false;
	const auto& Subject = ActiveShot->Targets[H.SubjectIndex];
	if (!Subject.Target.Offset.Equals(H.AuthoredOffset) || !Subject.ManualBoundsExtent.Equals(H.AuthoredExtent)
		|| !H.SubjectIdentity.Matches(Subject.Target) || !EnsureEffectiveShotCache()
		|| !CachedEffectiveShot.Targets.IsValidIndex(H.SubjectIndex)
		|| H.ResolvedSubjectActor.Get() != CachedEffectiveShot.Targets[H.SubjectIndex].Target.Actor.Get()) return false;
	FTransform Pivot;
	if (!CachedEffectiveShot.Targets[H.SubjectIndex].Target.ResolvePivotTransform(Pivot)) return false;
	return H.Type == EHandleType::SubjectOffsetAxis
		|| (H.Type == EHandleType::SubjectBoundsFace && Subject.BoundsShape == EShotTargetBoundsShape::ManualExtent);
}

bool FComposableCameraShotEditorViewportClient::ProjectWorldToViewport(const FVector& World, FVector2D& Pixel) const
{
	FVector2D Screen;
	if (!ComposableCameraSystem::ProjectWorldPointToScreen(World, GetViewLocation(), GetViewRotation(),
		FMath::Tan(FMath::DegreesToRadians(ViewFOV * .5f)), GetCameraAspectRatio(), Screen)) return false;
	Pixel = NormalizedScreenToPixel(Screen, GetRenderSize());
	return !Pixel.ContainsNaN();
}

void FComposableCameraShotEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);
	DrawSubjectGuides(PDI);
}

void FComposableCameraShotEditorViewportClient::DrawSubjectGuides(FPrimitiveDrawInterface* PDI)
{
	if (!bShowCompositionGuides || !ActiveShot || !ActiveHost.IsValid() || !PDI || !Viewport) return;
	const bool bSubjectPage = ShowSubjectGuides();
	if (!bSubjectPage && ActiveShot->Lens.FOVMode != EShotFOVMode::SolvedFromBoundsFit) return;
	if (!EnsureEffectiveShotCache()) return;
	// Borrow the editor's resolved value cache; bounds refresh never touches authored/runtime storage.
	for (auto& Target : CachedEffectiveShot.Targets)
	{
		ComposableCameraSystem::ShotSubjectGizmo::FSubjectGeometry Geometry;
		if (!ComposableCameraSystem::ShotSubjectGizmo::Resolve(Target.Target, Geometry)) continue;
		if (bSubjectPage)
		{
			const FLinearColor PivotColor(.08f, .8f, .72f);
			PDI->DrawPoint(Geometry.Base, Geometry.bUsedBone ? FLinearColor(1.f, .65f, .2f) : FLinearColor::White, 6.f, SDPG_Foreground);
			PDI->DrawPoint(Geometry.Pivot, PivotColor, 9.f, SDPG_Foreground);
			PDI->DrawLine(Geometry.Base, Geometry.Pivot, PivotColor, SDPG_Foreground, 1.5f);
			// Short triad = placement heading. Solid offset axes/handles are drawn on Canvas.
			if (Geometry.bHasBasis)
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					const FLinearColor Color = Axis == 0 ? FLinearColor(.8f, .3f, .3f) : Axis == 1 ? FLinearColor(.3f, .8f, .3f) : FLinearColor(.3f, .45f, 1.f);
					const FVector Direction = Geometry.BasisRotation.RotateVector(ComposableCameraSystem::ShotSubjectGizmo::Axis(Axis));
					PDI->DrawLine(Geometry.Base, Geometry.Base + Direction * 12., Color, SDPG_Foreground, .8f);
				}
		}
		if (Target.BoundsShape == EShotTargetBoundsShape::AutoFromComponentBounds) Target.RefreshAutoBoundsCache();
		if (!ComposableCameraSystem::ShotSubjectGizmo::HasBounds(Target)) continue;
		const bool bConsumed = CachedEffectiveShot.Lens.FOVMode == EShotFOVMode::SolvedFromBoundsFit && Target.BoundsContributionWeight > 0.f;
		if (!bSubjectPage && !bConsumed) continue;
		const FBox Box(Geometry.Pivot - Target.GetEffectiveBoundsExtent(), Geometry.Pivot + Target.GetEffectiveBoundsExtent());
		bool bAllInFront = true;
		FVector Vertices[8]; Box.GetVertices(Vertices);
		for (const auto& Vertex : Vertices)
		{
			FVector2D Pixel;
			if (!ProjectWorldToViewport(Vertex, Pixel)) { bAllInFront = false; break; }
		}
		const FLinearColor Color = !bConsumed ? FLinearColor(.5f, .55f, .6f)
			: bAllInFront ? FLinearColor(.2f, .9f, .3f) : FLinearColor(1.f, .85f, .2f);
		DrawWireBox(PDI, Box, Color, bSubjectPage ? SDPG_Foreground : SDPG_World, 1.5f);
	}
}

void FComposableCameraShotEditorViewportClient::DrawSubjectHandles(FCanvas& Canvas)
{
	if (!ShowSubjectGuides() || !EnsureEffectiveShotCache()) return;
	using namespace ComposableCameraSystem::ShotSubjectGizmo;
	using namespace ComposableCameraSystem::ShotEditorCanvas;
	const float Scale = ValidScale(Canvas.GetDPIScale());
	const FIntRect RenderRect = GetRenderRect();
	if (RenderRect.Width() <= 0 || RenderRect.Height() <= 0) return;
	const FBox2D Image(FVector2D(RenderRect.Min), FVector2D(RenderRect.Max));
	const FBox2D CanvasImage(ViewportToCanvas(Image.Min, Scale), ViewportToCanvas(Image.Max, Scale));
	const auto Session = AuthoringSession.Pin();
	const bool bEditable = CurrentMode == EShotEditorMode::Drag && Session && Session->CanEdit();
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const auto DrawLabel = [&](FVector2D Position, const TCHAR* Text, FLinearColor Color, float Width)
	{
		const double Margin = 2.;
		const double Height = Font ? Font->GetMaxCharHeight() : 0.;
		if (CanvasImage.GetSize().X < 2. * Margin || CanvasImage.GetSize().Y < Height + 2. * Margin) return;
		Position.X = FMath::Clamp(Position.X, CanvasImage.Min.X + Margin, CanvasImage.Max.X - Margin);
		Position.Y = FMath::Clamp(Position.Y, CanvasImage.Min.Y + Margin, CanvasImage.Max.Y - Height - Margin);
		DrawOverlayText(Canvas, Font, Position, Text, Color, FMath::Min(Width, float(CanvasImage.Max.X - Position.X - Margin)));
	};
	for (int32 Index = 0; Index < CachedEffectiveShot.Targets.Num(); ++Index)
	{
		const auto& Subject = CachedEffectiveShot.Targets[Index];
		if (!ActiveShot->Targets.IsValidIndex(Index)) continue;
		FSubjectGeometry Geometry;
		if (!Resolve(Subject.Target, Geometry)) continue;
		FVector2D PivotPixel;
		if (!ProjectWorldToViewport(Geometry.Pivot, PivotPixel)) continue;
		TCHAR NumberedLabel[32] = {};
		if (Index >= 2) FCString::Snprintf(NumberedLabel, UE_ARRAY_COUNT(NumberedLabel), TEXT("Subject %d"), Index + 1);
		const TCHAR* Label = Index == 0 ? TEXT("Subject A") : Index == 1 ? TEXT("Subject B") : NumberedLabel;
		if (Image.IsInside(PivotPixel))
			DrawLabel(ViewportToCanvas(PivotPixel, Scale) + FVector2D(8.f, -20.f), Label, FLinearColor::White, 96.f);
		// Approximate constant 36-Slate-unit axis length, stable across camera distance and editor DPI.
		const double Depth = GetViewRotation().Quaternion().UnrotateVector(Geometry.Pivot - GetViewLocation()).X;
		const double AxisLength = FMath::Max(1., Depth * 2. * FMath::Tan(FMath::DegreesToRadians(ViewFOV * .5f))
			* 36. * Scale / FMath::Max(1, RenderRect.Width()));
		const auto AddHandle = [&](EHandleType Type, int32 AxisIndex, float Sign, const FVector& Point, const FVector& Direction, FLinearColor Color)
		{
			FVector2D Pixel, AxisPixel;
			if (!ProjectWorldToViewport(Point, Pixel) || !ProjectWorldToViewport(Point + Direction * AxisLength, AxisPixel)
				|| !Image.IsInside(Pixel)) return;
			const FVector2D PixelsPerUnit = (AxisPixel - Pixel) / AxisLength;
			const bool bDraggable = bEditable && CanDrag(PixelsPerUnit);
			const bool bHover = HoveredHandleType == Type && HoveredSubjectIndex == Index
				&& HoveredSubjectAxis == AxisIndex && HoveredSubjectFaceSign == Sign;
			const bool bActive = ActiveDragHandleType == Type && ActiveSubjectHandle.SubjectIndex == Index
				&& ActiveSubjectHandle.AxisIndex == AxisIndex && ActiveSubjectHandle.FaceSign == Sign;
			if (!bDraggable) Color = kHandleDisabledColor;
			const FVector2D Center = ViewportToCanvas(Pixel, Scale);
			const float Radius = bHover || bActive ? 6.f : 4.f;
			if (Type == EHandleType::SubjectOffsetAxis)
			{
				FCanvasLineItem AxisLine(ViewportToCanvas(PivotPixel, Scale), Center);
				AxisLine.SetColor(Color); AxisLine.LineThickness = bHover || bActive ? 2.5f : 1.5f; Canvas.DrawItem(AxisLine);
				const TCHAR* Name = AxisIndex == 0 ? TEXT("X") : AxisIndex == 1 ? TEXT("Y") : TEXT("Z");
				DrawLabel(Center + FVector2D(6.f, -12.f), Name, Color, 20.f);
			}
			FCanvasNGonItem Disc(Center, FVector2D(Radius), 12, Color); Canvas.DrawItem(Disc);
			if (!bDraggable || CachedHandles.Num() >= CachedHandles.Max()) return;
			FHandleScreenPosCache Hit;
			Hit.Type = Type; Hit.SubjectIndex = Index; Hit.SubjectCount = ActiveShot->Targets.Num();
			Hit.AxisIndex = AxisIndex; Hit.FaceSign = Sign; Hit.PixelPos = Pixel; Hit.PixelsPerUnit = PixelsPerUnit;
			Hit.SubjectRenderRect = RenderRect;
			Hit.SubjectIdentity.Capture(ActiveShot->Targets[Index].Target);
			Hit.ResolvedSubjectActor = Subject.Target.Actor.Get();
			Hit.AuthoredOffset = ActiveShot->Targets[Index].Target.Offset;
			Hit.AuthoredExtent = ActiveShot->Targets[Index].ManualBoundsExtent;
			Hit.HitArea = FBox2D(FVector2D(FMath::Max(double(RenderRect.Min.X), Pixel.X - 8. * Scale), FMath::Max(double(RenderRect.Min.Y), Pixel.Y - 8. * Scale)),
				FVector2D(FMath::Min(double(RenderRect.Max.X), Pixel.X + 8. * Scale), FMath::Min(double(RenderRect.Max.Y), Pixel.Y + 8. * Scale)));
			CachedHandles.Add(Hit);
		};
		// Bounds faces first: offset axis endpoints take priority when controls overlap.
		if (Subject.BoundsShape == EShotTargetBoundsShape::ManualExtent)
			for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
				for (const float Sign : { -1.f, 1.f })
				{
					const FVector Direction = Axis(AxisIndex);
					AddHandle(EHandleType::SubjectBoundsFace, AxisIndex, Sign,
						Geometry.Pivot + Direction * FMath::Max(0., Subject.ManualBoundsExtent[AxisIndex]) * Sign,
						Direction, FLinearColor(1.f, .75f, .25f));
				}
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			const FVector Direction = Geometry.OffsetRotation.RotateVector(Axis(AxisIndex));
			const FLinearColor Color = AxisIndex == 0 ? FLinearColor(1.f, .25f, .25f) : AxisIndex == 1 ? FLinearColor(.25f, 1.f, .25f) : FLinearColor(.3f, .5f, 1.f);
			AddHandle(EHandleType::SubjectOffsetAxis, AxisIndex, 1.f, Geometry.Pivot + Direction * AxisLength, Direction, Color);
		}
	}
}

// D.4 implementation

FVector2D FComposableCameraShotEditorViewportClient::NormalizedScreenToPixel(const FVector2D& ScreenPos, const FIntPoint&) const
{
	// Solver convention: ScreenPos in [-0.5, 0.5], +Y up.
	// Pixel convention: origin top-left, +Y down.
	const FIntRect Rect = GetRenderRect();
	const FIntPoint Size = Rect.Size();
	const FIntPoint Origin = Rect.Min;
	return FVector2D(Origin.X + (ScreenPos.X + 0.5f) * Size.X,
		Origin.Y + (0.5f - ScreenPos.Y) * Size.Y);
}

FVector2D FComposableCameraShotEditorViewportClient::PixelToNormalizedScreen(int32 PixelX, int32 PixelY, const FIntPoint&) const
{
	const FIntRect Rect = GetRenderRect();
	const FIntPoint Size = Rect.Size();
	const FIntPoint Origin = Rect.Min;
	const float W = static_cast<float>(FMath::Max(Size.X, 1));
	const float H = static_cast<float>(FMath::Max(Size.Y, 1));
	return FVector2D(static_cast<float>(PixelX - Origin.X) / W - 0.5f,
		0.5f - static_cast<float>(PixelY - Origin.Y) / H);
}

void FComposableCameraShotEditorViewportClient::DrawHandles(FViewport& InViewport, FCanvas& Canvas)
{
	CachedHandles.Reset();
	if (!ActiveShot)
	{
		return;
	}

	const FIntPoint VPSize = GetRenderSize();
	using namespace ComposableCameraSystem::ShotEditorCanvas;
	const float CanvasScale = ValidScale(Canvas.GetDPIScale());
	const float CanvasWidth = static_cast<float>(VPSize.X) / CanvasScale;
	const float CanvasHeight = static_cast<float>(VPSize.Y) / CanvasScale;
	const auto ScreenToCanvas = [&](const FVector2D& ScreenPosition)
	{
		return ViewportToCanvas(NormalizedScreenToPixel(ScreenPosition, VPSize), CanvasScale);
	};

	// Per-mode drawing rules:
	// - All modes draw handles at the LIVE PROJECTION of each anchor's
	// resolved world point through the current camera. This makes the
	// handle "follow the anchor's actor" - changing
	// `Placement.PlacementAnchor.TargetIndex` visibly moves the yellow
	// handle to wherever the new target projects, even before the
	// designer drags it.
	// - Exception: while a handle is *actively being dragged*, it
	// follows the cursor (which equals the just-written
	// `Placement.ScreenPosition` / `Aim.ScreenPosition`) so the drag
	// UX is responsive without a one-frame lag.
	// - Drag mode: full color, hit-tested. Free: greyed out,
	// non-interactive.
	const TSharedPtr<FComposableCameraShotAuthoringSession> Session = AuthoringSession.Pin();
	const bool bInteractive = CurrentMode == EShotEditorMode::Drag && (!Session || Session->CanEdit());
	const bool bDisabled = !bInteractive;

	// Only draw screen controls consumed by the current solver mode.
	const bool bShowAim = ShowLookAtHandle();
	const bool bShowPlacement = ComposableCameraSystem::ShotViewportDisplay::ShowFollowHandle(*ActiveShot);
	const bool bAimDisabled = bDisabled;
	const bool bAimInteractive = bInteractive && bShowAim;
	const bool bPlacementDisabled = bDisabled;
	const bool bPlacementInteractive = bInteractive && bShowPlacement;

	UFont* HandleLabelFont = GEngine ? GEngine->GetSmallFont() : nullptr;

	// Pre-compute camera state for live projection.
	const FVector CamPos = GetViewLocation();
	const FRotator CamRot = GetViewRotation();
	const float LiveAspect = GetCameraAspectRatio();
	const float TanHalfHOR = FMath::Tan(FMath::DegreesToRadians(ViewFOV * 0.5f));

	// Helper: draw filled circle + cross overlay at a given pixel pos.
	auto DrawHandleAt = [&Canvas](const FVector2D& PixelPos, float Radius, const FLinearColor& Fill, bool bWithCross)
	{
		FCanvasNGonItem Disc(PixelPos, FVector2D(Radius, Radius), 24, Fill);
		Canvas.DrawItem(Disc);

		if (bWithCross)
		{
			const float CrossArm = Radius * 0.55f;
			FCanvasLineItem H(FVector2D(PixelPos.X - CrossArm, PixelPos.Y),
				FVector2D(PixelPos.X + CrossArm, PixelPos.Y));
			H.SetColor(kHandleCrossColor);
			H.LineThickness = 1.5f;
			Canvas.DrawItem(H);

			FCanvasLineItem V(FVector2D(PixelPos.X, PixelPos.Y - CrossArm),
				FVector2D(PixelPos.X, PixelPos.Y + CrossArm));
			V.SetColor(kHandleCrossColor);
			V.LineThickness = 1.5f;
			Canvas.DrawItem(V);
		}
	};

	// Project the resolved anchor world point through the current camera.
	// `AnchorAccessor` extracts the relevant anchor from the override-
	// resolved EffectiveShot. Returns false (OutNorm untouched) when the
	// anchor can't resolve or the world point is behind camera - caller
	// falls back to the authored ScreenPosition in that case so the
	// handle stays visible during transient unresolvable states.
	auto ProjectAnchorWorld = [&](auto AnchorAccessor, FVector2D& OutNorm) -> bool
	{
		FComposableCameraShot EffectiveShot;
		FVector AnchorWorld;
		if (!BuildEffectiveShotForPreview(EffectiveShot))
		{
			return false;
		}
		const FComposableCameraAnchorSpec& Anchor = AnchorAccessor(EffectiveShot);
		if (!Anchor.ResolveWorldPosition(EffectiveShot.Targets, AnchorWorld))
		{
			return false;
		}
		FVector2D Projected;
		if (!ComposableCameraSystem::ProjectWorldPointToScreen(AnchorWorld, CamPos, CamRot, TanHalfHOR, LiveAspect, Projected))
		{
			return false;
		}
		OutNorm = Projected;
		return true;
	};

	auto DrawAnchorHandle = [&](EHandleType HandleType,
		const FVector2D& AuthoredScreenPos,
		auto AnchorAccessor,
		const FLinearColor& AnchorColor,
		const TCHAR* LabelText,
		bool bHandleDisabled,
		bool bHandleInteractive)
	{
		// The disc represents the AUTHORED ScreenPosition - the designer's
		// "I want the anchor here" target - so it always
		// renders at `AuthoredScreenPos`, regardless of whether zones
		// are on or where the anchor currently projects. Drag mutates
		// this same field, so the disc naturally tracks the cursor 1:1.
		// The anchor's actual projected position (which can drift away
		// from SP inside the dead zone) is shown as a separate read-only
		// marker further down - only when zones are enabled, because
		// zones-off keeps projection SP and the marker would just
		// double-stamp the disc.
		const FVector2D NormPos = AuthoredScreenPos;
		const FVector2D PixelPos = ScreenToCanvas(NormPos);
		// Anchor hover should only fire when the cursor is on the anchor's
		// own disc - NOT when it's on one of the anchor's zone edges (which
		// also share `HoveredHandleType == HandleType`). The `!bHoveredIsZoneEdge`
		// guard separates the two so zone-edge hover doesn't visually inflate
		// the anchor disc.
		const bool bHovered = bHandleInteractive
			&& HoveredHandleType == HandleType
			&& !bHoveredIsZoneEdge
			&& ActiveDragHandleType == EHandleType::None;
		// Likewise: the anchor "is being dragged" only when the active drag
		// is the anchor disc itself, not a zone edge attached to this anchor.
		const bool bDragging = ActiveDragHandleType == HandleType
			&& !bActiveDragIsZoneEdge;
		const FLinearColor Fill = bHandleDisabled ? kHandleDisabledColor: AnchorColor;
		const float Radius = (bHovered || bDragging)
			? kHandleAnchorRadius * kHandleHoverRadiusBoost: kHandleAnchorRadius;
		DrawHandleAt(PixelPos, Radius, Fill, /*bWithCross=*/!bHandleDisabled);

		// Centered label above the disc - same hue family as the handle but
		// drops to the disabled grey when the handle is non-interactive so
		// the visual coupling between text + disc is preserved.
		if (HandleLabelFont && LabelText)
		{
			const FVector2D LabelPos(PixelPos.X,
				PixelPos.Y - Radius - 14.f);
			FCanvasTextItem Label(LabelPos, FText::FromString(LabelText),
				HandleLabelFont, Fill);
			Label.bCentreX = true;
			Label.EnableShadow(FLinearColor::Black);
			Canvas.DrawItem(Label);
		}

		// E.1: hover tooltip - when the cursor is over an interactive handle
		// (Drag mode + mode-config that consumes ScreenPosition), show
		// authored ScreenPosition + projected world position + Distance
		// (Placement only) below-right of the disc. Designer doesn't need
		// the Details panel open just to inspect anchor values during
		// rapid composition iteration. Resolves the anchor world position
		// inline because it's only computed on hover (rare, no hot-path
		// concern) - keeps the lambda free of extra threaded outputs.
		if (bHovered && HandleLabelFont)
		{
			FVector AnchorWorld = FVector::ZeroVector;
			bool bHaveWorld = false;
			{
				FComposableCameraShot EffectiveShot;
				if (BuildEffectiveShotForPreview(EffectiveShot))
				{
					const FComposableCameraAnchorSpec& Anchor =
						AnchorAccessor(EffectiveShot);
					bHaveWorld = Anchor.ResolveWorldPosition(EffectiveShot.Targets, AnchorWorld);
				}
			}

			TArray<FString, TInlineAllocator<4>> Lines;
			Lines.Add(FString::Printf(TEXT("%s anchor"), LabelText));
			if (HandleType == EHandleType::PlacementAnchor)
			{
				Lines.Add(FString::Printf(TEXT("Distance: %.1f cm"),
					ActiveShot->Placement.Distance));
			}
			Lines.Add(FString::Printf(TEXT("Screen: (%.3f, %.3f)"),
				AuthoredScreenPos.X, AuthoredScreenPos.Y));
			Lines.Add(bHaveWorld
				? FString::Printf(TEXT("World: (%.0f, %.0f, %.0f)"),
					AnchorWorld.X, AnchorWorld.Y, AnchorWorld.Z)
				: FString(TEXT("World: unresolved")));

			float TipY = PixelPos.Y + Radius + 6.f;
			const float TipX = PixelPos.X + Radius + 6.f;
			for (const FString& Line: Lines)
			{
				FCanvasTextItem Item(FVector2D(TipX, TipY),
					FText::FromString(Line),
					HandleLabelFont, Fill);
				Item.EnableShadow(FLinearColor::Black);
				Canvas.DrawItem(Item);
				TipY += 14.f;
			}
		}

		if (bHandleInteractive)
		{
			FHandleScreenPosCache AnchorCache;
			AnchorCache.PixelPos = CanvasToViewport(PixelPos, CanvasScale);
			AnchorCache.Type = HandleType;
			AnchorCache.bIsZoneEdge = false;
			// Anchor hit area = square inscribing the hit-test radius. Square
			// (vs. true circle) keeps hit math uniform with edge entries while
			// only marginally over-claiming the corner pixels.
			AnchorCache.HitArea = CanvasHitAreaToViewport(FBox2D(PixelPos - FVector2D(kHandleHitRadius, kHandleHitRadius),
				PixelPos + FVector2D(kHandleHitRadius, kHandleHitRadius)), CanvasScale);
			CachedHandles.Add(AnchorCache);
		}

		// Zone overlay.
		//
		// Centered on the anchor's current displayed pixel position. Drawing
		// here (not in a separate pass) means the rects auto-track the anchor
		// - both during projection-driven motion ("the actor moved") and
		// during an active anchor drag (PixelPos = cursor pos in that case).
		// Resolves the zones data via accessor lambdas so we don't have to
		// branch on HandleType inside the draw block.
		auto ResolveZones = [&]() -> const FShotScreenZones*
		{
			if (HandleType == EHandleType::PlacementAnchor)
			{
				// Placement zones only consume `Placement.ScreenPosition`
				// (i.e., `AnchorAtScreen`); other modes silently ignore the
				// zone struct. Skip the overlay there to avoid implying an
				// effect the solver won't honor.
				if (ActiveShot->Placement.Mode != EShotPlacementMode::AnchorAtScreen)
				{
					return nullptr;
				}
				return &ActiveShot->Placement.PlacementZones;
			}
			else if (HandleType == EHandleType::AimAnchor)
			{
				// AimZones is meaningless under `NoOp` (Aim layer doesn't
				// read `ScreenPosition` at all) - same-rationale skip.
				if (ActiveShot->Aim.Mode == EShotAimMode::NoOp)
				{
					return nullptr;
				}
				return &ActiveShot->Aim.AimZones;
			}
			return nullptr;
		};

		const FShotScreenZones* Zones = ResolveZones();
		if (Zones && Zones->bEnabled && bHandleInteractive)
		{
			// Zone center = ScreenPosition (the AUTHORED screen target,
			// not the anchor's currently-projected position). Anchor
			// floats inside the zone - its disc may sit anywhere within
			// the rect during a hold; the rect itself stays anchored to
			// SP. Convert SP from normalized `[-0.5, 0.5]` to pixel.
			const FVector2D ZoneCenterPx = ScreenToCanvas(AuthoredScreenPos);

			// Per-side pixel paddings. Normalized full-viewport span = 1.0
			// (X) / 1.0 (Y), so pixel padding = padding_normalized x VPSize.
			const float DeadLpx = Zones->DeadZone.Left * CanvasWidth;
			const float DeadRpx = Zones->DeadZone.Right * CanvasWidth;
			const float DeadTpx = Zones->DeadZone.Top * CanvasHeight; // top = +Y normalized = -Y canvas
			const float DeadBpx = Zones->DeadZone.Bottom * CanvasHeight;
			const float SoftLpx = FMath::Max(Zones->SoftZone.Left, Zones->DeadZone.Left) * CanvasWidth;
			const float SoftRpx = FMath::Max(Zones->SoftZone.Right, Zones->DeadZone.Right) * CanvasWidth;
			const float SoftTpx = FMath::Max(Zones->SoftZone.Top, Zones->DeadZone.Top) * CanvasHeight;
			const float SoftBpx = FMath::Max(Zones->SoftZone.Bottom, Zones->DeadZone.Bottom) * CanvasHeight;

			// Pixel-space rectangles (top-left to bottom-right corners).
			// "Top" in our normalized convention = +Y (upward) = numerically
			// smaller pixel Y, so Dead's top-left pixel uses (-Left, -Top).
			const FBox2D DeadRectPx(FVector2D(ZoneCenterPx.X - DeadLpx, ZoneCenterPx.Y - DeadTpx),
				FVector2D(ZoneCenterPx.X + DeadRpx, ZoneCenterPx.Y + DeadBpx));
			const FBox2D SoftRectPx(FVector2D(ZoneCenterPx.X - SoftLpx, ZoneCenterPx.Y - SoftTpx),
				FVector2D(ZoneCenterPx.X + SoftRpx, ZoneCenterPx.Y + SoftBpx));

			// Dead zone is intentionally unfilled now (see below). The
			// `kZoneDeadFill*` constants are retained for symmetry with
			// the LS overlay, which still references them through the
			// shared draw helper signature; not used here.
			const FLinearColor SoftFill = (HandleType == EHandleType::PlacementAnchor)
				? kZoneSoftFillPlacement: kZoneSoftFillAim;

			// Translucent rect tile helper. SE_BLEND_Translucent so multiple
			// stacked panels (e.g. when both anchors' zones overlap) blend
			// rather than punching through.
			auto DrawFilledTile = [&Canvas](const FBox2D& Box, const FLinearColor& Color)
			{
				if (Box.GetSize().X <= 0.f || Box.GetSize().Y <= 0.f)
				{
					return;
				}
				FCanvasTileItem Tile(Box.Min,
					FVector2D(Box.GetSize().X, Box.GetSize().Y),
					Color);
				Tile.BlendMode = SE_BLEND_Translucent;
				Canvas.DrawItem(Tile);
			};

			// Soft "ring" - Soft Dead area, four tiles
			// Layout:
			//
			// TOP
			//
			// LEFT (DEAD) RIGHT
			//
			// BOTTOM
			//
			// Top spans full Soft width x (DeadTop SoftTop) height.
			// Bot spans full Soft width x (SoftBottom DeadBottom) height.
			// Left spans (SoftLeft DeadLeft) wide x Dead height.
			// Right spans (SoftRight DeadRight) wide x Dead height.
			// Each tile is no-op when the corresponding side has Dead == Soft
			// (degenerate / authored matching values) - DrawFilledTile early
			// outs on zero size.
			DrawFilledTile(FBox2D(FVector2D(SoftRectPx.Min.X, SoftRectPx.Min.Y),
				FVector2D(SoftRectPx.Max.X, DeadRectPx.Min.Y)), SoftFill);
			DrawFilledTile(FBox2D(FVector2D(SoftRectPx.Min.X, DeadRectPx.Max.Y),
				FVector2D(SoftRectPx.Max.X, SoftRectPx.Max.Y)), SoftFill);
			DrawFilledTile(FBox2D(FVector2D(SoftRectPx.Min.X, DeadRectPx.Min.Y),
				FVector2D(DeadRectPx.Min.X, DeadRectPx.Max.Y)), SoftFill);
			DrawFilledTile(FBox2D(FVector2D(DeadRectPx.Max.X, DeadRectPx.Min.Y),
				FVector2D(SoftRectPx.Max.X, DeadRectPx.Max.Y)), SoftFill);

			// Dead inner - intentionally NOT filled
			// Visually "empty" inside the dead zone - the soft ring frames
			// the area without obscuring whatever the camera is currently
			// holding on. Edges remain interactive: `CacheZoneEdges` below
			// still pushes hit areas for L/R/T/B of the dead rect, so
			// designers can grab the (invisible) inner edges and drag to
			// resize. Compared to drawing a faint inner fill, the empty
			// interior reads more like "this is the hold zone, don't
			// touch" without competing with the framed subject for
			// attention.

			// Edge hit-cache + hover/drag highlight lines
			// Each enabled zone contributes 4 edge entries (L/R/T/B).
			// `EdgeRect(rect, edge)` is a thin rect along the matching
			// side of `rect`, `kZoneEdgeHitThickness` perpendicular.
			auto EdgeRect = [&](const FBox2D& Rect, int32 EdgeIndex) -> FBox2D
			{
				const float HitHalf = kZoneEdgeHitThickness * 0.5f;
				switch (EdgeIndex)
				{
				case 0: return FBox2D( // Left edge - vertical at Rect.Min.X
					FVector2D(Rect.Min.X - HitHalf, Rect.Min.Y),
					FVector2D(Rect.Min.X + HitHalf, Rect.Max.Y));
				case 1: return FBox2D( // Right edge - vertical at Rect.Max.X
					FVector2D(Rect.Max.X - HitHalf, Rect.Min.Y),
					FVector2D(Rect.Max.X + HitHalf, Rect.Max.Y));
				case 2: return FBox2D( // Top edge - horizontal at Rect.Min.Y
					FVector2D(Rect.Min.X, Rect.Min.Y - HitHalf),
					FVector2D(Rect.Max.X, Rect.Min.Y + HitHalf));
				case 3: return FBox2D( // Bottom edge - horizontal at Rect.Max.Y
					FVector2D(Rect.Min.X, Rect.Max.Y - HitHalf),
					FVector2D(Rect.Max.X, Rect.Max.Y + HitHalf));
				}
				return FBox2D(ForceInit);
			};

			auto CacheZoneEdges = [&](const FBox2D& Rect, bool bSoft)
			{
				for (int32 Edge = 0; Edge < 4; ++Edge)
				{
					FHandleScreenPosCache EdgeCache;
					// Center of the edge - used as anchor for tooltips /
					// the highlight line midpoint.
					switch (Edge)
					{
					case 0: EdgeCache.PixelPos = FVector2D(Rect.Min.X, (Rect.Min.Y + Rect.Max.Y) * 0.5f); break;
					case 1: EdgeCache.PixelPos = FVector2D(Rect.Max.X, (Rect.Min.Y + Rect.Max.Y) * 0.5f); break;
					case 2: EdgeCache.PixelPos = FVector2D((Rect.Min.X + Rect.Max.X) * 0.5f, Rect.Min.Y); break;
					case 3: EdgeCache.PixelPos = FVector2D((Rect.Min.X + Rect.Max.X) * 0.5f, Rect.Max.Y); break;
					}
					EdgeCache.Type = HandleType;
					EdgeCache.PixelPos = CanvasToViewport(EdgeCache.PixelPos, CanvasScale);
					EdgeCache.bIsZoneEdge = true;
					EdgeCache.bIsSoftZone = bSoft;
					EdgeCache.EdgeIndex = Edge;
					EdgeCache.HitArea = CanvasHitAreaToViewport(EdgeRect(Rect, Edge), CanvasScale);
					CachedHandles.Add(EdgeCache);

					// Hover / drag highlight - single thin line along the
					// edge so the designer sees which edge they're about
					// to grab. Resting state has no border (just the
					// translucent fills above).
					const bool bHoveredHere = bHandleInteractive
						&& bHoveredIsZoneEdge
						&& HoveredHandleType == HandleType
						&& HoveredZoneIsSoft == bSoft
						&& HoveredZoneEdgeIndex == Edge
						&& ActiveDragHandleType == EHandleType::None;
					const bool bDraggingHere = bActiveDragIsZoneEdge
						&& ActiveDragHandleType == HandleType
						&& ActiveDragZoneIsSoft == bSoft
						&& ActiveDragZoneEdgeIndex == Edge;
					if (bHoveredHere || bDraggingHere)
					{
						FVector2D A, B;
						switch (Edge)
						{
						case 0: A = FVector2D(Rect.Min.X, Rect.Min.Y); B = FVector2D(Rect.Min.X, Rect.Max.Y); break;
						case 1: A = FVector2D(Rect.Max.X, Rect.Min.Y); B = FVector2D(Rect.Max.X, Rect.Max.Y); break;
						case 2: A = FVector2D(Rect.Min.X, Rect.Min.Y); B = FVector2D(Rect.Max.X, Rect.Min.Y); break;
						case 3: A = FVector2D(Rect.Min.X, Rect.Max.Y); B = FVector2D(Rect.Max.X, Rect.Max.Y); break;
						}
						FCanvasLineItem Highlight(A, B);
						Highlight.SetColor(kZoneEdgeHighlightColor);
						Highlight.LineThickness = kZoneEdgeHighlightThickness;
						Canvas.DrawItem(Highlight);

						// Numeric label - show the active padding value
						// for the hovered/dragged edge so designers can
						// dial precise sizes without opening the Details
						// panel. Resolved inline from the (Zones, bSoft,
						// Edge) tuple.
						if (HandleLabelFont)
						{
							const FShotScreenZonePadding& Pad =
								bSoft ? Zones->SoftZone: Zones->DeadZone;
							float PadValue = 0.f;
							const TCHAR* SideLabel = TEXT("");
							switch (Edge)
							{
							case 0: PadValue = Pad.Left; SideLabel = TEXT("L"); break;
							case 1: PadValue = Pad.Right; SideLabel = TEXT("R"); break;
							case 2: PadValue = Pad.Top; SideLabel = TEXT("T"); break;
							case 3: PadValue = Pad.Bottom; SideLabel = TEXT("B"); break;
							}
							const FString LabelText = FString::Printf(TEXT("%s %s: %.3f"),
								bSoft ? TEXT("Soft") : TEXT("Dead"),
								SideLabel, PadValue);

							// Anchor the label adjacent to the edge
							// midpoint, with a small offset away from the
							// rect so it doesn't overlap the highlight line.
							const FVector2D EdgeMid = (A + B) * 0.5f;
							FVector2D LabelOffset(0.f, 0.f);
							switch (Edge)
							{
							case 0: LabelOffset = FVector2D(-72.f, -7.f); break;
							case 1: LabelOffset = FVector2D(6.f, -7.f); break;
							case 2: LabelOffset = FVector2D(6.f, -18.f); break;
							case 3: LabelOffset = FVector2D(6.f, 4.f); break;
							}
							FCanvasTextItem Label(EdgeMid + LabelOffset,
								FText::FromString(LabelText),
								HandleLabelFont, kZoneEdgeHighlightColor);
							Label.EnableShadow(FLinearColor::Black);
							Canvas.DrawItem(Label);
						}
					}
				}
			};

			// Cache push order matters: HitTestHandles iterates in reverse
			// so the LAST-pushed entry wins on overlap. Push soft first
			// (outer edges), then dead (inner edges) - when a click could
			// hit either (Soft-side Dead-side, e.g. authored matching),
			// the dead edge wins, matching the visual "smaller rect on top".
			CacheZoneEdges(SoftRectPx, /*bSoft=*/true);
			CacheZoneEdges(DeadRectPx, /*bSoft=*/false);

			// Live-position marker - anchor's projected pixel
			// The main disc (filled, drawn earlier) sits at the AUTHORED
			// ScreenPosition (drag target). This second marker shows
			// where the anchor IS this frame - its world point projected
			// through the live camera. Inside the dead zone the two
			// drift apart; outside they converge. Read-only - no hit
			// area, no drag, smaller than the main disc, half-alpha.
			{
				FVector2D ProjNorm;
				if (ProjectAnchorWorld(AnchorAccessor, ProjNorm))
				{
					const FVector2D ProjPx = ScreenToCanvas(ProjNorm);
					constexpr float kProjRingRadius = 5.f;
					constexpr float kProjCrossArm = 4.f;
					const FLinearColor ProjColor(AnchorColor.R, AnchorColor.G, AnchorColor.B, 0.55f);

					constexpr int32 kProjRingSegs = 16;
					FVector2D PrevPt(ProjPx.X + kProjRingRadius, ProjPx.Y);
					for (int32 s = 1; s <= kProjRingSegs; ++s)
					{
						const float T = (s / static_cast<float>(kProjRingSegs)) * 2.f * PI;
						const FVector2D Pt(ProjPx.X + FMath::Cos(T) * kProjRingRadius,
							ProjPx.Y + FMath::Sin(T) * kProjRingRadius);
						FCanvasLineItem Seg(PrevPt, Pt);
						Seg.SetColor(ProjColor); Seg.LineThickness = 1.f;
						Canvas.DrawItem(Seg);
						PrevPt = Pt;
					}
					FCanvasLineItem H(FVector2D(ProjPx.X - kProjCrossArm, ProjPx.Y),
						FVector2D(ProjPx.X + kProjCrossArm, ProjPx.Y));
					H.SetColor(ProjColor); H.LineThickness = 1.f;
					Canvas.DrawItem(H);
					FCanvasLineItem V(FVector2D(ProjPx.X, ProjPx.Y - kProjCrossArm),
						FVector2D(ProjPx.X, ProjPx.Y + kProjCrossArm));
					V.SetColor(ProjColor); V.LineThickness = 1.f;
					Canvas.DrawItem(V);
				}
				// Anchor unresolvable / behind camera: silently skip.
				// The disc at SP is enough on its own; the absent marker
				// implicitly says "we can't tell where the anchor is".
			}
		}
	};

	// Orbit still uses its world anchor, but has no Follow screen-position control.
	if (bShowPlacement) DrawAnchorHandle(EHandleType::PlacementAnchor,
		ActiveShot->Placement.ScreenPosition,
		[](const FComposableCameraShot& S) -> const FComposableCameraAnchorSpec&
		{ return S.Placement.PlacementAnchor; },
		kHandlePlacementColor,
		TEXT("Follow"),
		bPlacementDisabled,
		bPlacementInteractive);

	if (bShowAim) DrawAnchorHandle(EHandleType::AimAnchor,
		ActiveShot->Aim.ScreenPosition,
		[](const FComposableCameraShot& S) -> const FComposableCameraAnchorSpec&
		{ return S.Aim.AimAnchor; },
		kHandleAimColor,
		TEXT("Aim"),
		bAimDisabled,
		bAimInteractive);
}

bool FComposableCameraShotEditorViewportClient::HitTestHandles(int32 PixelX, int32 PixelY,
	FHandleScreenPosCache& OutHit) const
{
	// Iterate in reverse so handles drawn last (z-top in our DrawHandles
	// order) win the hit test on overlap. Top-of-z-order: dead-zone edges
	// > soft-zone edges > anchor disc (because anchors push *before* their
	// zone edges, and within a zone tier dead is pushed after soft). Net
	// effect: a click in the corner where an anchor disc and an edge box
	// both report a hit grabs the edge first, which matches user intent
	// (the anchor disc is the visible focal point but the edge is the
	// thinner / more deliberate target).
	OutHit = FHandleScreenPosCache{};
	const auto Session = AuthoringSession.Pin();
	if (!ActiveShot || !ActiveHost.IsValid() || !bShowCompositionGuides || CurrentMode != EShotEditorMode::Drag || (Session && !Session->CanEdit())) return false;
	for (int32 i = CachedHandles.Num() - 1; i >= 0; --i)
	{
		const FHandleScreenPosCache& H = CachedHandles[i];
		// Input can arrive between a mode edit and the next paint; reject stale cached hits.
		if (!ActiveShot
			|| (H.Type == EHandleType::PlacementAnchor && !ComposableCameraSystem::ShotViewportDisplay::ShowFollowHandle(*ActiveShot))
			|| (H.Type == EHandleType::AimAnchor && !ShowLookAtHandle())) continue;
		if ((H.Type == EHandleType::SubjectOffsetAxis || H.Type == EHandleType::SubjectBoundsFace) && !IsSubjectHandleValid(H)) continue;
		if (H.Type == EHandleType::OrbitDirection)
		{
			if (!ShowOrbitControl()) continue;
			using namespace ComposableCameraSystem::ShotEditorCanvas;
			const FIntRect Rect = GetRenderRect();
			const auto Layout = ComposableCameraSystem::ShotViewportOverlay::OrbitLayout(FBox2D(
				ViewportToCanvas(FVector2D(Rect.Min), OrbitControlDpiScale), ViewportToCanvas(FVector2D(Rect.Max), OrbitControlDpiScale)));
			if (Layout.Hit(ViewportToCanvas(FVector2D(PixelX, PixelY), OrbitControlDpiScale)))
			{
				OutHit = H;
				return true;
			}
			continue;
		}
		if (H.HitArea.IsInside(FVector2D(PixelX, PixelY)))
		{
			OutHit = H;
			return true;
		}
	}
	OutHit = FHandleScreenPosCache{};
	return false;
}

void FComposableCameraShotEditorViewportClient::StartHandleDrag(const FHandleScreenPosCache& Hit, int32 PixelX, int32 PixelY)
{
	const auto Session = AuthoringSession.Pin();
	if (!ActiveShot || !ActiveHost.IsValid() || !bShowCompositionGuides || CurrentMode != EShotEditorMode::Drag
		|| Hit.Type == EHandleType::None || IsEditingGesture() || (Session && !Session->CanEdit())) return;
	if ((Hit.Type == EHandleType::OrbitDirection && !ShowOrbitControl())
		|| (Hit.Type == EHandleType::PlacementAnchor && !ComposableCameraSystem::ShotViewportDisplay::ShowFollowHandle(*ActiveShot))
		|| (Hit.Type == EHandleType::AimAnchor && !ShowLookAtHandle())) return;
	const bool bSubject = Hit.Type == EHandleType::SubjectOffsetAxis || Hit.Type == EHandleType::SubjectBoundsFace;
	if (bSubject && !IsSubjectHandleValid(Hit)) return;
	DragTransaction = MakeUnique<FScopedTransaction>(Hit.Type == EHandleType::OrbitDirection
		? LOCTEXT("DragShotOrbit", "Orbit Shot Camera") : Hit.Type == EHandleType::SubjectOffsetAxis
		? LOCTEXT("DragSubjectOffset", "Move Shot Subject Pivot") : Hit.Type == EHandleType::SubjectBoundsFace
		? LOCTEXT("DragSubjectBounds", "Resize Shot Subject Bounds") : Hit.bIsZoneEdge
		? LOCTEXT("DragShotZoneEdge", "Resize Framing Zone") : LOCTEXT("DragShotScreenPosition", "Drag Shot Screen Position"));
	// Snapshot without Modify/OnObjectModified: Sequencer must not respawn a camera mid-gesture.
	SaveToTransactionBuffer(ActiveHost.Get(), /*bMarkDirty=*/false);
	ActiveDragHandleType = Hit.Type;
	bActiveDragIsZoneEdge = Hit.bIsZoneEdge;
	ActiveDragZoneIsSoft = Hit.bIsSoftZone;
	ActiveDragZoneEdgeIndex = Hit.EdgeIndex;
	OrbitDragLastMouse = FVector2D(PixelX, PixelY);
	OrbitDragDpiScale = OrbitControlDpiScale;
	bOrbitDragChanged = false;
	if (bSubject)
	{
		ActiveSubjectHandle = Hit;
		SubjectDragLastMouse = FVector2D(PixelX, PixelY);
		bSubjectDragChanged = false;
	}
}

void FComposableCameraShotEditorViewportClient::ApplyDragToShot(int32 PixelX, int32 PixelY)
{
	const auto Session = AuthoringSession.Pin();
	if (!ActiveShot || !ActiveHost.IsValid() || !Viewport || !bShowCompositionGuides || CurrentMode != EShotEditorMode::Drag
		|| (Session && !Session->CanEdit()) || ActiveDragHandleType == EHandleType::None
		|| (ActiveDragHandleType == EHandleType::PlacementAnchor && !ComposableCameraSystem::ShotViewportDisplay::ShowFollowHandle(*ActiveShot))
		|| (ActiveDragHandleType == EHandleType::AimAnchor && !ShowLookAtHandle())
		|| (ActiveDragHandleType == EHandleType::OrbitDirection && !ShowOrbitControl())) return;
	if (ActiveDragHandleType == EHandleType::SubjectOffsetAxis || ActiveDragHandleType == EHandleType::SubjectBoundsFace)
	{
		if (!IsSubjectHandleValid(ActiveSubjectHandle)) { EndDrag(); return; }
		const FVector2D Mouse(PixelX, PixelY);
		const bool bShift = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);
		const bool bCtrl = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
		const double Speed = bShift == bCtrl ? 1. : bShift ? 5. : .2;
		const double Delta = ComposableCameraSystem::ShotSubjectGizmo::DragDelta(Mouse - SubjectDragLastMouse, ActiveSubjectHandle.PixelsPerUnit, Speed);
		SubjectDragLastMouse = Mouse;
		if (!FMath::IsFinite(Delta)) return;
		auto& Subject = ActiveShot->Targets[ActiveSubjectHandle.SubjectIndex];
		double& Value = ActiveDragHandleType == EHandleType::SubjectOffsetAxis
			? Subject.Target.Offset[ActiveSubjectHandle.AxisIndex] : Subject.ManualBoundsExtent[ActiveSubjectHandle.AxisIndex];
		const double NewValue = ActiveDragHandleType == EHandleType::SubjectOffsetAxis ? Value + Delta
			: FMath::Max(0., Value + Delta * ActiveSubjectHandle.FaceSign);
		if (FMath::IsFinite(NewValue) && !FMath::IsNearlyEqual(Value, NewValue))
		{
			Value = NewValue;
			ActiveSubjectHandle.AuthoredOffset = Subject.Target.Offset;
			ActiveSubjectHandle.AuthoredExtent = Subject.ManualBoundsExtent;
			bSubjectDragChanged = true; NotifyInteractiveEdit();
		}
		return;
	}
	if (ActiveDragHandleType == EHandleType::OrbitDirection)
	{
		const FVector2D Mouse(PixelX, PixelY);
		const bool bShift = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);
		const bool bCtrl = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
		const float Speed = bShift == bCtrl ? 1.f : bShift ? 5.f : .2f;
		const FVector2D Direction = ComposableCameraSystem::ShotViewportOverlay::DragOrbitDirection(
			ActiveShot->Placement.LocalCameraDirection, Mouse - OrbitDragLastMouse, OrbitDragDpiScale, Speed);
		OrbitDragLastMouse = Mouse;
		if (!Direction.Equals(ActiveShot->Placement.LocalCameraDirection))
		{
			ActiveShot->Placement.LocalCameraDirection = Direction;
			bOrbitDragChanged = true;
			NotifyInteractiveEdit();
		}
		return;
	}

	const FIntPoint VPSize = GetRenderSize();
	NotifyInteractiveEdit();

	// Zone-edge drag: cursor distance from SP center -> new padding
	//
	// Single-side semantics: each edge mutates exactly one of the four
	// padding scalars (Left / Right / Top / Bottom) on either dead or
	// soft zone. Zones are centered on `ScreenPosition` (NOT on anchor's
	// projected pixel), so the drag math anchors to SP's pixel - same
	// reference the renderer uses, so the dragged edge lands exactly
	// under the cursor.
	if (bActiveDragIsZoneEdge)
	{
		// Resolve which zones struct to mutate (matches DrawAnchorHandle's
		// ResolveZones logic - zones-edge drags can only fire when zones
		// are interactive, but keep the null check for defense).
		FShotScreenZones* Zones = nullptr;
		FVector2D AuthoredSP(0.f, 0.f);
		if (ActiveDragHandleType == EHandleType::PlacementAnchor
			&& ActiveShot->Placement.Mode == EShotPlacementMode::AnchorAtScreen)
		{
			Zones = &ActiveShot->Placement.PlacementZones;
			AuthoredSP = ActiveShot->Placement.ScreenPosition;
		}
		else if (ActiveDragHandleType == EHandleType::AimAnchor
			&& ActiveShot->Aim.Mode != EShotAimMode::NoOp)
		{
			Zones = &ActiveShot->Aim.AimZones;
			AuthoredSP = ActiveShot->Aim.ScreenPosition;
		}
		if (!Zones || !Zones->bEnabled)
		{
			return;
		}

		const FVector2D ZoneCenterPx = NormalizedScreenToPixel(AuthoredSP, VPSize);
		const float DXpx = static_cast<float>(PixelX) - ZoneCenterPx.X;
		const float DYpx = static_cast<float>(PixelY) - ZoneCenterPx.Y;
		const float VPWidth = FMath::Max(static_cast<float>(VPSize.X), 1.f);
		const float VPHeight = FMath::Max(static_cast<float>(VPSize.Y), 1.f);

		// Convert per-side cursor offset to a single normalized padding.
		// Padding is always non-negative; the matching axis sign on the
		// cursor offset determines whether the cursor is on the correct
		// side of SP at all (negative collapse padding to 0). Clamp to
		// [0, 0.5] = at most half a viewport per side.
		FShotScreenZonePadding& Active = ActiveDragZoneIsSoft ? Zones->SoftZone: Zones->DeadZone;
		FShotScreenZonePadding& Other = ActiveDragZoneIsSoft ? Zones->DeadZone: Zones->SoftZone;
		float NewPad = 0.f;
		float* ActivePadPtr = nullptr;
		float* OtherPadPtr = nullptr;
		switch (ActiveDragZoneEdgeIndex)
		{
		case 0: // Left - cursor must be to the LEFT of SP (DXpx < 0)
			NewPad = FMath::Clamp(-DXpx / VPWidth, 0.f, 0.5f);
			ActivePadPtr = &Active.Left;
			OtherPadPtr = &Other.Left;
			break;
		case 1: // Right - cursor must be to the RIGHT of SP (DXpx > 0)
			NewPad = FMath::Clamp(DXpx / VPWidth, 0.f, 0.5f);
			ActivePadPtr = &Active.Right;
			OtherPadPtr = &Other.Right;
			break;
		case 2: // Top - cursor must be ABOVE SP (DYpx < 0, +Y normalized = top)
			NewPad = FMath::Clamp(-DYpx / VPHeight, 0.f, 0.5f);
			ActivePadPtr = &Active.Top;
			OtherPadPtr = &Other.Top;
			break;
		case 3: // Bottom
			NewPad = FMath::Clamp(DYpx / VPHeight, 0.f, 0.5f);
			ActivePadPtr = &Active.Bottom;
			OtherPadPtr = &Other.Bottom;
			break;
		default:
			return;
		}
		check(ActivePadPtr && OtherPadPtr);
		*ActivePadPtr = NewPad;

		// Enforce the dead <= soft invariant on the same side only. Dragging
		// soft below dead pulls dead inward; dragging dead past soft pushes
		// soft outward. Other three sides are untouched - that's the whole
		// point of the asymmetric padding model.
		if (ActiveDragZoneIsSoft)
		{
			// Active = Soft side; Other = Dead side. Soft >= Dead invariant
			// pull Dead down to NewPad if Dead was larger.
			*OtherPadPtr = FMath::Min(*OtherPadPtr, NewPad);
		}
		else
		{
			// Active = Dead side; Other = Soft side. Push Soft up.
			*OtherPadPtr = FMath::Max(*OtherPadPtr, NewPad);
		}

		// Shift-symmetric drag - when the user holds Shift, mirror the
		// edited padding onto the opposite side of the same axis (Left
		// Right or Top Bottom). Cinemachine has the same modifier; the
		// expected designer flow is "default to single-side, hold Shift
		// when I want a centered zone". Modifier read is one Slate query
		// per frame during a drag - cheap. The opposite-side update goes
		// through the same dead/soft invariant clamp, with the *opposite*
		// pad as the active side now (so the invariant is enforced on
		// both sides of the axis).
		const bool bShiftHeld = FSlateApplication::IsInitialized()
			&& FSlateApplication::Get().GetModifierKeys().IsShiftDown();
		if (bShiftHeld)
		{
			float* MirrorActivePtr = nullptr;
			float* MirrorOtherPtr = nullptr;
			switch (ActiveDragZoneEdgeIndex)
			{
			case 0: MirrorActivePtr = &Active.Right; MirrorOtherPtr = &Other.Right; break; // Left Right
			case 1: MirrorActivePtr = &Active.Left; MirrorOtherPtr = &Other.Left; break;
			case 2: MirrorActivePtr = &Active.Bottom; MirrorOtherPtr = &Other.Bottom; break; // Top Bottom
			case 3: MirrorActivePtr = &Active.Top; MirrorOtherPtr = &Other.Top; break;
			}
			if (MirrorActivePtr && MirrorOtherPtr)
			{
				*MirrorActivePtr = NewPad;
				if (ActiveDragZoneIsSoft)
				{
					*MirrorOtherPtr = FMath::Min(*MirrorOtherPtr, NewPad);
				}
				else
				{
					*MirrorOtherPtr = FMath::Max(*MirrorOtherPtr, NewPad);
				}
			}
		}
		// Same Sequencer-respawn rationale as the anchor drag below - skip
		// per-frame PostEditChangeProperty.
		return;
	}

	FVector2D NewScreenPos = PixelToNormalizedScreen(PixelX, PixelY, VPSize);
	NewScreenPos.X = FMath::Clamp(NewScreenPos.X, -0.5f, 0.5f);
	NewScreenPos.Y = FMath::Clamp(NewScreenPos.Y, -0.5f, 0.5f);

	if (ActiveDragHandleType == EHandleType::PlacementAnchor)
	{
		ActiveShot->Placement.ScreenPosition = NewScreenPos;
	}
	else if (ActiveDragHandleType == EHandleType::AimAnchor)
	{
		ActiveShot->Aim.ScreenPosition = NewScreenPos;
	}

	// Skip per-frame PostEditChangeProperty(Interactive). For a section host,
	// firing this every mouse move triggers Sequencer's section-edit listener
	// chain, which transiently resets the source SK's bone array - that
	// surfaces as a one-frame A-pose flash on the preview proxy each frame
	// of the drag. The solver reads `ActiveShot` directly each tick, so the
	// live drag visualization is unaffected. The final ValueSet notification
	// fires once on EndDrag, which is enough for Details-panel refresh and
	// other commit-time listeners.
}

void FComposableCameraShotEditorViewportClient::EndDrag()
{
	if (ActiveDragHandleType == EHandleType::None)
	{
		return;
	}

	// Final commit - ValueSet change type so host listeners that distinguish
	// "live drag" from "settled value" (e.g. expensive caches) update only
	// at the end.
	if ((ActiveDragHandleType == EHandleType::OrbitDirection && !bOrbitDragChanged)
		|| ((ActiveDragHandleType == EHandleType::SubjectOffsetAxis || ActiveDragHandleType == EHandleType::SubjectBoundsFace) && !bSubjectDragChanged))
	{
		if (DragTransaction) DragTransaction->Cancel();
	}
	else NotifyCommittedEdit();

	// Drop the transaction - destructor closes it; undo system records the
	// whole drag as a single step from its initial snapshot to here.
	DragTransaction.Reset();

	ActiveDragHandleType = EHandleType::None;
	bOrbitDragChanged = false;
	bSubjectDragChanged = false;
	ActiveSubjectHandle = {};
	bActiveDragIsZoneEdge = false;
	ActiveDragZoneIsSoft = false;
	ActiveDragZoneEdgeIndex = -1;
}

void FComposableCameraShotEditorViewportClient::StartRollDrag()
{
	const auto Session = AuthoringSession.Pin();
	if (!ActiveShot || !ActiveHost.IsValid() || !Viewport || bRollDragActive
		|| ActiveDragHandleType != EHandleType::None
		|| (Session && !Session->CanEdit()))
	{
		return; // can't start - no Shot, or already mid-gesture
	}

	RollTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("DragShotRoll", "Drag Shot Roll"));
	if (UObject* Host = ActiveHost.Get())
	{
		// Same SaveToTransactionBuffer-bypass-Modify pattern as the LMB
		// handle drag (see InputKey LMB branch + TechDoc Section 7.2). For a
		// Sequencer Section host, `Modify()` broadcasts OnObjectModified
		// -> Sequencer invalidates eval cache -> Spawnable re-spawn ->
		// one-frame A-pose flash on every mid-drag write. The bypass
		// records the undo snapshot only; EndRollDrag's
		// PostEditChangeProperty(ValueSet) is the single broadcast.
		SaveToTransactionBuffer(Host, /*bMarkDirty=*/false);
	}
	bRollDragActive = true;
	RollDragLastMouseX = Viewport->GetMouseX();
}

void FComposableCameraShotEditorViewportClient::ApplyRollDrag(int32 PixelX)
{
	if (!bRollDragActive) return;
	if (!ActiveShot || !ActiveHost.IsValid())
	{
		if (RollTransaction) RollTransaction->Cancel();
		RollTransaction.Reset();
		bRollDragActive = false;
		return;
	}
	const auto Session = AuthoringSession.Pin();
	if (Session && !Session->CanEdit())
	{
		EndRollDrag();
		return;
	}

	// 0.5 deg/pixel matches the perceived "feel" of base RMB-look's yaw
	// rate on a default 1920-wide viewport - quarter-width drag (~480 px)
	// gives ~240 deg of Roll, full-width drag wraps. Per-pixel rate feels
	// right for both fine adjustments and large rolls.
	constexpr float RollDegPerPixel = 0.5f;

	const int32 DeltaX = PixelX - RollDragLastMouseX;
	if (DeltaX != 0)
	{
		ActiveShot->Roll = FMath::UnwindDegrees(ActiveShot->Roll + DeltaX * RollDegPerPixel);
		NotifyInteractiveEdit();
		// No PostEditChangeProperty(Interactive) per frame - same
		// rationale as ApplyDragToShot. Drag mode picks up Shot.Roll
		// via the per-tick solver; Free mode picks it up via the
		// view.Roll = Shot.Roll sync at the end of
		// RunSolverAndDriveCamera. Both produce immediate visual
		// feedback within the same frame the cursor moves.
	}
	RollDragLastMouseX = PixelX;
}

void FComposableCameraShotEditorViewportClient::EndRollDrag()
{
	if (!bRollDragActive)
	{
		return;
	}

	NotifyCommittedEdit();

	RollTransaction.Reset();
	bRollDragActive = false;
}

bool FComposableCameraShotEditorViewportClient::TryAdjustDistanceFromMouseWheel(bool bScrollUp)
{
	// Wheel must not nest a transaction or emit a commit halfway through a captured gesture.
	if (IsEditingGesture()) return false;
	if (TSharedPtr<FComposableCameraShotAuthoringSession> Session = AuthoringSession.Pin(); Session && !Session->CanEdit()) return false;
	if (!ActiveShot)
	{
		return false;
	}

	// Only AnchorOrbit / AnchorAtScreen consume Distance in the forward
	// solver. FixedWorldPosition reads `Placement.FixedWorldPosition`
	// directly and ignores Distance entirely - bumping it would silently
	// edit a hidden field with no visible effect, so refuse the wheel
	// instead.
	const EShotPlacementMode Mode = ActiveShot->Placement.Mode;
	if (Mode != EShotPlacementMode::AnchorOrbit
		&& Mode != EShotPlacementMode::AnchorAtScreen)
	{
		return false;
	}

	// Multiplicative zoom - feels uniform across distance ranges (a
	// click at 10 m moves by ~1 m; a click at 1 m moves by ~10 cm).
	// Scroll up = zoom in (distance shrinks); scroll down = zoom out.
	// Default 1.1x per click matches PIE / Persona / standard editor
	// viewport wheel dolly. Modifier keys scale the per-click *step*
	// (factor 1) to match DCC convention so a single rate doesn't
	// feel coarse-or-tedious depending on geometry scale: Shift = 5x
	// step (factor 1.5, ~50% per click) for fast traversal; Ctrl = 0.2x
	// step (factor 1.02, ~2% per click) for fine framing. Holding both
	// (or neither) falls back to the default - composing the two is
	// ambiguous (1x of default default), and refusing to disambiguate
	// is safer than picking arbitrarily.
	const bool bShiftHeld = Viewport
		&& (Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift));
	const bool bCtrlHeld = Viewport
		&& (Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl));
	const float ZoomFactor =
		ComposableCameraSystem::ShotEditorWheelMath::ComputeWheelZoomFactor(bShiftHeld, bCtrlHeld);

	const float OldDistance = ActiveShot->Placement.Distance;
	const float NewDistance = bScrollUp
		? OldDistance / ZoomFactor: OldDistance * ZoomFactor;
	// Clamp to the canonical authoring range. Lower bound matches the
	// solver's pre-flight floor (1cm); upper bound is the 100m soft cap
	// against scroll-spam (Shift+wheel reaches 1e9 in ~10 clicks
	// otherwise). UPROPERTY meta clamps the Details-panel slider to the
	// same range, but meta doesn't enforce on direct field writes -
	// gesture writers must opt in explicitly.
	const float ClampedDistance = FMath::Clamp(NewDistance, FShotPlacement::MinDistance, FShotPlacement::MaxDistance);

	if (FMath::IsNearlyEqual(ClampedDistance, OldDistance))
	{
		// Already at the floor and trying to shrink further - no-op,
		// don't bother with the transaction or notification.
		return false;
	}

	UObject* Host = ActiveHost.Get();
	{
		// One wheel click = one undo step. Snapshot without broadcasting
		// OnObjectModified; release emits one session-aware scalar commit.
		FScopedTransaction Tx(LOCTEXT("WheelAdjustShotDistance", "Adjust Shot Distance"));
		if (Host)
		{
			SaveToTransactionBuffer(Host, /*bMarkDirty=*/false);
		}
		ActiveShot->Placement.Distance = ClampedDistance;
		NotifyCommittedEdit();
	}
	return true;
}

EMouseCursor::Type FComposableCameraShotEditorViewportClient::GetCursor(FViewport* InViewport, int32 X, int32 Y)
{
	if (bShowCompositionGuides && (ShowOrbitControl() || ShowSubjectGuides()))
	{
		const auto Session = AuthoringSession.Pin();
		if (CurrentMode == EShotEditorMode::Drag && (!Session || Session->CanEdit()))
		{
			if (ActiveDragHandleType == EHandleType::OrbitDirection || ActiveDragHandleType == EHandleType::SubjectOffsetAxis
				|| ActiveDragHandleType == EHandleType::SubjectBoundsFace) return EMouseCursor::GrabHandClosed;
			FHandleScreenPosCache Hit;
			if (HitTestHandles(X, Y, Hit) && (Hit.Type == EHandleType::OrbitDirection || Hit.Type == EHandleType::SubjectOffsetAxis
				|| Hit.Type == EHandleType::SubjectBoundsFace)) return EMouseCursor::GrabHand;
		}
	}
	return FEditorViewportClient::GetCursor(InViewport, X, Y);
}

bool FComposableCameraShotEditorViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	const auto Session = AuthoringSession.Pin();
	const bool bReadOnly = Session && !Session->CanEdit();
	// Release can arrive after a lock change and before Tick closes the gesture.
	if (EventArgs.Event == IE_Released)
	{
		if (EventArgs.Key == EKeys::RightMouseButton && bRollDragActive) { EndRollDrag(); return true; }
		if (EventArgs.Key == EKeys::LeftMouseButton && ActiveDragHandleType != EHandleType::None) { EndDrag(); return true; }
	}
	// Read-only authoring disables mouse writes; keyboard shortcuts still route normally.
	// Inspect retains native camera navigation, including Alt+RMB dolly when Roll cannot be authored.
	if (bReadOnly && CurrentMode != EShotEditorMode::Free && EventArgs.Key.IsMouseButton()) return true;
	// Helper: is Alt currently held? Used by the Alt+RMB Roll path.
	const auto IsAltHeld = [&]() -> bool
	{
		return Viewport
			&& (Viewport->KeyState(EKeys::LeftAlt)
				|| Viewport->KeyState(EKeys::RightAlt));
	};

	// Alt+RMB Roll drag - supported in Drag and Free modes.
	// Detected at the top so it preempts the
	// mode-specific mouse-handling branches below: in Drag mode it runs
	// before the catch-all "eat all mouse buttons" guard; in Free mode it
	// runs before the fall-through to base-class RMB-look. The
	// gesture writes `Shot.Roll` (NOT view-rotation Roll) inside a
	// transaction so Ctrl+Z restores the prior value - Drag mode picks
	// up the change via the next-tick solver, Free mode via the
	// view.Roll = Shot.Roll sync at the bottom of RunSolverAndDriveCamera.
	// Returning `true` on the press makes Slate capture the mouse for us
	// (same path the LMB handle drag uses), so CapturedMouseMove fires for
	// the gesture's motion regardless of which mode we're in.
	if (EventArgs.Key == EKeys::RightMouseButton && Viewport)
	{
		if (EventArgs.Event == IE_Pressed && IsAltHeld() && !bRollDragActive && !bReadOnly)
		{
			StartRollDrag();
			return true;
		}
	}

	// Drag mode: solver drives the camera, designer interacts only through
	// the on-screen anchor handles. LMB-on-handle starts a drag; every
	// other mouse input is **eaten** so the base class can't engage its
	// orbit / pan / dolly camera-track behavior. Free camera movement is
	// the Free mode's job - Drag stays solver-authoritative.
	if (CurrentMode == EShotEditorMode::Drag)
	{
		if (EventArgs.Key == EKeys::LeftMouseButton && Viewport)
		{
			if (EventArgs.Event == IE_Pressed && ActiveShot)
			{
				FHandleScreenPosCache Hit;
				if (HitTestHandles(Viewport->GetMouseX(), Viewport->GetMouseY(), Hit))
				{
					StartHandleDrag(Hit, Viewport->GetMouseX(), Viewport->GetMouseY());
					return true;
				}
				// LMB pressed off-handle in Drag mode -> eat so base class
				// doesn't start a camera-track gesture.
				return true;
			}
			if (EventArgs.Event == IE_Released)
			{
				if (ActiveDragHandleType != EHandleType::None)
				{
					EndDrag();
				}
				return true; // eat release regardless of whether a drag was active
			}
		}

		// Mouse wheel -> modify `Shot.Placement.Distance` for the modes
		// that read it (AnchorOrbit / AnchorAtScreen). FixedWorldPosition
		// ignores the wheel (helper returns false) and the event drops
		// through to the general mouse-button eater below - so the wheel
		// is silent in that mode rather than being a base-class dolly
		// (which would be confusing in Drag mode where the camera is
		// solver-driven). Note: `MouseScrollUp` / `MouseScrollDown` come
		// in as IE_Pressed events (no IE_Released for wheel ticks);
		// they're FKey instances that report `IsMouseButton() == true`,
		// so the catch-all below would eat them otherwise - handle here
		// first.
		if ((EventArgs.Key == EKeys::MouseScrollUp || EventArgs.Key == EKeys::MouseScrollDown)
			&& EventArgs.Event == IE_Pressed)
		{
			if (TryAdjustDistanceFromMouseWheel(EventArgs.Key == EKeys::MouseScrollUp))
			{
				return true;
			}
			// Fall through - FixedWorldPosition mode returns false; the
			// catch-all below still consumes the wheel so base class
			// doesn't dolly the camera (preserving the "Drag mode is
			// solver-authoritative" invariant from Section 23.x).
		}

		// Any other mouse button (RMB / MMB / unhandled wheel) -> eat so
		// base class can't engage RMB-look / MMB-pan / wheel-zoom.
		// Keyboard falls through so editor-wide shortcuts (Ctrl+Z etc.)
		// still work.
		if (EventArgs.Key.IsMouseButton())
		{
			return true;
		}
		return FEditorViewportClient::InputKey(EventArgs);
	}

	return FEditorViewportClient::InputKey(EventArgs);
}

void FComposableCameraShotEditorViewportClient::CapturedMouseMove(FViewport* InViewport, int32 InMouseX, int32 InMouseY)
{
	if (ActiveDragHandleType != EHandleType::None)
	{
		ApplyDragToShot(InMouseX, InMouseY);
		return; // don't forward - base would interpret as camera-track drag
	}
	if (bRollDragActive)
	{
		ApplyRollDrag(InMouseX);
		return; // don't forward - base would yaw the camera
	}
	FEditorViewportClient::CapturedMouseMove(InViewport, InMouseX, InMouseY);
}

void FComposableCameraShotEditorViewportClient::MouseMove(FViewport* InViewport, int32 X, int32 Y)
{
	FEditorViewportClient::MouseMove(InViewport, X, Y);

	// Hover detection - only meaningful in Drag mode (handles are
	// non-interactive in Free).
	if (CurrentMode != EShotEditorMode::Drag
		|| ActiveDragHandleType != EHandleType::None)
	{
		HoveredHandleType = EHandleType::None;
		bHoveredIsZoneEdge = false;
		HoveredZoneIsSoft = false;
		HoveredZoneEdgeIndex = -1;
		return;
	}
	FHandleScreenPosCache Hit;
	if (HitTestHandles(X, Y, Hit))
	{
		HoveredHandleType = Hit.Type;
		bHoveredIsZoneEdge = Hit.bIsZoneEdge;
		HoveredZoneIsSoft = Hit.bIsSoftZone;
		HoveredZoneEdgeIndex = Hit.EdgeIndex;
		HoveredSubjectIndex = Hit.SubjectIndex;
		HoveredSubjectAxis = Hit.AxisIndex;
		HoveredSubjectFaceSign = Hit.FaceSign;
	}
	else
	{
		HoveredHandleType = EHandleType::None;
		bHoveredIsZoneEdge = false;
		HoveredZoneIsSoft = false;
		HoveredZoneEdgeIndex = -1;
	}
}

void FComposableCameraShotEditorViewportClient::ResetViewToShot()
{
	const EShotEditorMode SavedMode = CurrentMode;
	CurrentMode = EShotEditorMode::Drag;
	RunSolverAndDriveCamera(/*DeltaSeconds=*/0.f);
	CurrentMode = SavedMode;
}

EShotEditorReverseSolveStatus FComposableCameraShotEditorViewportClient::DiagnoseReverseSolveCurrentCamera() const
{
	if (!ActiveShot)
	{
		return EShotEditorReverseSolveStatus::NoActiveShot;
	}
	if (TSharedPtr<FComposableCameraShotAuthoringSession> Session = AuthoringSession.Pin(); Session && !Session->CanEdit()) return EShotEditorReverseSolveStatus::SourceReadOnly;

	FComposableCameraShot EffectiveShot;
	if (!BuildEffectiveShotForPreview(EffectiveShot))
	{
		return EShotEditorReverseSolveStatus::EffectiveShotInvalid;
	}

	FVector PlacementAnchorWorld;
	if (!EffectiveShot.Placement.PlacementAnchor.ResolveWorldPosition(EffectiveShot.Targets, PlacementAnchorWorld))
	{
		return EShotEditorReverseSolveStatus::PlacementAnchorUnresolvable;
	}

	FVector AimAnchorWorld;
	if (!EffectiveShot.Aim.AimAnchor.ResolveWorldPosition(EffectiveShot.Targets, AimAnchorWorld))
	{
		return EShotEditorReverseSolveStatus::AimAnchorUnresolvable;
	}

	if (EffectiveShot.Placement.Mode == EShotPlacementMode::AnchorAtScreen)
	{
		const FVector CameraPosition = GetViewLocation();
		const FRotator CameraRotation = GetViewRotation();
		const FVector PlacementAnchorCameraSpace =
			CameraRotation.UnrotateVector(PlacementAnchorWorld - CameraPosition);
		if (PlacementAnchorCameraSpace.X <= UE_KINDA_SMALL_NUMBER)
		{
			return EShotEditorReverseSolveStatus::PlacementAnchorBehindCamera;
		}
	}

	return EShotEditorReverseSolveStatus::Ok;
}

bool FComposableCameraShotEditorViewportClient::CanReverseSolveCurrentCamera() const
{
	if (TSharedPtr<FComposableCameraShotAuthoringSession> Session = AuthoringSession.Pin(); Session && !Session->CanEdit()) return false;
	return DiagnoseReverseSolveCurrentCamera() == EShotEditorReverseSolveStatus::Ok;
}

FText ShotEditorReverseSolveStatusToText(EShotEditorReverseSolveStatus Status)
{
	switch (Status)
	{
	case EShotEditorReverseSolveStatus::Ok:
		return FText::GetEmpty();
	case EShotEditorReverseSolveStatus::NoActiveShot:
		return LOCTEXT("ReverseSolveStatus_NoActiveShot",
			"No Shot is bound to the editor.");
	case EShotEditorReverseSolveStatus::EffectiveShotInvalid:
		return LOCTEXT("ReverseSolveStatus_EffectiveShotInvalid",
			"Shot data could not be resolved (binding overrides may be incomplete).");
	case EShotEditorReverseSolveStatus::PlacementAnchorUnresolvable:
		return LOCTEXT("ReverseSolveStatus_PlacementAnchorUnresolvable",
			"Placement anchor is unresolvable - assign a valid Target to the Placement layer.");
	case EShotEditorReverseSolveStatus::AimAnchorUnresolvable:
		return LOCTEXT("ReverseSolveStatus_AimAnchorUnresolvable",
			"Aim anchor is unresolvable - assign a valid Target to the Aim layer.");
	case EShotEditorReverseSolveStatus::PlacementAnchorBehindCamera:
		return LOCTEXT("ReverseSolveStatus_PlacementAnchorBehindCamera",
			"Placement anchor is at or behind the camera - move the camera so the anchor is in front of it.");
	case EShotEditorReverseSolveStatus::SourceReadOnly:
		return LOCTEXT("ReverseSolveStatus_SourceReadOnly", "The Shot section or its sequence is locked.");
	}
	return FText::GetEmpty();
}

bool FComposableCameraShotEditorViewportClient::ReverseSolveCurrentCameraToShot()
{
	if (TSharedPtr<FComposableCameraShotAuthoringSession> Session = AuthoringSession.Pin(); Session && !Session->CanEdit()) return false;
	using namespace ComposableCameraSystem::ShotSolver;

	if (!Viewport)
	{
		return false;
	}

	const EShotEditorReverseSolveStatus Status = DiagnoseReverseSolveCurrentCamera();
	if (Status != EShotEditorReverseSolveStatus::Ok)
	{
		UE_LOG(LogComposableCameraSystemEditor, Warning,
			TEXT("ShotEditor: reverse-solve aborted - %s"),
			*ShotEditorReverseSolveStatusToText(Status).ToString());
		return false;
	}

	FComposableCameraShot EffectiveShot;
	BuildEffectiveShotForPreview(EffectiveShot);

	FVector PlacementAnchorWorld;
	FVector AimAnchorWorld;
	EffectiveShot.Placement.PlacementAnchor.ResolveWorldPosition(EffectiveShot.Targets, PlacementAnchorWorld);
	EffectiveShot.Aim.AimAnchor.ResolveWorldPosition(EffectiveShot.Targets, AimAnchorWorld);

	const FVector CameraPosition = GetViewLocation();
	const FRotator CameraRotation = GetViewRotation();
	const float FieldOfView = ViewFOV;
	const float ViewportAspectRatio = GetCameraAspectRatio();
	const float TanHalfHorizontalFOV = FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5f));
	const float TanHalfVerticalFOV = TanHalfHorizontalFOV / ViewportAspectRatio;

	const EShotPlacementMode PlacementMode = EffectiveShot.Placement.Mode;

	float NewDistance = ActiveShot->Placement.Distance;
	FVector2D NewLocalCameraDirection = ActiveShot->Placement.LocalCameraDirection;
	FVector2D NewPlacementScreenPosition = ActiveShot->Placement.ScreenPosition;
	FVector NewFixedWorldPosition = ActiveShot->Placement.FixedWorldPosition;

	switch (PlacementMode)
	{
	case EShotPlacementMode::AnchorOrbit:
		{
			const FVector AnchorToCamera = CameraPosition - PlacementAnchorWorld;
			NewDistance = FMath::Clamp(static_cast<float>(AnchorToCamera.Length()),
				FShotPlacement::MinDistance, FShotPlacement::MaxDistance);
			const FVector DirectionWorld = (NewDistance > UE_KINDA_SMALL_NUMBER)
				? AnchorToCamera / NewDistance
				: FVector(1.f, 0.f, 0.f);

			const FQuat Basis = ResolvePlacementBasis(EffectiveShot);
			const FVector DirectionLocal = Basis.Inverse().RotateVector(DirectionWorld);

			NewLocalCameraDirection.X =
				FMath::RadiansToDegrees(FMath::Atan2(DirectionLocal.Y, DirectionLocal.X));
			NewLocalCameraDirection.Y =
				FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(DirectionLocal.Z, -1.f, 1.f)));
			break;
		}

	case EShotPlacementMode::AnchorAtScreen:
		{
			const FVector PlacementAnchorCameraSpace =
				CameraRotation.UnrotateVector(PlacementAnchorWorld - CameraPosition);
			NewDistance = FMath::Clamp(static_cast<float>(PlacementAnchorCameraSpace.X),
				FShotPlacement::MinDistance, FShotPlacement::MaxDistance);
			NewPlacementScreenPosition.X =
				FMath::Clamp(static_cast<float>(PlacementAnchorCameraSpace.Y
					/ (2.f * TanHalfHorizontalFOV * PlacementAnchorCameraSpace.X)), -0.5f, 0.5f);
			NewPlacementScreenPosition.Y =
				FMath::Clamp(static_cast<float>(PlacementAnchorCameraSpace.Z
					/ (2.f * TanHalfVerticalFOV * PlacementAnchorCameraSpace.X)), -0.5f, 0.5f);
			break;
		}

	case EShotPlacementMode::FixedWorldPosition:
		NewFixedWorldPosition = CameraPosition;
		break;
	}

	FVector2D NewAimScreenPosition = ActiveShot->Aim.ScreenPosition;
	if (EffectiveShot.Aim.Mode == EShotAimMode::LookAtAnchor)
	{
		FVector2D ProjectedAimScreenPosition;
		if (ComposableCameraSystem::ProjectWorldPointToScreen(AimAnchorWorld,
			CameraPosition,
			CameraRotation,
			TanHalfHorizontalFOV,
			ViewportAspectRatio,
			ProjectedAimScreenPosition))
		{
			NewAimScreenPosition.X = FMath::Clamp(ProjectedAimScreenPosition.X, -0.5f, 0.5f);
			NewAimScreenPosition.Y = FMath::Clamp(ProjectedAimScreenPosition.Y, -0.5f, 0.5f);
		}
	}

	{
		FScopedTransaction ReverseSolveTransaction(
			LOCTEXT("ReverseSolveCamera", "Save Camera Framing as Shot Params"));

		UObject* Host = ActiveHost.Get();
		if (Host)
		{
			SaveToTransactionBuffer(Host, /*bMarkDirty=*/false);
		}

		switch (PlacementMode)
		{
		case EShotPlacementMode::AnchorOrbit:
			ActiveShot->Placement.Distance = NewDistance;
			ActiveShot->Placement.LocalCameraDirection = NewLocalCameraDirection;
			break;
		case EShotPlacementMode::AnchorAtScreen:
			ActiveShot->Placement.Distance = NewDistance;
			ActiveShot->Placement.ScreenPosition = NewPlacementScreenPosition;
			break;
		case EShotPlacementMode::FixedWorldPosition:
			ActiveShot->Placement.FixedWorldPosition = NewFixedWorldPosition;
			break;
		}

		ActiveShot->Aim.ScreenPosition = NewAimScreenPosition;
		ActiveShot->Roll = FMath::UnwindDegrees(CameraRotation.Roll);

		if (ActiveShot->Lens.FOVMode == EShotFOVMode::Manual)
		{
			ActiveShot->Lens.ManualFOV = FieldOfView;
		}

		NotifyCommittedEdit();
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

void FComposableCameraShotEditorViewportClient::RebuildProxies()
{
	DestroyProxies();
	LastRebuiltHost = ActiveHost;

	if (!ActiveShot || !PreviewScene)
	{
		return;
	}

	ProxyActors.Reserve(ActiveShot->Targets.Num());
	LastResolvedSources.Reset(ActiveShot->Targets.Num());
	LastResolvedPreviewMeshes.Reset(ActiveShot->Targets.Num());
	LastResolvedComponentNames.Reset(ActiveShot->Targets.Num());
	for (int32 i = 0; i < ActiveShot->Targets.Num(); ++i)
	{
		AActor* Source = ResolveSourceActorForTargetIndex(i);
		USkeletalMesh* PreviewMesh = ResolvePreviewMeshForTargetIndex(i);
		const FTransform PreviewTransform = ResolvePreviewTransformForTargetIndex(i);
		FTransform MeshRelative = FTransform::Identity;
#if WITH_EDITORONLY_DATA
		MeshRelative = ActiveShot->Targets[i].Target.EditorPreviewMeshRelativeTransform;
#endif
		AActor* Proxy = SpawnProxyForTarget(Source, PreviewMesh, PreviewTransform, ActiveShot->Targets[i].Target.ComponentName, MeshRelative);
		ProxyActors.Add(Proxy);
		LastResolvedSources.Add(Source);
		LastResolvedPreviewMeshes.Add(PreviewMesh);
		LastResolvedComponentNames.Add(ActiveShot->Targets[i].Target.ComponentName);
	}
}

void FComposableCameraShotEditorViewportClient::DestroyProxies()
{
	for (TWeakObjectPtr<AActor>& WeakProxy: ProxyActors)
	{
		if (AActor* Proxy = WeakProxy.Get())
		{
			Proxy->Destroy();
		}
	}
	ProxyActors.Reset();
	LastResolvedSources.Reset();
	LastResolvedPreviewMeshes.Reset();
	LastResolvedComponentNames.Reset();
}

void FComposableCameraShotEditorViewportClient::SyncProxyTransforms()
{
	if (!ActiveShot)
	{
		return;
	}

	const int32 N = FMath::Min(ProxyActors.Num(), ActiveShot->Targets.Num());
	for (int32 i = 0; i < N; ++i)
	{
		AActor* Proxy = ProxyActors[i].Get();
		if (!Proxy)
		{
			continue;
		}
		// Use the same override-aware resolver as RebuildProxies so a
		// Section's TargetActorOverrides actually drive per-frame transform
		// sync. The previous direct read of `Targets[i].Target.Actor.Get()`
		// hit the placeholder (which is None for binding-driven sections)
		// and parked the proxy at z=-100000, making the preview useless.
		AActor* Source = ResolveSourceActorForTargetIndex(i);
		if (!Source)
		{
			// Template SKM preview has no live source actor. Drive the proxy
			// from editor-authored preview transform so pure ShotAsset
			// authoring can arrange multiple targets in space.
			const FTransform PreviewTransform =
				ResolvePreviewTransformForTargetIndex(i);
			if (ResolvePreviewMeshForTargetIndex(i))
			{
				Proxy->SetActorTransform(PreviewTransform);
#if WITH_EDITORONLY_DATA
				if (USkeletalMeshComponent* Mesh = Proxy->FindComponentByClass<USkeletalMeshComponent>())
					Mesh->SetRelativeTransform(ActiveShot->Targets[i].Target.EditorPreviewMeshRelativeTransform);
#endif
			}
			else
			{
				Proxy->SetActorLocationAndRotation(PreviewTransform.GetLocation(),
					PreviewTransform.GetRotation());
			}
			continue;
		}

		// Source-mesh-component-aware transform sync - same rationale as in
		// SpawnProxyForTarget: ACharacter offsets its Mesh component by
		// (0, 0, -88) within the actor, so source mesh world transform !=
		// source actor world transform. ASkeletalMeshActor / AStaticMeshActor
		// proxies have mesh-as-root, so syncing proxy actor transform to
		// SOURCE MESH world transform keeps the proxy mesh aligned with the
		// source mesh on every frame (without this, ACharacter sources end
		// up 88cm higher in the preview than they are in the level).
		FTransform SrcTransform;
		if (USkeletalMeshComponent* SrcSK = ResolveSourceMesh<USkeletalMeshComponent>(Source, ActiveShot->Targets[i].Target.ComponentName))
		{
			SrcTransform = SrcSK->GetComponentTransform();

			// Direct component-space pose copy: read source's CST array
			// (post-anim-eval bone transforms in component space), write
			// onto proxy's editable CST, flip the double buffer. Skips
			// both LeaderPose and AnimBP entirely on the proxy side, so
			// Sequencer's transient bone-array resets on the source can't
			// surface as A-pose flashes on the proxy - the next frame's
			// SyncProxyTransforms reads whatever the source has settled
			// to AFTER Sequencer finishes its evaluation pass. Requires
			// matching skeleton (same SkeletalMeshAsset on both sides);
			// SpawnProxyForTarget sets ProxySK->SetSkeletalMeshAsset(Mesh)
			// from SrcSK's mesh asset so this holds.
			if (USkeletalMeshComponent* ProxySK = Cast<USkeletalMeshComponent>(Proxy->GetRootComponent()))
			{
				const TArray<FTransform>& SrcCST = SrcSK->GetComponentSpaceTransforms();
				TArray<FTransform>& ProxyCST = ProxySK->GetEditableComponentSpaceTransforms();
				if (SrcCST.Num() > 0 && SrcCST.Num() == ProxyCST.Num())
				{
					ProxyCST = SrcCST;
					ProxySK->ApplyEditedComponentSpaceTransforms();
				}
			}
		}
		else if (UStaticMeshComponent* SrcSM = ResolveSourceMesh<UStaticMeshComponent>(Source, ActiveShot->Targets[i].Target.ComponentName))
		{
			SrcTransform = SrcSM->GetComponentTransform();
		}
		else
		{
			// No mesh component (cylinder fallback case) - use actor transform
			// directly since the proxy was authored against actor location.
			SrcTransform = Source->GetActorTransform();
		}

		// Proxy's scale is preserved on a per-proxy basis (the capsule
		// fallback sets a non-unit scale; SK / SM proxies stay at unit).
		// So we copy location + rotation only.
		Proxy->SetActorLocationAndRotation(SrcTransform.GetLocation(), SrcTransform.GetRotation());
	}
}

void FComposableCameraShotEditorViewportClient::RunSolverAndDriveCamera(float DeltaSeconds)
{
	using namespace ComposableCameraSystem::ShotSolver;

	if (!ActiveShot || !Viewport)
	{
		return;
	}
	if (bUseLevelWorld && CurrentMode != EShotEditorMode::Free)
	{
		if (UCineCameraComponent* Camera = OutputCamera.Get())
		{
			FMinimalViewInfo ViewInfo;
			Camera->GetCameraView(0.f, ViewInfo);
			ControllingActorViewInfo = ViewInfo;
			ControllingActorViewInfo.bConstrainAspectRatio = true;
			ControllingActorViewInfo.AspectRatio = GetCameraAspectRatio();
			SetViewLocation(ViewInfo.Location);
			SetViewRotation(ViewInfo.Rotation);
			ViewFOV = ViewInfo.FOV;
			CachedFocusDistance = Camera->CurrentFocusDistance;
			CachedAperture = Camera->CurrentAperture;
			bCachedDoFValid = true;
			return;
		}
	}
	// Detached templates and free inspection retain the configured camera aspect.
	ControllingActorViewInfo.bConstrainAspectRatio = true;
	ControllingActorViewInfo.AspectRatio = GetCameraAspectRatio();

	FShotSolveContext Context;
	Context.ViewportAspectRatio = GetCameraAspectRatio();
	// Use the current ViewFOV as the previous-frame FOV - the solver's
	// SolvedFromBoundsFit mode converges in 1-2 frames; Manual mode ignores it.
	Context.PreviousFrameFOV = ViewFOV;

	// Apply per-section TargetActorOverrides before solving so the camera
	// follows the bound LS actor instead of the Shot's placeholder. Without
	// this, an Inline shot whose Targets[i].Actor is None would always
	// produce an UNRESOLVED solve in the preview viewport even though the
	// runtime evaluation (LSComponent's TrackInstance push) does see the
	// override-resolved actors.
	FComposableCameraShot EffectiveShot;
	if (!BuildEffectiveShotForPreview(EffectiveShot))
	{
		return;
	}

	// Seed AutoFromComponentBounds caches on the EffectiveShot copy. Editor
	// preview can't honor `EBoundsCachePolicy::StaticSnapshot` cleanly: the
	// runtime node refreshes once in `OnInitialize` against its own Shot
	// instance, but here EffectiveShot is rebuilt every tick from ActiveShot,
	// AND Sequencer binding overrides may resolve to a different Actor than
	// ActiveShot.Target.Actor (which is often None for binding-driven Shot
	// Sections) - so a seed-once-on-host-change pass against ActiveShot would
	// silently no-op for the override case. Refreshing per-tick on the
	// EffectiveShot copy mirrors `EBoundsCachePolicy::Live` semantics and
	// trades a `GetComponentsBoundingBox` call per AutoFromComponentBounds
	// target per tick (O(actor component count), single-actor scope) for
	// "bounds-fit FOV actually works in the editor preview". The cache write
	// lives on the discarded local; ActiveShot is not touched.
	for (FComposableCameraShotTarget& T: EffectiveShot.Targets)
	{
		if (T.BoundsShape == EShotTargetBoundsShape::AutoFromComponentBounds)
		{
			T.RefreshAutoBoundsCache();
		}
	}

	// Hand the solver a prior-pose snapshot when zones may need it. Null
	// on the first solve after Shot bind / cache invalidation - the solver
	// then takes the hard-constraint path to seed an initial pose.
	FShotPriorPose PriorPose;
	const FShotPriorPose* PriorPosePtr = nullptr;
	if (bHasCachedPriorPose)
	{
		PriorPose.Position = CachedPriorPos;
		PriorPose.Rotation = CachedPriorRot;
		PriorPose.LastDistance = CachedPriorDistance;
		PriorPose.LastFOV = CachedPriorFOV;
		PriorPose.LastRoll = CachedPriorRoll;
		PriorPosePtr = &PriorPose;
	}

	const FShotSolveResult Result = SolveShot(EffectiveShot, Context, PriorPosePtr, DeltaSeconds);

	// LENS (FOV) - always applied, regardless of `Result.bValid` AND
	// regardless of editor mode. The solver pre-fills `R.FieldOfView`
	// before the pose-failure exits (Manual mode -> ManualFOV exactly;
	// SolvedFromBoundsFit on pose-fail -> PreviousFrameFOV best-effort),
	// so this assignment is meaningful even when an anchor can't resolve.
	// Without this, dragging the
	// Manual FOV slider (or any Details-panel value) is invisible to the
	// designer whenever the Shot is in an unsolvable state - they have
	// no way to escape because the slider that would fix it appears
	// frozen.
	ViewFOV = Result.FieldOfView;
	ControllingActorViewInfo.FOV = Result.FieldOfView;

	if (!Result.bValid)
	{
		// Pose unresolvable - leave camera where it was so the designer
		// can still see whatever proxies are spawned. Lens (above) was
		// already applied so Details-panel slider drags stay live and
		// the designer can drag toward a solvable configuration. Skip
		// the DoF push (focus distance from a stale pose would be wrong)
		// and the pose write.
		//
		// Prior-pose cache is not cleared here - when the shot becomes
		// resolvable again, projecting through the last known good pose
		// is a better starting point for the zone math than a hard-seed,
		// because the visible camera pose is still that pose. (Mirrors
		// the runtime FramingNode's `OnTickNode` behavior.)
		bCachedDoFValid = false;
		return;
	}

	// Cache the solved pose for next-frame zone preprocessing. Done
	// regardless of mode so that switching from Free -> Drag still has
	// a usable prior.
	CachedPriorPos = Result.CameraPosition;
	CachedPriorRot = Result.CameraRotation;
	CachedPriorDistance = Result.EffectiveDistance;
	CachedPriorFOV = Result.FieldOfView;
	CachedPriorRoll = Result.CameraRotation.Roll;
	bHasCachedPriorPose = true;

	if (CurrentMode != EShotEditorMode::Free)
	{
		SetViewLocation(Result.CameraPosition);
		SetViewRotation(Result.CameraRotation);
	}
	else
	{
		FRotator FreeRot = GetViewRotation();
		const float TargetRoll = ActiveShot ? ActiveShot->Roll: 0.f;
		if (!FMath::IsNearlyEqual(FreeRot.Roll, TargetRoll, 1e-3f))
		{
			FreeRot.Roll = TargetRoll;
			SetViewRotation(FreeRot);
		}
	}

	float EffectiveFocusDistance = Result.FocusDistance;
	if (CurrentMode == EShotEditorMode::Free)
	{
		EffectiveFocusDistance = SolveFocus(
			*ActiveShot, GetViewLocation(), GetViewRotation());
	}

	// Cache for HUD readout.
	CachedFocusDistance = EffectiveFocusDistance;
	CachedAperture = Result.Aperture;
	bCachedDoFValid = true;

	// CineCamera-style DoF injection. Writes directly into
	// `ControllingActorViewInfo.PostProcessSettings` because that's the
	// route the engine reads when `bUseControllingActorViewInfo == true`
	// (see EditorViewportClient.cpp:1444-1448). The virtual
	// `OverridePostProcessSettings` hook is the OTHER branch (fires only
	// when bUseControllingActorViewInfo is false) and would never be
	// called in our setup.
	//
	// Mirrors the subset of fields `UCineCameraComponent::GetCameraView`
	// populates: FocalDistance + Fstop drive the bokeh circle radius (the
	// visible "out-of-focus" amount), SensorWidth scales the relationship
	// between f-stop and bokeh size (smaller sensor = less bokeh per f-stop).
	// Shot does not currently expose sensor width. Defaulting to 35mm
	// full-frame (36mm horizontal) matches CineCamera's default
	// `Filmback.SensorWidth` so the preview matches stock CineCamera playback.
	FPostProcessSettings& PP = ControllingActorViewInfo.PostProcessSettings;

	PP.bOverride_DepthOfFieldFocalDistance = true;
	PP.DepthOfFieldFocalDistance = FMath::Max(EffectiveFocusDistance, 1.f);

	PP.bOverride_DepthOfFieldFstop = true;
	PP.DepthOfFieldFstop = FMath::Max(Result.Aperture, 0.5f);

	PP.bOverride_DepthOfFieldSensorWidth = true;
	PP.DepthOfFieldSensorWidth = 36.f; // 35mm full-frame
}

AActor* FComposableCameraShotEditorViewportClient::ResolveSourceActorForTargetIndex(int32 TargetIndex) const
{
	if (TSharedPtr<FComposableCameraShotAuthoringSession> Session = AuthoringSession.Pin()) return Session->ResolveTarget(TargetIndex);
	if (!ActiveShot || !ActiveShot->Targets.IsValidIndex(TargetIndex))
	{
		return nullptr;
	}

	// Per-section TargetActorOverrides take precedence over the Shot's
	// authored TSoftObjectPtr<AActor>. Only meaningful when the host is a
	// UMovieSceneComposableCameraShotSection - for ShotAsset / direct
	// CompositionFramingNode hosts, no override mechanism applies.
	if (UObject* Host = ActiveHost.Get())
	{
		if (UMovieSceneComposableCameraShotSection* Section = Cast<UMovieSceneComposableCameraShotSection>(Host))
		{
			for (const FComposableCameraShotTargetActorOverride& Override: Section->TargetActorOverrides)
			{
				if (Override.TargetIndex != TargetIndex || !Override.Binding.IsValid())
				{
					continue;
				}

				// Locate the open Sequencer for this section's owning
				// sequence. Without an open Sequencer there's no playback
				// context to resolve binding GUIDs against - fall through
				// to the Shot's authored placeholder. Same edge case as
				// "user closed the Sequencer window before opening the
				// Shot Editor".
				UMovieSceneSequence* Sequence = Section->GetTypedOuter<UMovieSceneSequence>();
				if (!Sequence)
				{
					continue;
				}

				FComposableCameraSystemEditorModule& EditorModule =
					FModuleManager::GetModuleChecked<FComposableCameraSystemEditorModule>("ComposableCameraSystemEditor");
				const TSharedPtr<ISequencer> OpenSequencer = EditorModule.FindOpenSequencerForSequence(Sequence);
				if (!OpenSequencer.IsValid())
				{
					continue;
				}

				// FindBoundObjects walks the sequence instance's binding map.
				// Possessables resolve immediately to their level-world actor;
				// Spawnables resolve only while their binding's section is
				// active in the timeline (so user must scrub the playhead
				// inside the spawnable's range to see it in the preview).
				// FocusedTemplateID handles sub-sequences correctly (binding
				// GUIDs are namespaced per-sub-sequence).
				const TArrayView<TWeakObjectPtr<>> Bound =
					OpenSequencer->FindBoundObjects(Override.Binding.GetGuid(), OpenSequencer->GetFocusedTemplateID());
				for (TWeakObjectPtr<UObject> Weak: Bound)
				{
					if (AActor* Actor = Cast<AActor>(Weak.Get()))
					{
						return Actor;
					}
				}
				// Override entry exists but binding doesn't resolve this
				// frame - fall back to placeholder so the proxy doesn't
				// flicker between live actor and cylinder.
				break;
			}
		}
	}

	// Authored runtime actor / placeholder.
	if (AActor* AuthoredActor = ActiveShot->Targets[TargetIndex].Target.Actor.Get())
	{
		return AuthoredActor;
	}

	return nullptr;
}

USkeletalMesh* FComposableCameraShotEditorViewportClient::ResolvePreviewMeshForTargetIndex(int32 TargetIndex) const
{
#if WITH_EDITORONLY_DATA
	if (!ActiveShot || !ActiveShot->Targets.IsValidIndex(TargetIndex))
	{
		return nullptr;
	}
	const TSoftObjectPtr<USkeletalMesh>& PreviewMesh =
		ActiveShot->Targets[TargetIndex].Target.EditorPreviewMesh;
	return PreviewMesh.IsNull() ? nullptr: PreviewMesh.LoadSynchronous();
#else
	return nullptr;
#endif
}

FTransform FComposableCameraShotEditorViewportClient::ResolvePreviewTransformForTargetIndex(int32 TargetIndex) const
{
#if WITH_EDITORONLY_DATA
	if (!ActiveShot || !ActiveShot->Targets.IsValidIndex(TargetIndex))
	{
		return FTransform::Identity;
	}
	return ActiveShot->Targets[TargetIndex].Target.EditorPreviewTransform;
#else
	return FTransform::Identity;
#endif
}

bool FComposableCameraShotEditorViewportClient::EnsureEffectiveShotCache() const
{
	if (bEffectiveShotCacheValid) return bEffectiveShotCacheBuiltOk;

	if (!ActiveShot)
	{
		bEffectiveShotCacheValid = true;
		bEffectiveShotCacheBuiltOk = false;
		return false;
	}

	// Keep one resolved value buffer per frame. Read-only HUD cards borrow it;
	// solver and existing callers still receive independent mutable copies.
	if (UMovieSceneComposableCameraShotSection* Section =
			Cast<UMovieSceneComposableCameraShotSection>(ActiveHost.Get()))
	{
		if (!Section->BuildEffectiveShotWithoutBindings(CachedEffectiveShot))
		{
			bEffectiveShotCacheValid = true;
			bEffectiveShotCacheBuiltOk = false;
			return false;
		}
	}
	else
	{
		CachedEffectiveShot = *ActiveShot;
	}
	for (int32 i = 0; i < CachedEffectiveShot.Targets.Num(); ++i)
	{
		bool bExplicitBinding = false;
		if (const auto* Section = Cast<UMovieSceneComposableCameraShotSection>(ActiveHost.Get()))
			for (const auto& Override : Section->TargetActorOverrides)
				bExplicitBinding |= Override.TargetIndex == i && Override.Binding.IsValid();
		CachedEffectiveShot.Targets[i].Target.Actor.Reset();
		// `ResolveSourceActorForTargetIndex` already encapsulates the
		// override->placeholder -> null fallback chain. Assigning a raw
		// AActor* into the soft-pointer captures the actor's path so the
		// solver's downstream `.Get()` resolves to the same instance.
		// When the resolver returns nullptr we leave the field as-is from
		// the placeholder copy (which may also be null - solver will
		// UNRESOLVED gracefully in that case).
		if (AActor* Resolved = ResolveSourceActorForTargetIndex(i))
		{
			CachedEffectiveShot.Targets[i].Target.Actor = Resolved;
		}
#if WITH_EDITORONLY_DATA
		else if (!bUseLevelWorld && !bExplicitBinding && ResolvePreviewMeshForTargetIndex(i) && ProxyActors.IsValidIndex(i))
		{
			if (AActor* Proxy = ProxyActors[i].Get())
			{
				CachedEffectiveShot.Targets[i].Target.Actor = Proxy;
				// A template proxy has a single mesh; the named source component does not exist in this world.
				if (!CachedEffectiveShot.Targets[i].Target.ComponentName.IsNone())
					if (const USkeletalMeshComponent* Mesh = Proxy->FindComponentByClass<USkeletalMeshComponent>()) CachedEffectiveShot.Targets[i].Target.ComponentName = Mesh->GetFName();
			}
		}
#endif
	}

	bEffectiveShotCacheValid = true;
	bEffectiveShotCacheBuiltOk = true;
	return true;
}

bool FComposableCameraShotEditorViewportClient::BuildEffectiveShotForPreview(FComposableCameraShot& OutShot) const
{
	if (!EnsureEffectiveShotCache()) return false;
	OutShot = CachedEffectiveShot;
	return true;
}

AActor* FComposableCameraShotEditorViewportClient::SpawnProxyForTarget(AActor* SourceActor,
	USkeletalMesh* PreviewMesh,
	const FTransform& PreviewTransform, FName ComponentName, const FTransform& MeshRelativeTransform)
{
	if (!PreviewScene)
	{
		return nullptr;
	}
	UWorld* PreviewWorld = PreviewScene->GetWorld();
	if (!PreviewWorld)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags = RF_Transient;
	Params.bNoFail = true;

	// 1. Skeletal mesh? Highest fidelity stand-in - copy the SK mesh asset
	// PLUS the AnimBP class so the proxy plays whatever idle / locomotion
	// animation the source actor has (e.g. ABP_Manny in the UE5 third-
	// person template gives a clean default Idle pose instead of the
	// SkeletalMesh's raw A-pose). `SetUpdateAnimationInEditor(true)` is
	// REQUIRED - without it AnimBP doesn't tick in editor preview worlds.
	//
	// Note: `SetLeaderPoseComponent` was tried as a way to mirror the
	// SOURCE actor's live Sequencer-driven pose into the preview, but
	// cross-world leader-follower (source in level / PIE world, follower
	// in our AdvancedPreviewScene's world) doesn't propagate into the
	// follower's render-scene registration - the follower stays
	// invisible. Fall back to the proxy's own AnimBP tick (independent
	// Idle / locomotion) until a per-bone copy alternative is wired up.
	if (SourceActor)
	{
		if (USkeletalMeshComponent* SourceSK = ResolveSourceMesh<USkeletalMeshComponent>(SourceActor, ComponentName))
		{
			if (USkeletalMesh* Mesh = SourceSK->GetSkeletalMeshAsset())
			{
				ASkeletalMeshActor* Proxy = PreviewWorld->SpawnActor<ASkeletalMeshActor>(Params);
				if (Proxy && Proxy->GetSkeletalMeshComponent())
				{
					USkeletalMeshComponent* ProxySK = Proxy->GetSkeletalMeshComponent();

					// CRITICAL: place the actor at the source's MESH world
					// transform FIRST, before any SetSkeletalMeshAsset / anim
					// init / bounds work. Doing it in the other order leaves
					// the SceneProxy initialized against world-origin bounds;
					// the subsequent transform change marks bounds dirty but
					// the cached SceneProxy in the FScene's static array is
					// already established and frustum culling against camera
					// (155, 619, 164) misses the world-origin sphere->mesh
					// is silently culled and never drawn. Transform-first
					// keeps bounds and SceneProxy aligned from the start.
					//
					// Why source MESH transform, not source ACTOR transform:
					// `ACharacter` defaults Mesh component to RelativeLocation
					// (0, 0, -88) so the mesh's feet align with the capsule's
					// bottom; `ASkeletalMeshActor` (our proxy class) has
					// SkeletalMeshComponent as its ROOT (no -88 offset). If
					// we used the source's actor transform, the proxy mesh
					// would render 88cm higher than the source mesh, and any
					// bone (including head) would project to a different
					// screen position than the solver expects.
					Proxy->SetActorTransform(SourceSK->GetComponentTransform());

					ProxySK->SetSkeletalMeshAsset(Mesh);

					// Disable BOTH the anim graph AND the component tick.
					// Notes after reading SkeletalMeshComponent.cpp ShouldTickPose:
					// - SetUpdateAnimationInEditor(false) only short-
					// circuits when WorldType==Editor. AdvancedPreviewScene
					// uses WorldType::EditorPreview, so that flag is
					// IGNORED and the AnimSingleNode AnimInstance ticks
					// every frame, outputting ref pose which overwrites
					// the CST we manually copy in SyncProxyTransforms.
					// - SetComponentTickEnabled(false) is the unconditional
					// gate - no component tick -> no TickPose -> no anim
					// graph eval -> no CST overwrite. The component still
					// renders, transform changes propagate via
					// SetActorTransform, and we drive the bones via
					// ApplyEditedComponentSpaceTransforms each frame from
					// our viewport's own Tick. No need for the proxy's
					// own tick at all.
					ProxySK->SetAnimationMode(EAnimationMode::AnimationSingleNode);
					ProxySK->SetComponentTickEnabled(false);
					Proxy->SetActorTickEnabled(false);
					ProxySK->UpdateBounds();
					ProxySK->MarkRenderTransformDirty();
					ProxySK->MarkRenderStateDirty();

					return Proxy;
				}
			}
		}

		// 2. Static mesh? Mid fidelity - copy the SM asset.
		if (UStaticMeshComponent* SourceSM = ResolveSourceMesh<UStaticMeshComponent>(SourceActor, ComponentName))
		{
			if (UStaticMesh* Mesh = SourceSM->GetStaticMesh())
			{
				AStaticMeshActor* Proxy = PreviewWorld->SpawnActor<AStaticMeshActor>(Params);
				if (Proxy && Proxy->GetStaticMeshComponent())
				{
					Proxy->GetStaticMeshComponent()->SetStaticMesh(Mesh);
					// Same alignment principle as the SkelMesh path: match
					// the source's MESH COMPONENT world transform so any
					// bone-equivalent reference (e.g. a static-mesh socket)
					// projects faithfully under the solver-driven camera.
					Proxy->SetActorTransform(SourceSM->GetComponentTransform());
					return Proxy;
				}
			}
		}
	}

	// 3. Template preview mesh? Asset-only ShotAsset authoring path.
	if (PreviewMesh)
	{
		ASkeletalMeshActor* Proxy = PreviewWorld->SpawnActor<ASkeletalMeshActor>(Params);
		if (Proxy && Proxy->GetSkeletalMeshComponent())
		{
			USkeletalMeshComponent* ProxySK = Proxy->GetSkeletalMeshComponent();
			USceneComponent* ActorRoot = NewObject<USceneComponent>(Proxy, TEXT("PreviewActorRoot"), RF_Transient);
			Proxy->SetRootComponent(ActorRoot);
			ActorRoot->RegisterComponent();
			ProxySK->AttachToComponent(ActorRoot, FAttachmentTransformRules::KeepRelativeTransform);
			ProxySK->SetRelativeTransform(MeshRelativeTransform);
			Proxy->SetActorTransform(PreviewTransform);
			ProxySK->SetSkeletalMeshAsset(PreviewMesh);
			ProxySK->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			ProxySK->SetComponentTickEnabled(false);
			Proxy->SetActorTickEnabled(false);
			ProxySK->UpdateBounds();
			ProxySK->MarkRenderTransformDirty();
			ProxySK->MarkRenderStateDirty();
			return Proxy;
		}
	}

	// 4. Fallback: capsule-ish cylinder at the source's transform (or origin).
	UStaticMesh* CapsuleMesh = LoadObject<UStaticMesh>(nullptr, kCapsuleFallbackMeshPath);
	AStaticMeshActor* Proxy = PreviewWorld->SpawnActor<AStaticMeshActor>(Params);
	if (Proxy && Proxy->GetStaticMeshComponent())
	{
		if (CapsuleMesh)
		{
			Proxy->GetStaticMeshComponent()->SetStaticMesh(CapsuleMesh);
		}
		Proxy->SetActorScale3D(kCapsuleFallbackScale);
		if (SourceActor)
		{
			Proxy->SetActorLocationAndRotation(SourceActor->GetActorLocation(), SourceActor->GetActorRotation());
		}
		else
		{
			Proxy->SetActorLocationAndRotation(PreviewTransform.GetLocation(),
				PreviewTransform.GetRotation());
		}
	}
	return Proxy;
}
