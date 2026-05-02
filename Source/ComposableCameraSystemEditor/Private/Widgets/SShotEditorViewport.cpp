// Copyright Sulley. All Rights Reserved.

#include "Widgets/SShotEditorViewport.h"

#include "AdvancedPreviewScene.h"
#include "Editors/ComposableCameraShotEditorViewportClient.h"

void SShotEditorViewport::Construct(const FArguments& /*InArgs*/)
{
	// Create the PreviewScene first — the client constructor takes a pointer
	// to it, so it must exist before MakeEditorViewportClient runs.
	//
	// FAdvancedPreviewScene gives the standard "asset preview" look:
	// sky sphere + skylight + reflection capture + floor mesh + post-process,
	// loaded from the project's UAssetViewerSettings profile (same source
	// the StaticMesh / Anim / Persona editors use). Plain FPreviewScene
	// would only give a directional + sky light with a flat dark grey
	// background — wrong baseline for camera framing where designers need
	// to see how the subject reads against environment cues.
	PreviewScene = MakeUnique<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues()
		.SetCreatePhysicsScene(false)        // we don't simulate
		.SetTransactional(false)             // don't track edits in undo
		.AllowAudioPlayback(false));         // no sound in preview

	SEditorViewport::Construct(SEditorViewport::FArguments());
}

SShotEditorViewport::~SShotEditorViewport()
{
	// Two interlocking lifetime concerns (research Q7 + destruction-order
	// review):
	//
	// 1. Research Q7 lifetime gotcha #1 — clear the client's `Viewport`
	//    pointer so a paint between now and the actual client destruction
	//    doesn't dereference a freed FViewport.
	//
	// 2. The base `SEditorViewport` ALSO holds a TSharedPtr to our client.
	//    Our member destruction order is reverse-decl: `ViewportClient`
	//    (this widget's local copy) → `PreviewScene`. After our members
	//    are destroyed, `~SEditorViewport` runs and only THEN drops the
	//    base's ref to the client → the client's destructor finally fires.
	//    By that time our `PreviewScene` is already torn down, so the
	//    client cannot safely call `Proxy->Destroy()`. Drain proxies HERE,
	//    while everything is still alive, via the explicit
	//    `ReleaseSceneResources` API.
	if (ViewportClient.IsValid())
	{
		ViewportClient->Viewport = nullptr;
		ViewportClient->ReleaseSceneResources();
	}
	// Don't explicitly Reset() ViewportClient or PreviewScene — let member
	// destruction (reverse decl: ViewportClient → PreviewScene) handle them
	// in the right order. The proxies were already drained above, so the
	// later client destruction in `~SEditorViewport` has nothing scene-bound
	// to clean up.
}

void SShotEditorViewport::SetActiveShot(FComposableCameraShot* Shot, UObject* HostObject)
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->SetActiveShot(Shot, HostObject);
	}
}

void SShotEditorViewport::SetMode(EShotEditorMode InMode)
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->SetMode(InMode);
	}
}

EShotEditorMode SShotEditorViewport::GetMode() const
{
	return ViewportClient.IsValid() ? ViewportClient->GetMode() : EShotEditorMode::Drag;
}

EShotEditorReverseSolveStatus SShotEditorViewport::DiagnoseReverseSolveCurrentCamera() const
{
	return ViewportClient.IsValid()
		? ViewportClient->DiagnoseReverseSolveCurrentCamera()
		: EShotEditorReverseSolveStatus::NoActiveShot;
}

bool SShotEditorViewport::CanReverseSolveCurrentCamera() const
{
	return ViewportClient.IsValid() && ViewportClient->CanReverseSolveCurrentCamera();
}

bool SShotEditorViewport::ReverseSolveCurrentCameraToShot()
{
	return ViewportClient.IsValid() && ViewportClient->ReverseSolveCurrentCameraToShot();
}

TSharedRef<FEditorViewportClient> SShotEditorViewport::MakeEditorViewportClient()
{
	check(PreviewScene.IsValid());
	ViewportClient = MakeShared<FComposableCameraShotEditorViewportClient>(
		PreviewScene.Get(), SharedThis(this));
	return ViewportClient.ToSharedRef();
}
