# ComposableCameraSystem Design

Updated: 2026-10-05

This document describes the current runtime architecture of the UE 5.6
ComposableCameraSystem plugin. It is intentionally compact. Implementation
details live in `TechDoc.md`. Editor details live in `EditorDesignDoc.md`.
Shot authoring details live in `ShotBasedKeyframing.md`.

## 1. Design Goals

- Compose camera behavior from small nodes, not subclasses.
- Keep mode switching separate from per-camera blending.
- Keep Evaluation Tree transitions pose-only. A camera transition blends two
  poses. Modifier value transitions are a separate scalar-timing system owned
  by one camera instance; they never enter the Evaluation Tree.
- Keep camera type data in assets. Graph assets compile into runtime data
  layouts and execution chains.
- Keep gameplay and Sequencer evaluation on compatible camera code paths.
- Keep hot paths allocation-free where practical.

## 2. Runtime Shape

```text
AComposableCameraPlayerCameraManager
  - UComposableCameraContextStack
      - UComposableCameraDirector per context
          - UComposableCameraEvaluationTree
          - UComposableCameraPatchManager
  - UComposableCameraModifierManager
  - camera actions
  - current / previous pose history
```

Three runtime modules exist:

- `ComposableCameraSystem`: runtime.
- `ComposableCameraSystemEditor`: editor and authoring tools.
- `ComposableCameraSystemUncookedOnly`: K2 nodes and editor-only Blueprint helpers
  that must exist in PIE but not shipping builds.

## 3. Per-Frame Runtime Flow

```text
PCM::UpdateCamera
  -> ContextStack::Evaluate
       -> active Director::Evaluate
            -> EvaluationTree::Evaluate
                 -> CameraBase::TickCamera on leaves
                     -> optional camera-owned Modifier value overlays
                 -> transition nodes blend source and target poses
                 -> finished transitions collapse
            -> PatchManager::Apply on tree output
       -> auto-pop active transient context if its camera finished
  -> PCM modifiers
  -> FMinimalViewInfo projection
```

Only the top context is evaluated by the stack. Lower contexts can still tick
when the active tree contains a captured reference leaf that points at their
tree snapshot.

## 4. Context Stack

`UComposableCameraContextStack` is Tier 1: macro mode switching.

- Normal gameplay context names come from
  `UComposableCameraProjectSettings::ContextNames`.
- `EnsureContext` creates the context if missing.
- If the context already exists below the top, `EnsureContext` moves it to the
  top. Position matters.
- The top entry is active. Base context cannot be popped.
- Popping an inactive context destroys it immediately.
- Popping the active context resumes the previous context in place.
- Transient contexts auto-pop when their running camera finishes.
- Popped contexts that still feed a transition move to `PendingDestroyEntries`
  until the transition finishes.
- Non-top transient contexts can be implicitly demoted to pending destruction
  when an inter-context activation replaces them.
- `PushTemporaryContext` is the internal exception to configured names. It
  creates a collision-free caller-owned Context only above an existing base
  Context. Scoped systems must retain its returned name and pop it; they must
  never claim or pop a gameplay-owned Context with the same logical label.

Pop does not respawn the resumed camera. Its existing tree stays alive, so node
state such as damping, interpolation, and spline progress continues.

## 5. Director

`UComposableCameraDirector` owns one context.

It owns:

- `UComposableCameraEvaluationTree`.
- `UComposableCameraPatchManager`.
- `RunningCamera`.
- `LastEvaluatedPose`.
- `PreviousEvaluatedPose`.

Director evaluation order:

1. Save previous pose.
2. Evaluate the tree.
3. Apply camera patches on top of the tree pose.
4. Sync `RunningCamera` from the tree.

Patch overlays are therefore context-local and happen after all camera
transition blending inside that context.

## 6. Evaluation Tree

`UComposableCameraEvaluationTree` is Tier 2: per-context camera blending.

Tree node variants:

- Leaf: wraps one `AComposableCameraCameraBase`.
- Reference leaf: wraps a captured `TSharedPtr` snapshot of another tree root.
- Inner transition: owns a transition plus left source and right target child.

Important rules:

