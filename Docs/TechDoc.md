# ComposableCameraSystem Tech Notes

Updated: 2026-07-22

Purpose: compact implementation reference. Keep this file current when code
patterns, public APIs, hot-path rules, node catalogs, or gotchas change.

## 1. Module Map

Runtime module: `Source/ComposableCameraSystem`

- `Core`: PCM, context stack, director, evaluation tree, runtime data block,
  parameter block, type-asset instantiation.
- `Cameras`: runtime camera actor and camera type asset interfaces.
- `Nodes`: camera and compute node implementations.
- `Transitions`: transition data asset and transition classes.
- `Modifiers`: PCM-level modifier manager.
- `Actions`, `AsyncActions`: Blueprint-facing camera actions.
- `DataAssets`: type assets, patch assets, shot assets, transition table.
- `Patches`: patch handle, manager, instance, envelope, activation params.
- `LevelSequence`, `MovieScene`: Sequencer component, actor, tracks, sections.
- `Debug`: runtime panel, dumps, viewport draw.
- `Math`, `Interpolator`, `Utils`, `EditorHooks`.
- `MeshCamera`: Level-local painted surface data, query actor, and world
  subsystem profile application.

Editor module: `Source/ComposableCameraSystemEditor`

- asset definitions, factories, graph editor, toolkit, schema.
- details customizations, widgets, Sequencer track editors, Shot Editor.

UncookedOnly module: `Source/ComposableCameraSystemUncookedOnly`

- custom K2 nodes.
- graph pin widgets and pin type helpers.

## 2. Runtime Data Block

`FComposableCameraRuntimeDataBlock` is the shared storage layer used by nodes,
parameters, variables, Sequencer bags, and patches.

Storage shape:

- `Storage`: flat byte pool for POD-like values.
- `StructSlots`: `FInstancedStruct` pool for non-POD struct values.
- offset maps for node input pins, output pins, internal variables, and exposed
  values.
- shape descriptors that must match before read/write.
- actor/object mirrors for GC-visible references.

Read/write rules:

- Verify slot shape.
- Verify byte bounds in all builds.
- Verify object class compatibility for object/actor reads and writes.
- Use `Try*` accessors where offset validity is not guaranteed.
- Do not trust `check()` as a shipping guard.

## 3. Pin Types

Current `EComposableCameraPinType` coverage:

- Bool.
- Int32.
- Float.
- Double.
- Vector2D, Vector3D, Vector4.
- Rotator.
- Transform.
- Actor.
- Object.
- Struct.
- Name.
- Enum.
- Delegate.

Storage conventions:

- Enum values store as `int64`.
- Name values store as `FName`.
- Delegate values live in `FComposableCameraParameterBlock::DelegateValues`.
- Struct values use POD byte storage when safe, otherwise `FInstancedStruct`.
- Actor and Object values are mirrored for GC and class-checked.

String conversion:

- DataTable activation uses `FComposableCameraParameterBlock::ApplyStringValue`.
- Supported string targets include Bool, Int32, Float, Double, Vector2D/3D/4D,
  Rotator, Transform, Struct, Object soft path, Name, and Enum.
- Actor string conversion is not supported by the DataTable path.

## 4. Parameter Blocks

`FComposableCameraParameterBlock` carries activation-time and Sequencer-time
values.

Maps:

- `Values`: scalar / POD byte values.
- `ActorValues`.
- `ObjectValues`.
- `StructValues`.
- `DelegateValues`.

The struct opts into manual reference collection. Keep that trait if maps or
delegate targets change.

Blueprint wildcard setter caveat:

- `SetParameterBlockValue` is a custom thunk.
- Literal wildcard values can lose type information.
- Prefer generated typed K2 pins / typed setters for activation nodes.

## 5. Type Asset Build

`UComposableCameraTypeAsset` owns durable graph-derived data.

Build responsibilities:

- validate node templates.
- assign and deduplicate node GUIDs.
- build exposed parameter, internal variable, and exposed variable layouts.
- build main and compute runtime data layouts.
- build pin connection maps.
- build full execution chains.
- bind delegate pins.
- apply parameter block values.

Activation path:

```text
Blueprint/K2/DataTable
  -> PCM resolves context
  -> Director spawns camera
  -> Camera.Initialize
  -> ConstructCameraFromTypeAsset
       -> duplicate node templates
       -> build runtime data block
       -> apply params/delegates
       -> assign nodes/chains
  -> modifiers
  -> FinishSpawning
  -> evaluation tree activation
```

### Node Property Modifiers

Every `Modifiers` element serializes as an exact
`UComposableCameraModifierBase` wrapper. `bUseCustomModifierClass` selects one
of two retained branches, so toggling modes does not discard either branch:

- Node Type: `NodeTemplate` owns an instanced concrete camera node
  and `OverrideProperties` stores checked `FName` property names.
- Custom Modifier Class: `CustomModifier` owns an instanced user Blueprint/C++
  subclass. The wrapper targets that object's legacy `NodeClass`, classifies it
  as post-initialize, and invokes its Blueprint `ApplyModifier` event. Generic
  template state is never consulted in this branch.

`PostLoad` converts pre-wrapper derived array elements into exact base wrappers
and duplicates the old object beneath `CustomModifier`. Generic exact-base
entries need no migration. The custom-class picker excludes the base class,
abstract classes, deprecated classes, and superseded Blueprint classes.

Camera instances and type assets expose `FGameplayTagContainer CameraTags`.
Modifier assets expose `FGameplayTagQuery CameraTagQuery`, using UE's native
recursive ALL / ANY / NONE query editor and token-stream evaluator. Empty query
means all cameras; non-empty query calls `Matches` against the full camera tag
container. The manager stores every candidate in one node-class bucket and
filters queries only when rebuilding `EffectiveModifiers`; no global bucket or
tag sentinel exists. Higher priority wins among matching candidates.

Legacy single camera `CameraTag` properties remain hidden serialized fields.
Type-asset `PostLoad` and runtime construction migrate them into `CameraTags`.
Legacy modifier `CameraTags` containers remain hidden and migrate during
`PostLoad` through `MakeQuery_MatchAnyTags`, preserving their former OR intent.

