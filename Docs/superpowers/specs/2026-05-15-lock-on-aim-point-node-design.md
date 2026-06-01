# Lock-On Aim Point Node Design Archive

Updated: 2026-06-01

Status: implemented.

Current source:

- `Source/ComposableCameraSystem/Public/Nodes/ComposableCameraLockOnAimPointNode.h`
- `Source/ComposableCameraSystem/Private/Nodes/ComposableCameraLockOnAimPointNode.cpp`
- `Source/ComposableCameraSystem/Public/Math/ComposableCameraLockOnAimPoint.h`
- `Source/ComposableCameraSystem/Private/Math/ComposableCameraLockOnAimPoint.cpp`
- `Source/ComposableCameraSystem/Private/Tests/ComposableCameraLockOnAimPointNodeTests.cpp`

## Purpose

`UComposableCameraLockOnAimPointNode` builds a stable virtual aim point for
lock-on composition.

Recommended chain:

```text
ScreenSpacePivot(follow)
  -> LockOnAimPoint
  -> ScreenSpacePivot(aim)
```

The node writes output pin `PivotPosition`.

## Inputs

Follow side:

- `FollowSource`: world position or actor position.
- `FollowActorSource`: explicit actor or controlled pawn.
- `FollowWorldPosition`.
- `FollowActor`.
- `FollowWorldUpOffset`.

Aim side:

- `AimSource`: world position or actor position.
- `AimActorSource`: explicit actor or controlled pawn.
- `AimWorldPosition`.
- `AimActor`.
- `AimWorldUpOffset`.

Solve controls:

- `Radius`.
- `PitchRange`.
- `BlendOutTime`.
- `Weights` for pitch, camera-to-aim, and camera-forward additions.

## Algorithm

`ComputeLockOnAimPoint`:

1. Resolve follow and aim positions.
2. If radius is invalid or aim is outside radius in 2D, blend out existing
   correction and return raw aim plus remaining correction.
3. Compute pitch-preserving addition.
4. Compute camera-to-aim addition.
5. Compute camera-forward addition.
6. Weighted-sum additions.
7. Store correction state.
8. Return `AimPosition + CurrentAddition`.

State supports smooth blend-out when the target leaves the correction radius.

## Edge Cases

- Degenerate planar direction falls back to camera forward, then world forward.
- Pitch clamps to safe range.
- Zero/near-zero radius disables correction and blends out.
- Quadratic failures return zero addition, not invalid vectors.
- Result must remain finite.

## Debug

Console variable:

```text
CCS.Debug.Viewport.LockOnAimPoint
```

Requires:

```text
CCS.Debug.Viewport 1
```

Debug draw shows the stable aim point and correction line when viewed outside
the camera.

## Tests

Automation coverage exists for:

- outside radius returns raw aim.
- pitch addition enforces planar radius.
- current pitch inside range.
- clamped pitch outside range.
- degenerate overlap stays finite.
- outside radius blends out existing offset.
- node writes stable pivot output.

Run tests from Unreal/Rider. Codex must not run Unreal automation from shell in
this project.