- Right child is the target / dominant camera.
- Left child is source while a transition runs.
- Finished transitions collapse by destroying the left/source subtree and
  promoting the right/target subtree.
- Reference leaves capture tree topology at creation time. They do not follow
  later root mutations.
- Reference leaves evaluate the captured subtree directly. They do not call back
  into `Director::Evaluate`.
- A captured DAG can reference the same camera UObject through more than one
  path. Leaf and wrapper memoization prevents duplicate per-frame node ticks.

Inter-context blends use reference leaves so the source context can keep
animating through the blend without creating cycles.

## 7. Camera Type Assets

`UComposableCameraTypeAsset` is the source of runtime camera composition data.

Main persisted data:

- `NodeTemplates`.
- `NodePinOverrides`.
- `ComputeNodeTemplates`.
- `ComputeNodePinOverrides`.
- main-chain and compute-chain pin connections.
- main-chain and compute-chain execution order.
- full execution chains with set-variable entries.
- exposed parameters.
- internal variables.
- exposed variables.
- variable nodes.
- camera gameplay-tag container.
- optional enter / exit transitions.
- default preserve-pose flag.
- editor node positions.

Build converts the asset graph into:

- runtime node instances.
- runtime data-block layout.
- pin offset maps.
- input-source maps.
- execution chains.

Editor graph data is transient. Durable state lives on the asset.

## 8. Cameras and Nodes

`AComposableCameraCameraBase` is the runtime evaluator.

It owns:

- camera nodes.
- compute nodes.
- full execution chains.
- runtime data block.
- source type asset.
- source parameter block.
- current and last frame pose.
- optional transient in-place Modifier runtime state.

Runtime node execution:

```text
input pose
  -> pre-node actions
  -> optional cached Modifier property operation
  -> node tick
  -> post-node actions
  -> optional set-variable entries
  -> next node
  -> output pose
```

Compute nodes run in the camera BeginPlay path. They seed data before normal
per-frame camera nodes run.

Built-in compute nodes cover activation-time helpers such as actor distance /
direction, actor-between world positions with height offsets, and initial
camera rotation setup.

Set Rotation nodes can resolve a base rotation from an actor forward vector,
an explicit vector, a literal rotator, or the direction from one resolved actor
to another, then apply a final rotation offset. That offset keeps yaw in world
space around Z, while pitch and roll apply in the resolved camera local space.

Node pin categories currently include:

- Bool, Int32, Float, Double.
- Vector2D, Vector3D, Vector4, Rotator, Transform.
- Actor, Object.
- Struct.
- Name.
- Enum.
- Delegate.

Struct pins use `FInstancedStruct` storage for non-POD structs. Enum pins use
canonical integer storage. Delegate pins live in the parameter block and bind by
reflection during activation.

## 9. Transitions

Transitions derive from `UComposableCameraTransitionBase`.

Transition selection order:

1. Caller override.
2. transition table exact source / target pair.
3. source type asset exit transition.
4. target type asset enter transition.
5. hard cut.

A transition receives source pose, target pose, and delta time. It emits one
pose. It must not own camera lifecycle. On activation the base transition caches
its typed outer `AComposableCameraPlayerCameraManager` only as owner context for
actor-input resolution; camera ownership and lifetime still stay outside the
transition.

Built-in transition families include linear, smooth, ease, cubic, inertialized,
cylindrical, spline, path-guided, dynamic deocclusion, composition-preserving,
and view-target.

`UComposableCameraModifierTransitionBase` is intentionally outside this
hierarchy. It produces only a normalized scalar weight for property
interpolation inside one camera. It does not receive poses, own a camera, or
create an Evaluation Tree node. Linear, smooth-step, smoother-step, ease, and
curve timing are available without forwarding pose-dependent transition
algorithms into the property domain.

Composition-preserving transitions preserve subject composition in the driving
rotation space. At transition start they capture the subject's source-camera
local offset. Each tick a nested driving transition computes rotation `R'` and
blend weight alpha, while the target-camera local offset is recomputed from the
live target pose and live subject. The output blends normal non-transform pose
fields from current source to current target, then overrides rotation with `R'`
and location with
`SubjectNow - R'.RotateVector(Lerp(CapturedSourceOffset, LiveTargetOffset, alpha))`.
The target pose is not modified, and alpha = 1 converges to the live target
pose to avoid a collapse-frame snap.

