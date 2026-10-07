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

## 2026-09-29 - Bulk Patch expiration depends on caller retaining its handle

- Status: fixed in source on 2026-10-05; see the follow-up entry below.
  Regression and runtime fix await IDE compilation and editor execution.
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
- Initial proposed fix (applied on 2026-10-05): share instance-level expiration logic between
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

## 2026-10-03 - Mesh Profile Context regression test called a nonexistent PCM API

- Symptom: C2039 for PushCameraContext at MeshProfileEffectsTests.cpp lines 134
  and 201 blocks UE5_6Editor compilation.
- Trigger / repro: compile the Mesh Profile changes in Rider/Visual Studio with
  WITH_DEV_AUTOMATION_TESTS enabled.
- Why / root cause: test setup assumed a PCM push API without checking the
  current declaration. Named activation owns the public path into EnsureContext.
- History / blast radius: reviewed context position/lifecycle and obsolete test
  API entries in this log; searched all PushCameraContext callers and inspected
  ActivateNewCamera overloads, named activation, and PopCameraContext.
- Touched files: ComposableCameraMeshProfileEffectsTests.cpp, TechDoc.md, BugLog.md.
- Fix: create/switch test Contexts through ActivateNewCamera(..., ContextName)
  and assert active names and restoration after pop. No PCM API added.
- Regression-test name:
  `ComposableCameraSystem.MeshCamera.ExclusiveProfileDispatchAndCleanup`.
  Updated; not compiled/run. Compile UE5_6Editor in the IDE and run the test in
  the editor, including original-manager Patch cleanup after a real Context switch.
- Avoid next time: verify test setup methods against current declarations and
  exercise the production activation path instead of inventing a push helper.
- Possible conflicts: test setup only; Context registration is scoped/restored
  with TGuardValue. Runtime Context behavior and Blueprint APIs are unchanged.

## 2026-10-03 - Mesh Action object picker mixed TObjectPtr and raw class operands

- Symptom: C2445 at ParameterTableRowCustomization.cpp line 589 prevents the
  editor module from compiling.
- Trigger / repro: compile the Action Object parameter asset-picker branch with
  UE5.6's FObjectPropertyBase::PropertyClass declaration.
- Why / root cause: PropertyClass is TObjectPtr<UClass>; the conditional's other
  operand is UClass*. Bidirectional conversions make the common type ambiguous.
- History / blast radius: reviewed prior TObjectPtr test/type mismatches; audited
  every PropertyClass conditional and the shared customization's callers. The
  failing branch is specific to Mesh Action Object inputs.
- Touched files: ComposableCameraParameterTableRowCustomization.cpp,
  ComposableCameraTestObjects.h, ComposableCameraMeshProfileCustomizationTests.cpp,
  TechDoc.md, BugLog.md.
- Fix: use PropertyClass.Get() so both conditional operands are UClass*.
- Regression-test name:
  `ComposableCameraSystem.Editor.MeshCamera.ActionObjectParameterPicker`.
  Added a reflected Object input to the existing Action fixture and check that
  actual generated Details contains SObjectPropertyEntryBox without creating
  an override. Not compiled/run; close the editor, compile in Rider/Visual Studio,
  then run editor automation and inspect an Action Profile with an Object input.
- Avoid next time: normalize pointer wrappers at native/Slate argument boundaries;
  include Actor, Object and Delegate inputs in the generated-parameter UI tests.
- Possible conflicts: fixture gains an exposable Object property; existing tests
  target fields by name and do not depend on the fixture's exposed-property count.
  Runtime camera evaluation and parameter serialization are unchanged.

## 2026-10-04 - Mesh Profile Type edits left the previous family in Details

- Symptom: selecting Modifier still displays Camera assets/parameters; the
  legacy mixed-Profile confirmation row also remains after Type acknowledges it.
- Trigger / repro: open a Camera Mesh Profile, change Type to Modifier, Action,
  or Patch in the same Details panel without reopening the asset. For a legacy
  mixed Profile, change Type or click its confirmation button.
- Why / root cause: both callbacks used PropertyUtilities.RequestRefresh.
  UE5.6's real Details view can redraw the tree without rebuilding the class
  customization, which selects its family when the layout is constructed.
  The test row generator rebuilds for either refresh request, masking the issue.
- History / blast radius: reviewed prior Mesh Profile duplication and Context
  ownership entries, Type migration, the shared parameter wrapper, and both
  PropertyEditor implementations. Runtime dispatch already reads the new Type;
  this defect affects the authoring panel and migration feedback.
- Touched files: ComposableCameraMeshProfileCustomization.cpp,
  ComposableCameraMeshProfileCustomizationTests.cpp, EditorDesignDoc.md,
  TechDoc.md, BugLog.md.
- Fix: request a deferred full rebuild with RequestForceRefresh for Type edits
  and legacy confirmation. Inactive serialized configuration remains intact.
- Regression-test name:
  `ComposableCameraSystem.Editor.MeshCamera.ProfileTypeRefresh`.
  Edits one real Details view through Modifier -> Action -> Patch -> Camera Type,
  waits for editor ticks, and checks exclusive asset fields and hidden Context.
  Added, not compiled/run. Compile UE5_6Editor in Rider/Visual Studio, run editor
  automation, then repeat switching and legacy confirmation in an asset panel.
- Avoid next time: test changes on the actual long-lived view; use deferred full
  rebuilds whenever a selector changes the set of authored widgets.
- Possible conflicts: layout rebuild releases obsolete widgets/delegates on the
  next tick. No changes to runtime ownership, parameter schemas, or serialization.

## 2026-10-04 - Flattened Camera fields leaked a second Activation section

- Symptom: Camera Type displays both Activation Params and Activation. Context
  Name, Is Transient and Life Time also leak into other Profile families.
- Trigger / repro: open any Camera Mesh Profile. Expand advanced settings and
  compare both Activation sections; select another Type and inspect Camera rows.
- Why / root cause: Camera has ShowOnlyInnerProperties, so PropertyEditor places
  its children into default category rows before class customization. Hiding the
  parent alone does not hide those independent rows. Explicitly adding selected
  fields suppresses only those fields, leaving omitted ones in the default layout.
- History / blast radius: reviewed the July 20 Camera-parent duplication fix and
  current Context/lifetime ownership policy; inspected default struct flattening,
  HideProperty and custom AddProperty behavior. DataTable uses the same row schema
  but does not use this Profile class customization.
- Touched files: ComposableCameraMeshProfileCustomization.cpp,
  ComposableCameraMeshProfileCustomizationTests.cpp, EditorDesignDoc.md,
  TechDoc.md, BugLog.md.
- Fix: hide config parents plus immediate struct children, then add only the
  selected family. Camera renders one custom Activation group with its four
  supported fields. Do not recursively hide Modifier elements or transform fields.
- Regression-test name:
  `ComposableCameraSystem.Editor.MeshCamera.ProfileDetailsLayout`.
  Extended to reject the default Camera ActivationParams row and raw Action
  bindings, require one Camera Activation group, and retain existing family,
  Context/lifetime, parameter and InitialTransform checks. Not compiled/run;
  compile in the IDE, run editor automation, and inspect all four Type panels.
- Avoid next time: when suppressing a flattened struct, hide its independent
  child rows as well; assert omitted fields and group counts in layout tests.
- Possible conflicts: DataTable row layout, shared schemas, Modifier array
  expansion, activation semantics and serialized data remain unchanged.

## 2026-10-04 - Mesh Brush release could leave a captured stroke active

- Symptom: a Brush drag may remain active after release when Alt is held, or
  after viewport focus is lost; subsequent hover movement can continue painting.
- Trigger / repro: begin a left-mouse Brush stroke, press Alt before releasing
  left mouse, then move without buttons. Also test focus loss during a drag.
- Why / root cause: release cleanup was inside the same !IsAltPressed condition
  used to begin painting. The mode had no focus-loss cleanup for captured state.
- History / blast radius: inspected brush spacing/erase and prior mode close
  lifecycle entries. New Shape drag input shares the same capture lifecycle;
  modifier state should gate beginning an operation, not finishing it.
- Touched files: ComposableCameraMeshLayerEdMode.h/.cpp,
  ComposableCameraMeshLayerShapeTests.cpp, EditorDesignDoc.md, TechDoc.md,
  ExecutionFlowExamples.md, BugLog.md.
- Fix: release active Brush input before checking modifier keys; LostFocus and
  explicit draft cancellation clear captured state. Already committed brush
  triangles remain in the working document.
- Regression-test name:
  `ComposableCameraSystem.Editor.MeshCamera.ShapeInteraction`.
  Checks release independently of viewport/modifier access and focus-loss draft
  cleanup. Added, not compiled/run. IDE compile + editor automation required;
  manually repeat the Alt-release and focus-loss strokes in a Level viewport.
- Avoid next time: process captured-input teardown before begin-operation guards;
  cover release, focus loss and tool teardown for every new drag tool.
- Possible conflicts: Alt navigation keeps its ordinary begin behavior; draft
  cancellation does not discard already authored triangles or affect runtime.

## 2026-10-04 - Active Shape draft could hide validation feedback

- Symptom: a failed Polygon confirmation can show only live vertex count,
  hiding the reason that the draft could not be committed.
- Trigger / repro: while a Polygon draft remains active, confirm a crossing or
  zero-area outline, or exceed the shape density/vertex limit.
- Why / root cause: the initial Shape status implementation selected either
  live measurements or feedback. Invalid Polygon drafts intentionally remain
  active for correction, so that choice hid their validation feedback.
- History / blast radius: checked all new Shape result branches and status
  consumers. Rectangle/Circle clear their draft after release; Polygon retains
  its points. Both paths need the same visible result messages.
- Touched files: ComposableCameraMeshLayerEdMode.cpp,
  ComposableCameraMeshLayerShapeTests.cpp, EditorDesignDoc.md, TechDoc.md, BugLog.md.
- Fix: render measurements and feedback as separate status fields, and report
  the Polygon vertex cap explicitly.
- Regression-test name:
  `ComposableCameraSystem.Editor.MeshCamera.ShapeInteraction`.
  Checks the actual formatted status while a draft and feedback coexist.
  Added, not compiled/run. Compile in the IDE and inspect a rejected Polygon
  in the editor while correcting points with Backspace.
- Avoid next time: retained draft state and failure feedback must coexist;
  validation should never depend on whether a preview is still active.
- Possible conflicts: display-only feedback; no change to existing triangles,
  Layer GUIDs, runtime selection, bake or serialization.

## 2026-10-04 - Mesh authoring could not Undo or Redo

- Symptom: Ctrl+Z/Y cannot restore painted regions, completed Shapes or Layer edits.
- Trigger / repro: open Edit Mesh Camera Layers, draw a rectangle or drag a
  multi-stamp brush stroke, then press Ctrl+Z followed by Ctrl+Y. Edit Layer
  properties, delete/reorder a Layer, and repeat.
- Why / root cause: source was a native FEdMode member outside UObject transaction
  serialization; settings lacked RF_Transactional and mutations lacked scoped
  transactions. Layer Details pointed at array memory with a non-owning struct
  scope, so it could not record an owning document and could become stale on undo.
- History / blast radius: reviewed graph Undo intermediate-callback resync,
  stale reset baselines, mesh mode lifetime/capture and all source/bake consumers.
  Inspected UE5.6 PropertyEditor transaction ordering and FEditorUndoClient.
- Touched files: ComposableCameraMeshLayerToolSettings.h/.cpp,
  ComposableCameraMeshLayerEdMode.h/.cpp, ComposableCameraMeshLayerModeToolkit.h/.cpp,
  ComposableCameraMeshLayerShapeTests.cpp, DesignDoc.md, EditorDesignDoc.md,
  TechDoc.md, ExecutionFlowExamples.md, BugLog.md.
- Fix: reflected transactional source/revision on the settings UObject; scoped
  Layer/Shape edits and one transaction per whole stroke. Stable UObject Details
  proxy records its source owner before changes. Final undo callbacks rebuild
  caches/proxy; property callbacks reject GIsTransacting so PostEditUndo cannot
  reauthor intermediate state. Revision checkpoints restore saved/unsaved state. Cancelled
  and no-op strokes discard their transactions; shortcuts route to Unreal Undo.
- Regression-test name: `ComposableCameraSystem.Editor.MeshCamera.DocumentUndoRedo`.
  Covers drawing, actual multi-stamp erase, save checkpoint, Layer create/delete,
  restored geometry/masks/GUIDs and a real Details property handle. Added, not run.
  Compile in Rider/VS, run editor automation, and manually repeat shortcuts in
  viewport and toolkit (including after Save and after focus loss).
- Avoid next time: put authoritative mutable source in reflected transactional
  storage; test subsequent editing and Save checkpoints, not only one Undo pair.
- Possible conflicts: global editor transaction ordering, Details notifications,
  array relocation and mode teardown. No actor save transaction or graph sync is
  introduced; runtime sees only the last explicitly saved document.

## 2026-10-04 - Completed Shapes lost their editable source

- Symptom: drawing a Shape succeeds but clicking it cannot select or adjust it.
- Trigger / repro: commit Rectangle/Circle/Polygon, try to move/resize a corner
  or vertex, save, reopen the tool, and try again.
- Why / root cause: confirmation appended projected triangles and discarded the
  controls. Neither durable Shape identity nor triangle-to-Shape ownership existed.
- History / blast radius: audited authoring Reset/IsConsistent, storage copy/bake,
  brush append, orphan pruning, erase, visualization and legacy fixture consumers.
- Touched files: ComposableCameraMeshSurfaceTypes.h,
  ComposableCameraMeshLayerShapes.h/.cpp, ComposableCameraMeshLayerEdMode.h/.cpp,
  ComposableCameraMeshLayerToolSettings.h/.cpp, ComposableCameraMeshLayerModeToolkit.h/.cpp,
  ComposableCameraMeshLayerShapeTests.cpp and four design/tech/flow docs, BugLog.md.
- Fix: persist editor-only GUID/control/projection records and triangle ownership;
  Select ray-picks actual geometry, hit proxies identify controls, drag previews
  remain transient, validated replacement preserves identity, and numeric Details
  edits position/size/radius/vertices. Delete removes selected source and mesh.
- Regression-test name: `ComposableCameraSystem.Editor.MeshCamera.ShapeEditingAndErase`
  and `DocumentUndoRedo`. Covers picking, reflected save/reload, replacement,
  rectangle/circle/polygon controls, cancel and deletion. Added, not compiled/run.
  IDE compilation and viewport control picking/reopen verification remain required.
- Avoid next time: retain the source representation whenever later editing is
  expected; generated triangles cannot reconstruct original authored controls.
- Possible conflicts: editor source layout and legacy documents. Empty ownership
  arrays remain valid; legacy triangle-only regions support erase but cannot
  recover lost Shape controls. Cooked query layout remains unchanged.

## 2026-10-04 - Erase was hidden and removed whole triangles by centroid

- Symptom: Shape tools expose no Erase action; a small brush can miss coverage
  inside a large triangle or remove far more coverage than its footprint.
- Trigger / repro: draw a large rectangle with coarse sampling, erase a small
  circle inside it away from triangle centroids, then adjust that Shape.
- Why / root cause: bErase was visible only for Brush and deletion tested only
  each triangle centroid. There was no geometric subtraction or retained cut source.
- History / blast radius: reviewed existing component-seam projection, brush
  release cleanup, runtime nearest-surface queries and both mesh ownership arrays.
- Touched files: ComposableCameraMeshLayerShapes.h/.cpp,
  ComposableCameraMeshLayerToolSettings.h/.cpp, ComposableCameraMeshLayerEdMode.h/.cpp,
  ComposableCameraMeshLayerShapeTests.cpp, DesignDoc.md, EditorDesignDoc.md,
  TechDoc.md, ExecutionFlowExamples.md, BugLog.md.
- Fix: independent Erase tool under Drawing, preserving temporary Shift erase.
  Subtract a bounded 32-sided circular prism and interpolate surviving fragments.
  Preserve ownership/other Layers; retain document-local cuts on affected Shapes
  and replay them on rebuild. Identical retained cuts are idempotent.
- Regression-test name: `ComposableCameraSystem.Editor.MeshCamera.ShapeEditingAndErase`
  and `DocumentUndoRedo`. Covers a hole in two large triangles, unaffected
  neighboring coverage/other Layer, repeated cut, reopened mask replay and whole
  stroke Undo/Redo. Added, not compiled/run; IDE/editor execution required.
- Avoid next time: test erasure against triangle interiors/edges independent of
  tessellation density, and preserve subtraction when regenerating authored mesh.
- Possible conflicts: dense source fragment growth, document transforms,
  overlapping Layer coverage and later Shape edits. Cut records remain in document
  coordinates, projection depth isolates other surface heights, and cook only
  receives final clipped triangles.

## 2026-10-04 - Mesh toolkit used the wrong Details API and property header

- Symptom: UE5.6 compilation reports C2039 for
  `LayerDetailsView->RequestForceRefresh()` and C1083 for `IPropertyHandle.h`.
- Trigger / repro: compile ComposableCameraSystemEditor in Rider/Visual Studio
  after adding Shape selection Details and the DocumentUndoRedo regression.
- Why / root cause: the deferred method from IPropertyUtilities was called on
  IDetailsView; the interface type name was also assumed to be its header name.
  UE5.6 IDetailsView exports ForceRefresh, and IPropertyHandle is declared by
  PropertyHandle.h. These exact API/header contracts were not verified.
- History / blast radius: reviewed the 2026-07-15 Generic Modifier compile entry
  for the same nonexistent header, Profile deferred-refresh history, and all
  RequestForceRefresh consumers. Existing Profile customizations correctly call
  IPropertyUtilities and need no change. Selection edits can request refresh from
  property notifications, so simply making refresh synchronous would risk reentry.
- Touched files: ComposableCameraMeshLayerModeToolkit.h/.cpp,
  ComposableCameraMeshLayerShapeTests.cpp, EditorDesignDoc.md, TechDoc.md, BugLog.md.
- Fix: include PropertyHandle.h. Queue/coalesce toolkit requests on a one-shot
  FTSTicker callback and invoke the actual IDetailsView::ForceRefresh API there.
  Capture the toolkit weakly and remove the ticker on destruction.
- Regression-test name:
  `ComposableCameraSystem.Editor.MeshCamera.SelectionDetailsRefresh` checks actual
  deferred layout rebuilding, repeated-request coalescing and pending-close
  lifetime. `DocumentUndoRedo` compiles against the correct property header and
  exercises actual property handles. Added/updated, not compiled or run by Codex.
- Test blocker / manual verification: command-line builds and editor test runs
  are prohibited by project instructions. Recompile in Rider/VS, then run both
  editor tests and select/edit Shapes through Undo/Redo in the viewport.
- Avoid next time: check the exact receiver's exported API and actual header
  path in the target engine; search BugLog before reusing remembered names.
- Possible conflicts: property callback reentry, deferred toolkit teardown,
  Profile customizations and Undo notifications. Deferral is preserved; other
  PropertyUtilities consumers, authored geometry and runtime bake remain unchanged.

## 2026-10-04 - Selection Details regression used a nonexistent layout header

- Symptom: C1083 at ComposableCameraMeshLayerShapeTests.cpp(9), unable to open
  IDetailLayoutBuilder.h.
- Trigger / repro: rebuild the Editor module after correcting PropertyHandle.h
  and the toolkit Details refresh API; compilation reaches the next invalid include.
- Why / root cause: the new regression copied the interface name into an include
  filename. UE5.6 declares IDetailLayoutBuilder in DetailLayoutBuilder.h. The
  preceding correction audited the reported include rather than the complete list.
- History / blast radius: reviewed the 2026-07-15 and preceding 2026-10-04 header
  failures; searched every layout-builder include in the plugin and checked the
  engine's actual PropertyEditor/Public files. Existing customizations and Profile
  tests already use DetailLayoutBuilder.h correctly.
- Touched files: ComposableCameraMeshLayerShapeTests.cpp, TechDoc.md, BugLog.md.
- Fix: use DetailLayoutBuilder.h and check every quoted include in this test
  against the target plugin/UE5.6 source files.
- Regression-test name: `ComposableCameraSystem.Editor.MeshCamera.SelectionDetailsRefresh`
  compilation gate, followed by its existing actual Details-view regression.
- Test blocker / manual verification: a missing-header failure precedes automation
  execution; project instructions prohibit command-line compilation/tests. Header
  existence and diff checks run here; Rider/VS recompilation and editor automation
  must confirm the full translation unit.
- Avoid next time: verify all newly introduced header paths in one pass against
  the target engine; never infer filenames from interface names.
- Possible conflicts: include-only correction in editor automation; no runtime,
  reflection, authoring data or UI behavior changes.

## 2026-10-04 - Mesh drawing and editing tools shared one mixed options panel

- Symptom: Draw variants, Select and Erase were peers in one Drawing Tool enum;
  selected Shape controls appeared even when a different tool was active.
- Trigger / repro: open Mesh Camera Layers, inspect the Drawing Tool dropdown,
  select a completed Shape, then switch to Brush or Erase. Tool modes have no
  separate top-level controls and the Shape/delete panel remains visible.
- Why / root cause: toolkit rendered the complete viewport dispatch enum through
  default Details and combined Layer/Shape properties in one unfiltered proxy view.
- History / blast radius: reviewed the prior Drawing consolidation, retained
  Shape/Erase source, document Undo and deferred Details refresh entries. Audited
  every Tool dispatch consumer; its enum values and existing input behavior stay
  intact. Mode changes still cancel drafts and finish strokes, without dirtying source.
- Touched files: ComposableCameraMeshLayerToolSettings.h/.cpp,
  ComposableCameraMeshLayerModeToolkit.h/.cpp, ComposableCameraMeshLayerEdMode.h,
  ComposableCameraMeshLayerShapeTests.cpp, EditorDesignDoc.md, TechDoc.md,
  ExecutionFlowExamples.md, BugLog.md.
- Fix: parallel Draw/Select/Erase buttons below Layer properties. Draw contains
  only Brush/Rectangle/Circle/Polygon and remembers its previous variant. Filter
  tool options by mode; split Layer/Shape proxy views and show Shape editing only
  in Select. Deferred refresh updates all three views together.
- Regression-test name: `ComposableCameraSystem.Editor.MeshCamera.ToolPanels`
  covers actual Details filtering/widget visibility, draft cancellation and
  preservation of Shape identity/revision plus remembered Draw type.
  `SelectionDetailsRefresh` retains the deferred/coalesced refresh regression.
- Test blocker / manual verification: project instructions prohibit shell builds
  and editor test runs. Restart/rebuild in Rider/VS, run both tests in the editor,
  then switch all three buttons, draw/edit/erase and exercise Undo/Redo manually.
- Avoid next time: distinguish top-level interactions from creation variants;
  inspect visible fields against the active mode rather than the backing enum alone.
- Possible conflicts: draft/stroke lifetime on switching, nested property visibility,
  Undo callbacks and shared proxy ownership. UI modes are native/nontransactional;
  source storage, authored Shape GUIDs and runtime bake are unchanged.

## 2026-10-04 - Mesh numeric dragging rebuilt captured widgets and could block Undo

- Symptom: Draw/Select/Erase option sliders are difficult to drag; later Erase
  Undo/Redo appears unavailable even though the brush stroke has a scoped transaction.
- Trigger / repro: drag a tool numeric value across multiple editor ticks, then
  erase a region and try Ctrl+Z/Y. Also queue a Details refresh just before dragging.
- Why / root cause: ToolSettings dispatched OnToolSettingsChanged for every
  Interactive property notification. The mode cancelled input and refreshed all
  Details, replacing the captured numeric widget. UE5.6 SPropertyEditorNumeric
  owns transaction completion on release/final SetValue; losing that callback
  can leave the editor transaction active. UTransBuffer::CanUndo/CanRedo refuse
  active transactions, affecting later authoring actions across all tool modes.
- History / blast radius: reviewed deferred Details refresh, document transactions,
  retained Erase masks and mixed-tool layout entries; audited every property
  callback, input release, CancelInteraction, Begin/FinishStroke and geometry commit.
  Erase already begins a stroke and updates revision through PaintAtHover. The old
  regression called EraseBrushStamp directly and manually supplied revision changes,
  so it did not check the actual mutation/dirty-state path.
- Touched files: ComposableCameraMeshLayerToolSettings.cpp,
  ComposableCameraMeshLayerModeToolkit.h/.cpp, ComposableCameraMeshLayerShapeTests.cpp,
  DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md, BugLog.md.
- Fix: skip Interactive settings callbacks, preserve the final commit notification,
  and keep the refresh ticker pending while an editor transaction is active. This
  protects both new and already queued requests without ending unrelated transactions.
- Regression-test name: `ComposableCameraSystem.Editor.MeshCamera.ToolSliderTransactions`
  reproduces native numeric property-handle Begin/Interactive/final-commit behavior
  across editor ticks in all three modes; checks stable layout, one final rebuild
  and completed transactions. `DocumentUndoRedo` now uses PaintAtHover for multi-stamp
  Erase and checks Shape adjustment/deletion and Rectangle/Circle/Polygon source
  creation. Existing SelectionDetailsRefresh retains deferral/coalescing/close checks.
- Test blocker / manual verification: project instructions prohibit shell builds
  and test execution. Rebuild/restart through Rider/VS, run the three editor tests,
  then physically drag sliders, draw with Brush/all Shapes, move/resize Shapes,
  delete and erase through Undo/Redo. Collision-floor input requires an actual Level.
- Avoid next time: defer refresh until an interaction finishes, not merely until
  the next tick; test an already queued refresh and use the same mutation path as UI.
- Possible conflicts: global editor transaction state, Undo restoration and toolkit
  teardown. Pending work remains weakly owned and cancellable on close; source data,
  save checkpoints and the existing stroke transaction grouping remain intact.

## 2026-10-04 - Selected Shape exposed internal toggles and a stretched delete action

- Symptom: Shape Type/Position have checkboxes and Delete Selected Shape occupies
  the full panel width below the Shape fields.
- Trigger / repro: choose Select and click a retained Shape; inspect and toggle
  the checkboxes, then inspect the delete button's placement and width.
- Why / root cause: a simple EditCondition on hidden bHasShape automatically
  creates PropertyNode's legacy edit-condition toggle. The delete SButton lived
  in a default fill-aligned vertical slot after the Shape Details view.
- History / blast radius: reviewed stable proxy ownership and mode-specific
  filtering; audited all bHasShape consumers and selected Shape property metadata.
  Shape presence is derived from the selected GUID and must not be authored by users.
- Touched files: ComposableCameraMeshLayerToolSettings.h,
  ComposableCameraMeshLayerModeToolkit.cpp, ComposableCameraMeshLayerShapeTests.cpp,
  EditorDesignDoc.md, TechDoc.md, BugLog.md.
- Fix: HideEditConditionToggle on every selected Shape field. Move deletion into
  the first Select Options custom row with left-aligned content-sized width;
  enable it only with a valid selected Shape and capture the toolkit weakly.
- Regression-test name: `ComposableCameraSystem.Editor.MeshCamera.ToolPanels`
  checks engine-recognized toggle metadata and existing real property visibility;
  `DocumentUndoRedo` covers deletion with cut-source Undo/Redo.
- Test blocker / manual verification: rendered button alignment and physical
  checkbox absence require the editor UI; no shell editor/test runs are allowed.
  After a full rebuild, select/deselect Rectangle/Circle/Polygon, confirm no
  internal-state toggles, and check deletion above snapping in Select Options.
- Avoid next time: hide toggles for derived selection-state conditions; choose
  destructive-action placement and content width explicitly.
- Possible conflicts: Shape field visibility, disabled/read-only type display,
  selection clearing and deferred toolkit teardown. Conditions still control
  visibility; source selection and document transaction ownership are unchanged.

## 2026-10-04 - Brush and Erase rebuilt the whole preview on every stamp

- Symptom: dragging Brush or Erase becomes very slow on a populated mesh Layer
  document; erasing empty space can still stall mouse movement.
- Trigger / repro: author a large/dense region with multiple Layers, then drag
  Brush and Erase across a small area. Repeat Erase over already empty coverage.
- Why / root cause: PaintAtHover called RefreshDocumentState for each changed
  stamp, invalidating whole-document rasterization and rebuilding Shape controls
  plus redrawing every viewport. Erase allocated polygon buffers for all source
  triangles/clip planes and copied all retained mask histories. Failed stamps did
  not update spacing state, so empty-region movement retried at every mouse event.
- History / blast radius: reviewed resolved visualization/color de-duplication,
  cross-component floor projection, retained cuts, brush release, document Undo
  and numeric transaction refresh entries. Audited authoring/runtime visualization,
  preview/PIE mesh callers, all erase consumers and save/undo/cancel paths.
- Touched files: ComposableCameraMeshLayerEdMode.h/.cpp,
  ComposableCameraMeshLayerRendering.h/.cpp, ComposableCameraMeshLayerShapes.h/.cpp,
  ComposableCameraMeshLayerVisualizationTests.cpp,
  ComposableCameraMeshLayerShapeTests.cpp, DesignDoc.md, EditorDesignDoc.md,
  TechDoc.md, ExecutionFlowExamples.md, BugLog.md.
- Fix: retained sparse grid lookup and regional rasterization during strokes,
  with dense-cell index repair, stacked-surface/Layer resolution and sub-cell
  fallback preserved. Track complete changed-triangle bounds for erasure.
  Reject far triangles in brush coordinates; use bounded inline clipping scratch,
  reserved output and transfer retained Shape source only on real changes.
  Space all attempts, update dirty state immediately, invalidate only the active
  viewport per stamp, and refresh full preview/controls once on release. Grid
  growth is bounded by an early-rebuild threshold. Whole-stroke transactions remain.
- Regression-test names: `ComposableCameraSystem.Editor.MeshCamera.IncrementalVisualization`,
  `ComposableCameraSystem.Editor.MeshCamera.EraseBroadPhase`,
  `ComposableCameraSystem.Editor.MeshCamera.StrokeVisualizationRefresh`.
  They compare full/regional results (remote cells, stacked floors, Layer fallback,
  tiny fragments, grid growth and empty source), count expensive clipping work,
  and exercise actual Erase/release/no-op spacing. Existing DocumentUndoRedo and
  ResolvedVisualization remain required.
- Test blocker / manual verification: project rules prohibit shell compilation,
  editor launch and automation runs. Tests added, not compiled/run. Compile in
  Rider/VS with UE closed, reopen, run these editor tests, and compare Brush/Erase
  dragging on the same dense Level. Real collision traces and perceived viewport
  frame time require that IDE/editor smoke test; no measured speedup claimed.
- Avoid next time: cache dirtiness must distinguish local input from full document
  changes; exclude distant geometry before allocating clip work; test no-op input
  and preview equivalence rather than relying on wall-clock thresholds.
- Possible conflicts: sparse-cell swap indices, stacked surfaces, partial-grid
  fallbacks, cancellation/release redraw, preview memory and document growth.
  Cache is native/disposable; serialized layout, saved coverage and runtime query
  data are unchanged. No unrelated editor transaction is closed.

## 2026-10-04 - Mesh preview expanded outlines into square grid steps

- Symptom: painted Layer silhouettes have obvious square stair steps; small
  regions/erase holes can be displayed as whole cells or disappear from preview.
- Trigger / repro: paint a curved/diagonal boundary or erase a narrow hole, then
  inspect its filled overlay. Add distant coverage to coarsen the document-wide
  preview grid and compare the same nearby outline. Check Edit, Show and PIE.
- Why / root cause: the visualization sampled each triangle at cell centers and
  drew complete square quads for winning samples, including a centroid fallback
  for sub-cell triangles. Its spatial-index resolution became the silhouette
  resolution. Whole-cell Layer ownership also discarded lower coverage in the
  uncovered portion of an upper Layer's boundary cell.
