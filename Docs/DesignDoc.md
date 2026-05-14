# ComposableCameraSystem �?Design & Technical Reference

*Internal reference for the ComposableCameraSystem UE5.6 plugin.*
*In-design feature spec: [ShotBasedKeyframing.md](ShotBasedKeyframing.md) �?Shot-based composition authoring (Composition Solver, Shot Editor, LS Section integration). Phase A (`FComposableCameraTargetInfo` extraction) is the next implementation step; see that doc's §6.*
*Last updated: 2026-05-13. Detailed change history (review passes, prior architectural revisions) lives in `git log Docs/DesignDoc.md`; recent runtime-side addenda are inlined below until they fold into the relevant sections.*

*2026-05-12 addendum: the CCS Level Sequence CameraCut ECS gate has been removed. Spawn Tracks now own LS camera lifecycle; spawned LS components evaluate while their actor exists, then `OnUnregister` / `EndPlay` tear down the transient evaluator.*
*2026-05-12 addendum: LS-owned evaluators now use Sequencer-aware DeltaTime. Runtime spawned actors resolve the owning `UMovieSceneSequencePlayer` via spawnable annotation / spawn register and scale by `GetPlayRate()`; pure editor preview resolves the active `ISequencer` through `FGetEditorSequencerPlaybackDeltaTime` and scales by `GetPlaybackSpeed()`. Paused preview contributes zero DeltaTime.*
*2026-05-12 addendum: Phase F Shot overlap exits preserve prior-pose continuity. The LS component tracks both primary and secondary Sections; when the previous secondary becomes the new primary (A+B -> B), `CompositionFramingNode` promotes the secondary prior cache into primary instead of hard-seeding. True hard cuts still clear primary prior state; secondary swaps (A+B -> A+C) clear secondary prior state.*
*2026-05-13 addendum: Lens/Exposure split. `LensNode` still owns focal length, aperture, blade count, and `FocusDistance`, and `PhysicalCameraBlendWeight` now gates only DoF physical-camera fields. `ExposureNode` owns `ISO`, `ShutterSpeed`, and `ExposureBlendWeight`. `ApplyPhysicalCameraSettings` applies DoF and exposure separately, so adding Lens for DoF no longer changes scene brightness unless an Exposure node is present or `ExposureBlendWeight` is driven.*
*2026-05-13 addendum: `LookAtNode` now treats a zero-length camera-to-target vector as a no-op. This matters for the supported `SpiralNode -> LookAtNode` composition: Spiral can place the camera exactly on its pivot when the radius curve is null, zero, or crosses zero; a downstream LookAt aimed at that same pivot has no meaningful forward direction to solve. Preserving the incoming rotation avoids zero-rotation snaps / jitter at the singularity.*
*2026-05-13 addendum: `SpiralNode` gained `PivotActorInitialForward` as a Reference Direction. It captures the resolved pivot actor's forward vector on the first tick where the actor is valid, then reuses that fixed vector for the node lifetime. This covers character/pawn pivots whose location is stable but whose actor rotation can be driven by camera-to-ControlRotation sync; `PivotActorForward` remains live-every-frame for authors who intentionally want the spiral basis to track actor yaw.*

*2026-05-11 addendum: `DirectionalMoveNode` now has a `Duration` input / UPROPERTY. Negative duration preserves the original infinite move; non-negative duration clamps travel time so the camera holds its final directional offset after `Duration` seconds.*

---

## 1. Vision and Design Philosophy

ComposableCameraSystem is a modular, composable camera framework for Unreal Engine 5.6. It replaces the monolithic "one camera manager does everything" pattern with a layered architecture where cameras are assembled from reusable nodes, blended through a tree-based evaluation system, and orchestrated across independent contexts.

### Core Design Principles

**Composability over inheritance.** Cameras are not defined by subclassing a monolithic camera class. Instead, a camera is a lightweight container that holds an ordered list of nodes. Each node is a single-responsibility operator (e.g., "look at target", "apply collision push", "control rotation from input"). New camera behaviors are created by composing different node combinations, not by writing new camera subclasses.

**Separation of concerns across two tiers.** The system separates macro-level mode switching (gameplay vs. cinematic vs. UI) from micro-level camera blending (transitioning between two cameras within the same mode). Tier 1 is the Context Stack; Tier 2 is the Evaluation Tree. This separation means that pushing a cinematic context doesn't destroy or interfere with the gameplay camera underneath �?it simply suspends it.

**Pose-only transitions.** Transitions never reference cameras or Directors directly. They receive two poses (source and target) each frame and output a blended pose. This makes transitions fully reusable across any camera pair and any context boundary.

**Data-driven configuration.** Camera behavior is defined entirely through `UComposableCameraTypeAsset` data assets created in the visual editor. The camera base class is `NotBlueprintable` �?Blueprint subclassing is forbidden. Designers create camera types by composing nodes, wiring pins, and exposing parameters in the type asset editor, then activate cameras from Blueprint using the custom K2 activation node.

**Reference-based inter-context blending.** When switching between contexts, the system doesn't freeze the old context's output. Instead, it creates a reference leaf node that captures a `TSharedPtr` snapshot of the old context's tree root and walks that captured subtree each frame (the cameras inside still tick live, but the topology is fixed at capture time). This produces seamless blending even when the source context's camera is still animating, and �?because the snapshot doesn't follow later mutations to the source director �?keeps the evaluation reachable graph a DAG with no cycles, even during pop-while-push-still-active.

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────�?
�?                 AComposableCameraPlayerCameraManager           �?
�?                 (replaces APlayerCameraManager)                �?
�?                                                                �?
�? ┌──────────────────────�? ┌──────────────────────────────�?    �?
�? �? ModifierManager     �? �? CameraActions (TSet)        �?    �?
�? └──────────────────────�? └──────────────────────────────�?    �?
�?                                                                �?
�? ┌──────────────────────────────────────────────────────────�?  �?
�? �?             UComposableCameraContextStack                �?  �?
�? �?             (Tier 1: macro mode switching)               �?  �?
�? �?                                                          �?  �?
�? �?   ┌─────────�? ┌─────────�? ┌─────────────────�?        �?  �?
�? �?   �?Base    �? �?Cutscene�? �?Active (top)    �?�?eval  �?  �?
�? �?   �?Context �? �?Context �? �?Context         �?  here   �?  �?
�? �?   �?        �? �?        �? �?                �?        �?  �?
�? �?   │Director �? │Director �? │Director ────────┼───�?    �?  �?
�? �?   └─────────�? └─────────�? └─────────────────�?  �?    �?  �?
�? �?                                                    �?    �?  �?
�? �?   PendingDestroyEntries: [...popped contexts...]   �?    �?  �?
�? └─────────────────────────────────────────────────────┼─────�?  �?
�?                                                       �?        �?
�? ┌─────────────────────────────────────────────────────▼─────�?  �?
�? �?             UComposableCameraDirector                     �?  �?
�? �?             (one per context)                             �?  �?
�? �?                                                          �?  �?
�? �? ┌────────────────────────────────────────────────────�?  �?  �?
�? �? �?        UComposableCameraEvaluationTree            �?  �?  �?
�? �? �?        (Tier 2: camera transition blending)       �?  �?  �?
�? �? �?                                                   �?  �?  �?
�? �? �?             [Inner: Transition]                    �?  �?  �?
�? �? �?              /              \                     �?  �?  �?
�? �? �?       [Leaf: CamA]    [Leaf: CamB] �?target      �?  �?  �?
�? �? �?       (source)        (running)                   �?  �?  �?
�? �? └────────────────────────────────────────────────────�?  �?  �?
�? �?                                                          �?  �?
�? �? RunningCamera, LastEvaluatedPose, PreviousEvaluatedPose  �?  �?
�? └───────────────────────────────────────────────────────────�?  �?
└─────────────────────────────────────────────────────────────────�?
```

### Component Summary

| Component | Role |
|---|---|
| **PlayerCameraManager (PCM)** | Top-level integration point. Replaces UE's APlayerCameraManager. Owns the ContextStack, ModifierManager, and CameraActions. Drives the per-frame DoUpdateCamera loop. |
| **ContextStack** | LIFO stack of named contexts. Each context owns a Director. Only the top context is actively evaluated (unless referenced by a reference leaf). Handles push, pop, auto-pop of transient cameras, and pending-destroy lifecycle for popped contexts in transition. |
| **Director** | Per-context camera manager. Owns the EvaluationTree and tracks RunningCamera, LastEvaluatedPose, and PreviousEvaluatedPose. Handles camera creation, activation, reactivation, and resume. |
| **EvaluationTree** | Binary tree of nodes (with snapshot RefLeaves the reachable graph is a DAG, not strictly a tree). Leaf nodes wrap cameras, reference leaf nodes hold a `TSharedPtr` snapshot of another director's tree root, and inner nodes wrap transitions that blend their left (source) and right (target) children. Collapse logic promotes the target subtree when a transition finishes. |
| **CameraBase** | A camera actor holding an ordered array of CameraNodes. Executes nodes sequentially each tick to produce a pose. Supports transient lifetime. |
| **CameraNode** | Single-responsibility operator. Reads the current pose and pin values, applies its logic, and writes the modified pose. Nodes communicate through the RuntimeDataBlock pin system. |
| **Transition** | Pose-only blender. Receives source and target poses, maintains internal blend state, outputs blended pose. Has lifecycle hooks: OnBeginPlay (first frame setup), OnEvaluate (per-frame blend), OnFinished (cleanup). |
| **ModifierManager** | Manages modifier data assets. When modifiers change, computes effective per-node-class modifiers (highest priority wins) and may trigger camera reactivation. |

---

## 3. Per-Frame Evaluation Pipeline

Each frame, `AComposableCameraPlayerCameraManager::DoUpdateCamera(DeltaTime)` drives the full pipeline:

```
DoUpdateCamera(DeltaTime)
  �?
  ├─ ContextStack->Evaluate(DeltaTime)
  �?   �?
  �?   ├─ Check auto-pop: if active context's camera is transient and finished,
  �?   �?  pop the context (with transition to previous context)
  �?   �?
  �?   ├─ ActiveDirector->Evaluate(DeltaTime)
  �?   �?   �?
  �?   �?   ├─ EvaluationTree->Evaluate(DeltaTime)
  �?   �?   �?   �?
  �?   �?   �?   ├─ Recursive DAG evaluation (per-frame memoization on all three node kinds):
  �?   �?   �?   �?   Leaf �?Camera->TickCamera(DeltaTime) �?returns pose
  �?   �?   �?   �?          (early-out if LastTickedFrameCounter == GFrameCounter)
  �?   �?   �?   �?   RefLeaf �?SnapshotRoot->Evaluate(DeltaTime) �?returns pose
  �?   �?   �?   �?          (walks captured TSharedPtr subtree; does NOT call back into a director)
  �?   �?   �?   �?   Inner �?eval left, eval right, Transition->Evaluate(src, tgt) �?blended pose
  �?   �?   �?   �?          (early-out with cached blend if reached twice in one frame)
  �?   �?   �?   �?
  �?   �?   �?   └─ CollapseFinishedTransitions(RootNode)
  �?   �?   �?        - If inner node's transition is finished: destroy left subtree, promote right
  �?   �?   �?        - If inner node's transition is NOT finished: recurse into left subtree only
  �?   �?   �?        - Leaf/RefLeaf: return as-is
  �?   �?   �?
  �?   �?   ├─ PreviousEvaluatedPose = LastEvaluatedPose
  �?   �?   └─ LastEvaluatedPose = tree result
  �?   �?
  �?   └─ Return pose
  �?
  ├─ RunningCamera = ContextStack->GetRunningCamera()
  ├─ CurrentContext = ContextStack->GetActiveContextName()
  ├─ CurrentCameraPose = evaluated pose
  �?
  ├─ UpdateActions(DeltaTime)  // tick camera actions
  �?
  └─ Convert pose �?FMinimalViewInfo �?apply to viewport
```

### Camera Tick (inside TickCamera)

When a leaf node evaluates, it calls `Camera->TickCamera(DeltaTime)`:

```
TickCamera(DeltaTime)
  �?
  ├─ LastFrameCameraPose = CameraPose  // save previous frame
  ├─ If transient: decrement RemainingLifeTime
  �?
  ├─ OnActionPreTick.Broadcast(...)    // action pre-tick hook (whole-camera scope)
  ├─ OnPreTick.Broadcast(...)          // node pre-tick hooks
  �?
  ├─ For each CameraNode (via FullExecChain, or linear fallback):
  �?   For each action in PreNodeTickActions where TargetNodeClass matches Node's class:
  �?     Action->OnExecute(DeltaTime, InOutPose, InOutPose)   // per-node pre hook
  �?   Node->TickNode(DeltaTime, CurrentPose, OutPose)
  �?   For each action in PostNodeTickActions where TargetNodeClass matches Node's class:
  �?     Action->OnExecute(DeltaTime, InOutPose, InOutPose)   // per-node post hook
  �?   CurrentPose = OutPose            // chain output to next node's input
  �? (FullExecChain also interleaves SetVariable entries between camera nodes)
  �?
  ├─ OnPostTick.Broadcast(...)         // node post-tick hooks
  ├─ OnActionPostTick.Broadcast(...)   // action post-tick hook (whole-camera scope)
  �?
  ├─ OnUpdateCamera(...)               // Blueprint override opportunity
  �?
  └─ CameraPose = final pose
      return CameraPose
```

`OnPreTick` receives the same in-flight pose object that downstream node execution will use. Nodes that undo previous-frame post-processing before upstream solvers run must write that in-flight `OutCameraPose`, not only `CameraPose` on the owning actor. `CollisionPushNode` depends on this: it stores the position before collision adjustment, then restores that unpushed position in pre-tick so screen-space pivot / constraint nodes solve from the authored camera pose instead of from last frame's collision-shifted pose.

---

## 4. Context Stack (Tier 1)

### Purpose

The Context Stack manages camera "modes" �?gameplay, cinematic, UI overlay, etc. Each mode is a named context with its own Director and evaluation tree, completely independent from other contexts.

### Lifecycle

Contexts are identified by `FName` and must be registered in `UComposableCameraProjectSettings::ContextNames`. The first entry is the base context, initialized during `PCM::InitializeFor` (PostInitializeComponents phase, before any actor's BeginPlay).

**Push**: `EnsureContext(ContextName)` checks if the context exists on the stack. If not, creates a new entry with a fresh Director and pushes it. If it exists already, returns the existing Director. Importantly, "ensure" means move-to-top if already present �?position matters because only the top context is evaluated.

**Pop**: `PopContext(ContextName)` or `PopActiveContext()` removes a context. If the popped context is the active (top) one and a transition is provided, the system sets up an inter-context blend: the previous context's Director *resumes its existing running camera in place* via `ResumeCurrentCameraWithReferenceSource` �?the running camera and its whole sub-tree are preserved, their per-node state (damping, interpolators, spline progress) continues without reset. The popped entry moves to `PendingDestroyEntries` and stays alive until the transition finishes. If the popped context is not the top one, it is removed immediately.

**Auto-pop**: When the active context's running camera is transient and its lifetime expires (`IsFinished() == true`), the context is automatically popped during `Evaluate()`.

**Base context protection**: The last remaining context (the base) cannot be popped.

### Inter-Context Transitions

There are two distinct inter-context flows, and they use different APIs. Mixing them up is a real source of bugs, so they are spelled out explicitly here.

#### The reference leaf is a SNAPSHOT, not a live pointer

Before either flow, fix this mental model: a `FComposableCameraEvaluationTreeReferenceLeafNodeWrapper` holds a `TSharedPtr<FComposableCameraEvaluationTreeNode> SnapshotRoot` �?a shared pointer to the source director's tree root **at the moment the RefLeaf was created**. Subsequent mutations to the source director's tree (for example, the source being popped and having its own pop inner installed at its root) do NOT follow into the RefLeaf; the RefLeaf keeps pointing at the original captured subtree.

Two consequences:

- The evaluation reachable graph is a **DAG with no cycles**. Same original leaf may be reached via multiple paths (e.g. pop side + pop-source snapshot �?push-source snapshot �?same leaf), but there is never a back-edge that loops to an already-visited node.
- Multiple cameras visible in one frame may correspond to the same UObject instance via different paths. To keep per-node state (damping, interpolators, noise seeds, `RemainingLifeTime`) from double-advancing, `AComposableCameraCameraBase::TickCamera` memoizes on `GFrameCounter` �?the first call in a frame ticks and caches, every subsequent same-frame call returns the cached pose. The same memoization principle applies to `FComposableCameraEvaluationTreeInnerNodeWrapper` (so a shared transition's `RemainingTime` advances once per frame) and `FComposableCameraEvaluationTreeReferenceLeafNodeWrapper` (skip the re-walk).

#### Push (A �?B, new context on top) �?live source, fresh target

1. B's Director activates a new camera with `ActivateNewCameraWithReferenceSource`.
2. B's evaluation tree is built with a root inner node:
   - Left child: reference leaf with `SnapshotRoot = A.Director->GetEvaluationTree()->GetRootNode()` captured right now �?typically the `[Leaf] A` node.
   - Right child: leaf wrapping the freshly-spawned B camera.
   - Transition: the blend from A to B.
3. Each frame, the reference leaf re-evaluates the captured A subtree (ticking A's camera). The source director's own `Evaluate()` is **not called** �?`A.Director` is untouched by this path. A stays animated because its leaf is live inside the snapshot.
4. When the transition finishes, `CollapseFinishedTransitions` promotes B's leaf to the root; the reference leaf is discarded. The TSharedPtr to A's leaf drops, but A.Director still holds its own reference, so A's leaf stays alive.

#### Pop (B popped, A below resumes) �?live source, preserved target

1. A's Director calls `ResumeCurrentCameraWithReferenceSource` �?this does NOT spawn a new camera. A's existing `RunningCamera` and its whole sub-tree are preserved.
2. A's evaluation tree is mutated in one place only: the current `RootNode` is wrapped as the right child of a new inner node.
   - Left child: reference leaf with `SnapshotRoot = B.Director->GetEvaluationTree()->GetRootNode()` captured right now �?this may be a `[Leaf] B` (push already settled) or an `[Inner push] (RefLeaf �?A, [Leaf] B)` (push still in flight).
   - Right child: A's preserved root (same TSharedPtr as before, un-mutated).
   - Transition: the pop blend from B to A.
3. B's entry moves to `PendingDestroyEntries`.
4. When the transition finishes, `CollapseFinishedTransitions` promotes A's preserved root back to the tree root; `OnTransitionFinishesDelegate` fires, A's `PendingDestroyEntry` is cleaned up, and `B.Director->DestroyAllCameras()` runs.

#### Why snapshot semantics work where live pointers don't

Under the old "RefLeaf holds a live `UComposableCameraDirector*`" scheme, a pop-while-push-still-active window produced a cycle: A.tree has `RefLeaf �?B.Director`, B.tree still has `RefLeaf �?A.Director` from the push, and `A.Evaluate �?B.Evaluate �?A.Evaluate` loops. Band-aid defenses (reentrancy guard returning `LastEvaluatedPose`, force-freezing the source on pop) either still leaked feedback through the cached-pose field or papered over the cycle by discarding a semantically meaningful frame of animation.

With snapshots: A's new pop-side RefLeaf captures B.tree.root **at pop time**, and that snapshot includes B's push-side `RefLeaf �?A_old_leaf`. The `A_old_leaf` captured there is the raw A leaf (which is also the Right child of A.tree's new root) �?the same TSharedPtr. So the DAG is:

```
A.new_root (Inner pop)
  ├─ Left  : RefLeaf ──�?B.push_Inner
  �?                       ├─ Left  : RefLeaf ──�?A_old_leaf  (same ptr as below)
  �?                       └─ Right : B_leaf
  └─ Right : A_old_leaf  (same ptr as above)
```

No self-reference. `A.Evaluate` walks its tree in one linear pass, `A_old_leaf` is reached twice but only ticks A once thanks to `LastTickedFrameCounter`, and `B.Evaluate` is never called from anywhere. Pop blend at the top uses a live B pose (from walking the snapshot) as source and a live A pose as target �?exactly what "blend from what the player was seeing to A" means semantically.

#### Why the pop path preserves the existing camera

Spawning a fresh instance of A's camera class at pop time (the `ActivateNewCameraWithReferenceSource` approach used for pushes) would reset every node's internal state �?damping history, interpolator `bStartFrame` flags, spline progress, noise seeds. A's camera has been ticking throughout B's lifetime via the push-side reference leaf snapshot, so there is continuous state to preserve; tearing it down at pop time produces a visible snap on the first post-pop frame. `ResumeCurrentCameraWithReferenceSource` only mutates the tree topology (wrapping the root), leaving every camera and node untouched.

### Stack Data Structure

```cpp
TArray<FComposableCameraContextEntry> Entries;          // LIFO: [0] = base, Last() = active
TArray<FComposableCameraContextEntry> PendingDestroyEntries;  // popped but in transition