## 10. Modifiers and Actions

Modifier selection lives at the PCM level. Legacy reactivation is initiated
there; opt-in in-place values are consumed inside the selected camera's node
evaluation.

Blueprint authors add Modifier assets through `Add Camera Modifier`, a custom
K2 node forwarding the existing PCM and asset inputs to `AddModifier`.
The raw library function is hidden from new-node menus but retained for saved
Blueprint calls and C++; Modifier registration and application semantics stay
unchanged.

`UComposableCameraNodeModifierDataAsset` stores fixed base-wrapper entries.
Each wrapper's first choice selects one of two mutually exclusive branches:
Node Type mode owns a concrete built-in or Blueprint node template plus a sparse
set of explicitly enabled property names; Custom Modifier Class mode owns an
instanced user-authored `UComposableCameraModifierBase` subclass.
Each camera and camera type asset owns an `FGameplayTagContainer`. The modifier
asset's `FGameplayTagQuery` scopes the whole asset: an empty query means every
camera; a non-empty query supports nested ALL / ANY / NONE expressions against
the camera's complete tag container. Registered candidates remain bucketed by
exact target node class, but Node Type candidates compete independently for
each checked property. Priority resolves overlapping ownership; equal priority
preserves the existing later-registration-wins rule. Therefore two assets may
simultaneously modify disjoint properties on the same node class. A candidate
may be effective for all, some, or none of its checked properties.

Custom Modifier callbacks keep a whole-node lane: when the same candidate that
would have won the legacy node-class election is Custom, it remains the single
winner for that node-class bucket and is not composed with Node Type entries.
The callback runs once after the activation parameters have reached the node.
Non-wired pin-backed properties it actually changes become owned by that
Modifier for this camera instance; untouched and wired pins continue following
their normal sources. Wired outputs may first be produced during BeginPlay,
after the Custom callback, so their pre-BeginPlay values are not frozen.
This gives a one-shot Custom addition the same effective priority whether its
base value came from the type asset or from a K2 activation parameter. Later
changes to that owned pin do not recompute the addition.
At camera construction, the manager matches exact node class and copies each
effective property from its winning template onto every matching runtime node.
Node-class metadata and transient fields are never modifier inputs. Unchecked
node values continue to come from the camera type asset.

Checked properties are a higher-priority authored layer than the target node's
graph wires and exposed parameters. The node's pin resolver skips those exact
fields for this camera instance, so an override stays effective after the first
tick. Type-asset construction applies data-driven modifiers before node
initialization, so interpolator and solver caches are built from override values.

Custom wrappers read the nested modifier's `NodeClass`, always retain the legacy
post-initialize phase, and invoke its `ApplyModifier` event. The runtime compares
non-wired pin-backed fields before and after that event, then protects changed fields
from later pin resolution. Existing assets that stored
Blueprint modifier subclasses directly in the array migrate those objects into
Custom Modifier Class wrappers during load, preserving `NodeClass`, custom
fields, and their `ApplyModifier` event path.
Legacy camera `CameraTag` fields migrate into the new container. Legacy modifier
`CameraTags` lists migrate into an ANY query, preserving their OR intent.

Each Modifier asset selects one runtime application mode:

- `ReactivateCamera` is value zero and preserves the existing behavior for all
  previously saved assets. A change reconstructs the current camera and uses
  the existing pose transition path.
- `ModifyExistingInstance` is explicit opt-in. The manager changes only the
  running camera's transient Modifier state; no camera is spawned, destroyed,
  or inserted into the Evaluation Tree.

In-place Node Type entries build property bindings only when effective Modifier
selection changes. Every matching node instance receives an independent
binding, identified by runtime node plus property/pin rather than parameter
name alone. Pin-backed properties form a logical overlay above wire, exposed
parameter, per-instance default, and class default values. The lower
RuntimeDataBlock layer remains untouched and live, so removing the Modifier
reveals the current K2/wire value rather than a type-asset template snapshot.
Explicit `GetInputPinValue` readers consult the same node-local ownership cache
as automatic pin resolution.

