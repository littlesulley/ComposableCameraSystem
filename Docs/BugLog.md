# Bug Log

## 2026-09-28 - Custom Modifier FOV addition lost to first pin refresh

- Symptom: `CM_Level_1` appeared ACTIVE but `CM_FieldOfViewAddition` had no
  visible effect on the FOV of camera type `CCCC`.
- Trigger / repro: expose the `FieldOfView` pin on `CCCC`, supply 120 through
  the activation K2 node, register `CM_Level_1` with its Custom Modifier Class
  adding 20, then evaluate the camera in PIE. Expected 140; observed 120.
- Why it happens: node initialization resolves the K2 parameter to 120, then
  the Custom callback changes the node property once. The first `TickNode`
  resolves the same parameter again before the FOV node writes its pose.
- Root cause: Custom callbacks did not register ownership of the pin-backed
  properties they changed; generic Node Type modifiers already did.
- Touched files: `Source/ComposableCameraSystem/Public/Nodes/ComposableCameraCameraNodeBase.h`,
  `Source/ComposableCameraSystem/Private/Nodes/ComposableCameraCameraNodeBase.cpp`,
  `Source/ComposableCameraSystem/Private/Modifiers/ComposableCameraModifierBase.cpp`,
  `Source/ComposableCameraSystem/Private/Tests/ComposableCameraModifierPropertyOverrideTests.cpp`,
  `Docs/DesignDoc.md`, `Docs/TechDoc.md`, `Docs/ExecutionFlowExamples.md`,
  `Docs/BugLog.md`.
- Fix: after the one-shot callback, compare pin-backed properties with their
  pre-callback values and register only changed non-wired pins as Modifier-owned.
  Untouched and wired pins continue to refresh normally. Wired sources are
  excluded because BeginPlay compute output may not exist at callback time.
- Regression-test name:
  `System.Engine.ComposableCameraSystem.Modifiers.Custom.PreservesChangedFov`.
  Compile and run it in Rider or Visual Studio / the editor; shell execution is
  prohibited by project instructions.
- How to avoid: test custom callback writes against both activation parameter
  slots and node defaults, including the first node tick, untouched pins, and
  wired inputs whose sources run after the callback.
- Possible conflicts: a Custom callback that intentionally changes a pin-backed
  property once and then expects later graph/K2 updates to overwrite it now
  retains its one-shot result until camera reactivation. Wired pins and non-pin
  effects remain unchanged.

## 2026-09-27 - Action duration regression test changed CDO after class initialization

- Symptom: `NonPositiveDurationRejected` failed its zero-duration,
  negative-duration, and empty-PCM assertions while other Action tests passed.
- Trigger / repro: run `System.Engine.ComposableCameraSystem.Actions` in the
  editor after compiling the Action asset changes. The test temporarily writes
  `Duration` on `UComposableCameraActionBase`'s CDO before calling
  `AddCameraAction` for that same class.
- Why it happens: the temporary CDO mutation did not yield the intended
  Duration on newly constructed Action instances; registration correctly
  checked each instance's actual value, which remained positive.
- Root cause: the regression fixture modeled authored class defaults by
  mutating an already initialized CDO instead of declaring test classes with
  those defaults.
- Touched files: `Source/ComposableCameraSystem/Private/Tests/ComposableCameraTestObjects.h`,
  `Source/ComposableCameraSystem/Private/Tests/ComposableCameraActionTests.cpp`,
  `Docs/TechDoc.md`, `Docs/BugLog.md`.
- Fix: use dedicated test Action classes with zero, negative, and
  Condition-only zero Duration defaults. Keep the runtime registration guard.
- Regression-test name: `System.Engine.ComposableCameraSystem.Actions.NonPositiveDurationRejected`.
  Compile in Rider or Visual Studio, restart the editor, then rerun it.
- Avoid next time: test class-based creation with real fixture class defaults;
  do not mutate a CDO at runtime to simulate authored defaults.
- Possible conflicts: test-only reflected classes add UHT types but do not
  change Action runtime API or other plugin modules.

## 2026-09-27 - Action instances could not receive caller context

- Symptom: one Action Blueprint needed many subclasses for different defaults;
  a call site could not pass a live Actor or other context value into its logic.
- Trigger / repro: create a Blueprint Action that calculates direction toward a
  target Actor; add it from Blueprint. The class-only AddAction node offers no
  input for the target, so the Action cannot receive the call site's Actor.
- Why it happens: PCM created an Action directly from its class defaults and
  registered it before any caller values could be applied.
- Root cause: Action authoring had no reusable asset template or typed parameter
  transport at activation.
- Touched files: `Source/ComposableCameraSystem/Public/DataAssets/ComposableCameraActionTypeAsset.h`,
  `Source/ComposableCameraSystem/Private/DataAssets/ComposableCameraActionTypeAsset.cpp`,
  `Source/ComposableCameraSystem/Public/Core/ComposableCameraPlayerCameraManager.h`,
  `Source/ComposableCameraSystem/Private/Core/ComposableCameraPlayerCameraManager.cpp`,
  `Source/ComposableCameraSystem/Public/Utils/ComposableCameraBlueprintLibrary.h`,
  `Source/ComposableCameraSystem/Private/Utils/ComposableCameraBlueprintLibrary.cpp`,
  `Source/ComposableCameraSystemUncookedOnly/Public/K2Node_AddCameraAction.h`,
  `Source/ComposableCameraSystemUncookedOnly/Private/K2Node_AddCameraAction.cpp`,
  `Source/ComposableCameraSystemEditor/{Public,Private}/Factories/ComposableCameraActionTypeAssetFactory.*`,
  `Source/ComposableCameraSystemEditor/{Public,Private}/AssetTools/AssetDefinition_ComposableCameraActionTypeAsset.*`,
  `Source/ComposableCameraSystem/Private/Tests/ComposableCameraActionTests.cpp`,
  `Source/ComposableCameraSystem/Private/Tests/ComposableCameraTestObjects.h`,
  `Docs/DesignDoc.md`, `Docs/EditorDesignDoc.md`, `Docs/TechDoc.md`,
  `Docs/ExecutionFlowExamples.md`, `Docs/BugLog.md`.
- Fix: add an Action Type Asset with an instanced Action template; duplicate it
  per activation and apply only connected typed K2 inputs before PCM registration.
  Return the instance as a handle for exact-instance Blueprint removal.
- Regression-test name: `System.Engine.ComposableCameraSystem.Actions.AssetParameters`.
  IDE manual check: create an Action Type Asset with a Blueprint Action template,
  connect an Actor pin on Add Camera Action, compile the calling Blueprint, and
  verify `OnExecute` reads that Actor. Run after a full editor restart.
- Avoid next time: separate reusable behavior from per-call inputs at the
  activation boundary; keep template state immutable during execution.
- Possible conflicts: existing class-based AddAction remains; asset-backed
  Actions are separate instances and may share one Blueprint class. The K2
  node exposes only editable Blueprint-visible subclass fields; unconnected
  inputs retain template defaults.

## 2026-09-27 - Action Condition used previous blended PCM pose

- Symptom: MoveTo, RotateTo, or ResetPitch could expire while the executing
  camera had not reached its target. During a blend, the rendered pose could
  reach the target through cancellation between source and target cameras;
  the view then moved away again after the Action was removed.
- Trigger / repro: blend a source camera at X=-50 with a target camera whose
  MoveTo Action leaves it at X=+50. At blend weight 0.5, the PCM output is X=0.
  Set MoveTo target X=0; on the next update it expired although its camera was
  still at X=+50.
- Why it happens: PCM called `OnCanExecute` before evaluation using the prior
  frame's final blended `CurrentCameraPose`, while `OnExecute` mutated the
  camera-local pose inside its own tick.
- Root cause: Condition and execution observed different pose domains.
- Touched files: `Source/ComposableCameraSystem/Public/Actions/ComposableCameraActionBase.h`,
  `Source/ComposableCameraSystem/Private/Actions/ComposableCameraActionBase.cpp`,
  `Source/ComposableCameraSystem/Public/Cameras/ComposableCameraCameraBase.h`,
  `Source/ComposableCameraSystem/Private/Cameras/ComposableCameraCameraBase.cpp`,
  `Source/ComposableCameraSystem/Private/Core/ComposableCameraPlayerCameraManager.cpp`,
  `Source/ComposableCameraSystem/Private/Tests/ComposableCameraActionTests.cpp`,
  `Docs/DesignDoc.md`, `Docs/TechDoc.md`, `Docs/ExecutionFlowExamples.md`,
  `Docs/BugLog.md`.
- Fix: keep time/manual expiration in the PCM; run pose-dependent Condition at
  the Action's camera-local execution hook. The running camera owns the global
  completion decision when source and target cameras both execute a persistent
  Action; a current-camera-only Action uses its bound camera.
- Regression-test name: `System.Engine.ComposableCameraSystem.Actions.ConditionUsesLocalPose`.
- Avoid next time: evaluate completion in the same pose domain and pipeline
  stage as the behavior being completed.
- Possible conflicts: Blueprint Condition callbacks now run at a matching
  camera hook rather than before evaluation; Actions without a matching hook
  no longer expire through Condition alone. Current-camera-only Actions use
  their original camera during a blend. Time and manual channels retain their
  PCM update timing.

## 2026-09-27 - Non-positive Action duration still executed once

- Symptom: an Action configured with Duration expiration and Duration <= 0 was
  accepted and could execute on its first PCM update, despite the authored
  property contract saying it would not be added.
- Trigger / repro: set a Blueprint Action class default Duration to zero or a
  negative value with the Duration bit enabled; call AddAction; update the PCM.
- Why it happens: AddCameraAction did not validate Duration, and OnCanExecute
  returned the previous `bCanExecuteDuration` value before marking it false.
- Root cause: registration and execution lacked a non-positive duration guard.
- Touched files: `Source/ComposableCameraSystem/Private/Core/ComposableCameraPlayerCameraManager.cpp`,
  `Source/ComposableCameraSystem/Private/Actions/ComposableCameraActionBase.cpp`,
  `Source/ComposableCameraSystem/Private/Tests/ComposableCameraActionTests.cpp`,
  `Docs/DesignDoc.md`, `Docs/TechDoc.md`, `Docs/BugLog.md`.
- Fix: reject invalid Duration-enabled actions at registration and reject a
  later invalid runtime duration before execution.
- Regression-test name: `System.Engine.ComposableCameraSystem.Actions.NonPositiveDurationRejected`.
- Avoid next time: validate authored lifetime constraints both at registration
  and at the point of execution when C++ can change them later.
- Possible conflicts: only Duration-enabled actions with non-positive values
  change behavior; Instant, Manual, and Condition-only actions keep their
  existing frame-based lifetime semantics.

## 2026-09-27 - Viewport Debug Panel content crossed screen height

- Symptom: the panel border stopped at viewport bottom, but a tall region
  could keep drawing its own border or rows below it. Later regions disappeared.
- Trigger / repro: enable `CCS.Debug.Panel 1` in a short PIE viewport with
  enough Running Camera nodes, Modifier candidates, or warnings to exceed
  available height. Check pages at `CCS.Debug.Panel.Width 0.32` and `0.60`.
- Why it happens: the old layout clamped only outer `PanelH`. Region drawing
  still used unbounded estimated heights, and Legend rows ignored body height.
- Root cause: no viewport-height budget was applied to region placement.
- Touched files:
  - `Source/ComposableCameraSystem/Private/Debug/ComposableCameraDebugPanel.cpp`
  - `Source/ComposableCameraSystem/Public/Debug/ComposableCameraDebugPanel.h`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: compact shared row spacing, pack whole regions into height-limited
  pages, cap an oversized region to remaining height, and bound Legend rows.
  `CCS.Debug.Panel.Page` selects a zero-based page; footer reports clipping.
- Regression-test name: `ComposableCameraSystem.DebugPanel.ViewportHeightPaging.PIEVisual`.
- Test blocker: current automation has no stable PIE viewport Canvas capture or
  pixel-boundary assertion for this overlay. Project rules also prohibit shell
  editor runs. Compile in Rider or Visual Studio; in PIE, resize viewport to
  720p and shorter, fill Running Camera and Modifier regions, switch pages,
  and confirm no region border or text crosses the panel/screen bottom.
- Avoid next time: estimate region heights and enforce the same viewport
  budget in the draw pass; every structured renderer must honor body height.
- Possible conflicts: debug overlay only. Page selection changes what is
  visible on short viewports; camera evaluation and editor graph are unchanged.

## 2026-07-30 - Immediate in-place Modifier exit skipped lower-layer restoration

- Symptom: `ImmediateTransitions` failed both null-transition and
  zero-duration exit assertions. The node retained the active Modifier value
  instead of restoring its baseline/lower pin value on the next evaluation.
- Trigger / repro: activate a `ModifyExistingInstance` Float override, evaluate
  it once, reconcile to an empty effective Modifier set with either no
  transition or a transition whose Duration is zero, then tick the camera.
- Why it happens: `StartExitTransition` marked the property binding
  `bPendingRemoval`, expecting `ApplyForNode` to call `ReleaseProperty`.
  `ReconcileNode` then removed every pending binding immediately, before the
  node evaluation phase could restore the lower layer and unregister override
  ownership.
- Root cause: reconciliation cleanup treated a zero-time normal runtime exit
  like the synchronous camera-construction `bImmediate` path. Pending removal
  was interpreted as dead state instead of required release work.
- Touched files:
  - `Source/ComposableCameraSystem/Private/Modifiers/ComposableCameraModifierRuntimeState.cpp`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraModifierPropertyOverrideTests.cpp`
  - `Docs/DesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: prune pending bindings inside `ReconcileNode` only for the
  camera-construction `bImmediate` path, after it synchronously calls
  `ApplyProperty`. Normal null/zero-duration exits retain the binding until
  `ApplyForNode` calls `ReleaseProperty`, restores the live lower layer,
  unregisters ownership, and then removes the completed binding.
- Regression-test name:
  `System.Engine.ComposableCameraSystem.Modifiers.InPlace.TransitionMatrix.ImmediateTransitions`.
- Test blocker: project rules prohibit Codex from launching Unreal automation
  from the shell. Recompile in Rider or Visual Studio, then rerun the test in
  Unreal Editor.
- Avoid next time: distinguish pending cleanup from completed cleanup. Any
  state whose removal owns restoration/unregistration work must survive until
  that work has run.
- Possible conflicts: initial camera construction still applies and removes
  immediate bindings synchronously. Timed exits, interrupted transitions,
  live K2/wire lower values, Evaluation Tree structure, and camera lifecycle
  are unchanged.

## 2026-07-28 - Structured Modifier panel overlaps proportional-font columns

- Symptom: the runtime Debug Panel's Modifier section overlapped `Camera Tags`
  with its value, crowded Scope against Blend, clipped long transition class
  names, repeated a redundant Fields row, and mixed title-case labels with
  lowercase count/status text.
- Trigger / repro: enable `CCS.Debug.Panel 1` in PIE with several registered
  Modifiers, including Reactivate and In-Place candidates, then inspect the
  Modifier section at normal panel widths.
- Why it happens: `Camera Tags` used a fixed 74-pixel label width, while Scope
  and Blend were forced into a single row split at 61 percent. Unreal's
  proportional debug font exceeded those assumptions. Reactivate blend text
  also printed the full `Composable Camera ... Transition` class name.
- Root cause: the first structured layout treated variable-width Canvas text
  like fixed-width columns and spent horizontal space on information already
  represented by the target-Node group.
- Touched files:
  - `Source/ComposableCameraSystem/Private/Debug/ComposableCameraDebugPanel.cpp`
  - `Source/ComposableCameraSystem/Public/Debug/ComposableCameraDebugPanel.h`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: measure label widths in pixels and add an explicit gap; fold compact
  Scope into the Modifier identity row and give Blend a full-width row; remove
  Fields storage/rendering; shorten common transition class names to their
  meaningful type; apply one shared right-side safety inset to counts,
  Priority/Mode, and Blend; normalize visible headings, counts, placeholders,
  and status text to title case.
- Regression-test name:
  `ComposableCameraSystem.DebugPanel.ModifierStructuredLayout.PIEVisual`.
- Test blocker: proportional-font clipping and overlap depend on live
  `UCanvas`, font metrics, viewport DPI, and panel width. No existing
  automation harness renders the DebugDrawService panel to a deterministic
  image. Verify in PIE after IDE compilation at
  `CCS.Debug.Panel.Width 0.32`, `0.40`, and `0.60`.
- Avoid next time: Canvas key/value layouts must measure actual font width and
  reserve a visible gap. Do not split two unbounded user-authored values across
  one row unless both columns have compact bounded representations.
- Possible conflicts: none. This changes only panel snapshot strings and
  drawing layout while the panel is enabled; Modifier selection, transitions,
  camera evaluation, and lifecycle are untouched.

## 2026-07-27 - In-place struct interpolation used invalid LWC reflection access

- Symptom: Editor compilation failed with C2039/C2672 because
  `StaticStruct` was not a member of `FVector2D`, `FVector`, `FVector4`,
  `FRotator`, `FTransform`, or `FLinearColor`.
- Trigger / repro: compile the in-place Modifier struct interpolation helpers
  for the UE5.6 Editor target.
- Why it happens: the generic read/write helpers called `T::StaticStruct()`.
  UE5.6 LWC math names are aliases of `UE::Math` templates and do not expose
  reflected type access as a member function.
- Root cause: reflected built-in structs and user-defined USTRUCTs were treated
  as though they shared one member-access convention.
- Touched files:
  - `Source/ComposableCameraSystem/Public/Nodes/ComposableCameraCameraNodeBase.h`
  - `Source/ComposableCameraSystem/Private/Modifiers/ComposableCameraModifierRuntimeState.cpp`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: both generic struct read and write helpers now compare against
  `TBaseStructure<T>::Get()`, matching the existing continuous-type classifier
  and UE5.6's built-in-struct reflection API. Node-local explicit pin readers
  use the same accessor for LWC types, while LinearColor lower-layer reads
  reuse the generic Struct-slot copy path.
- Regression-test name:
  `ComposableCameraSystem UE5.6 non-unity compile: in-place LWC struct reflection`
  plus
  `System.Engine.ComposableCameraSystem.Modifiers.InPlace.TransitionsLivePinValues`.
- Test blocker: the failure occurs before automation modules load. Project
  rules prohibit command-line UBT, so verification requires another full
  Editor-target build in Rider or Visual Studio.
- Avoid next time: use `TBaseStructure<T>::Get()` in generic code instantiated
  with UE built-in math/color structs. Reserve `T::StaticStruct()` assumptions
  for constrained user-defined USTRUCT types.