- History / blast radius: reviewed prior alpha de-duplication, top-row priority,
  PIE ownership/teardown, retained Erase and regional preview performance entries.
  Audited native cache readers, Edit/Show rendering and PIE mesh export. Keeping
  the old squares at lower resolution would worsen the reported boundary defect.
- Touched files: ComposableCameraMeshLayerRendering.h/.cpp,
  ComposableCameraMeshLayerVisualizationTests.cpp, ComposableCameraMeshLayerShapes.cpp
  (changed-bounds comment), ComposableCameraMeshLayerModeToolkit.cpp (requested UI),
  DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md, BugLog.md.
- Fix: normalize triangle winding and clip coverage to actual cell polygons.
  Resolve geometric coverage/Layer priority by convex subtraction inside each
  height bucket. Cache boundary patches and fan-triangulate them for editor/PIE;
  retain full-cell coplanar quad/early-out paths and regional cache updates.
  Offset meshes along document Z to preserve coincident shared XY edges. Remove
  centroid expansion. Button layout also now uses centered fixed 125 x 24 Slate
  units, Delete aligned with Shape Grid Size, and a green Save footer action.
- Regression-test names: `ComposableCameraSystem.Editor.MeshCamera.VisualizationBoundary`
  covers sloped oblique edges, reversed winding, runtime preview, coarser grids
  and a small Erase hole. `ComposableCameraSystem.Editor.MeshCamera.VisualizationPartialOverlap`
  checks exact areas and same-cell point ownership, duplicate stamps, insertion
  order and disabling an upper row. IncrementalVisualization now compares actual
  patch coverage; ResolvedVisualization checks source area/uncovered corners.
- Test blocker / manual verification: project rules prohibit shell builds/editor
  test runs. Tests added/updated but not compiled/run here. Close UE, compile in
  Rider/VS, reopen and run these mesh visualization regressions plus existing
  stroke/Undo tests. Compare Brush/Erase dragging on the same dense Level and
  inspect diagonal/curved edges, tiny holes, stacked floors, Layer overlap, Show
  and PIE. Verify button alignment/centering/green style at the user's editor DPI.
- Avoid next time: use spatial cells only as a cache/index; test emitted mesh
  area and uncovered points, not just cell counts. A preview simplification must
  preserve outlines and within-cell ownership rather than grow coverage.
- Possible conflicts: boundary-patch memory/fragmentation, same-height tolerance,
  alpha partition, sparse-cell swap repair, and PIE mesh export. Cache remains
  editor-only/disposable; serialized source, bake, runtime queries and document
  transactions are unchanged. Polygon complexity still reflects authored circle
  segments and floor sampling; this fix removes grid-shaped preview expansion.

## 2026-10-04 - Preview rebuild reported uninitialized dirty-grid coordinates

- Symptom: MSVC C4701 for DirtyMin/DirtyMax in BuildResolvedVisualization,
  repeated for authoring, incremental authoring and runtime-preview instantiations.
- Trigger / repro: compile ComposableCameraMeshLayerRendering.cpp in the UE5.6
  Editor target after introducing regional preview updates; inspect the warnings
  at StartX/EndX and the equivalent Y-coordinate clamp expressions.
- Why / root cause: FIntPoint's default constructor deliberately does not
  initialize X/Y. The variables were assigned only in the regional branch; full
  rebuilds select triangle bounds instead. The later conditional expressions
  depend on the same flag, but MSVC did not establish that branch correlation.
  No actual uninitialized read was demonstrated on the full-rebuild path.
- History / blast radius: reviewed regional rebuild, exact boundary preview and
  sparse-grid index-repair entries; audited every dirty-bound read and all three
  BuildResolvedVisualization callers. No additional use escapes the regional guard.
- Touched files: ComposableCameraMeshLayerRendering.cpp,
  ComposableCameraMeshLayerVisualizationTests.cpp, TechDoc.md, BugLog.md.
- Fix: explicitly initialize both FIntPoint values to (0,0). Regional work still
  overwrites them before removal/clamping, while full rebuilds ignore them.
- Regression-test name: `ComposableCameraSystem.Editor.MeshCamera.IncrementalVisualization`
  adds invalid dirty-bound fallback on an existing cache with distant coverage
  and compares full patch/index results. Existing initial-build, growth-rebuild,
  regional paint/erase and empty-source cases retain branch coverage.
- Test blocker / manual verification: command-line builds/tests are prohibited.
  Recompile through Rider/VS and confirm C4701 disappears for all three template
  instantiations; run IncrementalVisualization in the editor. Test updated, not run.
- Avoid next time: explicitly initialize UE math locals whose default constructor
  leaves storage unspecified, even when correlated guards prevent inactive reads.
- Possible conflicts: initialized zeros must never truncate a full rebuild;
  the regression checks remote coverage. Preview partition, grid resolution,
  serialized data and Undo/Redo behavior remain unchanged.

## 2026-10-04 - Brush release repeated full preview work and Erase rebuilt remote geometry

- Symptom: Brush stalls after releasing the mouse; Erase is substantially slower
  on populated Layers, especially a small cut inside a large authored triangle.
- Trigger / repro: paint a long stroke on an existing dense document, then release.
  Erase a small region of a large Shape or overlapping brush source and release;
  compare input responsiveness with an empty document and repeat over empty space.
- Why / root cause: FinishStroke called RefreshDocumentState after successful
  stamps had already updated the preview, invalidating the full cache for the
  next Render and rebuilding unchanged Shape controls. Erase copied/re-emitted
  every unaffected source triangle into a new document and dirtied the complete
  bounds of cut source triangles. Exact preview subtraction also split disjoint
  polygons along unrelated supporting edge lines, growing patch fragmentation.
- History / blast radius: reviewed the earlier regional brush optimization,
  exact-outline preview, dirty-bound warning, retained masks, source Undo and
  PIE/alpha ownership entries. Full release regridding was originally intentional
  for sampled quads; clipped boundaries now retain their outline at any cache
  size. Audited FinishStroke/revert/focus/tool-switch/Undo/Save, every erase caller,
  Layer/Shape GUID ownership, optional legacy Shape IDs and preview mesh consumers.
- Touched files: ComposableCameraMeshLayerEdMode.cpp,
  ComposableCameraMeshLayerShapes.cpp, ComposableCameraMeshLayerRendering.cpp,
  ComposableCameraMeshLayerModeToolkit.cpp, ComposableCameraMeshLayerShapeTests.cpp,
  ComposableCameraMeshLayerVisualizationTests.cpp, DesignDoc.md, EditorDesignDoc.md,
  TechDoc.md, ExecutionFlowExamples.md, BugLog.md.
- Fix: successful release retains current coverage/control caches and closes
  its transaction before redrawing viewports. Revert/Undo and structural edits
  still invalidate source-dependent caches. Erase scans backward, swap-removes
  only cut triangles in parallel index/ownership arrays, appends surviving pieces
  and preserves other vertices/Shape buffers. Dirty bounds follow removed
  polygons; remote union coverage is retained despite source retessellation.
  Convex separation checks avoid clipping/fragmenting disjoint or touching pairs.
  Save now uses a static rounded green button style at the requested fixed size.
- Regression-test names: `ComposableCameraSystem.Editor.MeshCamera.StrokeVisualizationRefresh`
  drives actual mouse release, retains the cache, closes the transaction and
  checks a subsequent cancelled stroke. EraseBroadPhase checks reserved-buffer
  reuse, original vertices, cut-sized bounds, ownership and no-op source stability.
  `ComposableCameraSystem.Editor.MeshCamera.EraseLocalVisualization` checks bounded
  cell work for a small cut in a large triangle, remote cache retention, lower-Layer
  reveal and optional legacy ownership. DisjointPreviewPatches checks exactly two
  unfragmented disjoint footprints despite repeats. IncrementalVisualization now
  compares Layer area and bidirectional interior coverage rather than tessellation
  identity, with 1.e-3 cm^2 area and 1.e-3 cm height tolerances for float source
  roundoff. Existing
  DocumentUndoRedo, ShapeEditingAndErase and visualization boundary/overlap tests
  remain required. Added/updated here, not compiled or run.
- Test blocker / manual verification: project instructions require Rider/VS builds
  and editor test runs; no command-line build/editor/test execution here. Compile,
  reopen the tool and run these mesh tests. On the same dense Level, compare Brush
  release, continued Erase, repeated empty erasing, cancellation and Undo/Redo.
  Verify small holes, Layer reveal, saved/reloaded source and Show/PIE preview.
  Check rounded Save styling at editor DPI. No measured speedup is claimed.
- Avoid next time: cache validity must follow actual coverage changes, not input
  release or source tessellation identity. Preserve untouched buffers; reject
  nonintersecting convex footprints before creating split fragments.
- Possible conflicts: unordered triangle swap-removal must move ownership with
  its index triplet; newly appended fragments must not be recut within the stamp.
  Shared vertices cannot be overwritten. Unreferenced source vertices remain in
  transient working data until Save's existing orphan cleanup compacts them
  (erase-to-empty clears them immediately), so long editing sessions can retain
  extra source capacity. Whole-stroke transactions, mask replay, saved data format,
  runtime query semantics, Layer order and PIE rendering are preserved.

## 2026-10-04 - Erase retessellation could change overlapping Shape selection

- Symptom: review of the in-place erase optimization found that Select could
  choose an older overlapping Shape after a nearby cut. Caught before handoff.
- Trigger / repro: create two overlapping Shapes in one Layer on the same slope,
  erase a small part of both, then click their untouched overlap. Add a newer
  Shape on a lower floor to check nearest-surface precedence separately.
- Why / root cause: picking broke equal-distance ties by reverse triangle order.
  Swap-removal and fragment append reorder triangles independently of retained
  Shape records. Comparing squared distances with a fixed epsilon also makes
  the same-surface tolerance depend on the ray origin, exposing float cut roundoff.
- History / blast radius: reviewed Shape editing/erase, retained masks and the
  preceding release/erase performance fix. Audited FindShapeOnRay consumers,
  Shape create/edit/delete record order, triangle ownership, legacy missing
  ownership and Undo restoration. Runtime spatial queries do not call this helper.
- Touched files: ComposableCameraMeshLayerShapes.cpp,
  ComposableCameraMeshLayerShapeTests.cpp, EditorDesignDoc.md, TechDoc.md, BugLog.md.
- Fix: nearest actual ray-hit distance wins. Same-surface hits within 0.001
  document units prefer the later retained Shape record, independent of triangle
  positions. Missing records retain the existing reverse-traversal fallback.
- Regression-test name: `ComposableCameraSystem.Editor.MeshCamera.ErasePickingOrder`
  checks later-Shape selection before/after a cut, an unpickable erased hole,
  preserved masks/ownership and a newer lower floor losing to the nearer surface.
  Added here, not compiled or run.
- Test blocker / manual verification: project requires Rider/VS compilation and
  editor automation runs. Compile and run the test, then pick overlapping Shapes
  before/after Erase and Undo/Redo on a sloped surface.
- Avoid next time: selection priority must follow retained identity/order, never
  mutable tessellation order. Distance tolerances need explicit linear units.
- Possible conflicts: separate surfaces farther than the tolerance still select
  the nearest hit. Shape edit/recreate and Undo preserve their existing record
  ordering; Layer filtering, erase masks and source serialization stay unchanged.

## 2026-10-04 - Shape confirmation blocked before any filled region appeared

- Symptom: Rectangle/Circle release and Polygon confirmation took several seconds
  before filled coverage appeared. The footer also repeated Saved/Unsaved state.
- Trigger / repro: draw a large densely sampled region on a populated document;
  release Rectangle/Circle or close a Polygon. Observe delayed fill and a stalled
  viewport; confirm that smaller spacing increases work.
- Why / root cause: CommitShape synchronously called CommitEditedShape, performing
  all vertex/midpoint/centroid collision queries in the input callback. Successful
  source mutation invalidated the entire resolved cache, so the next Render did
  more blocking work. GetStatusText always prepended document state.
- History / blast radius: checked projection limits/gaps, draft validation,
  retained Shape controls, exact preview coverage and the previous Brush/Erase
  release fixes. Audited every projection consumer, creation/edit transactions,
  tool/focus/Layer callbacks, Undo/Redo, Save/close, World lifetime and render paths.
  Existing control/Details edits keep their transaction semantics; runtime and
  serialized fields remain unchanged.
- Touched files: ComposableCameraMeshLayerShapes.h/.cpp,
  ComposableCameraMeshLayerEdMode.h/.cpp, ComposableCameraMeshLayerRendering.h/.cpp,
  ComposableCameraMeshLayerModeToolkit.cpp, ComposableCameraMeshLayerShapeTests.cpp,
  DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md, BugLog.md.
- Fix: validate/prepare the bounded outline and queue a small captured-plane fill
  immediately. Resume new-region projection with cached leaf/sample state, at most
  256 new queries / a soft 4 ms per editor frame. Exact coverage runs on worker-owned
  plain snapshots; no World queries or UObject/mode access occur off-thread.
  The editor consumes only ready results and commits source plus ready coverage
  together in one creation transaction. Existing source edits cause coverage
  rebasing with retained projected geometry; stale results never overwrite edits.
  Queued regions preserve order and survive tool/focus changes. Explicit cancel,
  Undo, Layer structural changes, lost World and exit discard pending work without
  joining workers. Save/Brush/Erase wait for completion. Document state moves to
  tooltips; empty footer text collapses without leading newlines.
- Regression-test names: `ComposableCameraSystem.Editor.MeshCamera.ShapeProjectionBudget`
  checks per-resume query caps, partially paused leaves, sample-cache reuse,
  projected heights, floor gaps and density rejection. `ComposableCameraSystem.Editor.MeshCamera.ShapeCreationPreview`
  checks immediate filled outlines for Rectangle/Circle/concave Polygon, consecutive
  queueing, focus/options/cancellation, lost World, Save/mutation gating, footer
  text and atomic completion Undo/Redo. Existing ShapeProjectionAndLimits,
  ShapeInteraction, ShapeEditingAndErase, DocumentUndoRedo and visualization tests
  remain required. Added/updated here, not compiled or run.
- Test blocker / manual verification: project requires Rider/VS compilation and
  editor test runs. Header changes require a full editor restart/build. On an actual
  Level, confirm immediate provisional fill and responsive input for all three
  shapes, then exact terrain/gap/Layer coverage, consecutive type switches, Save
  after completion and creation Undo/Redo. While a worker runs, edit an existing
  Shape and verify rebasing preserves that edit; cancel/change Layer/exit and
  confirm late results never reappear. Actual mode/World collision/worker timing
  cannot be verified by the pure fixtures alone; no latency measurement is claimed.
- Avoid next time: collision and coverage work must not accumulate in release or
  Render. Retain immutable job settings, budget game-thread queries, resolve plain
  snapshots off-thread, and validate document identity before publishing results.
- Possible conflicts: initial fill follows the captured plane and can temporarily
  cover gaps/overlaps until exact results replace it; it never enters saved source.
  One collision query, snapshot copy and final transaction serialization can exceed
  the soft query budget. Cancelled workers may finish on their private snapshots;
  they have no callback or reference to the mode. Pending authoring events allocate
  buffers; steady camera evaluation and the authored sampling contract are unchanged.

## 2026-10-05 - Failed package Save could replace a Discard baseline

- Symptom: restoring mesh Layer edits from the storage actor after a cancelled or
  failed Save would restore unsaved edits instead of the previous saved document.
  Undo could return working source to its clean revision while actor data from
  the failed attempt remained applied.
- Trigger / repro: open Mesh Camera Layers, edit a Layer or paint/erase coverage,
  click Save, then cancel/fail package checkout or saving. Click Discard, including
  after undoing working edits back to the clean revision. Repeat before the first
  successful Save when the attempt creates a hidden storage actor.
- Why / root cause: Save calls SetAuthoringData before PromptForCheckoutAndSave.
  The old mode retained only a SavedRevision GUID, with no independent saved
  document payload and no failed-save actor tracking. Reading the actor therefore
  cannot identify the last successful save. Source revision alone cannot detect
  actor data already applied by failed IO.
- History / blast radius: reviewed DocumentUndoRedo, stroke cancellation/cache
  retention, queued Shape cancellation/lifetime, saved revision checkpoints and
  Shot Editor discard history. Audited initialization, Save/exit, final Undo
  callbacks, GC references, Layer/Shape proxy refresh, storage source/runtime bake
  and native UE5.6 transaction/EditorDestroyActor APIs. No global Undo reset or
  package dirty clearing is introduced.
- Touched files: ComposableCameraMeshLayerEdMode.h/.cpp,
  ComposableCameraMeshLayerModeToolkit.h/.cpp, ComposableCameraMeshLayerShapeTests.cpp,
  DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md, BugLog.md.
- Fix: add the requested same-sized rounded Discard action to Save's right. Keep
  an independent nontransactional transient checkpoint, GC-tracked through the
  mode and reflected Layer references. Initialize it from the normalized opening
  document and advance only on successful Save. Discard cancels active/pending
  work, restores full document/revision in one scoped transaction and refreshes
  caches/Details. A retained weak failed-save actor restores checkpoint source or
  is removed if that attempt created it; final Undo callbacks compare reflected
  actor source/Layer data to recompute rollback state. Button enablement stays
  constant-time, clean Discard disables, and tool preferences remain unchanged.
- Regression-test name: `ComposableCameraSystem.Editor.MeshCamera.DiscardWorkingDocument`.
  Covers initial/latest checkpoints, Layer identity/properties/asset references,
  projected triangles/ownership, Shape controls/retained cuts, Discard Undo/Redo,
  preferences, selection/feedback/cache refresh, pending/draft cancellation,
  unfinished actual Erase transaction cancellation, simulated existing-actor
  failed-Save rollback, clean-source Undo with dirty actor, and repeated Discard
  after Undo, including actor-only geometry rollback Undo/Redo with unchanged
  Layer properties. Added, not compiled or run.
- Test blocker / manual verification: compile only in Rider/VS; header changes
  require a full editor restart/build. Run the editor automation test. Actual
  checkout/package-save dialogs and new-actor external-package lifetime cannot be
  validated by the pure fixture. In a real Level, cancel/fail Save before and
  after the first successful Save, Discard, Undo/Redo and save again. Confirm the
  failed-attempt-created actor disappears, Undo restores it, and late cancelled
  Shape jobs never return. Confirm both buttons' dimensions at editor DPI.
- Avoid next time: successful persistence owns the checkpoint; applied actor
  memory is not proof of disk Save. Check both working source and failed-save
  actor state, retain asset references through GC, and test Undo across checkpoints.
- Possible conflicts: failed saves leave packages dirty; Discard intentionally
  preserves editor-wide package state and does not write disk. Partial package
  IO is not a disk rollback. Undo of Discard restores committed source and any
  affected actor data, not unfinished drafts/jobs. Runtime bake uses existing
  SetAuthoringData; graph synchronization and camera evaluation are unchanged.

## 2026-10-05 - Bulk Patch expiration skips instances after public handle GC

- Status: fixed; user-reported editor automation passed on 2026-10-05.
- Symptom: context-wide expiration leaves Manual-only Patches/evaluators alive
  when the caller no longer retains their handles.
- Trigger / repro: add two Manual Patches, retain only one handle, finish their
  enter phase, collect garbage, call `ExpireAll`, and evaluate beyond the exit.
  Previously only the retained-handle Patch exited; both must exit.
- Why it happens / root cause: manager-owned cleanup delegated through the
  instance's weak, caller-owned handle. A collected handle caused an early
  return even though the instance remained strongly registered.
- Touched files: `Source/ComposableCameraSystem/Private/Patches/ComposableCameraPatchManager.cpp`,
  `Source/ComposableCameraSystem/Private/Tests/ComposableCameraPatchTests.cpp`, `Docs/DesignDoc.md`,
  `Docs/TechDoc.md`, `Docs/BugLog.md`.
- Fix: shared `ExpirePatchInstance` handles the envelope state and duration
  override. `ExpireAll` calls it directly for manager-owned instances;
  `ExpirePatch` resolves its handle then uses the same helper. Exiting/Expired
  instances remain idempotent; `Apply` retains removal/evaluator teardown.
- Regression-test name: `ComposableCameraSystem.Patches.ExpireAllWithoutRetainedHandle`.
  Expanded coverage includes actual handle GC, duration override, repeated
  expiration without clock reset, partial-enter alpha, and zero-duration exit.
- Verification: source/consumer review and `git diff --check`; the user's
  2026-10-05 editor result shows the named regression succeeded. No shell build
  or automation has been run by Codex.
- Avoid next time: cleanup APIs traverse owned state directly; optional public
  access handles must not control lifetime. Test GC plus envelope idempotency.
- Possible conflicts: Blueprint context cleanup, individual handles and Mesh
  Patch exit share this helper; duration/condition expiration and Sequencer's
  separate overlay map remain unchanged. The original 2026-09-29 issue is the
  same root cause, now fixed rather than bypassed through stronger handles.

## 2026-10-05 - Storage Undo requires rebuilding the new native BVH cache

- Status: caught during source review of the new acceleration, guarded before
  handoff. User-reported editor regression passed on 2026-10-05.
- Symptom: restored regions could miss a ray if triangles changed position
  without changing their counts while native bounds still describe the edit.
- Trigger / repro: save a storage actor transaction, move every triangle
  outside the original ray while preserving vertex/index counts, then Undo.
  Query the original region; Redo must make that ray miss again.
- Why it happens / root cause: Unreal transactions restore reflected geometry,
  while the disposable native BVH does not participate in serialization.
  Cardinality checks cannot detect same-count spatial changes. Actor annotated
  Undo uses a separate virtual overload from plain `PostEditUndo`.
- Touched files: `Source/ComposableCameraSystem/Public/MeshCamera/ComposableCameraMeshSurfaceStorageActor.h`,
  `Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshSurfaceStorageActor.cpp`,
  `Source/ComposableCameraSystem/Private/Tests/ComposableCameraMeshSurfaceTests.cpp`,
  `Docs/DesignDoc.md`, `Docs/TechDoc.md`, `Docs/EditorDesignDoc.md`, `Docs/BugLog.md`.
- Fix: both actor Undo overloads retain `Super` restoration and rebuild query
  resources. A live storage also refreshes selected-family preload resources.
- Regression-test name:
  `System.Engine.ComposableCameraSystem.MeshCamera.SpatialIndexTransformedDocument`.
  Uses actual Undo/Redo with its own `UTransBuffer`, a rotated/scaled document,
  and same-count translation; preserves the user's original transaction buffer.
- Verification: source/UE5.6 API inspection and whitespace checks; the user's
  2026-10-05 editor result shows the transformed-document and BVH-pruning
  regressions succeeded. Existing Mesh authoring Undo/Discard tests remain a
  separate check. No shell build/automation was run by Codex.
- Avoid next time: every disposable derived cache needs explicit load, rebuild
  and transaction-restoration seams; count checks alone are insufficient.
- Possible conflicts: Mesh Save/Discard actor restoration and the 2026-10-04
  authoring Undo work. Layer/Shape GUIDs and authored fields remain unchanged;
  acceleration and preload caches never become transaction source data.

## 2026-10-05 - Preload regression assumed Storage BeginPlay in a GameMode-less world

- Status: corrected in source; IDE compilation and editor rerun pending.
- Symptom: `ProfilePreloadMembershipAndLifetime` reported zero shared preload
  entries instead of two at registration and after the first document unload;
  Camera entry also logged an unavailable CameraType asset after GC.
- Trigger / repro: run the named regression in editor automation. Its helper
  creates a Game world without a GameMode, calls World BeginPlay, spawns two
  storage actors, sets authoring data, and expects automatic registration.
- Why it happens / root cause: UE5.6 World BeginPlay routes Actor BeginPlay
  through GameMode StartPlay. With no GameMode, the actors never begin play,
  so storage registration and live-rebuild preload refresh do not run. Direct
  EnsureProfilePreload calls later mask the missing lifecycle for only the
  Action asset; the soft-referenced Camera can be collected. The Camera
  fixture also had no nodes, and membership-only assertions could pass even
  when no temporary Camera Context was created.
- History / blast radius: reviewed the prior Mesh test API, temporary Context
  ownership, soft-reference GC, Patch-handle GC, and native-cache Undo entries.
  Checked every FMeshProfileTestWorld consumer, storage BeginPlay/rebuild/end,
  registration/reconciliation, and the UE5.6 World/Actor dispatch implementations.
- Touched files: `Source/ComposableCameraSystem/Private/Tests/ComposableCameraMeshProfileEffectsTests.cpp`,
  `Docs/TechDoc.md`, `Docs/BugLog.md`.
- Fix: dispatch BeginPlay explicitly on the two storage actors; assert begun
  play, two registrations, and shared preloads before calling direct cache
  helpers. Check live rebuild deduplication. Add a valid Camera node, verify
  Camera and Action cache retention through GC, and assert a real temporary
  Context and camera switch on ready entry. Keep the shared test-world helper
  and production lifecycle unchanged.
- Regression-test name: `ComposableCameraSystem.MeshCamera.ProfilePreloadMembershipAndLifetime`.
  Expanded existing regression; the user's preceding run reproduced the defect.
  Corrected source has not yet been compiled or executed.
- Verification / blocker: project rules require IDE compilation and editor
  automation. Compile UE5_6Editor in Rider/Visual Studio, then rerun the named
  regression and ExclusiveProfileDispatchAndCleanup in the Automation panel.
- Avoid next time: assert fixture lifecycle and registered ownership before
  invoking private helpers. Validate actual effect construction, not only
  membership or restoration to an unchanged camera. Soft references alone do
  not retain transient test assets through GC.
- Possible conflicts: test-only correction; existing shared world consumers,
  production async requests, query acceleration, and Patch expiration are not
  changed. Null-transition cut warnings are separate expected test behavior.

## 2026-10-05 - Mesh Layer PIE visualization rebuilds debug mesh buffers every frame

- Status: fixed in source; IDE compilation, editor regression and frame-time
  comparison pending.
- Symptom: enabling Show Mesh Camera Layers causes sustained heavy frame-rate
  loss throughout PIE, rather than only a startup/toggle stall.
- Trigger / repro: load a Level with substantial painted Layer coverage, enable
  Show Mesh Camera Layers, enter PIE, and compare `stat unit` at a fixed camera
  position with Show off/on. The user confirmed that the drop persists for the
  entire PIE session.
- Why it happens / root cause: the preview submitted resolved cell meshes once
  to the persistent LineBatcher, but persistence covered only its CPU mesh
  arrays. UE5.6 FLineBatcherSceneProxy::GetDynamicMeshElements fills a new
  FDynamicMeshBuilder per mesh/view/frame, creating/uploading all vertices and
  indices repeatedly. Large resolved grids magnify render-thread work. Moving
  storage also rebuilt the resolved visualization instead of changing a mesh
  transform.
- History / blast radius: reviewed the missing-PIE-preview, hidden-storage
  rendering, scene-release crash, boundary fidelity, Layer partition and regional
  cache entries. Audited rendering/export callers, Show/Edit/module teardown,
  streaming/transform ticker, and UE5.6 LineBatchComponent/GeometryFramework
  rendering/lifetime implementations. Keep the pre-teardown cleanup guarantee
  from 2026-07-22; only its renderer backend is superseded.
- Touched files: `Source/ComposableCameraSystemEditor/ComposableCameraSystemEditor.Build.cs`,
  `Private/MeshCamera/ComposableCameraMeshLayerPIEPreview.h/.cpp`,
  `Private/MeshCamera/ComposableCameraMeshLayerRendering.h`,
  `Private/Utilities/ComposableCameraMeshLayerTool.cpp`,
  `Private/Tests/ComposableCameraMeshLayerVisualizationTests.cpp` within that
  editor module; `Docs/EditorDesignDoc.md`, `Docs/DesignDoc.md`, `Docs/TechDoc.md`,
  `Docs/ExecutionFlowExamples.md`, and `Docs/BugLog.md`.
- Fix: replace debug mesh batches with engine DynamicMesh components using
  persistent render buffers. A transient Actor in the source PIE Level owns
  components and color materials; global caches retain weak references only.
  Movement updates Actor transforms without mesh changes. Disable collision,
  cooking updates, navigation, ticking, shadows and ray tracing. Cache empty
  documents. Destroy preview Actors on PrePIEEnded/Show off/Edit/module unload
  or disappeared storage; Level ownership also handles streamed-Level removal.
  Preserve resolved geometry, color/alpha and independent external debug draws.
- Regression-test names:
  `ComposableCameraSystem.Editor.MeshCamera.PIEPreviewPersistentMeshes` and
  `ComposableCameraSystem.Editor.MeshCamera.PIEPreviewWorldRouting`.
  New regression creates actual PIE Actor/components, compares exported mesh
  counts/local positions/material colors, performs GC, applies 120 transform
  changes with zero mesh-change events, and checks independent unregistration
  plus preservation of unrelated debug geometry. Not compiled/executed yet.
- Verification / blocker: project rules prohibit shell builds/editor runs.
  Close UE, compile UE5_6Editor in Rider/Visual Studio after the new private
  GeometryFramework module dependency, restart, and run the named regressions
  plus VisualizationBoundary/VisualizationPartialOverlap. Automated ownership
  checks cannot measure rendered frame time or the full PrePIEEnded/FScene
  release sequence: compare fixed-position Show off/on `stat unit`, then
  stop/restart PIE, toggle during PIE, test SIE/multiple PIE worlds and streamed
  Level removal. Insights creation/build scopes should occur on discovery,
  not on each steady frame or document transform update.
- Avoid next time: inspect the render-thread implementation before treating a
  persistent debug draw as a cached render mesh. Separate geometry lifetime
  from Actor transforms and couple resource ownership to the Level/PIE lifecycle.
- Possible conflicts: do not recreate the 2026-07-22 ownerless strong-component
  scene-release bug; do not inherit the storage actor's hidden-in-game state.
  Persistent geometry still consumes GPU work and translucent fill rate; actual
  gains require measurement on the affected Level. Camera evaluation, query
  BVH, selected-family preloading, Patch lifecycle and serialized data are not
  changed by this renderer fix.

## 2026-10-05 - PIE Mesh Layer overlay lost patches behind the rendered floor

- Status: the disabled-depth workaround below is superseded by the following
  entry, "PIE Mesh Layer overlay draws over the character". The user demonstrated
  its foreground-occlusion regression; current PIE restores depth testing and
  fits disposable preview vertices instead.
- Symptom: some colored Layer patches disappear in PIE while editing displays
  them correctly; camera effects still work. User confirmed the defect is PIE-only.
- Trigger / repro: paint or load Layers on non-flat terrain, enable Show Mesh
  Layers, compare the same area in the editor and PIE. Collision-sampled triangles
  can sit slightly below the rendered terrain between their projected vertices.
- Why it happens / root cause: editor PDI uses GeomMaterial, whose serialized
  bDisableDepthTest is true. PIE used DebugMeshMaterial, which depth-tests. A
  1.5 cm document-Z offset cannot guarantee that approximate triangles clear the
  rendered floor. Native components also lack PDI's disabled backface culling;
  GeomMaterial is one-sided, so changing only its parent would lose the reverse view.
- History / blast radius: reviewed boundary/within-cell ownership, alpha
  de-duplication, PIE world routing, hidden storage, pre-teardown cleanup and
  persistent-mesh frame-time entries. Audited Edit/Show/PIE draw and mesh-export
  consumers. Read UE5.6 DynamicMesh rendering/duplicate-triangle behavior and
  inspected the installed engine materials' serialized bool properties.
- Touched files: ComposableCameraMeshLayerPIEPreview.cpp,
  ComposableCameraMeshLayerVisualizationTests.cpp, DesignDoc.md,
  EditorDesignDoc.md, TechDoc.md and BugLog.md.