Each effective `(exact node class, property)` edge is diffed independently
rather than assigned one update-wide transition. A newly owned property uses
its winning asset's Enter Value Transition; a property whose winner changed
uses the new winner's Replace Value Transition; a removed winner uses the old
winner's Exit Value Transition. Null Replace preserves legacy priority
selection: desired Enter when desired priority is at least previous priority,
otherwise previous Exit. A zero-duration Replace is explicitly immediate. A
property with no current winner keeps any exit already in progress, so an
unrelated property election cannot restart its clock.

Continuous built-in value types interpolate per node immediately before that
node resolves pins and ticks. Discrete values switch once at the transition's
configured weight. Non-pin properties are rejected unless the node explicitly
opts them into runtime mutation; this prevents changing initialization-only
configuration without rebuilding its cache. Nodes that opt in a cached
property can rebuild derived state through `OnModifierPropertyChanged`.

Camera construction still applies effective values before node initialization.
The in-place transition exists only for changes to an already-running camera.
Custom Blueprint Modifier callbacks remain `ReactivateCamera`-only because
arbitrary side effects cannot be automatically interpolated or reverted.
Mixed effective changes reactivate when any changed entry uses the legacy mode.

Actions are runtime objects registered on the PCM. They can target:

- whole camera tick.
- pre-node tick.
- post-node tick.
- action-specific expiration.

Built-in actions include move-to, reset-pitch, and rotate-to.
`UComposableCameraActionTypeAsset` owns an instanced Action template. One
Blueprint Action class can therefore supply logic to many assets with distinct
defaults. `AddActionFromAsset` duplicates the template for every activation,
applies caller parameter values to compatible editable Blueprint-visible subclass fields,
then registers the new Action on the PCM. A connected K2 input can carry a live
Actor or other context-dependent value. Unconnected inputs leave the asset
default intact. Existing class-based `AddAction` and `CanExecute` / `OnExecute`
logic remain available. The returned Action instance is a handle; Blueprint
`RemoveActionInstance` removes that exact instance when several share one class.
Instant and Duration expiration count PCM update frames, even when no camera or
matching node executes the action. Actions with Duration enabled and a
non-positive Duration are rejected when added; a later invalid duration also
prevents execution.
Manual expiration is also checked by the PCM before evaluation. Pose-dependent
Condition is checked at the Action's actual camera or node hook against that
camera's local pose at that stage, not the previous PCM blended output. A
global Action may run on multiple cameras during a blend; the running camera's
first matching hook determines its per-update Condition and global expiration.
Source-camera hooks do not expire a global Action for reaching their own target.
An Action bound only to its original camera uses that camera's hook, even if it
becomes a transition source after the running camera changes.

## 11. Camera Patches

Camera patches are real runtime overlays, not stubs.

`UComposableCameraPatchTypeAsset` subclasses `UComposableCameraTypeAsset`.
Patch activation spawns a transient evaluator camera and builds it from the
patch asset.

Patch behavior:

- Owned by a director through `UComposableCameraPatchManager`.
- Added through Blueprint library / K2 node or Sequencer patch track.
- Evaluated after the director tree.
- Sorted by layer index, then push sequence.
- Each patch sees the pose produced by the tree plus lower-layer patches.
- Each patch evaluates with `TickWithInputPose`.
- Output blends into the running pose by envelope alpha.

Patch lifetime:

- Entering.
- Active.
- Exiting.
- Expired.

Expiration channels:

- Duration.
- Manual.
- Condition through `CanRemain`.
- optional expire-on-camera-change flag.

Sequencer patch overlays use a component-local path with stateless section
envelopes. They still tick patch evaluators and blend in layer order.

## 12. Level Sequence Path

Sequencer evaluation does not require a PCM.

`UComposableCameraLevelSequenceComponent` lives on the Level Sequence camera
actor path. It:

- owns an internal transient `AComposableCameraCameraBase`.
- builds it from a type asset reference.
- rebuilds parameter / variable bags from the type asset.
- reapplies bags to the runtime data block each tick.
- applies active shot overrides before camera tick.
- applies Sequencer patch overlays after camera tick.
- projects the final pose to a `UCineCameraComponent`.

