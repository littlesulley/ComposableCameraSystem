# Bug Log

## 2026-06-03 - Runtime Previewer Slate include path compile failure

- Symptom: `SComposableCameraRuntimePreviewer.cpp` failed to compile with
  `C1083: Cannot open include file: 'Widgets/Layout/SHorizontalBox.h'`.
- Trigger / repro: compile the UE5.6 plugin after adding Runtime Previewer.
- Why it happens: `SHorizontalBox` is declared through `Widgets/SBoxPanel.h` in
  UE5.6. `Widgets/Layout/SHorizontalBox.h` is not an engine header.
- Root cause: new Runtime Previewer toolbar code used an invented Slate include
  path instead of the existing local include pattern.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimePreviewer.cpp`
- Fix: replaced `Widgets/Layout/SHorizontalBox.h` with `Widgets/SBoxPanel.h`.
- Regression-test name: IDE compile of `ComposableCameraSystemEditor`.
- Test blocker: project rules prohibit Codex from invoking UBT, Build.bat,
  Unreal Editor, or automation tests from shell. User must rerun IDE compile.
- Avoid next time: grep existing Slate widget files for include patterns before
  introducing a new Slate header path.
- Possible conflicts: none expected; existing editor files already include
  `Widgets/SBoxPanel.h` for `SHorizontalBox`.

## 2026-06-03 - Runtime Previewer character proxy intersects preview floor

- Symptom: character preview appears half underground in Runtime Previewer.
- Trigger / repro: open Runtime Previewer during PIE with a Character pawn whose
  skeletal mesh component has a negative Z offset relative to the pawn root.
- Why it happens: Runtime Previewer uses the pawn transform as the preview
  reference frame, while `FAdvancedPreviewScene` places its floor at Z=0. UE
  Characters often put the skeletal mesh below the capsule/root origin, so the
  floor cuts through the mesh.
- Root cause: preview floor height was fixed instead of derived from the
  pawn-local proxy bounds.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h`
  - `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimePreviewerTests.cpp`
- Fix: compute floor offset from proxy bounds minimum Z and call
  `FAdvancedPreviewScene::SetFloorOffset` after proxy sync. This moves only the
  preview floor, not the pawn-relative camera or proxy transforms.
- Regression-test name:
  `ComposableCameraSystem.RuntimePreviewer.ComputeFloorOffsetForBounds`.
- Test blocker: automation test added, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must run it from IDE /
  Unreal Editor.
- Avoid next time: for pawn-relative previews, treat `FAdvancedPreviewScene`
  floor as a visual aid that must adapt to preview bounds; do not assume pawn
  origin equals floor contact.
- Possible conflicts: none expected. Shot Editor keeps world-space preview
  transforms and does not use this pawn-origin-relative floor rule.

## 2026-06-03 - Runtime Previewer does not follow PIE after first frame

- Symptom: Runtime Previewer opens with correct height, but then appears frozen
  while the player keeps moving in PIE.
- Trigger / repro: open Runtime Previewer during PIE, bind a runtime camera, then
  move the controlled pawn or camera.
- Why it happens: the toolkit pushes new runtime data every debug tick, but the
  preview scene synchronization was performed only from the viewport client's
  tick path. A docked editor viewport can sleep unless Slate is explicitly
  invalidated, so fresh data may not repaint or resync the proxy.
- Root cause: `SetPreviewData` stored the new snapshot without forcing an
  immediate proxy/floor refresh or `SEditorViewport::Invalidate()`.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h`
  - `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimePreviewer.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: added `RefreshPreviewNow()` on the viewport client, call it from
  `SetPreviewData`, and invalidate both the viewport client and
  `SEditorViewport` whenever live data or clear state is pushed.
- Regression-test name: manual PIE Runtime Previewer live-sync check.
- Test blocker: this bug depends on Slate active-timer / dock-tab repaint
  behavior. Project rules prohibit Codex from launching Unreal Editor or
  automation tests from shell, so the user must verify in IDE / Editor PIE.
- Avoid next time: editor preview widgets that receive external per-frame data
  must invalidate themselves at the data handoff point, not only inside the
  viewport tick.
- Possible conflicts: only Runtime Previewer uses this immediate refresh path.
  Graph debug overlays still use normal Slate repaint after snapshot values are
  copied.

## 2026-06-03 - Runtime Previewer swaps camera orbit into character rotation

- Symptom: when rotating the camera in PIE while the character is visually
  still, Runtime Previewer appears to rotate the character while the camera
  marker stays mostly fixed.
