// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
// FAdvancedPreviewScene's full definition is needed in the header because
// the `TUniquePtr<FAdvancedPreviewScene>` member's implicit destructor
// (synthesized for any TU that instantiates the widget's ctor / dtor)
// must call `delete` on the type, which requires the complete type to be
// visible. Forward-declaration would compile only if we explicit-defined
// the ctor + dtor in the.cpp; including the header here is simpler and
// the blast radius is small (only `SShotEditorViewport.cpp` and
// `SShotEditorRoot.cpp` include this header, and both are already in the
// editor module that has the `AdvancedPreviewScene` dep).
#include "AdvancedPreviewScene.h"

struct FComposableCameraShot;
class FComposableCameraShotEditorViewportClient;

/**
 * Tri-state mode the Shot Editor's viewport can be in. Replaces the V1.x
 * boolean Manual Mode toggle.
 *
 * - Drag (default): Solver drives camera every frame. Handles (Anchor +
 * per-target screen positions) are interactive - LMB drag updates the
 * authored Shot fields, RMB pops a context menu (Phase D.4.4) for
 * Method (Rotate / Translate). Mouse-only-on-handles policy: clicking
 * empty viewport area does NOT orbit the camera (solver would just
 * overwrite anyway).
 *
 * - Free: User has full mouse-driven camera control (orbit / pan /
 * dolly via base `FEditorViewportClient` defaults). Solver pauses - 
 * camera stays where the user moves it. Handles are drawn at LIVE
 * projected positions of the world anchor / target points (so they
 * visually track those world points as the camera moves around) but
 * are NON-interactive. Toggling back to Drag pops a "save current
 * camera framing as Shot params?" dialog (Phase D.4.3).
 *
 * - Lock: Solver drives camera (same as Drag) but ALL user input is
 * consumed - no handle interaction, no camera control. Read-only
 * preview state for screenshots / demos / "show me what runtime
 * would render".
 */
// Plain C++ enum (no UENUM macro - this header doesn't generate reflection
// metadata and the enum isn't a UPROPERTY anywhere). Reflection-tagged
// Shot enums live in `DataAssets/ComposableCameraShot.h`.
enum class EShotEditorMode: uint8
{
	Drag,
	Free,
	Lock
};

/**
 * Outcome of a reverse-solve precheck (or a completed reverse-solve attempt).
 * Lets the Free -> Drag transition dialog tell designers *why* "Save
 * composition" is unavailable instead of greying it out silently. Order of
 * checks is fixed: ActiveShot->EffectiveShot ->PlacementAnchor->AimAnchor
 * -> camera-frame depth (AnchorAtScreen-only). The first failing check
 * determines the status - later failures are not surfaced because the
 * earlier one already tells the designer what to fix first.
 */
enum class EShotEditorReverseSolveStatus: uint8
{
	Ok,
	NoActiveShot, // engine teardown / no Shot bound - shouldn't surface in normal UX
	EffectiveShotInvalid, // BuildEffectiveShotForPreview failed (override resolution stalled)
	PlacementAnchorUnresolvable, // Placement.PlacementAnchor.ResolveWorldPosition returned false
	AimAnchorUnresolvable, // Aim.AimAnchor.ResolveWorldPosition returned false
	PlacementAnchorBehindCamera, // AnchorAtScreen-only: PCam.X <= under user's free-flown rotation
};

/**
 * Per-status reason text shown in the Free -> Drag dialog body when
 * `Save composition` is unavailable. `Ok` returns empty text (caller picks
 * the success-path body instead). One sentence each - designer-actionable
 * ("assign a target", "move the camera in front of the anchor"), not
 * developer-facing ("BuildEffectiveShotForPreview failed").
 */
COMPOSABLECAMERASYSTEMEDITOR_API FText ShotEditorReverseSolveStatusToText(EShotEditorReverseSolveStatus Status);

