# ComposableCameraSystem Design

Updated: 2026-06-17

This document describes the current runtime architecture of the UE 5.6
ComposableCameraSystem plugin. It is intentionally compact. Implementation
details live in `TechDoc.md`. Editor details live in `EditorDesignDoc.md`.
Shot authoring details live in `ShotBasedKeyframing.md`.

## 1. Design Goals

- Compose camera behavior from small nodes, not subclasses.
- Keep mode switching separate from per-camera blending.
- Keep transitions pose-only. A transition blends two poses. It does not own
  cameras, directors, or contexts.
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

- A context name comes from `UComposableCameraProjectSettings::ContextNames`.
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

Runtime node execution:

```text
input pose
  -> pre-node actions
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

Modifiers live at the PCM level and are applied after context evaluation.

Actions are runtime objects registered on the PCM. They can target:

- whole camera tick.
- pre-node tick.
- post-node tick.
- action-specific expiration.

Built-in actions include move-to, reset-pitch, and rotate-to.

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

Viewport gizmo colors are centralized in the runtime debug palette. The bottom
Legend panel reads the same metadata as the 3D draw sites, so swatches match the
spheres and transition markers. Sink-routed sphere gizmos carry optional short
frame-local labels so live viewport drawing and rewind trace playback can show
the same marker names.

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

When CCS trace is enabled, the gameplay PCM emits paired rewind trace frames:
one evaluation frame for the CCS pose and captured gizmos, and one active-camera
frame for the final `FMinimalViewInfo` just written to the PCM cache. Both
records share the same frame cycle so tooling can compare evaluated CCS output
with the rendered camera view. The Level Sequence component emits an evaluation
frame with `SourceKind = CCS_LevelSequence`, its projection status, object ids
for the world / component / owning actor, the type asset name, the evaluated
CCS pose, and sink-captured camera gizmos from its internal camera. The LS path
does not emit transition primitives because it has no context stack / director
transition tree.

## 15. Hard Invariants

- Context stack position matters. `EnsureContext` may reorder entries.
- Base context is never popped.
- Inter-context blends use captured tree snapshots, not live director recursion.
- Evaluation tree transition nodes are pose-only.
- Patch overlays run after tree evaluation.
- Graph assets are durable source. `EditorGraph` is transient.
- Runtime data-block slot shape and byte bounds must both be valid.
- Hot paths must not allocate without a clear reason.
- UObject member fields / UPROPERTY references use `TObjectPtr`.
- New runtime logs use `LogComposableCameraSystem`.
- Command-line UBT / editor / automation test runs are not part of Codex
  verification for this project. Compile and automation run inside Rider or
  Visual Studio.

## 16. Document Map

- Runtime design: this file.
- Implementation techniques: `TechDoc.md`.
- Editor graph and tools: `EditorDesignDoc.md`.
- Shot authoring and solver: `ShotBasedKeyframing.md`.
- Worked flows: `ExecutionFlowExamples.md`.
- Packaging incident notes: `FabUnityBuildTroubleshooting.md`.