In Node Type mode, `GetTargetNodeClass` prefers the template's class. In Custom
mode, target lookup uses the nested modifier's `NodeClass` and execution stays
post-initialize. Matching
stays exact, same as node-scoped actions. `ApplyModifierToNode` reflects only during camera
construction / reactivation, never in the per-frame tick. It validates every
stored name through `IsNodePropertyOverridable`, then copies that one property.
An instanced-object property duplicates its source subobject into the runtime
node; other property types use normal `FProperty` copy semantics. It then
registers the target field offset in the node's inline modifier-override list.
The PCM-only `ConstructCameraFromTypeAsset` overload invokes a synchronous
pre-initialize callback after node/data-block setup; generic modifiers run in
that callback, before `InitializeNodes` builds interpolator / solver caches.
Custom Blueprint `ApplyModifier` callbacks keep their original post-init timing.

`ResolveAllInputPins` skips registered modifier field offsets. This makes a
checked modifier property higher priority than the same node's graph wire or
exposed parameter without per-frame reflection or allocation. The offset list
uses `TInlineAllocator<4>` and is filled only during construction.

Eligible properties are editable instance properties only. `EditDefaultsOnly`,
transient, deprecated, and `NoModifierOverride` fields are excluded. This hides
node metadata such as `PaletteCategory` and prevents stale serialized names from
changing non-runtime state.

The editor customization is registered on the Modifier data asset, not on
`UComposableCameraModifierBase`. UE class-layout customizations do not drive
`EditInlineNew` UObject children nested in an array. An asset-level
`FDetailArrayBuilder` preserves normal array controls and normalizes new null
elements to base wrappers. The first child row is `Use Custom Modifier Class`.
Unchecked shows the node-class picker plus external node-template property rows;
checked shows a filtered custom-modifier class picker plus that instance's
editable fields. Both authored branches remain serialized while only one is
active.

UE5.6 `FPropertyHandleObject::SetValue` deliberately returns `Fail` for
`EditInlineNew` property nodes. Modifier array normalization therefore writes
the single customized asset's authoritative `Modifiers[ArrayIndex]` slot before
composing that element row. Using `SetValue` here leaks UE's default polymorphic
object picker for every null or derived entry that was not already a wrapper.
The asset's `CameraTagQuery` stays in its normal Details category and uses the
engine-provided Gameplay Tags query customization.

## 6. Camera Tick

`AComposableCameraCameraBase::TickCamera` is memoized per `GFrameCounter`.

Camera tick:

1. Start from current camera pose.
2. Walk `FullExecChain`.
3. Run node pre-actions.
4. Tick node.
5. Run node post-actions.
6. Apply set-variable entries.
7. Store pose and frame cache.

`TickWithInputPose` is used by patches and Sequencer patch overlays. It lets a
patch node graph consume the upstream pose instead of synthesizing from the
camera actor's current pose.

`InvalidateTickCache` is required when an external system re-enters evaluation
in the same frame after changing inputs, such as first-frame Sequencer shot
override fixes.

## 7. Compute Nodes

Compute nodes derive from `UComposableCameraComputeNodeBase`.

Rules:

- Run on the camera BeginPlay/initialization chain.
- Editor sync serializes the BeginPlay exec path into `ComputeFullExecChain`,
  with `ComputeExecutionOrder` kept as a compute-node-only projection.
- Concrete compute node display names use `Begin Play:` as the editor-facing
  prefix so they stay distinct from per-frame camera nodes without implying
  they tick every frame.
- Write data into the runtime data block.
- Do not run per frame.
- Level Sequence compatibility defaults to compute-only; LS internal camera
  path skips compute nodes after spawn because BeginPlay happened before node
  construction in that path.

## 8. Evaluation Tree

`FComposableCameraEvaluationTreeNode` is a `TVariant` wrapper with:

- leaf camera.
- reference leaf captured subtree.
- inner transition.

Memoization exists at camera and wrapper level. Reference leaf snapshots are
`TSharedPtr` tree roots. They do not recurse through the source director.

Transition collapse:

- finished inner transition promotes its right child.
- left source subtree is destroyed or moved to pending destroy as needed.
- right target remains dominant.

When adding a new variant alternative, update every manual branch and debug
builder, not only `Visit()`.

## 9. Transitions

Transition duplication must be null-checked. Null means hard cut fallback.

Transition init uses:

- current source pose.
- previous source pose.
- delta time from a guarded `SafeDeltaSeconds`.
- a cached typed outer `AComposableCameraPlayerCameraManager`, used by
  transitions that need owner-aware actor input or temporary camera
  initialization.

Fallback rule: a transition that cannot compute should return one input pose,
usually target pose for activation-style transitions. Never return a default
constructed pose as an error fallback.

`UComposableCameraCompositionPreservingTransition` wraps a
`DrivingTransition`. On begin play it captures the subject actor offset from
the source pose in source-camera local space. If the wrapper transition's
`TransitionTime` is unset or zero, it adopts the `DrivingTransition` duration so
the wrapper does not finish before the driving transition can tick. Subject
lookup uses the base transition's cached PCM when `SubjectActorSource` is
`ControllerControlledPawn`, so split-screen or multi-controller worlds do not
fall back to the wrong first world controller. Each tick:

- evaluate `DrivingTransition(CurrentSourcePose, CurrentTargetPose)`.
- take the driving pose rotation as `R'` and the driving transition percentage
  as the blend weight.
- blend non-transform pose fields from current source pose to current target
  pose.
- recompute the live target-camera local offset from
  `CurrentTargetPose` and `SubjectLocation`.
- compute camera position as
  `SubjectLocation - R'.RotateVector(Lerp(CapturedSourceOffset, LiveTargetOffset, Percentage))`.
- overwrite output rotation with `R'`.

The target offset must be live, not captured at transition start. Otherwise a
moving subject can leave the near-final preserved pose offset from
`CurrentTargetPose`, and the evaluation tree collapse back to raw B will snap.

If the subject actor cannot be captured or disappears, the transition returns
the driving pose. If `DrivingTransition` is unset, it logs and falls back to the
target pose.

`UComposableCameraPathGuidedTransition` also uses the base cached PCM when it
initializes its temporary intermediate camera. It must not ask the Blueprint
library for player index 0, because the active PCM can belong to a different
local player.

## 10. Context Stack

`EnsureContext` can reorder stack entries. It moves an existing context to top.

Pop rules:

- base cannot pop.
- inactive pop destroys immediately.
- active pop resumes previous camera in place.
- transition pop holds popped context in pending destroy.
- transient camera finish triggers auto-pop.