- Fix: PIE reuses editor GeomMaterial and its Color parameter. One-sided
  materials get a disconnected reverse-winding mesh copy; two-sided materials
  get only the original mesh. Construction remains once per preview, with
  transform-only updates and existing Level ownership/PrePIEEnded cleanup.
- Regression-test name:
  `ComposableCameraSystem.Editor.MeshCamera.PIEPreviewPersistentMeshes` now
  asserts the actual material parent and depth policy, preserved front positions,
  reverse-winding positions/counts, color/alpha, no geometry writes on movement,
  GC ownership and independent cleanup. The old material fails the policy checks.
- Test blocker / manual verification: project instructions require Rider/VS
  compilation and Unreal Editor automation. Tests updated, not compiled or run
  here. Compile in the IDE, run the named regression plus PIEPreviewWorldRouting,
  then compare the reported terrain area in Edit/Show/PIE from above and below.
  Check Show off/on frame times and stop/restart PIE for resource cleanup.
- Avoid next time: renderer replacements must preserve material depth and
  backface policy as well as CPU geometry, color and resource lifetime.
- Possible conflicts: this is a debug overlay, so opaque scene geometry no
  longer occludes PIE Layers, matching editor behavior. One-sided-material
  backfaces increase cached vertices/indices once; they must not be rebuilt per
  frame or submitted twice with a two-sided material. Layer priority, floor
  sampling, authored/runtime data, queries and camera behavior are unchanged.

## 2026-10-05 - PIE Mesh Layer overlay draws over the character

- Status: fixed in source; IDE compilation, editor automation and visual
  verification pending.
- Follow-up: its whole-document publication gate is replaced by progressive
  chunk publication in the next entry. The depth-tested material and static
  floor-fitting policy remain in use.
- Symptom: colored Mesh Layer visualization appears to float over the player
  during PIE and tints foreground character geometry.
- Trigger / repro: enable Show Mesh Layers on a painted floor, enter PIE, and
  view the character in front of that floor. This followed the workaround for
  PIE-only missing patches recorded immediately above.
- Why it happens / root cause: GeomMaterial disables depth testing. Reusing its
  editor overlay policy bypassed the approximate floor geometry problem, but
  also drew through opaque foreground actors. The screenshot reflects missing
  occlusion, not an Actor transform moving above the character.
- History / blast radius: reviewed the previous missing-patch workaround,
  sustained LineBatch frame loss, source-Level ownership/PrePIEEnded cleanup,
  hidden storage, Layer boundaries, alpha partition and document-Z offset.
  Audited rendering exports, PIE factory/ticker/cleanup and automation consumers.
  Checked UE5.6 object-multi-query filtering and DynamicMesh/material APIs.
- Touched files:
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPIEPreview.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerRendering.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/Utilities/ComposableCameraMeshLayerTool.cpp
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerVisualizationTests.cpp
  - Docs/DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and BugLog.md
- Fix: restore depth-tested, two-sided DebugMeshMaterial and remove reverse-side
  mesh duplication. Before creating persistent meshes, fit disposable cached
  vertices to the nearest upward-facing static collision within +/-100 world cm
  along document Up, excluding Pawns. Preserve local XY, topology, color and
  source data; apply the existing 1.5 cm local-Z offset once. Misses retain their
  source positions. Shared source-XYZ samples preserve common boundaries and
  stacked floors. All PIE worlds/documents share 256 queries and a soft 4 ms
  per-ticker construction budget. Completed meshes reuse buffers without further
  floor queries. Show off/Edit/PIE teardown/unload cancel pending construction
  through the existing cache release path; Actor ownership remains Level-bound.
- Regression-test names:
  `ComposableCameraSystem.Editor.MeshCamera.PIEPreviewSurfaceProjection`,
  `ComposableCameraSystem.Editor.MeshCamera.PIEPreviewPersistentMeshes` and
  `ComposableCameraSystem.Editor.MeshCamera.PIEPreviewWorldRouting`.
  Projection tests cover budget deferral, shared vertices, retained XY/topology/
  colors, cancellation, Pawn rejection, nearest stacked floors, misses and scaled
  anchors. Persistent meshes assert depth testing and two-sided material without
  geometry duplication, GC ownership, transform-only reuse and cleanup.
- Test blocker / manual verification: project rules prohibit shell builds and
  editor automation. Tests added/updated, not compiled or executed here. Close
  UE, compile UE5_6Editor in Rider/Visual Studio and restart; run the named tests.
  Walk through the reported area: the character must occlude floor colors and
  previous missing patches must remain visible. Check stacked/sloped floors,
  Show off and stopping PIE during fitting, repeated PIE, and Show off/on
  `stat unit` at a fixed camera position after initial construction completes.
- Avoid next time: verify opaque foreground occlusion as well as buried-floor
  visibility when changing debug renderers. Fix display geometry instead of
  bypassing depth testing; bound initialization work and test cancellation.
- Possible conflicts / limits: fitting requires nearby static collision and
  cannot guarantee contact with render-only displacement or curvature between
  vertices. Unsupported vertices retain authored preview height; moving floors
  are not resampled after completion. Large documents appear after multiple
  fitting frames; the soft time budget cannot bound one physics query, initial
  clipping or final upload. Runtime queries, camera evaluation, serialized data,
  Layer ownership and editor PDI rendering remain unchanged.

## 2026-10-05 - Show Mesh Layers appears blank while PIE floor fitting is pending

- Status: fixed in source; IDE compilation and affected-Level timing/appearance
  verification pending. User had not waited 10-20 seconds, so indefinite
  non-progress was not established.
- Symptom: enable Show Mesh Layers before PIE; no Mesh visualization appears
  during the initial play period after adding budgeted floor fitting.
- Trigger / repro: load a large painted document, enable Show, enter PIE and
  inspect the initial frames while fitting still has unprocessed vertices.
  The old path creates no preview Actor until the complete document is fitted.
- Why it happens / root cause: a 256-query per-tick construction budget was
  combined with an all-or-nothing publication gate. Large caches therefore draw
  nothing throughout their entire fitting job. Additionally, the 4 ms deadline
  began before world discovery and cache building, allowing unrelated scanning
  work to spend the budget before fitting advances. Existing tests checked final
  mesh correctness and query deferral, but not visibility during construction.
- History / blast radius: reviewed PIE-only missing patches, disabled-depth
  character overdraw, persistent-buffer frame loss, ownership/PrePIEEnded cleanup,
  Layer partition and shared-boundary projection. Audited fitting, factory,
  ticker, transform updates, cancellation and all native-mesh consumers. Keep
  the depth-tested material; restoring disabled depth would regress occlusion.
- Touched files:
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPIEPreview.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/Utilities/ComposableCameraMeshLayerTool.cpp
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerVisualizationTests.cpp
  - Docs/DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and BugLog.md
- Fix: publish fitted triangle prefixes in an initial chunk of at most 64
  triangles, then chunks of at most 1024, with a global limit of two chunk
  publications per tick. Remap only ready vertex IDs, retain exact fitted
  positions/color/topology, and consume each
  triangle once. A Level-owned construction Actor keeps previous chunks visible
  while later ones fit. At completion, create the final one-component-per-Layer
  Actor and unregister/destroy construction components. Stable frames retain
  the prior persistent-buffer behavior. Measure only Advance time against the
  shared 4 ms fitting budget; discovery/clipping/uploads cannot starve it.
  Cancellation clears unpublished chunk cursors and destroys published chunks
  through existing cache release. Failed append preserves the old weak Actor
  reference so subsequent release can still unregister it.
- Regression-test name:
  `ComposableCameraSystem.Editor.MeshCamera.PIEPreviewProgressiveMeshes`.
  The fixture spans several chunks, advances with bounded queries, creates real
  registered PIE components from its first batch, before the whole document
  completes, and checks publication limits, exact triangle coverage/no duplicates,
  fitted positions,
  short tails, existing Actor/component reuse, one-component final consolidation
  and cancellation before completion. The previous implementation cannot publish
  those early components. Run PIEPreviewSurfaceProjection,
  PIEPreviewPersistentMeshes and PIEPreviewWorldRouting alongside it.
- Test blocker / manual verification: project rules prohibit shell compilation
  and editor automation. Static review only; the tests have not run here. Close
  UE, compile UE5_6Editor in Rider/Visual Studio and restart. Enable Show before
  PIE, then during PIE; inspect initial partial coverage and final coverage.
  Repeat with Show off or stop PIE during construction, then start another PIE.
  Confirm characters occlude the overlay and the previously missing floor areas
  remain visible. Compare steady-state `stat unit` after consolidation.
- Avoid next time: bounded initialization needs observable partial output as
  well as eventual completion. Test incomplete-job publication, and charge work
  budgets only to the work they govern. Temporary partitioning must consolidate
  so startup scheduling cannot multiply permanent render components/draw calls.
- Possible conflicts / limits: complete coverage still depends on cache size
  and collision-query cost; no fixed first-visible or completion time is claimed.
  Initial cache building, chunk upload and final consolidation are outside the
  soft projection-time budget. Chunks reuse final mesh/material policy, so
  character occlusion, Layer color partition, source data, camera effects and
  Scene cleanup remain unchanged. Static collision/render-geometry differences
  remain subject to the previous floor-fitting limitations.

## 2026-10-05 - PIE preview chunk remap fails to compile with C2373

- Symptom: UE5_6Editor compilation fails at
  ComposableCameraMeshLayerPIEPreview.cpp:114 with MSVC C2373, referencing the
  condition declaration at line 111.
- Trigger / repro: compile the progressive PIE preview change in Rider/Visual
  Studio; the editor-module unity translation unit includes TakeReadyMeshes.
- Why it happens / root cause: the if condition declares `const int32* Vertex`.
  Its scope includes the else body, where `const int32 Vertex` was declared
  again. Different branches do not give that condition variable separate scope.
- History / blast radius: reviewed the progressive-publication and prior
  floor-fitting entries; searched all TakeReadyMeshes callers and the remap
  declarations. The fix changes local names only, without API, geometry,
  publication, allocation or lifecycle changes.
- Touched files: ComposableCameraMeshLayerPIEPreview.cpp,
  ComposableCameraMeshLayerVisualizationTests.cpp, TechDoc.md and BugLog.md.
- Fix: use ExistingVertexIndex for the lookup pointer and NewVertexIndex for
  the newly inserted ID. Both branches retain their prior map/array operations.
- Regression-test name:
  `ComposableCameraSystem.Editor.MeshCamera.PIEPreviewSurfaceProjection`.
  Its first fixture now uses a two-triangle quad with shared IDs, plus a repeated
  unused position and a separate stacked height. It checks that insertion and
  reuse produce four output vertices and preserve the fan indices, while five
  distinct positions are sampled across the source fixtures.
- Verification / blocker: the compiler error itself is guarded by compiling
  the editor translation unit. Project rules prohibit shell compilation/tests;
  source and whitespace checks only here. Recompile UE5_6Editor in the IDE, then
  run PIEPreviewSurfaceProjection and PIEPreviewProgressiveMeshes in the editor.
- Avoid next time: name condition lookup pointers separately from insertion
  results, and remember that an if condition's scope covers its else branch.
- Possible conflicts: none expected; no header, reflection, module, data layout,
  material or serialized changes. This is a local compiler correction.

## 2026-10-05 - PIE floor fitting excludes non-static Blueprint floor collision

- Status: collision eligibility fixed in source; IDE compilation and affected
  Level verification pending. The reported missing patch's actual object type
  has not been inspected, so this mismatch is not yet proven to explain every
  remaining hole.
- Symptom: progressive Mesh Layer loading works, but a flat region still lacks
  color in PIE, even when approached. Editor visualization and camera effects
  work. User identifies the ground as a Static Mesh or Blueprint floor.
- Trigger / repro: author a Layer on a Visibility-blocking WorldDynamic or
  PhysicsBody floor, with cached preview height slightly below that floor;
  enable Show Mesh Layers and enter PIE. The old fitting query never samples
  that floor. Also place a nearer WorldStatic query volume that ignores or
  overlaps Visibility: the old object-only filter can select it as ground.
- Why it happens / root cause: authoring uses complex Visibility traces, while
  PIE queried AllStaticObjects (WorldStatic only). Collision object type and
  channel response are separate constraints. Missing eligible floors leave
  source vertices unchanged and can bury depth-tested overlays. Existing
  projection tests used only WorldStatic ground, missing this parity defect.
- History / blast radius: reviewed prior PIE-only holes, character overdraw,
  progressive publication, persistent rendering, chunk-remap compilation and
  pre-teardown ownership entries. The user's existing editor log reports all
  four PIEPreview tests passed at 2026-10-05 16:01 Hong Kong time before this
  fix. Audited authoring traces, projection consumers, component construction,
  cache reset/cancellation and Debugging console discovery. Keep depth testing
  and progressive persistent buffers to avoid reintroducing those regressions.
- Touched files:
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPIEPreview.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/Utilities/ComposableCameraMeshLayerTool.cpp
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerVisualizationTests.cpp
  - Docs/DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and BugLog.md
- Fix: query all object types, then choose the nearest upward-facing hit that
  blocks Visibility. Reject Ignore/Overlap, initial penetration and Pawns,
  including custom WorldStatic Pawns. Preserve the existing query budget,
  shared samples, local XY, one surface offset and source-height fallback.
  Add construction counters and the explicit editor command
  `CCS.Editor.MeshLayers.DumpPIEPreview`: log pending state, actual/expected
  triangles, queries, misses, accepted non-static floors and signed world-space
  height corrections. Count/log rejected native triangle insertions rather
  than silently losing their diagnostic evidence. No steady-state traces/logs.
- Regression-test name:
  `ComposableCameraSystem.Editor.MeshCamera.PIEPreviewSurfaceProjection`.
  Expanded physics fixture verifies WorldDynamic/PhysicsBody floors, closer
  Visibility Ignore/Overlap surfaces, Pawn rejection and diagnostic query/miss/
  non-static-hit counts. Existing stacked-floor, scale and topology assertions
  remain. The former query misses both non-static floors and selects the
  Visibility-ignoring surface; the new assertions expose both failures.
- Verification / blocker: project rules prohibit shell compilation/editor
  automation. Source review and whitespace checks only here. Close UE, build
  UE5_6Editor in Rider/Visual Studio, restart, and run the four PIEPreview tests.
  Enable Show, enter PIE and revisit the flat missing patch after loading.
  If it persists, execute `CCS.Editor.MeshLayers.DumpPIEPreview` in Output Log
  and return its `PIE Mesh Layers:` lines, including any triangle warning.
  `Pending=0` distinguishes completion; equal triangle counts distinguish
  submitted coverage from mesh insertion failure. Miss counts describe the
  whole document, not a particular location.
- Avoid next time: match authoring channel eligibility explicitly; object type
  alone cannot describe floor visibility. Test different object types and
  channel responses, and expose construction/submission counts before making
  another material or offset change.
- Possible conflicts / limits: all-object queries may collect more hits during
  initial fitting; shared query/time budgets still apply. Collision is sampled
  once, so render-only displacement, missing complex collision and moving floors
  remain limitations. This does not change runtime camera queries, serialized
  Layer data, authoring tools, material depth policy or Level-owned cleanup.

## 2026-10-05 - PIE preview diagnostic log fails to compile with C2064

- Symptom: ComposableCameraMeshLayerTool.cpp:83 UE_LOG fails with C2064,
  followed by C2131, C2971 and C2672 in FormatStringSan template validation.
- Trigger / repro: compile UE5_6Editor in Rider/Visual Studio after adding
  CCS.Editor.MeshLayers.DumpPIEPreview. The document-specific log is compiled
  even when no PIE preview cache exists at runtime.
- Why it happens / root cause: DumpPIEPreviewCaches uses a TMap range-for loop.
  Its Pair is a TPair, whose Key is a TWeakObjectPtr field. Pair.Key().Get()
  incorrectly calls the field as a zero-argument function. UE_LOG format
  validation also cannot form a constant result from the invalid argument;
  those template diagnostics are secondary to C2064.
- History / blast radius: reviewed the prior chunk-remap C2373 and floor
  eligibility/diagnostic entries. Searched all Pair.Key() occurrences in plugin
  source and audited the diagnostic callback, command registration and editor
  dispatch adapter. This was the only incorrect field call found.
- Touched files: ComposableCameraMeshLayerTool.cpp,
  ComposableCameraConsoleControlsTests.cpp, TechDoc.md and BugLog.md.
- Fix: access Pair.Key.Get(). Existing format, weak ownership, cache iteration,
  floor fitting and mesh submission remain unchanged.
- Regression-test name:
  ComposableCameraSystem.Editor.Debug.ConsoleControls.MeshLayerPreviewDump.
  Verifies real diagnostic registration, editor-action classification and
  dispatch without a selected game world. The reported compile error itself
  is guarded by compiling the callback's translation unit; a runtime test
  cannot execute an uncompilable implementation.
- Verification / blocker: project rules prohibit shell compilation and Unreal
  automation. Static checks only here. Recompile UE5_6Editor in the IDE and run
  the named test. In PIE with Show enabled, execute
  CCS.Editor.MeshLayers.DumpPIEPreview and verify per-document lines include
  object paths plus triangle/projection statistics. Return those lines if the
  original flat-floor hole persists.
- Avoid next time: distinguish range-for TPair fields from explicit TMap
  iterator accessors. For macro errors, inspect the first failing argument
  expression before altering includes, casts or format strings.
- Possible conflicts: none expected. No header, reflection, public API,
  serialized data, material or lifecycle changes; compiler correction only.

## 2026-10-05 - Large first PIE preview document starves a Level Instance

- Status: scheduling fixed in source; IDE compilation and affected-Level
  verification pending. The live diagnostic output demonstrates starvation of
  the House document. The user subsequently clarified that the reported missing
  patch is on LevelBlock, an ordinary Actor with a StaticMesh child in the main
  Level. Starvation is an independent finding, not the established cause of that
  missing patch; the earlier diagnosis over-attributed aggregate statistics.
- Symptom: a later document stays at zero fitting queries and components while
  the first document progresses. Camera effects remain independent of this
  visualization job.
- Trigger / repro: load a large main-Level storage document and a later Level
  Instance document, enable Show, enter PIE before the first job completes,
  and inspect the latter Level. The user's 16:35:39 Hong Kong log shows the
  main-Level CCS_IndoorSurfaceData_0 pending with 57 components,
  57,408/182,237 triangles and 77,312 queries (zero misses), while the House
  Level Instance's CCS_MeshSurfaceData_0 is pending with zero components,
  0/18,315 triangles and zero queries.
- Why it happens / root cause: the ticker discovers and processes each storage
  Actor sequentially. The first large pending document consumes the shared
  256-query/time budget every tick, so the next Actor never starts fitting.
  Sharing a limit does not make scheduling fair. Chunk publication likewise
  allows an earlier document to consume both publication slots. Existing
  progressive tests covered only one document.
- History / blast radius: reviewed progressive output, fitting-budget accounting,
  hidden storage/Level-owned teardown, collision eligibility, foreground
  occlusion and diagnostic compilation entries. All four prior PIEPreview
  tests passed in the user's 16:35 editor run. Audited ticker discovery, cache
  creation/pruning, fitting, chunk extraction, consolidation, weak ownership,
  source-Level unloading and all projection/factory consumers.
- Touched files:
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPIEPreview.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/Utilities/ComposableCameraMeshLayerTool.cpp
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerVisualizationTests.cpp
  - Docs/DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and BugLog.md
- Fix: discover/prune all documents first, then run separate resumable
  round-robin fitting and publication phases. Each fitting visit uses at most
  64 actual queries; each publication visit takes one ready chunk. Preserve
  the unserved job index when the global budget expires. Skip inactive jobs
  and stop after one entirely skipped cycle; a single active document may
  receive repeated visits. Reuse reserved weak-key scratch storage, normalize
  saved indices when streaming changes the queue and reset both positions on
  Show off/Edit/PIE teardown/module unload. Preserve the existing 256-query,
  soft 4 ms and two-publication global limits, depth-tested material, fitting
  semantics and final one-component-per-Layer consolidation.
- Regression-test name:
  ComposableCameraSystem.Editor.MeshCamera.PIEPreviewScheduling.
  Advances real fitting jobs with a 5,000-triangle first document and a
  512-triangle second document under a shared budget. Both receive queries
  and publish fitted first chunks while both remain incomplete. A one-chunk
  publication budget resumes at the second job next tick. A single active job
  retains the full global query budget through repeated visits. Further cases check
  budget suspension, skipped-cycle termination, changed/empty queues.
- Verification / blocker: project rules prohibit shell compilation and Unreal
  automation. Static review/whitespace checks only here. Header change requires
  closing UE, compiling UE5_6Editor in Rider/Visual Studio and restarting.
  Run all five PIEPreview tests. Enable Show, enter the affected House Level
  Instance in PIE and execute CCS.Editor.MeshLayers.DumpPIEPreview after a few
  frames: its query count should increase and fitted chunks should submit
  before the main-Level job finishes. Check stopping/toggling Show during
  fitting and repeat PIE for clean queue reset.
- Avoid next time: every bounded shared-work system needs an explicit fairness
  policy and multi-job tests. Test a small later job behind a large earlier one,
  and save independent positions when different budgets govern different phases.
- Possible conflicts / limits: no runtime camera, serialized data, Layer
  ownership or material-policy changes. Stable frames allocate no scheduling
  buffers or issue floor queries. Existing limitations for collision/render
  geometry differences and unbounded individual query/upload cost remain;
  this fix does not promise an exact first-visible or completion time.

## 2026-10-05 - Missing PIE overlay on an unchanged LevelBlock StaticMesh child

- Status: affected-point diagnostic output now establishes submitted geometry
  beneath the LevelBlock slab. See the following confirmed-cause entry for the
  source fix and exact regression. The diagnostic-only change compiled and ran
  in the user's PIE session; the new fitting fix still awaits IDE verification.
- Symptom: part of the Mesh Layer overlay remains invisible in PIE on flat
  LevelBlock ground, even when approached. Editor visualization and camera
  behavior work. LevelBlock is an ordinary Actor containing a StaticMesh; the
  user confirms no PIE position, scale or mesh changes. House is unrelated.
- Trigger / repro: paint/save Layers on the LevelBlock surface, enable Show Mesh
  Layers, enter PIE and stand inside the missing colored patch. Compare the
  same ground in the editor. No Construction Script/BeginPlay change is needed
  according to the user.
- Initial diagnosis: root cause was not established. Existing diagnostics count
  queries/triangles for entire documents and cannot identify whether triangles
  covering this specific position are absent, still pending or submitted below
  the actual floor. The House starvation finding does not answer this question.
  StaticMesh/Blueprint asset references alone do not prove live collision or
  material behavior. Do not assume non-static collision, actor motion, shader
  displacement or a House dependency without affected-point evidence.
- History / blast radius: reviewed all preceding PIE missing-patch, floor fitting,
  foreground occlusion, progressive output, scheduling, teardown and diagnostic
  compiler entries. Audited projection, preview creation/mesh ownership, native
  Layer query, command dispatch, and their test consumers. Point inspection is
  read-only and command-only; it does not change fitting or rendering policy.
- Touched files:
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPIEPreview.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/Utilities/ComposableCameraMeshLayerTool.cpp
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerVisualizationTests.cpp
  - Docs/EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and BugLog.md
- Diagnostic change: extend CCS.Editor.MeshLayers.DumpPIEPreview to probe the
  player Pawn's collision foot point. Log complex/simple floor results, raw
  complex hit identity/height/normal/response, nearby StaticMesh bounds candidates,
  mesh/material assets, native Layer height and submitted DynamicMesh height.
  Guard the preview-minus-floor delta with DepthComparable. Geometry inspection
  reads triangles and transforms without rewriting mesh data, changing materials
  or raising the overlay. No authored LevelBlock asset changes.
- Regression-test names:
  ComposableCameraSystem.Editor.MeshCamera.PIEPreviewSurfaceProjection now checks
  an ordinary Actor with SceneComponent root and scaled child StaticMesh using
  real complex queries. PIEPreviewPersistentMeshes checks submitted coverage at
  a point under a scaled/rotated stacked document, no coverage outside geometry,
  and submitted but buried geometry. These guard the diagnostic and floor cases;
  they do not yet reproduce the exact project-specific rendering failure.
- Initial verification / blocker: project rules prohibit shell Unreal compilation,
  editor launch and automation. No live Unreal inspection tool is available in
  this session; affected-point measurements have not been supplied. Close UE,
  compile UE5_6Editor in Rider/Visual Studio, restart, and run the expanded tests.
  In PIE stand on the missing LevelBlock area, then execute
  CCS.Editor.MeshLayers.DumpPIEPreview and inspect its PIE Mesh Layers lines.
  Pending=1 must be considered before classifying missing submitted coverage as
  final loss. Compare floor and submitted heights only with DepthComparable=1.
  Capturing the actual floor/component and geometry at this point is required
  before selecting the rendering fix and adding its exact failure regression.
- Avoid next time: local rendering defects require measurements at the affected
  point. Separate independent scheduler defects from the user's patch; retain
  uncertainty when aggregate data cannot establish causality. Test Actor-owned
  child StaticMeshes rather than relying only on box-component fixtures.
- Possible conflicts / limits: no runtime camera, source Layer data, collision
  settings, material policy or recurring ticker changes. Diagnostic scans may
  cause a one-time pause on a large mesh. Bounds candidates and collision results
  are not a rendered-depth measurement; a positive height delta alone cannot
  rule out material displacement, rendering visibility or another occluder.

## 2026-10-05 - PIE fitting buries Layers beneath a Visibility-ignoring LevelBlock slab

- Status: user confirms the LevelBlock overlay now displays. Focused automated
  regression results have not been supplied; its source remains pending IDE-side
  automation verification.
- Symptom: the flat LevelBlock region stays uncolored in PIE after loading
  completes, while camera effects and editor visualization work.
- Trigger / repro: author Layers on Landscape, cover that ground with a rendered
  StaticMesh slab slightly higher than Landscape and set the slab's Visibility
  response to Ignore. Enable Show Mesh Layers, enter PIE, stand on the slab and
  dump the preview after Pending reaches 0. The provided log at
  X=1116.067/Y=2188.391 shows 182237/182237 triangles, Pending=0, native Layer2
  Z=-1.992, one submitted intersection at Z=-0.493, LevelBlock_C_1.StaticMesh
  hit Z=0 with Visibility=0, and Landscape collision Z=-1.992 with Visibility=2.
  Both complex and simple fitting choose Landscape. No actor movement, changed
  mesh, missing triangle submission or House dependency is required.
- Why it happens / root cause: fitting reused authoring's Visibility response
  as an eligibility rule. The visible LevelBlock slab ignores Visibility, so
  it is excluded and the preview is fitted to Landscape beneath it. The 1.5 cm
  offset is smaller than the 1.992 cm slab separation, leaving the submitted
  depth-tested mesh about 0.493 cm below the opaque slab. Accepting the slab
  alone is insufficient because Landscape remains nearest to the native source.
- History / blast radius: reviewed previous disabled-depth character overdraw,
  non-static floor eligibility, progressive coverage, fair scheduling, point
  diagnostics and ownership/teardown entries. Audited all ProjectPIEPreviewVertex
  consumers, mesh export/publication and native Layer queries. Preserve existing
  primitive-volume/Pawn exclusions, query budgets, depth-tested material and
  immutable source/runtime triangles.
- Touched files:
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPIEPreview.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/Utilities/ComposableCameraMeshLayerTool.cpp
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerVisualizationTests.cpp
  - Docs/DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and BugLog.md
- Fix: accept upward-facing Visibility-blocking collision or rendered StaticMesh
  collision with an opaque/masked material. Check component/owner visibility;
  hidden or fully translucent Visibility-ignoring meshes remain excluded. Choose
  the nearest eligible floor, then promote to the highest rendered StaticMesh
  within 5 world cm above that fixed height. No chained lifts across hit order;
  a distinct upper storey remains separate. Reapply the original local offset
  once and retain XY/topology/Layer identity. No actor-name special case, floor
  collision edit, material depth override or native camera-data rewrite. Point
  diagnostics now log material blend mode and ShouldRender for StaticMesh hits.
- Regression-test name:
  ComposableCameraSystem.Editor.MeshCamera.PIEPreviewOccludingFloor.
  Uses an ordinary Actor's child StaticMesh at Z=0 ignoring Visibility over
  ground at Z=-1.992, plus a separate storey at Z=60. Fits and publishes real
  preview geometry, samples an interior point at the reported foot height and checks overlay Z=1.5
  with native Layer height unchanged. Also tests hidden component/owner rejection,
  translucent rejection, the fixed promotion band and standalone visible
  Visibility-ignoring floor eligibility. Existing PIEPreviewSurfaceProjection
  keeps box-volume Ignore/Overlap, Pawn, transformed-anchor and non-static cases.
- Verification / blocker: source consumer/API/format/whitespace review only.
  Project rules prohibit shell Unreal builds and automation. Close UE, compile
  UE5_6Editor in Rider/Visual Studio, restart, and run PIEPreviewOccludingFloor
  plus the five existing PIEPreview tests. Enable Show and repeat the same
  LevelBlock point after loading: native height stays near -1.992; submitted
  PreviewZ should be near 1.5 with full coverage. Check characters occlude the
  overlay and stable PIE frames retain the existing persistent-buffer behavior.
- Avoid next time: debug visualization follows scene occlusion, not necessarily
  authoring's pick channel. Test a visible Visibility-ignoring slab above the
  exact native collision surface; merely testing one isolated floor or document
  counts misses this failure. Keep nearest-floor selection separate from bounded
  occluder clearance so multi-storey scenes do not snap to the highest trace hit.
- Possible conflicts / limits: editor preview policy only; no runtime query,
  Profile, source data or serialization change. The fixed 5 cm band deliberately
  handles nearly coincident surfaces, not arbitrary floors above the source.
  Collision-free meshes, shader displacement and mixed-material face identity
  still require visual inspection. A StaticMesh with any opaque/masked material
  qualifies; the query does not identify which material section owns the hit.
  Material inspection and the extra hit pass are construction-only and add no
  recurring traces; existing global fitting budgets remain in force.

## 2026-10-05 - Show Mesh Layers stalls at initial geometry preparation

- Status: user confirms PIE loads progressively without the startup hitch.
  Editor Game View visibility regressed and is tracked in the following entry.
  Automated regression results and measured frame times have not been supplied.
- Symptom: user reports a 1-2 second freeze when enabling Show Mesh Layers in
  either the editor or PIE, despite accepting slow progressive mesh display.
- Trigger / repro: load the existing large Layer documents, leave Edit mode,
  enable Show while moving the editor camera or a PIE Pawn. The viewport/input
  pauses before geometry starts appearing. Repeat after toggling Show off.
- Why it happens / root cause: read-only EdMode Render synchronously resolves
  every first-seen document into up to roughly 100000 coverage cells and exports
  a complete Layer mesh. PIE discovery does the same clipping/export before
  reaching its existing floor-fitting budget. The large projection hash reserve
  also runs on the game thread. Limiting physics queries cannot bound these
  earlier stages. Editor PDI subsequently rebuilds full Layer buffers every
  render. PIE's final synchronous whole-document consolidation can cause a
  second hitch at completion.
- History / blast radius: reviewed progressive startup, global fitting budgets,
  fair publication, teardown/Scene ownership, depth testing and LevelBlock slab
  clearance bugs. Audited all runtime visualization, export, projection and
  preview actor factory consumers. Do not alter source data, floor eligibility,
  query budgets, native camera effects or Show menu routing.
- Touched files:
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPreviewBuild.cpp/.h (new)
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPreviewEdMode.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPIEPreview.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerRendering.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/Utilities/ComposableCameraMeshLayerTool.cpp
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerPreviewBuildTests.cpp (new)
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerVisualizationTests.cpp
  - Docs/DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and BugLog.md
