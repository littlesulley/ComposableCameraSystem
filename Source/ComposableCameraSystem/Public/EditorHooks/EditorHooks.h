// Copyright Sulley. All rights reserved.

#pragma once

#include "CoreMinimal.h"

struct FComposableCameraShot;

DECLARE_DELEGATE_RetVal(bool, FGetIsSimulatingInEditor);
struct COMPOSABLECAMERASYSTEM_API FIsSimulatingInEditor
{
public:
	static inline FGetIsSimulatingInEditor GetIsSimulatingInEditorDelegate;

	static bool GetIsSimulatingInEditor()
	{
#if WITH_EDITOR
		if (GetIsSimulatingInEditorDelegate.IsBound())
		{
			return GetIsSimulatingInEditorDelegate.Execute();
		}
#endif
		return false;
	}
};

/**
 * Bridge for "open the Shot Editor for this Shot" calls coming from runtime
 * UFUNCTION(CallInEditor) buttons (currently `UComposableCameraCompositionFramingNode::OpenShotEditor`,
 * future Phase E LS Section context menu).
 *
 * The runtime module declares the hook and exposes a guarded execute helper;
 * the editor module binds the hook in `FComposableCameraSystemEditorModule::StartupModule`
 * to the actual tab-spawning logic in `Editors/ComposableCameraShotEditor.h`.
 *
 * Parameters:
 *   - `Shot`     : pointer to the Shot data the editor should bind to. Must
 *                  remain valid for as long as the host UObject is alive
 *                  (the editor stores a raw pointer + a TWeakObjectPtr to
 *                  the host for liveness checks).
 *   - `HostObject`: the UObject that OWNS `Shot` (e.g. the
 *                  `UComposableCameraCompositionFramingNode` whose UPROPERTY
 *                  is the Shot, or the future LS Shot Section). Used by the
 *                  editor for transaction context, MarkPackageDirty, and
 *                  liveness invalidation when the host is GC'd.
 */
DECLARE_DELEGATE_TwoParams(FOpenShotEditorRequest, FComposableCameraShot* /*Shot*/, UObject* /*HostObject*/);
struct COMPOSABLECAMERASYSTEM_API FOpenShotEditor
{
public:
	/** Bound by the editor module; a no-op in cooked / non-editor builds. */
	static inline FOpenShotEditorRequest OpenShotEditorDelegate;

	/** Runtime-side helper. Routes through the delegate iff bound and we're
	 *  in an editor build; silently no-ops otherwise. Safe to call from any
	 *  WITH_EDITOR-conditional UFUNCTION body. */
	static void Open(FComposableCameraShot* Shot, UObject* HostObject)
	{
#if WITH_EDITOR
		if (OpenShotEditorDelegate.IsBound())
		{
			OpenShotEditorDelegate.Execute(Shot, HostObject);
		}
#endif
	}
};

/**
 * Editor-world viewport size resolver. Bound by the editor module to
 * `GEditor->GetActiveViewport()->GetSizeXY()` (or the perspective level
 * viewport's size). Lets runtime helpers — `TryGetEffectiveViewportSize`
 * specifically — return the actual editor-scrub viewport dimensions
 * instead of a hardcoded 1920×1080 fallback. Without this, the Composition
 * Solver runs with a wrong aspect during editor scrub of LS Spawnables,
 * causing anchor screen positions to drift from what designers see in the
 * Shot Editor preview.
 *
 * Returns false when no editor viewport is resolvable (cooked builds, very
 * early startup, headless commandlet) — caller falls back through later
 * resolution steps.
 */
DECLARE_DELEGATE_RetVal_OneParam(bool, FGetActiveEditorViewportSize, FIntPoint& /*OutSize*/);
struct COMPOSABLECAMERASYSTEM_API FGetActiveEditorViewport
{
public:
	static inline FGetActiveEditorViewportSize GetSizeDelegate;

	/** Runtime-side helper. Routes through the delegate iff bound and we're
	 *  in an editor build; silently returns false otherwise. */
	static bool TryGetSize(FIntPoint& OutSize)
	{
#if WITH_EDITOR
		if (GetSizeDelegate.IsBound())
		{
			return GetSizeDelegate.Execute(OutSize);
		}
#endif
		return false;
	}
};