`PendingDestroyEntries` directors may still be reachable through captured
reference subtrees. Do not destroy them before the owning transition finishes.

## 11. Camera Patches

Runtime path:

```text
AddPatch
  -> resolve activation params from per-call overrides + asset defaults
  -> spawn transient evaluator camera
  -> Initialize(nullptr)
  -> ConstructCameraFromTypeAsset
  -> insert by layer index

Director::Evaluate
  -> tree pose
  -> PatchManager::Apply
       -> advance envelope
       -> check expiration
       -> TickWithInputPose
       -> BlendBy alpha
       -> sweep expired
```

Activation params use paired override booleans. Unchecked means asset default.
Checked means caller value wins, including literal zero.

Expiration:

- Duration after Active phase starts.
- Manual through `ExpirePatch`.
- Condition through patch asset `CanRemain`.
- `bExpireOnCameraChange`.

Sequencer patch sections use a separate overlay map on
`UComposableCameraLevelSequenceComponent`. They sort by effective layer index,
apply latest parameter bags, tick evaluators, and blend by section envelope
alpha.

## 12. Level Sequence

Main types:

- `AComposableCameraLevelSequenceActor`.
- `UComposableCameraLevelSequenceComponent`.
- `FComposableCameraTypeAssetReference`.
- `UMovieSceneComposableCameraShotSection`.
- `UMovieSceneComposableCameraPatchSection`.
- corresponding tracks and track instances.

LS component rules:

- Creates an internal transient `AComposableCameraCameraBase`.
- Initializes with `Manager=nullptr`.
- Suppresses actor tick.
- Rebuilds parameter/variable bags from the TypeAsset.
- Reapplies bags to runtime data each tick.
- Applies shot overrides before `TickCamera`.
- Applies patch overlays after `TickCamera`.
- Projects final pose to `UCineCameraComponent`.
- Destroys internal camera and overlay evaluators on unregister/end play.

Hot-path asset resolution:

- Shot section soft references are cached off the eval path.
- Eval path does not call blocking `LoadSynchronous`.
- Null cached transition means hard cut / no blend.

## 13. Shot Solver

Core types:

- `FComposableCameraTargetInfo`.
- `FComposableCameraShotTarget`.
- `FComposableCameraShot`.
- `FComposableCameraShotSolveParams`.
- `FComposableCameraShotSolveResult`.
- `UComposableCameraShotSolver`.
- `UComposableCameraCompositionFramingNode`.

Pipeline:

```text
resolve targets
  -> anchor
  -> placement
  -> aim
  -> lens/FOV
  -> focus
  -> roll
```

The solver is mostly closed-form. Screen-space zones use prior pose/state for
damping. Keep target, placement, aim, lens, focus, and roll ownership separate.

## 14. Editor Round Trip

Graph source of truth in editor:

- Designers edit `UComposableCameraNodeGraph`.
- Save/build calls `SyncToTypeAsset`.
- Asset open/load calls `RebuildFromTypeAsset`.
- `EditorGraph` is transient.
- Runtime data lives on `UComposableCameraTypeAsset`.

Any new editor-side state needs both directions:

- Graph -> TypeAsset.
- TypeAsset -> Graph.

GUID stability matters. Never regenerate node identity unless the node truly is
new.

## 15. Debug

Runtime debug:

- `FComposableCameraContextStackSnapshot`.
- flattened DFS tree snapshots.
- patch snapshots from PCM path and Sequencer path.
- runtime panel and pose history panel.
- `CCS.Dump.*`.
- viewport debug draw CVars.
- viewport gizmo colors live in `FComposableCameraViewportDebugColors`; the
  panel Legend reads `FComposableCameraViewportDebug::GetLegendEntries()` so
  swatches and 3D markers share one source of truth. Legend rows still require
  the matching viewport debug CVar or `*.All` shortcut, but the panel filters
  them again through `ComposableCameraViewportDebugLegendUtils`: node rows must
  match a node class on the current `RunningCamera`, and transition rows must
  match an `InnerTransition` class in the active context tree snapshot.
- `FComposableCameraDebugDrawSink` is the primitive emission adapter. The live
  sink sends line / point / sphere / box / plane / frustum calls to Unreal debug
  draw helpers and keeps solid spheres routed through
  `FComposableCameraViewportDebug::DrawSolidDebugSphere` in non-shipping builds.
  The capture sink records the same calls as `FComposableCameraDebugPrimitive`
  values for rewind trace serialization. It also returns true from
  `ShouldForceDrawAllNodeGizmos` and
  `ShouldForceDrawAllTransitionGizmos`, so trace writers capture all 3D gizmos
  without enabling or refreshing live viewport CVars. The live sink keeps the
  default false values. It is a transient C++ adapter; its non-owning `UWorld*`
  is not a `UPROPERTY`. Sphere / solid-sphere primitives
  store segment count in `Size`, line thickness in `Thickness`, and optional
  marker text in `Label`; box primitives store line thickness in `Thickness`;
  plane primitives store center in `A`, normalized normal in `B`, and
  two-dimensional extents in `Extent.X/Y`. For `CameraFrustum` primitives only,
  `Radius` stores FOV, `Size` stores ortho width, and `Thickness` stores debug
  frustum scale. Raw default constructed primitives are not valid frustums; use
  `MakeCameraFrustum`, whose scale default is 1.0.
- Rewind trace emission is editor-only even though the frame data is produced
  by runtime classes. `UE_COMPOSABLE_CAMERA_TRACE` must include `WITH_EDITOR`,
  and `ComposableCameraSystem.Build.cs` must add `TraceLog` only when
  `Target.bBuildEditor` is true. Non-editor packaged targets should not compile
  `FComposableCameraTrace`, `CCS.Debug.Trace`, ObjectTrace includes, or
  PCM / LS trace writer bodies.
- Gameplay PCM trace capture lives in `AComposableCameraPlayerCameraManager`.
  `TraceCCSEvaluationFrame` must early-return on
  `FComposableCameraTrace::IsTraceEnabled()` before reserving primitive storage.
  It records the evaluated CCS pose, context, camera type asset name, owning
  PC/pawn/view target ids, and sink-captured camera / transition gizmos.
  `TraceActiveCameraFrame` records the final `FMinimalViewInfo` after
  `FillCameraCache`. Both records share one `FPlatformTime::Cycles64()` value
  sampled at the start of `DoUpdateCamera`.