struct FComposableCameraContextEntry {
    UComposableCameraDirector* Director;
    FName ContextName;
    FComposableCameraPose LastPose;
};
```

---

## 5. Evaluation Tree (Tier 2)

### Structure

The evaluation tree is a binary tree with three node types stored in a `TVariant`:

```cpp
TVariant<LeafNodeWrapper, ReferenceLeafNodeWrapper, InnerNodeWrapper>
```

**Leaf**: Wraps a single `AComposableCameraCameraBase*`. Evaluating it ticks the camera and returns its pose.

**Reference Leaf**: Holds a `TSharedPtr<FComposableCameraEvaluationTreeNode> SnapshotRoot` �?a shared pointer to another director's tree root **captured at the moment the RefLeaf was created**. Evaluating it walks the captured subtree directly (so the source context's cameras still tick live) without ever calling back into the source director's `Evaluate`. A weak `DebugSourceDirector` pointer is kept only for label display. Does not own any cameras �?the captured subtree's nodes are kept alive via `TSharedPtr`, and the camera UObjects inside are destroyed only by their home director's own `DestroyAllCameras`, not by this RefLeaf being discarded.

**Inner**: Wraps a `UComposableCameraTransitionBase*` and owns left (source) and right (target) children via `TSharedPtr`. Evaluation: evaluate left, evaluate right, pass both poses to the transition, return blended result.

### Camera Activation (Tree Building)

**With transition** (most common):
```
Before:          After:
  [CamA]           [Inner: Transition]
                    /              \
              [CamA: source]   [CamB: target]
```
The existing tree becomes the left subtree. A new leaf for the camera becomes the right subtree. A new inner node wrapping the transition becomes the root.

**Without transition** (camera cut):
The existing tree is destroyed and replaced with a single leaf for the new camera.

**With reference source** (inter-context):
```
  [Inner: Transition]
   /              \
[RefLeaf: OldDir]  [CamB: target]
```

**Nested activation** (intra-context under inter-context root):

When the root is an inter-context transition (left child is a reference leaf), new intra-context activations nest under the right subtree instead of wrapping the entire tree. This preserves the inter-context blend while allowing camera switches underneath:
```
Before:                          After activating CamB2:
  [InterCtx]                       [InterCtx]
   /        \                       /        \
[RefLeaf]  [CamB1]             [RefLeaf]  [IntraCtx]
                                            /       \
                                        [CamB1]   [CamB2]