This path shares type assets, nodes, runtime data blocks, parameter blocks,
shots, and patches with gameplay. It skips PCM-specific behavior such as
actions that require an owning player camera manager.

## 13. Shot-Based Composition

Shot data lives in:

- `FComposableCameraTargetInfo`.
- `FComposableCameraShotTarget`.
- `FComposableCameraShot`.
- optional `UComposableCameraShotAsset`.
- `UComposableCameraCompositionFramingNode`.

The solver writes a camera pose from shot intent:

```text
targets -> anchor -> placement -> aim -> lens -> focus -> roll
```

Shot sections can store inline data or reference a shot asset with a
section-local override copy. Target actor overrides bind shot target indices to
Sequencer bindings. Overlapping shot sections blend by the incoming section's
enter transition and the overlap duration.

## 14. Debugging

Runtime debug data is snapshot-based.

Available debug surfaces include:

- runtime context/tree/patch debug panel.
- pose history panel.
- console dumps under `CCS.Dump.*`.
- editor dumps under `CCS.Editor.Dump.*`.
- viewport node / transition / shot gizmos.
- editor graph debug overlay for selected camera instances.

Snapshots resolve pointers to display data early so UI consumers do not deref
runtime-owned objects later.

The Modifier panel also reads a display-only snapshot from each active
in-place property binding: current node value, target template (or live lower
layer on exit), and transition phase/progress. The PCM retains the last
modifier-selection decision only for the camera it affected, including the
first legacy enter/exit edge that required reactivation. Neither diagnostic
path changes modifier ownership or camera evaluation.

Per-node editor snapshots include the pose after evaluation, output-pin values,
and owned strings for every current node parameter. Declared inputs are emitted
before remaining editable properties, so both pin-backed values and
Details-only node settings are inspectable. The editor uses this data for a
runtime-parameter hover section and an active-node Runtime Debug panel without
retaining or dereferencing live runtime node pointers from Slate.

PIE live tuning is an editor-owned session for the connected Start chain.
FullExecChain camera entries (or legacy ExecutionOrder) determine scope and
order. BeginPlay compute nodes, disconnected nodes, and SetVariable data
dependencies are not editing targets. All editable authoring properties are
included automatically; no per-field metadata whitelist exists.

A transient per-node trial layer supersedes graph wires, activation parameters
and Modifiers while that node executes. Normal pin / Modifier resolution still
updates the underlying layer. Reflected value storage is exchanged before
FirstTick / Tick and restored afterward, so Reset removes the override and
resumes the current driver rather than restoring a stale baseline. Typed pin
getters consult the trial first, including compound subobject pins. The Editor
path exchanges preallocated storage without per-frame allocation. Shipping
builds contain no trial state or evaluation branch.

Editing refreshes only the affected node's derived configuration at event time.
Its Initialize / FirstTick state may restart; the camera, context, transition and
DAG memoization remain intact. Resource-owning nodes specialize refresh:
MixingCamera replaces child cameras only when its Cameras configuration changes;
ImpulseResolution retains its collision component and rebuilds its interpolator.

Apply to Asset preflights all edits on isolated authoring candidates, copies authoring properties and owned
subobjects into the original templates using fresh destination-owned duplicates
without inheriting proxy transient flags, updates direct/compound pin defaults,
and SyncToTypeAsset commits one Undo transaction. Wires, caller overrides and
Modifiers remain unchanged. A configuration change that removes or retypes an
existing wired/exposed pin rejects the entire Apply before authoring mutation.
Only changed fields undergo reference remapping; unrelated caller-populated
runtime references cannot block a scalar save. Undo/Redo restores asset and graph
together without graph callbacks synchronizing intermediate reconstruction.
Trial and pending-save state are independent:
Apply keeps the live trial; Reset remains available afterward. Editor/PIE actor
counterparts are remapped; a runtime-only actor reference cannot become a saved
default. A complete Reset rebases conflict snapshots against current source data,
including source values restored by Undo. Proxies release unmappable PIE
references at PrePIEEnded.