- Fix: snapshot triangle arrays and enabled/color/identity metadata, excluding
  the BVH, Profiles, World and actors. A dedicated low-priority single-worker
  pool is created during module registration, avoiding shared-pool contention
  and Show-time thread creation. ThreadPool jobs resolve/export coverage;
  editor jobs also prepare native chunks and PIE jobs reserve projection-cache
  capacity. Consume ready futures only. Normal Show off/Exit cancels without
  waiting; module shutdown cancels/drains the registry and joins its owned pool
  before code unload. Waiting only for future readiness is insufficient because
  AsyncPool still destroys the callable afterward; the pool join closes that race.
  Editor Tick registers persistent, non-selectable chunks at most twice per
  frame across viewports, checking a soft 2 ms upload budget. Temporary editor
  previews are hidden in game to prevent duplicated overlays over characters.
  PIE retains 256 queries / soft 4 ms fitting, then publishes at most two chunks
  under soft 2 ms. Fitting/publication completion are distinct; keep draining
  ready tails after fitting finishes. Completed chunks persist without a full
  Layer consolidation upload. Stable frames reuse native render buffers.
- Regression-test names:
  - ComposableCameraSystem.Editor.MeshCamera.AsyncPreviewBuild: proves worker
    execution, immutable snapshots despite source edits/GC, complete overlapping
    Layer coverage/colors against synchronous reference, bounded native chunks,
    projection-cache preparation, replacement/cancellation and complete PIE tails.
  - ComposableCameraSystem.Editor.MeshCamera.EditorPreviewPublication: checks
    world routing, transient source-Level ownership, no package dirtiness,
    hidden-in-game editor previews, non-selectable collision-free components,
    editor material/color, requested batch counts, short tails and unregistration.
  - ComposableCameraSystem.Editor.MeshCamera.PIEPreviewProgressiveMeshes: updated
    to check publication completion and retained full-coverage persistent chunks
    without replacing the first native mesh at load completion.
- Verification / blocker: static consumer/signature/lifetime/format review and
  whitespace checks only. Project instructions prohibit shell Unreal builds and
  automation; IDE compile and real frame timings cannot be verified here. Close
  UE, build UE5_6Editor in Rider/Visual Studio and restart. Run both new tests and
  the six PIEPreview tests. Enable Show in editor and while PIE is already running:
  keep moving during preparation, confirm no initial input freeze and progressive
  complete coverage. Also enter PIE with Show enabled, verify the LevelBlock region
  and character occlusion, and toggle off/on before loading finishes. Repeat PIE
  exit/re-entry and streaming cleanup; pending diagnostics reach 0 with matching
  submitted/expected counts after all tails upload.
- Avoid next time: budget discovery/preparation/upload as well as collision work.
  Moving heavy clipping to a worker is insufficient if result adoption allocates
  the full hash or completion performs a giant mesh upload. Never wait on a
  future from activation, Render or ordinary Tick. Worker inputs must be plain
  snapshots, and cancellation must prevent publication by stale generations.
- Possible conflicts / limits: only Editor-module visualization changes; runtime
  data, serialized authoring, Profiles and camera evaluation remain intact. More
  persistent chunks mean more components/draw calls than one mesh per Layer,
  balanced by small uploads and per-chunk culling. The 2/4 ms budgets are soft:
  one registration or physics query cannot be preempted. Initial source-array
  copies still occur on the game thread; very large source documents and first
  material/shader use require measured IDE-side profiling. Module unload may
  briefly wait for cancelled workers, while ordinary Show toggles never wait.

## 2026-10-05 - Persistent editor Mesh Layers disappear in Game View

- Status: user confirms Game View (G) is enabled. Root cause identified in UE5.6
  scene-proxy visibility code; source fix and regression updated. IDE compile,
  automation and visual confirmation remain pending.
- Symptom: after the asynchronous preview change, PIE is smooth and progressively
  displays Layers, but enabling Show in the editor displays no colored mesh.
- Trigger / repro: enable Game View with G in a Level Editor viewport, then
  enable Show Mesh Layers after leaving Edit mode. Meshes remain absent after
  geometry preparation/publication. Ordinary editor view uses different hide
  rules and does not reproduce the same gate.
- Why it happens / root cause: the preceding fix used both Hidden In Game and
  bIsEditorOnlyActor to keep persistent editor previews from drawing in PIE.
  Editor Game View uses the Game show flags too. FPrimitiveSceneProxy::IsShown
  rejects editor-only owners when Game is enabled and rejects DrawInGame=false
  from the owner's hidden flag. Both flags independently suppress the otherwise
  registered, populated component. The previous regression checked ownership,
  registration and triangle counts and even asserted those hiding flags; it
  never checked scene-proxy draw relevance for a Game View family.
- History / blast radius: reviewed asynchronous startup, persistent chunk
  publication, duplicated-overlay character overdraw, LevelBlock clearance and
  teardown bugs. Audited AppendNativePreviewMeshes, editor EdMode Tick, PIE
  factory callers and all visibility/duplication flag consumers. Read-only UE5.6
  references: Engine/Private/PrimitiveSceneProxy.cpp (IsShown and owner flags),
  CoreUObject/Private/Serialization/DuplicateDataWriter.cpp (RF_DuplicateTransient
  serializes a null reference), Engine/Private/Level.cpp and
  WorldPartition/WorldPartitionStreamingGeneration.cpp (bIgnoreInPIE).
- Touched files:
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPIEPreview.cpp
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerPreviewBuildTests.cpp
  - Docs/DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and BugLog.md
- Fix: keep preview actors visible and not editor-only. Editor previews instead
  receive RF_DuplicateTransient and bIgnoreInPIE, separating G-view visibility
  from exclusion during PIE duplication/streaming. Their transient, temporary,
  source-Level-owned lifetime and non-selectable components remain intact.
  PIE preview actors retain ordinary visible flags and the depth-tested material.
  Background geometry, publication/fitting budgets and source/runtime data are
  unchanged; no new production per-frame work or rendering waits.
- Regression-test name:
  ComposableCameraSystem.Editor.MeshCamera.EditorPreviewPublication.
  Extended to test the actual published component's render-thread GetViewRelevance
  with ordinary editor and Game View families. Both must have draw relevance.
  Independently restore Hidden In Game and editor-only flags as negative controls
  to reproduce invisible Game View with normal-view visibility, then restore
  the production policy and verify visibility returns. Also checks explicit
  PIE duplication exclusion, transient ownership, no Level dirtiness, material,
  bounded chunks and component unregistration. FlushRenderingCommands is confined
  to automation fixtures to safely query the real render proxy.
- Verification / blocker: consumer/lifetime/API/whitespace review only. Project
  rules require Rider/Visual Studio compilation and prohibit shell Unreal builds
  and automation. Compile UE5_6Editor in the IDE, run EditorPreviewPublication,
  and manually enable Show in normal view and G view. Both should progressively
  display the same geometry without the old startup freeze. Enter PIE with Show
  already enabled; confirm one depth-tested overlay, character occlusion and
  LevelBlock coverage. Exit PIE and toggle G/Show to check persistent cleanup.
- Avoid next time: editor Game View and PIE are distinct contexts sharing Game
  show flags. Use duplication/streaming policy for excluding preview objects,
  and renderer relevance for verifying visibility. Registered primitives and
  matching triangle counts do not prove a view will draw them.
- Possible conflicts / limits: this remains editor-module, transient debug
  visualization. Source data, camera effects, async work and floor fitting do
  not change. Editor Show intentionally remains visible in G view; PIE continues
  using its independent world/material route. RF_DuplicateTransient also prevents
  ordinary object duplication of these disposable previews. No shipping output
  contains their RF_Transient actors. Pixel appearance/frame times still require
  an IDE-built visual check.

## 2026-10-06 - Editor Show Mesh Layers is visible only from below after persistent-mesh migration

- Status: user supplied an underground-view screenshot after the Game View
  visibility fix. Source culling regression identified and fixed; IDE compilation,
  automation and visual confirmation remain pending. The screenshot alone does
  not prove that submitted vertex heights moved below the ground.
- Symptom: Show Mesh Layers appears absent from above in the editor but colored
  surfaces become visible when viewing from beneath the floor. PIE still loads
  progressively and displays the overlays.
- Trigger / repro: with the engine's one-sided GeomMaterial, load a saved Layer
  document, leave Edit mode, enable Show and wait for publication. Compare
  above-ground and below-ground views, including G / Game View. Edit mode's
  previous PDI path and PIE's two-sided material do not share this culling policy.
- Why it happens / root cause: the async optimization replaced PDI mesh draws
  with persistent DynamicMesh components. PDI passed bDisableBackfaceCulling=true;
  GeometryFramework's BaseDynamicMeshSceneProxy::DrawBatch does not set that
  override and follows the material's culling. Keeping the one-sided GeomMaterial preserved
  disabled depth testing but lost two-face visibility. Matching positions, draw
  relevance and source triangle counts could all pass while one viewing direction
  still culled the surface. Raising the mesh would not fix this renderer mismatch.
- History / blast radius: reviewed the recorded one-sided GeomMaterial caveat in
  the prior PIE missing-patches entry, its disabled-depth/character-overdraw
  follow-up, asynchronous startup budgets and independent Game View hide flags.
  Audited every BuildNativePreviewMeshes caller, worker snapshot, publication
  consumer and count assertion. Keep PIE's fitted, depth-tested, two-sided route
  and editor duplication/visibility flags intact; no runtime/authoring mutation.
- Touched files:
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPIEPreview.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPreviewBuild.cpp
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerPreviewBuildTests.cpp
  - Docs/DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and BugLog.md
- Fix: capture the editor material's two-sided policy on the game thread as a
  bool. For a one-sided editor material only, prepare disconnected reverse-winding
  faces on the worker at identical positions. Count both faces toward the native
  1024-triangle upload limit by reducing the source allowance. Native triangle
  accounting includes both faces. Two-sided editor materials and PIE receive
  only original faces. Publication remains two chunks / soft 2 ms per frame;
  toggling Show still never waits. No shader/material asset changes, World access
  on the worker, new floor traces or per-frame topology work.
- Regression-test names:
  - ComposableCameraSystem.Editor.MeshCamera.EditorPreviewBackfaces: verifies
    paired positions, opposite windings, disconnected indices, shared source
    vertices, slopes, mirrored/rotated transforms, unchanged height/XY/source
    data, total triangle bounds, short tails and the single-face route.
  - ComposableCameraSystem.Editor.MeshCamera.AsyncPreviewBuild: actual worker
    output must follow the editor material's face count within existing bounds.
  - ComposableCameraSystem.Editor.MeshCamera.EditorPreviewPublication: persistent
    chunk publication retains all requested faces alongside existing G-view checks.
- Verification / blocker: source/API/consumer/lifetime and whitespace review only.
  Project rules prohibit shell Unreal builds/automation and require Rider/Visual
  Studio. Compile UE5_6Editor in the IDE, restart to remove already published
  single-sided chunks, and run these three tests plus PIEPreviewPersistentMeshes.
  Enable Show and inspect the reported region from above/below, toggle G, and
  confirm progressive loading remains responsive. Enter PIE with Show enabled;
  confirm LevelBlock coverage and character occlusion remain correct.
- Avoid next time: renderer migrations must compare rasterization and material
  depth/culling policies, not only geometry bounds or render-proxy draw relevance.
  Review earlier BugLog caveats when moving a previously PDI-only material to
  native components. A below-only image can be backface culling rather than bad Z.
- Possible conflicts / limits: editor-only transient visualization; no serialized
  data, camera evaluation or Profile changes. Extra editor faces increase total
  cached topology and component count under the unchanged per-frame upload bound.
  No duplicate geometry is emitted with a two-sided material. Real pixel output
  and frame timings still need the IDE-built viewport check.

## 2026-10-06 - Mesh Layers lose uneven-surface patches at a distance

- Status: user confirms r.ForceLOD 0 restores complete coverage, and explicitly
  chooses automatic LOD stabilization while Show is enabled. View-scoped source
  fix and regression added; IDE compile, pixel output and frame times pending.
- Symptom: distant uneven regions have missing colors; moving closer restores
  complete coverage. The screenshot contains lower-Layer-colored gaps inside
  another Layer's region. It does not establish missing submitted triangles.
- Trigger / repro: enable Show, observe uneven ground from far away, approach
  until the coverage appears complete, then return to the identical distant view.
  Repeat in editor and PIE after progressive publication has finished.
- Why it happens / root cause: confirmed LOD dependence rather than a
  distance-controlled loader. The loader does not read the view
  location or delay documents by distance. Completed chunks are persistent and
  have no mesh LOD. Primitive defaults have zero min/max draw distance; the
  preview's Movable components are excluded by CullDistanceVolume eligibility.
  UE5.6 LandscapeVertexFactory.ush interpolates between LOD heights as distance
  changes, while PIE fitting samples static collision once with a 1.5 cm offset.
  This can bury PIE overlays. Installed
  GeomMaterial serializes bDisableDepthTest=true and DynamicMeshSceneProxy disables
  occlusion tests for such material relevance, so the editor report is not
  attributed solely to per-pixel floor depth. The same-view diagnostic establishes
  that stabilizing LOD restores the user's coverage; real output in both modes
  still needs verification with the scoped Landscape family policy.
- History / blast radius: reviewed material depth/character-overdraw, LevelBlock
  slab clearance, progressive startup, Game View flags and editor backfaces.
  Audited preview discovery/publication, materials, primitive draw-distance
  defaults, CullDistanceVolume filters, DynamicMesh proxy bounds/visibility and
  Landscape vertex LOD morphing. Preserve responsive loading, character occlusion,
  floor/storey separation and unchanged authored/runtime geometry.
- Touched files:
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPreviewViewExtension.cpp/.h (new)
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPIEPreview.cpp
  - Source/ComposableCameraSystemEditor/Private/Utilities/ComposableCameraMeshLayerTool.cpp
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerPreviewViewTests.cpp (new)
  - Docs/DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and BugLog.md
- Fix: use an Editor-module scene view extension to set LandscapeLODOverride=0
  on ordinary viewport families with a live, published editor/PIE preview Actor.
  BeginRenderViewFamily runs before scene renderer creation and Landscape LOD
  calculation. Show requested state, actual scene world, published Actor lifetime
  and PIE-ending state gate the policy. Asset previews, packaged Game worlds,
  unrelated worlds and scene/reflection/planar capture views are excluded.
  Every family is disposable; Show off and last-preview cleanup leave future
  families' original LOD policy intact. No global r.ForceLOD, serialized Landscape
  ForcedLOD property, vertex-height lift or material/depth policy mutation.
  Publication/cleanup updates weak registrations; callbacks allocate nothing.
  Normal toggles never flush; module unload drains in-flight families before
  releasing extension code. Default-1 CCS.Editor.MeshLayers.StabilizeLandscapeLOD
  permits opting out. User accepted the extra terrain draw cost for complete coverage.
- Regression-test name:
  ComposableCameraSystem.Editor.MeshCamera.PreviewLandscapeLODStability.
  Publishes real editor/PIE meshes, invokes the actual extension callback with
  normal/G-view near/far families, checks unrelated/asset/Game/capture exclusions,
  CVar opt-out, Show off/on, PIE teardown/re-entry, last-Actor removal/destruction,
  unchanged published mesh height, untouched r.ForceLOD and clean Level packages.
- Verification / blocker: source/API/lifetime/consumer review and whitespace
  checks only. Project rules require Rider/Visual Studio compilation and prohibit
  shell Unreal builds/automation. Close UE, compile UE5_6Editor in the IDE, restart,
  and run PreviewLandscapeLODStability, EditorPreviewPublication and
  PIEPreviewPersistentMeshes. Restore r.ForceLOD -1 before enabling Show, compare
  the same distant uneven region in editor/G/PIE, approach and retreat, check
  character occlusion and initial loading responsiveness. Toggle Show off and
  confirm normal terrain LOD resumes. Measure terrain frame cost with Show on.
- Avoid next time: do not equate colors recovering on approach with loading.
  Hold the view fixed and independently vary LOD/publication completion. Confirm
  material depth policy in each mode before attributing both to terrain occlusion.
- Possible conflicts / limits: all Landscape in an eligible view uses LOD 0,
  increasing distant terrain rendering cost; this is not limited to Layer bounds.
  StaticMesh LOD and source assets remain untouched. Collision/render mismatch,
  Nanite terrain, heightmap streaming and material displacement still need visual
  inspection. The scope is editor-only visualization. Disabling PIE depth testing
  would reintroduce character overdraw; lifting all vertices would regress near
  ground and stacked floors. No terrain property save/restore window or asset
  dirtiness is introduced. No new World, UObject or camera API enters the worker.

## 2026-10-06 - PIE Layer remains buried when a visible slab exceeds the 5 cm clearance

- Status: exact height mismatch confirmed in the live UE5_6 log; source fix and
  regression are pending Rider/Visual Studio compilation and editor verification.
- Symptom: the Pawn appears outside colored Mesh Layers while a Layer's Camera
  Profile still activates. The user clarifies that the apparent non-Mesh patch
  is missing only in PIE visualization, rather than an incorrect runtime entry.
- Trigger / repro: enable Show Mesh Layers, enter PIE, wait for nearby colors,
  stand on the flat LevelBlock patch and run CCS.Editor.MeshLayers.DumpPIEPreview.
  The initial Show=0/Documents=0 output cannot diagnose active preview geometry.
  The subsequent live log at 2026-10-06 10:09:41 HKT reports Show=1/Documents=2,
  main-document Pending=0 and 182237/182237 triangles. At foot
  X=2578.856/Y=2839.943/Z=2.275, native Layer2 is Z=-7.070, submitted coverage has
  one hit at Z=-5.570, and the visible opaque Visibility-ignoring LevelBlock
  StaticMesh top is Z=0. The unrelated House document has zero native/submitted
  coverage at this point.
- Why it happens / root cause: the previous LevelBlock fix promotes to a visible
  slab only within 5 world cm above the nearest eligible floor. At this deeper
  Landscape point the gap is 7.070 cm, so the slab is eligible but not selected.
  All triangles publish at native height plus the 1.5 cm offset, below the opaque
  slab. Native camera coverage is correct; runtime Layer data must not be edited
  to compensate for visualization depth. Sampling from the Pawn's foot instead
  finds the slab directly and reports FittedZ=1.5, hiding this source-height-specific
  distinction unless native/submitted heights are also compared.
- History / blast radius: reviewed the 2026-10-05 Visibility-ignoring LevelBlock
  fix, full point diagnostics, progressive publication, async construction,
  character occlusion and the separate distant Landscape LOD issue. Audited
  ProjectPIEPreviewVertex's construction/diagnostic/test callers, publication
  factories and storage QueryLayers. Preserve shared-vertex reuse, fixed-nearest
  anchoring, depth testing, exclusion filters and the global query/upload budgets.
- Touched files:
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerPIEPreview.cpp
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerVisualizationTests.cpp
  - Docs/DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and BugLog.md
- Fix: expand preview-only occluding-slab clearance from 5 to 10 world cm. The
  highest rendered slab is still measured from the original nearest floor,
  never from successively promoted hits. Keep the 100 cm trace reach, nearest
  floor selection, local XY/topology/colors, 1.5 cm offset, immutable native data
  and depth-tested material. No added physics queries, per-frame work or worker
  waits. The existing distinct storey at Z=60 remains excluded from promotion.
- Regression-test name:
  ComposableCameraSystem.Editor.MeshCamera.PIEPreviewOccludingFloor.
  Retains the earlier 1.992 cm fixture and adds the actual 7.070 cm gap, real
  budgeted fitting, publication and interior point sampling. Checks submitted
  Z=1.5, zero misses, full 7.070 cm correction, unchanged native query Z=-7.070
  and rejection of the 60 cm storey. Move the beyond-band slab fixture to Z=11
  so it exceeds the new clearance above native Z=-1.992. Hidden/Pawn/translucent
  eligibility and standalone-floor behavior retain their prior checks.
- Verification / blocker: source, consumer and whitespace review only. Project
  rules prohibit shell compilation/editor/automation execution. Compile in
  Rider/Visual Studio, run PIEPreviewOccludingFloor, then enable Show in a fresh
  PIE session at the recorded point. Expect Pending=0, matching triangle counts,
  NativeZ near -7.070 and submitted PreviewZ near 1.5. Check character occlusion,
  progressive loading and separate upper-storey visualization remain correct.
- Avoid next time: exercise the clearance threshold using source heights, not
  just foot-based queries or total triangle counts. Include a gap above the old
  limit and below the intended limit, along with beyond-limit and storey cases.
- Possible conflicts / limits: this intentionally promotes visible slabs 5-10 cm
  above the nearest floor that the earlier policy left untouched. No runtime
  membership, Profile lifecycle, actor collision, source serialization or Landscape
  LOD change. Gaps greater than 10 cm, collision-free floors, shader displacement
  and curvature between fitted vertices remain outside this bounded correction.

## 2026-10-06 - Mesh authoring hardcodes Visibility instead of a Layer surface channel

- Status: requested Layer Channel feature and focused regression sources added;
  Rider/Visual Studio compile and editor automation remain pending.
- Symptom: a visible StaticMesh surface that ignores Visibility cannot be selected
  as the Layer's drawing surface, even when it blocks a dedicated project channel.
  Hover/Brush/Shape projection can pass through it and hit underlying Landscape.
- Trigger / repro: place a query-collision StaticMesh slab above a Visibility-blocking
  floor. Make the slab ignore Visibility and block Camera or a custom trace channel.
  Previously the Layer offered no Channel choice, and every authoring trace used
  ECC_Visibility. The LevelBlock log demonstrates this response configuration;
  the user explicitly requests a per-Layer Channel field.
- Why it happens / root cause: Layer definitions held identity/Profile/color/enabled
  state but no authoring trace-channel policy. Four independent editor trace sites
  hardcoded Visibility: hover, Brush ring, queued Shape creation and Shape editing.
  StaticMesh type is supported; the fixed query channel prevents targeting it.
- History / blast radius: reviewed Visibility-ignoring LevelBlock occlusion fixes,
  bounded clearance, Shape async cancellation, document Undo/Discard and storage
  round-trip. Audited all Layer definitions/copies, Selection Details, four channel
  trace sites, worker snapshots, storage rebuild and runtime query consumers.
  Consulted read-only UE5.6 GameplayCameras collision-node channel fields and engine
  CollisionProfile enum naming; no reference code or engine file was modified.
- Touched files:
  - Source/ComposableCameraSystem/Public/MeshCamera/ComposableCameraMeshSurfaceTypes.h
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerEdMode.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerTraceChannelTests.cpp (new)
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerShapeTests.cpp
  - Docs/DesignDoc.md, EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and BugLog.md
- Fix: add serialized TraceChannel with displayed name Channel and default Visibility.
  Standard ECollisionChannel Details includes configured project channel names.
  Shared TraceLayerSurface resolves by owning Layer GUID, validates the channel and
  handles every editor surface trace. Missing Layers never use an implicit fallback.
  Existing normal/depth/query filters remain. Layer edits already cancel pending
  jobs; whole-struct Save/proxy/Undo/checkpoint copies retain Channel. Tagged old
  records default to Visibility. Existing triangles are not automatically moved;
  redraw or edit Shape controls to project them onto the newly selected channel.
- Regression-test names:
  - ComposableCameraSystem.Editor.MeshCamera.LayerTraceChannel: real upper StaticMesh
    ignores Visibility and blocks Camera/custom; lower mesh blocks Visibility.
    Checks independent Layer-GUID routing, unblocked/missing Layer rejection, actual
    Brush ring and Rectangle/Circle/Polygon projection, document storage, unchanged
    native coverage, tagged round-trip and legacy records without the new field.
  - ComposableCameraSystem.Editor.MeshCamera.DocumentUndoRedo: actual Channel Details
    handle, document/proxy Undo/Redo and stable Layer GUID.
  - ComposableCameraSystem.Editor.MeshCamera.DocumentDiscard: restore a changed
    Channel from the nontransactional opening checkpoint.
- Verification / blocker: source/API/consumer/whitespace review only. Project rules
  prohibit shell Unreal builds and automation. Close UE, compile UE5_6Editor in
  Rider/Visual Studio, restart and run these three tests. This USTRUCT/UPROPERTY
  change requires a full restart; Live Coding is insufficient. In the tool choose
  a Layer, select Channel, set the intended StaticMesh to block that channel and
  leave Visibility ignored, then paint/save/reopen and inspect editor/PIE coverage.
  Test the default Layer still paints through to the Visibility floor.
- Avoid next time: surface type and trace response are independent. Centralize
  query-channel selection and audit hover, repeated Brush samples, queued creation
  and retained Shape edits together; testing only the cursor hit misses divergence.
- Possible conflicts / limits: Channel selects editor authoring collision, not
  runtime activation filtering. Runtime queries still use baked triangles; PIE
  occluder fitting remains independent and preserves earlier character-depth fixes.
  Floors must have query collision and block the chosen channel. Changing Channel
  does not reproject old brush geometry; project channel definitions are project
  settings, not created/modified by this feature. No extra worker UObject reference,
  query budget change, reflection field on Shape or camera hot-path allocation.

## 2026-10-06 - Automatic Mesh membership crosses an unpainted blocking floor

- Status: explicit Query/Update/Clear API and collision-surface regression sources
  added. Rider/Visual Studio full build, UHT and Unreal automation remain pending.
- Symptom: business cannot choose when Mesh Profiles are queried/applied. A player
  standing on an unpainted upper StaticMesh may activate a Layer painted below it.
- Trigger / repro: paint a Layer at Z=0, place an unpainted query-collision floor at
  Z=100, and query from Z=150 with the former 300 cm downward world query. The old
  subsystem Tick reads Pawn.GetActorLocation and selects the lower saved triangles,
  regardless of the nearer scene collision. Start PIE without any explicit Mesh
  call: automatic polling can enter Profile effects. The user requires business
  ownership of invocation and explicit blocking Channels or a collision Profile.
- Why it happens / root cause: UTickableWorldSubsystem combined discovery, implicit
  Pawn-origin selection and effect reconciliation. Its geometric ray only saw painted
  triangles, so the nearest *painted* surface was treated as the physical floor.
  The authoring Layer Channel does not solve runtime occlusion or caller ownership.
- History / blast radius: reviewed temporary Camera Context ownership/restoration,
  exact Action/Patch cleanup, async Profile preload membership/lifetime, instanced
  storage identity, transformed BVH and Visibility-ignoring authoring/preview fixes.
  Audited all world/storage query callers, effect dispatch, storage registration/
  EndPlay, preload polling, removed Tick fields and companion documentation. Referenced
  read-only UE5.6 CollisionProfile/WorldCollision and GameplayCameras' explicit
  PlayerController activation APIs; no engine/reference code was modified.
- Touched files:
  - Source/ComposableCameraSystem/Public/MeshCamera/ComposableCameraMeshSurfaceTypes.h
  - Source/ComposableCameraSystem/Public/MeshCamera/ComposableCameraMeshWorldSubsystem.h
  - Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshWorldSubsystem.cpp
  - Source/ComposableCameraSystem/Private/Tests/ComposableCameraMeshQueryTests.cpp (new)
  - Source/ComposableCameraSystem/Private/Tests/ComposableCameraMeshProfileEffectsTests.cpp
  - Docs/DesignDoc.md, TechDoc.md, ExecutionFlowExamples.md, EditorDesignDoc.md and BugLog.md
- Fix: replace automatic ticking with a non-ticking UWorldSubsystem. Query APIs are
  read-only and take an explicit world origin plus QueryParams. Channels mode traces
  each distinct supplied channel, taking the nearest blocking hit over their union.
  Profile mode borrows a configured UE collision template and supplies its channel/
  responses, including supported named redirects. Empty/invalid configuration and
  a miss return empty; unknown Profile never falls back. Only saved Layer geometry
  within the first physical hit's +/- SurfaceTolerance is queried, including all
  enabled overlapping Layers and distinct storage instances. An unpainted blocker
  stops the search. Outputs report the collision point and distance, not buried
  preview geometry. Geometry-only storage queries remain internal/debug primitives.
  Explicit UpdateMeshLayers performs this same query then reconciles effects for
  the supplied local PC; invalid/missed queries exit prior scopes. ClearMeshLayers
  releases only that player's effects. Async completion cannot activate anything:
  readiness is retried only on later business Updates. Stop integration via Clear.
  PC/PCM EndPlay and document unregister release ownership without player polling;
  world teardown avoids reactivating ending cameras. Existing family dispatch,
  nested Context order and exact instance cleanup stay intact.
- Regression-test names:
  - ComposableCameraSystem.MeshCamera.BlockingSurfaceQuery: actual two-storey Actor
    StaticMesh collision, upper painted/unpainted cases, channel union/order/custom
    selection, ignored Actor, Block-vs-Overlap, Profile/invalid-name behavior, empty/
    malformed policy, distance and tolerance limits, above/below matching, disabled
    Layers, transformed repeated-GUID documents and public/inline query agreement.
  - ComposableCameraSystem.MeshCamera.ManualUpdateAndClear: no ticking, no query
    side effects, no-Pawn caller position, stable Action identity, exits/Clear
    preserving external Actions, stalled preload/readiness/cleared membership,
    invalid-policy exit, document unload, player EndPlay and CameraManager replacement/
    EndPlay binding cleanup. Its actors
    explicitly begin play because an isolated world without GameMode does not
    dispatch that lifecycle automatically.
  - Existing ExclusiveProfileDispatchAndCleanup and ProfilePreloadMembershipAndLifetime
    continue exercising Camera/Modifier/Action/Patch ownership; remove obsolete
    LastSeenFrame fixture state because automatic expiry no longer exists.
- Verification / blocker: API/consumer/lifecycle/enum/GC/whitespace source review only.
  Project rules forbid shell Unreal builds/editor/automation. Close UE, full build
  UE5_6Editor in Rider/Visual Studio, restart and run the two new tests plus existing
  MeshCamera tests. UCLASS superclass/USTRUCT/UFUNCTION changes require a full restart,
  not Live Coding. Refresh old Blueprint Query nodes and supply QueryParams. Manually
  wire Update after movement using a caller-selected origin and ignored Pawn; wire
  Clear on disable/unpossess. Verify no calls means no entry, two floors do not leak,
  overlapping Profiles exit in order, misses restore gameplay and disabling with
  Clear removes effects. Verify Show still works independently and loading remains
  nonblocking. Profile mode must be a collision preset name, not a Mesh camera Profile.
- Avoid next time: separate passive spatial query, business invocation and scoped
  effect ownership. Always test an unpainted collision blocker above painted geometry;
  testing only nearest painted triangles cannot prove actual surface selection.
- Possible conflicts / limits: automatic entry is intentionally removed; existing
  projects must provide explicit Update/Clear integration and refreshed query inputs.
  A policy that ignores the upper floor permits tracing through it by design. Floor
  query collision is required; no simple/complex fallback is added. SurfaceTolerance
  is an absolute cm band around collision; large values can merge nearby surfaces.
  Existing incorrectly projected geometry outside that band needs redraw or a
  business-selected tolerance. First/growth Blueprint outputs, unusually deep
  overlap (>16) or large UE ignored-Actor lists may allocate; normal BVH traversal,
  profile lookup and steady ownership introduce no extra heap work. Editor Channel,
  progressive visualization, LOD stabilization and authored geometry are unchanged.

## 2026-10-06 - Empty business Mesh Layer queries lack failure evidence

- Status: the user ran the diagnostic. The captured Update rejects a null supplied
  PlayerController before querying collision. Blueprint input correction and a new
  actual Query/Update dump remain pending; automation is not reported as run.