```
When the inter-context transition finishes first, the tree collapses to just the right subtree (the intra-context blend). When the intra-context blend finishes, it collapses to the latest camera. This is relevant when gameplay code activates a new camera in the active context while an inter-context blend is still running.

### Collapse Logic

After evaluation, `CollapseFinishedTransitions` walks the tree:

- **Inner node, transition finished**: Destroy left subtree cameras, promote right subtree, fire `OnTransitionFinishesDelegate`. The inner node is replaced by its right child.
- **Inner node, transition NOT finished**: Recurse into both subtrees. The left side may have chained transitions that finish while this one is active. The right side may have nested intra-context transitions (from LS camera cuts) that finish independently.
- **Leaf / Reference Leaf**: Return as-is.

This produces a natural right-grows pattern: older cameras collapse away from the left while the rightmost path always leads to the current running camera.

### GC Integration

The tree uses `TSharedPtr` for node ownership (since `USTRUCT` cannot be `UPROPERTY` with shared pointers to itself). To prevent UObject garbage collection of cameras and transitions held in the tree, `AddReferencedObjects` walks the tree recursively and registers all UObjects with the collector.

---

## 6. Cameras

### AComposableCameraCameraBase

Cameras derive from `ACameraActor` and are the fundamental evaluation unit. The class is marked `NotBlueprintable` �?Blueprint subclassing is forbidden. All camera behavior is defined through `UComposableCameraTypeAsset` data assets, which describe the node composition, pin connections, parameters, and transitions. At runtime, the system spawns the base class and populates it from the type asset.

A camera does not define behavior through virtual functions �?instead, it holds an ordered array of `UComposableCameraCameraNodeBase*` nodes that execute sequentially.

Key properties:

- `CameraTag` (FGameplayTag): Identifier used by modifiers to target specific cameras
- `EnterTransition`: Fallback transition for resume/reactivation scenarios
- `CameraNodes[]`: Array parallel to `NodeTemplates` �?`CameraNodes[i]` is the runtime duplicate of `NodeTemplates[i]`. Entries for nodes not referenced by the execution chain are nullptr (orphaned nodes are skipped during duplication to save memory and init cost). Per-frame execution is driven by `FullExecChain`, not array order.
- `ComputeNodes[]`: Array parallel to `ComputeNodeTemplates` �?`ComputeNodes[i]` is the runtime duplicate of `ComputeNodeTemplates[i]`. Same skip-unconnected rule as `CameraNodes`: orphaned entries are nullptr. Populated by `OnTypeAssetCameraConstructed`, which registers each non-null duplicate into the shared runtime data block using the offset `NodeTemplates.Num() + i` so compute and camera node pins live in a single flat pin space at runtime.
- `CameraPose` / `LastFrameCameraPose`: Current and previous frame poses
- `bIsTransient`, `LifeTime`, `RemainingLifeTime`: Transient camera lifecycle
- `SourceTypeAsset` (`TObjectPtr<UComposableCameraTypeAsset>`, strong): The type asset that built this camera. Stored during `OnTypeAssetCameraConstructed` so `PCM::ReactivateCurrentCamera` can restore the pending state and fully reconstruct the camera on reactivation. Strong (not weak) because the camera physically depends on this asset to rebuild �?a transiently-loaded source (soft path / DataTable row / BP local) would be reclaimed under a weak ref between activation and the next modifier-triggered reactivate, leaving an empty-shell camera. The runtime DataBlock still has to mark its slot `UScriptStruct` types independently because it isn't reflection-walked through this field.
- `SourceParameterBlock` (`FComposableCameraParameterBlock`): The caller-provided parameters applied at activation from a type asset. Stored alongside `SourceTypeAsset` for the same reactivation purpose.
- `TypeAssetNodeTemplateCount` (`int32`): The exact count of `TypeAsset->NodeTemplates` at construction time. Used as the base offset for compute-node pin keys in the RuntimeDataBlock (`compute node i` has pin key `NodeIndex = TypeAssetNodeTemplateCount + i`). Stored explicitly because `CameraNodes.Num()` can differ from `NodeTemplates.Num()` if `OnTypeAssetCameraConstructed` skips null templates during duplication.

**GC hazard �?Actor/Object pins in RuntimeDataBlock:** The RuntimeDataBlock stores all values (including `AActor*` and `UObject*` pointers) as type-erased bytes in a flat `TArray<uint8>`. The garbage collector cannot see these pointers. If a referenced actor is destroyed, the data block retains a dangling pointer (not null). Nodes that read Actor pins per-frame via `GetInputPinValue<AActor*>()` must use `IsValid(Actor)` instead of `Actor != nullptr` before dereferencing. Nodes that receive Actor values through subobject pin application (`ApplySubobjectPinValues`) are safe �?the value is written into a GC-tracked `UPROPERTY` during one-shot initialization, and the GC can null it if the actor is destroyed afterward.

### Lifecycle

Cameras are spawned from the base class with nodes duplicated from a `UComposableCameraTypeAsset` inside an OnPreBeginplay callback.

**Shared activation spine** (`Director::ActivateNewCamera`):

1. `SpawnActorDeferred<CameraClass>(InitialTransform)` �?the actor is allocated but not yet running `BeginPlay`.
2. `ForceCameraPoses(Camera, InitialTransform)` �?seeds `CameraPose` and `LastFrameCameraPose` so the first tick sees a consistent starting pose.
3. `Camera->Initialize(PlayerCameraManager)` �?stores `CameraManager` and calls `InitializeNodes()`, which walks `CameraNodes` and per node: calls `Node->Initialize(OwningCamera, PCM)` (which in turn fires `OnInitialize` �?the per-node one-shot hook) and wires the `OnPreTick` / `OnPostTick` delegates. It then walks `ComputeNodes` and calls `Node->Initialize` on each compute node without wiring tick delegates �?compute nodes get the same pin-system plumbing as camera nodes but must not burn per-frame cycles. For type-asset cameras both arrays are still empty at this point and the loops are no-ops �?the real per-node init happens in step 4.
4. `OnPreBeginplay.ExecuteIfBound(Camera)` �?the activation callback, bound to `OnTypeAssetCameraConstructed`, which: (a) renames the camera actor to `Camera_<TypeAssetName>` (both internal `Rename` and editor-visible `SetActorLabel`) so it is identifiable in the World Outliner and debug logs; (b) clears both `CameraNodes` and `ComputeNodes`, pre-sizes each to match `NodeTemplates.Num()` / `ComputeNodeTemplates.Num()`, then duplicates only those templates whose index appears in the execution chain (`FullExecChain` or `ExecutionOrder`); orphaned nodes (not referenced by any exec entry) are left as nullptr to save memory and init cost while preserving index correspondence with the type asset's layout; (c) builds a flat `OwnedRuntimeDataBlock` whose slot count equals `NodeTemplates.Num() + ComputeNodeTemplates.Num()`, calls `SetRuntimeDataBlock` on each non-null camera node using index `i` and on each non-null compute node using the offset `NodeTemplates.Num() + i`; and (d) calls `Camera->InitializeNodes()` explicitly �?this is the moment every non-null node's `Node::Initialize` (and therefore `OnInitialize_Implementation`) runs. The offset is the *only* place in the runtime or editor where the two NodeIndex spaces cross �?in the editor's durable state they are strictly disjoint (`NodeTemplates` indices and `ComputeNodeTemplates` indices both start at zero and don't overlap).
5. `PlayerCameraManager->ApplyModifiers(Camera, true)` �?effective per-node-class modifiers are applied. This runs *after* step 4 so that `CameraNodes` is fully populated before modifiers iterate it.
6. `Camera->FinishSpawning(InitialTransform)` �?drives `AActor::BeginPlay`, which calls `BeginPlayCamera()`. When a `ComputeFullExecChain` is available, `BeginPlayCamera` walks that chain, executing compute nodes and interleaving `SetVariable` node evaluations to populate scratch variables. Otherwise it falls back to a linear walk of `ComputeNodes` in array order, calling `ExecuteBeginPlay()` on each non-null compute node. By the time this runs, per-node `Initialize` has already fired for every compute node (in step 4), so compute nodes are free to use the pin system, internal variables, and `OwningPlayerCameraManager->GetCurrentCameraPose()` from inside `ExecuteBeginPlay`. On a camera with no compute nodes the loop is a harmless no-op.
7. `EvaluationTree::OnActivateNewCamera(Camera, Transition)` �?the new camera is wired into the tree as the right (target) subtree of a new inner node, or as a plain leaf if there is no transition.

After activation: each subsequent frame, `TickCamera()` executes the full node pipeline �?either via `FullExecChain` (when available) with interleaved `SetVariable` dispatch, or via linear `CameraNodes` walk (fallback). On collapse (transition finished): the camera actor is destroyed via `DestroySubtreeCameras`.

**Per-node one-shot setup** �?the rule is: everything a per-frame node needs to do exactly once per activation (caching `OwningCamera` / `OwningPlayerCameraManager`, instantiating internal objects, reading exposed parameters, seeding per-activation state) belongs in `OnInitialize_Implementation`, which runs from inside `Node::Initialize`. There is no longer a separate `OnBeginPlayNode` hook. Nodes that previously needed the outgoing camera's pose (what `BeginPlayNode` used to pass as `CurrentCameraPose`) can read the same value via `OwningPlayerCameraManager->GetCurrentCameraPose()` �?this is exactly what `AActor::BeginPlay` passed into `BeginPlayCamera` before the refactor.

**Camera-level one-shot compute** �?any logic that is *not* about a single node's own setup but is instead about shaping data shared across the camera (e.g., precomputing an offset transform, deriving a blend weight, reading gameplay state and publishing it as an internal variable downstream nodes consume) belongs in a compute node on the `ComputeNodes` chain. Compute nodes derive from `UComposableCameraComputeNodeBase` (itself a subclass of `UComposableCameraCameraNodeBase`) so they inherit the pin system and the `OnInitialize` hook, but the camera deliberately does not register them for `OnPreTick` / `OnPostTick`. They run exactly once per activation, from `BeginPlayCamera`, via `ExecuteBeginPlay()` �?a plain `virtual` on `UComposableCameraComputeNodeBase` (not a `BlueprintNativeEvent` in 4a; promote to one if Blueprint authoring of compute nodes becomes a requirement). Execution order is array order in `ComputeNodes`, which for type-asset cameras is produced by the editor's dedicated `UComposableCameraBeginPlayStartGraphNode` sentinel and its linear exec chain (see `EditorDesignDoc.md §8` for the BeginPlay compute chain and its sync/rebuild phases).

### Transient Cameras

A camera can be marked transient with a fixed lifetime. When `RemainingLifeTime <= 0`, `IsFinished()` returns true, and the context stack's auto-pop mechanism fires. Transient cameras always live in their own context �?they never share a context with persistent cameras. This design prevents a transient camera from destroying the persistent camera chain when it expires.

### FComposableCameraPose (Pose Structure)

`FComposableCameraPose` is the unit of data that flows along every edge of the evaluation tree. Every camera produces a full pose each frame; every transition takes two full poses and emits a full pose. Blueprint authors and C++ node authors both see the same struct. It is deliberately flat (no nesting) so that it can be passed by value cheaply along hot paths.

**Fields** (grouped by concern):

- **Transform**: `Position` (FVector), `Rotation` (FRotator).
- **FOV (dual-mode)**: `FieldOfView` (double, degrees) and `FocalLength` (float, mm). One of the two is the authoritative source per pose �?see the FOV rules below.
- **Physical camera (CineCameraComponent analogs)**: `SensorWidth`, `SensorHeight`, `Aperture`, `FocusDistance`, `ShutterSpeed`, `ISO`, `DiaphragmBladeCount`, `SqueezeFactor`, `Overscan`.
- **Physical application weights**: `PhysicalCameraBlendWeight` (0..1) scales DoF fields; `ExposureBlendWeight` (0..1) scales ISO/Shutter exposure fields. A pose with both weights at 0 is effectively "physical-fields ignored at application time" even though its numeric fields still participate in blending.
- **Projection / aspect**: `ProjectionMode` (Perspective / Orthographic), `ConstrainAspectRatio` (bool), `OverrideAspectRatioAxisConstraint` (bool), `AspectRatioAxisConstraint` (enum), `OrthographicWidth`, `OrthoNearClipPlane`, `OrthoFarClipPlane`.
- **Post-process**: `PostProcessSettings` (FPostProcessSettings). Default-constructed with all `bOverride_*` flags off, meaning "no opinion". Nodes (e.g., a future PostProcessNode) set specific `bOverride_*` flags and values. During blending, `FPostProcessUtils::BlendPostProcessSettings` lerps all properties including override flags. At apply-time in `GetCameraViewFromCameraPose`, pose PP is layered on top of the camera component's baseline PP via `FPostProcessUtils::OverridePostProcessSettings` (only properties with `bOverride_*` true take effect), followed by physical camera settings as before.

**FOV dual-mode and the "resolve before blend" rule.** A pose can describe its FOV either directly in degrees (`FieldOfView > 0`, `FocalLength <= 0`) or via the physical camera (`FocalLength > 0`, `FieldOfView <= 0`). `GetEffectiveFieldOfView()` resolves either form to a single degrees value using the standard `2·atan(croppedSensorWidth · Overscan / (2 · FocalLength))` formula for the physical side. **Blending never interpolates raw `FieldOfView` or raw `FocalLength` directly.** Both sides are resolved to degrees first, the degrees values are lerped, and the result is written as a degrees-mode pose (`FocalLength = -1`). This is mandatory for two reasons: (1) focal length is non-linear in FOV (24mm �?35mm is visually much larger than 85mm �?96mm) �?linear blending of mm would produce a visibly wrong curve; (2) when one side is in physical mode and the other in degrees mode, there is no meaningful way to lerp mm against mm-from-nothing. Node authors who set FOV from code must use `SetFieldOfViewDegrees(...)` (which clears the focal-length sentinel) so downstream consumers see an unambiguously degrees-mode pose.

**Blend semantics by field category:**
- Numeric physical fields (sensor, aperture, ShutterSpeed, ISO, DiaphragmBladeCount, SqueezeFactor, Overscan, PhysicalCameraBlendWeight, ExposureBlendWeight): linear lerp.
- `FocusDistance`: `LerpOptional` �?treats values `<= 0` as "unset". If both are valid, lerp; if only one is valid, pass it through; if neither, stay unset. This matches CineCamera's "-1 means use the tracking actor" convention.
- Transform and FOV: described above.
- `ProjectionMode`, `ConstrainAspectRatio`, `OverrideAspectRatioAxisConstraint`, `AspectRatioAxisConstraint`: snap-at-50% rule. These types are not meaningfully lerpable �?a "half-ortho, half-perspective" pose is nonsense. The output takes the source value for `t < 0.5`, the target value for `t �?0.5`.
- Orthographic fields (`OrthographicWidth`, `OrthoNearClipPlane`, `OrthoFarClipPlane`): linear lerp. Safe to interpolate even while `ProjectionMode` snaps.
- `PostProcessSettings`: blended via `FPostProcessUtils::BlendPostProcessSettings`. All properties (numerics and `bOverride_*` booleans) are interpolated; booleans snap at 50% like the projection fields. A camera with no PostProcess node has all overrides off �?blending against it naturally fades the overridden properties toward their defaults and turns off the override flags at the 50% mark.

`BlendBy(Other, Alpha)` is the single entry point for blending one pose into another using all the rules above. Every transition's `OnEvaluate` should start its result pose from `CurrentSourcePose`, call `BlendBy(CurrentTargetPose, Alpha)`, and then overwrite only the fields it specifically customizes (e.g., a cylindrical transition overwrites Position and Rotation after BlendBy; an inertialized transition does the same with inertializer-derived values). Transitions must not hand-lerp individual pose fields �?if they do, any new pose field added in the future will silently drop out of that transition.

**Pose is authoritative over CameraComponent.** For projection, aspect, and post-process, the pose holds the truth. `GetCameraViewFromCameraPose` copies `ProjectionMode`, `ConstrainAspectRatio`, and the override/axis fields from the pose into the outgoing `FMinimalViewInfo`. The `ACameraActor`'s `CameraComponent` contributes its `AspectRatio` numeric value and its `PostProcessSettings` as the base layer. The pose's `PostProcessSettings` is then applied on top via `FPostProcessUtils::OverridePostProcessSettings` (only overridden properties take effect), followed by `ApplyPhysicalCameraSettings` for DoF/exposure. This three-layer stack (component baseline -> pose PP -> physical camera) means cameras without a PostProcess node pass through the component's PP unchanged, while cameras with one can override specific properties without wiping the baseline. Nodes that want to change projection/aspect/PP do so by writing the pose, not by mutating the actor.

**Applying physical fields.** `ApplyPhysicalCameraSettings(PostProcessSettings, bOverwriteSettings)` writes DoF fields scaled by `PhysicalCameraBlendWeight` and exposure fields scaled by `ExposureBlendWeight`. It preserves the incoming `AutoExposureApplyPhysicalCameraExposure` value and does not compensate ISO. PCM calls this after the camera component's baseline is captured, so per-frame physical authoring layers cleanly without wiping user-authored PP elsewhere on the component. `FocusDistance <= 0` is treated as "leave DoF focal distance unset"; only the other DoF fields are applied in that case.
---

## 7. Camera Nodes

### Execution Model

Nodes are the atomic units of camera behavior. Each node is a `UObject` subclass (instanced, edit-inline). The base class `UComposableCameraCameraNodeBase` is `NotBlueprintable` �?all built-in node types are authored in C++. For user-authored nodes, `UComposableCameraBlueprintCameraNode` provides a `Blueprintable` abstract base that exposes `InitializeNode`, `TickNode`, and `GetPinDeclarations` as overridable Blueprint events. Blueprint subclasses are auto-discovered by the camera type asset editor via class iteration.

**Per-activation (once):** `Node::Initialize(OwningCamera, PCM)` is called from `Camera::InitializeNodes` �?explicitly from `OnTypeAssetCameraConstructed` after nodes have been duplicated from the type asset. It caches `OwningCamera` / `OwningPlayerCameraManager`, auto-applies subobject pin values, and then fires `OnInitialize()` �?a `BlueprintNativeEvent` whose `_Implementation` is where nodes instantiate internal objects, cache component refs, and seed per-activation state. C++ node subclasses override `OnInitialize_Implementation` and should call `Super::OnInitialize_Implementation()` when chaining.

**Per-tick (every frame):**

1. `OnPreTick()` fires for all nodes (via delegate)
2. For each node in order: `TickNode(DeltaTime, CurrentPose, OutPose)` �?the node reads `CurrentPose`, applies its logic, writes `OutPose`
3. The output of one node becomes the input of the next (sequential pipeline)
4. `OnPostTick()` fires for all nodes

### Two Parameter Types

Nodes have two kinds of configurable values:

**Input Parameters**: The node's own UPROPERTY members. Authored in the type-asset editor. Two distinct roles:

- **Non-pin UPROPERTYs** �?e.g. internal mode selectors, cached state, debug flags. Stay constant for the camera's lifetime unless a modifier or initializer touches them.
- **Pin-matched UPROPERTYs** �?a UPROPERTY whose `FName` exactly matches a pin name declared in `GetPinDeclarations()`. Refreshed *every frame* before `OnTickNode` runs via the base class's auto-resolve (see "Top-level Pin Auto-Resolution" below). The UPROPERTY's authored value is used only as the bottom-of-chain fallback; any wire, exposed parameter, or per-instance default override wins.

**Pin Values**: Typed data slots declared via `GetPinDeclarations()` and stored in the `RuntimeDataBlock`. Input pins can be wired from another node's output, exposed as parameters on the activation K2 node, fall back to a per-instance default override, or �?when matched to a UPROPERTY �?fall back to the node's UPROPERTY value. Output pins write values that downstream nodes can read. This is how nodes communicate: one node writes an output pin (e.g., pivot position), and downstream nodes read it via their input pins.

The `RuntimeDataBlock` storage layer has two parallel pools: a contiguous `Storage` byte array for bytewise-safe pin values (numeric / Name / Vector / Rotator / Transform / FFloatInterval / engine math whitelist / explicit `STRUCT_IsPlainOldData` USTRUCTs) and a typed `StructSlots` array of `FInstancedStruct` for all other struct pin values. Generic user USTRUCTs are not accepted by reflected-property heuristics because non-UPROPERTY members are invisible to reflection. Offsets stored in the per-role lookup maps (`OutputPinOffsets`, `ExposedParameterOffsets`, `InternalVariableOffsets`, `ExposedInputPinOffsets`, `DefaultValueOffsets`, `InputPinSourceOffsets`) discriminate the two pools by magnitude �?values `>= StructSlotsOffsetBase` (`INT32_MAX/2`) index into `StructSlots`, smaller values are real byte offsets in `Storage`. The dispatch lives in templated `ReadValue<T>` / `WriteValue<T>` via `if constexpr (TModels_V<CStaticStructProvider, T>)` plus a runtime offset check, so call sites stay unified �?a node Tick emitting `WriteOutputPin<FMyStruct>(...)` works the same whether the struct is POD or not. Runtime slot metadata records `{PinType, Size, StructType}` for every offset; typed access refuses unknown offsets, pin-type mismatches, size mismatches, and same-size cross-struct reads/writes before memcpy or `CopyScriptStruct`. Non-template paths that need the same dispatch (the auto-resolve loop's Struct case, subobject pin resolution) call `ResolveInputPinOffset` to get the offset, then branch on `IsStructSlotOffset`.

For **input pins matched to a UPROPERTY**, subclass code can read the UPROPERTY member directly �?no `GetInputPinValue<T>()` call is needed, because `TickNode` re-resolves the member before `OnTickNode_Implementation` runs. The auto-resolve writer uses memcpy for POD pins and `Property->CopyCompleteValue` for non-POD struct pins (which routes through each property's `operator=` so embedded `FString` / `TArray` / object refs get a proper per-property copy). Per-frame copy into a non-POD struct UPROPERTY is no-alloc once embedded heap-owned members fit their existing capacity �?the warmed-up steady state is just memcpy of bytes into the existing FString buffer; first-tick or grow events allocate once and that allocation is bounded per camera lifetime. For **input pins with no UPROPERTY backing** (typically Blueprint-authored nodes that introduce pins not tied to a reflected field), subclasses still call `GetInputPinValue<T>("PinName")`. Output pins always go through `SetOutputPinValue<T>("PinName", Value)`.

### Top-level Pin Auto-Resolution

To keep subclass `OnTickNode` code free of repetitive `GetInputPinValue<T>(FName)` calls, the base `TickNode()` re-resolves every declared input pin that has a matching top-level UPROPERTY into that UPROPERTY each frame, before `OnTickNode_Implementation` fires. The flow is:

1. On first call for a given UClass, `GetOrBuildPinBindings()` walks the CDO's `GetPinDeclarations()` output, indexes the class's edit-visible top-level UPROPERTYs by FName, and emits one `FComposableCameraNodePinBinding` per pin that (a) is an Input, (b) has a matching UPROPERTY, and (c) has a pin type that matches the UPROPERTY's reflected type via `TryMapPropertyToPinType`. The table is cached module-locally keyed by `UClass*`.
2. `TickNode` calls `ShouldAutoResolveInputPins()` (default `true`) and, if allowed, calls `ResolveAllInputPins()` �?a tight switch-dispatch loop that performs `RuntimeDataBlock->TryResolveInputPin<T>()` for every bound pin and writes the result directly into the node's field at the recorded byte offset.

Subobject property pins (compound names like `Interpolator.Speed`) are **not** touched by `ResolveAllInputPins` �?they continue to be resolved once at `Initialize()` via `AutoApplySubobjectPinValues`, which matches the one-shot lifecycle of interpolator/subobject state.

**Opt-out.** Nodes that manage their own pin reads (rarely needed �?e.g. nodes that keep a UPROPERTY as a non-pin cached state that is coincidentally named the same as a pin �?can override `ShouldAutoResolveInputPins()` to return `false`.

**Requirements for a pin to participate.** The pin and UPROPERTY names must match exactly (case and �?for bools �?the `b` prefix). The UPROPERTY must be `EditAnywhere` (or otherwise carry `CPF_Edit`) and must not be an Instanced subobject reference. Type mismatches are silently skipped at build time; the editor-side pin validator flags them at asset-save.

**Cost model.** One unordered-map `Find` per pin per frame plus one typed memory write. No reflection in the hot path. The cache builder runs once per concrete UClass on first activation.

**Known limitation.** The cache is keyed by `UClass*`. When a Blueprint-subclassed node is recompiled, the old cache entry becomes unreachable but is not evicted until the editor process ends. The new class rebuilds fresh. No runtime correctness issue; a small memory growth in iterative BP authoring sessions.

**Blueprint authoring notes.** The binding builder walks the full UClass hierarchy via reflection, so Blueprint-added variables on `UComposableCameraBlueprintCameraNode` subclasses participate in auto-resolve exactly like C++ UPROPERTYs. The rules:

- A BP variable whose name exactly matches a declared pin (and whose type maps cleanly via `TryMapPropertyToPinType`) gets overwritten each frame by the resolved pin value. Authors can read the variable directly in the TickNode event graph without calling `GetInputPinValueFloat` / `GetInputPinValueVector2D` / etc.
- The BP variable must be **Instance Editable** (carry `CPF_Edit`). Non-editable BP variables are skipped by the builder and do not participate.
- Pin names declared in a BP `GetPinDeclarations` override must follow the same exact-FName-match rule as C++ �?including the `b` prefix for booleans.
- If a pin-matched BP variable is used as mid-tick scratch storage (state that must persist across frames), override `ShouldAutoResolveInputPins` to return `false` on that BP subclass, or rename the variable so it no longer collides with the pin name and fall back to `GetInputPinValueX` for reads.
- BP recompile invalidates the cache entry for the old `UClass*`. The next camera activation after recompile rebuilds fresh bindings �?no editor restart required.

### Built-in Nodes (Summary)

| Node | Purpose |
|---|---|
| `ReceivePivotActorNode` | Reads an actor's position and writes it to an output pin |
| `PivotOffsetNode` | Offsets the pivot position in world/actor/camera space |
| `CameraOffsetNode` | Applies an offset in camera-local space |
| `LookAtNode` | Rotates camera to face a target (hard or soft constraint). No-ops when the camera and resolved target coincide so the singular `SpiralNode -> LookAtNode` composition (Spiral places the camera on its pivot when the radius curve crosses zero) does not produce a zero-rotation snap. |
| `ControlRotateNode` | Reads Enhanced Input and applies yaw/pitch rotation |
| `AutoRotateNode` | Auto-rotates back toward a reference forward when yaw/pitch leave the valid range, rotating to the nearest boundary. Reference direction is either an explicit vector (`DirectionMode = Direction`) or an actor's forward vector (`DirectionMode = ActorForward`, via `PrimaryActor`). When `bYawOnly = false`, ANY axis leaving its range engages auto-rotation on both axes, driven as a unified rotation by a single `FRotator` interpolator so yaw and pitch land together. Optional user-input interrupt (`bInterruptOnUserInput`) with a cooldown and a per-instance max-interrupt count (`MaxCountAfterInputInterrupt`) for "player keeps fighting it, stop trying" semantics. |
| `PivotRotateNode` | Synchronises the camera's rotation to a `PivotActor`'s world rotation each frame, with a per-pivot-local-frame `RotationOffset` (composed as `PivotActor.Quat * RotationOffset.Quat` �?same convention as `USceneComponent::RelativeRotation`) and an optional Instanced `Interpolator` (rotator). Standard interpolator idiom (re-seed Current �?live rotation, Target �?resolved target, advance one DeltaTime step) �?matches `AutoRotateNode` / `LookAtNode` soft-mode. Snap mode when `Interpolator` is null; pass-through when `PivotActor` is null. Useful for vehicle / mount / cockpit cameras that should adopt the rig's heading with a fixed relative offset and smooth catch-up. Differs from `LookAtNode` (computes a *gaze* direction from camera→target) and from `RelativeFixedPoseNode` (locks the *full* pose, not just rotation). |
| `CollisionPushNode` | Dual-mode collision resolution: (1) **Trace collision** �?casts a line or sphere trace from pivot to camera, pushing the camera toward the pivot on occlusion, with configurable exemption time. (2) **Self collision** �?carries a sphere around the camera and, when the sphere overlaps an obstacle, pushes the camera to the **far side** of the obstacle via a reverse sphere sweep from beyond the camera back toward the pivot. Both modes share the same interpolator pair (push/pull) and ignored-actor list. |
| `OcclusionFadeNode` | Fades occluding / proximate primitives by swapping their materials for a user-supplied transparency material. Two independent detection paths feeding one swap pipeline: (A) **Line-of-sight occlusion** �?async multi-sphere sweep from camera �?PivotActor each frame; hits passing the component-tag + mesh-type filters get fade-marked. (B) **Proximity fade** �?sphere overlap at the camera position; actors of `ProximityActorClass` (default APawn) within `ProximityRadius` get their fadable components marked. Delta tracking against `AppliedMaterialOverrides` means SetMaterial / CreateDynamicMaterialInstance only fires on entering/leaving the faded set. Fade shape + timing live entirely in the swap material's shader. Complements `CollisionPushNode` �?CollisionPush physically moves the camera around obstacles, OcclusionFade ghosts obstacles you don't want the camera to move around (thin pillars, tagged foliage, the player body when camera zooms close). |
| `HitchcockZoomNode` | Dolly-zoom / Vertigo effect �?camera dollies along the camera→subject axis while FOV changes in the opposite direction, keeping subject on-screen size constant while the background perspective warps. Captures `InitialDistance` / `InitialFOV` / `LockConstant = InitialDistance · tan(InitialFOV/2)` on first tick; each subsequent tick preserves the lock constant while solving one of (FOV, Distance) from the other. Driver enum picks which quantity the author controls: `FromFOVDelta` (author FOV trajectory, distance derives) or `FromDistanceDelta` (author dolly distance, FOV derives). Both curves are **additive deltas** (X=[0,1] normalized time, Y=0 at t=0), so they stay portable across cameras with different initial FOVs. Direction is resampled every tick from the upstream pose so upstream `LookAt` / `CameraOffset` still steer rotation during the effect �?Hitchcock owns only the radial distance + FOV. Writes `FieldOfView` + clears `FocalLength` to -1 so the pose is in FOV-mode; pair with an upstream `LensNode` (set `bOverrideFieldOfViewFromFocalLength=false` so LensNode doesn't contest FOV) to keep aperture / filmback / physical blend weight intact. Once-only playback (no Loop/PingPong); optional distance clamp is a safety rail against pathological curve shapes. |
| `FocusPullNode` | Dynamically drives the pose's `FocusDistance` from the distance to a target actor (PivotActor + optional BoneName / PivotZOffset, same pattern as CollisionPush / OcclusionFade). Single-responsibility �?only writes `FocusDistance`. Aperture / blade count / `PhysicalCameraBlendWeight` come from an upstream `LensNode`; the intended pairing is `LensNode(FocusDistance=-1) �?FocusPullNode(drives FocusDistance dynamically)`. Optional `FFloatInterval` clamp and optional interpolator for focus-pull damping (first-frame seed bypasses the lerp so initial focus snaps to the real distance). When `bEnableFocusPull` is false the node is a pass-through this tick �?useful for Blueprint-driven "focus hold" (ADS, cinematic freeze, etc.). |
| `VolumeConstraintNode` | Constrains the camera to stay inside a single Box or Sphere volume. Volume source is either an actor with a `UShapeComponent` (FromActor, first shape wins) or inline world-space definition (VolumeCenter / Rotation / BoxExtents / SphereRadius). Each tick, if the upstream position is outside the volume it's projected to the nearest boundary point (OBB: per-axis local-space clamp; Sphere: center + normalized delta × radius). Optional `ClampInterpolator` (instanced interpolator, three per-axis 1D instances) smooths the output to eliminate release snaps at the boundary and crease artifacts when nearest-point face switches across a corner. When the interpolator is null the node is stateless and deterministic. Chain placement: after position-writing nodes (`CameraOffset` / `LookAt`) that produce the desired position, before `CollisionPush` so collision resolves on the already-clamped input. |
| `FieldOfViewNode` | Sets FOV in degrees (via `SetFieldOfViewDegrees`), optionally dynamic based on actor scale. Writes to `FComposableCameraPose::FieldOfView` and clears `FocalLength`. |
| `LensNode` | Authors physical-lens parameters on the pose: `FocalLength`, `Aperture`, `FocusDistance`, `DiaphragmBladeCount`, `PhysicalCameraBlendWeight`. When `bOverrideFieldOfViewFromFocalLength` is true, also clears `FieldOfView` so the pose resolves FOV from `FocalLength + SensorWidth`. Gates DoF post-process contribution via `PhysicalCameraBlendWeight`; exposure is handled by `ExposureNode`. |
| `ExposureNode` | Authors physical exposure parameters on the pose: `ISO`, `ShutterSpeed`, `ExposureBlendWeight`. Gates exposure post-process contribution separately from Lens/DoF so optics can drive focus without changing brightness. Visual brightness change requires BOTH `AutoExposureMethod = Manual` AND `AutoExposureApplyPhysicalCameraExposure = true` on the resolved PP stack (PP Volume / PP Component / camera component PP). The Apply-Physical flag is not in Project Settings; only the Method default is. The node never toggles either flag; see "Physical Exposure Ownership". |
| `FilmbackNode` | Authors sensor and aspect-ratio parameters on the pose: `SensorWidth`, `SensorHeight`, `SqueezeFactor`, `Overscan`, `ConstrainAspectRatio`, `OverrideAspectRatioAxisConstraint`, `AspectRatioAxisConstraint`. Sensor dimensions feed into the pose's focal-length-mode FOV resolution. |
| `OrthographicNode` | Switches the pose into orthographic projection and authors `OrthographicWidth`, `OrthoNearClipPlane`, `OrthoFarClipPlane`. Transitions snap `ProjectionMode` at 50% blend weight per the `BlendBy()` contract. |
| `RotationConstraints` | Constrains yaw/pitch within defined ranges |
| `ScreenSpacePivotNode` | Keeps pivot within screen-space bounds |
| `PivotDampingNode` | Dampens pivot position changes |
| `RelativeFixedPoseNode` | Maintains pose relative to a transform or actor |
| `DirectionalMoveNode` | Moves from `InitialTransform` along a camera-local `Direction` at `Speed`. `Duration < 0` moves forever; `Duration >= 0` clamps travel time and then holds the final offset. |
| `TwoPointMoveNode` | Moves from `SourceTransform` to `TargetTransform` over `Duration`, optionally shaped by a normalized float curve. |
| `SplineNode` | Places camera on a spline (multiple spline math backends) |
| `SpiralNode` | Positions the camera on a helical path around a pivot; orientation left for a downstream `LookAtNode`. Parameterized by three Progress curves (X �?[0,1] normalized time, Y in absolute world units): `RadiusCurve` (cm), `HeightCurve` (cm signed along Axis), `AngleCurve` (degrees) �?direct-eval semantics same as SplineNode's `AutomaticMoveCurve`, so `Position(t)` is O(1) at any t with no accumulated state. Spiral Space defined by `RotationAxis` (WorldUp / PivotActorUp / Custom) and `ReferenceDirection` (WorldX / PivotActorForward / CameraInitialForward / PivotActorInitialForward / Custom). Pivot comes from either an Actor or a raw Vector; `PivotOffset` is applied in Spiral Space so "orbit around the character's head" tracks actor rotation when a live actor basis is selected. PlayMode: Once / Loop / PingPong. Useful for victory-cam orbits, death-cam rises, ending pull-aways. |
| `ImpulseResolutionNode` | Resolves impulse forces from volumes |
| `MixingCameraNode` | Mixes output from multiple cameras |
| `PostProcessNode` | Applies `FPostProcessSettings` to the pose via `FPostProcessUtils::OverridePostProcessSettings`. Configured entirely in the Details panel (no pins) �?works like a PostProcessVolume scoped to a single camera type. Only properties whose `bOverride_*` flag is true take effect; all others pass through from the component baseline or earlier nodes. Multiple PostProcess nodes compose in execution order (later overrides win). |
| `ViewTargetProxyNode` | Lightweight internal node that reads `FMinimalViewInfo` from a target actor's `UCameraComponent` each tick and converts it into `FComposableCameraPose`. **Not user-facing** �?created programmatically by `PlayCutsceneSequence`. Supports two modes: static target (reads from a specific actor) or LS polling (walks the CameraCut track each tick to find the active camera at the current playback position). Captures transform, FOV, projection, and post-process from the target actor's evaluated camera state. |

### Level Sequence Compatibility (per node class)

Every node declares via `GetLevelSequenceCompatibility() const` (a `BlueprintNativeEvent`, so BP-authored nodes can override it alongside native ones �?C++ overrides go on `GetLevelSequenceCompatibility_Implementation()`) whether it is safe to evaluate through the LS authoring path (§17 explicit path �?PCM is null there). Three buckets:

| Bucket | Semantics | Built-in nodes |
|---|---|---|
| `Compatible` (default) | Evaluates correctly without a PCM. | All nodes except those listed below. |
| `RequiresPCM` | Hard-depends on PCM (e.g. spawns child cameras through it). No-ops in LS with a warning surfaced by the Details-panel customization on `FComposableCameraTypeAssetReference`. | `UComposableCameraMixingCameraNode`. |
| `ComputeOnly` | Never evaluated in LS �?the whole compute chain is skipped because BeginPlay already ran with empty arrays before nodes were populated. Designers should re-source any value the compute node would publish as an exposed parameter instead. | Every subclass of `UComposableCameraComputeNodeBase`. |

The `ScreenSpaceConstraintsNode` and `ScreenSpacePivotNode` used to be `RequiresPCM` in V1.4's first pass because they resolved viewport size through `PCM->GetOwningPlayerController()->GetViewportSize()`. V1.4 extracted that resolution into `UE::ComposableCameras::TryGetEffectiveViewportSize` (PCM �?`GEngine->GameViewport` �?1920×1080 fallback), which lets those nodes run in LS with pixel-exact accuracy in PIE / packaged builds and a 16:9 fallback during editor-world Spawnable preview.

### Built-in Compute Nodes

Compute nodes run once at camera activation (from `BeginPlayCamera`) and publish results that downstream camera nodes consume every frame. They live on the BeginPlay exec chain.

| Node | Purpose |
|---|---|
| `ComputeDistanceToActorNode` | Measures the distance and direction between two actors at activation time. Use to scale boom arm length, set initial FOV, or derive blend weights from actor proximity. Inputs: ActorA, ActorB (Actor). Outputs: Distance (Float), Direction (Vector3D). |

### Typical Node Composition (Third-Person Camera)

```
1. ReceivePivotActorNode     �?writes PivotPosition from character
2. PivotOffsetNode           �?offsets pivot upward (shoulder height)
3. CameraOffsetNode          �?offsets camera behind and to the side
4. ControlRotateNode         �?reads player input for orbit rotation
5. CollisionPushNode         �?pushes camera forward on wall collision
6. LookAtNode (soft)         �?soft look-at toward pivot
7. FieldOfViewNode           �?sets FOV with optional dynamic zoom
```

### Subobject Property Pin Exposure

Nodes that own **Instanced UObject subobjects** (e.g. `CollisionPushNode` owns a `PushInterpolator` and `PullInterpolator` of type `UComposableCameraInterpolatorBase*`) sometimes need to let callers override the subobject's individual parameters at runtime �?for example, changing the interpolation speed or spring stiffness based on game state. Exposing the entire subobject as a pin doesn't work: Instanced objects carry runtime state, lifecycle ownership, and don't fit the data-flow model that pins are designed for (see §12 Interpolators for the full rationale).

The solution is **subobject property pin exposure**: the base class automatically enumerates individual properties of every `Instanced` UObject UPROPERTY as first-class pins. These pins participate in the normal resolution chain (wire �?exposed parameter �?per-instance override �?UPROPERTY fallback) and the resolved values are written back into the subobject's UPROPERTYs before the node's first tick.

#### Pin Naming Convention

Subobject pins use **dot-separated compound names**: `SubobjectPropertyName.FieldName`.

Examples for `CollisionPushNode`:
- `PushInterpolator.Speed` (Float) �?from `UComposableCameraIIRInterpolator::Speed`
- `PushInterpolator.UseFixedStep` (Bool) �?from `UComposableCameraIIRInterpolator::bUseFixedStep`
- `PullInterpolator.DampTime` (Float) �?from `UComposableCameraSimpleSpringInterpolator::DampTime`

The dot separator is legal in `FName` and visually conveys hierarchy. The prefix avoids collisions when two subobjects expose a property of the same name (e.g. both `PushInterpolator.Speed` and `PullInterpolator.Speed` coexist without ambiguity).

#### Auto-Discovery via GatherAllPinDeclarations

All callers (editor graph nodes, type-asset builder, runtime data-block allocator) use the non-virtual `GatherAllPinDeclarations()` instead of `GetPinDeclarations()`. This method:

1. Calls the virtual `GetPinDeclarations()` chain to collect subclass-declared pins (PivotActor, PivotPosition, etc.).
2. Auto-iterates every `UPROPERTY` on the node class that has the `Instanced` flag (`CPF_InstancedReference` + `CPF_Edit`).
3. For each such property, resolves the subobject pointer and calls `DeclareSubobjectPins()` to append child-property pins.

Node subclasses do **not** need to call `DeclareSubobjectPins` manually �?it happens automatically for every Instanced property. The only cases where manual calls are needed are unusual subobject relationships that reflection cannot discover (e.g. subobjects stored in TArray containers).

Under the hood, `DeclareSubobjectPins` does:
1. Iterates the subobject's `EditAnywhere` UPROPERTYs via `TFieldIterator<FProperty>`
2. Maps each property type to `EComposableCameraPinType` via `TryMapPropertyToPinType`. Properties whose type has no pin mapping are silently skipped.
3. Generates a pin declaration with:
   - `PinName` = `SubobjectPropertyName.FieldPropertyName` (e.g. `PushInterpolator.Speed`)
   - `DisplayName` = `SubobjectDisplayName > FieldDisplayName` (e.g. `Push Interpolator > Speed`)
   - `Direction` = Input
   - `PinType` = mapped type
   - `DefaultValueString` = the subobject property's current value serialized to string
4. Properties tagged with `meta=(NoPinExposure)` on the subobject class are skipped, giving subobject authors an opt-out for properties that should never be externally driven.

Generic `UScriptStruct` properties can map to Struct pins. Bytewise-safe structs (engine math whitelist plus explicit `STRUCT_IsPlainOldData`) use byte storage; all other user structs use owned `FInstancedStruct` slots so constructors, destructors, copies, and embedded UObject references stay visible to the runtime data-block lifetime / GC path. Reflected-property heuristics are deliberately not used, because non-UPROPERTY native members would be invisible and could make an unsafe struct look bytewise-safe.

**Dynamic pin set:** when the user changes the Instanced subobject's class in the editor (e.g. switches `PushInterpolator` from `IIRInterpolator` to `SpringDamperInterpolator`), the available sub-properties change. The graph node must call `ReconstructPins()` in response, which re-queries `GatherAllPinDeclarations` and rebuilds the pin array. `ReconstructPins` uses `MovePersistentDataFromOldPin` to carry wired connections onto name-matched new pins; pins that no longer exist (e.g. `PushInterpolator.Speed` after switching to a class without `Speed`) lose their wires silently �?the same behavior as any other pin removal in the UE graph system. Old per-instance overrides for pins that no longer exist are pruned during `SyncPhase_RebuildNodePinOverrides`.

#### Auto-Apply at Runtime

`Initialize()` (the non-virtual wrapper on `UComposableCameraCameraNodeBase`) calls `AutoApplySubobjectPinValues()` before dispatching to `OnInitialize()`. This auto-iterates every Instanced property and writes resolved pin values into the subobject's UPROPERTYs. By the time the subclass's `OnInitialize_Implementation` runs, the subobject properties already reflect any wired or exposed overrides �?the subclass can immediately build typed instances (e.g. `BuildDoubleInterpolator()`) from the up-to-date config.

Under the hood, `ApplySubobjectPinValues` (called for each Instanced property):
1. Iterates the subobject's `EditAnywhere` UPROPERTYs
2. For each property that has a pin mapping, calls `TryResolveInputPin` with the compound name
3. If resolved, writes the value into the subobject's UPROPERTY via typed pointer write
4. If not resolved, the UPROPERTY retains its authored value (the Instanced editor default)

#### Editor: Details Panel Integration

The Details panel customization (`FComposableCameraNodeGraphNodeDetails`) handles Instanced subobject properties automatically:

1. Detects subobject pin prefixes by scanning `InputPinNameToIndex` for compound pin names containing dots.
2. For each Instanced property whose prefix matches:
   - Adds the parent property row via `AddExternalObjectProperty` with `CustomWidget(false)` to suppress native child expansion �?only the class picker is visible.
   - Adds each of the subobject's `EditAnywhere` child properties as separate external-object rows below the parent.
   - For children whose compound pin name exists in `InputPinNameToIndex`, the row is customized with inline "As Pin" checkbox and "[Exposed]" chip, matching the same layout as top-level pin-matched properties.
3. **Subobject class change �?pin reconstruction:** the `ForceRefreshDetails` callback on the parent property handle triggers when the user changes the class picker; the detail panel rebuilds, re-queries `GatherAllPinDeclarations`, and the new child properties appear with correct pin matching.

#### Invariants

- **Naming uniqueness.** The compound name `SubobjectPropertyName.FieldName` must be unique across all pins on the node. Since subobject UPROPERTY names are unique within a UClass and the prefix disambiguates across subobjects, this is guaranteed by construction.
- **Null subobject safety.** `DeclareSubobjectPins` and `ApplySubobjectPinValues` silently return if the subobject pointer is null (the user hasn't assigned an interpolator class yet). This means no pins are declared for null subobjects, which is correct.
- **No hot-path allocations.** `ApplySubobjectPinValues` follows the same constraint as the rest of the evaluation pipeline: no `new`, no `TArray` reallocation, no `FString` formatting. Property iteration is pointer arithmetic on the `UClass` metadata; `TryResolveInputPin` is a map lookup; value writes are typed memcpy.

---

## 8. Transitions

### Base Architecture

All transitions derive from `UComposableCameraTransitionBase`. They are pose-only operators �?they never hold references to cameras or Directors.

**Lifecycle**:
1. `TransitionEnabled(InitParams)`: Called once when the transition is first wired into the tree. Receives `CurrentSourcePose`, `PreviousSourcePose`, and `DeltaTime` at the moment of creation.
2. First `Evaluate()` frame: `OnBeginPlay()` fires before `OnEvaluate()`. This is where transitions set up internal state (spline control points, inertialization polynomials, etc.) using both the InitParams and the live source/target poses.
3. Each subsequent frame: `RemainingTime` is decremented, `Percentage` is updated, `OnEvaluate()` computes the blended pose from the live source and target poses.
4. When `RemainingTime <= 0`: `TransitionFinished()` is called, which sets `bFinished = true`, fires `OnFinished()`, and broadcasts `OnTransitionFinishesDelegate`.

### InitParams and Velocity

`FComposableCameraTransitionInitParams` captures the source state at the moment of transition creation:
- `CurrentSourcePose`: The blended output the player was seeing (from `Director::LastEvaluatedPose`)
- `PreviousSourcePose`: The previous frame's blended output (from `Director::PreviousEvaluatedPose`)
- `DeltaTime`: Frame delta time

This data is critical for velocity-based transitions (inertialization) which need `(Current - Previous) / DeltaTime` to compute initial velocity.

### Transition Resolution

When switching from camera A (built from type asset `SourceTypeAsset`) to camera B (built from `TargetTypeAsset`), the transition is resolved through a five-tier priority chain implemented in `AComposableCameraPlayerCameraManager::ResolveTransition`:

1. **Caller-supplied override** �?the `TransitionOverride` parameter passed to `ActivateNewCameraFromTypeAsset`, `PopCameraContext`, or `TerminateCurrentCameraContext`. Always wins when non-null.
2. **Transition table lookup** �?the project-level `UComposableCameraTransitionTableDataAsset` referenced from `UComposableCameraProjectSettings::TransitionTable`. Performs **exact-match only** on (Source, Target) pairs �?no wildcards. First matching entry in declaration order wins.
3. **Source's ExitTransition** �?`SourceTypeAsset->ExitTransition`. The source camera declares how to leave. Useful for cameras that always exit with a specific transition regardless of target (puzzle cameras, UI overlays, cinematic cameras).
4. **Target's EnterTransition** �?`TargetTypeAsset->EnterTransition`. The target camera declares how to enter. This is the existing per-type-asset default.
5. **Hard cut** �?no transition; the new camera appears instantly.

The table (tier 2) intentionally does not support wildcards. Per-camera ExitTransition and EnterTransition (tiers 3 and 4) serve as the per-camera fallbacks, covering the "always leave/enter this camera a certain way" use cases that wildcards would otherwise handle �?but without the priority conflict that a global wildcard would create by shadowing all per-camera transitions.

Both the in-context activation path (`ActivateNewCameraFromTypeAsset`) and the inter-context pop path (`PopActiveContextInternal`) use the same resolution chain. The `ResolveTransition` helper returns a raw `UComposableCameraTransitionBase*` (owned by the type asset or table entry); callers `DuplicateObject` before mutating.

**`UComposableCameraTransitionTableDataAsset`** is a data asset holding a `TArray<FComposableCameraTransitionTableEntry>`. Each entry has: `SourceTypeAsset` (soft ptr, required), `TargetTypeAsset` (soft ptr, required), and an instanced `Transition`. The asset is referenced from `UComposableCameraProjectSettings::TransitionTable` and is intended as a project-wide routing table for explicit camera-pair transitions. The lookup is O(N) over the entries array; for typical table sizes (tens of entries) this is negligible since it only runs on camera switches, not per-frame. The asset validates entries via `IsDataValid()`: null Source or Target emits an Error (entry is ignored at runtime), null Transition emits a Warning (falls through to lower priority tiers). `UpdateDisplayTitle()` renders inline warnings in the Details panel for each invalid entry.

**`UComposableCameraTransitionDataAsset`** is a thin wrapper around a single instanced `UComposableCameraTransitionBase*`. It exists so transitions can be referenced as standalone data assets (e.g., by the transition table, or as `TransitionOverride` on a DataTable row) without inlining the transition into the referencing object. Registered in the Content Browser under the "Composable Camera System" category with a warm orange color and a custom SVG thumbnail.

**`UComposableCameraNodeModifierDataAsset`** wraps modifier instances for data-driven management. Also registered in the Content Browser under "Composable Camera System" with a purple color and custom SVG thumbnail. See §9 (Modifiers) for runtime details.

### Built-in Transitions

| Transition | Method |
|---|---|
| `LinearTransition` | Linear interpolation (lerp) |
| `CubicTransition` | Cubic easing (smooth start/end) |
| `EaseTransition` | EaseInOut with configurable exponent |
| `SmoothTransition` | Hermite smooth step: `t²(3-2t)` or smoother step: `t³(t(6t-15)+10)` |
| `CylindricalTransition` | Arc around a pivot derived from ray intersection |
| `InertializedTransition` | Physics-based inertialization using 5th-order polynomials for position and rotation. Supports auto-computed transition time from max acceleration. Optional additive curve for shape control. |
| `SplineTransition` | Camera follows a computed spline (Hermite, Bezier, Catmull-Rom, or Arc) with configurable evaluation curves |
| `PathGuidedTransition` | Three-phase transition: Enter (inertialized blend onto spline) �?Spline (follow a rail actor) �?Exit (inertialized blend to target). Uses an intermediate camera on the rail. |
| `DynamicDeocclusionTransition` | Wraps a driving transition, dynamically adjusting blend weight based on ray-trace visibility |

### Inertialized Transition (Deep Dive)

The inertialized transition computes 5th-order polynomial blends for position (3 axes independently or combined) and rotation (yaw, pitch, roll independently). The polynomial `P(t)` is constructed so that:
- `P(0) = offset` (initial position/rotation error)
- `P'(0) = velocity` (initial velocity from source camera motion)
- `P(T) = 0`, `P'(T) = 0`, `P''(T) = 0` (smooth arrival at target with zero velocity and acceleration)

