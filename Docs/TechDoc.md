# ComposableCameraSystem Tech Notes

Updated: 2026-06-03

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

Fallback rule: a transition that cannot compute should return one input pose,
usually target pose for activation-style transitions. Never return a default
constructed pose as an error fallback.

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

Editor debug:

- selected runtime instance picker in type asset editor.
- graph overlay of live node data.
- runtime previewer tab showing visible-subject-local camera relation.
- `CCS.Editor.Dump.*`.

Snapshot rule: resolve runtime pointers to names and value copies early.

Runtime Previewer technique:

- The Camera Type Asset editor registers `RuntimePreviewerTabId`, but does not
  include it in the default layout.
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

## 16. Built-In Camera Nodes

Current node classes:

- `UComposableCameraAutoRotateNode`
- `UComposableCameraBeginPlaySetRotationNode`
- `UComposableCameraBlueprintCameraNode`
- `UComposableCameraCameraOffsetNode`
- `UComposableCameraCollisionPushNode`
- `UComposableCameraCompositionFramingNode`
- `UComposableCameraComputeDistanceToActorNode`
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
- `ComposableCameraShotSolverTests.cpp`
- `ComposableCameraPivotLookAheadNodeTests.cpp`
- `ComposableCameraLockOnAimPointNodeTests.cpp`
- `ComposableCameraNodeGraphSyncTests.cpp`

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
- FOV may be stored as FieldOfView or FocalLength. Use pose helper methods for
  effective FOV.
- Focus distance uses sentinel behavior. Do not blend invalid focus distance as
  a real distance.

## 22. Build and Verification

For this project:

- Do not invoke UBT, Build.bat, RunUBT.bat, dotnet, msbuild, Unreal Editor, or
  automation tests from Codex shell.
- Compile inside Rider or Visual Studio.
- Header/reflection/module changes require full editor restart, not Live Coding.
- Docs-only changes do not need a compile, but a non-trivial code-adjacent doc
  sweep should still be reviewed against source.

## 23. Maintenance Rule

Update this document when:

- adding/removing node, transition, patch, modifier, action, or interpolator.
- changing runtime data layout.
- changing graph sync/rebuild mechanics.
- changing hot-path behavior.
- discovering a new recurring gotcha.
- making stale comments or docs materially wrong.