- Symptom / repro: manually call QueryMeshLayers while standing in visibly colored
  coverage; returned array is empty. The user confirms Draw works after selecting
  the intended Layer Channel, but the query still needs investigation. Existing
  preview dumps do not record the business call's actual origin/policy or reject stage.
- Why / root cause: the public boolean/array collapses invalid policy, physical miss,
  penetration, absent registered documents and surface-band mismatch into one result.
  The root cause of this particular live query is not yet established. Earlier logs
  prove incorrectly authored Z=-7.070 geometry beneath a Z=0 LevelBlock, but cannot
  establish whether the newly tested call uses that geometry or a different policy.
- Subsequent runtime evidence: Reason=InvalidUpdateOwner, PlayerController=None,
  CameraManager=None, Local=0, Registered=2. Both saved documents are registered
  and have begun play. No blocking-channel or ignored-Actor rows are present in
  the supplied output, while TraceMode=0 selects Channels; if the output is complete,
  those two arrays are empty. Empty channels would also reject a standalone Query.
  Correct the business wiring: provide the intended local PlayerController to Update,
  supply the configured QueryParams to both APIs, and explicitly ignore the querying
  Pawn as appropriate. Authoring Layer Channel does not populate the runtime policy.
  NativeHit=0 is also reported at (530.359, -6688.660, 88.525), but the actual collision
  query never ran; recheck saved coverage at the intended test point after fixing
  inputs. Do not change first-blocker, tolerance or automatic player ownership rules
  based on this rejected Update. Existing NextQueryDiagnostics already covers null
  Update ownership and empty-channel validation; no runtime source change is needed.
- History / blast radius: read Layer Channel and Visibility-ignoring LevelBlock
  history, explicit Query/Update/Clear ownership, first-blocker protection and storage
  BeginPlay/registration. Audited all world query consumers and the native/BVH query
  path. Read UE5.6 console command and expected-message APIs; reference engine files
  remain unchanged. No Blueprint signature, drawing or collision behavior is changed.
- Touched files:
  - Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshWorldSubsystem.cpp
  - Source/ComposableCameraSystem/Private/Tests/ComposableCameraMeshQueryTests.cpp
  - Docs/DesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and BugLog.md
- Diagnostic fix: non-shipping CCS.MeshLayers.DebugNextQuery arms one weak Game/PIE
  world request. The next actual business Query or Update consumes it, including
  invalid input/ownership. Output gives a reject reason, actual caller configuration,
  blocking Actor/component, registered-document count, enabled/native triangle counts,
  and nearest enabled native source height at that XY inside a printed probe range.
  Other worlds cannot consume the request. Loaded document discovery never registers
  actors, enables Layers, rebuilds source or adds probe hits to results. No player
  lookup, implicit query/update, scene retrace, asset load or effect activation.
  Dormant diagnostics do not format strings or scan actors; shipping strips the command.
- Regression test: ComposableCameraSystem.MeshCamera.NextQueryDiagnostics.
  Uses the real console command and two query worlds. Checks one-shot consumption
  on empty policy, world isolation, exact -7.070 native/physical mismatch logging,
  read-only unregistered document discovery, unchanged successful Layer identity,
  physical miss and invalid Update owner. Existing BlockingSurfaceQuery and
  ManualUpdateAndClear retain membership/first-blocker/lifecycle coverage.
- Verification / blocker: source/consumer/format/whitespace review only. Project rules
  forbid shell Unreal builds/tests. Compile UE5_6Editor in Rider/Visual Studio and
  restart UE. Run NextQueryDiagnostics, BlockingSurfaceQuery and ManualUpdateAndClear.
  In a fresh PIE, stand on the failing region, enter CCS.MeshLayers.DebugNextQuery
  in the game console, then execute the existing business Query/Update. Paste all
  Mesh Layer Query lines; if only Armed appears, that world has not called the API
  again. Show Mesh Layers need not be enabled. Only .cpp/test/docs change this turn.
- Avoid next time: diagnose the actual caller's policy and collision hit before
  changing tolerance, guessing an Actor restriction or comparing fitted preview height.
  An empty query is valid behavior for several distinct rejection conditions.
- Possible conflicts / limits: this diagnostic does not fix or migrate buried geometry,
  supply missing channels, ignore a Pawn implicitly or activate camera effects. Native
  probe height is the nearest enabled saved surface inside ProbeRange and may belong
  to another storey; it is evidence, not membership. The first next call in the armed
  world wins when several business callers exist. Run the command in the PIE game
  console; Editor-world invocation reports that instruction without falling back.
  Explicit one-shot formatting/probing is diagnostic work, not normal per-frame work.

## 2026-10-06 - Scene-centered Layer matching changes the original ray and shares hit heights

- Status: source and regression tests updated; IDE compilation and automation
  execution remain pending. The user chose to reject only blockers above a Layer
  beyond tolerance; missing scene hits and empty Channels preserve native hits.
- Symptom: a saved Layer may be selected outside the business-supplied ray, or a
  farther scene surface may select a different painted floor. Grouped Layers can
  report another Layer's height and pass an occlusion check using that borrowed point.
- Exact trigger / repro:
  - With scene floor Z=0, saved Layer Z=4, origin Z=1 and tolerance 5, the old
    scene-centered ray returns geometry behind the original downward origin.
  - With scene floor Z=0, saved Layer Z=-2, origin Z=20, maximum distance 21 and
    tolerance 5, the old matching band returns geometry beyond the original endpoint -1.
  - With saved floors Z=100 and Z=0, origin Z=150, and a policy blocking only the
    lower floor, collision-centered matching selects Z=0 instead of native Z=100.
  - With global saved nearest Z=12, another document containing Layers Z=10 and
    Z=7, scene blocker Z=14 and tolerance 5, the Z=7 Layer can borrow Z=10 and
    incorrectly pass the blocker comparison. One Layer with triangles at Z=8
    and Z=10 must report Z=10 regardless of visitation order.
- Why / root cause: the world query used the scene hit to replace the geometry
  ray origin and length with a +/- tolerance band. This is height sampling around
  collision, not intersection with the original caller segment. Native storage
  also assigned one shared nearest point to every Layer returned in that band.
  Filtering documents by their shared point can include triangles outside the
  global nearest-surface band, especially across Level Instances/documents.
- History / blast radius: reviewed Visibility-ignoring LevelBlock projection,
  authoring Channel isolation, manual Query/Update/Clear ownership, first-blocker
  protection, one-shot query diagnostics, BVH pruning and transformed-document
  Undo/Redo. Audited native/world query consumers and Profile entry/exit consumers.
  Player ownership, preload readiness, source geometry and editor preview fitting
  remain separate from this query change.
- Touched files:
  - Source/ComposableCameraSystem/Public/MeshCamera/ComposableCameraMeshSurfaceTypes.h
  - Source/ComposableCameraSystem/Public/MeshCamera/ComposableCameraMeshWorldSubsystem.h
  - Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshSurfaceTypes.cpp
  - Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshSurfaceStorageActor.cpp
  - Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshWorldSubsystem.cpp
  - Source/ComposableCameraSystem/Private/Tests/ComposableCameraMeshQueryTests.cpp
  - Docs/DesignDoc.md, TechDoc.md, EditorDesignDoc.md, ExecutionFlowExamples.md and BugLog.md
- Fix: validate policy, independently intersect the original downward segment with
  saved geometry/BVH, find the global nearest hit, and collect overlaps using a
  length capped by global nearest plus tolerance and the original maximum. Native
  optional distance output retains each Layer's nearest triangle hit and aligned
  Layer ordering; storage transforms each hit independently. Scene collision uses
  the identical original segment. A blocker above each native Layer by more than
  tolerance rejects it; same-height, nearby or lower blockers do not. Empty Channels
  skips physics and a valid trace miss retains geometry. Never relocate outputs to
  collision or retry a farther saved floor after blocking the nearest hit. NativeRay
  diagnostics expose the original hit distance and signed blocker separation.
- Regression tests:
  - ComposableCameraSystem.MeshCamera.IndependentRayIntersections: real StaticMesh
    blockers, independent nearest geometry, blocker separation, Profile/ignored
    Actor, geometry-only/no-blocker policies, original origin/endpoint boundaries,
    global document clipping, per-Layer rejection and nearest hit precision.
    Compares indexed/linear distance outputs and checks pruning and miss reset.
  - Updated BlockingSurfaceQuery and NextQueryDiagnostics for the chosen occlusion
    rule; ManualUpdateAndClear now uses an actually invalid channel for its invalid
    input cleanup case. Existing SpatialIndexEquivalenceAndPruning and
    SpatialIndexTransformedDocument retain index/transform/Undo regression coverage.
- Verification / blocker: source/consumer/format/whitespace review only. Project
  instructions prohibit command-line Unreal compilation or automation. Close UE,
  compile UE5_6Editor in Rider/Visual Studio and restart; header/native signature
  edits require a full IDE build, not Live Coding. Run IndependentRayIntersections,
  BlockingSurfaceQuery, NextQueryDiagnostics, ManualUpdateAndClear and the existing
  spatial-index tests. In PIE, query a painted lower floor with an unpainted blocking
  floor above it, then remove/ignore the blocker or use empty Channels. Inspect both
  boolean/array results and native SurfacePosition. Arm DebugNextQuery before the
  next real business call to inspect NativeRay and BlockedBeforeLayer evidence.
- Avoid next time: derive both intersections from the same unchanged caller ray,
  test its endpoints, and preserve each returned Layer's actual intersection.
  BVH visitation order and a document's nearest height do not prove another Layer's
  nearest hit. Clip global collection before querying per-document overlap sets.
- Possible conflicts / limits: empty Channels and scene misses now permit saved
  membership intentionally; businesses requiring occlusion must supply channels
  that Block relevant surfaces. Layer authoring Channel still does not populate
  runtime policy. Unknown Profile/invalid channels fail; no automatic player polling
  or implicit ignored Pawn is added. SurfaceTolerance groups nearby native hits
  and permits that much blocker separation, so large values can merge floors.
  Incorrectly baked heights need redraw or a deliberate tolerance. Normal queries
  use inline capacity 16 and BVH traversal; deeper overlap/large UE ignored lists
  retain their documented allocation limits. Two native queries per document add
  BVH traversals; no per-frame index construction, visualization or collision edits.

## 2026-10-06 - Geometry-only membership crosses an unpainted supporting floor

- Status: source, regression tests and living documents updated. IDE compilation
  and UE automation execution remain pending; no build/test run is claimed.
- Symptom: a character standing on an unpainted upper storey can activate a Layer
  painted only on a lower floor. Collision channels that ignore the upper floor,
  or empty Channels, make membership independent of the actual supporting floor.
- Exact trigger / repro: paint a Layer at Z=0, place a walkable upper floor at
  Z=100 without a Layer, stand above that floor, and query downward 300 cm using
  empty BlockingChannels. The former query selects the lower saved triangles.
- Why / root cause: nearest painted geometry is not current physical ground.
  The previous chosen occlusion policy accepted empty Channels/no scene blocker;
  absence of a configured blocker did not identify the surface supporting the
  character. Adding a GUID to painted triangles alone would not discover the
  unpainted upper floor. This turn replaces that former policy explicitly.
- History / blast radius: reviewed the earlier automatic-membership bug, empty
  query diagnostics, independent/per-Layer ray intersections, cross-component
  authoring seams, native BVH/Undo transforms and manual ownership tests. Audited
  all query consumers, Profile readiness/entry/exit, PC/PCM replacement/EndPlay,
  storage unload and editor visualization. Referenced read-only UE5.6
  CharacterMovement CurrentFloor/ImpactPoint, Recast/Detour bounded projection and
  GameplayCameras explicit player activation. Existing external edits preserved.
- Touched files:
  - Source/ComposableCameraSystem/Public/MeshCamera/ComposableCameraMeshSurfaceTypes.h
  - Source/ComposableCameraSystem/Public/MeshCamera/ComposableCameraMeshWorldSubsystem.h
  - Source/ComposableCameraSystem/Private/MeshCamera/ComposableCameraMeshWorldSubsystem.cpp
  - Source/ComposableCameraSystem/Private/Tests/ComposableCameraMeshQueryTests.cpp
  - Docs/DesignDoc.md, TechDoc.md, ExecutionFlowExamples.md, EditorDesignDoc.md, BugLog.md
- Fix: Query/Update now require business GroundHit and
  FComposableCameraMeshGroundQueryParams (SurfaceTolerance only). Remove old trace
  mode, BlockingChannels, CollisionProfile, MaxQueryDistance, bTraceComplex and
  IgnoredActors. Require valid blocking nonpenetrating ground and a live same-world
  component. Intersect exact saved triangles at ImpactPoint XY in the bounded
  ground Z +/- tolerance interval using existing native BVH. Group the entire
  interval, check every Layer's own intersection, and preserve scoped document
  identity/order. SurfacePosition stays the saved point; VerticalDistance reports
  absolute ground-height separation. Missing/invalid/unmatched ground returns empty
  and Update exits old effects. Clear and all Profile lifecycle handling remain
  caller-driven. No physics retrace, Pawn polling, SurfaceId/provenance fields or
  cooked triangle format/memory changes are introduced.
- Regression-test names:
  - ComposableCameraSystem.MeshCamera.GroundHitQuery: real business Pawn traces on
    two Actor StaticMesh floors, upper unpainted/painted isolation, capsule Sweep
    ImpactPoint versus Location, no scene retrace, public API/result reset,
    invalid/penetrating/expired/wrong-world ground and invalid/overflowed numerics.
  - ComposableCameraSystem.MeshCamera.GroundHitGeometry: symmetric inclusive height
    bounds, per-Layer points/distances, disabled coverage, exact zero tolerance,
    triangle-vs-AABB coverage, slope height and repeated/transformed documents.
  - ComposableCameraSystem.MeshCamera.NativeRayPrecision: retain indexed/linear
    nearest per-Layer distance agreement, BVH pruning and aligned miss-output reset.
    This replaces the obsolete world-occlusion IndependentRayIntersections test.
  - NextQueryDiagnostics: updated one-shot actual-ground outcomes and read-only
    height discovery. ManualUpdateAndClear: retained ownership tests plus missing
    GroundHit and invalid tolerance exit. GroundHitQuery replaces BlockingSurfaceQuery.
- Verification / concrete blocker: source/consumer/lifetime/whitespace review only.
  Project instructions prohibit shell Unreal builds/editor/automation. Close UE,
  build UE5_6Editor in Rider or Visual Studio, restart, and run the five tests above
  plus SpatialIndexEquivalenceAndPruning, SpatialIndexTransformedDocument,
  ExclusiveProfileDispatchAndCleanup and ProfilePreloadMembershipAndLifetime.
  USTRUCT/UFUNCTION changes require a full build/restart, not Live Coding.
  Refresh/recreate old Blueprint Query/Update and Make QueryParams nodes. Connect
  CurrentFloor.HitResult after WalkableFloor in ordinary Character Walking, or
  business ground from custom movement. Call Update after movement for the local
  PC; Clear on loss of ground/disable/unpossess according to business policy.
  In PIE verify upper unpainted floor stays inactive, painted floors trigger only
  their own Layers, jumping/invalid ground clears owned effects, and Show loading
  remains independent. Existing painted documents need no redraw or resave.
- Avoid next time: select supporting ground independently of tagged coverage.
  Test an unpainted floor above painted data, not just two painted surfaces. Keep
  each Layer's actual triangle intersection and test both sides of the ground
  interval; capsule center is not its ground contact point.
- Possible conflicts / limits: old Blueprint/C++ query signatures require explicit
  migration. GroundHit freshness, walkability and custom movement/airborne policy
  belong to business; a wrong hit can still produce a wrong Layer. Matching uses
  world-Z height and coverage, not Component ownership. Large tolerance can merge
  close storeys; collision/render discrepancies may need corrected authoring or
  deliberate tolerance. Steady <=16-Layer inline queries add no heap allocations;
  larger overlap/Blueprint output growth retain documented allocation limits.
  Layer authoring Channel, preview fitting/LOD, existing geometry and Profile
  dispatch/cleanup semantics remain unchanged.

## 2026-10-07 - Uneven Mesh Layer paint accumulates alpha and leaves small uncovered/buried patches

- Status: source fix and three focused regressions added. The user's Level has
  not been executed here; IDE compilation, automation and viewport verification
  remain pending. Numeric geometry checks are not an Unreal test pass.
- Symptom: one Layer shows irregular darker fragments on uneven Landscape;
  painting also leaves many small areas apparently uncovered. The screenshot
  alone cannot distinguish missing saved triangles from buried preview geometry.
- Trigger / repro: repeatedly Brush or Draw the same convex/concave ground,
  including overlapping Layers; compare Edit, Show and PIE after loading. Use a
  tiny sloped footprint whose actual overlap differs by less than 5 units but
  whose extrapolated grid-center planes differ by more than 5; also test a curved
  surface with 100-unit initial sample spacing against its actual floor height.
- Why / root cause: the display cache assigned whole patches to height buckets
  using one extrapolated center height, then subtracted entire XY footprints.
  It could either retain duplicate translucent coverage or remove separated
  coverage when the height band crossed a cell. Authoring tested seven locations
  for Draw support but emitted only corners, with no curvature-error check;
  Brush emitted a center/rim fan and traced along its center normal. Interiors
  could remain far from the floor, projection directions could shift neighboring
  XY samples, and one failed sample discarded a large otherwise supported leaf.
- History / blast radius: reviewed cross-component brush seams, accumulated
  alpha/Layer priority, exact preview silhouettes, regional erase/stroke caches,
  PIE character occlusion, progressive publication and Landscape LOD stability.
  Audited every FProjectedShapeBuild/BuildProjectedShape consumer, native cache
  reader, incremental/full resolver, saved/runtime preview export and Layer
  Channel tests. Preserve real holes, separate storeys, erase masks, transactions,
  cancellation, Profile ownership and the business GroundHit query contract.
- Touched files:
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerRendering.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerShapes.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/MeshCamera/ComposableCameraMeshLayerEdMode.cpp/.h
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerVisualizationTests.cpp
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerShapeTests.cpp
  - Source/ComposableCameraSystemEditor/Private/Tests/ComposableCameraMeshLayerTraceChannelTests.cpp
  - Docs/EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md, BugLog.md
- Fix: index all elevations in one XY cell and clip pairwise subtraction to the
  actual plane-height band abs(delta) <= 5 local units. The full-cell shortcut
  verifies all incoming heights. Refine seven-point corner-interpolation error
  above 1 world cm, converting Draw thresholds through the anchor's maximum
  absolute scale, with bounded depth/edge size/output; mixed support refines before
  omission while entirely missing floor stays absent. Emit curved leaves from
  projected centroids and recursively shared cached edge samples; planar leaves
  retain one triangle. Brush reuses this builder with document-Up traces and
  relative-world centimeter coordinates before conversion to document local,
  preserving small details far from the origin. Draw projection/emission keeps
  the existing per-frame budget; Brush has a 4096-triangle stamp cap and explicit
  density feedback. Failed density preserves the preceding source.
- Regression-test names:
  - ComposableCameraSystem.Editor.MeshCamera.VisualizationUnevenSurface
    checks repeated/sorted ownership, center-extrapolation false separation,
    exact partial height-band subtraction, coarse grids, stacked floors and
    runtime-data preview parity.
  - ComposableCameraSystem.Editor.MeshCamera.ShapeCurvatureAndSeams
    checks complete footprint area, native saved triangle intersections near the
    actual curved ground, unequal-refinement edge continuity, scaled centimeter
    bounds, query budgets, sample reuse, coverage beside a small true gap and
    failure preservation.
  - ComposableCameraSystem.Editor.MeshCamera.UnevenBrushProjection
    uses actual complex collision on a curved StaticMesh, the real Brush method,
    saved storage queries and a scaled far-origin anchor to check interior precision.
- Verification / blocker: source/consumer/lifetime review and git diff --check.
  An independent arithmetic check of the curved fixture conserved 10000 units^2,
  hit all 100 interior probes, reached 0.234375-unit maximum height error and
  found no different-height T junctions. This does not compile or run UE code.
  Project instructions prohibit shell Unreal builds/editor automation. Close UE,
  compile UE5_6Editor in Rider/VS, restart, run the three regressions plus
  ShapeProjectionAndLimits, ShapeProjectionBudget, LayerTraceChannel,
  IncrementalVisualization, VisualizationBoundary, VisualizationPartialOverlap,
  AsyncPreviewBuild and DocumentUndoRedo. Repaint the same rough patch using
  Brush and all Draw types, overlap another Layer, erase a tiny real hole, inspect
  near/far Show and PIE, and confirm characters still occlude PIE overlays.
- Avoid next time: compare surfaces at actual overlapping footprints, not a
  representative extrapolated center. Check saved-query height and shared-edge
  continuity in addition to total area. A support sample alone does not constrain
  interpolation error or prove the whole triangle follows the floor.
- Possible conflicts / limits: curved authoring produces more triangles/traces
  and can increase saved/baked geometry memory; no per-triangle fields, SurfaceId,
  runtime API or source format changes. Brush remains bounded synchronous event
  work, so frame cost requires the viewport smoke test. Sampled approximation
  cannot promise arbitrary sub-resolution detail; missing collision, short
  Projection Distance, steep normal rejection, unresolved discontinuities and
  collision/render mismatch can still leave legitimate omissions. Existing
  triangle-only Brush strokes need repainting for denser source, and previously
  missing triangles cannot be recovered by merely toggling Show. Existing Shape
  controls can regenerate source through an edit. Display refresh alone applies
  the height-band resolver. Stable Landscape LOD and 1.5-unit preview offset,
  depth testing, progressive startup and manual GroundHit Update/Clear stay intact.

## 2026-10-07 - Mesh Layer Edit keeps rebuilding unchanged viewport fill buffers

- Status: source optimization and three regressions added; IDE compilation,
  automation, real viewport pixels and frame timings remain unverified.
- Symptom: opening Mesh Layers Edit (Brush / Draw / Layers) substantially reduces
  frame rate, including idle editing. User requires current functionality and
  presentation to remain intact.
- Trigger / repro: hold the viewport/camera fixed over a large painted document;
  compare closed Edit, fully loaded Show and Edit with the mouse outside the
  viewport and no pending Shape. Repeat with dense curved authoring geometry.
- Why / root cause: coverage polygons were cached, but every Edit Render called
  DrawResolvedLayer for every enabled Layer. Each Layer scanned the entire cache
  twice, reconstructed FDynamicMeshBuilder vertices/tangents/indices and dynamic
  material resources, then uploaded disposable buffers. Target grid density is
  approximately 100000 cells rather than an output-triangle cap; exact boundary
  fragments and denser curved source amplify this repeated work. This code path
  establishes avoidable work, not a measured percentage of the user's frame.
- History / blast radius: reviewed prior brush regional-cache, release refresh,
  exact boundary/alpha ownership, persistent PIE/Show buffers, Game View hiding,
  reverse-face opacity and Landscape LOD entries. Audited every visualization
  mutation, worker-installed Shape cache, Undo/Discard/cancel, Layer ordering,
  control-point hit proxy and mode Exit. Consulted installed UE5.6 renderer APIs
  read-only. Preserve saved/runtime data, sampling/query/Profile logic and Show/PIE.
- Touched files: MeshCamera/ComposableCameraMeshLayerEditPreview.h/.cpp (new),
  ComposableCameraMeshLayerEdMode.h/.cpp, ComposableCameraMeshLayerRendering.h/.cpp,
  Tests/ComposableCameraMeshLayerEditPreviewTests.cpp (new), EditorDesignDoc.md,
  TechDoc.md, ExecutionFlowExamples.md and BugLog.md, all editor code/docs.
- Fix: Level-owned transient fill components retain vertex/index/factory resources.
  Partition by Layer and 32-cell XY tiles; refresh only dirty tiles after
  Brush/Erase, including their unchanged neighbors and removed cells. Full
  changes/regrids refresh all tiles. Stable Render does not traverse patches or
  create/upload a fill mesh. Keep separate CPU/GPU dirty flags, including ready
  Shape installation. Reuse GeomMaterial and exact float RGB/alpha with the same
  clamp, fan topology, normals/tangents, UVs, offset, depth testing and disabled
  backface culling. Keep drafts/controls/hover and picking on PDI. Exclude capture
  views, collision, selection, shadows/nav/ray tracing, PIE duplication and
  temporal primitive occlusion; Game View remains visible. Exit destroys the actor;
  scene ownership releases buffers safely. Failed publication retains PDI fallback.
- Regression tests: ComposableCameraSystem.Editor.MeshCamera.EditPreviewPersistentBuffers
  (actual buffers/proxy reuse, negative tiles, scaled anchor, float colors/alpha,
  sloped heights, Layer enable/reorder, G view/capture policy, clean package and
  teardown); EditPreviewRegionalUpdates (erase/restoration and remote buffer
  identity, empty cache); EditPreviewInvalidation (actual mode idle Render creates
  no PDI fill resources, full dirty flags and color refresh).
- Verification / blocker: static consumer/resource-lifetime/API review and
  git diff --check only. Project rules prohibit shell builds/editor/tests. Close
  UE, compile UE5_6Editor in Rider/VS and restart: new reflected component requires
  full compilation. Run the three regressions plus DocumentUndoRedo,
  StrokeVisualizationRefresh, DiscardWorkingDocument, VisualizationUnevenSurface,
  EditorPreviewPublication and PreviewLandscapeLODStability. Manually compare
  fixed-view FPS and near/far pixels; Brush, all Draw types, Select/control drag,
  Erase/Shift, undo/redo, Discard/save, Layer reorder/color/enable, G and Edit/Show/PIE
  switching must retain current behavior. Look for ghosts or duplicate opacity.
- Avoid next time: caching CPU geometry does not imply persistent GPU buffers.
  Test actual proxy/buffer identity and idle draw resource counts, not cache flags
  alone; treat already-resolved asynchronous results as separate publication work.
- Possible conflicts / limits: cached CPU/GPU geometry consumes retained memory
  and spatial tiles add draw batches. First-open coverage and mutation-time tile
  uploads remain synchronous; Brush sampling and regional resolver scans remain
  as before. No mesh density/LOD/material asset/runtime API reduction is used.
  Pixels and timing must be confirmed in the user's Level before claiming parity
  or a specific speedup.

## 2026-10-07 - Mesh Layer Edit preview lacks builder definition and shadows Bounds

- Symptom: IDE compilation fails in ComposableCameraMeshLayerEditPreview.cpp
  with C2079 for FPrimitiveUniformShaderParametersBuilder, cascading C2664/C2665
  at BuildUniformShaderParameters/Uniform.Set, and C4458 for a local Bounds.
- Trigger / repro: compile UE5_6Editor in Rider or Visual Studio after adding
  the persistent Edit scene proxy, with inherited-member shadowing treated as
  an error and no incidental unity include supplying the builder definition.
- Why / root cause: PrimitiveSceneProxy.h and SceneManagement.h only
  forward-declare the builder. Its defining header was not included at the
  construction site. The component's local geometry box reused the inherited
  USceneComponent::Bounds name.
- History / blast radius: checked the 2026-07-19 mesh editor mode incomplete
  type/shadowing bug and all Edit preview callers. SetGeometry callers, scene
  proxy resource lifetime, render parameters and runtime queries are unchanged.
- Touched files: Source/ComposableCameraSystemEditor/Private/MeshCamera/
  ComposableCameraMeshLayerEditPreview.cpp; Docs/TechDoc.md; Docs/BugLog.md.
- Fix: include UE5.6's PrimitiveUniformShaderParametersBuilder.h directly and
  rename the local box to VertexBounds. No rendering or bounds math changes.
- Regression-test name: ComposableCameraSystemEditor compile: Edit preview
  builder include completeness and inherited-member shadowing.
- Test blocker / verification: these are compile-time failures before automation
  can load. Project rules prohibit shell builds. Rebuild UE5_6Editor in Rider/VS;
  where available also compile this translation unit without unity includes.
  Then run EditPreviewPersistentBuffers, EditPreviewRegionalUpdates and
  EditPreviewInvalidation in the editor. No IDE compile pass is claimed yet.
- Avoid next time: inspect the defining header, not only forward declarations or
  function signatures; audit base-class member names when adding component locals.
- Possible conflicts: none expected; only type visibility and local naming
  changed. Coverage, colors, transforms, tools and persistent-buffer behavior
  remain the same.

## 2026-10-07 - Show Mesh Layers does not resume after closing Edit

- Symptom: Show is checked but no Layer overlay returns after Edit closes;
  entering Edit from the tool menu can instead clear the previously enabled Show.
- Trigger / repro: enable Show Mesh Layers, enter Edit Mesh Layers from the menu
  or mode selector, then close its panel or deactivate Edit. Observe checked
  state and actual filled meshes after normal progressive loading.
- Why / root cause: Show intent and the actual read-only mode are separate.
  Mutual exclusion deactivates the Preview mode and releases its geometry, but
  there was no restoration path. The Edit menu also explicitly cleared intent.
  UE5.6 DeactivateMode removes the active flag before deferred Exit, so restoring
  solely when IsModeActive(Edit) becomes false can race document save/discard
  and actor cleanup.
- History / blast radius: checked the 2026-07-19 one-click Edit-to-Show bug,
  2026-07-21 independent PIE request state, Game View publication and persistent
  Edit buffer lifecycle entries. Audited tool menus, Window Debugging's shared
  toggle/check callbacks, mode selector/primary-tab close, registration/unload,
  PIE fitting teardown and scoped Landscape view routing.
- Touched files: Utilities/ComposableCameraMeshLayerTool.h/.cpp,
  MeshCamera/ComposableCameraMeshLayerEdMode.cpp,
  Tests/ComposableCameraMeshLayerPreviewModeTests.cpp in the editor module;
  Docs/EditorDesignDoc.md, Docs/TechDoc.md, Docs/ExecutionFlowExamples.md and this log.
- Fix: retain Show intent on Edit entry; notify the coordinator on actual Edit
  Enter/Exit. Suspend preview view routing and release PIE caches during Edit.
  The existing preview ticker restores the missing read-only mode only after
  full Edit cleanup, outside the mode-manager Exit stack. Explicit Show-off
  during Edit cancels resumption. Preview-on still closes Edit with one command.
- Regression test: ComposableCameraSystem.Editor.MeshCamera.PreviewModeResumeAfterEdit.
  Uses real registered modes and menu/selector/toolkit-close paths; covers
  checked intent, no duplicate read-only fill during Edit, the pending-Exit
  interval, repeated restoration, explicit off and one-click Show-on.
- Verification / blocker: static lifecycle/caller review and git diff --check;
  no compile or automation pass claimed. Project rules prohibit shell builds.
  Close UE, compile UE5_6Editor in Rider/VS, restart, and run the regression in
  an idle Level Editor outside PIE with Show off. Compare actual filled meshes
  through Show -> Edit -> close and repeat; verify Save vs discard, G view,
  Window Debugging toggle synchronization, Show off during Edit, and subsequent
  PIE sessions. Run existing EditPreview and PreviewLandscapeLODStability tests.
- Avoid next time: represent a visualization request independently of the
  temporary editor mode; inspect pending-deactivation lifecycle, not only active
  flags. Test real mode transitions as well as isolated geometry caches.
- Possible conflicts / limits: Show remains checked while its read-only backend
  is paused for Edit. Resumption uses saved data and the existing asynchronous
  build/publication budgets, so fill returns progressively. Runtime Layer query,
  Profile effects, materials, coverage and Edit tools remain unchanged.

## 2026-10-07 - Mesh Layer Brush/Erase repeats whole-source work while painting

- Symptom: Edit idle frame rate is acceptable after persistent fill buffering,
  but Brush/Erase frequently stalls while applying stamps to the ground.
- Trigger / repro: open Mesh Layers Edit in a dense Level, then hold and drag
  Brush or Erase across uneven Landscape/StaticMesh surfaces with many existing
  triangles, including remote stamps and overlapping Layers.