This produces a physically plausible blend that respects the source camera's momentum.

### PathGuidedTransition Architecture

A three-phase approach for cinematic path transitions:

1. **Enter Phase**: An `InertializedTransition` blends from the source camera's live pose to the spline's start point. Created in `OnBeginPlay` using `InitParams` for velocity data.
2. **Spline Phase**: The intermediate camera follows the rail spline, driven by a `DrivingTransition` and optional `SplineMoveCurve`.
3. **Exit Phase**: An `InertializedTransition` blends from the spline's end point to the target camera. Created lazily at the moment the spline phase ends, using `IntermediateCamera->CameraPose` and `IntermediateCamera->LastFrameCameraPose` for velocity data.

`BuildInternalSpline` duplicates the rail's spline component and prepends/appends control points to connect the source and target positions with C1-continuous tangents.

### Lifecycle and failure modes

The transition spawns world Actors (`IntermediateCamera` for `Inertialized`, `DebugSplineActor` for `Auto`), which means input validation and cleanup are both load-bearing.

- **Input validation runs before any spawn.** `ResolveAndValidateRail` in `OnBeginPlay` sync-loads the `RailActor` soft pointer and rejects null / unloaded rail, missing `RailSplineComponent`, or zero-point splines. On failure, no actors are spawned; the transition leaves `Rail = nullptr` and `OnEvaluate`'s nullcheck hard-cuts to the target pose.
- **Spawned-actor cleanup has two paths.** The normal completion path runs through an `OnTransitionFinishesDelegate` lambda registered once in `OnBeginPlay` immediately after spawn �?this fires via `TransitionFinished`'s broadcast regardless of which sub-transitions (`EnterTransition` / lazy `ExitTransition`) were actually constructed. The interrupted path (camera destroyed mid-blend, eval tree pruned, transition replaced) is caught by an overridden `BeginDestroy`. Both paths invoke the same `DestroySpawnedActors` helper, which is `IsValid()`-guarded so double-fire is a no-op.

This pattern (validate first, register cleanup once at spawn, override `BeginDestroy` as backstop, keep the cleanup helper idempotent) is the generic rule for any future transition that spawns world objects. See TechDoc §7.2 for the rationale.

---

## 9. Modifiers

### Purpose

Modifiers provide a way to dynamically alter node parameters at runtime without creating new camera subclasses. A modifier targets a specific node class and overrides its parameters before the node ticks.

### Architecture

```
UComposableCameraModifierBase (abstract, Blueprintable)
  - NodeClass: TSubclassOf<UComposableCameraCameraNodeBase>
  - ApplyModifier(): Blueprint-implementable

UComposableCameraNodeModifierDataAsset
  - Wraps modifier instances for data-driven management

UComposableCameraModifierManager
  - AddModifier() / RemoveModifier()
  - Tracks all active modifiers
  - Computes effective modifiers (highest priority per node class)
  - On change: may trigger camera reactivation with transition
  - Owns the GC root for all stored modifiers + assets via
    static AddReferencedObjects override (the entry struct is a plain
    C++ type living inside non-reflected nested TMaps, so reflection
    alone does not reach the pointers).
```

### Priority System

When multiple modifiers target the same node class, only the highest-priority one is active. When the effective modifier set changes (add/remove), `OnModifierChanged()` on the PCM checks if the running camera is affected and reactivates it with an appropriate transition.

### Reactivation and Type-Asset Cameras

Camera reactivation spawns a fresh camera of the same class and fires the same `OnPreBeginplayEvent` callback that was used during the original activation. For type-asset cameras, this callback is `OnTypeAssetCameraConstructed`, which reads `PendingTypeAsset` and `PendingParameterBlock` from the PCM to reconstruct the camera (duplicate nodes, build data block, wire exec chains).

To support this, each type-asset camera stores its source on construction:

- `SourceTypeAsset` (`TObjectPtr<UComposableCameraTypeAsset>`, strong): the type asset that built this camera. Strong by design �?see §5 GC hazard discussion: weak would let GC reclaim a transiently-loaded source asset between activation and reactivate, producing an empty-shell rebuild.
- `SourceParameterBlock` (`FComposableCameraParameterBlock`): the caller-provided parameters applied at activation.

`PCM::ReactivateCurrentCamera` restores these into `PendingTypeAsset` / `PendingParameterBlock` before calling through to the Director, so `OnTypeAssetCameraConstructed` sees valid state and fully reconstructs the new camera. The new camera is stamped with the same source info, so subsequent reactivations also work.

The same restore-and-callback pattern is used during context pop. `ContextStack::PopActiveContextInternal` calls `PCM::PrepareResumeCallback(ResumingCamera)`, which checks `SourceTypeAsset` on the resuming camera, restores the pending state if present, and returns a callback bound to `OnTypeAssetCameraConstructed`. For non-type-asset cameras it returns an empty delegate. This ensures the new camera spawned by `Director::ActivateNewCameraWithReferenceSource` during context pop is fully reconstructed from the type asset, not left as an empty shell.

---

## 10. Actions

Camera actions are persistent or camera-scoped behaviors that run around camera evaluation. Each action declares an `ExecutionType` that picks one of four hook points:

| `ExecutionType` | Fires | Dispatch |
|---|---|---|
| `PreCameraTick` | Once per frame, before any node ticks | Bound to `Camera->OnActionPreTick` multicast delegate |
| `PostCameraTick` | Once per frame, after all nodes tick | Bound to `Camera->OnActionPostTick` multicast delegate |
| `PreNodeTick` | Immediately before a matching node's `TickNode` | Iterated by TickCamera against `PreNodeTickActions`, filtered by `TargetNodeClass` |
| `PostNodeTick` | Immediately after a matching node's `TickNode` | Iterated by TickCamera against `PostNodeTickActions`, filtered by `TargetNodeClass` |

Node-scoped actions take an additional `TargetNodeClass: TSubclassOf<UComposableCameraCameraNodeBase>` and fire once per matching node instance on the camera (exact-class match, same rule as modifiers �?§9). If `TargetNodeClass` is unset or no matching node exists, the action is silently ignored. Mutations to the pose during a PreNodeTick action feed into the upcoming `TickNode` input; mutations during a PostNodeTick action feed into the next node's input.

```
UComposableCameraActionBase (abstract)
  ├─ UComposableCameraMoveToAction    �?smooth move to target position
  ├─ UComposableCameraRotateToAction  �?smooth rotate to target
  └─ UComposableCameraResetPitchAction �?reset pitch to zero
```