- Level Sequence trace capture lives in
  `UComposableCameraLevelSequenceComponent`. It samples one
  `FPlatformTime::Cycles64()` value before ticking the internal camera, returns
  a projection status from `ProjectPoseToCineCamera`, and emits a
  `CCS_LevelSequence` evaluation frame after projection. The function must
  early-return on `FComposableCameraTrace::IsTraceEnabled()` before reserving
  primitive storage. It captures only internal-camera gizmos; there is no LS
  context stack / director transition tree to draw.
- `FComposableCameraViewportDebug::DrawSolidDebugSphere` accepts an optional
  short `Label`. Sink-routed sphere gizmos pass the same label through the live
  sink and capture sink, and primitive stream version 2 serializes it in
  `FComposableCameraDebugPrimitive::Label`. Labels use
  `GetSphereLabelDurationSeconds() == 0.f`; do not make them persistent, or HUD
  debug text will remain at stale world positions while the sphere moves.

Editor debug:

- selected runtime instance picker in type asset editor.
- graph overlay of live node data.
- node tooltips append live `Runtime Parameters` from copied graph-node debug
  state. `SnapshotDebugState` prefers each declared input's resolved data-block
  slot, including exact reflected export for struct slots. It falls back to the
  runtime UPROPERTY when no slot exists; modifier-owned fields intentionally use
  that property because modifiers outrank pins. Remaining editable node and
  subobject properties follow. Runtime-data presence is tracked independently
  of active-node glow so skipped nodes remain inspectable. Slate consumes
  strings only and never follows runtime node pointers.
- `SComposableCameraGraphNode::GetToolTip` conditionally supplies a lazy
  interactive `SToolTip` only while runtime debug data exists. It keeps the
  normal `SGraphNode` tooltip path for authoring mode, caches one card for the
  current hover, and drops it from `OnToolTipClosing`. Because UE interactive
  tooltips intentionally remain open after leaving their source, the node Tick
  closes it only after neither node nor card is hovered for a short grace
  interval. Parameter text attributes capture only a weak graph-node pointer
  plus row index. Theme-aware rounded brushes live in
  `FComposableCameraEditorStyle`; do not fall back to CoreStyle's bright
  `ToolTip.Background` for this card.
- Runtime hover card content is shared by its transient `SToolTip` and a pinned
  `SWindow`. Pin detaches the existing card widget, captures the tooltip host's
  screen position, closes the reusable tooltip host, and inserts the same card
  at that position into at most one native child observer per graph-node Slate
  widget. Tooltip-host positions are already physical desktop coordinates;
  disable initial `SWindow` DPI size/position adjustment or high-DPI desktops
  scale the position twice. Repeated requests foreground it; owner destruction
  closes it. Pinned attributes read only weak graph-node state;
  active/idle/no-data status stays live without retaining PIE runtime objects.
- `SComposableCameraRuntimeDebugPanel` provides the default-left aggregate
  view. It filters copied graph-node state to active camera nodes, keeps
  expansion state by weak graph-node identity, and rebuilds `SListView` rows
  only for membership/filter/parameter-count/expansion changes. Pose and
  parameter text attributes remain live weak reads. Search matches title, class display name,
  and parameter labels. List selection is disabled; programmatic navigation
  scrolls directly and drives an outer content tint plus hit-test-invisible
  node-color overlay through a 1.25-second linear `FCurveSequence`. Every
  navigation restarts the sequence, producing clear whole-item feedback without
  selected-row blue. Headers omit parameter-count text. Non-empty membership,
  parameter-shape, and expansion refreshes arm one post-generation layout pass;
  `OnItemsRebuilt` consumes that flag and calls `RequestListRefresh` once more.
  The second pass reuses generated rows after expanded/wrapped DesiredSize values
  stabilize, recalculates collapsed-row scroll range and lower-row
  virtualization, and clears the flag before requesting refresh, preventing
  callback loops.
- Runtime-debug navigation uses
  `UComposableCameraNodeGraph::RequestShowRuntimeDebug`, a non-serialized
  multicast delegate bound by the owning toolkit. Double-click and the active-
  only `Show Debug Information` context action route through this bridge. The
  toolkit invokes `RuntimeDebugTabId`, clears hiding search text, expands the
  target row, requests scroll into view, and starts the focus fade. Remove the
  delegate in toolkit teardown before releasing the rooted graph.
- runtime previewer tab showing visible-subject-local camera relation.
- Rewind Debugger trace ingestion through the editor `Trace` folder:
  `FComposableCameraTraceModule` registers a TraceServices module,
  `FComposableCameraTraceAnalyzer` decodes `ComposableCameraSystem` trace
  events, and `FComposableCameraTraceProvider` exposes active-camera and
  CCS-evaluation point timelines for Rewind tracks. The same folder also owns
  `FComposableCameraRewindDebuggerExtension`, which toggles the CCS trace
  channel during recording and draws the recorded active camera frustum plus
  matched CCS primitives during playback, and
  `FComposableCameraRewindDebuggerTrackCreator`, which exposes a Pawn child
  track named `Composable Camera`.
- `CCS.Editor.Dump.*`.

Snapshot rule: resolve runtime pointers to names and value copies early.

Rewind provider technique:

- Provider append functions must run under `FAnalysisSessionEditScope` and call
  `Session.WriteAccessCheck()`.
- Timeline reads must run under a session read scope and call
  `Session.ReadAccessCheck()`.
- Rewind Debugger target lookup is also a trace read in UE 5.6:
  `IRewindDebugger::GetTargetActorId()` reaches `IGameplayProvider`, so CCS
  playback code must call it only while holding `FAnalysisSessionReadScope`.
- Provider timeline getters store timelines as `TSharedRef<TPointTimeline<...>>`;
  `TSharedRef::Get()` returns a reference, so return `&Timeline.Get()` when the
  getter exposes `const ITimeline<...>*`.
- Event time comes from `Context.EventTime.AsSeconds(Cycle)` so active and
  evaluation frames with the same runtime cycle align in Rewind playback.
- Serialized primitive arrays are copied out of trace event storage, decoded
  through `DeserializeComposableCameraDebugPrimitives`, and kept empty if the
  stream is malformed.
- Rewind playback drawing uses the active-camera trace as the authoritative
  rendered pose. CCS evaluation primitives are drawn only when a matching
  evaluation frame is found. Gameplay PCM frames match by PCM id plus frame
  cycle; Level Sequence evaluation frames match the active view target actor so
  they still pair when the PCM active-camera source is `Unknown`.