Viewport gizmo colors are centralized in the runtime debug palette. The bottom
Legend panel reads the same metadata as the 3D draw sites, so swatches match the
spheres and transition markers. Legend rows are also filtered against current
draw state: node rows appear only for gizmo types present on the current
running camera, and transition rows appear only for active transition nodes in
the active context tree. Source / target pose swatches appear only when a
relevant transition row can draw. Sink-routed sphere gizmos carry optional short
frame-local labels so live viewport drawing and rewind trace playback can show
the same marker names. Live labels use HUD debug text with a frame-local
lifetime; Rewind playback projects those labels directly onto the debug Canvas
because the visualized playback world is not guaranteed to own a usable HUD.

Debug primitive emission goes through a draw sink abstraction when it needs to
target either live viewport drawing or rewind trace capture. The live sink
adapts to `DrawDebug*` / `FComposableCameraViewportDebug`; the capture sink
appends immutable `FComposableCameraDebugPrimitive` snapshots for trace writers.
The capture sink explicitly forces all 3D node / transition gizmo gates open,
so `CCS.Debug.Trace 1` records Rewind primitives without depending on live
viewport CVars or cached `Nodes.All` / `Transitions.All` state. Live viewport
draws still obey the per-gizmo and All CVars.
The primitive stream supports line, point, sphere / solid-sphere, box, plane,
and camera-frustum records. Sphere records preserve optional segment count in
`Size`, line thickness in `Thickness`, and an optional `Label`; box records
preserve line thickness in `Thickness`. For `CameraFrustum` records only,
`Radius` stores FOV, `Size` stores ortho width, and `Thickness` stores debug
frustum draw scale. Raw default constructed primitives are not valid frustums;
the `MakeCameraFrustum` factory defaults scale to 1.0.

When CCS trace is enabled in an editor target, the gameplay PCM emits paired
rewind trace frames: one evaluation frame for the CCS pose and captured gizmos,
and one active-camera frame for the final `FMinimalViewInfo` just written to the
PCM cache. Both records share the same frame cycle so tooling can compare
evaluated CCS output with the rendered camera view. Non-editor and packaged
targets compile the Rewind trace channel / writer path out completely. The
Level Sequence component emits an evaluation frame with
`SourceKind = CCS_LevelSequence`, its projection status, object ids for the
world / component / owning actor, the type asset name, the evaluated CCS pose,
and sink-captured camera gizmos from its internal camera. The LS path
does not emit transition primitives because it has no context stack / director
transition tree.

## 15. Mesh Camera Layers

Mesh camera behavior uses a Level-local painted surface document. It does not
use collision volumes, floor-actor identity, or a persistent query StaticMesh.

```text
tool-authored local triangles
  -> hidden AComposableCameraMeshSurfaceStorageActor in current Level
  -> disposable runtime triangle data
  -> UComposableCameraMeshWorldSubsystem downward query
  -> every enabled layer on the nearest surface
  -> one active scope per Layer
  -> UComposableCameraMeshProfile per scope
       -> selected Type: CameraType / Modifier / Action / Patch
       -> exactly one effect family per Profile
```

The storage actor is `NotPlaceable`, excluded from Scene Outliner, and created
only by the tool. Its actor transform is the document anchor. A document saved
inside a streamed Level or Level Instance therefore follows that source instead
of baking world coordinates.

Profiles select one `EComposableCameraMeshProfileType`. Details displays only
that family's asset and settings; other configurations stay serialized for
later reuse but never execute. Camera embeds `FComposableCameraParameterTableRow`
and Patch uses the same exposed parameter/variable schema and typed parser.
Action exposes precisely the subclass properties accepted by its K2 node's
`IsExposableProperty` rule, keeping unoverridden template defaults. Modifier
uses its existing asset-template array without a second parameter schema.

Action Actor and Delegate inputs resolve per local player at Layer entry from
Pawn, PlayerController, CameraManager, StorageActor, RunningCamera, or None.
Delegate bindings additionally name a signature-compatible function on that
source. These are typed bindings, not persistent references to world actors.

