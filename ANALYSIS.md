# ComposableCameraSystem — Code Analysis

> Auto-updated by Claude Code on each commit. Last reviewed: 2026-03-20.

---

## How It Works

### Core Architecture

The system replaces Unreal's standard `APlayerCameraManager` with `AComposableCameraPlayerCamaraManager`, which drives the whole pipeline. Each frame, `DoUpdateCamera()` calls through:

```
PlayerCameraManager → Director → EvaluationTree → CameraBase → [Nodes in order]
```

**Camera definition:** A `AComposableCameraCameraBase` (inherits `ACameraActor`) holds an ordered array of `UComposableCameraCameraNodeBase*` and a reference to a `UComposableCameraVariableCollection` data asset. Each tick, nodes execute sequentially — each receives the `FComposableCameraPose` (position, rotation, FOV) produced by the previous node and outputs a modified one.

**Context variable system:** The key composability mechanism. Nodes have two property categories:
- `InputParameters` — local settings (e.g., damping speed, offset amount)
- `ContextParameters` — `F*ComposableCameraContextParameter` structs that bind to named typed variables (`FComposableCameraVariableID`) in the camera's `ContextVariables` collection

This lets one node write a pivot position into `ContextPivotPosition`, and the next node read it — without nodes having any knowledge of each other. The `FComposableCameraVariableID` is derived at load time from the variable's GUID hash.

**Transition system:** On camera switch, the `Director` spawns the new camera deferred, duplicates the transition object, and registers both cameras into the `EvaluationTree`. The tree is structured as a binary tree of leaf nodes (running cameras) and inner nodes (transitions), allowing multiple overlapping transitions to coexist. Transitions implement `OnEvaluateBySource(source, target)` and decrement `RemainingTime` until `bFinished`.

**Modifiers:** Tagged `UComposableCameraNodeModifierDataAsset` assets can be added/removed at runtime. The `ModifierManager` maps `CameraTag → NodeClass → highest-priority modifier`. When modifiers change, the current camera is re-instantiated via `ReactivateCurrentCamera` to pick up the new node overrides.

**Actions:** `UComposableCameraActionBase` are per-frame hooks that bind to pre/post camera tick. They support four expiration modes (Instant, Duration, Manual, Condition) and can be scoped to the current camera or global.

**Interpolators:** A template factory pattern — `UComposableCameraInterpolatorBase` creates `TUniquePtr<TCameraInterpolator<TValueTypeWrapper<T>>>` instances. The `TValueTypeWrapper<T>` provides arithmetic operators, with correct specializations for `FRotator` (normalized subtraction) and `FQuat` (quaternion multiplication). Implementations: IIR, SimpleSpring, SpringDamper.

---

### Module Breakdown

| Module | Type | Purpose |
|---|---|---|
| `ComposableCameraSystem` | Runtime | All gameplay camera classes |
| `ComposableCameraSystemEditor` | Editor | Asset editors, factories, customizations |
| `ComposableCameraSystemUncookedOnly` | UncookedOnly | Custom K2Nodes, Blueprint pin widgets |

### Key Classes

| Class | Role |
|---|---|
| `AComposableCameraPlayerCamaraManager` | Entry point; extends `APlayerCameraManager` |
| `UComposableCameraDirector` | Camera lifecycle — spawn, activate, resume |
| `UComposableCameraEvaluationTree` | Per-frame evaluation tree (leaf = camera, inner = transition) |
| `AComposableCameraCameraBase` | Camera actor; owns nodes + variable collection |
| `UComposableCameraCameraNodeBase` | Abstract base for all pipeline nodes |
| `UComposableCameraTransitionBase` | Abstract base for all camera transitions |
| `UComposableCameraModifierBase` | Runtime node parameter overrides, filtered by GameplayTag |
| `UComposableCameraActionBase` | Pre/post tick hooks with expiration logic |
| `UComposableCameraVariableCollection` | Data asset holding typed camera variables |
| `UComposableCameraInterpolatorBase` | Factory for typed `TCameraInterpolator<T>` instances |

### Provided Nodes

| Node | Purpose |
|---|---|
| `ReceivePivotActorNode` | Reads pivot actor location into a context variable |
| `PivotOffsetNode` | Applies offset to pivot position |
| `PivotDampingNode` | Directional interpolation on pivot (6 per-axis interpolators) |
| `CameraOffsetNode` | Offsets camera from pivot |
| `ControlRotateNode` | Reads player control rotation |
| `AutoRotateNode` | Auto-rotates toward pivot |
| `LookAtNode` | Aims camera at pivot |
| `CollisionPushNode` | Resolves collision (trace + self-sphere) |
| `FieldOfViewNode` | Sets FOV |
| `RelativeFixedPoseNode` | Fixed camera pose relative to a transform or actor |
| `ScreenSpacePivotNode` / `ScreenSpaceConstraintsNode` | Screen-space positioning |
| `RotationConstraints` | Clamps rotation ranges |
| `SplineNode` | Camera along a spline path |
| `KeyframeSequenceNode` | Animated camera sequence |
| `ImpulseResolutionNode` | Handles camera impulse events |
| `MixingCameraNode` | Weighted blend of multiple cameras (L1/L2/SoftMax normalization) |