- Rewind 3D primitive replay submits line-batcher primitives from an
  `FTSTicker` callback, not from `UDebugDrawService`. Game viewport rendering
  flushes non-persistent line batchers before the debug-draw service fires; if
  the service submits 3D primitives, they render a frame late and visibly jitter
  while scrubbing. The debug-draw service is kept only for Canvas text labels.
- Playback frame caches are keyed by trace time and target actor id; target
  selection changes must re-query even when the scrub time has not moved.
- Primitive replay must preserve the runtime primitive payload: sphere segment
  count from `Size` clamped to `[4, 32]`, sphere label from `Label`, line / box
  thickness from `Thickness`, frustum scale from `Thickness` with a fallback of
  1.0, and plane center / normal / extents from `A`, `B`, and `Extent.X/Y`.
  The active-camera frustum synthesized by the extension uses scale 1.0 to
  match `AComposableCameraCameraBase::DrawCameraDebug`; do not use the larger
  Blueprint camera helper scale. Rewind sphere labels are projected with
  `UCanvas::Project` and drawn with `FCanvasTextItem`; do not use
  `DrawDebugString` there because it writes through `AHUD::AddDebugText`, and
  Rewind's visualized world may not have a HUD/player-controller text path.

Runtime Previewer technique:

- Camera Type Asset layout v3 opens `RuntimeDebugTabId` in the left observer
  stack and keeps `RuntimePreviewerTabId` closed in that same stack.
- `SComposableCameraRuntimePreviewer` follows the Shot Editor viewport lifetime
  pattern: widget owns `FAdvancedPreviewScene`, viewport client borrows it, and
  widget destruction clears `ViewportClient->Viewport` before draining scene
  resources.
- Toolkit `DebugTick` pushes slim `FComposableCameraRuntimePreviewData` after a
  valid `SnapshotDebugState()` call. It copies only pawn weak pointer, pawn
  velocity, visual subject transform, camera position/rotation/FOV, context,
  and active-state; it does not store full `FComposableCameraPose` in Slate
  because that pose owns post-process settings with UObject references.
- `SetPreviewData` actively refreshes proxy pose, camera markers, floor offset,
  and viewport invalidation on the same Slate/game-thread handoff. Do not make
  runtime sync depend only on `FEditorViewportClient::Tick`; docked editor
  viewports may otherwise sleep between invalidations.
- Pawn proxy transforms are translation-relative via
  `MakeTranslationRelativeTransform`: subtract the visual subject translation
  but preserve each source transform's world rotation and scale.
  Subject transform selection prefers the skeletal root bone world transform,
  then a valid static mesh component, then the pawn actor transform. The
  skeletal root-bone anchor supplies the origin location for the copied pose.
  Do not apply the subject inverse rotation to proxy transforms; doing so eats
  the character's real runtime rotation.
- Runtime camera markers use `MakeCameraPreviewTransform`: subtract subject
  translation only and preserve the runtime camera rotation from
  `Snapshot.FinalPose`. Do not transform camera rotation through the subject
  rotation; character facing, root motion, or strafe pose changes must not
  create fake camera rotation.
- The preview floor offset is derived from proxy bounds with
  `ComputeFloorOffsetForBounds`, so a Character mesh whose root sits below the
  capsule/pawn origin is not clipped by the default `FAdvancedPreviewScene`
  floor.
- Skeletal pawn proxies use `ASkeletalMeshActor` plus direct
  component-space-transform copy:
  source `GetComponentSpaceTransforms()` -> proxy
  `GetEditableComponentSpaceTransforms()` ->
  `ApplyEditedComponentSpaceTransforms()`.
- If the source/proxy transform arrays are empty or have different counts, the
  previewer destroys the skeletal proxy, switches to the static fallback marker,
  and tracks the failed skeletal mesh so it does not rebuild into the same bad
  proxy every tick.
- Proxy animation and component ticking stay disabled so the copied pose is not
  overwritten by an editor-preview animation tick.
- The observer camera is the normal `FEditorViewportClient` camera. It is not
  coupled to runtime camera data.

Shot Editor quick-control technique:

- `SShotEditorRoot` keeps Quick controls in a collapsed-by-default strip as a
  mirror of selected `FComposableCameraShot` fields, not a parallel data model.
- Quick collapsed state and the viewport toolbar collapsed state are persisted
  in `GEditorPerProjectIni` under `ComposableCameraSystem.ShotEditorLayout`;
  defaults are Quick collapsed and viewport toolbar expanded.
- Quick numeric widgets use a per-widget `TOptional<float>` drag cache. The
  cache updates while editing; the Shot writes once on text commit or slider
  release through `FScopedTransaction`, host `Modify()`, and
  `PostEditChangeProperty(ValueSet)`.
- Quick controls must resolve the active Shot property the same way the Details
  bridge does: section inline shots post `InlineShot`, asset-reference override
  shots post `ShotOverrides`, and ShotAsset / node hosts post `Shot`.
- Do not fire per-tick `PostEditChangeProperty(Interactive)` from Quick
  controls. Sequencer-backed shot sections can respawn preview actors during
  those broadcasts.

Shot Editor status bar technique:

- `SShotEditorRoot::TrySetMode` classifies mode requests through
  `Widgets/ComposableCameraShotEditorModeSwitchUtils.h`; Free -> Drag / Lock
  does not apply immediately.
- `Widgets/ComposableCameraShotEditorStatusBarUtils.h` keeps status-bar
  priority and action mapping pure and testable. Clean active shots hide the
  bar, no-shot states show info, stale hosts show warning, and active Free-exit
  requests show warning plus Free-exit actions.
- Liveness states win over pending actions. A stale host or missing Shot must
  suppress Save / Discard / Stay, because Save writes through the active host.
- Free-exit requests cache the target mode and reverse-solve status, then show
  Save / Discard / Stay in the unified status bar below the top bar. Save
  calls `ReverseSolveCurrentCameraToShot()` before applying the pending mode;
  Discard applies the pending mode without writing Shot data; Stay clears the
  pending request and remains in Free.
- Free-exit actions are hidden when no pending request exists or the viewport
  has already left Free. Active Shot context swaps clear pending requests to
  avoid applying a stale Free camera pose to a different host.

Shot Editor mode-sensitive Details technique:

- Shot Details keeps the runtime structs unchanged and applies editor-only
  `IPropertyTypeCustomization` visibility gates for `FShotPlacement`,
  `FShotAim`, `FShotLens`, `FShotFocus`, and `FComposableCameraAnchorSpec`.
- Visibility rules live in `ComposableCameraShotModeVisibility.h` so Slate
  customizations and automation tests share the same mapping. Unknown fields
  default to visible; explicit mode branches only collapse rows that the solver
  ignores for the active mode.
- Keep cross-layer anchor dependencies visible. `Focus.FollowPlacementAnchor`
  can consume `Placement.PlacementAnchor`, and `Focus.FollowAimAnchor` can
  consume `Aim.AimAnchor` even when the corresponding Placement / Aim mode
  does not use that anchor locally.
- Hidden rows are not reset or rewritten. The values stay serialized and
  become editable again when the relevant mode is selected.

Shot Editor Sequencer-shot menu technique:

- The `Shots` dropdown is custom Slate content, not a plain `FMenuBuilder`
  list. It uses `SSearchBox` plus `SScrollBox` so filtering happens in place
  without closing the combo menu.
- Search matching lives in `Widgets/ComposableCameraShotMenuUtils.h` and is
  covered by automation tests. It tokenizes the user filter and requires every
  token to match the combined track label, shot title, or time/row suffix.
- The menu walks object-binding tracks and root tracks, de-duplicates sections,
  groups visible rows by track label, marks the active section with a check
  icon, and shows the same time/row suffix used by the Shot Editor breadcrumb.

## 16. Built-In Camera Nodes

Current node classes:

- `UComposableCameraAutoRotateNode`
- `UComposableCameraBeginPlaySetRotationNode`
- `UComposableCameraBlueprintCameraNode`
- `UComposableCameraCameraOffsetNode`
- `UComposableCameraCollisionPushNode`
- `UComposableCameraCompositionFramingNode`
- `UComposableCameraComputeDistanceToActorNode`
- `UComposableCameraComputePositionBetweenActorsNode`
- `UComposableCameraControlRotateNode`
- `UComposableCameraDirectionalMoveNode`
- `UComposableCameraExposureNode`
- `UComposableCameraFieldOfViewNode`
- `UComposableCameraFilmbackNode`
- `UComposableCameraFocusPullNode`
- `UComposableCameraHitchcockZoomNode`
- `UComposableCameraImpulseResolutionNode`
- `UComposableCameraLensNode`
- `UComposableCameraLockOnAimPointNode`
- `UComposableCameraLookAtNode`
- `UComposableCameraMixingCameraNode`
- `UComposableCameraOcclusionFadeNode`
- `UComposableCameraOrthographicNode`
- `UComposableCameraPivotDampingNode`
- `UComposableCameraPivotLookAheadNode`
- `UComposableCameraPivotOffsetNode`
- `UComposableCameraPivotRotateNode`
- `UComposableCameraPostProcessNode`
- `UComposableCameraReceivePivotActorNode`
- `UComposableCameraRelativeFixedPoseNode`
- `UComposableCameraRotationConstraints`
- `UComposableCameraScreenSpaceConstraintsNode`
- `UComposableCameraScreenSpacePivotNode`
- `UComposableCameraSetRotationNode`
- `UComposableCameraSplineNode`
- `UComposableCameraSpiralNode`
- `UComposableCameraTwoPointMoveNode`
- `UComposableCameraViewTargetProxyNode`
- `UComposableCameraVolumeConstraintNode`

Node notes:

- `UComposableCameraCameraOffsetNode` applies `CameraOffset` in camera-local
  space (`X=forward, Y=right, Z=up`). Its inline
  `ForwardOffsetDeltaByPitchCurve` samples X as current pitch in degrees and
  adds Y as a camera-forward delta in cm before building the final position.
- `UComposableCameraComputePositionBetweenActorsNode` runs on the BeginPlay
  compute chain. It resolves both actor endpoints through
  `EComposableCameraActorInputSource`, clamps `Alpha` to `[0, 1]`, linearly
  interpolates world locations, then adds `HeightOffset` on world Z before
  publishing `Position`.
- `UComposableCameraSetRotationNode` and
  `UComposableCameraBeginPlaySetRotationNode` share the same resolver. Rotation
  source values are `FromActor`, `FromVector`, `FromRotator`, and
  `FromTwoActors`. `FromTwoActors` resolves both endpoints through
  `EComposableCameraActorInputSource`, builds a rotation from first actor to
  second actor, then all source modes apply `RotationOffset` as
  `WorldYaw * Base * LocalPitchRoll`. This matches `ControlRotate` semantics:
  yaw is around world Z, while pitch / roll are authored in the resolved local
  camera frame.

Base classes:

- `UComposableCameraCameraNodeBase`
- `UComposableCameraComputeNodeBase`

## 17. Built-In Transitions

- `UComposableCameraLinearTransition`
- `UComposableCameraSmoothTransition`
- `UComposableCameraEaseTransition`
- `UComposableCameraCubicTransition`
- `UComposableCameraInertializedTransition`
- `UComposableCameraCylindricalTransition`
- `UComposableCameraSplineTransition`
- `UComposableCameraPathGuidedTransition`
- `UComposableCameraDynamicDeocclusionTransition`
- `UComposableCameraCompositionPreservingTransition`
- `UComposableCameraViewTargetTransition`

## 18. Built-In Actions and Interpolators

Actions:

- `UComposableCameraMoveToAction`
- `UComposableCameraResetPitchAction`
- `UComposableCameraRotateToAction`

Interpolators:

- `UComposableCameraInterpolatorBase`
- `UComposableCameraIIRInterpolator`
- `UComposableCameraSimpleSpringInterpolator`
- `UComposableCameraSpringDamperInterpolator`

## 19. Current Automation Tests

Existing test files include:

- `ComposableCameraBugFixTests.cpp`
- `ComposableCameraCompositionPreservingTransitionTests.cpp`
- `ComposableCameraComputePositionBetweenActorsNodeTests.cpp`
- `ComposableCameraShotSolverTests.cpp`
- `ComposableCameraPivotLookAheadNodeTests.cpp`
- `ComposableCameraLockOnAimPointNodeTests.cpp`
- `ComposableCameraModifierPropertyOverrideTests.cpp`
- `ComposableCameraDebugSnapshotTests.cpp`
- `ComposableCameraNodeGraphSyncTests.cpp`
- `ComposableCameraNodeRuntimeTooltipTests.cpp`
- `ComposableCameraRuntimeDebugPanelTests.cpp`
- `ComposableCameraSetRotationNodeTests.cpp`
- `ComposableCameraMeshSurfaceTests.cpp`
- `ComposableCameraMeshProfileTests.cpp`
- `ComposableCameraMeshProfileCustomizationTests.cpp`
- `ComposableCameraMeshLayerToolSettingsTests.cpp`
- `ComposableCameraMeshLayerVisualizationTests.cpp`