Layer changes are edge-triggered. Spatial membership is a set: entering an
overlapping Layer does not exit Layers that still cover the player. Every
active Modifier Layer contributes duplicated Modifier candidates. Removing one Layer
removes only its candidates; Modifier asset priority continues to resolve
same-property conflicts inside the modifier manager; disjoint properties on
the same node class compose.

Every Camera-bearing Layer pushes its own collision-free temporary Context.
Its readable hint contains `Mesh`, Layer name, and Layer GUID. Entering a
nested Camera Layer therefore suspends, rather than replaces, the outer
Layer's Director and camera instance. Exiting the nested Layer pops only its
Context and resumes the exact outer camera. Modifier, Action, and Patch Profiles
push no Context. Exiting the final Camera-bearing Layer restores the gameplay Context.
After an active pop, ModifierManager selection is recomputed without rebuilding
the resumed camera, releasing removed candidates while preserving node state.
The embedded row's authored `ContextName` is ignored and hidden for Mesh
Profiles because ownership comes from Layer identity. Normal traversal follows
Layer enter order. If several Layers first appear on one tick, the subsystem
enters bottom list rows first so the top row becomes the top Context.

Each Camera Layer entry is transactional: the PCM captures the current source
Director before its temporary push and activates through the inter-context
reference-source path. The configured transition therefore blends from the
camera currently visible at that nesting depth.

Mesh Layer presence owns camera lifetime. The subsystem therefore forces its
scoped Camera activation to non-transient with unlimited lifetime regardless
of the embedded row's fields; those fields are hidden in Profile Details.
Failed construction immediately pops the empty temporary Context; it cannot
strand an empty stack entry or overwrite the gameplay camera.

Action entry registers one Action through the PCM and records the exact
instance. Exit removes only that instance, preserving external same-class
Actions. Patch entry records the active Director's PatchManager and a strongly
GC-tracked handle. Exit expires that exact Patch on its original manager,
even after the active Context changes, respecting the Patch exit envelope.
Asset-driven natural expiration remains valid; neither effect restarts while
the player stays inside the Layer.

Profile schema uses a custom version. Legacy Camera-only and Modifier-only
assets migrate automatically. A legacy asset containing both retains all
settings and requires an explicit Type selection/confirmation before runtime
entry. New saves preserve the selection and pending migration flag.

The editor authors regions with Brush, dragged Rectangle/Circle, or clicked
simple Polygon outlines. Shape points use the Level document's XY coordinates;
optional grid snapping supports precise boundaries. On confirmation the editor
shows a provisional captured-plane fill immediately. Compatible-floor projection
continues in bounded batches on the editor thread; a worker resolves coverage on
plain snapshots. New source and ready coverage install together in one creation
transaction after revision validation. Existing edits cannot be overwritten by
stale results. Pending fills are disposable, excluded from saved source, and
Save waits for them to complete. Existing control/Details edits retain their
synchronous projection transaction. Sampling and final runtime coverage are unchanged.
Editor-only source retains Shape GUID, Layer GUID, controls, projection plane and
sampling settings, plus per-triangle Shape ownership. Select moves regions or
their controls; numeric Details edits replace only that Shape's projected mesh.
Saved documents retain these controls for later editing. Legacy triangle-only
documents remain valid; their old regions have no recoverable Shape controls.

The transient working UObject stores Layer definitions, full authoring source and
a revision GUID in Unreal editor transactions. One paint/erase stroke or confirmed
Shape edit is one Undo step. Undo/Redo restores source before refreshing Details
and visualization; dirty state compares against the last successful Save revision.
Numeric property widgets remain alive throughout interactive dragging. Details
rebuilding waits until the editor transaction finishes, preserving the release
callback that closes it and keeping source Undo/Redo available.
Save copies the document into the hidden actor and rebuilds cooked query data.
The editor retains an independent, GC-tracked checkpoint of the normalized opening
document and replaces it only after successful package Save. Discard restores that
checkpoint in one Undo step, including Layers, geometry, retained Shape controls
and erasures. It cancels unfinished strokes/drafts and queued creation work first;
tool preferences and unrelated editor history remain intact. Failed package Save
does not advance the checkpoint. If it already applied data to a storage actor,
Discard restores that actor as well, or removes the actor created by the failed
attempt. Discard itself performs no package Save.

