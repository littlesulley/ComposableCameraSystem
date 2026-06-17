# Bug Log

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