Codex must not invoke Unreal automation from shell in this project. Run tests
inside Rider or Visual Studio / Unreal Editor.

## 20. Hot-Path Rules

Assume these are hot:

- PCM update.
- context stack evaluation.
- director evaluation.
- evaluation tree walk.
- camera node tick.
- patch apply.
- Sequencer component tick.
- shot solver.
- mesh surface query and stable-profile subsystem tick.

Rules:

- No heap allocation unless pre-reserved or justified.
- No `LoadSynchronous`.
- No FString formatting in per-frame loops.
- No container mutation that can reallocate during iteration.
- Snapshot mutable callback lists before invoking Blueprint callbacks.
- Use weak pointers in snapshots that can survive arbitrary Blueprint work.
- Cache soft object resolution outside the eval path.

## 21. Gotchas

- `EnsureContext` means "exists and top", not merely "exists".
- `ReferenceLeaf` captures tree topology. It is not a live director pointer.
- Same camera UObject can be reached twice in one frame through snapshots.
- A patch evaluator is a transient camera actor, not a node grafted into the
  main camera.
- Patch activation override booleans are semantic. Zero is a valid value.
- Sequencer shot override can arrive after component tick; first-entry path must
  invalidate tick cache and evaluate at zero delta.
- `AddRaw` delegates owned by Slate widgets must be explicitly unbound in the
  toolkit destructor.
- Runtime data-block shape checks and byte-bounds checks are independent.
- Object/Actor pin class constraints still need earlier layout-time diagnostics;
  runtime guards prevent corruption but do not give the best authoring message.
- Local-player subsystem caches need weak pointers plus parent identity checks.
- Mesh active Layer identity is `(StorageActor, LayerGuid)`, not GUID alone:
  Level Instance copies can contain identical serialized Layer GUIDs. Cleanup
  must remove each Layer's duplicated Modifiers and its own temporary Context.
- FOV may be stored as FieldOfView or FocalLength. Use pose helper methods for
  effective FOV.
- Focus distance uses sentinel behavior. Do not blend invalid focus distance as
  a real distance.
- UE automation `UTEST_EQUAL` has no `FName` overload in UE 5.6. Use
  `UTEST_TRUE(NameA == NameB)` or compare strings when testing `FName`.
- Interpolator `Run()` returns an absolute value, not a delta. If a scalar
  damping helper computes only `Target - Current` progress, add it back to the
  current value before returning; Spline, FocusPull, and VolumeConstraint reset
  double interpolators from their last smoothed output each frame.
- Viewport Legend `Nodes.All` / `Transitions.All` means "show all relevant
  current-camera / active-transition legend rows", not the entire palette. Keep
  legend filtering tied to the same runtime classes that can actually draw this
  frame.
- Automation-test helpers in anonymous namespaces still need file-specific
  names. UE unity builds can concatenate multiple test `.cpp` files into one
  translation unit, where two same-signature anonymous-namespace helpers with
  the same name become duplicate definitions.
- A virtualized `SListView` does not automatically revise cached variable row
  heights when a nested `SExpandableArea` changes state. Route visible
  expansion changes through list refresh plus post-rebuild measurement.
- A non-owning `FStructOnScope` bound to a `TArray` element becomes invalid when
  add/remove/reorder relocates or replaces that element. Clear the structure
  Details view before mutation, then bind a fresh scope afterward.
- Forward declarations must use the same class-key as existing UE/project
  declarations. In particular, declare `FSpawnTabArgs` as `class`; MSVC C4099
  becomes a build failure when warnings are treated as errors.
- Lambdas returning a typed index in one branch and `INDEX_NONE` in another
  need an explicit `-> int32` return type. `INDEX_NONE` is an anonymous-enum
  sentinel, so implicit deduction fails with MSVC C3487.
- Do not mix `TObjectPtr<T>` and raw `T*` in a conditional expression. Call
  `.Get()` first, or use explicit branches when returning `TSubclassOf<T>` from
  a `UClass*`. In UE 5.6, include `PropertyHandle.h` for `IPropertyHandle`.
- Type-asset identity fields are copied after camera `Initialize()`. Any cache
  derived from `CameraTags` must refresh at the copy site; use
  `AComposableCameraCameraBase::RefreshCameraTags()` rather than updating the
  container and cached trace label independently.
- UE module dependencies are not linker-transitive. A module that directly
  calls exported `FGameplayTagContainer` / `FGameplayTagQuery` methods must list
  `GameplayTags` in its own Build.cs, even when a depended-on runtime module
  already lists it.
- Editor-module shutdown runs after UE's global Level Editor mode manager can
  be destroyed. Gate every shutdown-time `GLevelEditorModeTools()` access with
  `!IsEngineExitRequested()`; otherwise the accessor emits an ensure and
  recreates a mode manager during teardown.

## 22. Build and Verification

For this project:

- Do not invoke UBT, Build.bat, RunUBT.bat, dotnet, msbuild, Unreal Editor, or
  automation tests from Codex shell.
- Compile inside Rider or Visual Studio.
- Header/reflection/module changes require full editor restart, not Live Coding.
- Docs-only changes do not need a compile, but a non-trivial code-adjacent doc
  sweep should still be reviewed against source.
- For graph exec-chain serialization, variable identity and variable-node
  identity are different. `SetVariable` entries must store the exact graph
  node GUID and use variable GUID only as legacy fallback; otherwise a
  same-variable Get node can capture the rebuild lookup and drop exec wires
  after save/reopen.

## 23. Mesh Camera Surface Query

Runtime types:

- `FComposableCameraMeshLayerDefinition`: GUID, name, profile, enabled state,
  debug color. Array index is editor display order; index zero is topmost.
- `FComposableCameraMeshSurfaceAuthoringData`: editor-only full source using
  one stable Layer GUID per triangle.
- `FComposableCameraMeshSurfaceRuntimeData`: cooked indexed triangles using
  one Layer array index per triangle.
