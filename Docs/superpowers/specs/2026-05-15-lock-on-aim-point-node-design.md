# Lock-On Aim Point Node Design

Date: 2026-05-15

## Context

The current lock-on camera can be built from two `ScreenSpacePivot` nodes:

1. The first frames the player/follow pivot.
2. The second frames the lock target/aim pivot.

When the player pivot and lock target pivot become close in horizontal projection, the second screen-space solve becomes highly sensitive. This can produce very fast camera rotation because the target screen offset is being enforced against a nearly degenerate player-to-target relationship.

The legacy `Private_ComponentCameraSystem_v5.6` implementation handled this through `UModifyAimPointExtension`. That extension did not move the target actor. It computed an `AdditionalAimOffset` every frame, and `UTargetingAim::GetRealAimPosition(true)` returned the raw aim point plus that offset. In the composable graph, the equivalent should be a node that outputs an effective world-space aim point as an `FVector`.

Reference article: https://sulley.cc/2023/10/24/22/31/

## Goal

Add a node that constructs a stable, virtual aim position for lock-on camera composition. The node must work with the existing two-`ScreenSpacePivot` chain instead of replacing it.

The node output is a single `FVector`, named `PivotPosition` for consistency with existing pivot-producing nodes. This output is wired into the second `ScreenSpacePivot` as `PivotWorldPosition`.

## Recommended Chain

```text
Player pivot:
ReceivePivotActor(Player)
  -> PivotOffset(Player chest/head)
  -> ScreenSpacePivot #1

Target pivot:
ReceivePivotActor(LockTarget)
  -> PivotOffset(Target chest/head)
  -> LockOnAimPointNode
  -> ScreenSpacePivot #2
```

`LockOnAimPointNode` consumes the player pivot, raw target pivot, and current camera pose. It outputs the stable target pivot used by the second `ScreenSpacePivot`.

## Node API

Proposed class name:

```text
UComposableCameraLockOnAimPointNode
```

Palette category:

```text
Pivot
```

Inputs:

- `FollowSource` / `AimSource` (`Enum`): whether each point is a world-space position or actor-derived position.
- `FollowWorldPosition` / `AimWorldPosition` (`Vector3D`): world-space fallback or authored points.
- `FollowActorSource` / `AimActorSource`, `FollowActor` / `AimActor`, and world-up offsets: actor-based point resolution, matching the other camera nodes that use `Source` inputs.
- `Radius` (`Float`, details by default, default `500.0`): minimum horizontal projected distance before correction is active.
- `PitchRange` (`Vector2D`, details by default, default `(-45.0, 45.0)`): min/max pitch in degrees used by `PitchAddition`. Current pitch inside this range is used directly; outside the range it is clamped to the nearest boundary.
- `BlendOutTime` (`Float`, details by default, default `0.15`): seconds used to fade the correction offset to zero after leaving `Radius`.
- `Weights` (`Vector3D`, details by default, default `(0.2, 0.5, 0.3)`): blend weights for pitch-preserving, camera-to-aim, and camera-forward additions. The values are used directly, matching the legacy extension, rather than normalized.

Output:

- `PivotPosition` (`Vector3D`): stable virtual aim point in world space.

Runtime-only state:

- `bInModify`: whether the node is currently inside the close-distance correction region.
- `CurrentAddition`: the most recent blended correction offset, reused for blend-out after leaving the correction region.
- `LastOutputPivotPosition`: non-shipping debug mirror for the output point.

## Algorithm

Each tick:

1. Read `FollowPosition` and `AimPosition`.
2. Compute planar distance between the two positions.
3. If planar distance is greater than or equal to `Radius`, clear `bInModify` and blend the previous correction offset back to zero over `BlendOutTime`.
4. If planar distance is below `Radius`, compute three candidate offsets:
   - `PitchAddition`: uses the current follow-to-aim pitch if it is inside `PitchRange`, or clamps to the nearest pitch boundary while enforcing the minimum planar distance.
   - `CamToAimAddition`: extends along the current camera-to-aim direction, preserving screen-space feel as much as possible.
   - `CamForwardAddition`: extends along the camera forward vector projected onto the horizontal plane.
5. Blend the offsets with `Weights`.
6. Output `AimPosition + BlendedAddition` as `PivotPosition`.

The camera position and forward vector are read from the accumulated `OutCameraPose` passed to this node, so the node should be placed after the first `ScreenSpacePivot` if it needs to account for the player framing pass.

This matches the legacy extension's behavior while adapting it to the composable graph model: the stable aim point is data flowing between nodes rather than mutable state on an aim component.

## Edge Cases

- If `Radius <= 0`, treat it like leaving the correction region and blend any existing correction offset back to raw `AimPosition`.
- If the raw horizontal direction is nearly zero, use the camera forward projected onto the horizontal plane as a fallback direction.
- If a quadratic solve has no valid solution or a direction cannot be normalized, treat that candidate offset as zero.
- Clamp unsafe pitch math and quadratic coefficient `A` to avoid division by zero.
- The node does not mutate actors, camera rotation, or the second `ScreenSpacePivot`; it only produces a world-space point.

## Debugging

Add a non-shipping viewport gizmo controlled by:

```text
CCS.Debug.Viewport.LockOnAimPoint
```

The gizmo draws a sphere at the stable output point. If useful during implementation, it can also draw a line from raw aim to stable aim when the viewer is outside the camera.

## Testing

Add focused automation tests for the math behavior:

- Outside `Radius`, output equals raw `AimPosition`.
- Inside `Radius` with `Weights=(1,0,0)`, output planar distance from `FollowPosition` is within a small floating-point tolerance of `Radius`.
- Inside `Radius`, current pitch within `PitchRange` is used directly, while pitch outside `PitchRange` is clamped to the configured boundary.
- Leaving `Radius` blends the previous correction offset to zero instead of snapping to the raw aim position.
- Degenerate follow/aim overlap does not produce NaN or invalid vectors.

The tests should call a small pure helper where possible so the math is testable without requiring a full camera actor setup.
