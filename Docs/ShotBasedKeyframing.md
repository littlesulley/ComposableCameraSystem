# Shot-Based Keyframing

Updated: 2026-10-11

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
- optional named scene component (None preserves actor/auto-mesh behavior).
- optional bone/socket name.
- local/world offset.
- preview mesh, actor transform and relative mesh transform for editor.
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
- Basis: world, inherit from actor/component, horizontal two-target axis.

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

- subject role/component/bone controls in the authoring pane.
- target preview meshes.
- bone/socket picker through target info customization.
- anchor and zone overlays.
- distance/roll controls.
- reverse-solve helpers for interactive handles.
- single/pair/group templates and live numeric authoring.
- current-level/Sequencer preview plus isolated template preview.
- one-click labelled Sequence sections, subject bindings, Spawn coverage and
  Camera Cuts; three-clip dialogue sets and duplication.
- preset capture/restore preserving local actor roles.

The authoring session adapts reflected Shot fields on existing hosts and owns
an optional transient draft. It writes existing Shot data rather than adding a
second serialized camera description. Compose / Inspect label the Drag / Free
modes. Preview/Lock and shortcut 3 are removed. Advanced Details remain available.

The preview retains the original Shot Editor's interaction surface:

| Mode | Existing controls |
|---|---|
| Compose (1) | Solver camera, visible screen anchors/zones, wheel distance, Alt+right-drag Roll |
| Inspect (2) | Native orbit/pan/dolly, Reset, Alt+right-drag Roll on editable sources; leave with Save / Discard / Stay |

Lens, focus and authored Roll remain live in Inspect. Save reverse-solves the
inspected camera into Shot parameters. Shift mirrors dragged zone edges;
Ctrl/Shift refine/accelerate supported distance and guide gestures. HUD, Guides,
fit bounds, mesh previews, Ctrl+Alt+C view-transform copy, Ctrl+B browse, save and
Undo/Redo remain available. Read-only sections/sequences cannot author any field;
Inspect still navigates, and keyboard routing remains available in every mode.
Guide visibility follows the selected Follow/Aim/Subjects page as described below.

Task tabs are Create / Edit / Sequence / Presets / Advanced, above the parameter
and preview columns. A vertical divider adjusts their widths.
Edit navigation above the preview separates Follow, Aim, Lens & Focus,
Motion and Subjects. Follow exposes all three Placement modes and their complete
parameters; Aim exposes both Aim modes, anchors/zones and roll. Follow and Aim
place the Anchor group first, before behavior and screen-zone settings.
Lens & Focus exposes FOV limits, full aperture settings, four focus modes and a custom focus
anchor. Motion gathers depth/lens/roll and screen-zone response. Subjects exposes
pivot offset/space, orientation basis, bounds/cache policy and continuous weight;
Preview model holds reusable-asset mesh and transforms. Add Subject appends an
empty slot; Add Selected
Actors appends the level selection without replacing subjects or applying a
template. Both work from an empty draft, with one Undo per append action.
Existing weighted-anchor membership stays authored; add the new slot to those
anchor lists explicitly when it should contribute to their centroid. Native property
widgets edit the real Shot through the existing host NotifyHook. Flat native
black groups use lighter muted-gray headers and 24-unit native label/value rows.
All parameter groups and actions live in a scrollable left pane; Preview occupies
an independent right pane. A native divider adjusts the initial 40% / 60% widths.
Groups stack normally and can share two columns if the parameter pane is widened.
Compact 28-unit task buttons (14-unit icons, 9-point labels)
use the full camera teal for selection; 26-unit subtabs use a softer teal/gray
mix. Fixed 156 x 24 actions use native Unreal gray Button states.
Each whole Subject can collapse. Its actor picker and four groups stack
vertically; Component / pivot, Pivot / orientation, Framing bounds / weight and
Preview model always fill the same available row width. Component/bone and
preview-model options start collapsed. Whole-Subject/component fold state survives
refresh, and the up/down square buttons are removed. Motion removes its extra
Aim response title and two help lines while retaining response parameters.
Only whole-Subject headers keep the primary gray fill/bold text; all nested
foldouts use dark rounded native expandable rows and regular text, matching
the preview-transform foldouts. Create and Edit omit the final Swap actors A / B
button.
Presets navigation uses an open-folder icon. Numeric, Boolean and Enum commits
retain page/control widgets and scroll positions, including scalar Subject edits.
Subject headers expose a centered trash-icon Delete button with a tooltip,
also while folded. Add/Delete
retains existing role cards, action rows, page/scroll containers and folds;
native handles rebind in the same refresh tick after array changes. Delete is
one Undo/Redo step, removes weighted memberships and remaps surviving Anchor,
two-subject basis and Section binding indices. A direct reference to the deleted
slot becomes unresolved rather than selecting another subject. The last subject
can be deleted; Add remains available on the empty list. Shared presets stay
unchanged when deleting from a Section's local override copy.
Mode/basis/anchor-policy changes show/hide existing fields. Source/array changes,
external edits and history refresh handles while retaining page containers.
Centered Level Preview and Follow playhead buttons stay at navigation right.
One camera-frame/anchor icon identifies the tab, Tools entry, asset toolbar,
Sequencer action and ShotAsset thumbnail. Navigation is
view state only and preserves the current Shot, transactions and active source.