- Possible conflicts: none. Runtime type checks and copied values are unchanged;
  only the valid UE5.6 reflection accessor changed.

## 2026-07-27 - Modifier blendability classifier dereferenced null properties

- Symptom: a caller passing an unresolved/null reflected property to
  `IsNodePropertyContinuouslyBlendable` could crash.
- Trigger / repro: call
  `UComposableCameraModifierBase::IsNodePropertyContinuouslyBlendable(nullptr)`.
- Why it happens: the classifier called `Property->IsA` before validating its
  input.
- Root cause: the new public helper omitted the null contract already used by
  the neighboring `IsNodePropertyOverridable` helper.
- Touched files:
  - `Source/ComposableCameraSystem/Private/Modifiers/ComposableCameraModifierBase.cpp`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraModifierPropertyOverrideTests.cpp`
  - `Docs/BugLog.md`
- Fix: return false for null before inspecting numeric or struct types.
- Regression-test name:
  `System.Engine.ComposableCameraSystem.Modifiers.InPlace.ApplyModeCompatibility`
  (`Null properties are not continuously blendable` assertion).
- Test blocker: automation was updated, but project rules prohibit launching
  Unreal Editor tests from shell. Run it after the IDE build succeeds.
- Avoid next time: reflection classifier helpers must treat null as unsupported,
  matching Unreal's `CastField` behavior and neighboring eligibility APIs.
- Possible conflicts: none. Valid property classification is unchanged.

## 2026-07-27 - In-place Modifier reflection helpers failed UE5.6 compilation

- Symptom: Editor compilation failed with C2039 for
  `FNumericProperty::IsUnsigned` and C2672/C2737 for every struct call to
  `ResolveStructEndpoint`.
- Trigger / repro: compile the new in-place Modifier implementation for the
  UE5.6 Editor target.
- Why it happens: the integer reader used a numeric-property query that is not
  part of UE5.6's `FNumericProperty` API. The struct endpoint helper also
  accepted only `UComposableCameraCameraNodeBase*`, while its valid snapshot
  and template inputs were stored as the wider `UObject*` type.
- Root cause: both reflection helpers were written against narrower, assumed
  APIs instead of the types already used by this plugin's UE5.6 reflection
  paths.
- Touched files:
  - `Source/ComposableCameraSystem/Public/Nodes/ComposableCameraCameraNodeBase.h`
  - `Source/ComposableCameraSystem/Private/Modifiers/ComposableCameraModifierRuntimeState.cpp`
  - `Docs/BugLog.md`
- Fix: read integer-backed enum values through
  `GetSignedIntPropertyValue`, matching the existing parameter extraction
  implementation. Widen the read-only struct endpoint container to
  `const UObject*`; no cast or runtime behavior change is required.
- Regression-test name:
  `ComposableCameraSystem UE5.6 non-unity compile: in-place Modifier reflection helpers`.
- Test blocker: both failures occur before automation modules load. Project
  rules prohibit command-line UBT, so verification requires another full
  Editor-target build in Rider or Visual Studio.
- Avoid next time: reuse reflection APIs already compiled in the target engine
  version. Helper container parameters should match the widest valid owner type
  consumed by `FProperty::ContainerPtrToValuePtr`.
- Possible conflicts: none expected. Integer conversion keeps the existing
  canonical signed `int64` pin representation. Struct reads still operate on
  the same node snapshot/template UObjects.

## 2026-07-22 - Nested Mesh Layer replaced instead of suspending outer Layer

- Symptom: entering an inner painted Layer removed the outer Layer's Camera
  Type and every Modifier even while the player was still inside the outer
  shape. Exiting the inner Layer could not resume the original outer camera.
- Trigger / repro: paint overlapping red and yellow Layers, give both Profiles
  camera effects, walk red -> overlap -> red -> outside.
- Why it happens: the ray query returned one winning Layer and the subsystem
  stored one Profile, one Modifier array, and one Mesh Context per player.
  A same-surface Priority winner therefore replaced the complete previous
  scope.
- Root cause: Layer was modeled as an exclusive selector instead of an
  irregular Trigger scope with independent enter/exit lifetime.
- Touched files:
  - `Source/ComposableCameraSystem/Public/MeshCamera/ComposableCameraMeshSurfaceTypes.h`
  - `Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshSurfaceTypes.cpp`
  - `Source/ComposableCameraSystem/Public/MeshCamera/ComposableCameraMeshSurfaceStorageActor.h`
  - `Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshSurfaceStorageActor.cpp`
  - `Source/ComposableCameraSystem/Public/MeshCamera/ComposableCameraMeshWorldSubsystem.h`
  - `Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshWorldSubsystem.cpp`
  - `Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshProfileState.h`
  - `Source/ComposableCameraSystem/Public/Core/ComposableCameraPlayerCameraManager.h`
  - `Source/ComposableCameraSystem/Private/Core/ComposableCameraPlayerCameraManager.cpp`
  - `Source/ComposableCameraSystem/Public/Core/ComposableCameraContextStack.h`
  - `Source/ComposableCameraSystem/Private/Core/ComposableCameraContextStack.cpp`
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerRendering.cpp`
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerModeToolkit.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Customizations/ComposableCameraMeshProfileCustomization.cpp`
  - Mesh runtime/editor automation tests and design documents.
- Fix: remove numeric Layer Priority. Query every enabled Layer on the nearest
  surface and reconcile an active set keyed by storage actor plus Layer GUID.
  Every active Layer owns its Modifier instances. Every Camera-bearing Layer
  pushes a unique temporary Context whose readable hint contains Layer name and
  GUID. Inner exit pops only its Context, restoring the original outer camera;
  final outer exit restores gameplay. A Camera-less Layer leaves the lower
  camera active. Active pop also refreshes ModifierManager selection without
  rebuilding the resumed camera, preventing removed duplicated assets from
  remaining strongly referenced. Editor overlap color now follows
  top-to-bottom list order.
- Regression-test names:
  - `System.Engine.ComposableCameraSystem.MeshCamera.SurfaceLayerSet`
  - `ComposableCameraSystem.MeshCamera.LayerCameraContextOwnershipPolicy`
  - `ComposableCameraSystem.MeshCamera.LayerExitModifierRefreshPolicy`
  - `System.Engine.ComposableCameraSystem.ContextStack.NestedTemporaryContextsRestoreInOrder`
  - `ComposableCameraSystem.Editor.MeshCamera.ResolvedVisualization`
  - `ComposableCameraSystem.Editor.MeshCamera.ProfileDetailsLayout`
- Test coverage: pure tests cover same-surface active-set collection, nearest
  floor isolation, per-Layer Context ownership, Modifier refresh policy, list
  order rendering, and hidden manual Context Name. Full red -> yellow -> red ->
  gameplay camera-instance restoration requires an IDE-built PIE smoke test.
- Avoid next time: keep spatial membership separate from effect conflict
  resolution. Never collapse overlapping Trigger-like scopes before their
  independent exit events have been derived.
- Possible conflicts: saved Priority values are intentionally discarded after
  the reflected field removal. Mesh Profile Context Name remains in the
  shared row schema but is hidden and ignored. This entry supersedes the old
  Priority-winner behavior recorded by the 2026-07-20 overlay bug.

## 2026-07-22 - PIE exit released Scene with Mesh preview component still registered

- Symptom: after enabling `Show Mesh Camera Layers` during PIE, stopping PIE
  triggered an ensure/crash in `FScene::Release` from
  `UWorld::FinishDestroy` during garbage collection.
- Trigger / repro: start PIE, enable Show Mesh Camera Layers, then stop PIE
  while the read-only Layer overlay is visible.
- Why it happens: PIE preview meshes live in ownerless transient
  `ULineBatchComponent`s held by `TStrongObjectPtr`. Cleanup depended on the
  next core-ticker scan noticing that the PIE storage actor/world disappeared.
  `EndPlayMap` can proceed directly to world/scene teardown and GC without that
  later scan.
- Root cause: the preview component lifetime was coupled to polling after world
  disappearance instead of UE's pre-PIE-teardown lifecycle boundary. The
  strong reference kept a registered primitive alive while its `FScene` was
  being released.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Utilities/ComposableCameraMeshLayerTool.cpp`
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerRendering.h`
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerRendering.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerVisualizationTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/ExecutionFlowExamples.md`
  - `Docs/BugLog.md`
- Fix: stop creating editor-owned components. Preview now submits each storage
  mesh under a unique non-zero BatchID to the PIE world's own
  `WorldPersistent` LineBatcher, and cleanup calls `ClearBatch`. Also bind
  `PrePIEEnded` to clear all CCS batches and reject new work before
  `EndPlayMap`; `PostPIEStarted` re-enables routing for the next session.
- Regression-test name:
  `ComposableCameraSystem.Editor.MeshCamera.PIEPreviewWorldRouting`.
- Test coverage: automation verifies requested PIE rendering is rejected while
  teardown is active. Full `PrePIEEnded -> EndPlayMap -> FScene::Release`
  ordering requires an IDE-built PIE smoke test; project rules prohibit
  launching Unreal Editor automation from shell.
- Avoid next time: prefer world-owned rendering services for PIE overlays. Any
  editor-owned `UPrimitiveComponent` registered with a PIE world must still be
  destroyed on `PrePIEEnded`, never after weak world keys expire or during GC.
- Possible conflicts: Show remains logically enabled across PIE sessions. Its
  Level Editor preview stays active; only PIE batches are removed at
  pre-end and rebuilt after the next `PostPIEStarted`.

## 2026-07-21 - Mesh Profile exit could not restore gameplay camera

- Symptom: after the player left every Mesh Layer, the Mesh Profile camera
  remained active instead of returning to the normal gameplay camera.
- Trigger / repro: start with a gameplay camera, enter a Layer whose Profile
  has a Camera Type and uses `ContextName=None` or a Context already on the
  stack, then leave all Layers.
- Why it happens: those activations replaced the camera inside an externally
  owned Context. Mesh recorded no `OwnedCameraContextName`, so exit removed
  Modifiers and called `OnModifierChanged()` on the still-running Mesh camera.
  The previous same-Context camera/tree may already have been destroyed.
- Root cause: reversible Layer lifetime was implemented with destructive
  same-Context activation; Context ownership depended on whether the authored
  name happened to be absent before entry.
- Touched files:
  - `Source/ComposableCameraSystem/Public/Core/ComposableCameraContextStack.h`
  - `Source/ComposableCameraSystem/Private/Core/ComposableCameraContextStack.cpp`
  - `Source/ComposableCameraSystem/Public/Core/ComposableCameraPlayerCameraManager.h`
  - `Source/ComposableCameraSystem/Private/Core/ComposableCameraPlayerCameraManager.cpp`
  - `Source/ComposableCameraSystem/Public/MeshCamera/ComposableCameraMeshWorldSubsystem.h`
  - `Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshWorldSubsystem.cpp`
  - `Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshProfileState.h`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraContextStackTests.cpp`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraMeshProfileTests.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Customizations/ComposableCameraMeshProfileCustomization.cpp`
  - `Docs/DesignDoc.md`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/ExecutionFlowExamples.md`
  - `Docs/BugLog.md`
- Fix: the first Camera-bearing Profile now pushes a unique caller-owned
  temporary Context above the current gameplay Context. Nested Camera Profiles
  reuse it. Final Camera-Layer exit pops it and resumes the untouched lower
  camera tree. The PCM transaction captures the gameplay Director before the
  push, preserving the entry reference-source transition. Mesh forces the
  scoped camera non-transient, and failed camera construction rolls back the
  empty Context immediately.
- Regression-test names:
  `System.Engine.ComposableCameraSystem.ContextStack.TemporaryContextRestoresPrevious`
  and
  `ComposableCameraSystem.MeshCamera.ProfileCameraContextOwnershipPolicy`.
- Test coverage: automation verifies unique temporary push/pop preserves the
  original Director and verifies first-entry/reuse/exit ownership policy. Full
  camera-actor pointer restoration still needs an IDE-built PIE smoke test:
  Gameplay A -> Mesh B -> no Layer must return to A with original node state.
- Avoid next time: any scoped feature that must restore prior camera state must
  own a separate Context. Never treat a cached old camera pointer or an
  external Context as a reversible override.
- Possible conflicts: while Mesh Camera is active,
  `GetActiveContextName()` reports its generated internal temporary name.
  Mesh treats the authored Context Name as a logical/debug hint and ignores
  transient lifetime fields because Layer presence is authoritative. Normal
  DataTable activation keeps existing Context/ActivationParams semantics.

## 2026-07-21 - Show Mesh Camera Layers did not render in PIE

- Symptom: `Show Mesh Camera Layers` displayed filled Layer colors in the
  Level Editor viewport but not in the PIE game viewport.
- Trigger / repro: enable Show before or during PIE and inspect the same painted
  floor through the PIE viewport.
- Why it happens: the preview existed only in
  `FComposableCameraMeshLayerPreviewEdMode::Render()`. Editor-mode rendering
  receives the editor world and is not called by the PIE GameViewport.
- Root cause: preview visibility was modeled as one editor-mode render callback
  instead of one logical Show state with per-world rendering backends.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerRendering.h`
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerRendering.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Utilities/ComposableCameraMeshLayerTool.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerVisualizationTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/ExecutionFlowExamples.md`
  - `Docs/BugLog.md`
- Fix: Show now owns an independent preview-request flag. For each PIE storage
  actor, the editor module converts the same resolved non-stacking cells into a
  unique batch on that PIE world's persistent LineBatcher. Meshes submit once;
  a ticker handles already-running PIE, multiple PIE worlds, transform changes,
  streaming removal, and deterministic pre-PIE-end cleanup.
- Regression-test names:
  `ComposableCameraSystem.Editor.MeshCamera.PIEPreviewWorldRouting` and
  `ComposableCameraSystem.Editor.MeshCamera.ResolvedVisualization`.
- Test coverage: automation verifies routing accepts only requested PIE worlds
  and verifies PIE mesh generation emits one quad per already-resolved winning
  cell. Actual GameViewport rendering cannot be asserted by headless
  automation. After IDE compilation: toggle Show before/during PIE, test SIE,
  stop/restart PIE, and confirm overlays appear/disappear without stale color.
- Avoid next time: viewport tools spanning Editor and PIE need explicit world
  routing. Reuse resolved visualization data, but give each viewport family a
  renderer that actually participates in its scene.
- Possible conflicts: PIE geometry uses the world LineBatcher because attaching
  a render component to the hidden storage actor would suppress it. BatchIDs
  isolate CCS cleanup from unrelated debug drawing. The implementation lives in
  the Editor module and is absent from Shipping builds.

## 2026-07-21 - Editor exit accessed destroyed Level Editor mode tools

- Symptom: closing Unreal Editor emitted an ensure from
  `GLevelEditorModeTools()` at `UnrealEdGlobals.cpp:119`.
- Trigger / repro: load the CCS editor module, then exit Unreal Editor. Module
  shutdown called `FComposableCameraMeshLayerTool::Unregister()` after UE had
  destroyed its global Level Editor mode manager.
- Why it happens: `Unregister()` queried the active mesh edit/preview modes
  unconditionally. `GLevelEditorModeTools()` treats a missing singleton as an
  early-startup access, emits an ensure, and creates a replacement manager even
  when the real cause is late shutdown.
- Root cause: mesh mode cleanup assumed module shutdown always happened while
  the global Level Editor mode manager was alive.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Utilities/ComposableCameraMeshLayerTool.cpp`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: active-state callbacks return false after engine exit is requested, and
  edit/preview toggle commands become no-ops. `Unregister()` therefore skips
  mode-manager access during engine teardown while retaining normal hot-unload
  cleanup before exit.
- Regression-test name:
  `ComposableCameraSystem Editor Mesh Layer shutdown lifecycle smoke test`.
- Test blocker: automation cannot safely request full engine exit, wait until
  UE destroys the global mode-manager singleton, then continue inside the same
  test process. After IDE compilation, open the editor and exit; verify no
  `UnrealEdGlobals.cpp:119` ensure appears.
- Avoid next time: any editor shutdown path that touches
  `GLevelEditorModeTools()` must first reject `IsEngineExitRequested()`.
- Possible conflicts: none expected. Normal edit/preview toggling and module
  hot-unload before engine exit keep existing behavior.

## 2026-07-21 - Stray identifier broke PCM compilation

- Symptom: Editor compilation failed with C2065 for undeclared identifier
  `ibin`, followed by C2146 before `DesiredView`.
- Trigger / repro: compile `ComposableCameraPlayerCameraManager.cpp` with the
  stray token appended after the aspect-ratio constraint block.
- Why it happens: C++ parsed `ibin` as an identifier between the closing brace
  and the following statement, corrupting the function grammar.
- Root cause: an accidental text fragment remained after an earlier edit.
- Touched files:
  - `Source/ComposableCameraSystem/Private/Core/ComposableCameraPlayerCameraManager.cpp`
  - `Docs/BugLog.md`
- Fix: remove the stray `ibin` token. Camera pose, aspect-ratio, and
  post-process behavior remain unchanged.
- Regression-test name:
  `ComposableCameraSystem non-unity compile: PCM stray-token hygiene`.
- Test blocker: the failure occurs during C++ compilation before automation can
  load, and project rules prohibit command-line UBT. Verify through the next
  Rider or Visual Studio Editor-target build.
- Avoid next time: inspect the complete edited statement boundary and run the
  IDE compiler before treating a multi-file change as verified.
- Possible conflicts: none. This is syntax-only cleanup.

## 2026-07-20 - Mesh Profile Context was free text and Camera row was duplicated

- Symptom: Mesh Profile `Context Name` rendered as a free-text FName field,
  and the Camera category ended with an extra `Camera` struct field after all
  intended Camera properties.
- Trigger / repro: open any `ComposableCameraMeshProfile` asset in Details.
- Why it happens: the shared parameter row's ContextName property had no
  `GetOptions` metadata. The Profile class customization re-added the parent
  Camera property even though `ShowOnlyInnerProperties` already flattened it.
- Root cause: the embedded row depended on default property rendering while the
  class customization also attempted to own its placement.