/**
 * SEditorViewport subclass that fills the Shot Editor's middle splitter
 * region (Phase D.2). Owns the FPreviewScene + FEditorViewportClient that
 * render the camera-framing preview.
 *
 * Composition (research Q1 - engine canonical pattern):
 * SShotEditorRoot
 * SSplitter
 * SShotEditorViewport (this - extends SEditorViewport)
 * FAdvancedPreviewScene (sky sphere + skylight + floor +
 * post-process - same baseline
 * StaticMesh / Persona editors use)
 * FComposableCameraShotEditorViewportClient
 * ProxyActors (one per Shot.Targets entry)
 * SolveShot output->SetViewLocation/Rotation/FOV
 *
 * Lifetime gotchas (research Q7):
 * - Destructor MUST clear `EditorViewportClient->Viewport = nullptr;`
 * before the SWidget tears down, otherwise the client outlives the
 * FSceneViewport and the next Draw dereferences a freed pointer.
 */
class COMPOSABLECAMERASYSTEMEDITOR_API SShotEditorViewport: public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SShotEditorViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual ~SShotEditorViewport() override;

	/** Forwarded from SShotEditorRoot::SetActiveShot. Triggers proxy rebuild
	 * on the next viewport tick. Both args may be null. */
	void SetActiveShot(FComposableCameraShot* Shot, UObject* HostObject);

	/** Forwarded mode setter for the Shot Editor's toolbar SSegmentedControl.
	 * See EShotEditorMode above for per-mode semantics. */
	void SetMode(EShotEditorMode InMode);
	EShotEditorMode GetMode() const;

	/** Forwarded reverse-solve API for the Free -> Drag transition dialog
	 * (Phase D.4.3). `DiagnoseReverseSolveCurrentCamera` returns the
	 * precheck status - `Ok` when reverse-solve will succeed, otherwise
	 * the first failing check (see `EShotEditorReverseSolveStatus`).
	 * `CanReverseSolveCurrentCamera` is the boolean shortcut, kept for
	 * call sites that don't need a reason. `ReverseSolveCurrentCameraToShot`
	 * performs the actual write (wrapped in a transaction internally). */
	EShotEditorReverseSolveStatus DiagnoseReverseSolveCurrentCamera() const;
	bool CanReverseSolveCurrentCamera() const;
	bool ReverseSolveCurrentCameraToShot();

	/** Copy this preview viewport's current camera transform to the system
	 * clipboard as FTransform text that UE property rows can paste. */
	bool CopyCurrentCameraTransformToClipboard() const;

protected:
	// SEditorViewport overrides 

	/** Factory hook - returns our FEditorViewportClient subclass. Called once
	 * during SEditorViewport::Construct, after PreviewScene is built. */
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

	/** SEditorViewport calls this to populate any toolbar overlay; we return
	 * no overlay in Phase D.2 (no toolbar, just the rendered scene). D.3+
	 * may add one (free-look toggle, view mode switcher). */
	virtual TSharedPtr<SWidget> MakeViewportToolbar() override { return nullptr; }

private:
	/** PreviewScene owned by this widget (NOT by the client - the client
	 * borrows it via raw pointer; FAdvancedPreviewScene IS-A FPreviewScene
	 * so the client's pointer type stays unchanged). Kept as a unique_ptr
	 * so the scene outlives any in-flight tick when the widget tears down.
	 *
	 * FAdvancedPreviewScene provides sky sphere + skylight + reflection
	 * capture + floor mesh + post-process baseline - same setup as engine
	 * StaticMesh / Persona editors, far better visual baseline than plain
	 * FPreviewScene (which only has a directional light + sky light, no
	 * background, no floor). */
	TUniquePtr<FAdvancedPreviewScene> PreviewScene;

	/** The FEditorViewportClient subclass that does all the work. */
	TSharedPtr<FComposableCameraShotEditorViewportClient> ViewportClient;
};