- `AComposableCameraMeshSurfaceStorageActor`: hidden, `NotPlaceable`,
  Level-local serialization anchor.
- `UComposableCameraMeshWorldSubsystem`: loaded-storage registration,
  per-local-player query, profile switching.

MVP query:

```text
player world position
  -> each storage actor inverse transform
  -> local downward ray against indexed triangles
  -> nearest surface
  -> collect every enabled Layer within SameSurfaceTolerance
  -> top-to-bottom Layer-order results
```

No `UStaticMesh` or collision query mesh is involved. Query outputs use inline
capacity for 16 overlapping Layers; deeper overlap may allocate. The current
query uses two linear triangle passes. A later BVH/tile index may replace
traversal without changing actor, profile, or tool contracts.

`UComposableCameraMeshProfile` stores an embedded
`FComposableCameraParameterTableRow Camera` plus the existing Modifier asset
array. `FComposableCameraParameterTableRow::BuildParameterBlock` is the shared
string-to-typed block path for both DataTable and Mesh Profile activation;
do not duplicate parameter/default/orphan handling in new callers.

Layer Profile Modifier assets are templates. The subsystem duplicates them with the
player camera manager as Outer. This prevents mesh removal from unregistering
the same asset instance owned by another gameplay system. When a Profile also
activates a Camera Type, `ReplaceModifiers(..., false)` updates candidates
without reactivating the old camera. `OnTypeAssetCameraConstructed` resolves and
applies those candidates to the new camera. Failed Camera activation explicitly
falls back to `OnModifierChanged`. Modifier-only Layer edges update candidates,
then refresh the currently active camera once.

The subsystem stores an active set keyed by storage actor plus Layer GUID. Each
active Layer owns its duplicated Modifier instances. Every Camera-bearing Layer
also owns a separate temporary Context; entering a nested Layer pushes above
the outer Context, and exit pops only that Layer. Lower camera instances retain
their Director/tree state and resume in place. A Camera-less Layer creates no
Context. Debug hints use `Mesh_<LayerName>_<LayerGuid>`; the context stack
sanitizes and preserves the readable hint while adding a collision-free serial.
The shared row's authored `ContextName` is ignored for Mesh. Mesh forces
`bIsTransient=false` because Layer presence owns lifetime. Each entry
transaction captures the source Director before pushing, then activates with a
reference source. Activation failure rolls back only that empty Layer Context.
Exits process in reverse entry order. Simultaneous first hits process bottom
list rows first, making the top row the deterministic top Context.
When an active Layer Context pops, the resumed lower camera already contains
the correct pre-entry properties. `RefreshEffectiveModifierSelection` updates
only ModifierManager bookkeeping so removed duplicated assets are released;
calling `OnModifierChanged` there would rebuild and reset the resumed camera.
Camera Type and Transition soft references sync-load only on a Profile edge,
never on the per-frame unchanged fast path.

Editor technique:

- Mesh Profile Details has ordered Camera, Modifier, Action, and Patch
  categories. Camera hides its parent property row and explicitly adds the four
  relevant children of its embedded parameter row, so the existing exposed-value
  customization still reaches the sibling CameraType without emitting a second
  trailing Camera field. ContextName is omitted because Layer identity creates
  the runtime Context. Action/Patch currently show reserved messages only.
- Tool works on a transient document.
- Layer selection is GUID-based. A selected `SListView` row updates the private
  paint index; the index is not exposed in Details.
- Selected Layer properties use an `IStructureDetailsView` over a non-owning
  `FStructOnScope`. Detach that scope before any Layer-array add/delete/reorder
  that could relocate elements, then bind it to the newly selected element.
- Save copies source into the hidden Level actor.
- Bake drops orphaned Layer GUID triangles and resolves remaining GUIDs to
  compact indices.
- Brush-ring traces accept compatible floor hits across collision-component
  boundaries. Component identity is not surface identity; floor normal and
  bounded projection remain the geometric filters.
- Authoring and preview visualization rasterize saved triangles into a separate
  anchor-local cell cache. Each same-surface cell emits once. The first enabled
  Layer in top-to-bottom list order resolves the visual winner before
  `FDynamicMeshBuilder` submission, so transparent color never accumulates from
  repeat stamps or lower rows. Query and save structures remain untouched.
- Edit mode invalidates this cache after geometry or Layer changes. Read-only
  Preview builds one cache per loaded storage actor and releases all caches on
  mode exit.
- PIE cannot use `FEdMode::Render`: that callback draws only Level Editor
  viewports and resolves the editor world. The Show command therefore builds
  the same resolved cell meshes into non-zero, per-storage BatchIDs on each PIE
  world's `WorldPersistent` `ULineBatchComponent`. One persistent submission
  per Layer avoids frame-over-frame alpha accumulation; `ClearBatch` removes
  only CCS-owned geometry. A ticker handles already-running PIE, multi-PIE
  worlds, streaming add/remove, and transform changes.
- Never register an ownerless editor-created `UPrimitiveComponent` into a PIE
  world and retain it through a global `TStrongObjectPtr`: `EndPlayMap` can
  release the world's `FScene` before a later ticker/GC pass drops that object.
  Use a world-owned renderer or tear it down on `PrePIEEnded`. Mesh preview
  does both: its batcher belongs to `UWorld`, `PrePIEEnded` clears all BatchIDs,
  and `PostPIEStarted` re-enables routing. Editor-module placement excludes
  Shipping.
- Closing the mode's primary tab routes back to `FEdMode::RequestDeletion`.
  Exit guards against close-callback re-entry and explicitly redraws Level
  viewports. Preview toggle first deactivates edit mode, then activates preview.
- Register custom Level Editor modes with a resolved normal/small icon pair
  from `FComposableCameraEditorStyle`; reuse that pair for related ToolMenus
  entries. Passing default `FSlateIcon()` leaves the active-mode icon slot
  blank even when the mode name renders correctly.
- Read-only preview is a separate legacy editor mode and never exposes storage
  actor details. Its PIE companion is rendering-only and never changes data.
- Geometry optimization must consume authoring data and emit runtime data. It
  must not round-trip simplified geometry back into authoring state.

## 24. Maintenance Rule

Update this document when:

- adding/removing node, transition, patch, modifier, action, or interpolator.
- changing runtime data layout.
- changing graph sync/rebuild mechanics.
- changing hot-path behavior.
- discovering a new recurring gotcha.
- making stale comments or docs materially wrong.