- Touched files:
  - `Source/ComposableCameraSystem/Public/DataAssets/ComposableCameraParameterTableRow.h`
  - `Source/ComposableCameraSystemEditor/Private/Customizations/ComposableCameraMeshProfileCustomization.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshProfileCustomizationTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: bind ContextName to the configured project Context list through native
  `GetOptions`; hide the parent Camera row and explicitly add its five children
  in the desired order.
- Regression-test name:
  `ComposableCameraSystem.Editor.MeshCamera.ProfileDetailsLayout`.
- Test coverage: property-row generation verifies no visible Camera parent,
  all five Camera children, and dropdown values sourced from project Settings.
  Final spacing and labels still require an IDE-built Details-panel smoke test.
- Avoid next time: when a class customization flattens a struct, it must own the
  child rows explicitly and suppress the parent instead of mixing implicit and
  explicit placement.
- Possible conflicts: ContextName becomes constrained to configured Contexts in
  both DataTable rows and Mesh Profiles. Stored FName data and runtime
  activation behavior remain unchanged.

## 2026-07-20 - Expired Camera-only Mesh Profile could preserve owned Context

- Symptom: an Mesh Profile containing a Camera but no Modifier could leave
  its Profile-created Context on the stack after the Profile asset expired.
- Trigger / repro: enter a Layer whose Profile creates an explicit Context and
  has zero Modifier assets, unload the Profile source, allow GC, then tick the
  mesh subsystem outside that Layer.
- Why it happens: the unchanged-null fast path treated an empty Modifier
  instance array as proof that no Profile effects remained.
- Root cause: Profile residual state expanded from Modifier instances to
  Modifier instances plus owned Camera Context, but the weak-Profile cleanup
  predicate still modeled only the original Modifier-only layout.
- Touched files:
  - `Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshProfileState.h`
  - `Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshWorldSubsystem.cpp`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraMeshProfileTests.cpp`
  - `Docs/BugLog.md`
- Fix: the fast path now requires both an empty Modifier instance array and no
  owned Camera Context when the resolved Profile is null.
- Regression-test name:
  `ComposableCameraSystem.MeshCamera.ExpiredCameraOnlyProfileCleanupGuard`.
- Test coverage: pure automation covers live Profile, empty null state,
  expired Camera-only state, and expired Modifier state. A PIE smoke test must
  still verify the actual Context pop after streamed-Level unload and GC.
- Avoid next time: every weak-source equality fast path must audit all applied
  runtime effects, not only the effect type that existed when the state struct
  was first introduced.
- Possible conflicts: none. Stable live Profiles keep the same zero-work fast
  path; only null state with a residual owned Context now performs cleanup.

## 2026-07-20 - Mesh edit mode showed a blank active-mode icon

- Symptom: after activating `Mesh Camera Layers Mode`, the Level Editor
  displayed its name but left the leading mode-icon slot blank.
- Trigger / repro: open `Tools > Edit Mesh Camera Layers`, then inspect the
  active-mode selector in the Level Editor toolbar.
- Why it happens: both mesh editor modes and their ToolMenus entries were
  registered with the default-constructed, unset `FSlateIcon`.
- Root cause: mode registration added labels and priorities but never connected
  the feature to a registered editor-style brush.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/ComposableCameraEditorStyle.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Utilities/ComposableCameraMeshLayerTool.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerToolSettingsTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: register normal and small mesh-mode brushes using the existing CCS
  camera SVG, then supply the shared icon to edit/preview mode registration and
  both Tools menu actions.
- Regression-test name:
  `ComposableCameraSystem.Editor.MeshCamera.ModeIcon`.
- Test coverage: automation verifies the visible edit mode has a configured
  icon and that both normal and small brushes resolve from Slate style data.
  Final placement/scale still needs a Level Editor visual smoke test after the
  IDE build.
- Avoid next time: every visible `RegisterMode` call must receive a non-empty
  icon with both normal and small style resources.
- Possible conflicts: the mesh commands now use the existing CCS camera
  glyph. No mode activation, painting, preview, or runtime query behavior
  changes.

## 2026-07-20 - Visualization Layer resolver mixed int32 and enum returns

- Symptom: Editor compilation failed with C3487 in the authoring-visualization
  Layer resolver lambda.
- Trigger / repro: compile `ComposableCameraMeshLayerRendering.cpp` after
  adding GUID-to-Layer-index resolution.
- Why it happens: a lambda without an explicit return type must deduce one exact
  type from every return expression. Dereferencing the map returned `int32`,
  while `INDEX_NONE` retained its anonymous-enum type.
- Root cause: the resolver relied on implicit lambda return-type deduction for
  a sentinel whose declared type differs from the successful value type.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerRendering.cpp`
  - `Docs/BugLog.md`
  - `Docs/TechDoc.md`
- Fix: declare the resolver lambda return type as `int32` explicitly.
- Regression-test name:
  `ComposableCameraSystemEditor non-unity compile: visualization resolver return-type hygiene`.
- Test blocker: C3487 occurs during C++ compilation before automation modules
  load, and project rules prohibit command-line UBT. Verify through the next
  full Editor-target rebuild in Rider or Visual Studio.
- Avoid next time: explicitly declare lambda return types when branches mix a
  typed value with legacy enum sentinels such as `INDEX_NONE`.
- Possible conflicts: none. Resolver values and runtime behavior are unchanged.

## 2026-07-20 - Mesh brush left gaps at collision-component seams

- Symptom: visible floor regions inside the brush sometimes could not be
  painted, leaving long gaps aligned with floor or Landscape component seams.
- Trigger / repro: paint while the brush overlaps two collision components
  that form one continuous floor surface.
- Why it happens: every projected ring hit was required to belong to the exact
  component hit at the brush center. Valid samples on an adjacent component
  were discarded, and the fan skipped triangles beside invalid samples.
- Root cause: collision-component identity was incorrectly treated as floor
  surface identity.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerEdMode.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/ExecutionFlowExamples.md`
  - `Docs/BugLog.md`
- Fix: remove the component-identity gate. Projection distance, collision hit,
  and minimum floor normal remain required, allowing continuous floors made
  from multiple components.
- Regression-test name:
  `ComposableCameraSystem Editor Mesh Layer cross-component projection smoke test`.
- Test blocker: reproducing the bug requires a live Level viewport plus two
  separately colliding floor components. After IDE compilation, paint across a
  Landscape/component seam and confirm no seam-shaped gap remains.
- Avoid next time: use geometric continuity tests for surface painting; never
  infer one logical paint surface from one `UPrimitiveComponent` pointer.
- Possible conflicts: a projection can now continue onto an adjacent valid
  floor component inside the configured projection distance. Holes still fail
  when no compatible floor hit exists.

## 2026-07-20 - Mesh overlay accumulated color and ignored Layer Priority

- Symptom: repeated strokes made one Layer progressively darker, overlapping
  Layer colors mixed, and a lower-Priority Layer could remain visible over a
  higher-Priority Layer.
- Trigger / repro: repeatedly paint one location, then paint a second enabled
  Layer with a different Priority over the same surface.
- Why it happens: viewport rendering submitted every stored stamp triangle to
  a translucent material. Overdraw accumulated alpha, and draw order followed
  Layer-array order instead of resolving Priority.
- Root cause: durable query triangles were rendered directly even though their
  additive stamp representation is not a valid compositing representation.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerRendering.h`
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerRendering.cpp`
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerEdMode.h`
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerEdMode.cpp`
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPreviewEdMode.h`
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPreviewEdMode.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerVisualizationTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/ExecutionFlowExamples.md`
  - `Docs/BugLog.md`
- Fix: derive an anchor-local resolved visualization grid. Same-surface repeat
  coverage emits one cell, disabled Layers emit none, and the enabled Layer
  with greatest Priority owns each overlapping cell. Edit and Preview modes
  cache resolved data instead of rebuilding it every viewport frame.
- Regression-test name:
  `ComposableCameraSystem.Editor.MeshCamera.ResolvedVisualization`.
- Test blocker: automation covers repeat de-duplication, Priority order
  independence, and Enable fallback. Perceived edge quality and transparency
  still require a Level viewport smoke test after IDE compilation.
- Avoid next time: never render additive authoring primitives directly when UX
  requires set-like paint coverage or winner-takes-all Layer semantics.
- Possible conflicts: visualization uses a 10-unit minimum cell size and may
  coarsen for very large document bounds. Durable authoring/runtime triangles
  and runtime query results remain unchanged.

## 2026-07-20 - Mesh toolkit mismatched FSpawnTabArgs declaration kind

- Symptom: Editor compilation failed with C4099 because `FSpawnTabArgs` was
  first declared as a class and later declared as a struct.
- Trigger / repro: compile the Editor target after adding the mesh toolkit's
  custom primary-tab spawn callback.
- Why it happens: MSVC tracks the class-key used by forward declarations, and
  this project treats the resulting warning as an error.
- Root cause: the mesh toolkit header used `struct FSpawnTabArgs`, while the
  existing Shot Editor and UE editor headers use `class FSpawnTabArgs`.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerModeToolkit.h`
  - `Docs/BugLog.md`
  - `Docs/TechDoc.md`
- Fix: use the canonical `class FSpawnTabArgs` forward declaration.
- Regression-test name:
  `ComposableCameraSystemEditor non-unity compile: tab-spawn declaration-kind hygiene`.
- Test blocker: C4099 occurs during C++ compilation before automation modules
  load, and project rules prohibit command-line UBT. Verify through the next
  full Editor-target rebuild in Rider or Visual Studio.
- Avoid next time: match UE's existing class-key exactly when forward-declaring
  engine types; search all project declarations before adding one.
- Possible conflicts: none. The type remains incomplete in the header and the
  callback signature is unchanged.

## 2026-07-19 - Closing the mesh edit tab left its mode rendering active

- Symptom: closing the `Edit Mesh Camera Layers` panel removed the UI, but
  painted Layer visualization remained in Level viewports.
- Trigger / repro: activate the mesh edit mode, paint or load Layer data,
  then close its primary mode tab with the tab close button.
- Why it happens: UE's world-centric mode-tab close removes the tab but does
  not automatically deactivate its legacy `FEdMode`. No viewport redraw was
  requested during the tool's exit path either.
- Root cause: toolkit-tab lifetime and edit-mode lifetime were treated as the
  same state without an explicit close bridge.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerEdMode.h`
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerEdMode.cpp`
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerModeToolkit.h`
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerModeToolkit.cpp`
  - `Docs/BugLog.md`
- Fix: bind the primary tab's close callback to edit-mode deletion, guard exit
  against callback re-entry, and redraw Level viewports after exit.
- Regression-test name:
  `ComposableCameraSystem Editor Mesh Layer close-tab lifecycle smoke test`.
- Test blocker: reproducing a world-centric mode tab close requires a live
  Level Editor tab manager and registered `FEditorModeTools`; the headless
  automation harness cannot create that host reliably. After IDE compilation,
  open Edit, close its mode tab, and verify the overlay disappears immediately.
- Avoid next time: world-centric toolkit tab closure must explicitly define
  whether its owning legacy mode survives or exits.
- Possible conflicts: none expected. Programmatic mode exit uses the same
  callback but is suppressed by the exit re-entry guard.

## 2026-07-19 - Mesh preview command silently ignored active edit mode

- Symptom: `Show Mesh Camera Layers` sometimes appeared to do nothing and
  required repeated clicks.
- Trigger / repro: invoke Show while mesh edit mode is active, including the
  stale-active state left after closing its tab.
- Why it happens: preview toggle activated preview only when edit mode was
  already inactive. The active-edit branch performed no action.
- Root cause: mutual exclusion was encoded as a silent guard instead of a
  deterministic edit-to-preview transition.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Utilities/ComposableCameraMeshLayerTool.cpp`
  - `Docs/BugLog.md`
- Fix: a preview-on request first deactivates edit mode, then activates preview
  and redraws Level viewports. A preview-off request deactivates and redraws.
- Regression-test name:
  `ComposableCameraSystem Editor Mesh Layer one-click preview transition smoke test`.
- Test blocker: command behavior depends on the global Level Editor mode stack
  and its registered mode factories. Verify after IDE compilation: activate
  Edit, click Show once, and confirm Edit closes while Preview becomes checked.
- Avoid next time: mutually exclusive mode commands must transition from every
  valid source state; never encode one source state as a no-op.
- Possible conflicts: switching Edit to Preview now triggers the existing dirty
  save prompt before Preview activates. This is intentional and prevents silent
  loss of transient edits.

## 2026-07-19 - Mesh editor mode had incomplete and shadowed toolkit types

- Symptom: Editor compilation failed with C2027 when `Enter()` called
  `Owner->GetToolkitHost()`, then C4458 because a local `DetailsView` hid
  `FModeToolkit::DetailsView`.
- Trigger / repro: compile the new mesh Layer editor mode as independent
  translation units with warnings treated as errors.
- Why it happens: `EdMode.h` only forward-declares `FEditorModeTools`, so
  dereferencing `Owner` requires `EditorModeManager.h`. The toolkit also chose
  the same name as a protected base-class member for its custom Details view.
- Root cause: implementation relied on transitive/unity include visibility and
  did not audit inherited member names.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerEdMode.cpp`
  - `Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerModeToolkit.cpp`
  - `Docs/BugLog.md`
- Fix: include `EditorModeManager.h` at the `FEditorModeTools` dereference site;
  rename the custom local widget to `LayerDetailsView`. Keep it separate from
  the base member because `FModeToolkit::Init` creates and replaces its own
  `DetailsView`.
- Regression-test name:
  `ComposableCameraSystemEditor non-unity compile: mesh mode include and shadowing hygiene`.
- Test blocker: both failures occur before automation modules load, and project
  rules prohibit command-line UBT. Verify through a full Editor-target rebuild
  in Rider or Visual Studio.
- Avoid next time: include defining headers where forward-declared types are
  dereferenced; check protected base members before naming toolkit locals.
- Possible conflicts: none. Changes affect compile-time visibility and a local
  identifier only; Tool layout, settings ownership, and save behavior remain
  unchanged.

## 2026-07-19 - Mesh storage field shadowed AActor Layers

- Symptom: Unreal Header Tool rejected
  `AComposableCameraMeshSurfaceStorageActor::Layers` because `AActor`
  already defines a member with that name.
- Trigger / repro: compile the Editor target after adding the mesh surface
  storage actor.
- Why it happens: UHT forbids a reflected member in a derived class from
  shadowing an inherited member, even when the base member is not used by the
  feature.
- Root cause: the new private `UPROPERTY` was named without auditing inherited
  `AActor` fields.
- Touched files:
  - `Source/ComposableCameraSystem/Public/MeshCamera/ComposableCameraMeshSurfaceStorageActor.h`
  - `Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshSurfaceStorageActor.cpp`
  - `Docs/BugLog.md`
- Fix: rename the serialized private field from `Layers` to
  `LayerDefinitions`; keep the public `GetLayers()` API unchanged.
- Regression-test name:
  `ComposableCameraSystemEditor non-unity compile: mesh storage inherited-name collision`.
- Test blocker: UHT fails before an automation module can load, and project
  rules prohibit command-line UBT. Verify with a full Editor-target rebuild in
  Rider or Visual Studio.
- Avoid next time: audit inherited reflected names before adding fields to
  `AActor` descendants; use domain-specific storage names.
- Possible conflicts: serialized property name changes, but this actor has not
  yet completed its first successful compile/save. No asset migration is
  required. Runtime query behavior and public tool API remain unchanged.

## 2026-07-19 - Expired mesh Profile weak pointer could preserve old modifiers

- Symptom: after a mesh storage Level unloads and its Profile becomes
  unreachable, a local player can keep the duplicated mesh Modifier set even
  though the next surface query returns no Profile.
- Trigger / repro: enter a painted mesh Layer, unload its owning streamed
  Level or Level Instance, allow GC to release the Profile source, then tick the
  mesh world subsystem.
- Why it happens: player state intentionally keeps Profile as a weak pointer.
  Once it expires, `State.Profile.Get()` and the new null Profile compare equal.
  The old early-return treated that as an unchanged null state without checking
  whether duplicated Modifier instances were still registered.
- Root cause: profile identity and applied modifier-instance lifetime were
  collapsed into one weak-pointer equality check.
- Touched files:
  - `Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshWorldSubsystem.cpp`
  - `Docs/BugLog.md`
- Fix: null-profile equality may early-return only when the state's duplicated
  Modifier instance array is also empty. An expired source with live instances
  now enters replacement, removes the old set, and clears state.
- Regression-test name:
  `ComposableCameraSystem Mesh Profile streamed-Level unload smoke test`.
- Test blocker: reproducing weak source expiry requires a PIE world, a streamed
  Level lifecycle, forced GC, and a live CCS player camera manager. The focused
  pure surface-query automation test does not model those engine lifetimes, and
  project rules prohibit launching Unreal Editor automation from shell. Verify
  in PIE after IDE compilation by entering the Layer, unloading its Level,
  running GC, and checking that the PCM effective Modifier set is empty.
- Avoid next time: weak source identity cannot alone describe applied runtime
  state. Any null-equality fast path must also verify that owned runtime
  instances are empty.
- Possible conflicts: none expected. Stable non-null Profiles still use the
  same no-work fast path; only expired/null state with residual instances now
  performs cleanup.

## 2026-07-19 - Runtime Debug editor files relied on incomplete node types

- Symptom: editor compilation failed with C2061/C2665/C2511 around
  `ShowRuntimeDebugForNode`, then C2027/C2232 when Runtime Debug widgets called
  `NodeTemplate->GetClass()`.
- Trigger / repro: compile the Runtime Debug port as separate editor translation
  units, as done by the UE5.7 build that reported the failure.
- Why it happens: the toolkit header used
  `UComposableCameraNodeGraphNode*` without declaring that type. Two widget
  implementation files dereferenced `UComposableCameraCameraNodeBase` while
  seeing only its forward declaration through other headers.
- Root cause: new Runtime Debug code depended on incidental transitive/unity
  includes instead of declaring pointer-only types and including definitions at
  dereference sites.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Public/Toolkits/ComposableCameraTypeAssetEditorToolkit.h`
  - `Source/ComposableCameraSystemEditor/Private/Editors/SComposableCameraGraphNode.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimeDebugPanel.cpp`
  - `Docs/BugLog.md`
- Fix: forward-declare `UComposableCameraNodeGraphNode` in the toolkit header;
  explicitly include `Nodes/ComposableCameraCameraNodeBase.h` in both widget
  implementation files before dereferencing `NodeTemplate`.
- Regression-test name: `ComposableCameraSystemEditor non-unity compile: Runtime Debug include completeness`.
- Test blocker: this failure occurs before an automation module can load.
  Project rules also prohibit command-line UBT. Verify by fully rebuilding the
  Editor target in Rider or Visual Studio with the affected files compiled as
  separate translation units.
- Avoid next time: forward-declare every pointer parameter named by a public
  header; include the defining header in each `.cpp` that dereferences an
  otherwise incomplete UObject type. Never rely on unity grouping.
- Possible conflicts: none. Only compile-time type visibility changes; runtime
  debug behavior and serialized data remain unchanged.

## 2026-07-18 - Collapsing Runtime Debug rows hides lower nodes

- Symptom: with all Runtime Debug items expanded, collapsing the first few rows
  from top to bottom makes lower node items disappear and leaves a large empty
  area in the panel.