- Trigger / repro: open Runtime Previewer during PIE, bind a runtime camera,
  rotate the game camera without moving the character.
- Why it happens: Runtime Previewer used the controlled pawn actor transform as
  the reference frame. Some pawn setups rotate the actor/root/control frame as
  camera input changes while the visible mesh remains still. Converting camera
  and proxy transforms through that root frame cancels the camera orbit and
  pushes the rotation onto the character proxy.
- Root cause: preview reference frame represented pawn root, not the visible
  subject being drawn.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Public/Widgets/SComposableCameraRuntimePreviewer.h`
  - `Source/ComposableCameraSystemEditor/Private/Toolkits/ComposableCameraTypeAssetEditorToolkit.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h`
  - `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimePreviewerTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: store a `SubjectWorldTransform` in preview data and make all proxy,
  camera, and movement-arrow math subject-relative. Subject selection prefers a
  valid skeletal mesh component, then static mesh component, then pawn actor
  transform. Reference scale is stripped.
- Regression-test name:
  `ComposableCameraSystem.RuntimePreviewer.VisualSubjectReferenceKeepsCameraOrbit`.
- Test blocker: automation test added, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must run it from IDE /
  Unreal Editor.
- Avoid next time: for visual preview tools, choose the reference frame from
  the visual subject being drawn, not from a controlling root that may rotate
  for gameplay/camera bookkeeping.
- Possible conflicts: camera relation values now report against the visible
  subject frame instead of pawn root. This is intended for Runtime Previewer and
  does not affect runtime camera evaluation.

## 2026-06-03 - Runtime Previewer still rotates skeletal character after subject-relative fix

- Symptom: after switching from pawn-root-relative preview math to
  subject-relative math, rotating the camera in PIE can still make the Runtime
  Previewer skeletal character appear to rotate.
- Trigger / repro: open Runtime Previewer during PIE, bind a runtime camera, and
  rotate the camera while the visible character should remain stationary.
- Why it happens: `SubjectWorldTransform` was based on the skeletal mesh
  component transform, but the proxy copies the live component-space bone
  transforms directly. If the source animation/controller path writes yaw into
  the skeleton root bone, that root-bone yaw is still present in the copied pose.
- Root cause: the skeletal proxy anchor did not include the root bone world
  transform, so root-bone global yaw was treated as local pose rotation.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h`
  - `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Toolkits/ComposableCameraTypeAssetEditorToolkit.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimePreviewerTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: added `MakeSkeletalSubjectWorldTransform`, which anchors skeletal
  previews to `ComponentSpaceTransforms[0] * ComponentWorldTransform`. The
  proxy component is then placed relative to that root-bone anchor, cancelling
  copied root-bone yaw while preserving child-bone pose.
- Regression-test name: superseded by
  `ComposableCameraSystem.RuntimePreviewer.SkeletalRootTransformIsPreservedInProxy`,
  which keeps the visual root transform synchronized while camera marker math
  stays decoupled from subject rotation.
- Test blocker: automation test added, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must run it from IDE /
  Unreal Editor.
- Avoid next time: when copying skeletal component-space transforms into a
  proxy, decide whether the root bone is subject transform or pose detail. Do
  not anchor at the component when the copied root bone can carry global yaw.
- Possible conflicts: skeletal preview uses the root-bone world transform as
  the origin anchor, but no longer strips real visual-root rotation from the
  drawn proxy. Local child-bone pose still copies from the live pawn.

## 2026-06-03 - Runtime Previewer fakes camera rotation during character strafe

- Symptom: when pressing A in PIE so the character moves left in camera space,
  Runtime Previewer shows the camera rotating even though the PIE camera itself
  has not rotated.
- Trigger / repro: open Runtime Previewer during PIE, bind a runtime camera,
  hold A to move the controlled character left while keeping the camera
  rotation unchanged.
- Why it happens: camera marker math used the same
  `SourceWorldTransform.GetRelativeTransform(SubjectWorldTransform)` path as
  pawn proxy math. If the visual subject/root/pose rotates while strafing, that
  inverse subject rotation is applied to the runtime camera marker too.
- Root cause: Runtime Previewer coupled camera marker rotation to subject
  rotation instead of using the camera's final runtime rotation as the sole
  source of camera orientation.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h`
  - `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimePreviewerTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: added `MakeCameraPreviewTransform`, which subtracts subject translation
  but preserves the runtime camera rotation. Pawn proxy pose math still uses
  the visible-subject-relative transform path.
- Regression-test name:
  `ComposableCameraSystem.RuntimePreviewer.CameraPreviewIgnoresSubjectRotation`.