Actions are managed on the PCM. Camera-scoped actions (`bOnlyForCurrentCamera`) are automatically expired when the camera transitions away. Persistent actions survive across camera switches �?`AComposableCameraPlayerCameraManager::BindCameraActionsForNewCamera` re-binds persistent actions onto each newly activated camera (whole-camera actions via delegates, node-scoped actions via `Camera->RegisterNodeAction`).

Expiration for all four execution types is driven centrally by `PCM::UpdateActions`, which runs once per frame before `ContextStack->Evaluate` and removes any action whose `OnCanExecute` returned false. Node-scoped actions do not require any per-node expiration bookkeeping.

---

## 11. Camera Patches

Patches are time-bounded, additively-composable overlays. They sit at the Director level (parallel to the EvaluationTree) and let designers apply effects like a 2-second DollyZoom or a 4-second pivot drift on top of whatever camera is currently active, without authoring a new CameraTypeAsset and without going through the Transition machinery.

### Conceptual model

A Patch is a small CameraBase actor whose node graph reads the upstream pose and writes a modified pose. Each Patch has:

- A **layer index** for ordering when multiple patches stack.
- An **envelope** (Entering �?Active �?Exiting �?Expired) that drives a per-patch alpha in [0, 1] over time, shaped by an ease enum (Linear / EaseIn / EaseOut / EaseInOut / Smooth).
- A **schedule** �?bitmask of expiration channels (Duration / Manual / Condition) plus an `bExpireOnCameraChange` flag �?that decides when the patch leaves Active.
- A live **evaluator actor** spawned from the Patch's TypeAsset (a subclass of `UComposableCameraTypeAsset`).

The Director's per-frame pipeline is updated to:

```
Director::Evaluate(DeltaTime)
  pose  = EvaluationTree.Evaluate(DeltaTime)        �?unchanged
  pose  = PatchManager.Apply(DeltaTime, pose)       �?NEW
  LastEvaluatedPose = pose
```

`PatchManager.Apply` walks `ActivePatches` (sorted by `LayerIndex` ascending, push-order tiebreaker) and for each:

1. **AdvanceEnvelope** �?phase machine updates `CurrentAlpha`.
2. **CheckSchedule** �?Active-phase only; checks Duration / Condition / OnCameraChange channels. A triggering channel calls a shared `TransitionPatchToExiting` helper that snapshots `CurrentAlpha` into `ExitStartAlpha` so a half-faded-in patch fades out from where it was, not from 1.
3. If not Expired, the evaluator runs `TickWithInputPose(DeltaTime, RunningResult)` to produce its evaluated pose; `RunningResult.BlendBy(Evaluated, CurrentAlpha)` chains it.
4. **End-of-pass sweep** �?reverse-iter compaction destroys evaluators of Expired patches and removes them from `ActivePatches`.

Each Patch's evaluator is its own `AComposableCameraCameraBase`, constructed via the LS-compatible `Initialize(nullptr)` + `UE::ComposableCameras::ConstructCameraFromTypeAsset` spine. Patches don't participate in PCM-level Action dispatch and aren't visible to the EvaluationTree.

### Why Director-level (not Camera-level, not Tree-level)

Director-scoping survives `RunningCamera` changes. The motivating scenario:

> Add a 2 s DollyZoom Patch at T=0s. Activate AimCamera (1 s blend) at T=0.5s. The DollyZoom keeps applying through the blend and onto AimCamera's pose, expiring naturally at T=2.0s.

Camera-level scoping would tie the patch to FollowCamera's actor lifetime �?the patch would die when `CollapseFinishedTransitions` destroys FollowCamera at T=1.5s. Tree-level scoping (a new TVariant alternative) is the cleaner long-term home but requires reworking the binary tree's "left=source, right=target" semantics and the collapse pass; deferred to V2 (see "Cross-context behavior" below).

### Relationship to Modifier and Action

Orthogonal. Patches do NOT replace either:

- **Modifier** (§9) edits parameters of existing nodes on the running camera and triggers reactivation. No envelope, no duration. Use for "boss arena makes default FOV wider".
- **Action** (§10) hooks a function around camera/node tick. No node graph, no per-frame evaluator. Use for "fire a callback when this node ticks".
- **Patch** adds new logic on top of the running camera with its own envelope and lifetime. Use for "do a DollyZoom for the next 2 s".

A Patch *can* express what a Modifier can (a Patch with a single `+10 FOV` node �?an FOV Modifier of `+10`), but the lifecycle semantics differ and both tools survive in V1.

### Cross-context behavior (V1 limitation)

A patch active on the gameplay context is NOT preserved across an inter-context blend into a cutscene context. The cutscene Director's RefLeaf walks the gameplay tree's snapshot directly without calling `gameplay.Director.Evaluate()`, so `gameplay.PatchManager.Apply` is skipped during the blend window. Documented and intentionally accepted in V1; the V2 fix is to promote the Patch overlay into the tree as a new wrapper variant. See `PatchSystemProposal.md` §10 for the V2 sketch.

### Architecture additions

```
Director (per context)
  ├─ EvaluationTree                 �?unchanged
  ├─ RunningCamera, LastEvaluatedPose, PreviousEvaluatedPose
  └─ PatchManager                   �?NEW
       └─ ActivePatches : TArray<UComposableCameraPatchInstance*>
            ├─ Evaluator           �?AComposableCameraCameraBase actor
            ├─ Phase / CurrentAlpha / ExitStartAlpha
            ├─ Schedule (ExpirationType bitmask, Duration, bExpireOnCameraChange)
            ├─ RunningCameraAtAdd  �?per-patch baseline for OnCameraChange
            └─ Handle              �?caller-facing weak wrapper
```

The Director destroys its PatchManager via the standard UPROPERTY GC chain. Synchronous teardown on context pop / Director shutdown goes through `Director::DestroyAllCameras �?PatchManager.DestroyAll`, which calls `Evaluator->Destroy()` on each Patch's actor.

### Public API surface (V1)

- `UComposableCameraBlueprintLibrary::AddCameraPatch(WC, PlayerIndex, PatchAsset, ContextName, Params, ParameterBlock) �?UComposableCameraPatchHandle*` �?`BlueprintInternalUseOnly`. Surface mirrors `ActivateComposableCameraFromTypeAsset` (PlayerIndex resolves PCM internally; parameter block is separate). `ContextName == NAME_None` targets the active context (typical case); a non-None name targets a specific context that must already be on the stack �?patches on a buried context still tick (their Director's `Evaluate` runs as long as the context lives) but are not user-visible until the context returns to the top. BP authors interact through `UK2Node_AddCameraPatch` (EditorDesignDoc §22). The runtime `PatchManager::AddPatch` takes the parameter block as a separate argument too.
- `UComposableCameraBlueprintLibrary::ExpireCameraPatch(Handle, ExitDurationOverride = -1)` �?flip a single patch to Exiting via its envelope.
- `UComposableCameraBlueprintLibrary::ExpireAllPatchesOnContext(WC, PlayerIndex, ContextName, ExitDurationOverride = -1)` �?bulk soft-expire every active patch on the named (or active) context's PatchManager. Each patch runs its own exit ramp; mid-Entering patches fade out from their current alpha rather than popping. Backed by `UComposableCameraPatchManager::ExpireAll`.
- BP-pure handle introspection (weak-handle-safe �?null/stale handle �?defaulted return, no need to null-check at every call site): `IsPatchActive(Handle) �?bool`, `GetPatchPhase(Handle) �?EComposableCameraPatchPhase`, `GetPatchAlpha(Handle) �?float`, `GetPatchElapsedTime(Handle) �?float`. Match the §12.1 surface of `PatchSystemProposal.md`.
- `UComposableCameraPatchManager::ApplyParameterBlockToActivePatch(Handle, ParameterBlock)` �?mid-life parameter mutation. Re-applies a parameter block onto the live evaluator's runtime data block via the source asset's `ApplyParameterBlock` (same path the LS Component uses on its per-tick re-sync). Drives the Sequencer track's per-frame `OnAnimate` re-sync; safe to call every frame on the same handle. No-op on null / stale handle, on Patches in Exiting / Expired phase, or when the evaluator has no runtime data block yet. The single-key `SetPatchParameter(handle, name, value)` analog is still deferred �?Sequencer is the only current driver for keyed patch parameters and it produces complete parameter blocks naturally.

### Sequencer integration

Patches are authored in Sequencer through a dedicated track triple living under `Source/ComposableCameraSystem/{Public,Private}/MovieScene/`:

```
UMovieSceneComposableCameraPatchTrack       �?root-level track (no object binding)
 └── UMovieSceneComposableCameraPatchSection : UMovieSceneParameterSection
      ├── PatchAsset             : UComposableCameraPatchTypeAsset*
      ├── TargetActorBinding     : FMovieSceneObjectBindingID  �?bound LS Actor (sole addressing)
      ├── Params                 : FComposableCameraPatchActivateParams (envelope / layer)
      ├── Parameters             : FInstancedPropertyBag  �?from PatchAsset.ExposedParameters (static defaults / fallback)
      ├── Variables              : FInstancedPropertyBag  �?from PatchAsset.ExposedVariables (static defaults / fallback)
      ├── ScalarParameterNamesAndCurves   �?
      ├── BoolParameterNamesAndCurves     �?inherited from UMovieSceneParameterSection;
      ├── Vector2DParameterNamesAndCurves �?each entry = a named keyable channel that
      ├── VectorParameterNamesAndCurves   �?takes priority over the bag fallback for
      └── ColorParameterNamesAndCurves    �?the matching ExposedParameter name

Single per-frame path (runs in all worlds �?Editor / EditorPreview / PIE / Game):

  TrackInstance::OnAnimate(per frame):
    for each in-range section:
      LSComp = section.TargetActorBinding �?LS Actor �?LS Component
      if LSComp:
        params = section.BuildParameterBlock(currentFrame)             �?channel sample
        alpha  = PatchEnvelope::ComputeStatelessAlpha(currentFrame, �? �?pure function
        LSComp->SetSequencerPatchOverlay(section, params, alpha)

  LS Component::TickComponent (after InternalCamera tick, before projection):
    for each registered overlay (sorted by section's LayerIndex):
      apply parameter block to overlay.Evaluator's runtime data block
      eval = Evaluator->TickWithInputPose(dt, Pose)
      Pose.BlendBy(eval, overlay.Alpha)
    write Pose.Position + Pose.Rotation + Pose.FOV �?CineCamera        �?FOV here so DollyZoom previews

Sequencer patches follow their host LS Component lifetime: they apply while the spawned LS Actor exists and ticks. PCM/Director-side patches
(BP `AddCameraPatch`) are a separate orthogonal path on the gameplay camera.
(BP `AddCameraPatch`) are a separate orthogonal path on the gameplay camera.

UMovieSceneComposableCameraPatchTrackInstance �?engine UMovieSceneTrackInstanceSystem dispatch:
  OnInputAdded   �?resolve PCM + PatchManager + build initial parameter block �?AddCameraPatch
  OnAnimate      �?per-input re-sync: rebuild block from current bag values �?ApplyParameterBlockToActivePatch
  OnInputRemoved �?ExpireCameraPatch (section ease-out as exit-duration override)
  OnDestroyed    �?ExpirePatch(handle, 0.f) on every still-live entry �?defensive teardown
```

Section easing folds into the patch envelope: when the user has *not* overridden `EnterDuration` / `ExitDuration` on `Params`, the section's `Easing.GetEaseInDuration` / `GetEaseOutDuration` (converted to seconds via the owning movie scene's `TickResolution`) is forwarded to the runtime via `bOverrideEnterDuration` / `bOverrideExitDuration = true`. Dragging the section's ease handles in Sequencer thus directly reshapes the live patch envelope. Already-overridden values are left alone �?the designer can pin a specific enter/exit time without losing it to whatever ease was authored.

Root-track binding (no per-actor `ObjectBinding`) keeps the track addressable through the same `(PlayerIndex, ContextName)` pair that BP `AddCameraPatch` uses, so the Sequencer surface is a 1:1 mapping of the runtime call. Patches still target the Director resolved through the active context (or an explicitly named one); the section is the authoring artifact, the Director is the runtime owner.

Editor-side: `FComposableCameraPatchTrackEditor` registers the track type with Sequencer's track-editor system, surfaces "Add Track �?Composable Camera Patch Track" in the root menu, paints sections by their patch asset name, and exposes per-section bag-leaf keying via `BuildSectionContextMenu` �?right-click a section �?"Camera Parameters" / "Camera Variables" lists every keyable leaf, click materialises a stock `UMovieSceneFloatTrack` / `UMovieSceneObjectPropertyTrack` / etc. on the path `Parameters.Value.{LeafName}` (or `Variables.Value.{LeafName}`). Sequencer's stock evaluation animates the bag's backing FProperty in place each frame; the runtime `OnAnimate` picks up the animated values and pushes them through `ApplyParameterBlockToActivePatch` without any custom channel implementation.

Pin-type �?bag-descriptor mapping is shared between this section and `FComposableCameraTypeAssetReference` via the new `LevelSequence/ComposableCameraExposedBagUtils.{h,cpp}` helpers �?a single canonical "exposed parameter �?CPF_Edit | CPF_Interp bag entry" pipeline so any future pin-type addition flows through both surfaces uniformly.

No gate path is needed here: patches are not Spawnables and do not drive viewport lifetime. They attach to the already-spawned LS Component and follow that component's normal tick.

Full design rationale (asset-class subclassing decision, envelope as enum vs. instanced Transition, layer override mechanism, V2 paths) lives in `PatchSystemProposal.md`.

---

## 12. Interpolators

Interpolators provide smooth value blending used by nodes for damping, spring physics, and smooth transitions.

```
UComposableCameraInterpolatorBase (abstract)
  ├─ UComposableCameraIIRInterpolator           �?exponential damping (IIR filter)
  ├─ UComposableCameraSimpleSpringInterpolator   �?simple spring physics
  └─ UComposableCameraSpringDamperInterpolator   �?second-order spring-damper system
```

Each interpolator can build typed instances: `TCameraInterpolator<Double>`, `TCameraInterpolator<Vector2D>`, `TCameraInterpolator<Vector3D>`, `TCameraInterpolator<Quat>`, `TCameraInterpolator<Rotator>`.

Interpolators are Instanced UObject subobjects owned by their parent node. Their individual parameters (e.g. `Speed`, `DampTime`, `Frequency`) are **automatically exposed as pins** by `GatherAllPinDeclarations()` �?see §7 "Subobject Property Pin Exposure". No per-node boilerplate is needed; any node that declares an `Instanced` interpolator property gets subobject pins for free. This allows callers to override interpolation behavior at runtime without replacing the entire interpolator object �?the interpolator class (IIR vs. Spring vs. SpringDamper) remains a design-time choice, while its tuning parameters flow through the standard pin resolution chain.

---

## 13. Spline Math

The plugin implements multiple spline types for use in SplineNode and SplineTransition:

| Spline | Implementation | Use Case |
|---|---|---|
| `BuiltInSpline` | Wrapper for UE's USplineComponent (cubic Bezier) | CameraRig_Rail integration |
| `BezierSpline` | Custom cubic Bezier | SplineTransition Bezier mode |
| `CubicHermiteSpline` | Hermite interpolation with explicit tangents | SplineTransition Hermite mode, SplineNode |
| `BasicSpline` | B-spline | SplineNode |
| `NURBSpline` | Non-uniform rational B-spline | SplineNode |

All implement `IComposableCameraSplineInterface` for uniform evaluation.

---

## 14. Blueprint Integration

### Blueprint Function Library

`UComposableCameraBlueprintLibrary` provides the primary Blueprint API:

- `ActivateComposableCameraFromTypeAsset(...)`: Internal entry point for type-asset-based camera activation (called by the custom K2 node, not exposed in the BP palette)
- `ActivateComposableCameraFromDataTable(WorldContextObject, PlayerIndex, DataTable, RowName)`: Internal entry point for DataTable-based camera activation (called by the custom K2 node, not exposed in the BP palette). All activation params (pose preservation, transient flags, lifetime) come from the row's `FComposableCameraActivateParams ActivationParams` field �?callers do not supply them separately
- `TerminateCurrentCamera(...)`: Pops the active context
- `PopCameraContext(...)`: Pops a named context
- `AddModifier(...)` / `RemoveModifier(...)`: Runtime modifier management
- `AddAction(...)` / `ExpireAction(...)`: Runtime action management
- `SetComposableCameraVariableRuntimeValue(...)` / `GetComposableCameraVariableRuntimeValue(...)`: Type-safe variable access via custom thunks

Note: The legacy class-based activation functions (`ActivateComposableCameraByClass`, `CreateComposableCameraByClass`) have been removed. All camera activation now goes through type assets or DataTable rows. The internal `PCM::CreateNewCamera` and `PCM::ActivateNewCamera` functions remain for use by internal subsystems (e.g., the MixingCameraNode).

### Context Name Dropdown

Context names configured in project settings are exposed to Blueprint via `GetContextNames` metadata on `FName` parameters, providing a dropdown selector in the editor.

---

## 15. Initialization and Timing

### Boot Sequence

1. **PCM Constructor**: Creates ContextStack and ModifierManager as default subobjects
2. **PCM::InitializeFor** (PostInitializeComponents phase): Reads project settings and calls `ContextStack->EnsureContext()` for the base context. This runs before any actor's BeginPlay.
3. **PCM::BeginPlay**: Currently empty �?all critical init is in InitializeFor to avoid BeginPlay ordering issues between the PCM and level actors.
4. **Gameplay code activates cameras via K2 node / DataTable**: Can safely happen in any actor's BeginPlay because the base context is already initialized.

### Why InitializeFor, Not BeginPlay

UE does not guarantee BeginPlay order across actors. If a level actor's BeginPlay fires before the PCM's BeginPlay, camera activation would find an empty context stack and crash. Moving base context initialization to `InitializeFor` (which runs during `PostInitializeComponents`, guaranteed before any BeginPlay) eliminates this race condition.

---

## 16. Project Settings

`UComposableCameraProjectSettings` (accessible via Project Settings > Composable Camera System):

- `ContextNames` (TArray<FName>): Registered context names. The first entry is the base context, auto-initialized on startup. At least one context must be defined. All context operations validate names against this list.

---

## 17. Level Sequence Integration and Implicit Camera Activation

### Architecture: Implicit Camera Activation via SetViewTarget

CCS supports two camera activation paths:

1. **Explicit activation** �?`ActivateCamera` / `ActivateNewCameraFromTypeAsset`. The user explicitly specifies which camera, context, and transition.
2. **Implicit activation** �?The PCM's `SetViewTarget` override. When external code calls `SetViewTarget` on an actor with a `UCameraComponent`, the PCM automatically creates a transient proxy camera wrapping that actor and activates it in the current context's director. If `FViewTargetTransitionParams.BlendTime > 0`, the blend params are converted into a `UComposableCameraViewTargetTransition` (which delegates to `FViewTargetTransitionParams::GetBlendAlpha()` for the blend curve).

The `SetViewTarget` override always calls `Super::SetViewTarget(NewViewTarget, FViewTargetTransitionParams())` �?empty transition params, so the PCM's built-in `PendingViewTarget` blend is never used. CCS handles all blending through its own evaluation tree.

This design follows the same pattern as Epic's `AGameplayCamerasPlayerCameraManager::SetViewTarget`, which also strips transition params and converts them into its own blend node system (`UViewTargetTransitionParamsBlendCameraNode`).

### Level Sequence Integration via Implicit Activation

Level Sequences fire `SetViewTarget` on the PCM via the engine's CameraCut handler (`FCameraCutGameHandler::SetCameraCut()` in `MovieSceneCameraCutGameHandler.cpp`). The call chain is: `UMovieSceneCameraCutTrackInstance::OnAnimate()` �?`FCameraCutAnimator::AnimateBlendedCameraCut()` �?`FCameraCutGameHandler::SetCameraCut()` �?`CameraManager->SetViewTarget()`.

With implicit activation, each `SetViewTarget` from the engine creates a **new transient proxy camera** in the cutscene context. This means:

- **Hard CameraCut** (no easing): `SetViewTarget(B1)` �?proxy camera B1 created, no transition (hard cut).
- **Blended CameraCut** (easing overlap): `SetViewTarget(B2, {BlendTime=0.5s, EaseInOut})` �?proxy camera B2 created with a 0.5s `UComposableCameraViewTargetTransition`. The evaluation tree blends between B1 and B2 using CCS's normal transition machinery.
- **Intra-LS camera blends are fully supported** �?each camera in the LS becomes a separate CCS camera in the evaluation tree, and CCS handles the blend through its own pose-only transition system.