- Trigger / repro: run PIE with many active nodes, expand every Runtime Debug
  item, then collapse them sequentially from the top. The second or third
  collapse exposes the stale blank region.
- Why it happens: `SExpandableArea` updates its own DesiredSize, but the owning
  virtualized `SListView` is not told that a generated variable-height row
  changed shape. It keeps expanded row heights and the old scroll range, so it
  does not generate lower rows for newly available space.
- Root cause: expansion state was persisted in `ExpandedNodes` without routing
  the row-shape change through the panel's list remeasurement policy.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Public/Widgets/SComposableCameraRuntimeDebugPanel.h`
  - `Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimeDebugPanel.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimeDebugPanelTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: centralize variable-height list refresh, invoke it whenever a visible
  item expands or collapses, and reuse the existing one-shot post-rebuild pass.
  Both scroll range and lower-row virtualization are then recalculated.
- Regression-test name:
  `ComposableCameraSystem.Editor.Debug.RuntimeDebugPanel`.
- Test blocker: automation verifies that collapse schedules and consumes one
  remeasurement pass. Exact lower-row generation after sequential clicks needs
  a PIE visual smoke test after IDE compilation; project rules prohibit
  launching Unreal Editor automation from shell.
- Avoid next time: persisting child expansion state is insufficient inside a
  virtualized variable-height list; notify the owner whenever row shape changes.
- Possible conflicts: each actual visible expansion change adds one two-pass
  list refresh. Live value-only updates still do not rebuild rows every frame.

## 2026-07-18 - Pinning a runtime debug card moves it on high-DPI desktops

- Symptom: clicking Pin reuses the visible node debug card, but the resulting
  persistent window jumps to another screen position instead of staying put.
- Trigger / repro: use Windows display scaling above 100%, hover a runtime
  camera node, then click the card's Pin button.
- Why it happens: the tooltip host position is already a DPI-adjusted physical
  desktop coordinate. A normal `SWindow` defaults
  `AdjustInitialSizeAndPositionForDPIScale` to true and multiplies that captured
  position by the monitor DPI scale again.
- Root cause: the hover-to-window promotion mixed tooltip physical-screen
  coordinates with normal-window DPI-adjusted initialization semantics.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Public/Editors/SComposableCameraGraphNode.h`
  - `Source/ComposableCameraSystemEditor/Private/Editors/SComposableCameraGraphNode.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraNodeRuntimeTooltipTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: construct the pinned native child with
  `AdjustInitialSizeAndPositionForDPIScale(false)`, matching UE5.6 tooltip-window
  coordinate semantics. Card identity and captured outer-window position stay
  unchanged.
- Regression-test name:
  `ComposableCameraSystem.Editor.Debug.NodeRuntimeTooltip`.
- Test blocker: automation verifies the pinned `SWindow` keeps the captured
  initial screen position. Exact OS placement across mixed-DPI monitors needs a
  PIE visual smoke test after IDE compilation; project rules prohibit launching
  Unreal Editor automation from shell.
- Avoid next time: when promoting popup content into another Slate window,
  audit whether the source coordinate is logical Slate space or physical desktop
  space before accepting `SWindow`'s default DPI adjustment.
- Possible conflicts: native-child ownership, title bar, card reuse, and manual
  movement remain unchanged. Only initial position conversion changes.

## 2026-07-18 - Runtime Debug navigation focus transition is too subtle

- Symptom: double-clicking an active graph node successfully scrolls to and
  expands its Runtime Debug item, but the visual transition is easy to miss.
- Trigger / repro: run PIE, double-click an active camera graph node, then watch
  the corresponding item in the left Runtime Debug panel.
- Why it happens: the focus effect only multiplied item content 30% toward the
  node color. Its 0.8-second `CubicOut` curve removed most contrast near the
  beginning, especially on dark editor themes and similarly colored cards.
- Root cause: navigation feedback relied on a short, low-contrast content tint
  without a dedicated background/foreground highlight layer.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Public/Widgets/SComposableCameraRuntimeDebugPanel.h`
  - `Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimeDebugPanel.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimeDebugPanelTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: use a 1.25-second linear fade combining stronger content tint with a
  32%-opaque, hit-test-invisible whole-item node-color overlay. Explicitly jump
  the sequence to its start so repeated double-clicks replay full feedback.
- Regression-test name:
  `ComposableCameraSystem.Editor.Debug.RuntimeDebugPanel`.
- Test blocker: automation verifies that focus begins with overlay opacity at
  least 25%. Exact perceived contrast and timing require a PIE visual smoke test
  after IDE compilation; project rules prohibit launching Unreal Editor
  automation from shell.
- Avoid next time: navigation feedback needs a measurable contrast floor and a
  replay policy; animation-active state alone does not prove visibility.
- Possible conflicts: overlay sits above the card but is hit-test-invisible, so
  disclosure controls remain interactive. Selection remains disabled; no blue
  row state returns.

## 2026-07-18 - Runtime Debug initial expanded rows render incompletely

- Symptom: when Runtime Debug first becomes populated, some left-panel node
  items are clipped or missing content; one mouse-wheel step makes every item
  render completely.
- Trigger / repro: start PIE with Runtime Debug open and enough active nodes or
  parameters to produce multiple expanded variable-height rows. Observe the
  first populated frame before scrolling.
- Why it happens: `RequestListRefresh` performs the first row-generation pass
  before expanded rows containing wrapped text have stable width-dependent
  DesiredSize values. With unchanged membership, CCS requests no second layout.
  Mouse-wheel input incidentally calls the list's layout-refresh path.
- Root cause: the aggregate panel treated one list regeneration as sufficient
  for dynamically sized expanded rows.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Public/Widgets/SComposableCameraRuntimeDebugPanel.h`
  - `Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimeDebugPanel.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimeDebugPanelTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: arm a post-rebuild flag for each non-empty membership/shape refresh.
  `OnItemsRebuilt` clears the flag first, then requests exactly one second
  layout pass so existing rows are measured again after their sizes stabilize.
- Regression-test name:
  `ComposableCameraSystem.Editor.Debug.RuntimeDebugPanel`.
- Test blocker: automation verifies that initial active rows arm the second pass
  and that the rebuild callback consumes it exactly once. Actual clipping and
  first-paint completeness require a PIE visual smoke test after IDE compile;
  project rules prohibit launching Unreal Editor automation from shell.
- Avoid next time: variable-height Slate rows with expanded or wrapped children
  need an explicit post-generation layout policy; do not rely on scrolling to
  invalidate stale DesiredSize caches.
- Possible conflicts: each non-empty list-shape change adds one layout-only
  refresh. Live value updates still do not rebuild rows every frame.

## 2026-07-17 - Runtime Debug hover, Pin, and focus interaction regressions

- Symptom: an unpinned node hover card remains visible after the cursor leaves;
  Pin opens equivalent content at a new cursor-offset location; graph-node
  navigation leaves a blue selected-row background in Runtime Debug; item
  headers also show an unwanted parameter-count number.
- Trigger / repro: during PIE, hover a camera graph node and move away; hover
  again and click Pin; then double-click an active graph node and inspect the
  target Runtime Debug item header/background.
- Why it happens: UE interactive tooltips deliberately stay alive when no new
  tooltip replaces them. Pin rebuilt a second card and positioned its window
  from the current cursor. `FocusNode` called `SListView::SetSelection`, which
  activates the default table-row selection brush. The header explicitly
  rendered `ParameterDisplayValues.Num()`.
- Root cause: the first Runtime Debug implementation relied on default Slate
  interaction semantics that did not match the intended transient-card and
  navigation UX.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Public/Editors/SComposableCameraGraphNode.h`
  - `Source/ComposableCameraSystemEditor/Private/Editors/SComposableCameraGraphNode.cpp`
  - `Source/ComposableCameraSystemEditor/Public/Widgets/SComposableCameraRuntimeDebugPanel.h`
  - `Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimeDebugPanel.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraNodeRuntimeTooltipTests.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraRuntimeDebugPanelTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: actively close the interactive card after both source and card lose
  hover, with a crossing grace interval. Pin detaches and reuses the current
  card at its tooltip-host position. Runtime Debug disables selection and uses
  a whole-item `FCurveSequence` tint fade for navigation. Remove header count.
- Regression-test names:
  `ComposableCameraSystem.Editor.Debug.NodeRuntimeTooltip` and
  `ComposableCameraSystem.Editor.Debug.RuntimeDebugPanel`.
- Test blocker: automation covers leave-policy decisions, hover-card reuse,
  focus animation state, and absence of list selection. Final position,
  disappearance timing, tint appearance, and removed header number need a PIE
  visual smoke test after IDE compilation. Project rules prohibit launching
  Unreal Editor automation from shell.
- Avoid next time: inspect Slate's default lifecycle and paint semantics before
  relying on `IsInteractive`, `SetSelection`, or cursor-derived popup positions;
  separate navigation feedback from persistent selection.
- Possible conflicts: the 0.12-second leave grace intentionally keeps the card
  alive while crossing from node to card. Pinned observers remain independent
  native child windows and still close with their graph-node widget.

## 2026-07-17 - Runtime Debug panel repeats wrong SOverlay include

- Symptom: ComposableCameraSystemEditor compile fails with
  `C1083: Cannot open include file: 'Widgets/Layout/SOverlay.h'` in
  `SComposableCameraRuntimeDebugPanel.cpp`.
- Trigger / repro: compile the editor module after adding the aggregate Runtime
  Debug panel.
- Why it happens: the new panel guessed `SOverlay` belonged under Slate's
  `Widgets/Layout/` include directory.
- Root cause: `SOverlay` is declared by UE5.6 SlateCore at
  `Widgets/SOverlay.h`; this repeated the previously recorded Shot Editor
  include-path bug.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraRuntimeDebugPanel.cpp`
  - `Docs/BugLog.md`
- Fix: replace `#include "Widgets/Layout/SOverlay.h"` with
  `#include "Widgets/SOverlay.h"`.
- Regression-test name: `ComposableCameraSystemEditor IDE compile`.
- Test blocker: automation cannot validate a missing C++ include before the
  module compiles, and project rules prohibit Codex from invoking UBT or an IDE
  build from shell. User must compile in Rider or Visual Studio.
- Avoid next time: before adding a Slate include, search UE5.6 headers and this
  module's existing includes; also check BugLog for the widget name.
- Possible conflicts: none expected; only the include path changed.

## 2026-07-16 - Modifier array entries after Index 0 lost the mode checkbox

- Symptom: Index 0 showed `Use Custom Modifier Class`, but later array entries
  displayed UE's default polymorphic Modifier picker and exposed only the old
  custom Modifier fields.
- Trigger / repro: open a Modifier data asset, configure Index 0 as Node Type,
  add Index 1, then choose a custom Modifier subclass in the default object row.
- Why it happens: the customization tried to replace null or derived inline
  objects through `IPropertyHandle::SetValue`. UE5.6
  `FPropertyHandleObject::SetValue` deliberately returns `Fail` when its property
  node has `EditInlineNew`, so `EnsureWrapperForElement` returned null and left
  the default row visible.
- Root cause: the normalization path assumed normal object-property write
  semantics for an inline-instanced object handle.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Public/Customizations/ComposableCameraModifierDetails.h`
  - `Source/ComposableCameraSystemEditor/Private/Customizations/ComposableCameraModifierDetails.cpp`
  - `Docs/TechDoc.md`
- Fix: for a single customized asset, normalize the authoritative
  `Modifiers[ArrayIndex]` slot directly before creating the property row. Null
  entries become exact-base wrappers; derived legacy entries are duplicated into
  the wrapper's Custom branch.
- Regression-test name: Modifier multi-element wrapper Details smoke test.
- Test blocker: array-row composition and add-button refresh require a live
  Unreal Editor Details view. After compiling, create at least three entries and
  verify every index starts with `Use Custom Modifier Class` and supports both
  modes.
- How to avoid: do not call object-handle `SetValue` for `EditInlineNew` nodes;
  update the authoritative owner slot with transaction/dirty tracking before
  composing custom rows.
- Possible conflicts: multi-object Details intentionally avoids normalizing
  differing array slots. Edit one Modifier data asset at a time for this custom
  authoring UI.

## 2026-07-15 - Modifier Details duplicated a local weak utilities variable

- Symptom: `ComposableCameraSystemEditor` failed with C2374 in
  `ComposableCameraModifierDetails.cpp`: `LocalWeakUtilities` was redefined.
- Trigger / repro: compile the Editor module after adding the per-item mode
  checkbox to `GenerateModifierElement`.
- Why it happens: the function already declared the weak utilities pointer for
  the element value-change callback; the mode-row block declared the same local
  name again in the same scope.
- Root cause: the new UI block copied an existing local declaration instead of
  reusing it.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Customizations/ComposableCameraModifierDetails.cpp`
- Fix: remove the second declaration. Both callbacks capture the original weak
  pointer declared at function entry.
- Regression-test name: `ComposableCameraSystemEditor Modifier Details compile`.
- Test blocker: this is a translation-unit compile regression. Verification
  requires rebuilding the Editor target in Rider or Visual Studio; runtime
  automation cannot run while the module fails to compile.
- How to avoid: before introducing a callback-local alias, search the complete
  function scope for an existing alias with the same lifetime and purpose.
- Possible conflicts: none. Runtime and Details behavior remain unchanged.

## 2026-07-15 - Custom Modifier wrapper could enter generic pre-initialize path

- Symptom: a Custom Modifier Class entry could be classified as a generic node
  override and skip its Blueprint `ApplyModifier` callback if its nested object
  contained inherited `NodeTemplate` state.
- Trigger / repro: enable Custom mode, assign a custom modifier whose inherited
  `NodeTemplate` is non-null, then construct a type-asset camera.
- Why it happens: the first wrapper implementation delegated
  `UsesNodeTemplateOverride` and `ApplyModifierToNode` wholesale to the nested
  object, even though Node Type and Custom Modifier Class are mutually exclusive
  authoring modes.
- Root cause: branch selection existed in the UI but was not enforced at the
  runtime execution-phase boundary.
- Touched files:
  - `Source/ComposableCameraSystem/Private/Modifiers/ComposableCameraModifierBase.cpp`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraModifierPropertyOverrideTests.cpp`
- Fix: Custom mode now reads only the nested modifier's legacy `NodeClass`,
  always reports post-initialize timing, and invokes its `ApplyModifier` event
  directly. It never consults generic template state.
- Regression-test name:
  `System.Engine.ComposableCameraSystem.Modifiers.Wrapper.SelectsActiveBranch`.
- How to avoid: when a bool selects mutually exclusive serialized branches,
  enforce the branch at every runtime dispatch point, not only in Details UI.
- Possible conflicts: custom subclasses that intentionally populated inherited
  generic `NodeTemplate` state must use Node Type mode instead; Custom mode is
  reserved for the original `NodeClass + ApplyModifier` contract.

## 2026-07-15 - Editor module failed to link Gameplay Tags string formatting

- Symptom: `ComposableCameraSystemEditor` failed with LNK2019 for
  `FGameplayTagContainer::ToStringSimple(bool) const` from
  `ComposableCameraEditorDumpCommands.cpp`.
- Trigger / repro: compile the Editor module after the camera identity field
  changed to `FGameplayTagContainer` and the editor dump began formatting the
  full container.
- Why it happens: C++ headers were visible through the runtime module, so the
  editor translation unit compiled, but exported Gameplay Tags symbols still
  require a direct module link dependency.
- Root cause: `ComposableCameraSystemEditor.Build.cs` omitted `GameplayTags`.
- Touched files:
  - `Source/ComposableCameraSystemEditor/ComposableCameraSystemEditor.Build.cs`
- Fix: add `GameplayTags` to `PrivateDependencyModuleNames` for the Editor
  module.
- Regression-test name: `ComposableCameraSystemEditor GameplayTags link`.
- Test blocker: this is a module-link regression, not runtime behavior.
  Verification requires a full Rider or Visual Studio Editor build; Unreal
  automation cannot detect a DLL that failed to link.
- How to avoid: every module that directly calls a non-inline exported method
  must declare the exporting module itself. Do not rely on another module's
  public dependency for linker ownership.
- Possible conflicts: none expected. `GameplayTags` is already a Runtime module
  dependency; this only links the Editor module directly.

## 2026-07-15 - Type-asset camera tag trace label stayed empty after tag-container migration

- Symptom: a type-asset camera carried the correct runtime `CameraTags`, but
  its cached Insights trace label still showed `(none)`.
- Trigger / repro: spawn a deferred type-asset camera. Director calls
  `Initialize()` before `ConstructCameraFromTypeAsset()` copies identity fields.
  Read `CameraTagsTraceName` after construction.
- Why it happens: tag-string caching ran only from `Initialize()`, while the
  type asset populated tags later through the pre-BeginPlay construction
  callback.
- Root cause: the tag-container refactor copied the old cache site without
  auditing activation ordering.
- Touched files:
  - `Source/ComposableCameraSystem/Public/Cameras/ComposableCameraCameraBase.h`
  - `Source/ComposableCameraSystem/Private/Cameras/ComposableCameraCameraBase.cpp`
  - `Source/ComposableCameraSystem/Private/Core/ComposableCameraTypeAssetInstantiator.cpp`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraModifierPropertyOverrideTests.cpp`
- Fix: centralize legacy-tag migration, compatibility-field synchronization,
  and trace-label caching in `RefreshCameraTags()`. Call it from both camera
  initialization and type-asset construction after tags are copied.
- Regression-test name:
  `System.Engine.ComposableCameraSystem.Modifiers.CameraTagQuery.FiltersCameraTags`
  (`Type-asset construction refreshes the cached trace label`).
- How to avoid: when a value is copied after `Initialize()`, audit every cache
  derived from that value and refresh it at the final write site.
- Possible conflicts: direct runtime mutation of `CameraTags` after construction
  still requires an explicit `RefreshCameraTags()` call; normal type-asset and
  native-camera initialization paths already call it.

## 2026-07-15 - Modifier node override UI never appeared inside the array

- Symptom: a Modifier data asset showed the default `Node Class` and
  `Node Template` fields instead of a `Node Override` category. Selecting a
  node class left `Node Template` as `None`, so no override checkboxes or node
  parameter rows appeared.
- Trigger / repro: open a `UComposableCameraNodeModifierDataAsset`, add a base
  `UComposableCameraModifierBase` entry to `Modifiers`, expand index 0, and
  select any camera node class.
- Why it happens: `RegisterCustomClassLayout` for
  `UComposableCameraModifierBase` applies when that UObject is a root Details
  object. The modifier is an `EditInlineNew` UObject nested inside a `TArray`,
  so the asset Details view generated its default child layout instead.
