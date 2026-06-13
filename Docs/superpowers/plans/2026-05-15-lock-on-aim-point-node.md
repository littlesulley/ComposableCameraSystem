# Lock-On Aim Point Node Plan Archive

Updated: 2026-06-01

Status: implemented. This file is now an archive summary, not an active plan.

## Delivered Files

- `Public/Math/ComposableCameraLockOnAimPoint.h`
- `Private/Math/ComposableCameraLockOnAimPoint.cpp`
- `Public/Nodes/ComposableCameraLockOnAimPointNode.h`
- `Private/Nodes/ComposableCameraLockOnAimPointNode.cpp`
- `Private/Tests/ComposableCameraLockOnAimPointNodeTests.cpp`

## Delivered Behavior

- Pure math helper computes stable aim point.
- Node resolves follow and aim points from world positions or actors.
- Explicit actor and controlled-pawn actor sources are supported.
- Node outputs `PivotPosition`.
- Correction activates inside `Radius`.
- Correction blends out over `BlendOutTime`.
- Debug gizmo available through `CCS.Debug.Viewport.LockOnAimPoint`.

## Verification Surface

Automation tests cover math and node output behavior. Test names live under:

```text
System.Engine.ComposableCameraSystem.Nodes.LockOnAimPoint.*
```

Codex must not run Unreal automation from shell for this project. Use
Rider/Visual Studio or Unreal Editor.

## Current Maintenance Rule

When changing this node:

- update node catalog in `TechDoc.md`.
- update design archive if behavior changes.
- add or update focused automation tests.
- compile in IDE.
