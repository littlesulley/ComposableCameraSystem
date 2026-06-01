# Shot-Based Keyframing

Updated: 2026-06-01

This document describes the current shot authoring and composition solver path.
Old phase plans and shipped task lists were removed. Runtime context lives in
`DesignDoc.md`; implementation details live in `TechDoc.md`.

## 1. Purpose

Shot-based keyframing lets designers author camera intent instead of raw
keyframed transform curves.

A shot describes:

- who matters.
- where the camera should be placed.
- what it should aim at.
- how much of the target set should fit in frame.
- focus and roll behavior.

The solver converts that intent into `FComposableCameraPose`.

## 2. Main Types

Runtime:

- `FComposableCameraTargetInfo`
- `FComposableCameraShotTarget`
- `FComposableCameraShot`
- `FComposableCameraShotSolveParams`
- `FComposableCameraShotSolveResult`
- `UComposableCameraShotSolver`
- `UComposableCameraCompositionFramingNode`
- `UComposableCameraShotAsset`

Sequencer:

- `UMovieSceneComposableCameraShotTrack`
- `UMovieSceneComposableCameraShotSection`
- `UMovieSceneComposableCameraShotTrackInstance`
- `UComposableCameraLevelSequenceComponent`

Editor:

- Shot Editor nomad tab.
- shot section customization.
- shot track editor.
- target info customization.
- viewport overlays and handles.

## 3. Data Layers

### 3.1 Target Info

`FComposableCameraTargetInfo` resolves one pivot target.

Data includes:

- soft actor reference.
- optional bone/socket name.
- local/world offset.
- preview mesh and preview transform for editor.
- skeletal mesh basis options.

Target info is shared by runtime nodes, shot solver, shot editor, and Sequencer
target overrides.

### 3.2 Shot Target

`FComposableCameraShotTarget` adds framing metadata to a target.

Data includes:

- target info.
- bounds mode.
- manual bounds.
- auto-bounds cache policy.
- weight.

Bounds cache policies:

- static.
- periodic.
- live.

### 3.3 Shot

`FComposableCameraShot` owns the full composition request.

Major groups:

- target array.
- anchor spec.
- placement.
- aim.
- lens.
- focus.
- roll.
- framing zones.

Common modes:

- Anchor: single target, weighted world centroid, fixed world position.
- Placement: anchor orbit, anchor at screen, fixed world position.
- Aim: look at anchor, no-op.
- FOV: manual, solved from bounds fit.
- Focus: manual, follow placement, follow aim, follow custom.
- Basis: world, inherit from actor.

## 4. Solver Pipeline

```text
input pose + shot + targets
  -> resolve target pivots and bounds
  -> anchor
  -> placement
  -> aim
  -> lens/FOV
  -> focus
  -> roll
  -> output pose
```

The solver is mostly closed-form. It uses prior pose/state for framing-zone
damping and continuity.

Hard rules:

- invalid targets must fail gracefully.
- output pose must stay finite.
- lens/focus/roll changes must not be hidden by transform solve failures unless
  the caller explicitly chooses to hold pose.
- target bounds cannot mutate shared asset data during evaluation.

## 5. Framing Zones

Screen-space zones support dead/soft behavior and damping.

They help avoid camera jitter when a target moves inside an acceptable region.
Zone logic uses prior solve state. First frame of a new shot section must reseed
state so it does not inherit stale screen-position history.

## 6. Composition Framing Node

`UComposableCameraCompositionFramingNode` consumes `FComposableCameraShot`.

Behavior:

- no normal graph pins for shot fields.
- overwrites pose from the solver output.
- writes FOV / physical lens fields.
- writes focus fields.
- supports Sequencer shot overrides.
- supports two-shot blending during overlaps.
- not compatible with patch graphs.

Sequencer calls `SetActiveShotsFromSequencer` with:

- primary shot.
- optional secondary shot.
- optional incoming transition.
- overlap alpha.
- section identity-change flags.

The node handles solver state continuity across cuts, overlaps, and handoffs.

## 7. Shot Storage

Shot sections support two source modes:

- Inline: section owns `InlineShot`.
- AssetReference: section references `UComposableCameraShotAsset` and owns
  section-local `ShotOverrides`.

Important rule: editing an AssetReference section edits `ShotOverrides`, not the
shared shot asset.

`TargetActorOverrides` map shot target indices to Sequencer bindings. Runtime
builds an effective shot copy each evaluation frame. The section or referenced
asset is not mutated.

## 8. Sequencer Evaluation

Shot track instance:

1. Finds in-range shot sections.
2. Resolves bound `AComposableCameraLevelSequenceActor`.
3. Builds effective shot per section.
4. Computes row order and overlap alpha.
5. Sends shot entries to `UComposableCameraLevelSequenceComponent`.

LS component:

1. Reapplies type asset bags.
2. Applies active shot override to first `UComposableCameraCompositionFramingNode`.
3. Invalidates/re-evaluates on first section entry when needed.
4. Ticks internal camera.
5. Applies patch overlays.
6. Projects pose to CineCamera.

Overlap semantics:

- lowest row index is primary/outgoing.
- next row is secondary/incoming.
- incoming section's `EnterTransition` selects blend curve/math.
- overlap duration comes from the section overlap, not transition asset time.
- null transition means hard cut.

## 9. Shot Editor

Shot Editor is a single nomad tab that swaps context.

It can edit:

- a composition framing node's shot.
- a shot asset.
- a shot section inline data.
- a shot section asset-reference override copy.

Viewport/editor features include:

- target outliner.
- target preview meshes.
- bone/socket picker through target info customization.
- anchor and zone overlays.
- distance/roll controls.
- reverse-solve helpers for interactive handles.

Transactions must wrap edits. Preview state must not leak into runtime assets
unless the edited host is the asset itself.

## 10. Patch and Shot Interaction

Shot solve happens inside the internal camera tick or gameplay camera tick.

Patch overlays happen after the camera tick:

- runtime PCM path: director patch manager.
- Sequencer path: LS component overlay map.

Therefore patches see the shot-produced pose as upstream input.

## 11. Debugging

Useful surfaces:

- Shot Editor viewport overlays.
- runtime viewport gizmos.
- debug panel pose and patch rows.
- Sequencer section painter overlap visualization.
- automation tests in `ComposableCameraShotSolverTests.cpp`.

Codex does not run those automation tests from shell. Run them in Unreal/Rider
when verifying code changes.

## 12. Current Limitations

- Only the first composition framing node on an LS internal camera receives
  shot overrides.
- Actor references in shot DataTable/string paths are not supported.
- Asset soft references should be cached before evaluation; eval path should not
  block-load.
- Object/Actor pin class mismatch diagnostics could be earlier in the graph
  layout step.

## 13. Maintenance

Update this file when changing:

- shot structs.
- solver modes.
- composition framing node behavior.
- shot editor UX.
- Sequencer shot section or track behavior.
- target info customization.
- overlap/transition semantics.