### Provided Transitions

Linear, Ease, Smooth, Cubic, Cylindrical, Inertialized (physics-based), PathGuided, Spline, DynamicDeocclusion.

---

## Known Bugs

### 1. Dead code — transient cameras never auto-terminate

**File:** `Source/ComposableCameraSystem/Private/Core/ComposableCameraEvaluationTree.cpp`
**Lines:** 71–100

The new tree-based `Evaluate()` has an early `return ResultPose` on line 71, making the remaining lines unreachable. That dead block contains the `RunningCamera->IsFinished()` check that was supposed to auto-terminate transient cameras. Cameras with a configured `LifeTime` will never self-destruct.

### 2. `EvaluationTree` array is always empty → guaranteed crash

**File:** `Source/ComposableCameraSystem/Private/Core/ComposableCameraEvaluationTree.cpp`

`GetRootNode()` asserts `EvaluationTree.Num() > 0`, but `OnActivateNewCamera()` only sets the legacy `RunningCamera` / `Transition` members — it never populates `EvaluationTree`. The first `Evaluate()` call will always crash. The tree-based refactor is incomplete.

### 3. Dangling references in `FComposableCameraEvaluationTreeInnerNodeWrapper`

**File:** `Source/ComposableCameraSystem/Public/Core/ComposableCameraEvaluationTree.h`

```cpp
FComposableCameraEvaluationTreeNode& LeftNode;
FComposableCameraEvaluationTreeNode& RightNode;
```

These are C++ references into `TArray<FComposableCameraEvaluationTreeNode>`. Any `Add()`, `Reserve()`, or reallocation will invalidate these references. Use indices or stable pointers instead.

### 4. Null dereference in `UActorComposableCameraVariable::FormatDefaultValue()`

**File:** `Source/ComposableCameraSystem/Public/Variables/ComposableCameraVariable.h`

```cpp
virtual FString FormatDefaultValue() const override { return LexToString(DefaultValue->GetName()); }
```

`DefaultValue` is `AActor* DefaultValue { nullptr }` by default. This crashes unconditionally when no actor is assigned.

### 5. Typo: `bCanExecuteManul`

**File:** `Source/ComposableCameraSystem/Public/Actions/ComposableCameraActionBase.h:100`

Should be `bCanExecuteManual`.

---

## Improvement Suggestions

### 1. Fix rotation blending in `FComposableCameraPose::BlendBy()`

Current component-wise Euler lerp can misbehave near gimbal lock. Replace with quaternion slerp:

```cpp
// Current (problematic)
const FRotator DeltaAng = (Other.Rotation - Rotation).GetNormalized();
Rotation = (Rotation + OtherWeight * DeltaAng).GetNormalized();

// Better
Rotation = FQuat::Slerp(Rotation.Quaternion(), Other.Rotation.Quaternion(), OtherWeight).Rotator();
```

### 2. Variable lookup is O(n) per tick

`UComposableCameraVariableCollection::FindValue()` linearly scans all variables every time a node reads a context parameter. Build a `TMap<FComposableCameraVariableID, UComposableCameraVariable*>` at `PostLoad` for O(1) lookup.

### 3. Class name typo: `CamaraManager`

`AComposableCameraPlayerCamaraManager` is missing an 'e'. Add a redirector in `DefaultEngine.ini` before the API is shipped to users.

### 4. Implement or remove `PreNodeTick` / `PostNodeTick` action execution types

`EComposableCameraActionExecutionType::PreNodeTick` and `PostNodeTick` are exposed in Blueprint but marked `@TODO: Currently not implemented`. Remove them from the enum or implement them using the existing `OnPreTick`/`OnPostTick` node virtuals.

### 5. `FComposableCameraVariableID` INVALID sentinel can collide with a real hash

`INVALID = uint32(-1)` (`0xFFFFFFFF`) is a valid output of `HashCombineFast`. A variable whose GUID hashes to this value will silently never resolve. Use a separate `bIsValid` flag or `TOptional<uint32>`.

### 6. `UComposableCameraModifierBase::ApplyModifier` has no C++ path

It's a `BlueprintImplementableEvent` with no `_Implementation`. C++ modifier subclasses cannot override logic without going through Blueprint. Change to `BlueprintNativeEvent`.

### 7. `ContextVariables` soft pointer can silently return null

```cpp
TSoftObjectPtr<UComposableCameraVariableCollection> ContextVariables;
```

If the asset isn't loaded when a node tries to read it, context lookups silently return default values. Switch to a hard `TObjectPtr<>` — the collection is needed every tick and should always be loaded.

### 8. `MixingCameraNode` weight update binding is fragile

The node requires external code to bind `FOnReceiveMixingCameraWeights` before the first tick. There is no fallback if nobody binds it. Expose a `BlueprintNativeEvent GetMixingWeights()` that camera subclasses can implement directly, eliminating the external binding ceremony.

### 9. Silent camera chain truncation

`RefreshCameraChain()` caps the parent camera chain length at 3 with no user-visible warning. Add a `UE_LOG(LogComposableCameraSystem, Warning, ...)` so designers know when cameras are silently dropped.