- Why / root cause: regional visualization retained remote cells, but each stamp
  still scanned all source triangles for bounds and again for regional candidates.
  Brush additionally discarded and re-resolved nearby old coverage despite only
  appending geometry. Erase still visited every source triangle before its exact
  broad phase. Curvature refinement increases this repeated work. These are
  code-path findings; no timing trace or measured speedup is claimed.
- History / blast radius: checked previous regional-cell caching, local erase
  footprint, disjoint-patch rejection, release-cache preservation and persistent
  Edit GPU-buffer entries. Audited all geometry/resolver callers, source mutation
  and undo/save paths, tile refresh and runtime preview builders. Retain exact
  projection, surface-height bands, Layer priority, source order and Shape masks.
- Touched files: MeshCamera/ComposableCameraMeshLayerAuthoringIndex.h/.cpp (new),
  ComposableCameraMeshLayerEdMode.h/.cpp, ComposableCameraMeshLayerRendering.h/.cpp,
  ComposableCameraMeshLayerShapes.h/.cpp; Tests/ComposableCameraMeshLayerShapeTests.cpp
  and ComposableCameraMeshLayerVisualizationTests.cpp in the editor module;
  Docs/EditorDesignDoc.md, Docs/TechDoc.md, Docs/ExecutionFlowExamples.md and this log.
- Fix: retain native per-Layer bounds in 128-triangle source blocks. Brush merges
  only appended triangles into existing cells and refreshes tail blocks. Erase
  tests candidate blocks conservatively in brush space, runs unchanged exact
  clipping in descending source order, then refreshes removal/swap-tail/fragment
  blocks. Regional resolution visits only candidate blocks covering whole dirty
  cells; block bounds replace per-stamp full vertex-bound scans. Full invalidation
  and grid growth retain complete rebuild fallback. Source replacement, undo,
  cancel, Shape changes and compaction reset the index even with identical counts.
- Regression-test names: ComposableCameraSystem.Editor.MeshCamera.BrushAppendCoverage,
  IndexedEraseCoverage and IndexedEraseEquivalence. Cover repeated append/full
  equivalence, slopes, stacked/disabled Layers, stable remote cells, grid growth,
  small local erase/lower-Layer reveal, same-count restoration, exact source-array
  equality under rotated/scaled/sheared erasers, repeated no-op cuts, retained
  Shape masks and complete source removal.
- Verification / blocker: static caller/invalidation/order review and
  git diff --check; no IDE compile or automation run yet. Project rules prohibit
  shell builds/editor tests. Close UE, compile UE5_6Editor in Rider/VS and restart
  because the native mode header changed. Run the new regressions plus
  IncrementalVisualization, EraseLocalVisualization, StrokeVisualizationRefresh,
  UnevenBrushProjection, DocumentUndoRedo, DiscardWorkingDocument and existing
  EditPreview tests. In the same dense Level, compare Brush/Erase dragging,
  Shift-erase, Layer overlap, stacked floors, boundaries, undo/redo, cancel,
  Save/Discard and Show -> Edit -> close behavior. Record Insights scopes if
  stalls remain; distinguish projection, coverage, index refresh and upload work.
- Avoid next time: local output invalidation alone does not bound input traversal.
  Test how many source triangles are visited and compare full/unindexed results.
  Preserve source order in a broad phase and invalidate native indexes for
  same-count rewrites; bounds caches are never authoritative authoring data.
- Possible conflicts / limits: the index adds editor-only retained memory and
  candidate scratch allocation. Broad block boxes can include remote triangles,
  so worst-case scattered source still approaches a full scan. Initial cache
  construction, collision sampling, exact local clipping/resolution, stroke
  snapshots and GPU tile uploads remain synchronous. No sampling/quality,
  serialized Layer data, runtime ground query or Profile behavior changes.

## 2026-10-07 - Show Mesh Layers loads slowly and static viewports delay publication

- Symptom: Show waits a long time before complete display; apparent recovery
  after moving/approaching with the camera. User first reported PIE missing,
  then clarified it eventually displays but loads too slowly and may need movement.
- Trigger / repro: enable Show in a static/non-realtime Level viewport, particularly
  while Slate throttles expensive tasks. Enter PIE with the same large documents,
  or toggle Show off/on; keep the editor/PIE camera fixed throughout preparation.
- Why / root cause: editor adoption/publication ran only through viewport EdMode
  Tick. UE5.6 UEditorEngine::Tick skips visible viewport ticks under Slate
  throttling unless bNeedsRedraw is already set. Worker completion itself did not
  invalidate a viewport, so loading could wait for camera input. Publication
  redraw also had no follow-up for deferred render updates. Independently, fixed
  caps of two uploads/256 floor queries stopped cheap work despite unused 2/4 ms
  budgets. Every editor/PIE/toggle request repeated identical coverage clipping
  and export on the serial worker. No camera-distance check exists in this loader.
  These are source findings; no measured first-show timing or pixel capture is claimed.
- History / blast radius: checked initial-hitch, progressive first-chunk, fair
  scheduling, fitted-tail completion, Game View, reverse-face, Landscape LOD,
  LevelBlock slab and Edit-to-Show resumption entries. Audited module registration/
  unload, core/viewport tick callers, both publication loops, snapshot ownership,
  cancellation, independent editor vs PIE-ending state and all async result consumers.
- Touched files: editor MeshCamera/ComposableCameraMeshLayerPreviewEdMode.h/.cpp,
  ComposableCameraMeshLayerPreviewBuild.h/.cpp, ComposableCameraMeshLayerPIEPreview.h,
  Utilities/ComposableCameraMeshLayerTool.cpp,
  Tests/ComposableCameraMeshLayerPreviewModeTests.cpp and
  Tests/ComposableCameraMeshLayerPreviewBuildTests.cpp; Docs/DesignDoc.md,
  EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and this log.
- Fix: core Show ticker also advances editor results through the frame-guarded
  AdvancePreview; viewport Tick shares that guard. Publication requests redraw
  and two subsequent static draws, then stops after completion. PIE-ending gates
  only PIE work. Keep soft 2 ms publication/4 ms fitting and 64-query per-document
  slices; raise safety caps to 16 chunks and 2048 queries so spare time is useful.
  The owned worker caches exact resolved local meshes with four-entry LRU/64 MiB
  retained-buffer limits. Matching compares complete geometry and required Layer
  metadata, not counts or pointer identity. Each caller gets a copy; PIE still
  fits to its World, and world-specific fitted data never enter the cache. Native
  cache access stays on that single worker; clear only after joining on unload.
  Explicit DumpPIEPreview also reports geometry reuse and elapsed preparation/
  adoption wait, including worker queue time, to separate remaining startup stages.
- Regression-test names: ComposableCameraSystem.Editor.MeshCamera.StationaryPreviewPublication,
  PreviewPublicationBudget and PreviewGeometryCacheInvalidation; AsyncPreviewBuild
  now checks editor-to-PIE reuse. Tests cover native components/viewport invalidation
  without viewport Tick or camera movement, shared-frame guarding, all tails and
  redraw quiescence; cheap throughput vs expensive time expiry; same-count vertex,
  index, triangle ownership, color and enabled-state changes vs full reference meshes.
- Verification / blocker: static UE5.6 API/caller/lifetime review and git diff
  --check only; no IDE compile/automation pass or timed speedup claimed. Project
  rules prohibit shell builds/editor tests. Close UE, compile UE5_6Editor in
  Rider/VS and restart (native mode/build headers changed). Run the three new
  regressions plus AsyncPreviewBuild, PreviewModeResumeAfterEdit,
  EditorPreviewPublication, PIEPreviewScheduling, PIEPreviewProgressiveMeshes and
  PreviewLandscapeLODStability. Hold the view fixed with realtime off/on and G
  off/on; compare first cold Show, repeated toggles, editor -> PIE, Show enabled
  during PIE, stationary LevelBlock and distant uneven Landscape. Check complete
  coverage/depth, Show -> Edit -> close, PIE exit/reentry and off before readiness.
- Avoid next time: worker completion must have a consumer independent of a
  render/input event. Budget elapsed work separately from safety caps; verify
  cheap work can actually use its time. Share immutable geometry only after exact
  content validation, while keeping projection/collision tied to the actual World.
- Possible conflicts / limits: bounded native cache retains editor memory after
  Show off, without retaining actors/Profiles/Worlds. Cold, changed, evicted or
  oversized data still resolve on the worker, and native conversion/floor fitting
  still take time. No density, coverage, clipping, floor eligibility, material,
  Landscape LOD, serialized data, runtime Layer query or camera effects change.
  A single physics query/component registration can exceed the soft budget.

## 2026-10-07 - Show waits for complete background geometry before its first mesh

- Symptom / trigger: cold Show Mesh Layers in Editor or PIE waits many seconds
  before any fill appears, even with progressive component creation and no camera
  movement. Entering PIE while editor preparation is unfinished can also queue
  active-world work behind redundant editor work.
- Source root: C:/Users/Sulley/Documents/Unreal Projects/UE5_6/Plugins/ComposableCameraSystem.
  Verified UE5_6.uproject EngineAssociation 5.6 and installed UE_5.6 Build.version
  5.6.1. Generated/engine files are reference only.
- Root cause: TakeResult could consume only a ready whole-document future.
  Background clipping, priority resolution, export, native conversion and cached
  output copying all preceded first adoption. Moving full work off the game thread
  removed a hitch but retained long blank startup. The single worker also serialized
  unfinished editor requests before PIE requests.
- History / blast radius: reviewed initial Show hitch, static viewport redraw,
  progressive PIE chunks/tails, fair fitting/publication, editor backfaces/Game View,
  Edit-to-Show restoration, Landscape LOD, LevelBlock fitting and unload ownership.
  Audited all Build.Begin/TakeResult/Projection.Begin consumers and cancellation.
- Fix: resolve disjoint 32x32-cell tiles using the original global cell-size rule,
  source order, clipping and coverage resolver. Bounds/candidate binning precede
  the first tile; complete-document clipping does not. First try an occupied
  8x8-cell final region near the view, excluding its cells from the tile remainder.
  Capture the view origin
  for nearest-first order only, never distance culling. Before PIE's first camera
  update, use the possessed Pawn's position instead of its empty camera cache.
  This changes preview ordering only, never business Query/Update. Enqueue each final tile mesh
  on a per-job SPSC queue; editor appends unsubmitted native meshes, PIE fits/publishes
  one batch at a time with a worker-preallocated hash. A distinct terminal result
  closes the job after every queued batch and retains full triangle accounting.
  Dispose abandoned output queues on the owned worker after cancellation, keeping
  large native mesh destruction out of preview Tick; shutdown joins disposal work.
  Recheck dequeue after future readiness to avoid a completion race. Cache entries
  distinguish tiled/reference layouts, retain mesh centers and stream copies in the
  new view's order. Preserve the document's already-published flag across PIE batches
  so small startup chunks do not multiply. While PlayWorld exists, cancel pending
  editor jobs, retain displayed components and restart interrupted documents on return.
  Existing complete caches and runtime Layer queries are unchanged. FirstGeometrySec
  measures first PIE batch adoption; -1 means no geometry batch has arrived.
- Files: MeshCamera/ComposableCameraMeshLayerRendering.h/.cpp,
  ComposableCameraMeshLayerPreviewBuild.h/.cpp, ComposableCameraMeshLayerPreviewEdMode.h/.cpp,
  ComposableCameraMeshLayerPIEPreview.h/.cpp, Utilities/ComposableCameraMeshLayerTool.cpp,
  Tests/ComposableCameraMeshLayerPreviewBuildTests.cpp,
  Tests/ComposableCameraMeshLayerVisualizationTests.cpp and the four design/flow docs.
- Regression: StreamingPreviewGeometry compares the triangle multiset with complete
  resolution on slopes crossing the same-surface tolerance, overlapping paint,
  distinct storeys, negative tile seams and disabled bounds. It covers view order,
  cancellation after first tile, snapshots, partial-before-terminal results and
  editor-to-PIE cached streaming. PIEPreviewProgressiveMeshes checks a later batch
  retains the regular chunk size. Existing async/cache/native/static-preview tests
  remain on their original default complete-reference path.
- Verification: git diff --check and static UE5.6 API/consumer/ownership review.
  No compile, automation pass, pixel result or first-show timing claimed. Repository
  AGENTS.md requires Rider/Visual Studio compilation and forbids shell builds/tests.
  Close UE, compile UE5_6Editor completely in the IDE, restart, run StreamingPreviewGeometry,
  PIEPreviewProgressiveMeshes, AsyncPreviewBuild, PreviewGeometryCacheInvalidation,
  StationaryPreviewPublication and PreviewModeResumeAfterEdit. Keep cameras fixed:
  test cold/repeated Show with G and realtime off/on; enter PIE immediately during
  cold editor loading; exit/re-enter; toggle off before completion; inspect uneven
  Landscape, LevelBlock, overlapping Layers and level-streaming cleanup. Compare
  first visibility and complete coverage; DumpPIEPreview should end with Pending=0
  and matching submitted/expected counts.
- Avoid next time: asynchronous completion is not progressive delivery. A ready
  output queue must carry independently final regions before the terminal future;
  do not create provisional colors requiring later whole-document replacement.
- Limits / conflicts: bounds/binning, queued earlier documents and first-tile cost
  remain; no guaranteed millisecond timing without measuring the user's map.
  Spatial tile tails can add components; per-batch hashes repeat boundary queries
  instead of growing a document-sized hash on the game thread. The existing 2/4 ms
  budgets remain soft. No density, source topology, floor eligibility, material,
  Landscape LOD, serialized schema or business Query/Update/Clear contract change.

## 2026-10-07 - Continuous Brush/Erase still stalls on synchronous stamp work

- Symptom / trigger: Show/PIE loading is now satisfactory, but holding and moving
  Brush or Erase over dense/uneven floor coverage produces repeated editor hitches.
- Root cause: spatial bounds and persistent idle buffers removed earlier repeated
  full-source/per-render work; mouse events still synchronously finished every
  adaptive projection, exact prism cut, cell coverage update and whole affected
  tile assembly. Multiple input events could repeat that work before one frame.
  These are source-path findings, not captured timing percentages.
- History / blast radius: checked prior Brush/Erase indexing, uneven-surface seams,
  height-band coverage, local erasure, release-cache preservation, transactions,
  persistent Edit fill and Show startup fixes. Audited geometry/resolver callers,
  Enter/Exit/Tick/input, Save/Discard, tool/Layer/Shape changes and PostUndo. Runtime
  ground query, SurfaceId data, Profiles and Show/PIE construction are untouched.
- Official reference: installed read-only UE 5.6.1 LandscapeEditor/Private/
  LandscapeEdModeTools.h queues interactor positions in MouseMove, applies in Tick
  and flushes in EndTool. MeshModelingTools/Private/Sculpting/MeshSculptToolBase.cpp
  and MeshVertexSculptTool.cpp separate pending drag state, Tick stamps and regional
  render notification. Their scheduling/ROI patterns inform this implementation;
  no reference source is copied and no engine files are edited. Keep every existing
  spacing-qualified sample here rather than the sculpt tool's latest-ray overwrite.
- Fix: capture every accepted stamp's point/normal, Layer/channel, radius/segments,
  projection/floor-normal settings and temporary erase state. Advance FIFO once per
  GFrameCounter with a shared soft 4 ms budget. Reuse the exact projection builder,
  resume erasure between descending original candidates and coverage between clear/
  triangle/cell operations. Preserve exact source order, rounding, masks, normal
  filtering, 4096 cap, 1 cm curvature requirement, 35%-radius spacing and height/Layer
  competition. Render does not rebuild partially owned native data. Assemble current
  32-cell tiles incrementally; retain old fill until a complete tile replaces it.
  Full/regional cell ordering stays as before. Exact visible attribute/color equality
  skips redundant proxy/buffer replacement. Normal release finishes its queue over
  ticks before closing the one transaction; explicit Save/tool/focus/close boundaries
  flush, while Esc/Discard cancel and restore the whole checkpoint. PostUndo discards
  pending work without restoring a stale checkpoint over the undone UObject.
  Settings' native pre-edit delegate flushes before Layer/options mutation and
  before add/delete/reorder/toggle transactions. Post-change-only notification
  would otherwise move Layer indices under pending coverage or merge the new edit
  into the unfinished stroke transaction. Undo restoration bypasses this boundary.
- Touched files: editor MeshCamera/ComposableCameraMeshLayerEdMode.h/.cpp,
  ComposableCameraMeshLayerShapes.h/.cpp, ComposableCameraMeshLayerRendering.h/.cpp,
  ComposableCameraMeshLayerEditPreview.h/.cpp, ComposableCameraMeshLayerToolSettings.h/.cpp,
  ComposableCameraMeshLayerModeToolkit.cpp; Tests/ComposableCameraMeshLayerBudgetedStrokeTests.cpp
  (new), ComposableCameraMeshLayerVisualizationTests.cpp, ComposableCameraMeshLayerEditPreviewTests.cpp;
  Docs/EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and this log.
- Regression tests: BudgetedStroke (mixed custom-channel StaticMesh Brush/Erase,
  preserved options/spacing/source arrays, released completion, cancellation after
  a partial cut, explicit flush, Undo/Redo, pre-reorder completion and separate
  stroke/Layer Undo); BudgetedCoverage (independent complete resolver,
  tiny slices, slopes/overlap/stacked floors, exact erase source/Shape masks, lower-Layer reveal,
  growth regrid and disabled Layers); BudgetedEditPreview (negative coordinates,
  old fill until completion, no-op buffer reuse, linear color, empty/cancelled updates).
- Verification / blocker: source/reference review and git diff --check. No IDE
  compile, automation pass or actual Level timing trace yet: project AGENTS requires
  Rider/Visual Studio compilation and forbids shell builds/editor tests. Close UE,
  compile UE5_6Editor and restart because native headers changed. Run the three new
  tests plus StrokeVisualizationRefresh, DocumentUndoRedo, DiscardWorkingDocument,
  UnevenBrushProjection, IndexedEraseEquivalence, IndexedEraseCoverage and EditPreview
  tests. Smoke-test long fast Brush/Erase drags on Landscape/StaticMesh, Shift changes,
  release/cancel/focus/tool switch, Undo/Redo, Save/Discard and Show/Edit/PIE toggling.
- Avoid next time: do not turn an exhausted finite budget into zero (builders treat
  zero as unlimited). Preserve complete FIFO input and transaction boundaries when
  moving mouse work to Tick. A source/index/coverage operation must finish or cancel
  before another document mutation; ordinary Render cannot invalidate partial work.
- Limits / conflicts: checkpoints, index/candidate setup and explicit boundary
  flushing remain synchronous. A single query/polygon operation or completed tile
  upload can exceed the soft budget. Pending release work can trail the cursor on
  very complex geometry; it remains visible in the document status and is not dropped.
  Added native tasks/scratch are editor authoring allocations, not camera evaluation.
  Actual frame time and unchanged pixels still require target-Level IDE/editor checks.

## 2026-10-07 - Budgeted Brush/Erase develops excessive display latency

- Symptom / trigger: after the initial hitch fix, sustained Brush/Erase input has
  conspicuous delayed fill and continues playing old work after the cursor moves.
- Root cause: each exact source stamp was serialized through projection, clipping,
  coverage and complete affected-tile assembly/publication under the same 4 ms
  budget. The next stamp could not begin until every preview tile of the previous
  stamp finished. Repeated updates of the same tiles produced a display FIFO,
  reducing authoring throughput instead of only distributing frame cost. No actual
  target-Level timing trace was captured; this dependency is confirmed in source.
- History / blast radius: reviewed the immediately preceding budgeted-stroke fix,
  persistent Edit buffers, local coverage/erase indexing, uneven-surface fidelity,
  progressive Show/PIE startup and pre-Layer-edit transaction fixes. Audited the
  preview APIs, mode Tick/input/Render, release/flush/cancel, Undo/Discard and worker
  teardown. Official read-only UE5.6.1 MeshVertexSculptTool.cpp demonstrates separate
  asynchronous native computation and regional render notifications; its pending
  cursor overwrite is still unsuitable for preserving our accepted source edits.
- Fix: remove the serial Preview phase. After coverage completes, capture complete
  affected-tile cells/polygons in native owned snapshots and permit the next source
  stamp immediately. A waiting tile keeps only its latest complete display snapshot;
  every source stamp remains FIFO with unchanged options/geometry. Dispatch one
  tile worker immediately after capture; it holds only cells and enabled flags,
  assembles exact fan vertices/indices and has no World/UObject/mode access or callback.
  Prioritize waiting tiles near the latest completed edit. Tick services ready
  publication before more source work (a quarter of the same soft frame budget),
  and with remaining time afterward. Finite calls never wait for a future. Preserve
  the existing complete-tile publisher, colors/normals/offset, no-op buffers and
  full/regional cell order. An active version may finish before its latest successor
  to prevent continuous input from endlessly restarting a tile. Normal release
  closes its transaction once source finishes; independent Tick publication keeps
  progressing without holding the next press behind a display flush. Explicit boundaries flush the
  one active future and remaining native snapshots before changing/saving data.
  Cancel/regrid/ordinary full updates detach obsolete work without joining or
  allowing any later result to resurrect cancelled geometry. Render cannot replace
  the queued publication with a synchronous cache rebuild.
  A shared native atomic lifetime flag also stops detached workers between
  cells/patches; cancellation never waits for the job or leaves a UObject reference.
- Touched files: editor MeshCamera/ComposableCameraMeshLayerEdMode.cpp,
  ComposableCameraMeshLayerEdMode.h, ComposableCameraMeshLayerEditPreview.h/.cpp,
  Tests/ComposableCameraMeshLayerEditPreviewTests.cpp and ComposableCameraMeshLayerBudgetedStrokeTests.cpp;
  Docs/EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and this log.
- Regression: QueuedEditPreviewSnapshots checks waiting-version coalescing, one
  active plus one latest successor, snapshot ownership while live coverage resets,
  real worker dispatch/nonblocking finite calls, exact vertex/normal/index/color
  output, cancellation and empty-tile removal. Existing BudgetedStroke retains
  mixed Brush/Erase source-array equivalence, spacing/options, cancellation,
  Undo/Redo and separate Layer transactions, and now checks source-complete release
  and a new press do not drain display work. BudgetedCoverage retains resolver
  equivalence; legacy BudgetedEditPreview still covers resumable publication.
- Verification / blocker: source/consumer/lifetime review and git diff --check.
  Compilation and editor automation require Rider/Visual Studio under project
  AGENTS.md; not run here. Close UE, compile UE5_6Editor, restart (native header
  changed), run the four tests above and smoke-test continuous/fast Brush, Erase,
  Shift transitions, release, Esc/Undo/Redo/Save/Discard and Layer reordering. Check
  Show/Edit/PIE remains intact. Inspect CCS_MeshLayers_EditTileSnapshot,
  EditTileWorker and EditTilePublication in Insights alongside StrokeTick and
  BrushProjection/EraseGeometry/CoverageUpdate to measure remaining queue latency.
- Avoid next time: asynchronous/budgeted work must improve throughput, not create
  a mandatory render barrier after every input sample. Coalesce only derived
  complete display states, never authoritative source edits. A background job
  must own its polygons and cannot refer to a mutating visualization/index table.
- Limits / conflicts: source projection/erase/coverage remain exact budgeted
  editor-thread work; an extremely complex stamp can still trail input. Snapshot
  capture and engine buffer publication remain indivisible. Waiting snapshots
  duplicate touched resolved polygons temporarily, bounded to one latest version
  per tile plus an active job. Full regrid can still duplicate the full cache.
  Actual no-hitch/low-latency behavior requires profiling the user's Level; no
  guaranteed timings or pixel comparison are claimed. No runtime query/API,
  SurfaceId schema, source sampling/spacing/priority or Show/PIE loading change.

## 2026-10-07 - Brush/Erase still trails input after asynchronous tile assembly

- Symptom / repro: sustained Brush and Erase both display substantial latency at
  approximately 150 cm radius after the preceding asynchronous-preview fix. Open
  Mesh Layers Edit on populated uneven ground, hold/drag either tool, and watch
  colored coverage lag behind input. The user reports similar delay for both.
- Why / root cause: only tile vertex/index assembly had moved off-thread. Each
  source stamp still waited for its Coverage phase, which cleared/rasterized cells
  under the same 4 ms editor budget before the next stamp could start. Dense
  overlaps therefore turned low frame work into a long FIFO source delay. A second
  scheduling cost came from dispatching one display tile per worker/frame cycle,
  including the neighboring tiles touched by an ordinary-sized stamp. These are
  confirmed control-flow bottlenecks; their time shares have not been measured.
- History / blast radius: reviewed the earlier uneven-surface/alpha fixes, source
  indexing and append/local Erase caches, BudgetedStroke/Coverage, prior tile
  snapshots, cancellation/Undo, transactions and explicit Save/Layer boundaries.
  Audited all resolver Begin callers and both stroke phases. Show/Edit/PIE lifecycle,
  persisted geometry, SurfaceId, business ground-hit queries and camera hot paths
  retain their previous implementation.
- Fix: remove Coverage from the authoritative stroke task. After a complete source
  mutation, capture only appended triangles or indexed whole-dirty-cell candidates
  plus document bounds and original planned grid size. FMeshLayerStrokeCoverage
  transfers cache ownership to a native worker and runs the unchanged resolver
  outside the editor frame budget. Adjacent append-only inputs merge in original
  order; mixed Brush/Erase retains each snapshot/operation separately, because
  resolving their union from the latest source could change same-Layer heights.
  Full rebuilds supersede obsolete waiting operations. Every source mutation still
  records grid growth, including growth followed by Erase before publication.
  An empty local snapshot removes only local fill; empty document clears all fill.
  Completed coverage batches publish progressively and do not retain a source
  transaction or block the next press. Background work owns no UObject/World/mode/
  source/index references and cancellation drops results without callbacks or waits.
  Display assembly dispatches up to four nearest tiles together; ready tile uploads
  retain the existing budget, exact attributes and per-tile atomic replacement.
- Touched files: MeshCamera/ComposableCameraMeshLayerStrokeCoverage.h/.cpp (new),
  ComposableCameraMeshLayerEdMode.h/.cpp, ComposableCameraMeshLayerRendering.h/.cpp,
  ComposableCameraMeshLayerEditPreview.h/.cpp; Tests/ComposableCameraMeshLayerVisualizationTests.cpp,
  ComposableCameraMeshLayerBudgetedStrokeTests.cpp, ComposableCameraMeshLayerEditPreviewTests.cpp;
  Docs/EditorDesignDoc.md, TechDoc.md, ExecutionFlowExamples.md and this log.
- Regression tests: AsyncStrokeCoverage compares ordered mixed and merged append
  operations with the original synchronous resolver, including slopes, same-Layer
  height differences, stacked/disabled Layers, local snapshot counts, source/index
  replacement, empty regional/whole source, cancellation and transient growth.
  QueuedEditPreviewBatch checks four tiles start together and retain exact geometry,
  color, normals and indices. BudgetedStroke now checks finite source work ends its
  transaction while background coverage remains, the next press retains that work,
  and explicit boundaries drain both coverage and fill; existing source/Undo checks
  remain. QueuedEditPreviewSnapshots retains successor/coalescing/cancel coverage.
- Verification / blocker: consumer/lifetime/operation-order review and static diff
  checks only. Project AGENTS.md requires Rider/Visual Studio compilation and
  editor-side automation; compilation, tests and measured latency were not run.
  Close UE, compile UE5_6Editor and fully restart (native headers/new source files).
  Run tests above plus BudgetedCoverage, UnevenBrushProjection, IndexedEraseCoverage,
  IndexedEraseEquivalence, StrokeVisualizationRefresh, DocumentUndoRedo and
  DiscardWorkingDocument. Smoke-test continuous 150 cm Brush/Erase, Shift changes,
  rapid alternation, release/new press, Esc/Undo/Redo/Save/Discard/Layer reorder,
  and Show/Edit/PIE. Insights scopes StrokeCoverageSnapshot/StrokeCoverageWorker,
  StrokeTick, BrushProjection, EraseGeometry, EditTileWorker and EditTilePublication
  (all CCS_MeshLayers_ prefix) distinguish source capture/work/publication.
- Avoid next time: moving mesh assembly off-thread does not remove upstream
  coverage serialization. Keep exact source operations independent of derived
  display jobs, preserve ordered mixed edits, and batch neighboring render jobs
  without increasing main-thread work budgets or lowering fidelity.
- Limits / conflicts: physics projection and exact source Erase remain editor-thread
  work. Source/tile capture, checkpoints, individual queries/clips and GPU uploads
  remain indivisible; explicit boundaries may flush a backlog. Native snapshots
  add temporary memory proportional to outstanding ordered regional operations,
  with adjacent Brush merging and full rebuild supersession. The transferred
  coverage cache is not cloned per stamp. This removes the identified wait barriers,
  but no zero-hitch/zero-latency or pixel/timing result is claimed before IDE testing.

## 2026-10-07 - Opening Mesh Layers Edit, Discard and Undo stall on full derived rebuilds

- Symptom / repro: after continuous Brush/Erase became responsive, opening Edit,
  pressing Discard and Ctrl+Z still freeze for too long on a populated Mesh Layers
  document. Open Edit, modify a saved document, Discard or Undo/Redo; first Render
  rebuilt the entire index/coverage and synchronously assembled/uploaded every tile.
- History / blast radius: reviewed preceding Edit persistent-buffer, Show/Edit
  restoration, budgeted-stroke, asynchronous coverage and tile-publication fixes.
  Audited Enter/Exit/Tick/Render, invalidation, Shape completion/editing, source
  stroke, pre-edit boundaries, Save/Discard/PostUndo and every preview API consumer.
  Show/PIE and runtime business ground queries/data serialization are untouched.
- Root cause: only interactive source updates used asynchronous derived work.
  RefreshDocumentState and initial load still led to full synchronous Render work.
  Ctrl+Z called CancelInteraction, which flushed display work before Undo immediately
  discarded it. Ready canceled futures/cache arrays also risked large frees on
  the editor thread. This shares the same expensive derived stages across three triggers.
- Fix: new FMeshLayerDocumentBuild snapshots original triangle arrays and native
  GUID/enabled metadata. Worker builds the broad phase, exact complete coverage and
  prepared 32-cell tile meshes. Tick checks revision and consumes only ready results;
  QueuePreparedUpdate moves geometry directly into soft-budget publication, including
  empty tiles for stale/empty source. Render does no full index/cache build/upload.
  Source edits/restoration/exit cancel old generations; no callbacks or live UObject
  references enter workers. Retire old cache and canceled full/coverage/tile native
  state off-thread. PrepareUndo flushes accepted source only, closes its transaction
  and skips obsolete coverage/mesh computation. Preserve all source, sampling,
  priority, color, height, topology, Save and transaction semantics. Small Shape
  draft fills remain visible while the full document rebuild runs.
- Files: MeshCamera/ComposableCameraMeshLayerDocumentBuild.h/.cpp (new),
  ComposableCameraMeshLayerEdMode.h/.cpp, ComposableCameraMeshLayerEditPreview.h/.cpp,
  ComposableCameraMeshLayerStrokeCoverage.cpp; Tests/ComposableCameraMeshLayerVisualizationTests.cpp,
  ComposableCameraMeshLayerEditPreviewTests.cpp, ComposableCameraMeshLayerBudgetedStrokeTests.cpp;
  EditorDesignDoc.md, TechDoc.md and ExecutionFlowExamples.md.