### Implicit Activation for Arbitrary Actors

The same `SetViewTarget` path handles non-LS use cases:

- `SetViewTargetWithBlend(SecurityCamera, 1.0s)` �?proxy camera wrapping the security camera, 1.0s transition.
- `SetViewTarget(SomeActor)` �?proxy camera wrapping SomeActor, hard cut.
- `SetViewTarget(Pawn)` where the pawn has no `UCameraComponent` �?no proxy created, CCS continues evaluating normally.

It is the user's responsibility to return to the previous camera via explicit `ActivateCamera` when done.

### Components

| Component | Role |
|---|---|
| `ViewTargetProxyNode` | Lightweight node that reads `FMinimalViewInfo` from a static target actor's `UCameraComponent` each tick. Created programmatically by the PCM's `SetViewTarget` override, not user-facing. |
| `UComposableCameraViewTargetTransition` | Transition that wraps `FViewTargetTransitionParams` and delegates to `GetBlendAlpha()` for the blend curve. Created programmatically by the PCM's `SetViewTarget` override when `BlendTime > 0`. |
| `UAsyncPlayCutsceneSequence` | Blueprint-callable async action. Pushes a cutscene context, creates `ULevelSequencePlayer`, and starts playback. Engine CameraCut events flow through `SetViewTarget` �?implicit activation, creating proxy cameras in the cutscene context. Pops the context when LS ends. |
### End-to-End Flow (PlayCutsceneSequence)

1. **Blueprint calls `PlayCutsceneSequence`**: pushes a cutscene context (with inter-context transition from gameplay), creates the LS player, starts playback. Tree:
   ```
   InterContextTransition
   ├── ReferenceLeaf �?Gameplay
   └── InitialCamera (empty, awaiting first SetViewTarget)
   ```

2. **First CameraCut fires**: Engine calls `SetViewTarget(CamA)` �?PCM creates proxy camera wrapping CamA, activates in cutscene director (hard cut, replacing the initial camera). Tree:
   ```
   InterContextTransition
   ├── ReferenceLeaf �?Gameplay
   └── ProxyCamera_CamA (ViewTargetProxyNode �?CamA)
   ```

3. **Each tick**: CamA's `CineCameraActor` animates via Sequencer. The proxy node reads `GetCameraView()` from CamA's `UCameraComponent` and outputs the pose.

4. **Blended CameraCut (CamA �?CamB with 0.5s ease)**: Engine calls `SetViewTarget(CamB, {BlendTime=0.5s})` �?PCM creates proxy camera wrapping CamB with a `ViewTargetTransition`. Tree:
   ```
   InterContextTransition
   ├── ReferenceLeaf �?Gameplay
   └── ViewTargetTransition (0.5s)
       ├── ProxyCamera_CamA
       └── ProxyCamera_CamB
   ```

5. **Intra-LS blend completes**: `ViewTargetTransition` finishes �?`CollapseFinishedTransitions` promotes right subtree, destroys ProxyCamera_CamA. Tree simplifies to ProxyCamera_CamB.

6. **LS ends**: `OnFinished` fires �?pops cutscene context �?inter-context transition back to Gameplay.

### Explicit LS Authoring Path (V1.4)

The implicit-activation path above is the right answer when an existing gameplay flow wants to hand the viewport to a Level Sequence that references **pre-built camera actors** (CineCameraActors, user-authored cameras). It treats the LS as an external animation source.

A parallel path solves the complementary problem: **authoring a CCS camera directly inside a Level Sequence**, with its exposed parameters keyable on Sequencer tracks. This is the only path that gives designers per-parameter animation of a composable camera.

**Components:**

```
AComposableCameraLevelSequenceActor        (Spawnable only, NotPlaceable)
 ├── UCineCameraComponent (Output)                       �?RootComponent, Camera Cut Track target
 └── UComposableCameraLevelSequenceComponent              (sibling UActorComponent; pure logic)
      ├── FComposableCameraTypeAssetReference
      �?   ├── TypeAsset           : UComposableCameraTypeAsset*
      �?   ├── Parameters          : FInstancedPropertyBag  �?from ExposedParameters
      �?   └── Variables           : FInstancedPropertyBag  �?from ExposedVariables
      ├── OutputCineCameraComponent : TObjectPtr<UCineCameraComponent>  �?reference to root
      ├── InternalCamera           : AComposableCameraCameraBase*  (transient)
      └── Tick loop                 (editor + PIE both)
```

Structure mirrors `ACineCameraActor`: the CineCamera is the Actor's RootComponent, so every native UE path (`FindComponentByClass<UCameraComponent>`, Camera Cut Track, viewport Pilot, `PCM::SetViewTarget`'s implicit-activation filter) resolves to the root immediately �?same fast path as a plain CineCameraActor. The LevelSequenceComponent is a plain `UActorComponent` (not a SceneComponent) �?a logic-only driver with no transform of its own, holding a reference back to the root CineCamera.

**Authoring flow:**

1. Designer opens a Level Sequence, right-clicks �?`Add Spawnable �?ComposableCameraLevelSequenceActor`.
2. Selects the Spawnable's binding, Details panel shows `TypeAssetReference.TypeAsset`. Picks a TypeAsset.
3. `RebuildBagsFromTypeAsset` regenerates the `Parameters` and `Variables` bags to mirror the TypeAsset's exposed surface; existing values under matching names are preserved (`MigrateToNewBagStruct`).
4. Designer adds a Camera Cut Track pointing at the Spawnable, and (optionally) a component track for `LevelSequenceComponent`. Right-click `+Track` on the component binding �?**Camera Parameters** / **Camera Variables** sections (added by `FComposableCameraLevelSequenceComponentTrackEditor`) list every keyable bag leaf. One click �?stock `UMovieSceneFloatTrack` / `UMovieSceneDoubleVectorTrack` / etc. materializes on the path `TypeAssetReference.Parameters.Value.{ParamName}`.
5. Designer keys values over time; Sequencer's stock property-track evaluation writes directly into the bag's backing FProperty each frame.

**Runtime evaluation (editor + PIE, same pipeline):**

1. Sequencer spawns the `AComposableCameraLevelSequenceActor` via its spawn register when the binding's Spawn track enters range. Component's `OnRegister` calls `SetEvaluationEnabled(true)`, preserving the default-on behavior while deferring the zero-delta warm-up to the next component tick.
2. First `TickComponent`:
   - `EnsureInternalCamera` spawns a transient `AComposableCameraCameraBase` into the same world. `Initialize(nullptr)` runs; `ConstructCameraFromTypeAsset(Camera, TypeAsset, BuiltBlock)` duplicates node templates, builds the runtime data block, applies parameter values from the bag, fires per-node `OnInitialize`. The compute chain is a no-op because `BeginPlayCamera` already fired with an empty array before nodes were populated.
3. Each subsequent tick:
   - Rebuild an `FComposableCameraParameterBlock` from the current bag values.
   - `TypeAsset->ApplyParameterBlock(*RuntimeDataBlock, Block)` re-syncs the bag into the data block (cheap �?O(exposed-parameter count), no allocations).
   - Resolve Sequencer-aware DeltaTime: runtime spawned actors ask the owning `UMovieSceneSequencePlayer` for `GetPlayRate()`, while pure editor preview asks the active `ISequencer` through the runtime/editor hook.
   - `InternalCamera->TickCamera(SequencerAwareDeltaTime)` produces a pose, so history-based nodes (for example `TwoPointMove` and `DirectionalMove`) follow Level Sequence playback speed instead of raw world tick speed.
   - `ProjectPoseToCineCamera(Pose)` writes **position and rotation only** onto the child `UCineCameraComponent`. Optical fields (FocalLength / Aperture / Filmback / Focus / PostProcess) are left for the designer to control directly on the CineCamera, either in Details or via Sequencer's native property tracks �?this is the explicit design point: LS cameras own their optics on the CineCamera, CCS owns spatial behavior.
4. Sequencer's Camera Cut Track picks up the Spawnable's `UCineCameraComponent` via `FindCameraComponent` and routes the viewport through it. No special handshake with CCS runtime is needed.
5. When the Spawn track exits range, Sequencer destroys the actor. `EndPlay` calls `DestroyInternalCamera` which releases the internal evaluator.

**Decoupling from PCM:** nothing in this path touches `AComposableCameraPlayerCameraManager`. The existing PCM-driven gameplay stack runs in parallel in the same world. CCS cameras activated through gameplay code live on their own context stack; LS-driven cameras live on the LS actor's isolated evaluator. The Camera Cut Track mediates which one the viewport sees at any moment.

**Node compatibility surface:** some nodes hard-depend on the PCM (e.g. `MixingCameraNode` calls `PCM->CreateNewCamera` at init; compute nodes are ComputeOnly by design). Each node declares its stance via `UComposableCameraCameraNodeBase::GetLevelSequenceCompatibility() �?EComposableCameraNodeLevelSequenceCompatibility`. The method is a `BlueprintNativeEvent` so BP-authored camera nodes (subclasses of `UComposableCameraBlueprintCameraNode`) can override it alongside C++ nodes. A Details-panel customization (`FComposableCameraTypeAssetReferenceCustomization`) inspects the bound TypeAsset's nodes and surfaces a yellow warning banner listing incompatible classes �?designer-facing signal that those nodes will silently no-op in the LS path.

### Spawn Track-owned LS camera lifecycle (Phase G cleanup)

The LS-authoring path uses the Sequencer Spawn Track as the sole lifecycle authority. A spawned `AComposableCameraLevelSequenceActor` owns a child `UCineCameraComponent` plus a sibling `UComposableCameraLevelSequenceComponent`; while the Spawnable actor exists, the component evaluates its TypeAsset every tick and projects position / rotation into the CineCamera. When the Spawn track exits range, Sequencer destroys the actor and `OnUnregister` / `EndPlay` destroy the transient internal evaluator.

There is no CCS MovieScene ECS gate in this path. CameraCut still decides which CineCamera reaches the viewport, but it no longer opens or closes CCS evaluation. If a sequence wants a camera to stop evaluating, key its Spawn Track false or destroy the owning actor.

First-frame behavior stays deterministic: `OnRegister` calls `SetEvaluationEnabled(true)` and defers a zero-delta first evaluation to the next component tick, after Sequencer property tracks have written the current bag values. `SetSequencerShotOverride` may still force a same-frame `EvaluateOnce(0.f)` when a Shot section first appears, because TrackInstance order can deliver the override after the component tick for that frame.

Subsequent non-zero evaluations resolve DeltaTime from Sequencer ownership rather than blindly using component tick DeltaTime. Runtime playback uses the spawnable annotation plus the owning player's spawn register to find the `UMovieSceneSequencePlayer`; editor preview uses `FGetEditorSequencerPlaybackDeltaTime` bound by the editor module to read the active `ISequencer` playback speed. Paused Sequencer preview returns zero DeltaTime.

---

## 18. Key Invariants

1. **Only the top context is evaluated** �?unless a reference leaf in the top context's tree reaches into a lower context.
2. **Transitions are pose-only** �?they never hold camera or Director references. The tree structure handles the wiring.
3. **Right subtree is always the target** �?`CollapseFinishedTransitions` always promotes right, destroys left.
4. **Collapse recurses into both subtrees** �?the right subtree can contain nested intra-context transitions (from same-context activations under an inter-context root) that need collapsing independently of the left side.
5. **Transient cameras always get their own context** �?prevents lifetime expiry from destroying persistent cameras.
6. **Base context cannot be popped** �?there must always be at least one context on the stack.
7. **PendingDestroyEntries stay alive until their transition finishes** �?the reference leaf in the active context's tree holds a `TSharedPtr` snapshot of the popped director's tree; the pending-destroy director stays registered on the stack (but is never called through `Evaluate`) until the transition's `OnTransitionFinishesDelegate` fires cleanup.
8. **TVariant nodes must handle all three types** �?when adding new node types or modifying the variant, every branching function (Visit, switch, if-chain) must be updated for all variants.
9. **EnsureContext is move-to-top, not just existence-check** �?position in the stack determines which context is active.
10. **Per-node one-shot setup belongs in `OnInitialize`, not `BeginPlayCamera`** �?`UComposableCameraCameraNodeBase::OnInitialize` is a `BlueprintNativeEvent` (retained for the `_Implementation` pattern even though nodes are `NotBlueprintable`); C++ subclasses override `OnInitialize_Implementation` and must chain `Super::OnInitialize_Implementation()`. Nodes that need the outgoing camera pose at setup time read it from `OwningPlayerCameraManager->GetCurrentCameraPose()`.
11. **`InitializeNodes` may run twice on the type-asset activation path** �?once with an empty `CameraNodes` / `ComputeNodes` array during `SpawnActorDeferred �?Camera::Initialize` (no-op), then again from `OnTypeAssetCameraConstructed` after the node arrays have been duplicated from the type asset. Any future per-node init work must remain idempotent across this double-call, or the path must be refactored to call `InitializeNodes` exactly once.
12. **Compute nodes must not run per-frame** �?`UComposableCameraComputeNodeBase` inherits from `UComposableCameraCameraNodeBase` for the pin system but is executed exactly once per activation, from `BeginPlayCamera`, via `ExecuteBeginPlay`. `InitializeNodes` deliberately skips tick-delegate registration for entries in `ComputeNodes`; don't add them back, and don't override `OnTickNode_Implementation` on compute node subclasses �?it will never be called.
13. **Variable nodes (Get/Set) are allowed on both compute and camera chains** �?`SetVariable` nodes may appear on either the compute chain (to populate initial values from evaluated data at activation) or the camera chain (to write computed values during ticking). When `FullExecChain` is available, both chain types support interleaved variable writes; without it, linear `CameraNodes` walk still allows Get/Set node execution at any position.
14. **Pose blending must go through `BlendBy`** �?transitions never hand-lerp individual fields of `FComposableCameraPose`. The correct pattern is to initialize the output pose from `CurrentSourcePose`, call `OutPose.BlendBy(CurrentTargetPose, Alpha)`, then overwrite only the fields the transition explicitly computes itself (e.g. cylindrical / inertialized Position and Rotation). This guarantees that every existing and future pose field (FOV, physical camera, projection, aspect, orthographic) participates in the blend.
15. **FOV must be resolved to degrees before blending** �?a pose is FOV-ambiguous as long as both `FieldOfView` and `FocalLength` are independently in play. `BlendBy` resolves each side via `GetEffectiveFieldOfView()` first, lerps the degrees values, and writes the output as a degrees-mode pose (`FocalLength = -1`). Nodes that author FOV from code must call `SetFieldOfViewDegrees(...)` �?assigning directly to `FieldOfView` leaves a stale `FocalLength` and produces mixed-mode poses that corrupt downstream blending.
16. **Projection mode and aspect booleans use target-wins-immediately, never lerp** �?these fields have no meaningful interpolation. `BlendBy` adopts the target's value as soon as the blend starts (`OtherWeight > 0`). This applies to `ProjectionMode`, `ConstrainAspectRatio`, `OverrideAspectRatioAxisConstraint`, and `AspectRatioAxisConstraint`. This ensures that a camera which sets e.g. `ConstrainAspectRatio = true` takes effect at the start of the transition, not mid-blend. Orthographic scalar fields (`OrthographicWidth`, `OrthoNearClipPlane`, `OrthoFarClipPlane`) still lerp normally.
17. **The pose is authoritative over the `UCameraComponent` for projection, aspect, and post-process** �?`GetCameraViewFromCameraPose` reads projection mode, aspect constraint booleans, and the axis-constraint enum off the pose. Post-process is a three-layer stack: (1) the camera component's designer-authored `PostProcessSettings` as the baseline, (2) the pose's `PostProcessSettings` layered on top via `FPostProcessUtils::OverridePostProcessSettings` (only properties with `bOverride_*` true take effect), (3) physical camera settings (`ApplyPhysicalCameraSettings`) for DoF/exposure on top. A camera with no PostProcess node has all overrides off �?layers 2 and 3 are no-ops and the component baseline passes through unchanged. Nodes that want to change projection/aspect/PP do so by writing the pose, not by mutating the actor.
18. **Enum slots are normalized to int64 in the data block** �?`EComposableCameraPinType::Enum` always reads / writes through the `int64` storage path (`GetPinTypeSize` returns 8, `GetPinTypeAlignment` returns 8). Producers that publish to an enum slot must cast through `int64` regardless of the source UPROPERTY's underlying width (uint8 / int32 / int64), and consumers that pull from an enum slot must narrow-cast back to the destination property's width using `FEnumProperty::GetUnderlyingProperty()` (modern `enum class`) or `FByteProperty::GetIntPropertyEnum()` (legacy `TEnumAsByte`). The K2 thunk in `ComposableCameraBlueprintLibrary` does this normalization automatically using `FProperty` type detection on the wired pin; bypassing it (e.g. memcpy of the native enum into the data block) silently corrupts adjacent slots once the underlying width is anything other than 8 bytes. Persistence (DataTable rows, variable initial values, exposed-parameter defaults) stores the value as the authored entry name string (e.g. `"EMyEnum::Alpha"`) �?this is enum-renumbering safe and SCM-friendly. The runtime parser resolves names through `UEnum::GetValueByNameString`, and the debug HUD renders values back through `UEnum::GetNameStringByValue` whenever the slot's `UEnum*` is known, falling back to the raw integer otherwise so debug output never silently lies about the slot.
19. **Name slots transport `FName` as POD** �?`EComposableCameraPinType::Name` uses the same memcpy-based fast path as the numeric / vector types because `FName` is an 8-byte POD comparison index. Writes go through `WriteValue<FName>` and copies preserve the index without touching the global name table. There is no string allocation on the hot path; `FString` was deliberately excluded from the pin type set for the same reason.
20. **Delegate pins bypass the data block entirely** �?`EComposableCameraPinType::Delegate` has zero size in the runtime data block (`GetPinTypeSize` returns 0). Delegates are not POD and cannot be memcpy'd into the byte array. Instead, the `FComposableCameraParameterBlock` carries them in a parallel `DelegateValues` map (`TMap<FName, FScriptDelegate>`), and `UComposableCameraTypeAsset::ApplyDelegateBindings` writes them into the target node's `FDelegateProperty` UPROPERTY via reflection at activation time. The per-frame auto-resolve path (`GetOrBuildPinBindings`, `ResolveAllInputPins`) skips delegate-typed properties because `TryMapPropertyToPinType` returns false unless the caller explicitly opts in via the 5th `OutSignatureFunction` parameter. `ApplySubobjectPinValues` carries a no-op `case Delegate:` for switch exhaustiveness. In the K2 `ExpandNode`, unconnected delegate pins are skipped entirely �?an unbound `FScriptDelegate` is a no-op and emitting a `SetParameterBlockValue` call for it would be wasted work. The `FComposableCameraNodePinDeclaration` for a delegate pin carries a `SignatureFunction` pointer (the `UFunction*` generated by `DECLARE_DYNAMIC_DELEGATE_*`) so the editor pin conversion (`MakeEdGraphPinTypeFromCameraPinType`) can produce a properly typed `PC_Delegate` pin with a `PinSubCategoryMemberReference` that the K2 schema uses for wiring validation. `FComposableCameraExposedParameter` stores the same `SignatureFunction` for the K2 ActivateComposableCamera node to consume.
21. **Get variable nodes are transparent at runtime** �?Get variable graph nodes (`UComposableCameraVariableGraphNode` with `bIsSetter == false`) have no runtime identity. They don't appear in `NodeTemplates`, `ComputeNodeTemplates`, or any exec chain. Instead, `BuildRuntimeDataLayout` resolves their connections directly: for each consumer input pin wired to a Get node's output, `InputPinSourceOffsets` maps the consumer's input key to the variable's `InternalVariableOffsets` slot. The consumer reads the variable value via the normal `TryResolveInputPin` wired-connection path �?priority 1 �?with zero per-frame overhead from the getter itself. This means the consumer's input pin resolves to the same storage slot that `ApplyParameterBlock` / `InitialValueString` seeding writes into. **Exception:** if the same consumer pin is also an ExposedParameter target, the `ExposedInputPinOffsets` entry takes semantic priority and the variable getter connection is skipped �?exposing a pin breaks the wire, so a coexisting getter connection is stale.