- Root cause: the customization was registered at the wrong Details hierarchy
  level. Its `OnNodeClassSelected` callback never existed in the rendered tree,
  leaving `NodeTemplate` uncreated.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Public/Customizations/ComposableCameraModifierDetails.h`
  - `Source/ComposableCameraSystemEditor/Private/Customizations/ComposableCameraModifierDetails.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: register the customization on the Modifier data asset and rebuild
  `Modifiers` with UE5.6 `FDetailArrayBuilder`. Generic base entries now render
  the Node Type picker and checked property rows directly under their array
  element. Legacy Modifier Blueprint entries keep the default inline layout.
  Existing broken base entries with `NodeClass` but no template are repaired on
  open by creating `NodeTemplate` from that class.
- Regression-test name: Modifier asset Node Override Details smoke test.
- Test blocker: Details-row composition and interactive class-picker refresh
  require a live Unreal Editor Details view. Project rules prohibit Codex from
  launching the Editor or automation from shell. After compiling, reopen the
  asset and verify `Node Override -> Modifiers -> Node Type`; choosing
  `CameraOffset` must show its editable parameters and no `PaletteCategory`.
- Avoid next time: register class layouts at the actual root object owned by the
  Details view. For polymorphic inline UObject arrays, use an asset-level array
  builder instead of assuming nested class customizations will run.
- Possible conflicts: custom Modifier Blueprint subclasses intentionally keep
  their legacy default fields. Generic base entries use the new data-driven
  rows only.

## 2026-07-15 - Generic Modifier `TObjectPtr` expressions failed to compile

- Symptom: compiling `ComposableCameraSystem` failed with C2445 in
  `ComposableCameraModifierManager.cpp` and
  `ComposableCameraModifierBase.cpp`; the Editor module also failed with C1083
  because `IPropertyHandle.h` does not exist in UE 5.6.
- Trigger / repro: compile after generic Modifier entries changed transition
  and node-template references to `TObjectPtr`.
- Why it happens: C++ conditional expressions had one `TObjectPtr<T>` operand
  and one raw `T*` operand, so MSVC could not choose a common result type.
  The node-class helper mixed `UClass*` with `TSubclassOf<T>` the same way.
  UE 5.6 exposes `IPropertyHandle` through `PropertyHandle.h`.
- Root cause: implicit smart-pointer conversion was assumed in mixed `?:`
  expressions, and the editor include name was guessed instead of following
  existing UE5.6 module usage.
- Touched files:
  - `Source/ComposableCameraSystem/Private/Core/ComposableCameraModifierManager.cpp`
  - `Source/ComposableCameraSystem/Private/Modifiers/ComposableCameraModifierBase.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Customizations/ComposableCameraModifierDetails.cpp`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: use `.Get()` on modifier-asset transition references before the ternary,
  return the template class through an explicit `TSubclassOf` branch, and
  include `PropertyHandle.h`.
- Regression-test name: `ComposableCameraSystem IDE generic Modifier compile`.
- Test blocker: this is a C++ type/include compile failure. No runtime
  automation test can execute before modules compile, and project rules forbid
  Codex from invoking UBT or IDE builds from shell. Rebuild in Rider / Visual
  Studio; then run `System.Engine.ComposableCameraSystem.Modifiers.PropertyOverride.*`.
- Avoid next time: never mix `TObjectPtr<T>` and raw `T*` in `?:`; normalize
  to raw pointers with `.Get()`. Do not infer UE header names; grep existing
  module includes first.
- Possible conflicts: none expected. Pointer ownership and serialized asset
  data remain unchanged.

## 2026-06-23 - Test actor helper names collided in unity build

- Symptom: compiling tests failed with C2084 "`SpawnActorWithRoot` already has
  a body" when `ComposableCameraLockOnAimPointNodeTests.cpp` and
  `ComposableCameraComputePositionBetweenActorsNodeTests.cpp` were compiled in
  the same unity translation unit. Follow-up C2065 / C2440 errors appeared at
  LockOnAimPoint call sites because the duplicate definition confused name
  lookup after the first error.
- Trigger / repro: compile the `ComposableCameraSystem` tests in Rider /
  Visual Studio with unity builds enabled so both test files are grouped
  together.
- Why it happens: both files placed an `AActor* SpawnActorWithRoot(UWorld*,
  const FVector&)` helper inside an anonymous namespace. Anonymous namespaces
  give internal linkage per translation unit, but unity builds concatenate
  several `.cpp` files into one translation unit, so the names still collide.
- Root cause: test-local helper names were generic instead of file-specific.
- Touched files:
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraLockOnAimPointNodeTests.cpp`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: rename the LockOnAimPoint test helper to
  `SpawnLockOnAimPointActorWithRoot` and update its call sites.
- Regression-test name: `ComposableCameraSystem IDE unity test compile`.
- Test blocker: this is a C++ translation-unit compile failure; no runtime
  automation test can run until compilation succeeds. Project rules prohibit
  Codex from invoking UBT or IDE compilation from shell, so user must recompile
  in Rider / Visual Studio.
- Avoid next time: give test-local helpers file-specific names even inside
  anonymous namespaces, especially in `Private/Tests` where unity grouping is
  common.
- Possible conflicts: none expected; this changes only a private test helper
  name and two call sites.

## 2026-06-23 - Viewport Legend listed the whole debug palette under All CVars

- Symptom: the debug panel's bottom Legend could show every node and transition
  color entry when `CCS.Debug.Viewport.Nodes.All` or
  `CCS.Debug.Viewport.Transitions.All` was enabled, even if the current camera
  did not contain those node types and no transition of that type was running.
- Trigger / repro: enable `CCS.Debug.Panel`, `CCS.Debug.Viewport`, and the node
  / transition All viewport CVar while playing a camera with only a small subset
  of debug-capable nodes, then look at the bottom Legend.
- Why it happens: `BuildLegendRows` filtered only by viewport CVars. The shared
  palette correctly listed every known gizmo type, but the panel did not compare
  those entries against the current `RunningCamera` or the active evaluation
  tree.
- Root cause: the Legend conflated "debug type exists and is enabled globally"
  with "this type can draw this frame".
- Touched files:
  - `Source/ComposableCameraSystem/Private/Debug/ComposableCameraDebugPanel.cpp`
  - `Source/ComposableCameraSystem/Private/Debug/ComposableCameraViewportDebugLegendUtils.h`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraBugFixTests.cpp`
  - `Docs/DesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: add a shared private legend matcher and make the panel require both CVar
  enablement and relevance. Node rows now match node classes on the current
  running camera; transition rows now match active `InnerTransition` class names
  in the active context tree snapshot. Source / target swatches appear only when
  at least one relevant transition row can draw.
- Regression-test name:
  `System.Engine.ComposableCameraSystem.Debug.ViewportLegend.MatchesRuntimeClasses`.
- Test blocker: automation test added, but project rules prohibit Codex from
  invoking Unreal automation or UBT from shell. User must compile and run the
  test from Rider / Visual Studio / Unreal Editor.
- Avoid next time: palette metadata is not draw state. Any panel row that claims
  "currently drawing" must also check the live camera / active tree source that
  invokes the corresponding draw override.
- Possible conflicts: Blueprint subclasses whose class names do not include the
  base gizmo token may not match transition rows because tree snapshots store
  display class names, not `UClass*`. Node rows walk the superclass chain and
  should handle inherited node gizmo overrides.

## 2026-06-17 - Rewind trace writers compiled into non-editor runtime builds

- Symptom: CCS Rewind support was intended to be editor-only, but the runtime
  module always depended on `TraceLog` and compiled the trace writer code in
  non-editor Development / DebugGame targets. Shipping already stripped the
  writer functions through `!UE_BUILD_SHIPPING`, but packaged non-shipping
  targets still carried the trace channel / CVar path.
- Trigger / repro: inspect `ComposableCameraSystem.Build.cs` and
  `Debug/ComposableCameraTrace.h` after the Rewind Debugger integration.
- Why it happens: `UE_COMPOSABLE_CAMERA_TRACE` checked `UE_TRACE_ENABLED`,
  `!IS_PROGRAM`, `!UE_BUILD_SHIPPING`, and `!UE_BUILD_TEST`, but did not also
  require `WITH_EDITOR`. The public runtime Build.cs therefore had to list
  `TraceLog` unconditionally so the public trace header could include
  `Trace/Config.h`.
- Root cause: editor-only Rewind instrumentation lived in the runtime module
  without an editor build gate. The editor module owned Rewind playback /
  TraceServices ingestion correctly, but the runtime emission side was too
  broadly compiled.
- Touched files:
  - `Source/ComposableCameraSystem/ComposableCameraSystem.Build.cs`
  - `Source/ComposableCameraSystem/Public/Debug/ComposableCameraTrace.h`
  - `Source/ComposableCameraSystem/Private/Debug/ComposableCameraTrace.cpp`
  - `Source/ComposableCameraSystem/Private/Core/ComposableCameraPlayerCameraManager.cpp`
  - `Source/ComposableCameraSystem/Private/LevelSequence/ComposableCameraLevelSequenceComponent.cpp`
  - `Docs/DesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: add `WITH_EDITOR` to `UE_COMPOSABLE_CAMERA_TRACE`, include TraceLog /
  ObjectTrace headers only when that macro is enabled, and move the runtime
  module's `TraceLog` dependency into the editor-target branch.
- Regression-test name: non-editor package dependency audit for CCS Rewind
  trace. The static check is that `TraceLog` appears in the runtime Build.cs
  only under `Target.bBuildEditor`, and non-editor builds see
  `UE_COMPOSABLE_CAMERA_TRACE == 0`.
- Test blocker: project rules prohibit Codex from running UBT, packaging, or
  automation from shell. User must compile a packaged/non-editor target in
  Rider / Visual Studio to validate link output.
- Avoid next time: editor tooling that needs runtime instrumentation must use a
  runtime shim gated by `WITH_EDITOR`; do not rely on `!UE_BUILD_SHIPPING` when
  the feature is editor-only.
- Possible conflicts: Rewind recording in PIE/editor remains enabled because
  editor targets define `WITH_EDITOR`. Packaged Development builds no longer
  expose `CCS.Debug.Trace` or emit CCS Rewind trace events.

## 2026-06-17 - Rewind sphere labels were invisible and 3D primitives jittered while scrubbing

- Symptom: Rewind playback showed correctly sized CCS camera / node gizmos, but
  sphere text labels were absent. Dragging the Rewind timeline made 3D gizmos
  such as spheres and lines twitch between positions.
- Trigger / repro: record CCS playback, select the pawn in Rewind Debugger,
  scrub / drag the timeline over frames with node or transition 3D gizmos.
- Why it happens: Rewind label replay called `DrawDebugString`, which writes
  through `AHUD::AddDebugText`; the Rewind visualized playback world is not
  guaranteed to have a player-controller / HUD path for that text. The same
  extension submitted 3D `DrawDebug*` primitives from a `UDebugDrawService`
  callback. UE 5.6 game viewport rendering flushes temporary line batchers
  before the debug-draw-service pass, so those 3D primitives render on the next
  scene frame and can appear one scrub step behind the visualized actors.
- Root cause: Rewind playback used HUD debug text and post-scene debug-draw
  service timing for data that needed immediate Canvas text and current-frame
  3D line-batcher submission.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerExtension.h`
  - `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerExtension.cpp`
  - `Docs/DesignDoc.md`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: split Rewind visualization into two paths. A core ticker submits 3D
  active-frustum and primitive line-batcher draws before scene rendering. The
  `UDebugDrawService` callback now draws only projected sphere labels with
  `FCanvasTextItem`, so labels do not depend on HUD debug strings.
- Regression-test name: Rewind Debugger selected-pawn playback smoke test.
- Test blocker: this is an editor viewport rendering / scrub timing issue.
  Project rules prohibit Codex from launching Unreal Editor or automation from
  shell. User must verify by recording, selecting a pawn, scrubbing, and
  checking that labels appear and 3D gizmos no longer twitch.
- Avoid next time: do not submit Rewind 3D line-batcher primitives from
  `UDebugDrawService`; use a pre-render ticker or a scene proxy. Do not use
  `DrawDebugString` for Rewind labels; draw Canvas text directly.
- Possible conflicts: live viewport debug still uses the existing ticker /
  HUD-string helper path. This change only affects Rewind playback.

## 2026-06-17 - Rewind trace tests used unsupported FName UTEST_EQUAL overload

- Symptom: compiling `ComposableCameraTraceTests.cpp` failed with
  `C2665: 'FAutomationTestBase::TestEqual': no overloaded function could
  convert all the argument types` at the `FName` label assertions.
- Trigger / repro: compile the Rewind label fix in Rider / Visual Studio.
- Why it happens: UE 5.6 automation `UTEST_EQUAL` has overloads for strings,
  colors, numbers, and several engine types, but not for `FName`.
- Root cause: the Rewind label tests asserted `FName` equality through
  `UTEST_EQUAL`, so the macro expanded into `TestEqual` and overload resolution
  could not choose a valid function.
- Touched files:
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraTraceTests.cpp`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: assert label identity with `UTEST_TRUE` and explicit `FName` comparison
  / `IsNone()` checks.
- Regression-test name:
  `ComposableCameraSystem.RewindTrace.PrimitiveRoundTrip`,
  `ComposableCameraSystem.RewindTrace.PrimitiveV1Compatibility`, and
  `ComposableCameraSystem.RewindTrace.CaptureSinkRecordsPrimitives`.
- Test blocker: project rules prohibit Codex from running Unreal builds or
  automation from shell. User must recompile in Rider / Visual Studio.
- Avoid next time: use `UTEST_TRUE(NameA == NameB)` or convert both sides to
  strings when testing `FName`; do not pass `FName` to `UTEST_EQUAL`.
- Possible conflicts: none; this changes test assertions only.

## 2026-06-17 - Rewind playback drew oversized active frustum and dropped sphere labels

- Symptom: Rewind Debugger playback drew a huge blue active-camera frustum that
  dominated the viewport, and several recorded sphere gizmos had no text label.
- Trigger / repro: record CCS camera playback, select the pawn in Rewind
  Debugger, then scrub a frame that contains node / transition sphere gizmos.
- Why it happens: the editor Rewind extension synthesized the active-camera
  frustum with scale `100.0f`, unlike live CCS camera debug which uses scale
  `1.0f`. Sphere label text also stopped at the live-only
  `DrawSolidDebugSphere` helper because `FComposableCameraDebugDrawSink` and
  `FComposableCameraDebugPrimitive` had no label payload.
- Root cause: Rewind playback used a Blueprint-style camera debug scale for
  active camera display, and primitive stream version 1 had no marker-name
  field for sink-routed sphere gizmos.
- Touched files:
  - `Source/ComposableCameraSystem/Public/Debug/ComposableCameraDebugDrawSink.h`
  - `Source/ComposableCameraSystem/Public/Debug/ComposableCameraTraceTypes.h`
  - `Source/ComposableCameraSystem/Private/Debug/ComposableCameraDebugDrawSink.cpp`
  - `Source/ComposableCameraSystem/Private/Debug/ComposableCameraTraceTypes.cpp`
  - `Source/ComposableCameraSystem/Private/Nodes/*`
  - `Source/ComposableCameraSystem/Private/Transitions/*`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraTraceTests.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerExtension.cpp`
  - `Docs/DesignDoc.md`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: draw the synthesized active-camera frustum at scale `1.0f`, add an
  optional `Label` through `DrawSphere`, serialize it in primitive stream
  version 2, replay it for solid and wire spheres, and label the built-in node /
  transition sphere call sites with short role names.
- Regression-test name:
  `ComposableCameraSystem.RewindTrace.PrimitiveRoundTrip` and
  `ComposableCameraSystem.RewindTrace.CaptureSinkRecordsPrimitives` cover label
  serialization / capture. `ComposableCameraSystem.RewindTrace.PrimitiveV1Compatibility`
  covers old label-free primitive streams. The active-frustum scale requires a
  Rewind Debugger selected-pawn playback smoke test.
- Test blocker: automation tests were updated, but project rules prohibit
  Codex from running Unreal automation or launching the editor from shell. User
  must compile and verify Rewind playback in Rider / Visual Studio / Unreal
  Editor.
- Avoid next time: editor replay of a runtime debug primitive should preserve
  the same scale and label contract as live CCS debug. Do not synthesize
  Blueprint camera helper scales for CCS camera poses.
- Possible conflicts: primitive stream version 2 adds label data but still
  accepts version 1 streams; old recordings simply replay without labels.

## 2026-06-17 - Rewind playback target lookup read GameplayProvider without session read scope

- Symptom: Rewind Debugger playback can break into the debugger with exception
  `0x80000003` at `TraceServices::FAnalysisSessionLock::ReadAccessCheck()`.
  The stack shows `FGameplayProvider::EnumerateObjects`,
  `FRewindDebugger::GetTargetActorId`, and
  `FComposableCameraRewindDebuggerExtension::Update`.
- Trigger / repro: compile the CCS Rewind support, open Rewind Debugger with a
  selected actor, then let the CCS rewind extension tick during playback /
  scrub.
- Why it happens: UE 5.6 `FRewindDebugger::GetTargetActorId()` enumerates the
  `GameplayProvider` but does not create its own
  `FAnalysisSessionReadScope`. Callers must already hold the analysis session
  read lock.
- Root cause: `FComposableCameraRewindDebuggerExtension::Update` called
  `RewindDebugger->GetTargetActorId()` before entering any analysis session
  read scope, only to build its playback cache key.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerExtension.h`
  - `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraRewindDebuggerExtension.cpp`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: add `GetTargetActorIdForPlayback`, wrap the target lookup in
  `FAnalysisSessionReadScope`, and pass the resulting actor id into
  `FindPlaybackFrames` so frame lookup no longer performs an extra target query.
- Regression-test name: Rewind Debugger selected-pawn playback smoke test.
- Test blocker: this is an editor Rewind Debugger tick path and project rules
  prohibit Codex from launching Unreal Editor or automation from shell. User
  must verify by recording, selecting a pawn, and scrubbing in Rewind Debugger.
- Avoid next time: every call into Rewind Debugger APIs that may read
  `IGameplayProvider` must be audited for caller-owned
  `FAnalysisSessionReadScope`; do not assume helper APIs create their own lock.
- Possible conflicts: none expected. The cache key still includes target actor
  id; only the lock ownership changed.

## 2026-06-17 - Trace provider returned TSharedRef values as timeline pointers

- Symptom: `ComposableCameraSystemEditor` compile fails in
  `ComposableCameraTraceProvider.cpp` with `C2440: 'return': cannot convert
  from TraceServices::TPointTimeline<...> to const ITimeline<...>*`.