- Test blocker: automation test added, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must run it from IDE /
  Unreal Editor.
- Avoid next time: do not share one relative-transform helper between visual
  subject normalization and camera marker orientation. Camera rotation in the
  preview must come only from the runtime camera pose.
- Possible conflicts: camera relative location is subject-origin-relative with
  world/camera rotation preserved. Pawn proxy transforms were later changed to
  the same translation-only rule so real character rotation remains visible.

## 2026-06-03 - Runtime Previewer drops protagonist transform sync

- Symptom: after decoupling camera marker rotation from subject rotation,
  Runtime Previewer no longer synchronizes the protagonist's live transform
  rotation.
- Trigger / repro: open Runtime Previewer during PIE, bind a runtime camera, and
  rotate or strafe the controlled character so the protagonist transform changes
  while the camera does not rotate.
- Why it happens: pawn proxy transforms still used
  `SourceWorldTransform.GetRelativeTransform(SubjectWorldTransform)`. That path
  removes subject rotation. It is useful for fully local math, but wrong for
  drawing a live character proxy whose transform rotation should remain visible.
- Root cause: Runtime Previewer used one "subject-relative" transform rule for
  two different jobs: removing world translation and normalizing orientation.
  Camera/character decoupling needs translation removal only.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.h`
  - `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraRuntimePreviewerViewportClient.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimePreviewerTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: added `MakeTranslationRelativeTransform` and use it for pawn proxy
  spawn/sync. The helper subtracts subject translation but preserves the source
  world rotation and scale. Camera marker math now uses the same
  translation-only rule through `MakeCameraPreviewTransform`.
- Regression-test names:
  `ComposableCameraSystem.RuntimePreviewer.TranslationRelativeTransformPreservesRotation`
  and
  `ComposableCameraSystem.RuntimePreviewer.SkeletalRootTransformIsPreservedInProxy`.
- Test blocker: automation tests added, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must run them from IDE /
  Unreal Editor.
- Avoid next time: name helper functions by the transform components they
  remove. "Relative" was too broad and hid that rotation was also being
  stripped.
- Possible conflicts: character transform rotation is now visible in the
  preview. Camera marker rotation remains driven only by the runtime camera
  pose, so the earlier fake camera rotation should remain fixed.

## 2026-06-12 - Spline node stalls with SimpleSpring move interpolator

- Symptom: a `UComposableCameraSplineNode` using `MoveInterpolator =
  UComposableCameraSimpleSpringInterpolator` does not visibly advance along the
  spline.
- Trigger / repro: configure a BuiltInSpline node with `MoveMethod` Automatic or
  ClosestPoint, assign SimpleSpring as `MoveInterpolator`, then tick while the
  desired normalized spline position changes.
- Why it happens: Spline stores `Rail->CurrentPositionOnRail` from the
  interpolator return value each frame. That path expects `Run()` to return the
  absolute smoothed position.
- Root cause: `TSimpleSpringInterpolatorTraits<double>::Damp` returned only the
  damped delta (`Target - Current` progress), while the vector, rotator, and
  quat SimpleSpring paths returned absolute values. Any double interpolator
  reset from a non-zero smoothed value could collapse back toward a small delta.
- Touched files:
  - `Source/ComposableCameraSystem/Public/Interpolator/ComposableCameraSimpleSpringInterpolator.h`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraBugFixTests.cpp`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: make the double SimpleSpring trait return `CurrentValue + DampedDelta`,
  matching the `TCameraInterpolator` contract. Vector SimpleSpring traits now
  treat the scalar helper result as an absolute component value so they do not
  double-add `CurrentValue`.
- Regression-test names:
  `System.Engine.ComposableCameraSystem.Interpolator.SimpleSpring.DoublePerFrameResetProgressesTowardTarget`
  and
  `System.Engine.ComposableCameraSystem.Interpolator.SimpleSpring.VectorPerFrameResetDoesNotDoubleAddCurrent`.
- Test blocker: automation test added, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must run it from IDE /
  Unreal Editor.
- Avoid next time: when adding interpolator value-type specializations, test the
  per-frame `Reset(LastOutput, Target) -> Run(DeltaTime)` pattern and assert
  that `Run()` returns an absolute value.
- Possible conflicts: double SimpleSpring users that reset from `0` to a delta
  keep the same result. Users that reset from a non-zero smoothed state, such as
  Spline, FocusPull, and VolumeConstraint, now continue toward the target
  instead of treating the damped delta as the final value. Vector SimpleSpring
  behavior is intended to remain unchanged.