- Regression tests: AsyncDocumentPreviewBuild compares exact original full coverage
  for slopes, same-Layer height competition, Layer priority, stacked and disabled
  surfaces; snapshots survive live source replacement, stale revisions/cancel and
  empty documents. RestoredDocumentPreview checks no initial/waiting Render build,
  stationary progress, exact vertex/tangent/color/UV/fan component output, Discard
  while a job is pending, Undo/Redo generation replacement, empty stale-tile removal
  and new stroke cancellation. BudgetedStroke checks source-only Undo preparation
  retains all accepted samples and exact Undo/Redo source arrays.
- Verification: static whitespace and consumer/lifetime/order review only.
  Project AGENTS.md requires Rider/Visual Studio compilation and editor-side tests;
  no compilation, automation or measured timing was run here. Close UE, build
  UE5_6Editor in the IDE and restart (native headers/new source files). Run the three
  tests plus AsyncStrokeCoverage, QueuedEditPreviewSnapshots/Batch,
  EditPreviewPersistentBuffers/Invalidation, DocumentUndoRedo and DiscardWorkingDocument.
  In a dense Level test opening Edit, stationary load, repeated Undo/Redo, Discard,
  empty restoration, cancellation during build, Shape drafts and immediate Brush,
  plus Show -> Edit -> close and PIE. Inspect CCS_MeshLayers_EditDocumentSnapshot,
  EditDocumentWorker, EditDocumentTileWorker and EditTilePublication scopes.
- Avoid next time: audit initial/full restoration paths as well as incremental
  edits. Do not flush derived results immediately before replacing their source.
  Native cancellation must also avoid bulk destruction on the interactive thread.
- Limits / conflicts: snapshots/checkpoint copies and Unreal transaction serialization
  remain synchronous, as does each engine component upload. Ctrl+Z during a source
  backlog must still finish accepted scene queries/cuts for identical Undo semantics.
  Full rebuilds hold temporary native source/coverage/mesh memory; canceled work
  retires asynchronously and cannot publish. Source interaction before its index
  arrives retains the existing synchronous index fallback. No zero-stall or measured
  responsiveness claim is made before IDE verification. Runtime/Show/PIE paths unchanged.

## 2026-10-07 - Edit opening still waits for whole-document mesh computation

- Symptom / repro: after moving full rebuilds off the editor thread, the user
  reports a long wait before Mesh Layers appear when opening Edit. Open a populated
  multi-Layer document and keep the camera still. The screenshot shows incomplete
  colors during loading; progressive display is expected without initial stalls.
- History / blast radius: reviewed the preceding opening/Discard/Undo async fix,
  earlier Show streaming-first-region delivery and Brush coverage/tile-worker fixes.
  Audited document worker completion, shared streaming resolver, Edit component
  publication, Tick/Render, Shape completion, source/Undo/Discard/focus/tool/save
  boundaries and all consumers of the changed native APIs. Saved/runtime data,
  business queries, Show/PIE implementation and projection quality remain unchanged.
- Root cause: the previous document worker ran the entire coverage resolver and
  assembled every tile before its future exposed any output. Tick's publication
  budget only applied after that full wait. Background computation avoided editor
  freezes but did not deliver the first visible result promptly.
- Fix: convert only native GUID-to-Layer-index metadata on the worker and reuse
  BuildRuntimeVisualizationTiles. Captured local view origin prioritizes nearby
  regions. Enqueue exact meshes as each region resolves, starting with the existing
  small final 8-by-8 region. Keep one tile's scratch to combine first region and
  disjoint remainder before replacing its complete GPU tile. Tick consumes up to
  four ready regions under a soft 1 ms limit without waiting for the final future;
  normal publication retains its existing soft budget. An open prepared stream
  survives idle frames; only terminal completion removes unseen old tiles.
  The complete authoring cache aggregates each disjoint cell once and remaps grid
  indices; its index/cache arrive after all ready geometry is consumed. No second
  all-document assembly blocks first display. Build the authoring index and
  dispose of the old cache after region delivery, moving the original arrays back
  into the source snapshot for index construction. Cancel/revision rejection still
  detach native state for worker-side cleanup. Unchanged focus/tool boundaries
  preserve the stream; source interaction and cancellation detach both parts.
  Save compaction also detaches snapshots because counts can change without a
  new revision. No scene traces, UObject accesses or live mode pointers enter workers.
- Files: MeshCamera/ComposableCameraMeshLayerDocumentBuild.h/.cpp,
  ComposableCameraMeshLayerEditPreview.h/.cpp and ComposableCameraMeshLayerEdMode.h/.cpp;
  Tests/ComposableCameraMeshLayerVisualizationTests.cpp and ComposableCameraMeshLayerEditPreviewTests.cpp;
  EditorDesignDoc.md, TechDoc.md and ExecutionFlowExamples.md.
- Regression tests: ProgressiveEditPreview checks a small nearby final region
  becomes visible before terminal consumption, idle open streams retain it,
  remainder expansion keeps that region, exact oriented fan triangles/render
  attributes match the original full publisher, and completed empty source removes
  every old tile. AsyncDocumentPreviewBuild now drains ready regions before its
  terminal result and still compares slopes, priorities, heights, disabled bounds,
  ownership, stale revisions, cancellation and empty source. RestoredDocumentPreview
  uses real progressive publication, checks exact triangle/attribute multisets
  (cell order can follow region scheduling), focus retention, immediate stroke
  cancellation, Save compaction, Discard/Undo/Redo and empty restoration.
- Verification / blocker: consumer/lifetime/order review and static whitespace
  checks only. Project AGENTS.md requires IDE compilation and editor-side automation;
  no build, tests, first-pixel time or total loading time was measured here. Close
  UE, build UE5_6Editor in Rider/VS and restart (native signatures changed). Run
  tests above plus BudgetedStroke, AsyncStrokeCoverage, QueuedEditPreviewSnapshots/Batch,
  EditPreviewPersistentBuffers/Invalidation, DocumentUndoRedo and DiscardWorkingDocument.
  Smoke-test dense Edit opening with a stationary view, early first fill, final
  colors/holes, focus/tool changes mid-load, immediate Brush/Erase, cancel,
  repeated Undo/Redo, Discard, Save during load and Show/Edit/PIE transitions.
  Existing EditDocumentSnapshot/Worker, StreamingCoverage, EditDocumentTileWorker
  and EditTilePublication scopes separate preparation, first output and publication.
- Avoid next time: an asynchronous final future is not progressive delivery.
  Publish complete local results before global completion, preserve stream state
  across empty polling frames, and combine disjoint portions before replacing a
  common GPU tile. Do not validate snapshots solely by revision when arrays can
  be rewritten without changing it.
- Limits / conflicts: native bounds/binning precede first output, and one
  heavily overlapping final region can still take time. Checkpoint/source copies,
  transaction serialization and individual component uploads remain indivisible.
  Region scheduling may reorder cells/vertices, while exact geometry and rendering
  attributes remain unchanged. Native source, full coverage and queued geometry
  require temporary memory. Actual first-display/total timing and pixels still
  require IDE testing; no zero-delay claim is made.

## 2026-10-07 - Undo and Discard restore source quickly but leave stale Edit fill for seconds

- Symptom / repro: after the main-thread stall fixes, edit populated Mesh Layers,
  use Ctrl+Z or Discard and keep the view still. Source is restored promptly, but
  visible fill remains unchanged for roughly ten-plus seconds before catching up.
- History / blast radius: reviewed the opening/Discard/Undo native-worker fix,
  progressive first-region follow-up, continuous Brush/Erase source/coverage/tile
  pipeline, native cancellation, persistent buffers and Show/Edit lifecycle bugs.
  Audited source transaction completion, Settings revision restoration, PostUndo,
  Save compaction, focus/tool changes, empty documents, component/queue lifetime
  and every consumer of document/prepared/native buffer APIs. Runtime queries,
  floor ownership, projection, saved data and Show/PIE paths are unchanged.
- Root cause: moving full resolution off-thread preserves input responsiveness,
  but does not avoid recomputing an already displayed historical document. Mesh
  replacement still depends on expensive coverage work, and obsolete empty tiles
  are only removed at full completion. Nearby startup priority may also select
  unchanged tiles, giving no visible feedback for the actual undo operation.
- Fix: components retain immutable shared native vertices/indices/bounds.
  Remember complete displays by DocumentRevision with tile/row keys, exact linear
  color and cell size; snapshots share unchanged buffers without full array copies.
  Undo/Redo/Discard/cancel queue only changed tiles from a matching remembered
  revision, clearing missing rows/empty regions during budgeted publication and
  retaining remote buffers/proxies. Full index/coverage reconstruction still runs
  on a native worker, with preview assembly/publication disabled on a history hit;
  it cannot overwrite complete restored tiles with startup fragments or repeat a
  whole upload at completion. No-op focus boundaries retain pending restoration;
  source edits cancel it. Save compaction invalidates the index job but preserves
  unchanged historical display publication. Unknown/evicted versions use the normal
  progressive fallback. Source-only Undo preparation and PostUndo mark provenance
  invalid before clearing interactions, preventing the old fill from being mislabeled
  with already restored Settings. Explicit source-complete stroke boundaries record
  only after closing the transaction; partial/stale display is never remembered.
- Memory / lifetime: at most 32 snapshots, soft 128 MiB unique historical buffers
  excluding currently live buffers. Saved checkpoint/current revision are protected,
  even if their irreducible memory alone exceeds the limit. Evicted history and
  replaced native buffers retire off-thread; they contain no UObject or render
  resource references. Level/Actor reset clears historical identities. Ordinary
  unchanged frames do not capture snapshots or allocate; metadata capture happens
  at completed document mutations/publication. Component/proxy uploads remain on
  the editor thread and under the existing soft publication budget.
- Reference: read-only UE5.6 MeshModelingTools/Private/MeshVertexSculptTool.cpp,
  EndChange, OnDynamicMeshComponentChanged and OnTick use retained mesh changes
  plus regional rendering updates. Kept the plugin's existing UObject source Undo
  format and nonwaiting worker rules, using only the regional-display principle.
- Files: MeshCamera/ComposableCameraMeshLayerEditPreview.h/.cpp,
  ComposableCameraMeshLayerEdMode.h/.cpp, ComposableCameraMeshLayerDocumentBuild.h/.cpp;
  Tests/ComposableCameraMeshLayerEditPreviewTests.cpp and
  ComposableCameraMeshLayerVisualizationTests.cpp; EditorDesignDoc.md, TechDoc.md,
  ExecutionFlowExamples.md.
- Regression: RevisionEditPreviewHistory checks direct original buffer identity,
  exact fan geometry/normals/UVs/colors, changed-tile-only restoration, untouched
  remote revisions, new/deleted Layer rows, empty display, eviction/pinned Save,
  unknown revisions, cancellation and reset. ImmediateRestorationDisplay uses real
  engine Undo/Redo and Discard, checks visible changes before scheduling any cache
  rebuild, repeated restoration during native work, unchanged remote buffers,
  background completion without republishing, and no stale provenance capture.
  AsyncDocumentPreviewBuild checks cache-only reconstruction emits no preview
  tiles while retaining the exact complete cache/index. EditPreviewInvalidation
  explicitly verifies the unremembered-source fallback.
- Verification / blocker: static whitespace, signature/consumer, lifetime and
  transaction/order review only. Project AGENTS.md permits compilation/tests only
  through Rider/Visual Studio/editor. No compilation, automation, timing or pixel
  measurement was run. Close UE, build UE5_6Editor in the IDE and restart (native
  headers and component storage changed). Run the named tests plus RestoredDocumentPreview,
  ProgressiveEditPreview, BudgetedStroke, AsyncStrokeCoverage, QueuedEditPreviewBatch,
  EditPreviewPersistentBuffers/RegionalUpdates and DocumentUndoRedo/DiscardWorkingDocument.
  Smoke-test radius-150 Brush/Erase -> immediate Undo/Redo/Discard, removal into
  empty source, repeated Undo while background work is pending, focus changes,
  Save during restoration, palette/order changes and Show/Edit/PIE transitions.
  EditRememberRevision, EditRestoreRevision, EditTilePublication and
  EditDocumentWorker trace scopes distinguish display restoration from cache work.
- Avoid next time: source restoration and display restoration have separate
  latency requirements. Reuse complete historical render data and update changed
  regions before rebuilding derived coverage. Never trust the live Settings
  revision as provenance for a mesh that has not finished displaying that source.
- Limits / conflicts: never-rendered/evicted revisions still require progressive
  rebuilding. Background coverage remains potentially expensive but no longer gates
  remembered visible changes. Native history adds bounded shared-buffer metadata/
  retained buffers; source copies, Unreal transaction serialization and individual
  uploads remain indivisible. GPU upload time/FPS and actual restoration latency
  need IDE/editor validation; no zero-delay guarantee is made.

## 2026-10-07 - Discard followed immediately by Brush loses responsive Edit fill

- Symptom / repro: open populated Mesh Layers Edit, Brush/Erase, Discard, then
  immediately hold Brush again. New source is accepted, but the fill waits for
  seconds before changing. Also possible after Undo/Redo or stroke cancellation,
  including while restored display tiles have not all been published.
- History / blast radius: reviewed the preceding display-history fix, progressive
  document opening, continuous Brush/Erase pipeline, native cancellation, source
  provenance, source-only Undo, Save compaction, persistent regional buffers and
  Show/Edit/PIE routing. Audited every RememberRevision/QueueRestoreRevision,
  cached visualization/index reader and queued tile consumer. No runtime query,
  placement algorithm, Layer structure/channel, source serialization or Show/PIE
  behavior changed.
- Root cause: the previous optimization restored historical render buffers only.
  RefreshDocumentState left bVisualizationDirty set and rebuilt editable coverage
  on a whole-document worker. That worker moved away the mode's old cache.
  BeginStroke canceled it and the queued historical display; the next stamp saw
  an invalid/empty editing cache and captured every source triangle for full
  clipping. The added tests covered restore -> display and worker completion, but
  missed restore -> immediate real source edit. A visible historical mesh is not
  a valid editable base by itself.
- Fix / ownership: completed matching revisions retain a native immutable
  FEditPreviewCheckpoint containing the exact coverage grid/polygons and broad
  phase together with shared display buffers. Move coverage into the checkpoint
  without a main-thread polygon copy; copy its small 128-triangle block index.
  Reset moved-from bounds explicitly. A complete restore installs that editable
  base/index immediately and does not schedule document reconstruction. The first
  queued stroke makes a linear owned coverage copy on its native worker, then
  uses existing append/regional clipping. Later queued operations reuse the
  returned mutable result. History never references mutable source or UObjects.
  Legacy explicit synchronous/test calls retain synchronous boundary semantics.
- Fix / publication: BeginStroke keeps pending restored tiles when the restored
  editable base exists. A regional update supersedes only its touched unpublished
  restore tiles; distant restores and removed rows remain queued. Stroke tile
  workers may start before the remaining restoration publishes, and their ready
  output publishes ahead of historical tiles. Full/regridded results still replace
  the complete generation. Undo/Discard/teardown detach obsolete workers/queues.
- Reference: read-only installed UE5.6 MeshModelingTools/Private/
  MeshVertexSculptTool.cpp, OnBeginStroke, WaitForPendingUndoRedo, EndChange and
  OnDynamicMeshComponentChanged require editable state consistency and update the
  affected triangle region. LandscapeEditor/Private/LandscapeEdModeTools.h,
  TLandscapeEditCache::SetCachedData updates region cache and real data together.
  Applied those consistency/region principles while keeping this plugin's
  existing transactions, exact resolver and nonwaiting finite interaction.
- Memory / cost: retain at most 32 history versions, soft 128 MiB of noncurrent
  coverage/index plus unique historical buffers excluding live buffers; saved and
  current versions remain protected. Account for nested native polygon/index
  allocations. Large releases happen off-thread. The first edit of an immutable
  version still performs an O(cells + patches + vertices) native copy, with no
  full-document projection/clipping; subsequent pending stamps reuse that cache.
  Stable frames do not copy or create checkpoints. Never-rendered/evicted versions
  retain the existing progressive rebuild fallback; indivisible source/index
  snapshots and engine component uploads still need editor timing validation.
- Files: MeshCamera/ComposableCameraMeshLayerEdMode.h/.cpp,
  ComposableCameraMeshLayerEditPreview.h/.cpp,
  ComposableCameraMeshLayerStrokeCoverage.h/.cpp,
  ComposableCameraMeshLayerAuthoringIndex.h/.cpp;
  Tests/ComposableCameraMeshLayerEditPreviewTests.cpp and
  ComposableCameraMeshLayerShapeTests.cpp; EditorDesignDoc.md, TechDoc.md,
  ExecutionFlowExamples.md.
- Regression: RestoredPreviewImmediateStroke uses a real custom-channel
  StaticMesh floor, native snapshots, Discard -> Brush/Erase, Undo -> Erase and
  Redo -> Brush without waiting for restoration or scheduling a document worker.
  Checks that the first Brush captures only appended triangles, source completion
  stays independent, finite Tick progression completes, saved native state remains
  immutable and final near/distant tile geometry/colors/normals/priority match the
  complete resolver. Also compacts an unused saved source vertex, then edits
  immediately using restored coverage and refreshed broad-phase counts.
  RevisionEditPreviewHistory now exercises a new local update
  while both local and distant restore tiles are still queued, including worker
  dispatch before historical publication and exact final remote Layer rows.
  ImmediateRestorationDisplay now expects an immediately usable index/cache;
  StrokeVisualizationRefresh checks cell identity through immutable checkpoint
  ownership too.
- Verification / blocker: static source/consumer/lifetime/transaction review and
  whitespace checks only. Per project AGENTS.md, no shell compilation or editor
  test launch. Close UE, compile UE5_6Editor in Rider/VS, restart and run
  RestoredPreviewImmediateStroke, RevisionEditPreviewHistory,
  ImmediateRestorationDisplay, RestoredDocumentPreview, BudgetedStroke,
  AsyncStrokeCoverage, StrokeVisualizationRefresh, DocumentUndoRedo and
  DiscardWorkingDocument. Manually repeat radius-150 Brush -> Discard -> immediate
  Brush/Erase, Undo/Redo -> immediate stroke, quick repeated presses, Save
  compaction and Show/Edit/PIE transitions with the camera stationary.
- Avoid next time: every fast display restoration must restore a valid editable
  base or explicitly manage its handoff. Test restoration followed by a mutation,
  not just restoration in isolation. Never cancel unrelated restored tiles when
  accepting a local edit. Source revision, editable cache and display provenance
  have distinct ownership and must become consistent at the interaction boundary.
- Conflicts / limits: checkpoint index counts can become stale after Save's
  geometry-preserving compaction; existing IsCurrent rebuilding precedes the next
  source edit. Unknown or evicted history keeps the original fallback. No public
  Blueprint/runtime API or saved-data migration; native headers require a restart.

## 2026-10-07 - Audit: Edit publication redraws only the first viewport in a frame

- Status: production fix applied on 2026-10-07; IDE compile/test pending.
- Symptom: completed Edit Mesh fills can remain stale in other stationary,
  non-realtime Level viewports until unrelated input triggers their redraw.
- Trigger / repro: open two visible Level viewports on the same editor World,
  disable Realtime, enter Edit and let initial loading or a released Brush/Erase
  finish without moving either camera. Observe the view served second by mode Tick.
- Why / root cause: EdMode::Tick guards both document and stroke work with
  GFrameCounter, but the calls to ViewportClient->Invalidate(false, false) are
  inside those guards. Only the first caller is invalidated. Subsequent callers
  skip work and redraw together. Component publication does not invalidate other
  viewport clients. The Show mode's separate all-viewports redraw is suspended in
  Edit, so its fix does not cover this path. UE5.6 FSceneViewport::InvalidateDisplay
  routes only to that viewport client's RedrawRequested/bNeedsRedraw.
- History / blast radius: checked StationaryPreviewPublication, progressive Edit
  loading, persistent buffers and revision-history fixes. Audited Edit Tick,
  AdvanceDocumentPreview, AdvancePainting, component publication and Render,
  Show's core ticker and installed UE5.6 viewport tick/redraw scheduling.
- Touched files: Tests/ComposableCameraMeshLayerEditPreviewTests.cpp and BugLog.md.
  Defect locations: MeshCamera/ComposableCameraMeshLayerEdMode.cpp::Tick and
  ComposableCameraMeshLayerEditPreview.cpp::AdvanceQueuedUpdates.
- How fixed: Edit Tick keeps one computation budget and requests all-Level-viewport
  redraw during progress plus two tail frames. Stable frames stop redrawing.
  Implementation: MeshCamera/ComposableCameraMeshLayerEdMode.h/.cpp.
- Regression-test name: ComposableCameraSystem.Editor.MeshCamera.EditPreviewInvalidation
  now queues real Edit geometry, invokes two real viewport clients in one frame,
  and expects both bNeedsRedraw flags. Open two visible Level viewports for this
  assertion; otherwise it emits a warning. Restores flags and package dirty state.
- Verification / blocker: project AGENTS.md permits compilation/tests only in
  Rider/Visual Studio and the editor. Compile the test additions in the IDE and
  run with split viewports; no pixel-level verification or timing claimed here.
- Avoid next time: frame guards govern computation, not the set of observers
  requiring notification. Test multiple static views, not only one direct publisher.
- Possible conflicts: redraw must stay scoped to affected editor Worlds and stop
  on stable frames. Repeating source processing per viewport would regress budgets.

## 2026-10-07 - Audit: Show retains stale geometry after a live storage document changes

- Status: production fix applied on 2026-10-07; IDE compile/test pending.
- Symptom: Show can keep an empty or obsolete Mesh, colors and enabled-Layer state
  after source replacement or storage-actor Undo. Toggling Show off/on refreshes it.
- Trigger / repro: enable Show for a loaded document, wait for completion, then
  replace its source through SetAuthoringData while retaining the same actor,
  or Undo an existing storage-actor change while Show stays active. The renderer
  keeps its old components although RuntimeData and query acceleration change.
- Why / root cause: PreviewEdMode::AdvancePreview starts Build only for a new
  cache or bRestartAfterPIE. Its cache stores no data revision or snapshot identity.
  The PIE coordinator similarly rebuilds only for initialization, World changes
  or stale preview actors. Storage SetAuthoringData/PostEditUndo update query
  resources without notifying either visual cache. Exact snapshot equality inside
  FPreviewGeometryBuild cannot help because Begin is never called again.
- History / blast radius: reviewed warm-cache snapshot matching, Show resume after
  Edit and stationary publication. Audited storage mutation/Undo, both Show cache
  owners, worker snapshots and streamed actor removal. Normal Edit->Show resets
  the preview mode and is not the failing route described here.
- Touched files: Tests/ComposableCameraMeshLayerPreviewModeTests.cpp and BugLog.md.
  Defect locations: MeshCamera/ComposableCameraMeshLayerPreviewEdMode.cpp,
  Utilities/ComposableCameraMeshLayerTool.cpp and runtime MeshCamera/
  ComposableCameraMeshSurfaceStorageActor.cpp.
- How fixed: storage source rebuild and both Undo overloads advance a transient
  EditorDataRevision. Editor Show and PIE compare this identity, cancel stale jobs
  and replace old fill. Files: StorageActor.h/.cpp, PreviewEdMode.h/.cpp and Tool.cpp.
  SavedPreview additionally checks same-count cache invalidation.
- Regression-test name: ComposableCameraSystem.Editor.MeshCamera.StationaryPreviewPublication
  now replaces source on its existing transient storage actor without toggling
  Show or moving the camera. Samples actual submitted geometry at the new and
  old locations, expecting new coverage and removal of obsolete fill.
- Verification / blocker: IDE/editor execution required by AGENTS.md. The new
  behavioral assertion covers editor Show; PIE source-change/Undo integration
  still needs an editor-side fixture and manual validation. No run claimed.
- Avoid next time: actor lifetime, document content and worker generations are
  separate identities. Warm-cache tests must change source while the owner lives.
- Possible conflicts: avoid whole-array comparisons every Tick. Save/Undo,
  streamed Level lifetimes, progressive publication and cache reuse must remain
  consistent; changing visualization must not mutate runtime query geometry.

## 2026-10-07 - Audit: rapid second Brush press bypasses the stroke Tick budget

- Status: production fix applied on 2026-10-07; real-Level latency and IDE tests pending.
- Symptom: fast release/press sequences can freeze before the next Brush starts,
  despite the ordinary asynchronous 4 ms stroke progression.
- Trigger / repro: on dense or curved floor, release a Brush while accepted
  stamps still await Tick, then press again immediately. More queued stamps or
  expensive collision/coverage increases the synchronous work in that second press.
- Why / root cause: BeginStroke calls FinishStroke() when bPainting and
  bStrokeReleased are both true. FinishStroke calls AdvancePainting(MAX_int32,
  0.0) for pending source work. Zero disables the time budget and requests full
  coverage/publication drains; Future::Consume can wait. This projects accepted
  stamps and can resolve coverage/upload tiles inside the next input callback.
- History / blast radius: reviewed BudgetedStroke, source/coverage separation,
  queued tile work and immediate restored strokes. Existing repeat-press coverage
  starts the next stroke only after previous source completion, missing this state.
  Audited release, BeginStroke, FinishStroke, explicit Save/focus/tool boundaries,
  transaction ownership, AdvanceStrokeCoverage and tile publication.
- Touched files: Tests/ComposableCameraMeshLayerBudgetedStrokeTests.cpp and BugLog.md.
  Defect locations: MeshCamera/ComposableCameraMeshLayerEdMode.cpp::BeginStroke/
  FinishStroke, ComposableCameraMeshLayerStrokeCoverage.cpp::Advance and
  ComposableCameraMeshLayerEditPreview.cpp::AdvanceQueuedUpdates.
- How fixed: a rapid next press creates a deferred stroke with captured spacing/
  options/release state. Tick closes the previous source transaction, then starts
  the next FIFO stroke. Explicit boundaries flush; cancel removes deferred input.
  EdMode.h/.cpp implement ownership. BudgetedStroke now checks exact FIFO source
  and separate Undo steps as well as the nonprojecting second callback.
- Regression-test name: ComposableCameraSystem.Editor.MeshCamera.BudgetedStroke
  now queues two real custom-channel Brush samples, releases without an intervening
  Tick, presses again and checks that input has not synchronously projected them.
  Cleanup cancels the active stroke/transaction. This proves the input-boundary
  contract, not a hardware-dependent frame-time threshold.
- Verification / blocker: compile/run in IDE/editor per AGENTS.md. Measure the
  second press on the user's dense Level with Unreal Insights to quantify latency.
- Avoid next time: exercise input while source is pending, not only while derived
  display is pending. Unlimited flushes must not be reachable from normal presses.
- Possible conflicts: silently dropping old stamps or merging two Undo steps
  would break authoring semantics. A queued/deferred new stroke needs explicit
  ownership; Save/focus/tool transitions must still preserve completed input.

## 2026-10-07 - Audit: pending Edit workers are not drained before editor-module unload

- Status: production fix applied on 2026-10-07; external unload verification pending.
- Symptom: module unload/reload with pending Edit work can leave background tasks
  executing lambda/function/destructor code from an unloaded editor DLL.
- Trigger / repro: open a large Edit document or queue expensive Brush/Shape work,
  then unload/reload ComposableCameraSystemEditor while worker or native retirement
  tasks are pending. Normal mode close alone keeps the DLL loaded and is not this case.
- Why / root cause: DocumentBuild, StrokeCoverage, Shape creation and tile work use
  the shared EAsyncExecution::ThreadPool, including fire-and-forget retirement
  lambdas. Cancellation sets atomic flags and detaches futures; it does not join
  those tasks. MeshLayerTool::Unregister drains only PreviewBuild's separate owned
  pool. Cancellation protects document publication, not executable-code lifetime.
- History / blast radius: reviewed prior Show shutdown and render-extension
  lifetime fixes. Audited all Edit Async callsites, mode Exit/destruction,
  MeshLayerTool::Unregister, editor ShutdownModule and Show's owned pool joining.
- Touched files: BugLog.md only. Defect locations: MeshCamera/
  ComposableCameraMeshLayerDocumentBuild.cpp, ComposableCameraMeshLayerStrokeCoverage.cpp,
  ComposableCameraMeshLayerEditPreview.cpp, ComposableCameraMeshLayerEdMode.cpp,
  Utilities/ComposableCameraMeshLayerTool.cpp and ComposableCameraSystemEditorModule.cpp.
- How fixed: EditWork.h/.cpp own a two-thread pool used by document, coverage,
  Shape, tile and native retirement tasks. Tool Unregister stops mode producers,
  joins this pool and flushes render commands before DLL unload. Normal cancellation
  still detaches without waiting. Future readiness alone is not the unload gate.
- Regression-test name: proposed MeshLayerEditorUnloadWithPendingEditJobs.
- Concrete test blocker: a test implemented inside this same DLL cannot safely
  remain executing to assert its own unload. It requires a host-owned harness
  outside the module. IDE-side verification: unload/reload under the debugger
  with queued Edit work; confirm every plugin task has returned before unload.
  No shell editor launch, build, unload or destructive test was attempted.
- Avoid next time: asynchronous work needs both publication cancellation and
  module lifetime ownership, including delayed native-array destruction.
- Possible conflicts: waiting on every mode exit would regress responsiveness;
  joining belongs at DLL teardown. Detached cleanup tasks must join too, not just
  futures that report a computed result before callable destruction.

## 2026-10-07 - Audit notes: remaining Mesh loading and live-edit performance limits

- Historical audit baseline. The latency update below implements cached opening,
  fused publication, live Shape fill and differentiated metadata. Linear source/
  transaction copies, indivisible queries/uploads and Edit-vs-Show LOD policy remain
  costs requiring actual Level profiling. Earlier bullets describe pre-fix paths.
- Opening copies source into Settings and SavedDocument, then copies native
  geometry again for DocumentBuild. First fill still waits for GUID mapping,
  full-document bounds and candidate binning before tile clipping begins.
- Starting Brush before the opening document completes cancels its progressive
  loader, rebuilds the authoring index synchronously and requests full stroke
  coverage. StrokeCoverage publishes only after its entire worker batch returns.
  Initial-load responsiveness and restored-complete-checkpoint responsiveness
  therefore have different guarantees.
- Shape control drags update only outlines. Details Interactive events deliberately
  skip mutation. Release/final Details commit still calls synchronous
  BuildProjectedShape -> Advance(MAX_int32), unlike budgeted new-Shape creation.
  This is documented existing behavior; filled-surface drag preview is absent.
- A Layer Name/DebugColor/Profile edit calls NotifyLayerDataChanged, whole-source
  RemoveOrphanedTriangles and full cache invalidation, even when geometry/priority
  did not change. Opening/stroke source copies, transaction serialization, queued
  tile snapshots, checkpoint memory accounting and individual component uploads
  can all exceed the soft budget before its next check.
- Edit explicitly disables Show's Landscape LOD view extension and never registers
  its persistent fill actor there. Show's accepted distant uneven-ground policy
  therefore does not carry into Edit. This proves a policy difference, not missing
  pixels: editor GeomMaterial's depth policy means target-Level visual inspection
  must establish any actual LOD-dependent loss.
- Tests/verification: existing ShapeEditingAndErase and RestoredDocumentPreview
  validate committed geometry and cancellation, not live filled pixels or latency.
  Manual IDE/editor matrix: cold open -> immediate Brush; continuous drag -> quick
  release/press; Shape control and numeric-slider edits; color-only Layer edit;
  static split viewports; Undo/Discard while Show stays active; identical near/far
  uneven-ground views after publication finishes. Record source, coverage, mesh
  assembly and publication timings separately. No timing/pixel result claimed.
- History reviewed: 2026-10-04 asynchronous Shape creation; 2026-10-06 LOD policy;
  2026-10-07 progressive loading, budgeted strokes and restored editable checkpoints.
  Preserve exact projection, Layer priority, height competition, source FIFO and
  Undo identity when addressing these limits.

