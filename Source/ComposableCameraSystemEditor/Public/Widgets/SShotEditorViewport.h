// Copyright 2026 Sulley. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
// FAdvancedPreviewScene's full definition is needed in the header because
// the `TUniquePtr<FAdvancedPreviewScene>` member's implicit destructor
// (synthesized for any TU that instantiates the widget's ctor / dtor)
// must call `delete` on the type, which requires the complete type to be
// visible. Forward-declaration would compile only if we explicit-defined
// the ctor + dtor in the.cpp; including the header here is simpler and the
// blast radius is confined to the editor module that already has the
// `AdvancedPreviewScene` dep.
#include "AdvancedPreviewScene.h"

struct FComposableCameraShot;
class FComposableCameraShotEditorViewportClient;

/**
 * Tri-state mode the Shot Editor's viewport can be in.
 *
 * - Drag (default): Solver drives camera every frame. Handles (Anchor +
 * per-target screen positions) are interactive - LMB drag updates the
 * authored Shot fields, RMB pops the handle context menu. Mouse-only-on-handles policy: clicking
 * empty viewport area does NOT orbit the camera (solver would just
 * overwrite anyway).
 *
 * - Free: User has full mouse-driven camera control (orbit / pan /
 * dolly via base `FEditorViewportClient` defaults). Solver pauses -
 * camera stays where the user moves it. Handles are drawn at LIVE
 * projected positions of the world anchor / target points (so they
 * visually track those world points as the camera moves around) but
 * are NON-interactive. Toggling back to Drag / Lock asks the root widget
 * to show Save / Discard / Stay in the status bar.
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
 * Lets the Free -> Drag / Lock status bar tell designers why "Save
 * composition" is unavailable instead of greying it out silently.
 */
enum class EShotEditorReverseSolveStatus: uint8
{
	Ok,
	NoActiveShot,
	EffectiveShotInvalid,
	PlacementAnchorUnresolvable,
	AimAnchorUnresolvable,
	PlacementAnchorBehindCamera,
};

/** Per-status reason text shown in the Free -> Drag / Lock status bar. */
COMPOSABLECAMERASYSTEMEDITOR_API FText ShotEditorReverseSolveStatusToText(EShotEditorReverseSolveStatus Status);

/**
 * SEditorViewport subclass that fills the Shot Editor's middle splitter
 * region. Owns the FPreviewScene + FEditorViewportClient that render the
 * camera-framing preview.
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

	/** Reverse-solve API for the Free -> Drag / Lock status-bar action. */
	EShotEditorReverseSolveStatus DiagnoseReverseSolveCurrentCamera() const;
	bool CanReverseSolveCurrentCamera() const;
	bool ReverseSolveCurrentCameraToShot();

	/** Snap the preview camera back to the current solved Shot pose without
	 * changing Shot data or leaving Free mode. */
	void ResetViewToShot();

	/** Copy this preview viewport's current camera transform to the system
	 * clipboard as FTransform text that UE property rows can paste. */
	bool CopyCurrentCameraTransformToClipboard() const;

	/** Diagnostic HUD toggle (camera pose / aspect / focus text). */
	bool GetShowDiagnosticHud() const;
	void SetShowDiagnosticHud(bool bInShowDiagnosticHud);

	/** Composition guides toggle (handles, zones, bounds wireframes). */
	bool GetShowCompositionGuides() const;
	void SetShowCompositionGuides(bool bInShowCompositionGuides);

protected:
	// SEditorViewport overrides 

	/** Factory hook - returns our FEditorViewportClient subclass. Called once
	 * during SEditorViewport::Construct, after PreviewScene is built. */
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

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