22. **Source camera freeze is a leaf-level flag, not a camera-level flag** �?`FComposableCameraActivateParams::bFreezeSourceCamera` controls whether the outgoing (source) camera stops ticking during a transition. The flag is threaded through `Director::ActivateNewCamera*` �?`EvaluationTree::OnActivateNewCamera*` and applied via `FreezeSubtree`, which recursively sets `bFrozen` on every leaf and reference-leaf in the left (source) subtree. A frozen leaf returns `RunningCamera->CameraPose` (its last evaluated pose) without calling `TickCamera`; a frozen reference leaf returns `ReferencedDirector->GetLastEvaluatedPose()` without re-evaluating the source context's tree. The freeze flag lives on the wrapper structs (`FComposableCameraEvaluationTreeLeafNodeWrapper::bFrozen`, `FComposableCameraEvaluationTreeReferenceLeafNodeWrapper::bFrozen`), not on the camera actor �?a camera can be frozen in one tree position while still being alive. `ResumeCamera` and `ReactivateCurrentCamera` do not expose `bFreezeSourceCamera` and default to `false`. The auto-pop path in the context stack also defaults to `false` (the transient camera is finished and freezing it would be meaningless).

23. **Spline and path-guided transitions draw debug inline** �?`UComposableCameraSplineTransition::DrawDebugSpline` and `UComposableCameraPathGuidedTransition::OnEvaluate` call `DrawDebugPoint` directly in each loop iteration rather than collecting points into an intermediate `TArray<FVector>`. The CatmullRom spline type uses a virtual-index lambda (`GetVirtualControlPoint`) to access the `{ZeroVector, ControlPoints[0..N-1], EndVec}` sequence without copying the `ControlPoints` array or calling `Insert()`. These patterns eliminate per-frame heap allocations in the transition evaluation hot path. The sole justified per-frame allocation remaining in the node/transition layer is `MixingCameraNode::OnTickNode`'s `TArray<float> Weights` returned from the delegate �?the caller owns that allocation and the contract requires a by-value return.

24. **LS component lifetime follows the Spawn Track** �?`UComposableCameraLevelSequenceComponent::OnRegister` calls `SetEvaluationEnabled(true)` for every spawned LS Actor, and no CCS MovieScene gate flips it per CameraCut. `SetEvaluationEnabled(false)` remains a local teardown / external-host switch and destroys the transient internal camera. OFF->ON defers a one-shot zero-delta evaluation to the next component tick so Sequencer property tracks can write current bag values first. After that warm-up, LS-owned evaluators tick with Sequencer-aware DeltaTime so time-history nodes respect Level Sequence playback speed.