Brush/Erase update only affected cells in the editor's resolved visualization
cache during a stroke. Release retains that current cache and the unchanged
controls, closes the transaction and redraws other viewports. Grid resolution
can coarsen on substantial growth but is not recomputed merely for release.
No-op stamps obey brush spacing. Erase dirties only removed coverage, preserving
the cached union elsewhere despite changed source tessellation.
This scheduling changes only disposable editor preview work, not authored
coverage, transaction grouping or runtime queries.

The preview grid indexes affected regions; it does not define their outline.
Boundary cells retain clipped source polygons, with repeat coverage merged and
Layer priority resolved per covered point. Fully covered coplanar cells use a
quad fast path. Editor and PIE meshes share these disposable patches, preserving
the silhouette even when the spatial index coarsens. Serialized data is unchanged.

Erase subtracts a bounded circular prism from the active Layer's triangles,
interpolating fragment heights and preserving Shape ownership. Only cut triangles
are replaced; shared/untouched vertices and Shape controls stay in place. Save
compacts unreferenced vertices left by interactive cuts. A Shape retains
affected erase stamps in document-local coordinates; rebuilding its controls
reapplies these cuts at their authored locations. Delete removes the whole selected
Shape. Neither operation affects triangles owned by other Layers. Shape metadata
is editor-only; runtime still queries the baked triangles with Layer indices.

Authoring triangles and runtime triangles are separate serialized fields.
Runtime data is always rebuilt from authoring data. The MVP performs a linear
triangle-ray query; spatial acceleration and constrained simplification are
replaceable bake optimizations.

## 16. Hard Invariants

- Context stack position matters. `EnsureContext` may reorder entries.
- Base context is never popped.
- Inter-context blends use captured tree snapshots, not live director recursion.
- Evaluation tree transition nodes are pose-only.
- Modifier value transitions never create Evaluation Tree nodes or own camera
  lifecycle.
- In-place Modifier binding state cannot be discarded until exit restores the
  current lower layer and unregisters node ownership. Null and zero-duration
  exits still release during the next normal node evaluation.
- In-place Modifier transition ownership is property-local. Simultaneous
  Enter, Replace, and Exit bindings may use different assets and clocks.
- Node Type Modifier selection is property-local. Disjoint properties on one
  exact node class may have different effective assets; overlapping properties
  still resolve by asset priority and registration order.
- Custom Modifier callbacks remain whole-node winners because arbitrary
  Blueprint side effects cannot be safely composed with other Modifier entries.
  Only their changed non-wired pin-backed fields acquire one-shot pin ownership.
- Existing Modifier assets default to `ReactivateCamera`; in-place mutation is
  explicit opt-in.
- Patch overlays run after tree evaluation.
- Graph assets are durable source. `EditorGraph` is transient.
- Runtime data-block slot shape and byte bounds must both be valid.
- Hot paths must not allocate without a clear reason.
- UObject member fields / UPROPERTY references use `TObjectPtr`.
- New runtime logs use `LogComposableCameraSystem`.
- Mesh authoring data is durable source. Simplified/runtime surface data must
  never become the source for the next edit.
- Mesh surface vertices are storage-actor local. Do not bake world-space
  Level Instance transforms into the document.
- One Mesh Profile dispatches exactly one effect family. Unselected serialized
  configuration cannot contribute effects.
- Mesh Action/Patch cleanup uses exact owned instances, never class or asset
  identity; natural expiry never causes repeated entry.
- Every Mesh Camera Layer owns a separate temporary Context. A Layer exit
  pops only that Context; lower Mesh and external gameplay Contexts remain
  intact.
- Command-line UBT / editor / automation test runs are not part of Codex
  verification for this project. Compile and automation run inside Rider or
  Visual Studio.

## 17. Document Map

- Runtime design: this file.
- Implementation techniques: `TechDoc.md`.
- Editor graph and tools: `EditorDesignDoc.md`.
- Shot authoring and solver: `ShotBasedKeyframing.md`.
- Worked flows: `ExecutionFlowExamples.md`.
- Packaging incident notes: `FabUnityBuildTroubleshooting.md`.