In Edit / Follow, AnchorOrbit displays a draggable latitude/longitude globe in
Preview's lower-left when Guides is on. Horizontal drag changes local yaw,
vertical drag changes pitch; the selected basis and distance remain intact.
Ctrl refines movement, Shift accelerates it. Wheel changes distance after releasing
any active gesture. One drag = one Undo; value editing retains parameter widgets.
Other task/subtabs hide the globe; Inspect and read-only sources disable
it. Small frames hide the control instead of drawing beyond the image.
The Follow Anchor has no separate 3D orbit-center point; the lower-left globe
and orbit/distance operations remain available.

Create and Edit / Subjects show Pivot/Bone points, Offset connections, coordinate
axes and configured bounds when Guides is on. Drag RGB axis endpoints to change
Offset in the selected world/local space. Set BoundsShape = ManualExtent and
drag a yellow face point to change its world-aligned half-extent; either face
resizes both sides about the pivot. Ctrl refines movement; Shift accelerates it.
Auto Bounds are displayed read-only. None removes the box. Bounds remain visible
on these pages at Manual FOV or zero weight, using gray for inactive fit inputs.
Subject names appear beside visible effective pivots, matching the parameter
page. Pivot/bone, offset-space and weight summaries remain hidden; XYZ axis
labels remain.
Inspect and locked sources disable input. One drag = one Undo; changing
page/source/frame invalidates stale hits, and parameter controls remain retained.

HUD remains opt-in and now shows Camera / Composition cards with headers and
label/value rows instead of plain text. Cards contain camera optics/pose, Follow
mode/distance, Aim resolution and screen drift, plus local orbit configuration.
Camera stacks above Composition at a fixed upper-left inset, independent of
Guide visibility and task selection. Card geometry, text and spacing shrink/grow uniformly with the
camera image (0.85 scale at 1280 x 720); every diagnostic row remains present.
Short images cap the entire stack above the possible Orbit panel.
The compact group stays within the image and above the orbit control.

For a paused, in-range Section, edits refresh only its registered component
override and evaluate the complete camera pipeline at zero delta. The viewport
reads the native CineCamera view. Inactive pinned sources use solver preview.
The preview frame surrounds the actual camera image with a six-unit dark screen
bezel, a thin outer highlight and an inner lip. The outer screen edge aligns right;
the image retains filmback/squeeze/crop aspect and vertical centering, with black
letterbox margins outside the bezel when needed. Both columns use the
full available height. Window resizing and divider dragging adapt the native
image to the right pane while preserving camera aspect; height-limited images
stay fitted. Left parameters scroll independently. Resize keeps source/widgets intact.
Camera templates supply inactive spawnable settings;
detached presets/drafts use the CineCamera default. Window size never changes
Shot framing aspect. Wheel and drag-release scalar commits retain the parameter
controls and remain undoable.
Follow playhead is opt-in and stops during gestures/Inspect; a missing explicit
binding does not substitute a different actor. Preview uses game-view visibility
locally, hiding editor collision shapes, selection and other scene helpers.
Guides controls the Shot overlays: Follow screen handle appears only in
AnchorAtScreen; Aim requires Edit / Aim selected and LookAtAnchor active. Orbit anchors remain configurable
without displaying an unused Follow screen marker.

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
- V1 still authors `FComposableCameraShot`. Generic native CineCamera/free-pose
  adapters and parameter keyframing/baking belong to later versions.
- Templates provide initial framing from subject bounds; shoulder occlusion and
  dialogue timing still need designer adjustment.

## 13. Maintenance

Update this file when changing:

- shot structs.
- solver modes.
- composition framing node behavior.
- shot editor UX.
- Sequencer shot section or track behavior.
- target info customization.
- overlap/transition semantics.