25. **Inter-context activation defers OLD-root destruction until the new transition finishes** �?`UComposableCameraEvaluationTree::OnActivateNewCameraWithReferenceSource` does NOT call `DestroySubtreeCameras(RootNode)` when it installs the new Inner. Instead it stashes the pre-replacement RootNode in `PendingDestroyOldRoots` and registers a weak lambda on the new transition's `OnTransitionFinishesDelegate` that destroys the stash when the blend completes. Rationale: if the `SourceDirector` was previously PUSHED onto us, its RefLeaf captured OUR old RootNode as a `TSharedPtr` at push time �?destroying those leaves immediately would leave the SourceDirector's Tick walking through now-destroyed `Leaf.RunningCamera` actors during the blend (observable as `[leaf] (destroyed)` in the panel + `"RunningCamera is null or destroyed when evaluating leaf node."` errors). By the time the new transition finishes, `CollapseFinishedTransitions` has dropped the RefLeaf branch of the new root �?the stash is no longer reachable from this director's tree and can be destroyed. Backstop: `DestroyAll()` flushes any still-pending stashes so transitions that never complete (context popped mid-blend, transition replaced by another activation) don't leak. This is the activate-new counterpart to the pop-side `ResumeCurrentCameraWithReferenceSource` fix (invariant #7's pending-destroy machinery); together they guarantee camera actors stay valid for the entire lifetime of any RefLeaf snapshot that can reach them.

26. **Transient contexts demoted from the top by `EnsureContext` move-to-top are implicitly popped** �?`UComposableCameraContextStack::Evaluate`'s auto-pop loop inspects ONLY the top entry. A transient context that was on top at push time but later gets shoved below by `EnsureContext` reordering `Entries` to bring an existing context back to the top would, without further handling, be stranded: its running camera is transient but no longer at the top so auto-pop never sees it, its `RemainingLifeTime` expiring does nothing, and the context leaks until explicit `PopContext(name)`. `PCM::ActivateNewCamera` closes this gap �?after a successful inter-context activation (bContextSwitched path) it calls `ContextStack->DemoteNonTopTransientContextsToPending(ActivatingTransition)`, which walks every non-top entry, identifies those whose `RunningCamera->IsTransient()` is true, moves them from `Entries` into `PendingDestroyEntries`, and binds cleanup to `ActivatingTransition->OnTransitionFinishesDelegate` (camera-cut activations destroy immediately �?no blend means no RefLeaf chain needs the transient camera alive). Semantics match an explicit `PopContext` for each demoted transient: its director + camera stays GC-alive for the duration of the new blend, continues to tick through the activation tree's RefLeaf snapshot, then gets `DestroyAllCameras` + removed from the pending bucket exactly when the transition resolves. Non-transient contexts that happen to be below the top are NOT touched �?they are designer-managed (e.g. a UI context suspended behind gameplay) and stay suspended. This rule is the activation-side counterpart to invariants #7 and #25: together they guarantee that every transient camera actor's destruction timing is deterministic and no RefLeaf snapshot ever walks into a destroyed leaf.

27. **CameraCut blend correctness depends on Spawn Track overlap authoring** �?because CCS no longer gates LS components by CameraCut active set, any camera that should contribute to a cut blend must remain spawned for the whole authored blend window. Same-row / dual-row overlap authoring should keep both LS Actors alive long enough for their CineCameraComponents to provide fresh poses to the engine CameraCut blend.

28. **Patches live on the Director, not the EvaluationTree** �?`UComposableCameraPatchManager` is a Director subobject; `Director::Evaluate` calls `PatchManager.Apply` after `EvaluationTree.Evaluate`. Cross-context blends route through the source tree's RefLeaf snapshot WITHOUT invoking the source Director's `Evaluate`, so source-side patches do not contribute to the inter-context blend's source pose. Documented limitation; V2 promotes Patch into the tree as a new wrapper variant (see `PatchSystemProposal.md` §10).

29. **Patch evaluators construct PCM-independently** �?`UComposableCameraPatchManager::AddPatch` spawns the evaluator with `Initialize(nullptr)`, the same path the LS Component uses (§17 / TechDoc §3.14). Skips `BindCameraActionsForNewCamera` by construction, so Patch evaluators carry no Action delegates / arrays. PCM-level Action dispatch never crosses into a Patch �?a `TargetNodeClass` matching a node inside an active Patch would not fire (PatchSystemProposal §16.9).

30. **`ActivePatches` ordering is `(LayerIndex asc, PushSequence asc)`, sorted-inserted at AddPatch** �?`Apply` iterates in this order; later-layer patches see earlier-layer patches' output as their upstream. PushSequence is a monotonic counter on PatchManager. Older patches at equal layers run earlier (push-order tiebreaker). Layer override at AddPatch (`Params.LayerIndexOverride != MIN_int32`) wins over the asset's `DefaultLayerIndex`.

31. **Patch envelope's exit ramp scales by `ExitStartAlpha` snapshot** �?`ExpirePatch` (manual) and `CheckPatchScheduleExpiration` (Duration / Condition / OnCameraChange channels) both go through a shared `TransitionPatchToExiting` helper that snapshots `CurrentAlpha` into `ExitStartAlpha` before flipping Phase to Exiting. The Exiting branch computes alpha as `ExitStartAlpha · (1 - ease(t))`, so a Patch retired mid-Entering fades from where it had reached, not from 1. Short-circuit to Expired when `ExitStartAlpha <= 0` or `ExitDuration <= 0` �?no wasted invisible frames.

32. **Patch removal from `ActivePatches` happens at end of `Apply`, never mid-iteration** �?`ExpirePatch` only flips Phase to Exiting (or directly to Expired in the short-circuit case); the actual array compaction + evaluator destruction happens in the reverse-iter sweep at the tail of `Apply`. This makes `ExpirePatch` reentrancy-safe �?a Patch's node tick calling `ExpirePatch` does NOT invalidate the for-loop iterator. Patch evaluator destruction calls `AActor::Destroy()` then `Evaluator = nullptr`; the `UComposableCameraPatchInstance` itself is GC-collected on the next pass once the array no longer references it. (The remaining latent footgun is `AddPatch` during `Apply` �?`Insert` may reallocate `ActivePatches` and invalidate the iterator. Stage-2-era residual; will be addressed alongside the V2 deferred-add queue if a real callsite needs it.)

33. **Shot overlap exits promote prior state, hard cuts reseed** -- in the LS Shot Section path, the per-shot prior caches belong to Section identity, not to the temporary primary/secondary role. When an authored overlap exits A+B -> B, the LS component marks that the new primary was the previous secondary and `CompositionFramingNode` promotes `LastSecondaryOutput*` into `LastPrimaryOutput*`. This preserves framing-zone / damping continuity on the first post-blend frame. A true hard cut (changed primary that was not the previous secondary) still clears primary prior state so the new Shot hard-seeds at its authored screen position. A changed secondary (A+B -> A+C) clears secondary prior state so C does not inherit B's zone/damping cache.

---

## 19. Module Structure

```
ComposableCameraSystem.uplugin
  ├─ ComposableCameraSystem          (Runtime, PreDefault)
  �?   Public/
  �?     Core/         �?PCM, Director, ContextStack, EvaluationTree, ModifierManager
  �?     Cameras/      �?CameraBase, CameraPose, ActivateParams
  �?     Nodes/        �?All camera node types
  �?     Transitions/  �?All transition types
  �?     Modifiers/    �?Modifier base and data assets
  �?     Interpolator/ �?Interpolator base and implementations
  �?     Math/         �?Spline implementations
  �?     DataAssets/   �?TypeAsset, TransitionTable, Modifier, Transition data assets;
  �?     �?              ParameterTableRow (DataTable row struct)
  �?     Actions/      �?Camera action types
  �?     AsyncActions/ �?Async curve evaluators
  �?     Utils/        �?BlueprintLibrary, ProjectSettings, ImpulseShapes
  �?   Private/
  �?     (mirrors Public structure)
  �?     Tests/        �?Automation tests
  �?
  ├─ ComposableCameraSystemEditor    (Editor, PostEngineInit)
  �?   Public/ & Private/
  �?     Editors/         �?Graph editors, asset editor hosts
  �?     Toolkits/        �?FAssetEditorToolkit subclasses
  �?     Factories/       �?UFactory subclasses (TypeAsset, TransitionTable, Modifier, Transition)
  �?     AssetTools/      �?UAssetDefinition subclasses (Content Browser integration,
  �?     �?                 display names, colors, category, double-click actions)
  �?     Customizations/  �?IPropertyTypeCustomization / IDetailCustomization
  �?     Widgets/         �?Reusable Slate widgets
  �?     ScriptedActions/ �?Scripted asset actions
  �?   Resources/
  �?     Content/Icons/   �?SVG icons for ClassIcon / ClassThumbnail brushes
  �?
  └─ ComposableCameraSystemUncookedOnly  (UncookedOnly, Default)
       �?Custom K2 nodes, graph pin widgets, graph panel pin factory

Dependencies: EngineCameras, EnhancedInput, ActorSequence
```

### Content Browser Registration

All four primary data asset types are registered via `UAssetDefinitionDefault` subclasses in the editor module's `AssetTools/` folder. Each registers a display name, color, and Content Browser category ("Composable Camera System"), and most open a custom editor on double-click.

| Asset Type | Display Name | Color | Factory |
|---|---|---|---|
| `UComposableCameraTypeAsset` | Composable Camera Type | Teal/Cyan | `UComposableCameraTypeAssetFactory` |
| `UComposableCameraTransitionTableDataAsset` | Camera Transition Table | Soft blue | `UComposableCameraTransitionTableFactory` |
| `UComposableCameraNodeModifierDataAsset` | Camera Node Modifier | Purple (160,90,200) | `UComposableCameraModifierFactory` |
| `UComposableCameraTransitionDataAsset` | Camera Transition | Warm orange (220,140,50) | `UComposableCameraTransitionFactory` |

Custom SVG thumbnails are registered through `FComposableCameraEditorStyle` using the `ClassIcon.ClassName` / `ClassThumbnail.ClassName` naming convention. The style set is initialized in `FComposableCameraSystemEditorModule::StartupModule()` and provides 16×16 class icons and 64×64 thumbnails for all four asset types.

---

## 20. Debug HUD

The system provides an on-screen debug display via `AComposableCameraPlayerCameraManager::DisplayDebug`, triggered by UE's `showdebug` console command.

### Enabling

In-game console: `showdebug camera` (triggers the PCM's DisplayDebug override).

### HUD Sections

The debug HUD displays the following sections, top to bottom:

1. **Camera Pose**: Current position, rotation, FOV, aspect ratio
2. **Running Camera**: Class name, gameplay tag, transient status with lifetime countdown
3. **Camera Nodes**: Ordered list of all nodes on the running camera (by index and class name)
4. **Context Stack & Evaluation Tree**: Full stack dump from top to bottom, with each context's Director showing its evaluation tree structure. Tree nodes are color-coded:
   - Green: Leaf nodes (cameras)
   - Yellow: Transition inner nodes (with class name, percentage, elapsed/total time)
   - Orange: Reference leaf nodes (inter-context Director references)
   - Red: Pending destroy entries (popped contexts in transition)
   - White: Active context marker (`->`)
5. **Camera Actions**: All active actions with scope (camera-scoped vs. persistent)
6. **Modifiers**: All registered modifiers grouped by camera tag and node class, plus effective (highest-priority) modifiers

### Implementation

All debug consumers (the 2D panel, `showdebug camera`, and the `CCS.Dump.*`
console commands) read from a single structured-snapshot pipeline:

- `UComposableCameraEvaluationTree::BuildDebugSnapshot` �?flattens the tree DFS
  pre-order into a `TArray<FComposableCameraTreeNodeSnapshot>`.
- `UComposableCameraDirector::BuildDebugSnapshot` �?wraps the tree snapshot
  with running-camera label + last pose.
- `UComposableCameraContextStack::BuildDebugSnapshot` �?emits contexts top �?
  base (active first), then pending-destroy entries.

Text rendering for `showdebug camera` goes through
`ComposableCameraDebug::AppendTreeNodeLine` (in `Utils/ComposableCameraDebugFormatUtils.h`),
which formats a single snapshot node into a caller-provided `FStringBuilderBase`.
The panel uses the same snapshot but renders geometric primitives instead of
text. There is no parallel "debug string" pipeline �?adding a new tree node
kind is one switch statement in `AppendTreeNodeLine` plus the snapshot enum,
not two parallel branch sites.

### In-Viewport Debug Panel

Alongside the `showdebug camera` path, the plugin offers a richer in-viewport overlay built on `UDebugDrawService`:

- **Toggle**: console variable `CCS.Debug.Panel 0|1`. Layout width is controlled by `CCS.Debug.Panel.Width` (fraction of screen width, 0.15�?.60, default 0.32).
- **Lifecycle**: `FComposableCameraDebugPanel::Initialize()` / `Shutdown()` are called from `FComposableCameraSystemModule::StartupModule` / `ShutdownModule`. Registration happens once, unconditionally �?the draw delegate early-outs when the CVar is 0, so the cost of "disabled" is a single CVar read per viewport per frame.
- **Rendering hook**: `UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate)`. The delegate fires once per local viewport with a `UCanvas*` + `APlayerController*`. The panel resolves the PCM via `PC->PlayerCameraManager` and casts to `AComposableCameraPlayerCameraManager`; non-composable PCMs are skipped silently.
- **Layout**: a single left-aligned panel subdivided into vertically stacked regions, each with a title bar, border, and content area. Region order is fixed in phase 1 and covers: (1) Current Pose, (2) Context Stack & Evaluation Tree, (3) Running Camera (class, tag, lifetime, nodes, exposed parameters, internal variables), (4) Actions, (5) Modifiers (placeholder �?defer to `showdebug camera` for the full breakdown).
- **Current Pose region layout**: four labelled groups flowed across **two columns** (driven by `bIsPose` + `PoseGroups` on `FRegionLines`, rendered by `DrawPoseStructured`). Left column: **Transform** (Position / Rotation / Forward) and **Context** (active context name). Right column: **Projection** (Mode, FOV �?annotated with `(from Nmm)` when the pose is in `FocalLength` dual-mode; Aspect; Ortho W / Near / Far only when Mode is Orthographic) and **Physical** with three data sources (see below). Two-column layout keeps the region height to the max of the two column line-counts, avoiding a ~15-line single-column tower on poses with Physical active.
- **Physical group - three-way data source**: the pose's Physical block is authoritative when a Lens/Exposure-style node has driven `PhysicalCameraBlendWeight > 0` or `ExposureBlendWeight > 0`. In the proxy-via-CineCamera path (e.g. Sequencer Camera Cut Track -> LS Actor -> `UComposableCameraViewTargetProxyNode` writing the pose) the CineCamera has already baked DoF / exposure into `PostProcessSettings` itself, so both pose weights stay 0 and the raw Aperture / Focus / ISO / Shutter / Sensor fields remain at struct defaults; displaying those would be misleading. The Physical group therefore discriminates:
  1. **`PhysicalCameraBlendWeight > 0` OR `ExposureBlendWeight > 0`** - pose-driven. Header `-- Physical --`, rows: DoF Weight / Exposure Wt / Aperture / Focus (`auto` when `FocusDistance <= 0` sentinel) / ISO / Shutter / Sensor, all read from `CurrentCameraPose`; ISO/Shutter display `auto` when `ExposureBlendWeight == 0`.
  2. **`PhysicalCameraBlendWeight == 0` AND `ExposureBlendWeight == 0` AND `PCM->GetViewTarget()` has a `UCineCameraComponent`** �?CineCamera-driven. Header `-- Physical (CineCamera) --` (suffix makes the data source visible without spending a row on a `Source:` line), rows: Focal (from `CurrentFocalLength` �?the Projection group's `(from Nmm)` annotation is absent here because the proxy node writes FOV in degrees-mode) / Aperture (`CurrentAperture`) / Focus (`CurrentFocusDistance`) / ISO (`PostProcessSettings.CameraISO` if `bOverride_CameraISO`, else `auto`) / Shutter (`CameraShutterSpeed` with same override gate) / Sensor (`Filmback.SensorWidth × SensorHeight`).
  3. **neither** �?collapsed single row `Status: off`.
- **Data sources**: same read-only public accessors already used by `DisplayDebug`. The Context Stack & Tree region consumes `UComposableCameraContextStack::BuildDebugSnapshot` (the structured path described below) �?this is the single source of truth; no parallel string path exists. The PCM exposes `GetContextStack()` so the debug panel does not need to be a friend of the PCM class.
- **Relationship to the ShowDebug HUD**: the panel is additive, not a replacement. `showdebug camera` still works and remains the canonical "stacks with other engine showdebug categories" workflow. The panel targets richer layout and per-region toggling that cannot be expressed inside `FDisplayDebugManager`'s vertical text flow.

**Structured snapshot path.** The Context Stack & Tree region consumes a structured snapshot (same snapshot the `showdebug camera` text path walks):

- `UComposableCameraEvaluationTree::BuildDebugSnapshot(TArray<FComposableCameraTreeNodeSnapshot>&)` �?flattens the tree DFS pre-order (root at depth 0, Left before Right under each Inner). Per-node fields: Kind (Leaf / ReferenceLeaf / InnerTransition), Depth, DisplayLabel, bDestroyed, bIsTransient / LifeElapsed / LifeTotal, TransitionProgress / TransitionElapsed / TransitionTotal. Also tags `bIsDominantLeaf = true` on the single node you reach by walking root �?Right �?Right �?... (i.e. the node that would survive if every in-flight transition collapsed).
- `UComposableCameraDirector::BuildDebugSnapshot(FComposableCameraContextSnapshot&)` �?fills RunningCameraDisplay / LastPose + delegates to the tree.
- `UComposableCameraContextStack::BuildDebugSnapshot(FComposableCameraContextStackSnapshot&)` �?emits contexts top-to-base, then pending-destroy entries flagged `bIsPendingDestroy = true`.

The snapshot structs live in `Public/Debug/ComposableCameraDebugPanelData.h` and have no reflection / no `WITH_EDITOR` guard �?they are plain C++ data, always available.

This drives three visual features the string path could not express:

1. **Transition progress bars.** Inner-transition lines get a translucent amber fill underlay proportional to `TransitionProgress`, drawn behind the text.
2. **Proper geometric tree connectors.** Each tree node carries `bIsLastSibling` + `AncestorLastFlagsBitmask` (a 32-bit mask where bit L is set iff the ancestor at depth L was the last child of its parent). The renderer draws `�?/ �?/ │` connectors as filled rects: continuation stem `│` at column L when bit (L+1) is 0; at the node's own column, a half-height stem + horizontal tick for `└` (last sibling) or a full-height stem + tick for `├` (middle child). Geometric rendering means no dependency on box-drawing font glyphs.
3. **Dominant-leaf highlight.** The leaf tagged `bIsDominantLeaf` draws with a translucent green underlay and the text in `CActiveMarker` (bright cyan). No text prefix is used �?the underlay plus color shift alone disambiguate.

Visual markers are kept deliberately narrow to avoid glyph collisions:
- **Active context**: small filled-rect bullet to the left of the name + `CActiveMarker` text color. No `-> ` prefix (that glyph is reserved for nothing �?dropped from the UI).
- **Base context**: `(base)` suffix on the context name.
- **Pending-destroy context**: `[pending]` prefix + `CDestroyed` red.
- **Reference leaf**: explicit `[ref] DirectorName` prefix in `CRefLeaf` orange.
- **Dominant leaf**: green underlay + cyan text. No text prefix.
- **Transition node**: `%` + `(elapsed/total)s` suffix + amber progress-bar underlay.

`showdebug camera` renders the same snapshot as plain text through
`ComposableCameraDebug::AppendTreeNodeLine`. Adding a new tree node kind means
updating the `EComposableCameraTreeNodeKind` enum and the one switch in
`AppendTreeNodeLine`; both the 2D panel and the text HUD pick up the new kind
automatically.

### In-World 3D Debug Draw

Complementing the 2D HUD panel, a separate facility draws world-space debug primitives for the currently running camera:

- **Two-tier gating**:
    - `CCS.Debug.Viewport 0|1` is the master switch. When 1 it turns on the camera FRUSTUM (with an F8 gate �?see below) and invokes each node's `DrawNodeDebug` hook. **It does not turn per-node gizmos on by itself.**
    - Per-node gizmos are controlled by PER-NODE CVars: `CCS.Debug.Viewport.PivotOffset`, `CCS.Debug.Viewport.PivotDamping`, `CCS.Debug.Viewport.LookAt`, `CCS.Debug.Viewport.CollisionPush`, `CCS.Debug.Viewport.Spline`. Each defaults to 0. Users enable only the gizmos they need for the bug in front of them.
    - `CCS.Debug.Viewport.AlwaysShow 1` forces the frustum even while possessing (frustum-only override; per-node gizmos don't need it).
- **Frustum auto-hide (F8 gate)**: the frustum is the one piece of debug draw that actually occludes the scene when the player is looking through the camera, so it only fires while `GEditor->bIsSimulatingInEditor` is true (F8 eject / Simulate mode). Per-node gizmos have no F8 gate �?they typically live out in the world in front of the camera, and are valuable during live gameplay. So the policy is: frustum �?"observe from outside" only; node gizmos �?"any time their CVar is on".
- **Lifecycle**: `FComposableCameraViewportDebug::Initialize() / Shutdown()` from the runtime module. Registers an `FTSTicker::GetCoreTicker()` delegate. Ticker body compiles to nothing in shipping (`#if !UE_BUILD_SHIPPING`).
- **Rendering pathway**: the ticker finds the PIE/Game world, pulls the first PCM's running camera, and calls `DrawCameraDebug(World, bDrawFrustum)`. The bool is computed once per frame from the F8 gate + AlwaysShow override; the node walk inside `DrawCameraDebug` always happens. Draws go through the world's LineBatcher �?visible in every viewport that renders the world, including the editor viewport during F8 eject. (An earlier iteration used `UDebugDrawService::Register("Game", ...)` but that hook does not fire from editor viewports during F8 eject.) Runtime module adds a `Target.bBuildEditor` conditional `UnrealEd` dependency to access `GEditor->bIsSimulatingInEditor`; shipping builds don't link `UnrealEd` and the WITH_EDITOR block is stripped.
- **Default content**: a yellow frustum pyramid drawn at the running camera's `CameraPose` via `DrawDebugCamera`. During a transition the running camera is the most recent activation target �?this shows the target's frustum, not the blended PCM output. Seeing both source and target simultaneously is a follow-up (would require walking the active director's tree).
- **Extension point**: two virtuals on the camera hierarchy, both `#if !UE_BUILD_SHIPPING`:
    - `AComposableCameraCameraBase::DrawCameraDebug(UWorld*) const` �?non-virtual; draws the frustum + iterates `CameraNodes`, calling each node's `DrawNodeDebug`.
    - `UComposableCameraCameraNodeBase::DrawNodeDebug(UWorld*) const` �?virtual with an empty default. Concrete nodes override to draw their own gizmos. Nodes read current-frame pin values through the usual `GetInputPinValue<T>()` / pin-bound UPROPERTY path �?the hook fires after TickNode so those values reflect the most recent evaluation.
- **Shipped node overrides** (each with its own CVar, default 0):
    - `PivotOffsetNode` (`CCS.Debug.Viewport.PivotOffset`) �?yellow sphere at the post-offset pivot (mirrored in `LastComputedPivot` since output pins aren't re-readable by name).
    - `PivotDampingNode` (`CCS.Debug.Viewport.PivotDamping`) �?magenta sphere at the damped pivot (reads existing `LastPivotPosition` state).
    - `LookAtNode` (`CCS.Debug.Viewport.LookAt`) �?cyan sphere at the target (resolves `ByPosition` / `ByActor` / socket the same way the tick path does). Possessed play: sphere only. F8 eject: adds a thin cyan line from camera to target.
    - `CollisionPushNode` (`CCS.Debug.Viewport.CollisionPush`) �?green/red trace sphere at the pivot (bTraceUseSphere) or small point (line trace), red sphere at the hit location when blocked. Possessed play: spheres only. F8 eject: adds the full pivot→camera trace line and the cyan self-collision sphere around the camera. Cache members (`LastTraceStart/End/HitLocation`, `LastSelfSphereCenter`, `bLastTraceBlocked`) populated in `FindCollisionPoint`.
    - `SplineNode` (`CCS.Debug.Viewport.Spline`) �?violet polyline sampled 64 times via `IComposableCameraSplineInterface::GetWorldSpacePositionByDistanceOnSpline` + violet sphere at the camera's current position.
    - `ReceivePivotActorNode` (`CCS.Debug.Viewport.ReceivePivotActor`) �?white sphere at the pivot actor's location (or bone socket when `bUseBoneForPivot`).
    - `RelativeFixedPoseNode` (`CCS.Debug.Viewport.RelativeFixedPose`) �?orange sphere at the reference origin (the authored `RelativeTransform` location, or the `RelativeActor` / socket when in actor mode). Answers "what am I positioning relative to?".
    - `ScreenSpacePivotNode` (`CCS.Debug.Viewport.ScreenSpacePivot`) �?teal sphere in 3D at the resolved world pivot, PLUS a 2D HUD overlay (safe-zone rectangle, its center marker, and the projected pivot marker). The 2D overlay mirrors the node's own screen-space math: two branches for `bConstrainAspectRatio = false` (whole viewport) and `= true` (letterboxed, uses `ULocalPlayer::GetProjectionData` to offset for the pillar-boxed game area).
    - `ScreenSpaceConstraintsNode` (`CCS.Debug.Viewport.ScreenSpaceConstraints`) �?pink sphere in 3D at the constrained actor, PLUS the same two-branch 2D safe-zone overlay.
- **Two debug pipelines, one service.** The viewport debug now registers TWO UDebugDrawService hooks: (a) a 3D hook on the world LineBatcher (ticker-driven, fires in both PIE possessed and F8 eject via the always-running FTSTicker path); (b) a 2D hook on the "Game" show flag channel (fires during `UGameViewportClient::Draw`, so PIE possessed + standalone only, NOT F8 eject). Nodes implement `DrawNodeDebug(UWorld*, bool)` for the 3D path and `DrawNodeDebug2D(UCanvas*, APlayerController*)` for the 2D path. Both gated by the same per-node CVar so one toggle covers both views of the same node.
- **Intentionally without gizmos** (documented in headers): `ComputeDistanceToActorNode` (compute-chain, only runs at BeginPlay), `ControlRotateNode` / `AutoRotateNode` (pure rotation state, no clean spatial primitive), `CameraOffsetNode` (offset is derivative of the camera pose itself �?the frustum already shows the result), `ImpulseResolutionNode` (impulse shapes are actors with their own collision visuals), `RotationConstraints` (yaw/pitch range cones hard to draw cleanly), `ViewTargetProxyNode` (internal `Hidden` proxy), `MixingCameraNode` (multi-camera blend �?no single spatial anchor), `LensNode` / `FilmbackNode` / `FieldOfViewNode` / `OrthographicNode` / `PostProcessNode` (pure camera-parameter nodes �?frustum already reflects their effect).
- **Legacy flag removed.** The old `AComposableCameraPlayerCameraManager::bDrawDebugInformation` UPROPERTY is deleted. It gated pre-framework debug draws inside a handful of nodes and transitions; all usages have migrated to `DrawNodeDebug` (nodes) or `DrawTransitionDebug` (transitions �?see next bullet).
- **Per-transition gizmos** have a parallel framework to per-node ones:
    - `UComposableCameraTransitionBase::DrawTransitionDebug(UWorld*, bool bViewerIsOutsideCamera) const` �?virtual with empty default, fires every frame for every transition currently living in the eval tree. Each concrete transition's override self-gates on its own `CCS.Debug.Viewport.Transitions.<Name>` CVar (Linear / Smooth / Ease / Cubic / Inertialized / Cylindrical / Spline / PathGuided / DynamicDeocclusion).
    - Pose snapshots for the override's consumption are cached on the transition by the one call site that sees all three at once: `FComposableCameraEvaluationTreeInnerNodeWrapper::Evaluate` writes `LastDebugSourcePose` / `LastDebugTargetPose` / `LastDebugBlendedPose` under `#if !UE_BUILD_SHIPPING`. Value-type copies; no UPROPERTY; shipping builds strip them entirely.
    - Tree walking happens through `UComposableCameraEvaluationTree::DrawTransitionsDebug(UWorld*, bool)`, a DFS that recurses into reference-leaves so inter-context blends also surface the source context's in-flight intra-context transitions. The viewport ticker invokes this once per PCM via `PCM->GetContextStack()->GetActiveDirector()->GetEvaluationTree()->DrawTransitionsDebug(...)`.
    - Standard visualization lives on a shared base-class helper `DrawStandardTransitionDebug(World, bViewerIsOutsideCamera, AccentColor)` �?green source sphere + blue target sphere + accent progress sphere at the blended position, plus F8/SIE-only half-scale source/target frustums. The helper is deliberately silent about the PATH between source and target �?that's per-transition-type (see next bullet).
    - Each transition draws its own PATH polyline in accent color on top of the standard markers: Linear/Smooth/Ease/Cubic use a straight `DrawDebugLine` (they lerp position linearly in space �?only the timing curve differs), Cylindrical samples 32 points along the cylindrical arc via a static `SampleCylindricalPathPosition` helper, Inertialized pre-samples 33 target-relative offsets at OnBeginPlay (`DebugPathOffsets`) and adds the live target position at draw time, Spline reuses `EvaluatePositionOnCurve(t, start, end)`, PathGuided samples the rail spline 32 times, DynamicDeocclusion draws only feeler rays (its path is shaped by accumulating dynamic trace offsets and is not predictable without actually running the traces).
    - `ViewTargetTransition` (the `Hidden, NotBlueprintable` engine SetViewTarget bridge) is deliberately not exposed �?no user-authored instance to debug, a CVar would just be clutter.
    - Gizmos disappear the moment the transition's Inner node collapses (blend finished), which is exactly when the debug data stops being meaningful.
- **Legacy HUD overlays removed.** `ScreenSpacePivotNode` and `ScreenSpaceConstraintsNode` used to register a `HUD->OnHUDPostRender` lambda to paint a 2D safe-zone rectangle + projected-pivot marker. That entire path (the lambda, the `FDelegateHandle DrawDebugHandle` member, the `DrawDebugInfo` function, the `BeginDestroy` override) is gone �?2D screen-space overlays belong to a future 2D debug panel, not to per-node gizmos.

This is the "if it feels wrong but I don't know why" debugger: enable `CCS.Debug.Viewport 1`, fly the editor camera outside the player's head, and see where the camera's actually pointing and what each node is doing in world space. The panel tells you *which* nodes ran; the viewport draw tells you *where their output lives*.

### Editor Debug Snapshot

`AComposableCameraCameraBase` also exposes `SnapshotDebugState()` (`#if WITH_EDITOR`), which produces an `FComposableCameraDebugSnapshot` containing per-node active state, the camera pose after each node, and formatted output pin values. This is consumed by the Camera Type Asset editor's debug ticker during PIE to drive visual overlays on the graph. See EditorDesignDoc Section 20 for the full editor-side architecture.

---

## 21. Testing

Tests live in `Private/Tests/` and use UE's automation test framework:

- **EvaluationTreeTests**: Tree building, collapse logic (chained collapse, source-destroyed collapse), multi-level transitions
- **BugFixTests**: Regression tests for specific fixes (SmoothStep/SmootherStep correctness, null-safety guards)
- **ExecutionMockTests**: End-to-end scenario mocks (gameplay→cutscene flow, rapid switches, cut-interrupts-transition, nested transitions, blend verification, inter-context transitions, empty tree safety)

---

## Appendix A: Common Patterns

### Activating a Camera from Blueprint (via K2 Node)

The custom K2 node `UK2Node_ActivateComposableCamera` is placed in a Blueprint graph.
The designer picks a Camera Type Asset, and the K2 node dynamically generates typed
pins for each exposed parameter and exposed variable. At compile time, the node expands
into a call to `ActivateComposableCameraFromTypeAsset` with the parameter block populated.

```
Activate Composable Camera
  Camera Type Asset: ThirdPersonCamera
  Context Name: "Gameplay"          // auto-ensures this context
  Transition Override: (none)       // uses type asset's default transition
  // Activation Params (struct pin):
  bPreserveCameraPose: true
  bIsTransient: false
  // Exposed params shown as typed pins:
  FollowActor: PlayerPawnRef
  ArmLength: 300.0
```

### Pushing a Cinematic Context

```
Activate Composable Camera
  Camera Type Asset: CinematicCamera
  Context Name: "Cinematic"         // pushes new context on top
  Transition Override: InertializedTransitionAsset
  bIsTransient: true
  LifeTime: 5.0
// After 5 seconds: auto-pop back to Gameplay with default transition
```

### Returning from Cinematic

Automatic (transient camera expires) or manual:
```
TerminateCurrentCamera(
  TransitionOverride: FadeTransitionAsset
)
```

---

## Appendix B: Composable Camera Editor

The visual editor is the primary design surface for camera types. The full editor design is documented in **[EditorDesignDoc.md](EditorDesignDoc.md)**.

Key concepts:

- **Camera Type Assets** (`UComposableCameraTypeAsset`): Data assets defining camera node composition, exposed parameters, internal / exposed variables, and default transitions. This is the only supported way to define cameras. Type assets maintain two execution order structures: `ComputeExecutionOrder` (linear order of compute nodes) and `ComputeFullExecChain` (hierarchical structure interleaving compute nodes with `SetVariable` writes). At runtime, `BeginPlayCamera` and `TickCamera` prefer `FullExecChain` when available (supporting interleaved variable dispatch), falling back to linear array walks for legacy type-asset data saved before the exec chain was introduced.
- **Visual Node Graph**: Linear-chain node editor with typed data pins, forward-only wiring, and visible data flow between nodes.
- **Four-category Parameter System** �?the author-time taxonomy exposes four distinct value kinds that all share the same runtime `FComposableCameraRuntimeDataBlock`:
  - **Wired data pins** �?inter-node values. Producers are other nodes in the chain; the layout step assigns each output pin a slot and each connected input reads from that slot.
  - **Exposed parameters** �?caller-facing, one-shot. The K2 node and DataTable row both present them as input pins; their value lands in the `ParameterBlock` at activation and is copied into the slot once. Not readable or writable from inside the graph at runtime (they're a one-shot input, not a persistent variable).
  - **Internal variables** �?camera-level persistent slots that are *only* readable / writable from inside the graph via Get / Set variable nodes. The caller cannot reach them; their initial value comes from the variable's `InitialValueString`, applied at activation.
  - **Exposed variables** �?identical to internal variables at runtime (same slot map, same Get / Set node behavior), but the caller *may* override the initial value at activation time through the same `ParameterBlock` keyspace used by exposed parameters. After activation they remain writable from inside the graph like any internal variable. This is the natural middle ground: "node-only state that the author also wants to expose as an activation-time knob."
  - Name uniqueness is enforced across the union of exposed parameters, internal variables, and exposed variables, since all three share the runtime's `FName` keyspace inside the data block / ParameterBlock. See `UComposableCameraTypeAsset::Build()`.
- **Custom K2 Node**: `UK2Node_ActivateComposableCamera` dynamically generates Blueprint pins matching the selected camera type's exposed parameters AND exposed variables (both flow through the same `DynamicParameterPinNames` array and the same `SetParameterBlockValue` expansion path, so the author sees one unified set of "activation-time knobs").
- **DataTable Integration**: Camera parameters and exposed variables can be stored in DataTable rows for AI and data-driven camera selection. Rows use `FComposableCameraParameterTableRow` (a fixed USTRUCT carrying a `TMap<FName, FString>` of serialized values) and are activated via `UComposableCameraBlueprintLibrary::ActivateComposableCameraFromDataTable`. Both exposed parameters and exposed variables share the row's keyspace �?the runtime walks both sets at activation, routing each value into the correct slot. The shared string→typed-value parser lives at `FComposableCameraParameterBlock::ApplyStringValue` and is reused by the editor-side row customization so anything typed in the editor round-trips identically at runtime. See **[EditorDesignDoc.md §12](EditorDesignDoc.md)** for the full layering.

Actor-source resolution (`EComposableCameraActorInputSource::ControllerControlledPawn`) falls back from PCM ownership to the node's World and that World's first `PlayerController` when a camera evaluates without an `AComposableCameraPlayerCameraManager` (Level Sequence / PIE preview evaluators, Patch evaluators that call `Evaluator->Initialize(nullptr)`, etc.). Resolution walks `WorldContextObject->GetWorld()`, then the outer Actor's World, then PCM World, so UObject node subobjects whose direct World is null still resolve. Actor-source nodes such as `FocusPull`, `HitchcockZoom`, `LookAt`, `CollisionPush`, `ScreenSpacePivot`, and `Spiral` therefore reach the gameplay pawn in PIE regardless of whether the evaluator is LS-owned, Patch-owned, or PCM-owned.

## 2026-05-13 DoF Focus-Distance Blend Guard

Post-process layering remains: CameraComponent baseline -> pose PostProcessSettings -> physical camera settings. In the physical layer, PhysicalCameraBlendWeight still gates DoF numeric fields and ExposureBlendWeight gates ISO/Shutter exposure fields. Special case: DepthOfFieldFocalDistance = 0 is Unreal's disabled/invalid DoF sentinel, so ApplyPhysicalCameraSettings must not linearly interpolate from 0 toward a valid FocusDistance during a transition. When the current PP focal distance or target focal distance is 0, copy the target value instead; this avoids a near-zero focal plane and the transient full-screen blur spike. Other DoF fields continue to scale by PhysicalCameraBlendWeight.

## 2026-05-13 DoF Strength Fade

PhysicalCameraBlendWeight is also routed to FPostProcessSettings::DepthOfFieldScale. Focal distance uses the 0-sentinel guard above, but visible DoF strength must still fade through DepthOfFieldScale = PhysicalCameraBlendWeight when no earlier post-process layer already overrides that field. This gives transitions the intended behavior: focus plane is valid immediately, blur strength ramps from 0 to 1.

## 2026-05-13 Physical Exposure Ownership

GameplayCameras and UCineCameraComponent treat aperture as both a DoF input and a physical-exposure input. UE's renderer computes physical EV from DepthOfFieldFstop, CameraShutterSpeed, and CameraISO when AutoExposureApplyPhysicalCameraExposure is enabled. CCS preserves ownership of that enable flag: ApplyPhysicalCameraSettings does not turn AutoExposureApplyPhysicalCameraExposure on or off. It writes DoF fields from PhysicalCameraBlendWeight and ISO/Shutter from ExposureBlendWeight, and does not compensate ISO. If physical exposure is already enabled by the component baseline or a PostProcessNode, Lens-driven f-stop changes can affect brightness by UE design.