- Trigger / repro: compile after adding the CCS Rewind trace provider that
  stores active-camera and CCS-evaluation timelines as `TSharedRef<TPointTimeline>`.
- Why it happens: `TSharedRef::Get()` returns an object reference, unlike
  `TSharedPtr::Get()` which returns a pointer.
- Root cause: provider getters returned `ActiveTimeline.Get()` and
  `EvaluationTimeline.Get()` directly from functions whose return types are
  `const ITimeline<...>*`.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Trace/ComposableCameraTraceProvider.cpp`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: return the address of the shared reference's object,
  `&ActiveTimeline.Get()` and `&EvaluationTimeline.Get()`. `TPointTimeline`
  derives from `ITimeline`, so the address converts to the getter's interface
  pointer type.
- Regression-test name: `ComposableCameraSystemEditor IDE compile`.
- Test blocker: no focused automation test can catch this C++ type error
  without compiling the editor module, and project rules prohibit Codex from
  invoking UBT / IDE compilation from shell. User must compile in Rider or
  Visual Studio.
- Avoid next time: when exposing a `TSharedRef<T>` as a raw pointer, remember
  `Get()` returns `T&`; take its address, or store `TSharedPtr<T>` if pointer
  semantics are needed.
- Possible conflicts: none expected; provider ownership and timeline storage
  remain unchanged.

## 2026-06-17 - Rewind trace skipped node gizmos unless live viewport CVars were on

- Symptom: enabling `CCS.Debug.Trace 1` could record only the CCS camera
  frustum and omit node / transition gizmos in Rewind Debugger.
- Trigger / repro: start Rewind recording with CCS trace enabled, leave
  `CCS.Debug.Viewport.Nodes.All`, `CCS.Debug.Viewport.Transitions.All`, and
  per-node / per-transition viewport CVars off, then play a CCS gameplay or
  Level Sequence camera that owns 3D node gizmos.
- Why it happens: PCM and Level Sequence trace writers captured primitives by
  calling `DrawCameraDebug` with a capture sink, but each node / transition
  override still self-gated on live viewport CVars or cached `All` state before
  emitting anything into the sink.
- Root cause: the draw-sink abstraction carried draw primitive operations but
  did not carry the intent that trace capture needs all 3D gizmos independent
  of live viewport UI state.
- Touched files:
  - `Source/ComposableCameraSystem/Public/Debug/ComposableCameraDebugDrawSink.h`
  - `Source/ComposableCameraSystem/Public/Debug/ComposableCameraViewportDebug.h`
  - `Source/ComposableCameraSystem/Private/Cameras/ComposableCameraCameraBase.cpp`
  - `Source/ComposableCameraSystem/Private/Nodes/*`
  - `Source/ComposableCameraSystem/Private/Transitions/*`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraTraceTests.cpp`
  - `Docs/DesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: add force-all gizmo queries to `FComposableCameraDebugDrawSink`, make
  `FComposableCameraPrimitiveCaptureSink` return true for node and transition
  gizmos, and include that sink intent in every 3D node / transition CVar gate.
  Live draw sinks keep the default false value, so viewport CVar behavior is
  unchanged.
- Regression-test name:
  `ComposableCameraSystem.RewindTrace.CaptureSinkForcesGizmos`.
- Test blocker: automation test added, but project rules prohibit Codex from
  invoking Unreal Editor automation or UBT from shell. User must compile and
  run it from Rider / Visual Studio / Unreal Editor.
- Avoid next time: when a debug API is reused for capture, pass capture intent
  through the API itself instead of depending on viewport-global cached state.
- Possible conflicts: any future 3D node or transition gizmo must include the
  draw sink force check in its CVar early-out. 2D HUD-only gizmos still use
  live viewport CVars only and are not part of Rewind primitive capture.

## 2026-06-16 - Viewport sphere labels stayed at stale positions

- Symptom: viewport debug labels appeared after the sphere label pass, but the
  text stayed at its first generated world position instead of following the
  moving sphere.
- Trigger / repro: enable `CCS.Debug.Viewport` plus any moving node gizmo such
  as LookAt, ScreenSpacePivot, CollisionPush, or a transition marker. Move the
  target / camera; the sphere updates, while the label remains behind.
- Why it happens: `DrawDebugString` stores text through `AHUD::AddDebugText`.
  The sphere line primitive is redrawn every frame, but the label was submitted
  with persistent duration.
- Root cause: `DrawSolidDebugSphere` used `DrawDebugString(...,
  Duration=-1.f)` for labels, so HUD debug text kept the original absolute
  location instead of expiring and being replaced at the next frame's location.
- Touched files:
  - `Source/ComposableCameraSystem/Public/Debug/ComposableCameraViewportDebug.h`
  - `Source/ComposableCameraSystem/Private/Debug/ComposableCameraViewportDebug.cpp`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraBugFixTests.cpp`
  - `Docs/DesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: expose `GetSphereLabelDurationSeconds()`, use its frame-local `0.f`
  lifetime for sphere labels, and document that labels must not be persistent.
- Regression-test name:
  `System.Engine.ComposableCameraSystem.Debug.ViewportSphereLabels.UseFrameLifetime`.
- Test blocker: automation test added, but project rules prohibit Codex from
  invoking Unreal Editor automation or UBT from shell. User must compile and
  run it from Rider / Visual Studio / Unreal Editor.
- Avoid next time: when a viewport gizmo draws every tick, any attached text
  must expire every tick too. Do not use persistent HUD debug text for moving
  world markers.
- Possible conflicts: `FComposableCameraViewportDebug` gained another public
  helper in the runtime header. Header/API change means full editor restart is
  safer than Live Coding.

## 2026-06-16 - Viewport Legend colors drifted from 3D sphere colors

- Symptom: the debug panel's bottom Legend could show a color that did not
  match the matching 3D sphere, and dense sphere overlays were hard to map back
  to nodes.
- Trigger / repro: enable `CCS.Debug.Viewport`, enable node gizmos such as
  `CCS.Debug.Viewport.LookAt`, `CCS.Debug.Viewport.CollisionPush`, or
  `CCS.Debug.Viewport.Spline`, then compare the panel Legend swatch with the
  drawn sphere. Enable several node gizmos together to see unlabeled spheres
  pile up.
- Why it happens: the Legend owned a duplicated static color table while node
  and transition draw sites owned separate hard-coded `FColor` values.
- Root cause: no shared debug palette / legend metadata existed, so values such
  as LookAt cyan vs. blue and Spline violet values could drift silently.
- Touched files:
  - `Source/ComposableCameraSystem/Public/Debug/ComposableCameraViewportDebug.h`
  - `Source/ComposableCameraSystem/Private/Debug/ComposableCameraViewportDebug.cpp`
  - `Source/ComposableCameraSystem/Private/Debug/ComposableCameraDebugPanel.cpp`
  - `Source/ComposableCameraSystem/Private/Nodes/*`
  - `Source/ComposableCameraSystem/Private/Transitions/*`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraBugFixTests.cpp`
  - `Docs/DesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: add `FComposableCameraViewportDebugColors` and
  `FComposableCameraViewportDebug::GetLegendEntries()`, wire the Legend and 3D
  debug draw sites to the shared palette, and add optional text labels to
  `DrawSolidDebugSphere` call sites.
- Regression-test name:
  `System.Engine.ComposableCameraSystem.Debug.ViewportLegend.UsesSharedGizmoColors`.
- Test blocker: automation test added, but project rules prohibit Codex from
  invoking Unreal Editor automation or UBT from shell. User must compile and
  run it from Rider / Visual Studio / Unreal Editor.
- Avoid next time: never add a viewport gizmo color only inside a draw site or
  only inside the Legend. Add it to `FComposableCameraViewportDebugColors` and
  expose it through `GetLegendEntries()` when it needs a panel row.
- Possible conflicts: `DrawSolidDebugSphere` gained an optional `Label`
  parameter in a public runtime header. Header/API change means full editor
  restart is safer than Live Coding.

## 2026-06-16 - NodeGraphSync test local Candidate variables shadow each other

- Symptom: `ComposableCameraSystemEditor` compile fails with
  `C4456: declaration of 'Candidate' hides previous local declaration` in
  `ComposableCameraNodeGraphSyncTests.cpp`.
- Trigger / repro: compile after adding
  `ComposableCameraSystem.Editor.NodeGraphSync.BeginPlaySetVariableExecRoundTripWithGetNode`.
- Why it happens: MSVC treats reused local names inside the same `if / else if`
  chain as shadowing, and the project treats that warning as an error.
- Root cause: the new test used the generic local name `Candidate` for three
  different cast variables in one control-flow chain.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraNodeGraphSyncTests.cpp`
  - `Docs/BugLog.md`
- Fix: rename the locals to `BeginPlayCandidate`, `VariableCandidate`, and
  `GraphNodeCandidate`.
- Regression-test name: `ComposableCameraSystemEditor IDE compile`.
- Test blocker: no focused automation test can catch a C++ warning-as-error
  without compiling the module, and project rules prohibit Codex from invoking
  UBT / IDE compilation from shell. User must compile in Rider or Visual Studio.
- Avoid next time: avoid broad reused local names in chained `if / else if`
  declarations; use role-specific names, especially in test scans over mixed
  graph-node types.
- Possible conflicts: none expected; only test local variable names changed.

## 2026-06-16 - BeginPlay Set-variable exec wires break after editor reopen

- Symptom: BeginPlay compute-chain exec wires that pass through a variable
  `Set` node can disappear after closing and reopening the engine.
- Trigger / repro: create a BeginPlay chain such as `BeginPlay -> Begin Play:
  Position Between Actors -> Set PivotPosition -> Begin Play: Set Rotation`,
  and also keep a same-variable `Get PivotPosition` node elsewhere in the
  graph. Save, close, and reopen the editor.
- Why it happens: `ComputeFullExecChain` serialized a `SetVariable` step by
  variable GUID only. During `RebuildFromTypeAsset`, variable graph nodes were
  also looked up by variable GUID only.
- Root cause: one runtime variable can have multiple graph nodes. A same-variable
  `Get` node could overwrite the GUID lookup used to restore the `Set` node's
  exec pins, so the rebuild tried to find exec pins on the wrong graph node and
  skipped the links.
- Touched files:
  - `Source/ComposableCameraSystem/Public/Nodes/ComposableCameraNodePinTypes.h`
  - `Source/ComposableCameraSystemEditor/Public/Editors/ComposableCameraNodeGraph.h`
  - `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraNodeGraph.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraNodeGraphSyncTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: `FComposableCameraExecEntry` now stores the exact variable graph-node
  GUID for `SetVariable` entries. Graph rebuild resolves Set exec wires by node
  GUID first, then falls back to a Set-only variable GUID lookup for old assets.
- Regression-test name:
  `ComposableCameraSystem.Editor.NodeGraphSync.BeginPlaySetVariableExecRoundTripWithGetNode`.
- Test blocker: automation test added, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must compile and run the
  test from Rider / Visual Studio / Unreal Editor.
- Avoid next time: whenever serialized graph state can contain several nodes
  for one runtime object, persist graph-node identity separately from runtime
  object identity.
- Possible conflicts: this adds one reflected field to
  `FComposableCameraExecEntry`, so the editor needs a full restart. Runtime
  dispatch still uses variable name / slot data; the new GUID is editor rebuild
  metadata.

## 2026-06-16 - SetRotation RotationOffset applied pitch in world space

- Symptom: `SetRotation` / `Compute: Set Rotation` `RotationOffset` produced
  the wrong result when the base rotation already had non-zero pitch / roll and
  the offset included pitch. Yaw needed world-space behavior, while pitch needed
  local-space behavior.
- Trigger / repro: configure a SetRotation node with `RotationSource =
  FromRotator`, a non-trivial base rotation such as `(Pitch=25, Yaw=70,
  Roll=15)`, and `RotationOffset = (Pitch=20, Yaw=45, Roll=0)`.
- Why it happens: the resolver used
  `UKismetMathLibrary::ComposeRotators(Base, Offset)`, which applies the whole
  offset in one composition space instead of splitting yaw and pitch semantics.
- Root cause: `RotationOffset` was implemented as a generic rotator
  composition, but the intended camera-control convention is mixed-space:
  yaw around world Z, then pitch / roll in the resolved camera local frame.
- Touched files:
  - `Source/ComposableCameraSystem/Public/Nodes/ComposableCameraSetRotationNode.h`
  - `Source/ComposableCameraSystem/Private/Nodes/ComposableCameraSetRotationNode.cpp`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraSetRotationNodeTests.cpp`
  - `Docs/DesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: replace generic `ComposeRotators` with explicit quaternion composition
  `WorldYaw * Base * LocalPitchRoll`, matching `ControlRotate`'s world-yaw /
  local-pitch rule.
- Regression-test name:
  `System.Engine.ComposableCameraSystem.Nodes.SetRotation.OffsetYawWorldPitchLocal`.
- Test blocker: automation test added, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must run it from Rider /
  Visual Studio / Unreal Editor.
- Avoid next time: when a rotator offset mixes camera-control axes, document
  each axis's space and test with a non-trivial base rotation. Do not assume
  `FRotator` composition gives the desired per-axis frame.
- Possible conflicts: `SetRotation` and `Compute: Set Rotation` now differ from
  `PivotRotate`, whose `RotationOffset` remains fully local-space by design.

## 2026-06-13 - Shot Editor status bar could prioritize Free-exit actions over stale host state

- Symptom: the unified Shot Editor status bar could keep showing Free-exit
  Save / Discard / Stay actions even when the active host UObject had gone
  stale.
- Trigger / repro: enter Free mode, request Drag / Lock to queue a Free-exit
  status, then let the active Shot host be destroyed before the root widget's
  tick clears the context.
- Why it happens: the first status-bar priority draft checked pending Free-exit
  state before validating the active Shot / host pair.
- Root cause: Free-exit action visibility was treated as higher priority than
  host liveness, even though Save needs a live host to safely write Shot data.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Widgets/ComposableCameraShotEditorStatusBarUtils.h`
  - `Source/ComposableCameraSystemEditor/Private/Widgets/SShotEditorRoot.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraShotEditorTests.cpp`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: status resolution now checks stale-host and no-Shot states before
  pending Free-exit state. `CanSaveFreeExitStatus` also requires an active Shot
  and valid host.
- Regression-test name:
  `ComposableCameraSystem.ShotEditor.StatusBarState`.
- Test blocker: automation test added, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must run it from IDE /
  Unreal Editor.
- Avoid next time: any editor action row that writes data must gate on object
  liveness before checking action-specific pending state.
- Possible conflicts: only Shot Editor status-bar action visibility changes.
  Normal Free-exit Save / Discard / Stay behavior with a live host is unchanged.

## 2026-06-12 - CompositionPreserving snaps on final collapse frame

- Symptom: a CompositionPreserving transition keeps the subject framed during
  most of the blend, then jumps on the final frame / tree collapse.
- Trigger / repro: start an A to B CompositionPreserving transition, move the
  controlled pawn during the blend, and let the transition finish.
- Why it happens: the previous formula captured B-side subject offset only at
  transition start. After the pawn moved, alpha near 1 still produced a
  composition-preserving pose based on stale B offset. The base transition and
  evaluation tree then returned/collapsed to the live raw B pose.
- Root cause: the preserved pose did not converge to `CurrentTargetPose` when
  alpha reached 1 unless B's start-time subject offset still matched the live
  target pose.
- Touched files:
  - `Source/ComposableCameraSystem/Public/Transitions/ComposableCameraCompositionPreservingTransition.h`
  - `Source/ComposableCameraSystem/Private/Transitions/ComposableCameraCompositionPreservingTransition.cpp`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraCompositionPreservingTransitionTests.cpp`
  - `Docs/DesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: keep the A/source offset captured, but recompute B/target offset every
  frame from live subject location and `CurrentTargetPose`. The blend now uses
  `Lerp(CapturedSourceOffset, LiveTargetOffset, Alpha)`, so alpha = 1 matches
  live B and collapse has no stale-offset snap.
- Regression-test name:
  `System.Engine.ComposableCameraSystem.Transitions.CompositionPreserving.ConvergesToLiveTargetPose`.
- Test blocker: automation test added, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must run it from IDE /
  Unreal Editor.
- Avoid next time: any custom transition output that later collapses to the
  right/target child must converge to the live target pose at alpha = 1, unless
  the transition also owns an explicit post-collapse handoff.
- Possible conflicts: this changes only CompositionPreserving's target-side
  offset math. B remains unmodified; the transition just uses live B as the
  convergence endpoint.

## 2026-06-12 - Transition owner PCM lookup was duplicated and inconsistent

- Symptom: transition code that needed the owning player camera manager could
  either pay repeated outer lookups or resolve the wrong local player.
- Trigger / repro: use `ControllerControlledPawn` on a CompositionPreserving
  transition in a multi-controller world, or use PathGuided's intermediate
  camera from a non-zero / non-first local player.
- Why it happens: transitions had no shared owner-PCM cache. Composition
  initially did its own typed-outer lookup, while PathGuided asked the
  Blueprint library for player index 0.
- Root cause: `UComposableCameraTransitionBase` did not expose the same owning
  PCM context that camera nodes already receive.
- Touched files:
  - `Source/ComposableCameraSystem/Public/Transitions/ComposableCameraTransitionBase.h`
  - `Source/ComposableCameraSystem/Private/Transitions/ComposableCameraTransitionBase.cpp`
  - `Source/ComposableCameraSystem/Public/Transitions/ComposableCameraCompositionPreservingTransition.h`
  - `Source/ComposableCameraSystem/Private/Transitions/ComposableCameraCompositionPreservingTransition.cpp`
  - `Source/ComposableCameraSystem/Private/Transitions/ComposableCameraPathGuidedTransition.cpp`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraTestObjects.h`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraCompositionPreservingTransitionTests.cpp`
  - `Docs/DesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: cache the typed outer `AComposableCameraPlayerCameraManager` once in
  `TransitionEnabled`. CompositionPreserving resolves controlled pawn through
  that cache. PathGuided initializes its intermediate camera with that cache
  instead of player index 0.
- Regression-test name:
  `System.Engine.ComposableCameraSystem.Transitions.Base.CachesOuterPCM` and
  `System.Engine.ComposableCameraSystem.Transitions.CompositionPreserving.ResolvesControlledPawnFromOuterPCM`.
- Test blocker: automation tests added, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must run them from IDE /
  Unreal Editor. PathGuided's private intermediate-camera path still needs an
  IDE compile plus a non-first-player manual smoke test.
- Avoid next time: shared runtime context should live on the base class when
  more than one transition needs it. Do not hard-code player index 0 from a
  transition instance.
- Possible conflicts: Level Sequence and other no-PCM paths still pass through
  `nullptr`; `AComposableCameraCameraBase::Initialize(nullptr)` already supports
  that mode.

## 2026-06-12 - CompositionPreserving mixed driving rotation with raw target location

- Symptom: the controlled pawn could still drift out of the expected framing
  even after the transition resolved the right pawn and did not hard-cut.
- Trigger / repro: transition from camera A to camera B where B has a different
  rotation and world position from A, then move the subject during the blend.
- Why it happens: the previous implementation rebuilt only the source side
  around `R'`, then blended that source location toward raw target world
  location while forcing output rotation back to `R'`.
- Root cause: location and rotation were no longer in the same composition
  space. `R'` represented the driving rotation, but target location still
  represented B's evaluated pose under `R_B`.
- Touched files:
  - `Source/ComposableCameraSystem/Public/Transitions/ComposableCameraCompositionPreservingTransition.h`
  - `Source/ComposableCameraSystem/Private/Transitions/ComposableCameraCompositionPreservingTransition.cpp`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraCompositionPreservingTransitionTests.cpp`
  - `Docs/DesignDoc.md`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: capture both source and target subject offsets at transition start. This
  was later refined by the 2026-06-12 final-collapse entry: source offset stays
  captured, but target offset is recomputed live so the output converges to B.
- Regression-test name:
  `System.Engine.ComposableCameraSystem.Transitions.CompositionPreserving.RebuildsSourceFromMovingSubject`.
- Test blocker: automation test updated, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must run it from IDE /
  Unreal Editor.
- Avoid next time: whenever a transition overrides rotation, recompute location
  in the same rotation / composition space. Do not blend toward an endpoint
  world location after changing the rotation frame.
- Possible conflicts: CompositionPreserving no longer treats raw B location as
  the direct positional target during the blend. B's start composition relative
  to the subject is still represented by the captured target offset.

## 2026-06-12 - CompositionPreserving transition hard-cuts when wrapper time is unset

- Symptom: `UComposableCameraCompositionPreservingTransition` appears not to
  preserve the controlled pawn during a transition.
- Trigger / repro: add a CompositionPreserving transition, assign a valid
  `DrivingTransition` with a non-zero duration, but leave the outer wrapper's
  `TransitionTime` unset or zero.
- Why it happens: the base transition `Evaluate` decrements the outer wrapper's
  `RemainingTime` before `OnEvaluate`. With `RemainingTime == 0`, the wrapper
  finishes on the first frame and returns the target pose, so the subject
  capture and rebuilt source pose never affect output. In the default
  `ControllerControlledPawn` mode, the transition also resolved actors without
  passing its outer `AComposableCameraPlayerCameraManager`, so multi-controller
  worlds could select the world's first controller pawn instead of the PCM
  owner's pawn.
- Root cause: `OnBeginPlay` always pushed the wrapper's `TransitionTime` into
  the driving transition, but did not adopt the driving transition's authored
  duration when the wrapper duration was unset. `ResolveSubjectActor` also
  passed `nullptr` for the PCM even though runtime transition instances are
  outered under a director / PCM chain.
- Touched files:
  - `Source/ComposableCameraSystem/Private/Transitions/ComposableCameraCompositionPreservingTransition.cpp`
  - `Source/ComposableCameraSystem/Private/Tests/ComposableCameraCompositionPreservingTransitionTests.cpp`
  - `Docs/TechDoc.md`
  - `Docs/BugLog.md`
- Fix: when wrapper `TransitionTime <= 0` and `DrivingTransition` has a valid
  duration, set the wrapper transition time and remaining time from the driving
  transition before the base first-frame finish check can collapse the blend.
  Resolve `ControllerControlledPawn` through the owning PCM so the owning
  player controller wins over world-first-controller fallback. A later
  2026-06-12 entry moved that PCM lookup into the transition base class.
- Regression-test name:
  `System.Engine.ComposableCameraSystem.Transitions.CompositionPreserving.UsesDrivingTransitionTimeWhenWrapperTimeUnset`
  and
  `System.Engine.ComposableCameraSystem.Transitions.CompositionPreserving.ResolvesControlledPawnFromOuterPCM`.
- Test blocker: automation test added, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must run it from IDE /
  Unreal Editor.
- Avoid next time: wrapper transitions that delegate timing must either require
  an explicit outer duration or adopt the inner transition duration before the
  base class can apply the first-frame remaining-time finish check. Runtime
  transitions that expose `ControllerControlledPawn` must pass their owning PCM
  into `ResolveActorInput`, not rely on world fallback.
- Possible conflicts: the duration-adoption behavior is local to
  CompositionPreserving. Other wrapper transitions still require their wrapper
  duration to be authored explicitly.

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

## 2026-06-13 - Shot Editor viewport toolbar uses wrong SOverlay include

- Symptom: ComposableCameraSystemEditor compile fails with
  `C1083: Cannot open include file: 'Widgets/Layout/SOverlay.h'`.
- Trigger / repro: compile the editor module after adding the Shot Editor
  viewport floating toolbar.
- Why it happens: the new `SShotEditorRoot.cpp` include used the wrong Slate
  header path for `SOverlay`.
- Root cause: `SOverlay` lives at `Widgets/SOverlay.h`; `Widgets/Layout/`
  contains layout widgets like boxes and splitters, not this overlay header.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Widgets/SShotEditorRoot.cpp`
  - `Docs/BugLog.md`
- Fix: replace `#include "Widgets/Layout/SOverlay.h"` with
  `#include "Widgets/SOverlay.h"`.
- Regression-test name: `ComposableCameraSystemEditor IDE compile`.
- Test blocker: no focused automation test can catch a missing C++ include
  without compiling the module, and project rules prohibit Codex from invoking
  UBT / IDE compilation from shell. User must compile in Rider or Visual Studio.
- Avoid next time: for Slate widgets whose path is uncertain, grep a known UE
  source/include reference or prefer the canonical engine header path before
  committing.
- Possible conflicts: none expected; only include path changed.

## 2026-06-13 - Shot Editor Reset toolbar action enabled outside Free mode

- Symptom: Shot Editor viewport `Reset` button stays clickable in Drag and
  Lock even though those modes already reassert the solved Shot camera every
  tick.
- Trigger / repro: open Shot Editor, bind any Shot, stay in Drag or Lock mode,
  observe the floating viewport toolbar.
- Why it happens: the toolbar action-state helper treated `ResetView` like a
  generic active-Shot command instead of a Free-camera recovery command.
- Root cause: `ResetView` was grouped with HUD / Guides toggles in
  `IsToolbarActionEnabled`, while `FrameTargets` alone carried the Free-mode
  gate.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Widgets/ComposableCameraShotViewportToolbarUtils.h`
  - `Source/ComposableCameraSystemEditor/Private/Widgets/SShotEditorRoot.cpp`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraShotEditorTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/BugLog.md`
- Fix: remove the visible `Copy` toolbar action and gate both `FrameTargets`
  and `ResetView` on active Shot plus `EShotEditorMode::Free`.
- Regression-test name:
  `ComposableCameraSystem.ShotEditor.ViewportToolbarActionState`.
- Test blocker: automation test updated, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must run it from IDE /
  Unreal Editor.
- Avoid next time: model toolbar actions by user intent first. View-recovery
  actions belong to Free mode; always-on debug toggles should be separate.
- Possible conflicts: Ctrl+Alt+C still copies the viewport camera transform as
  a hidden/debug shortcut, but the floating toolbar no longer exposes Copy.

## 2026-06-13 - Shot Editor reverse-solve local shadows viewport AspectRatio member

- Symptom: ComposableCameraSystemEditor compile fails with
  `C4458: declaration of 'AspectRatio' hides class member` in
  `ComposableCameraShotEditorViewportClient.cpp`.
- Trigger / repro: compile the editor module after restoring the Shot Editor
  Free-mode reverse-solve path.
- Why it happens: `FComposableCameraShotEditorViewportClient` inherits from
  `FEditorViewportClient`, which already has an `AspectRatio` member. The new
  reverse-solve code declared a local variable with the same name inside a
  member function.
- Root cause: the local variable used a generic viewport-math name instead of a
  specific one, and MSVC warning C4458 is treated as an error in this build.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraShotEditorViewportClient.cpp`
  - `Docs/BugLog.md`
- Fix: rename the local to `ViewportAspectRatio` and update all uses in the
  reverse-solve projection math.
- Regression-test name: `ComposableCameraSystemEditor IDE compile`.
- Test blocker: no focused automation test can catch this C++ warning-as-error
  without compiling the module, and project rules prohibit Codex from invoking
  UBT / IDE compilation from shell. User must compile in Rider or Visual Studio.
- Avoid next time: inside `FEditorViewportClient` subclasses, use
  domain-specific local names such as `ViewportAspectRatio` instead of broad
  names that may collide with inherited engine members.
- Possible conflicts: none expected; the rename does not change projection
  math.

## 2026-06-13 - Shot Editor floating toolbar covers diagnostic HUD

- Symptom: Shot Editor viewport floating toolbar appears in the top-left corner
  and covers the diagnostic HUD text.
- Trigger / repro: open Shot Editor with diagnostic HUD enabled and observe the
  floating Reset / HUD / Guides toolbar in the same corner as the HUD readout.
- Why it happens: both overlays were anchored to `HAlign_Left` /
  `VAlign_Top`.
- Root cause: the toolbar was introduced as a viewport-local overlay without
  considering the existing top-left diagnostic overlay owned by the viewport
  client.
- Touched files:
  - `Source/ComposableCameraSystemEditor/Private/Widgets/SShotEditorRoot.cpp`
  - `Source/ComposableCameraSystemEditor/Public/Widgets/SShotEditorRoot.h`
  - `Source/ComposableCameraSystemEditor/Private/Widgets/ComposableCameraShotViewportToolbarUtils.h`
  - `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraShotEditorTests.cpp`
  - `Docs/EditorDesignDoc.md`
  - `Docs/BugLog.md`
- Fix: move the floating toolbar to the top-right corner and add a collapsible
  `Tools +/-` control that hides Reset / HUD / Guides when collapsed.
- Regression-test name:
  `ComposableCameraSystem.ShotEditor.ViewportToolbarActionState`.
- Test blocker: automation test updated, but project rules prohibit Codex from
  invoking Unreal Editor automation from shell. User must run it from IDE /
  Unreal Editor.
- Avoid next time: place new viewport overlays against existing paint-time
  overlays first; top-left belongs to diagnostic text unless the design doc
  explicitly changes that.
- Possible conflicts: on very narrow viewport widths, the top-right toolbar may
  overlap scene content, but it no longer competes with the diagnostic HUD and
  can be collapsed to the small Tools button.

## 2026-09-29 - Bulk Patch expiration depends on caller retaining its handle (open)

- Status: found by source review; runtime fix intentionally not implemented in
  this Patch architecture assessment. The regression below is expected to fail
  against the current implementation; it has not been compiled or run.
- Symptom: `ExpireAllPatchesOnContext` can leave a live Patch and its evaluator
  running after the caller has discarded the handle returned by `AddCameraPatch`.
- Trigger / repro: add a Manual-only Patch, let its enter envelope finish,
  release all strong references to its handle, run garbage collection, then
  call `ExpireAllPatchesOnContext`. Evaluate past the exit duration. The Patch
  should disappear, but remains registered. A second Patch whose handle is
  retained does expire, demonstrating the lifetime-dependent difference.
- Why it happens: the instance remains strongly owned by `ActivePatches`, while
  its handle back-link is deliberately weak. `ExpireAll` calls
  `ExpirePatch(Instance->Handle.Get(), ...)`; a collected handle resolves to null
  and the individual expiration function returns before changing instance state.
- Root cause: manager-owned bulk cleanup is routed through an optional
  caller-owned access object rather than the manager's live instance.
- Touched files: `Docs/BugLog.md`,
  `Source/ComposableCameraSystem/Private/Tests/ComposableCameraPatchTests.cpp`.
- Proposed fix (not applied): share instance-level expiration logic between
  `ExpirePatch` and `ExpireAll`; preserve exit-duration overrides and idempotency.
  Do not make handle retention a requirement for context-wide cleanup.
- Regression-test name:
  `ComposableCameraSystem.Patches.ExpireAllWithoutRetainedHandle`.
  It uses actual garbage collection, compares retained and released handles,
  and checks both the exit phase and eventual evaluator destruction.
- Verification blocker: project rules require Rider / Visual Studio compilation
  and IDE / editor-side automation. Codex has not run either. Compile the new
  test in the IDE and run it to confirm the reported failure; repeat after a fix.
- Avoid next time: cleanup APIs should traverse the state they own directly.
  Test bulk cleanup after optional public handles are collected.
- Possible conflicts: Blueprint context cleanup and Manual-only patches are
  affected. Duration / Condition may still retire a Patch independently.
  Director `DestroyAll` already traverses instances directly; Sequencer uses a
  separate overlay map. Keep those paths and individual-handle behavior intact.

## 2026-10-02 - PIE trial must preserve gameplay camera frame memoization

- Symptom: pre-compilation review found that a trial property write could make
  a shared camera evaluate twice in one frame, advancing state twice.
- Trigger / repro: evaluate a gameplay camera once; apply a live trial before
  another evaluation path reaches the same camera in that frame; evaluate it
  again. Both same-frame calls must return the first evaluated pose.
- Why it happens / root cause: the initial editor prototype reused
  `InvalidateTickCache`, whose documented scope is non-DAG external evaluators
  such as Sequencer. Gameplay live edits do not own the snapshot DAG's cache.
- Touched files: `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraLiveEditSession.cpp`,
  `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraLiveEditTests.cpp`,
  `Docs/DesignDoc.md`, `Docs/TechDoc.md`, `Docs/BugLog.md`.
- Fix: trial writes change input values only. Normal evaluation consumes them
  next frame; no camera rebuild, activation, or cache invalidation occurs.
- Regression-test name:
  `ComposableCameraSystem.Editor.LiveEdit.PreservesFrameMemoization`.
  Added, not executed: compile and run in Rider / Visual Studio and the editor.
- Avoid next time: verify the documented owner/scope of a cache bypass before
  borrowing it for a tool. Exercise edits between repeated same-frame reads.
- Possible conflicts: damping, spline progress, Action hooks, transient camera
  lifetime, and property-transition clocks rely on one camera tick per frame.
  Existing Sequencer-owned cache invalidation remains unchanged.

## 2026-10-02 - PIE trial reset must retain a newly Modifier-owned literal

- Superseded later on 2026-10-02: requested trial-over-driver behavior now uses
  an independent evaluation layer. Reset removes that layer immediately even
  for slotless Modifier-owned fields. The named regression was updated to
  verify restoration of the current driver rather than blocked restoration.

- Symptom: pre-compilation review found that Reset could drop a pending trial
  record without restoring its original literal value.
- Trigger / repro: trial an unwired FOV with no default/exposed data slot;
  let a Modifier acquire that property; press Reset while the Modifier owns it.
- Why it happens / root cause: restoring a property directly would overwrite
  the active Modifier, but no lower slot exists to restore underneath it. The
  initial reset loop cleared pending state even when the runtime write failed.
- Touched files: `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraLiveEditSession.h`,
  `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraLiveEditSession.cpp`,
  `Source/ComposableCameraSystemEditor/Private/Widgets/SComposableCameraLiveEditPanel.cpp`,
  `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraLiveEditTests.cpp`,
  `Docs/EditorDesignDoc.md`, `Docs/TechDoc.md`, `Docs/BugLog.md`.
- Fix: existing lower slots can reset without touching the Modifier-owned
  member. Slotless resets return failure and retain trial values until ownership
  releases or PIE ends; UI reports the pending reset rather than success.
- Regression-test name:
  `ComposableCameraSystem.Editor.LiveEdit.ParameterAndModifierOwnership`.
  Added, not executed: compile and run in Rider / Visual Studio and the editor.
- Avoid next time: treat restore success and record disposal as separate steps;
  test ownership changes between trial creation and Reset.
- Possible conflicts: generic and Custom Modifier field ownership must remain
  higher priority. This tool does not unregister their bindings or change
  modifier transition/lifecycle behavior.

## 2026-10-02 - PIE live-edit binding assumed a smart runtime node pointer

- Symptom: UE5.6 Editor-module compilation fails with C2228 in
  `ComposableCameraLiveEditSession.cpp:133`, followed by C3536, C2737 and C2446.
- Trigger / repro: compile `UE5_6Editor` in Rider / Visual Studio with the
  PIE live-edit implementation. The local build log records the failing target
  as this UE5_6 project using UE5.6.1.
- Why it happens / root cause: the binding loop used `.Get()` on an element of
  the existing `TArray<UComposableCameraCameraNodeBase*> CameraNodes`, assuming
  it had the same `TObjectPtr` representation as asset `NodeTemplates`.
  The three subsequent errors are cascades from the invalid initializer.
- Touched files:
  `Source/ComposableCameraSystemEditor/Private/Editors/ComposableCameraLiveEditSession.cpp`,
  `Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraLiveEditTests.cpp`,
  `Docs/TechDoc.md`, `Docs/BugLog.md`.
- Fix: read the runtime array element directly into a typed local pointer.
  Preserve the weak session references and the existing runtime container API.
- Regression-test name:
  `ComposableCameraSystem.Editor.LiveEdit.DefaultsAndSlotShapes`.
  Strengthened binding checks for the selected camera and selectable runtime
  node, with early failure before dereferencing an unbound editing proxy.
- Verification blocker: this syntax error requires an IDE compilation pass
  before automation can execute. Project rules forbid shell builds/tests.
  Recompile `UE5_6Editor` in Rider / Visual Studio, then run the named test.
- Avoid next time: inspect the exact container declaration before extracting
  an element. Related history: 2026-07-15 generic Modifier pointer-expression
  compilation failures; raw pointers and `TObjectPtr` cannot be interchanged
  by assuming identical member functions.
- Possible conflicts: no runtime layout, node ownership, pin resolution,
  Modifier behavior, or source-asset serialization changes. Other `CameraNodes`
  consumers continue to use the existing raw-pointer API.

## 2026-10-02 - Generic PIE cache refresh must retain owned runtime resources

- Symptom: source review of the expanded all-parameter refresh path found that
  rerunning ordinary initialization could add collision components or duplicate
  MixingCamera child actors every time a parameter changed.
- Trigger / repro: edit ImpulseResolution VelocityDamping / Interpolator several
  times and Reset; count its sphere components. Edit MixingCamera Cameras, then
  switch mixing enum parameters repeatedly; count owned child cameras.
- Why it happens / root cause: OnInitialize builds derived caches but also
  creates lifetime resources. It is not an idempotent generic refresh contract.
  Existing MixingCamera initialization appends to CameraInstances and ordinary
  ImpulseResolution initialization always allocates another sphere.
- Touched files: node base, MixingCamera and ImpulseResolution headers/cpps;
  `Private/Tests/ComposableCameraLiveEditTests.cpp` in the Editor module;
  `Docs/DesignDoc.md`, `Docs/EditorDesignDoc.md`, `Docs/TechDoc.md`,
  `Docs/ExecutionFlowExamples.md`, `Docs/BugLog.md`.
- Fix: editor-only OnLiveEditRefresh defaults to initialization, while resource
  owners specialize it. ImpulseResolution only rebuilds its typed interpolator;
  MixingCamera destroys/clears children and rebuilds them only for Cameras edits.
  Trial values are restored to the underlying layer after event-time refresh.
- Regression-test name:
  `ComposableCameraSystem.Editor.LiveEdit.RefreshReusesImpulseComponent`.
  Added, not run. Project rules require IDE compilation and editor automation.
  MixingCamera manual verification requires a PCM-backed PIE context: edit its
  Cameras array repeatedly, Reset, and confirm no obsolete children remain;
  changing MixMode must preserve child count and identities.
- Avoid next time: distinguish configuration caches from lifetime resources;
  audit every initialization override before adding a generic refresh caller.
- Possible conflicts: overlaps, input bindings, occlusion-material restoration,
  nested camera lifetime and interpolator state. UE5.6 BindActionValue already
  deduplicates action bindings; OcclusionFade initialization restores its previous
  overrides. Camera DAG memoization and Modifier clocks are unchanged.

## 2026-10-02 - Compound pin refresh overwrote nested PIE trial values

- Symptom: an inline interpolator Speed edit could revert to the lower pin
  default while refreshing the edited node's typed interpolator.
- Trigger / repro: Start -> PivotDamping; set UpwardInterpolator to IIR Speed=2,
  start PIE, then change Speed to 7 in Live Editing. Refresh and tick the node.
- Why it happens / root cause: the trial suppression guard was placed in pin
  declaration rather than AutoApplySubobjectPinValues. Refresh applied lower
  compound defaults into the trial-owned object before rebuilding its cache.
- Related history / blast radius: reviewed the resource-refresh entry above and
  compound pin consumers. Declaration must continue reporting the full schema;
  only applying lower values to an actively overridden root must be suppressed.
- Touched files: CameraNodeBase.cpp, ComposableCameraLiveEditTests.cpp,
  TechDoc.md and BugLog.md.
- Fix: move the guard into AutoApplySubobjectPinValues. Cache refresh retains
  all children of the trial root; normal initialization and Reset still resolve
  the lower configuration.
- Regression-test name:
  `ComposableCameraSystem.Editor.LiveEdit.NestedObjectsArraysAndCurveRoundTrip`.
  Checks Speed=7 before and after tick and saved compound default reconstruction.
  Added, not run; compile and run through IDE/editor per project rules.
- Avoid next time: test nested values across both refresh and execution; place
  precedence checks in mutation paths, never schema discovery.
- Possible conflicts: interpolator cache construction and compound pin defaults.
  No changes to wire storage, shipping execution or graph declaration behavior.

## 2026-10-02 - PIE trial actor references escaped the selected world

- Symptom: a reference picked from another PIE world could enter a selected
  camera's trial; an editor actor without a PIE counterpart could remain live.
- Trigger / repro: run two PIE worlds, select a camera in one and assign an
  actor from the other to ActorsForDynamicFoV; repeat with an unmapped editor actor.
- Why it happens / root cause: world remapping treated any PIE reference as
  already mapped and ignored missing mappings when writing runtime trials.
- Related history / blast radius: reviewed pending-trial lifetime and resource
  entries above; audited candidate writes, Apply preflight and PIE detachment.
- Touched files: ComposableCameraLiveEditSession.cpp,
  ComposableCameraLiveEditTests.cpp, EditorDesignDoc.md, TechDoc.md and BugLog.md.
- Fix: require the selected PIE world. Resolve counterparts by actor GUID when
  needed; reject missing actor/component mappings before creating an override,
  and restore the accepted proxy value. Selected-world spawned actors still
  support trials but cannot be saved as defaults without an editor counterpart.
- Regression-test name: `ComposableCameraSystem.Editor.LiveEdit.WorldReferenceScope`.
  Covers cross-world rejection, accepted-value rollback, selected-world trials,
  blocked asset write and clearing unmappable references on PIE end. Added, not
  run; IDE/editor compilation and automation required.
- Avoid next time: world type alone does not prove instance identity; validate
  every mapping result before copying references into a runtime owner.
- Possible conflicts: Actor/component pickers and multi-PIE sessions. Remapping
  walks owned proxy objects only; it never edits external actors or assets.

## 2026-10-02 - Repeated nested trial copies reused stale subobjects

- Symptom: the second nested edit could keep the first trial value; Apply could
  retain an older source interpolator value, and rollback could restore a stale
  accepted snapshot.
- Trigger / repro: Start -> PivotDamping, IIR Speed=2. Trial Speed=7, then 9;
  force a rejected edit to 11 by ending PIE. Verify runtime/rollback=9, Apply,
  then verify source and rebuilt compound default=9.
- Why it happens / root cause: CopyCompleteValue followed by InstanceSubobjects
  finds an existing same-name destination subobject and reuses it without
  copying the new source contents. It is an initialization operation, not a
  repeated deep-copy contract. Plain duplication can also propagate RF_Transient
  from an editor proxy into a saved asset's child object.
- Related history / blast radius: checked the nested-refresh and resource
  entries above; audited all CopyValue / SetLiveEditProperty consumers. The same
  copy routine serves runtime trials, editor baselines/accepted snapshots,
  rollback, Reset and authoring commits.
- Touched files: CameraNodeBase.h/.cpp, ComposableCameraLiveEditSession.cpp,
  ComposableCameraLiveEditTests.cpp, DesignDoc.md, EditorDesignDoc.md, TechDoc.md
  and BugLog.md.
- Fix: editor-only CopyLiveEditProperty duplicates owned references in the
  selected property with unique names, seeds owner/shared-object mappings and
  applies destination propagation flags. Two reference-replacement passes bind
  the new roots and remap their sibling references. All consumers share it.
- Regression-test name:
  `ComposableCameraSystem.Editor.LiveEdit.NestedObjectsArraysAndCurveRoundTrip`.
  Now covers two edits, rejected-edit rollback, latest saved contents, destination
  ownership and absence of transient flags on the saved child. Added, not run;
  project rules require IDE compilation and editor automation.
- Avoid next time: inspect instancing semantics before treating them as copying;
  always test a second write and a transient-to-authoring ownership round trip.
- Possible conflicts: inline subobjects, structs/containers holding instanced
  values, Undo references and GC. Copies occur only at editor event time, never
  in frame evaluation. External assets and actors are not duplicated.

## 2026-10-02 - Unedited runtime references blocked unrelated trial saves

- Symptom: editing FOV could fail because an unchanged runtime-only actor lived
  in ActorsForDynamicFoV; failed Apply could also remap the live editing proxy.
- Trigger / repro: populate the runtime actor array from a caller, including an
  actor in another PIE world; bind, edit only FOV and Apply. Then edit a selected
  PIE-only actor plus FOV and verify rejection leaves both source and proxy intact.
- Why / root cause: runtime writes and Apply remapped/validated the entire proxy;
  Apply performed that mutation before its source-conflict checks.
- History / blast radius: reviewed actor world-scope, owned-copy and atomic-source
  conflict entries above; audited Bind, WriteRuntimeProperty, Apply, Reset and
  DetachRuntime and all RemapWorldReferences consumers.
- Touched files: ComposableCameraLiveEditSession.cpp, ComposableCameraLiveEditTests.cpp,
  DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md, BugLog.md.
- Fix: serialize/remap only selected root properties and their owned subobjects.
  Runtime writes use isolated typed candidates; Apply uses GC-rooted authoring
  candidates, validates every changed root before any source write and never
  mutates editing proxies on failure. Rehash maps/sets after reference changes.
- Regression-test name:
  `ComposableCameraSystem.Editor.LiveEdit.ChangedReferenceScopeAndAtomicSave`.
  Added, not compiled/run; full IDE compile and editor automation required.
- Avoid next time: validation scope must match commit scope; preflight must not
  mutate working copies. Include unrelated caller-driven references in tests.
- Possible conflicts: actor/component, weak/lazy/soft references and nested
  containers; counterpart-world rejection remains intact. External objects are
  neither traversed nor edited.

## 2026-10-02 - Reset retained obsolete source-conflict snapshots after Undo

- Symptom: after Apply -> Undo -> Reset, subsequent trials could never Apply even
  though the user had explicitly discarded the previous trial.
- Trigger / repro: FOV 79 -> trial 101 -> Apply -> Undo -> Reset -> trial 112 -> Apply.
- Why / root cause: Reset cleared runtime overrides and proxy baselines but left
  SourceValues / SourceDefaults pointing at the undone authoring state.
- History / blast radius: reviewed atomic conflict and trial/Modifier ownership
  history; checked all Reset, Bind and Apply consumers and camera-switch gating.
- Touched files: ComposableCameraLiveEditSession.cpp, ComposableCameraLiveEditTests.cpp,
  DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md, BugLog.md.
- Fix: only after a complete Reset succeeds, capture current source signatures
  for every bound node. Failed resets keep their original conflict state.
- Regression-test name: `ComposableCameraSystem.Editor.LiveEdit.AssetUndo` now
  covers Undo/Redo visible defaults and a new trial after Undo/Reset. Added, not
  compiled/run; requires IDE compile and editor execution.
- Avoid next time: treat Reset as a new source baseline, not just clearing dirty
  flags; test continued editing after Undo, not merely one Undo/Redo pair.
- Possible conflicts: concurrent source edits and runtime identity loss. Apply's
  conflict checks still reject unreset pending edits.

## 2026-10-02 - Native pin defaults diverged from authored defaults

- Symptom: an object picker could show an old or empty object after Apply;
  reconstructed Undo/Redo pins could retain defaults from another transaction.
- Trigger / repro: expose HitchcockZoom.FOVDeltaCurve as a pin, author curve A,
  trial curve B, Apply, Undo, Redo, and rebuild the graph. Compare template,
  RuntimePinOverrides and the native picker after each operation.
- Why / root cause: default allocation/setters updated only DefaultValue, while
  native object/text widgets use DefaultObject / DefaultTextValue. Reconstruction
  moved all old persistent defaults over freshly allocated restored defaults.
- History / blast radius: audited all SetPinDefaultOverride and camera-node
  ReconstructPins consumers, native K2 default decoding, copy/paste and Undo.
- Touched files: ComposableCameraNodeGraphNode.h/.cpp,
  ComposableCameraLiveEditTests.cpp, EditorDesignDoc.md, TechDoc.md, BugLog.md.
- Fix: decode all visible default fields from the same authored string. Normal
  reconstruction preserves its existing defaults; PostEditUndo reconstructs
  restored authoring defaults while preserving matching links.
- Regression-test names:
  `ComposableCameraSystem.Editor.LiveEdit.ObjectPinDefaultsUndoRoundTrip`,
  `ComposableCameraSystem.Editor.LiveEdit.AssetUndo`. Added, not compiled/run;
  IDE/editor verification must include existing graph sync/copy-paste tests.
- Avoid next time: compare visible widget values with durable values, including
  object pointers and Undo/reopen; a string alone is not every pin's UI value.
- Possible conflicts: normal Details, exposed pins and copy/paste. Their default
  reconstruction semantics remain unchanged; only Undo opts out of old defaults.

## 2026-10-02 - Applying schema changes could silently remove existing drivers

- Symptom: replacing/removing an interpolator could remove a wired or exposed
  compound pin; undriven removed pins could retain obsolete override records.
- Trigger / repro: wire or expose PivotDamping.UpwardInterpolator.Speed, trial
  UpwardInterpolator=None, Apply. Repeat with the graph closed. With no driver,
  author Speed=4, remove the root, Apply, then Undo.
- Why / root cause: Apply validated values but not prospective declarations;
  ReconstructPins legitimately breaks unmatched pins, and old override records
  were not pruned when their root no longer declared those compound pins.
- History / blast radius: reviewed source conflict/identity and compound-refresh
  history; audited graph pin reconstruction, exposure and durable camera/variable
  connections in both open-graph and closed-asset paths.
- Touched files: ComposableCameraLiveEditSession.cpp,
  ComposableCameraNodeGraphNode.h/.cpp, ComposableCameraLiveEditTests.cpp,
  DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md, BugLog.md.
- Fix: reject the entire Apply before mutation when candidate name, direction,
  complete pin type or visibility would lose an existing wire/exposure. Permit
  undriven changes and transactionally prune only obsolete compound overrides
  beneath the edited root through a graph-node accessor.
- Regression-test names:
  `ComposableCameraSystem.Editor.LiveEdit.RejectsDrivenPinSchemaLoss`,
  `ComposableCameraSystem.Editor.LiveEdit.PreservesAuthoringGraphAndDrivers`.
  Added, not compiled/run; compile in IDE and execute in editor.
- Avoid next time: preflight schema consequences before invoking a reconstruct
  that intentionally drops unmatched pins. Test closed assets and subsequent Undo.
- Possible conflicts: variable Get/Set wires, exposed parameters, instanced
  configuration and Pin-as-Pin overrides. Default edits with unchanged pin shapes
  remain allowed; runtime trial schema changes remain allowed.

## 2026-10-02 - Undo graph notifications could resynchronize intermediate pins

- Symptom: an open toolkit's node PostEditUndo notifications could synchronize
  a partly reconstructed graph back into the asset while a transaction restored it.
- Trigger / repro: open an asset with camera wires, a variable getter, an exposed
  input and a BeginPlay chain; Apply two node defaults, Undo and Redo. Attach the
  toolkit-equivalent graph-change -> SyncToTypeAsset handler in automation.
- Why / root cause: SyncToTypeAsset guarded rebuild/sync reentry but not
  GIsTransacting; graph-node Undo callbacks notify separately as pins reconstruct.
- History / blast radius: reviewed graph rebuild reentry and Details coalescing
  history; inspected UE5.6 transaction restoration and all graph-sync entry points.
- Touched files: ComposableCameraNodeGraph.cpp, ComposableCameraLiveEditSession.cpp,
  ComposableCameraLiveEditTests.cpp, DesignDoc.md, EditorDesignDoc.md, TechDoc.md,
  ExecutionFlowExamples.md, BugLog.md.
- Fix: SyncToTypeAsset refuses Undo/Redo-time callbacks; the transaction restores
  both asset and graph. Apply's final notification holds its sync guard to avoid
  a second toolkit sync. Ordinary editing transactions still synchronize normally.
- Regression-test name:
  `ComposableCameraSystem.Editor.LiveEdit.PreservesAuthoringGraphAndDrivers`.
  Compares unchanged durable fields, camera/variable wires, template identity,
  GUID/layout, caller slots and BeginPlay state across Apply/Undo/Redo/rebuild.
  Added, not compiled/run; existing graph-sync suite also needs editor execution.
- Avoid next time: test transactions with production notification callbacks;
  testing a graph without its owning toolkit misses synchronous reentry.
- Possible conflicts: graph Undo/Redo and Details rebuild scopes. Only transaction
  restoration is gated; ordinary editing and subsequent explicit sync remain valid.

## 2026-10-02 - Live-edit curve test used an obsolete field type

- Symptom: the new curve round-trip test assigned/compared UCurveFloat* directly
  to CameraOffset.ForwardOffsetDeltaByPitchCurve, which is FRuntimeFloatCurve.
- Compiler evidence: the local UnrealBuildTool Log.txt completed at 13:54:44
  for UE5_6.uproject / UE_5.6. Its complete error list contains C2679 at old
  test line 517 (assignment), C2678 at old line 520 (comparison), and cascading
  C2661 at line 520 (TestTrue). Current test source was modified afterward and
  contains the corrected ExternalCurve / EditorCurveData operations; a fresh
  IDE compile is still required to verify the fix and newer review changes.
- Trigger / repro: full IDE compile with WITH_DEV_AUTOMATION_TESTS enabled.
- Why / root cause: test assumed the old pointer field instead of reading the
  current node declaration; FRuntimeFloatCurve has separate ExternalCurve and
  EditorCurveData members.
- History / blast radius: checked current CameraOffset declaration and all new
  curve test consumers; runtime curve evaluation needs no change.
- Touched files: ComposableCameraLiveEditTests.cpp and BugLog.md.
- Fix: edit/assert ExternalCurve and an inline key through EditorCurveData.
- Regression-test name:
  `ComposableCameraSystem.Editor.LiveEdit.NestedObjectsArraysAndCurveRoundTrip`.
  Corrected, not compiled/run; full IDE compile and editor execution required.
- Avoid next time: verify tests against current declarations before handoff;
  exercise both inline and external data for native curve structs.
- Possible conflicts: none to runtime curve behavior; change is test-only.

## 2026-10-02 - Live Editing action labels were not centered

- Symptom: Apply to Asset and Reset Trial text sits left of each button's center;
  the shorter Reset label makes the offset more obvious.
- Trigger / repro: open the global Edit Window's Live Editing page and compare
  the labels with their equal-size button bounds, including disabled Reset Trial.
- Why / root cause: equal-size SBox containers and parent-slot VAlign centered
  the button widgets only. SButton defaults to HAlign_Fill / VAlign_Fill; its
  generated STextBlock retains left text justification across the wider content
  area. Explicit content alignment was omitted in the preceding UI layout change.
- History / blast radius: reviewed prior Shot Editor toolbar placement history;
  checked both action callsites and UE5.6 SButton/SBorder construction. The defect
  is local to Live Editing content layout, independent of trial/asset commands.
- Touched files: SComposableCameraLiveEditPanel.cpp, ComposableCameraLiveEditTests.cpp,
  EditorDesignDoc.md, TechDoc.md, BugLog.md.
- Fix: explicitly set both buttons' content HAlign and VAlign to Center while
  retaining their shared dimensions/padding and existing action delegates.
- Regression-test name:
  `ComposableCameraSystem.Editor.LiveEdit.ActionButtonTextAlignment`.
  Arranges the actual tagged buttons and checks text-content centers in enabled
  and disabled states at layout scales 1.0 and 1.5. Added, not compiled/run.
  Compile in Rider/Visual Studio and run automation in Unreal Editor; reopen the
  window to inspect the new construction settings and font appearance.
- Avoid next time: verify content geometry as well as outer button dimensions;
  shorter labels expose Fill/left alignment that longer labels can conceal.
- Possible conflicts: none to camera evaluation, trial state, Apply/Reset, or Undo.