## 2026-10-07 - Mesh editing repeatedly rebuilds derived geometry before feedback

- Status: implementation and focused regressions added; IDE compile, automation,
  timing and visual validation pending. No measured zero-latency claim.
- Symptom / exact repro: save a populated document, close/reopen Show or Edit;
  drag Brush/Erase continuously, immediately start another stroke; drag Rectangle/
  Circle/Polygon or existing controls and Shape Details; edit Layer Color/Name/
  Profile. Before this change, cold opens reclip all source before progressive
  tile delivery, normal coverage waits for another snapshot/mesh worker, control
  edits show outlines and synchronously project on release, metadata invalidates
  all geometry, and changed tiles recreate GPU resources.
- Why / root cause: runtime triangles were the only reusable persisted preview
  input. Coverage and renderer preparation were separate jobs. UI notifications
  treated all Layer properties as geometric changes. Component proxies allocated
  exact-size buffers on every mutation. Flat block scans and eager Brush rollback
  copies added input/setup costs. Engine Undo and native preview provenance need
  separate identities when metadata can share geometric checkpoints.
- History / blast radius: reviewed progressive opening, regional Erase, exact
  overlap/height competition, restored editable checkpoints, failed-Save rollback,
  stationary redraw, live storage invalidation, rapid input and module unload
  entries above. Audited all new API consumers, editor/PIE cache owners, working/
  saved actor documents, native job cancellation and render-resource lifecycle.
- Fix: Save persists version-1 exact resolved editor coverage under editor-only
  actor storage; cached Show/Edit load one document without spatial clipping.
  Unsupported/corrupt/legacy cache falls back. Source rebuild clears cache;
  storage Undo and content revisions invalidate Show/PIE. Brush/Erase resolve and
  prepare touched native fill in one worker. Regional publication clears empty
  tiles and supersedes obsolete assembly without losing FIFO source or remote
  restore work. Native source-order bounds hierarchy prunes local candidate queries.
  Append-only Brush rollback stores counts; first Erase captures original source.
  Color updates a material parameter; Name/Profile/Channel reuse geometry, with
  metadata history aliases and prospective channel semantics. Intermediate property
  transactions skip per-frame history snapshots and record only the completed bookend.
  Shape draft/control/
  Details previews combine immediate planar fill and budgeted temporary projected
  fill; final creation/replacement stays atomic and retains erasures/identity.
  Initial/restored document publication completes before temporary surface tiles
  replace it. Separate old/new/prior footprints avoid resolving their empty gap;
  candidate bounds cover complete edge cells so neighboring coverage survives.
  Temporary pixels never enter source/history. Ctrl+Z cancels latest pending input
  before engine Undo. Same-capacity vertex/index changes update existing RHI
  buffers; growth recreates. Cancelled Shape results and checkpoint coverage copies
  retire/run on the owned Edit pool, which joins only on unload.
- Touched files: runtime MeshCamera/ComposableCameraMeshSurfaceTypes.h and
  ComposableCameraMeshSurfaceStorageActor.h/.cpp; editor MeshCamera/
  ComposableCameraMeshLayerSavedPreview.h/.cpp (new), EditWork.h/.cpp (new),
  AuthoringIndex.h/.cpp, DocumentBuild.h/.cpp, StrokeCoverage.h/.cpp,
  EditPreview.h/.cpp, EdMode.h/.cpp, PreviewBuild.h/.cpp, PreviewEdMode.h/.cpp,
  ToolSettings.h/.cpp; Utilities/ComposableCameraMeshLayerTool.cpp;
  Tests/ComposableCameraMeshLayerRealtimeTests.cpp (new), EditPreviewTests.cpp,
  BudgetedStrokeTests.cpp, ShapeTests.cpp; DesignDoc, EditorDesignDoc, TechDoc,
  ExecutionFlowExamples and BugLog. Abbreviated editor names use the
  ComposableCameraMeshLayer prefix.
- Regression-test names: SavedPreview (reflected cache round-trip, exact polygons/
  normals/heights, invalid cache atomic rejection, one-batch Edit/Show loading,
  same-count actor invalidation); FusedStrokePreview (same-worker exact fill and
  empty Erase); RealtimeEditing (geometry/proxy retention for Color/Name/topology,
  metadata Undo/Redo, live filled Shape source isolation/cancel, complete edge-cell
  neighbors, distant footprint movement and regional deletion); AuthoringHierarchy
  (pruning, ordering, append bounds and removed leaves). Updated BudgetedStroke
  checks rapid FIFO and separate Undo; existing EditPreviewInvalidation and
  StationaryPreviewPublication cover split views/content mutation. Existing
  Shape/Erase/coverage regressions retain final source precision and Save gating.
- Verification / concrete blocker: project AGENTS.md requires Rider/VS compilation
  and editor automation. Reflection changes require full editor restart. Same-DLL
  unload testing requires the external host harness noted above. Static source/
  consumer/whitespace checks do not establish compilation or GPU pixels. Manual
  IDE/editor matrix: Save legacy document -> cold reopen; immediate Brush during
  load; sustained mixed input -> second press; Shape drag/Details sliders -> cancel/
  release/Undo; Color/Profile/Name; split static views; Undo/Discard then immediate
  input; pending jobs during shutdown; failed Save and streamed Levels/PIE.
- Avoid next time: persist disposable display data at expensive boundaries;
  distinguish metadata/source/display identities; update complete affected regions
  once, retain resource capacity, isolate provisional pixels from transactions,
  and own both computation and delayed destruction until module unload.
- Possible conflicts / limits: cooked query source and camera evaluation remain
  unchanged. Larger editor packages/cache copies, hierarchy/checkpoint accounting,
  UObject transaction serialization, first cold legacy resolution and indivisible
  queries/uploads still cost time. Saved source compaction invalidates index counts.
  Temporary Shape projection has a lower 4096-triangle cap than final 16384 and
  is provisional. PIE still fits collision. Show/Edit switches and Show's LOD
  policy stay unchanged; Edit retains its prior LOD policy. Record separate source,
  coverage, snapshot, buffer-update and publication timings with Unreal Insights.

## 2026-10-07 - Deleting a Shape allows a queued replacement to recreate it

- Status: fix and regressions added; IDE compilation and editor execution pending.
- Symptom / exact repro: select a committed Shape, release a control/Details edit
  so its replacement is queued, then Delete before projection/coverage completes.
  Previously Delete removed the source, but the pending replacement could rebase
  against that revision and append the deleted Shape again. Deletion also requested
  full-document preview reconstruction for a local source removal.
- Why / root cause: queued replacements retained Shape identity independently of
  selected source. Delete did not revoke jobs for that identity. Revision rebasing
  correctly retained ordinary input, but incorrectly retained explicitly deleted input.
- History / blast radius: reviewed asynchronous Shape creation/rebasing, source
  ownership, retained Erase cuts, pending cancellation and restored checkpoint bugs.
  Audited control/Details replacement, Delete, Undo, Discard and Layer changes.
- Fix / touched files: MeshCamera/ComposableCameraMeshLayerEdMode.cpp cancels only
  pending replacements with the deleted GUID before source removal. It captures the
  removed footprint, rebuilds the disposable source index and queues fused regional
  coverage/native fill. Other Shape identities and remote geometry survive.
  Tests/ComposableCameraMeshLayerShapeTests.cpp and
  ComposableCameraMeshLayerRealtimeTests.cpp add focused checks; EditorDesignDoc,
  TechDoc, ExecutionFlowExamples and this log record the flow.
- Regression names: ShapeEditingAndErase queues a replacement immediately before Delete
  and asserts it is cancelled. RealtimeEditing commits then deletes a distant Shape,
  asserts no document rebuild starts, and checks exact remaining source/fill area.
- Verification blocker / manual check: AGENTS.md restricts compilation and test
  execution to Rider/VS/editor. Run these tests there. Manually drag/release a large
  Shape, Delete while its fill is pending, then wait, Undo and Redo; the deleted GUID
  must stay absent until Undo restores it.
- Avoid next time: deletion must revoke pending producers for the same identity,
  not merely remove current source. Native results may never resurrect revoked input.
- Possible conflicts / limits: the source transaction remains one engine Undo;
  source removal and index rebuilding still have linear event cost. Invalid/missing
  regional bases keep the existing full-rebuild fallback. Runtime queries are unchanged.

## 2026-10-07 - Color-only Edit preview fails to compile against UE5.6

- Status: source fix and regression update added; user IDE recompilation pending.
- Symptom / exact trigger: compile the UE5.6 editor target after the realtime
  preview changes. ComposableCameraMeshLayerEditPreview.cpp reports C2660 at
  MaterialProxy->InvalidateUniformExpressionCache().
- Why / root cause: FMaterialRenderProxy::InvalidateUniformExpressionCache requires
  bool bRecreateUniformBuffer in UE5.6, with no default argument. The call used a
  nonexistent overload. It was also redundant: the following unique-pointer
  replacement destroys the old proxy, whose destructor/ReleaseRHI releases its cache.
- History / blast radius: reviewed the realtime GPU/metadata update above; audited
  SetFillColor, render-command dispatch, material ownership and RealtimeEditing.
  Checked the installed UE5.6 MaterialRenderProxy.h/.cpp directly.
- Fix / touched files: removed the redundant invalidation from
  MeshCamera/ComposableCameraMeshLayerEditPreview.cpp; retained the owned colored
  proxy replacement. Updated Tests/ComposableCameraMeshLayerRealtimeTests.cpp,
  TechDoc, EditorDesignDoc and BugLog. No signature or geometry behavior changes.
- Regression: ComposableCameraSystem.Editor.MeshCamera.RealtimeEditing now repeats
  color replacement through end-of-frame/render-command publication, checks exact
  component color plus unchanged geometry/revision/scene proxy, then restores the
  document color before the existing Undo/Redo checks.
- Verification / blocker: declaration, destructor/release path and consumer checks
  passed; git whitespace check passed. AGENTS.md restricts compilation/automation
  to Rider/VS/editor. Recompile there and run RealtimeEditing. Manually drag the
  color picker continuously; verify visible color changes without mesh recreation.
  CPU/proxy assertions alone do not validate rendered pixels.
- Avoid next time: verify exact installed-engine signatures and resource destruction
  before introducing explicit invalidation calls.
- Possible conflicts: no runtime query, Show toggle, document identity or Save changes.
  The engine still owns material-resource synchronization and release.

## 2026-10-07 - Brush during Edit opening stops loading and produces no new fill

- Status: source changes and regressions added; IDE compilation/editor execution pending.
- Symptom / exact repro: open EditMeshLayers on a populated document, then Brush
  the floor before visible Tile loading finishes. Newly painted source has no fill;
  existing loading pauses and later resumes. The earlier cached-load revision
  removed spatial clipping but still published components over successive frames.
- Why / root cause: BeginStroke cancelled the opening document job. Loading marked
  coverage dirty, so the next Brush captured a whole-source rebuild rather than an
  append delta. The document polling guard also stopped during painting/coverage.
  Per-region component cleanup scanned all regions repeatedly, giving quadratic
  initial publication cost; proxy construction copied all vertices on the editor thread.
  An already-current empty index could additionally permit source edits before an
  older opening index arrived and replaced their incremental broad phase.
- History / blast radius: reviewed progressive startup, fused realtime coverage,
  exact historical restoration, deferred FIFO/Undo boundaries, pool retirement,
  metadata aliases and the UE5.6 material API compile fix. Audited initial open,
  Tick/Render, Brush/Erase source, Shape replacement, Layer changes, Save compaction,
  Undo/Discard, focus, exit, GPU initialization and all affected test consumers.
- Fix: production Edit uses StartResident. Native work releases index/grid early,
  prepares one complete shared geometry document and installs it in one scene update.
  Internal regions remain only for local editing. Brush retains opening and waits
  for its index even on empty source. Separate fixed-grid regional coverage provides
  complete current Brush/Erase feedback while exact FIFO deltas await the original
  base cache. Base installation skips all edited regions, including erased/empty
  ones, and cannot overwrite the already edited index. Base completion cancels
  provisional work before primary deltas resume. Central cancellation revokes both
  opening pipelines on Undo/Discard/structural replacement/exit. Current colors are
  selected at publication; historical colors keep their exact snapshots. Bounds
  preparation runs on native workers, initial vertex copying/resource setup runs
  on the render thread, and component removal uses region/Layer keys. Unused terminal
  index copies and skipped stale-region buffers also retire off the editor thread.
- Touched files: MeshCamera/ComposableCameraMeshLayerDocumentBuild.h/.cpp,
  ComposableCameraMeshLayerEditPreview.h/.cpp, ComposableCameraMeshLayerStrokeCoverage.h/.cpp,
  ComposableCameraMeshLayerEdMode.h/.cpp; Tests/ComposableCameraMeshLayerRealtimeTests.cpp
  and ComposableCameraMeshLayerEditPreviewTests.cpp; DesignDoc, EditorDesignDoc,
  TechDoc, ExecutionFlowExamples and this log.
- Regression: ComposableCameraSystem.Editor.MeshCamera.ResidentLoadingAndBrush
  withholds the base deterministically across Brush and Erase; checks local fill,
  pending exact deltas, single whole installation, distant source, stale-base
  exclusion, latest metadata colors, exact FIFO convergence, scene proxies,
  uncached single-result fallback and cancellation. A bounded worker gate checks
  empty/current source waits for early index delivery, then resumes exactly.
  RestoredDocumentPreview now asserts Brush retains resident loading; existing
  progressive standalone APIs retain their independent regression coverage.
- Verification blocker / manual steps: AGENTS.md permits compilation and automation
  only through Rider/Visual Studio/editor. Restart editor and build fully there;
  run ResidentLoadingAndBrush, RestoredDocumentPreview, BudgetedStroke,
  RealtimeEditing and RestoredPreviewImmediateStroke. Cold-open dense saved and
  legacy documents, immediately drag Brush/Shift-Erase, change color, release,
  Undo/Redo/Discard, and close/reopen while jobs are active. Inspect pixels and
  Insights source/index/coverage/publication/render timings; CPU assertions alone
  do not establish visual latency or GPU correctness.
- Avoid next time: distinguish a stale base load from ordered edit deltas. Never
  cancel baseline work merely for an append; protect changed regions/index from
  older results. Separate initial residency from incremental render decomposition,
  and inspect publication complexity plus proxy construction thread placement.
- Possible conflicts / limits: ShowMeshLayer/Edit window lifecycle and runtime
  queries are unchanged. Show/PIE retain existing budgets/floor fitting. Source/cache
  snapshots, index preparation, legacy exact clipping, component creation, GPU
  uploads and cold shaders still cost time; no measured zero-latency claim. If local
  feedback is still pending at base arrival, that region waits for its primary delta.
  Large document growth can still trigger the existing exact grid-resize fallback.

## 2026-10-07 - Mesh Layers Save repeats coverage resolution and expands indexed source

- Status: source optimization and focused regressions added; IDE compilation,
  automation execution and measured Save timings pending.
- Symptom / exact trigger: open EditMeshLayers on a populated document, finish
  Brush/Shape/Erase editing, then click Save. Saving remains slow even with exact
  preview coverage already ready, or after changing only Name/Color/Profile.
- Why / root cause: Save always rebuilt full authoring coverage on the editor
  thread. RemoveOrphanedTriangles expanded every triangle into three independent
  vertices; runtime baking expanded it again. SetAuthoringData always discarded
  cooked triangles/BVH and stored preview, even for metadata-only changes. Each
  BVH subtree was fully sorted despite requiring only its median partition.
  No phase timings separated this preparation from UE package serialization/I/O.
- History / blast radius: reviewed exact saved-preview loading, resident opening
  with Brush/Erase deltas, same-count content/index invalidation, historical
  editable checkpoints, failed-save/Discard rollback, source compaction, storage
  PostEditUndo and per-Layer nearest-hit/BVH regressions. Audited all
  SetAuthoringData/RebuildQueryResources callers, authoring/runtime vertex-index
  consumers, Layer enable/reorder rules, Shape ownership and transaction boundaries.
  Installed UE5.6 GeometryCore DynamicMesh3_Edits.cpp::CompactInPlace provides the
  vertex-remap/sharing reference; engine source remains read-only.
- Fix: finish accepted input and consume resident exact coverage/FIFO deltas
  before compaction invalidates source counts. Convert the current immutable or
  mutable exact coverage directly into saved polygons; only a miss resolves once,
  retaining that result for editing. Keep an unchanged actor's stored preview
  only after an allocation-free comparison with current exact coverage, rejecting
  stale/corrupt version-one cache headers, normals, ownership and polygons.
  Compact/bake through vertex-ID remapping, preserving existing shared vertices,
  exact triangle order/positions, Layer/Shape GUIDs, controls and erasures. A no-op
  retains source allocation/index. Metadata with unchanged geometry/GUID rows
  reuses runtime triangles/BVH; enabled filtering reads current rows, while coverage
  is invalidated when its policy changes. Missing nonempty runtime data rebuilds;
  inconsistent source/Shape ownership cannot take the reuse shortcut.
  std::nth_element replaces full recursive subtree sorting, using cached bounds
  and the original triangle-ID tie-break. Log finalize/runtime/preview/packages/
  checkpoint milliseconds and cache reuse flags after successful Save.
- Touched files: runtime MeshCamera/ComposableCameraMeshSurfaceStorageActor.h/.cpp,
  ComposableCameraMeshSurfaceTypes.cpp; editor MeshCamera/ComposableCameraMeshLayerEdMode.h/.cpp
  and ComposableCameraMeshLayerSavedPreview.h/.cpp;
  Tests/ComposableCameraMeshLayerRealtimeTests.cpp and runtime
  Tests/ComposableCameraMeshSurfaceTests.cpp; DesignDoc, EditorDesignDoc, TechDoc,
  ExecutionFlowExamples and this log.
- Regression-test names: ComposableCameraSystem.Editor.MeshCamera.SavePreparation
  checks exact checkpoint reuse, compaction sharing/order/Shape ownership/erasures,
  no-op index retention, same-count coverage misses, empty coverage, unchanged
  successful-save baseline/dirty state, runtime sharing, live metadata queries,
  stale/corrupt stored-cache rejection, enabled/reordered Layer policy,
  missing-runtime repair, invalid-ownership fallback
  and invalid/orphan filtering.
  System.Engine.ComposableCameraSystem.MeshCamera.MedianPartitionEquivalence
  compares indexed versus linear nearest surfaces and each Layer's own hit across
  repeated centroids/triangles, odd splits, disabled rows, invalid indices, misses
  and repeated construction, with unchanged serialized topology. Existing
  SpatialIndexEquivalenceAndPruning, SpatialIndexTransformedDocument, SavedPreview,
  ResidentLoadingAndBrush, RestoredPreviewImmediateStroke and DocumentDiscard
  retain pruning, transforms, Undo and failed-save coverage.
- Verification: consumer/signature/indexed-storage audit and static delimiter,
  preprocessor/whitespace checks passed across 52 Mesh source/test files;
  git diff --check passed. These are not compilation or executed regression tests.
- Verification blocker / manual IDE steps: AGENTS.md permits compilation and
  automation only inside Rider/Visual Studio/editor. Close UE and fully build the
  editor target there because native headers changed; run the above tests. Save
  after dense Brush/Shape/Erase, after metadata-only edits, immediately during
  resident opening and repeatedly without edits. Inspect Save ms logs and pixels;
  compare cached and legacy Levels. Reload the saved Level, inspect retained Shape
  controls/erasures and test transformed runtime queries/PIE. Cancel/fail package
  saving, Discard, Undo/Redo and Save again; the independent baseline must advance
  only after success. No timing threshold or measured speedup is claimed.
- Avoid next time: persistence should consume current derived results, preserve
  indexed vertex identity and invalidate caches according to actual dependencies.
  Median selection needs a partition, not complete subtree sorting. Keep phase
  timings so package I/O is distinguishable from repeated derived-data computation.
- Possible conflicts / limits: ShowMeshLayer/Edit window lifecycle, source precision,
  reflected serialized field layout, Layer priority, projection, query outputs,
  package scope and failed-save/Discard/Undo semantics remain unchanged. Existing
  triangle-soup source is not position-welded; only existing sharing is retained.
  Explicit interaction completion can still drain pending display work. Source
  comparison/compaction, transaction serialization, native snapshots, new-runtime
  baking, saved polygon conversion, UE package writes and successful checkpoint
  copies remain event costs. Package timing includes any checkout/modal wait.

## 2026-10-08 - Held Erase delays completed coverage behind the next partial cut

- Status: source optimization and regression tests added; IDE compilation,
  automation execution and measured held-stroke latency pending.
- Symptom / exact trigger: open a populated EditMeshLayers document, select Erase
  (or hold Shift with Brush), hold left mouse and continuously move over dense
  geometry. The source queue can keep another cut partially active, delaying the
  preceding completed cut's preview. Starting over an empty indexed region also
  incurred an unnecessary full native rollback copy; dense fully covered triangles
  still ran every clipping plane.
- Why / root cause: AdvanceStrokeCoverage passed !StrokeTask as bAllowStart even
  though its queued inputs already owned complete immutable source mutations.
  The next resumable Erase therefore blocked starting a pending coverage batch.
  Source rollback capture occurred before discovering an empty candidate set.
  Every non-rejected triangle used all 34 prism planes, including safe interior.
- History / blast radius: reviewed regional Erase/remote coverage, nearest-floor
  picking, immutable async source ownership, exact mixed-operation FIFO, native
  rollback/transaction boundaries, resident opening, Undo/Discard and Save reuse
  entries. Audited all FEraseGeometryBuild::Begin/Advance, EraseShapeGeometry,
  FEraseGeometryStats and coverage-start consumers; checked queued input capture,
  cancellation, prepared publication and RememberPreview's completed-history guards.
  UE5.6's read-only MeshModelingTools/Private/MeshVertexSculptTool.cpp per-stamp
  TriangleROI/precompute notification provided the held-stroke display reference;
  no engine code was copied or modified.
- Fix: let completed queued coverage start and publish while the next source cut
  remains partial. It uses only previously captured native input, never live
  partial source/index or UObject state. Retain source/mixed-operation FIFO,
  per-mutation grid policy, cancellation and complete-revision checkpoint rules.
  Begin rejects empty indexed candidates before the queued path's rollback copy;
  successful Begin remains read-only and capture precedes any mutation. Reuse the
  existing brush-coordinate corner tests to identify triangles inside the 32-gon's
  apothem disk and depth slab with a margin. Skip splitting only for safe interior,
  preserving the original area threshold, removed bounds, descending swap-removal,
  Layer/Shape ownership, index refresh and mask finalization. Boundary triangles
  retain exact clipping, including circle-to-polygon slivers and affine axes.
  Add InteriorTriangles and EraseBegin/EraseSourceSnapshot profiling scopes.
- Touched files: editor MeshCamera/ComposableCameraMeshLayerEdMode.h/.cpp and
  ComposableCameraMeshLayerShapes.h/.cpp;
  Tests/ComposableCameraMeshLayerBudgetedStrokeTests.cpp and
  ComposableCameraMeshLayerShapeTests.cpp; DesignDoc, EditorDesignDoc, TechDoc,
  ExecutionFlowExamples and this log. No runtime or reflected serialized fields
  change in this fix.
- Regression-test names: ComposableCameraSystem.Editor.MeshCamera.ContinuousErasePreview
  checks empty-candidate rollback avoidance, starts prior completed coverage while
  the mouse is held and another source cut is partial, compares an independent
  complete preview, checks exposed lower-floor and untouched next-cut display
  heights, preserves live partial arrays/revision and cancels the whole stroke.
  ComposableCameraSystem.Editor.MeshCamera.EraseInteriorFastPath checks safe
  interior removal, sloped heights, retained 32-gon boundary slivers, partial
  boundary subtraction, Layer/Shape ownership, repeated-mask idempotence,
  scaled/sheared axes, index refresh and empty Begin/Advance no-op semantics.
  Existing BudgetedStroke, BudgetedCoverage, AsyncStrokeCoverage,
  IndexedEraseEquivalence, IndexedEraseCoverage, ResidentLoadingAndBrush,
  DocumentUndoRedo and SavePreparation cover surrounding ordering/history/storage.
- Verification: consumer/ownership/ordering review, including the final code/test
  pass, found no additional API, GC, Blueprint or runtime evaluation changes.
  Static delimiter/preprocessor/whitespace checks passed across 52 Mesh source/test
  files and git diff --check passed. These checks are not compilation or executed
  automation and do not measure visual latency.
- Verification blocker / manual IDE steps: AGENTS.md permits compilation and
  automation only through Rider/Visual Studio/editor. Close UE and fully build the
  editor target because native headers changed; run the above focused tests and
  surrounding regressions. Hold/drag dense Erase and Shift-Erase, start in blank
  space, test tilted/scaled anchors and stacked floors, then release/repress,
  Esc/cancel, Undo/Redo, Discard and Save with coverage pending. Also erase during
  resident opening and confirm no old base restores the hole. Inspect actual pixels
  and Insights EraseBegin/EraseSourceSnapshot/EraseGeometry/StrokeCoverageSnapshot/
  StrokeCoverageWorker/publication timings. No measured speedup is claimed.
- Avoid next time: a complete immutable derived input does not depend on the next
  live mutation finishing. Gate capture on source completeness, not worker start
  on source idleness. Discover indexed no-ops before extra document copies and
  classify safe convex containment before expensive boundary clipping. Preserve
  every ordered Erase/mixed source input; do not replace it with the latest source.
- Possible conflicts / limits: ShowMeshLayer/Edit window lifecycle, whole-document
  initial Edit publication, radius/depth/sampling, Layer priority, source precision,
  runtime queries and Save/Undo/Discard semantics retain their existing paths.
  Settings::Modify still serializes a transaction; the first candidate-bearing cut
  still copies native source once, even when broad-phase candidates prove uncut.
  Boundary clipping, index refresh, regional snapshots/coverage, component work and
  GPU upload remain costs; a large individual stamp can still span frames. The
  shared frame budget remains soft and requires Level-side measurement.

## 2026-10-08 - Erase assembles obsolete display meshes before coalescing

- Status: native pipeline changes and focused regressions added; IDE compilation,
  executed automation and measured dense-Level latency remain pending.
- Symptom / exact trigger: after removing the held-source start barrier, hold
  Erase or Shift-Erase and move continuously over dense existing coverage. The
  same render region can accumulate many completed source inputs and still trail
  the cursor. Region publication discards older versions only after native workers
  have already assembled each complete mesh. Mixed coarse source leaves also
  copy remote and disabled triangles into every local snapshot.
- Why / root cause: ResolveStrokeCoverage called PrepareEditPreview after every
  immutable input, before downstream QueuePreparedRegion coalescing. Normal
  prepared outputs used raw arrays, leaving bounds construction on the editor
  thread. FindVisualizationCandidates pruned 128-triangle blocks, but snapshot
  allocation/copy treated every member as relevant. Removing a scheduling gate
  alone did not reduce these repeated computations.
- History / blast radius: reviewed held-Erase snapshot safety/interior clipping,
  display FIFO latency, exact async mixed-operation ordering, persistent buffer
  reuse, resident opening/stale-region protection, history restoration, empty
  removal and Save coverage reuse. Audited all PrepareEditPreview, TakePrepared,
  Advance/HasPending/Cancel and SetGeometry/SetSharedGeometry consumers, including
  retained checkpoints, opening feedback, full/regrid and module-owned retirement.
  Read-only UE5.6 MeshVertexSculptTool.cpp's render decomposition and per-stamp
  precompute/ROI notifications remain the reference for native regional work.
- Fix: prepared jobs move at most eight waiting native inputs using a cursor;
  remaining inputs keep FIFO. Resolve every exact input. Record each region's
  final input within that job and assemble it once at that point. Disjoint regions
  can publish early; bounded jobs continue completed-prefix feedback through long
  backlogs. Full/regrid jobs emit their latest complete document. Exact region-key
  preparation deduplicates keys, preserves empty removals and never assembles the
  rectangle between distant edits. Filter regional candidates by enabled GUID and
  inclusive triangle XY bounds against whole dirty cells before Reserve/copy,
  preserving original order, neighbors and lower-floor replay. Both normal and
  opening workers produce immutable shared geometry with precomputed bounds.
  Exact bounds/index/vertex-attribute comparison retains unchanged component
  geometry identities/revisions; real changes still upload. Redundant shared
  buffers retire on the owned native pool. Add completed coverage/prepared-tile
  counters and StrokePreviewPlan profiling; no source sample is discarded.
- Touched files: editor MeshCamera/ComposableCameraMeshLayerStrokeCoverage.h/.cpp,
  ComposableCameraMeshLayerEditPreview.h/.cpp;
  Tests/ComposableCameraMeshLayerVisualizationTests.cpp and
  ComposableCameraMeshLayerEditPreviewTests.cpp; DesignDoc, EditorDesignDoc,
  TechDoc, ExecutionFlowExamples and this log. No runtime/reflected fields change.
- Regression-test names: ComposableCameraSystem.Editor.MeshCamera.ErasePreviewBatch
  queues eight real near Erases, a mixed Brush and a remote Erase; checks all ten
  coverage inputs, bounded prefix progress, three prepared regions across two jobs,
  exact final positions/tangents/normals/indices, worker bounds/current-color policy,
  immutable source ownership, gap exclusion, duplicate/empty keys, local removal,
  cancellation and latest full-plus-local output.
  ComposableCameraSystem.Editor.MeshCamera.RegionalSnapshotFiltering checks two
  enabled local floors within a leaf containing remote/disabled triangles, source
  replacement and exact retained coverage. BudgetedEditPreview now checks equal
  shared buffers retain identity/revision while equal-count changed vertices update.
  Existing ContinuousErasePreview, AsyncStrokeCoverage, FusedStrokePreview,
  BudgetedStroke, IndexedEraseCoverage, ResidentLoadingAndBrush,
  RestoredPreviewImmediateStroke, DocumentUndoRedo and SavePreparation cover
  neighboring lifecycle, precision, history and storage behavior.
- Verification: native ownership, queue-cursor/full-reset, Layer/order, shared-buffer
  lifetime and consumer/API review passed. Static delimiter/preprocessor/whitespace
  checks passed across 52 Mesh source/test files; git diff --check passed. No
  compilation or automation was executed and no timing improvement was measured.
- Verification blocker / manual IDE steps: AGENTS.md requires compilation and
  automation in Rider/Visual Studio/editor only. Close UE, fully build the editor
  target because native headers changed, and run the above regressions. Hold Erase
  and Shift-Erase over dense coverage, rapidly revisit one region and move between
  distant regions; test stacked/sloped floors and transformed anchors. Release and
  re-press with backlog, Undo/Redo, Esc/Discard, Save with pending coverage, and
  erase during resident opening. Inspect rendered holes and Insights source
  clipping, snapshot, preview planning/worker, publication and render upload costs.
  Counters/CPU checks do not establish visual latency; no measured speedup claim.
- Avoid next time: coalesce disposable display work before expensive assembly,
  while retaining authoritative source and ordered derived operations. Bound
  catch-up batches so coalescing cannot wait for the entire drag backlog. Coarse
  index hits are candidates, not mandatory snapshot contents. Move reusable native
  buffer/bounds preparation off the editor thread and retain exact no-op detection.
- Possible conflicts / limits: initial Edit remains one whole-document publication;
  Show/Edit toggles, source clipping/sampling, Layer/height priority, transaction
  and Save/Undo/Discard boundaries retain their existing paths. This is batching
  incremental coverage inputs, not reinstating initial Tile loading. Individual
  source/coverage operations, first native/transaction copies, interleaved index
  traversal, unchanged-buffer comparison, component work and GPU upload can still
  cost time; actual latency must be measured in the target Level.
