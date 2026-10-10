# ComposableCameraSystem Editor Design

Updated: 2026-10-11

This document describes the current editor module. It replaces the old
phase-by-phase implementation plan. Runtime architecture lives in
`DesignDoc.md`. Implementation notes live in `TechDoc.md`.

## 1. Editor Goals

- Author camera type assets as node graphs.
- Keep the graph friendly, but store durable runtime data on the asset.
- Preserve node identity across save/load.
- Keep graph -> asset and asset -> graph round trips deterministic.
- Support gameplay activation, DataTable activation, patches, Level Sequence,
  and shot authoring from one data model.

## 2. Module Scope

Editor module: `Source/ComposableCameraSystemEditor`

Main areas:

- `Editors`: graph, schema, node classes, graph commands, graph debug.
- `Toolkits`: asset editor toolkit.
- `AssetTools`: asset definitions, track editors, thumbnails.
- `Factories`: type asset, patch asset, shot asset, transition asset, etc.
- `Customizations`: details customizations.
- `Widgets`: Slate widgets for debug and shot editing.
- `ScriptedActions`: asset scripts.
- `Trace`: Rewind Debugger trace provider, analyzer, and TraceServices module.

UncookedOnly module adds K2 nodes and graph pin widgets used in editor/PIE.

### Global Tools Menu

Global tools share **Tools -> Composable Camera System** in the Level Editor.
Its entry uses the custom `ComposableCamera.Tools` SVG and `.Small` brush: a
white camera operator riding a tilted camera with a teal lens and orange
exhaust, simplified for compact menu sizes. The submenu contains four sections:

- Editors: Composable Camera System Edit Window and Shot Editor.
- Mesh Camera Layers: Edit Mesh Camera Layers and Show Mesh Camera Layers.
- Viewport Camera: Copy Active Viewport Camera Transform and Key Active
  Viewport Camera Transform To Sequencer.
- Sequencer: Key Spawn Tracks From Camera Cuts.

The two global Nomad spawners hide their automatic Window menu entries to avoid
duplicate launchers. Tools menu launchers focus existing tab instances; opening
Shot Editor this way preserves its active Shot. Contextual asset, graph, and
Sequencer entry points continue to supply their own authoring context.

The Edit Window launcher and Camera Type Asset's Live Edit toolbar button share
the custom tuning-panel-and-pencil SVG registered as `ComposableCamera.EditWindow`
with a compact `.Small` brush. The Edit Window tab uses the same icon. Edit Mesh
Camera Layers and Show Mesh Camera Layers menu entries are text-only toggles.

## 3. Asset Editor

`FComposableCameraTypeAssetEditorToolkit` is the main editor for:

- `UComposableCameraTypeAsset`.
- `UComposableCameraPatchTypeAsset`.

Current surfaces:

- graph tab.
- details tab.
- build messages tab.
- runtime debug tab, default-open in the left editor stack.
- runtime previewer tab, opened from the Window menu.
- debug instance picker / graph overlay.
- Live Edit toolbar entry targeting this toolkit's selected Debug instance.
- toolbar command to open Shot Editor for selected composition framing node.

The toolkit uses `FBaseAssetToolkit`. It owns graph commands, selection sync,
save/build hooks, property-change hooks, debug ticker, and selected-instance
tracking.

The v3 default layout places `RuntimeDebugTabId` in a narrow left stack beside
the graph and Details panels. `RuntimePreviewerTabId` is a closed tab in that
same stack, so Window opens it in the same local observer area instead of an
unrelated floating region. Both share the existing Debug instance picker
selection; neither creates a separate runtime camera picker.

Delegate rule: any `AddRaw(this, ...)` binding to a details view, graph, ticker,
or external editor object must be explicitly removed in the toolkit destructor.

## 4. Graph Source Model

Editor graph types:

- `UComposableCameraNodeGraph`
- `UComposableCameraNodeGraphSchema`
- `UComposableCameraNodeGraphNode`
- graph node classes for start, begin-play start, output, variable get/set, and
  node templates.

The graph is the authoring source during editing. The asset is the durable
source on disk.

Round trip:

```text
Editor graph edit
  -> SyncToTypeAsset
       -> NodeTemplates
       -> PinOverrides
       -> Connections
       -> ExecutionOrder
       -> FullExecChain
       -> ComputeExecutionOrder
       -> ComputeFullExecChain
       -> VariableNodes
       -> editor positions

Asset open / rebuild
  -> RebuildFromTypeAsset
       -> recreate transient graph
       -> restore nodes, pins, wires, positions, selection-safe identity
```

`EditorGraph` is transient. Do not rely on it for serialized truth.

## 5. Node Identity

Each runtime/editor node needs stable identity.

Rules:

- Use existing GUID when rebuilding.
- Generate a GUID only for a genuinely new node.
- Preserve GUIDs through copy/paste when the copied node should be a new node
  instance but still internally stable after paste.
- Connections and pin overrides must address the intended node after save/load.

Any new editor-side data must decide whether identity follows:

- node template GUID.
- variable GUID/name.
- pin name.
- section-local asset data.

## 6. Graph Schema

The schema enforces graph shape and pin compatibility.

Execution chains:

- Main chain starts at Start and ends at Output.
- Compute chain starts at BeginPlayStart.
- Concrete compute node titles use `Begin Play:` in the graph / palette to
  make activation-time behavior explicit.
- Set-variable nodes belong to the chain they are wired into.
- Compute nodes do not run in the main per-frame camera chain.
- Camera nodes do not run in the compute chain.

Pure data nodes:

- variable get nodes are pure and can feed compatible input pins.
- node output pins can feed later compatible input pins.
- exposed parameter and variable data compile into runtime data-block slots.

Cross-chain direct execution wires are rejected. Data flow is allowed only where
the runtime data model can represent it safely.

## 7. Pin Editing

Editor pins mirror runtime pin declarations.

Supported kinds:

- Bool, Int32, Float, Double.
- Vector2D, Vector3D, Vector4.
- Rotator, Transform.
- Actor, Object.
- Struct, Name, Enum, Delegate.

Editor responsibilities:

- display default values.
- expose selected UPROPERTY values as pins.
- serialize pin overrides.
- validate connection compatibility.
- provide object/class pickers where applicable.
- avoid silently dropping Struct, Name, Enum, or Delegate pins.

Object and Actor pins have runtime class guards. Earlier layout-time diagnostics
for class mismatch remain a useful future improvement.

## 8. Parameters and Variables

Type assets support:

- exposed parameters: activation-time inputs.
- internal variables: graph-local state.
- exposed variables: mutable values surfaced to activation and Sequencer bags.

Editor responsibilities:

- details UI for parameter / variable metadata.
- graph nodes for get/set variable.
- DataTable row string conversion metadata.
- stable sync/rebuild for variable nodes and connections.
- exec-chain `SetVariable` entries must preserve the exact variable graph-node
  GUID; variable GUID alone is not enough because multiple Get/Set nodes can
  point at the same variable.
- generated K2 pins for activation nodes.

Removed system:

- legacy context-variable assets and collections are not part of current code.

## 9. Build Messages

Build/validation messages are the author-facing contract for graph errors.

Use them for:

- missing required pins.
- invalid connections.
- incompatible patch nodes.
- bad variable get/set entries.
- duplicate or missing identity.
- data layout failures.

Messages should attach to the relevant graph node when possible.

## 10. Patch Asset Editor

`UComposableCameraPatchTypeAsset` uses the same graph editor as camera type
assets.

Patch-specific asset data:

- default enter duration.
- default exit duration.
- default ease.
- default layer index.
- default expiration bitmask.
- default duration.
- `CanRemain` condition hook.

Editor responsibilities:

- show patch-specific details.
- validate node patch compatibility through `GetPatchCompatibility`.
- let K2 node `AddCameraPatch` generate typed pins from exposed parameters and
  exposed variables.
- keep graph sync identical to type assets.

Patch-incompatible nodes should be errors. Compatible-with-caveat nodes should
be warnings.

## 11. K2 Nodes

UncookedOnly contains custom Blueprint nodes:

- `UK2Node_ActivateComposableCamera`.
- `UK2Node_ActivateComposableCameraFromDataTable`.
- `UK2Node_AddCameraPatch`.
- `UK2Node_AddCameraAction`.
- `UK2Node_AddCameraModifier`.
- `UK2Node_PlayCutsceneSequence`.

Asset-parameter nodes generate typed pins from selected assets and must refresh
those pins when the asset changes. K2 nodes compile to runtime Blueprint library
calls.
`UK2Node_AddCameraPatch` uses the Patch asset's warm-orange title color
(`#E08020`, sRGB 224/128/32), keeping Patch identity consistent between
Blueprint graphs and the Content Browser.

`UK2Node_AddCameraAction` reads the selected Composable Camera Action asset's
instanced Action template. It shows pin-compatible editable Blueprint-visible
fields declared below ActionBase as optional advanced pins. Only connected
pins enter a parameter block; the asset template supplies every unconnected
default. A variable-driven asset input has no static parameter pins but may
still activate an asset at runtime.
The node expands to `AddActionFromAsset` in the runtime Blueprint library.
Composable Camera Action assets use the normal Details editor, not the camera
node graph. The Content Browser displays that name without the internal
`TypeAsset` suffix. A camera silhouette with an indigo action bolt is registered
as both class icon and thumbnail through `FComposableCameraEditorStyle`.
The Add Camera Action K2 node title uses the same indigo (`#665AE5`) as the asset,
distinguishing it from the default Blueprint function-node blue. Its node icon
is `ClassIcon.CameraComponent`, matching the camera activation and Patch nodes.

`UK2Node_AddCameraModifier` exposes exec input/output, `PlayerCameraManager`, and
`ModifierAsset`. Its title uses the Modifier asset's purple (`#A05AC8`) and the
camera component icon. A literal asset selection adds the asset name to the
title; a variable-driven asset keeps the generic title. Modifier settings stay
on the asset, so this node does not generate parameter override pins.
It expands to the existing `UComposableCameraBlueprintLibrary::AddModifier`.
That function is `BlueprintInternalUseOnly`: new graph menus offer the custom
node, while previously saved function-call nodes retain their function reference
and runtime behavior.

Important activation data:

- camera type or patch type asset.
- context name.
- transition override.
- activation params.
- parameter block.
- exposed variables when applicable.

Do not duplicate runtime conversion logic inside K2 nodes. Generate pins, then
feed runtime APIs.

## 12. DataTable Path

DataTable activation uses:

- camera type asset.
- context name.
- transition override.
- activation params.
- parameter rows encoded as strings.

Editor pin widgets help choose DataTable and row names. Runtime conversion
happens through parameter-block string parsing.

Actor values are not parsed from DataTable strings. Use Blueprint/K2 activation
for actor references.

## 13. Level Sequence Editor Integration

Editor integration covers:

- `AComposableCameraLevelSequenceActor`.
- type asset reference details.
- parameter / variable bags.
- shot track and shot sections.
- patch track and patch sections.
- track editors.
- section painters.
- section context menus.
- target actor binding overrides.

Shot sections:

- can store inline shot data.
- can reference a `UComposableCameraShotAsset`.
- asset-reference sections edit a section-local `ShotOverrides` copy.
- target actor overrides map target indices to Sequencer bindings.
- incoming section `EnterTransition` drives overlap blend.

Patch sections:

- use parameter-section style keyed values.
- bind to a target LS actor.
- evaluate through LS component overlay path.

## 14. Shot Editor

Shot Editor is a nomad tab used to edit `FComposableCameraShot` data.

Hosts:

- selected `UComposableCameraCompositionFramingNode`.
- selected shot section.
- shot asset.
- transient draft created from selected level actors.

Current behavior:

- one global tab instance.
- context swaps when user opens another shot source.
- edits write directly to the host object inside transactions.
- preview viewport resolves target actors, meshes, bounds, anchors, zones, and
  framing overlays.
- compact top bar combines asset commands, active host breadcrumb, Sequencer
  Shot dropdown, Recent dropdown, and Compose / Inspect modes
  (internal Drag / Free enums; hotkeys 1 / 2). Preview/Lock and its shortcut 3
  are removed; mode state is transient and not serialized.
- a unified status bar appears below the top bar only when the editor has
  something actionable or diagnostic to report. It currently owns Free-exit
  Save / Discard / Stay actions, reverse-solve unavailable reasons, no-Shot
  guidance, and stale-host guidance.
- viewport-local floating toolbar owns Reset, HUD, and Guides. It is anchored
  at the top-right of the viewport so the top-left diagnostic HUD remains
  readable, and it can collapse down to a small Tools button. That collapsed
  state persists per project. Reset is Free-mode-only and snaps the preview
  camera back to the current solved Shot pose without writing Shot data. HUD
  toggles structured Camera / Composition cards and starts off to keep the camera
  image clear. Cards use headers, aligned label/value rows, subdued panels and
  status colors; the old plain-text block and bottom summary strip are removed.
  Camera shows view mode, frame/aspect, pose, FOV, focus, aperture and roll;
  Composition shows Follow mode/distance, Aim resolution, screen target/projected
  drift and local orbit basis/angles. Damped values retain authored -> effective
  comparisons. Camera stacks above Composition at a fixed eight-unit upper-left
  image inset. Their size, text, headers, row spacing and card gap uniformly scale
  with the camera image (0.85 at 1280 x 720, about 21% larger than the previous cards); resizing
  preserves every diagnostic row. Short images cap the complete stack to the
  space above the possible orbit panel, including an eight-unit gap, even when
  Guides is off. Orbit visibility and task selection cannot move or reflow the HUD. Cards
  stay inside the image and clear of the lower-left orbit control. Guides toggles handles, framing zones,
  orbit control, subject gizmos and bounds wireframes. The toolbar deliberately does not expose a separate Frame
  command.
- Shot dropdown is the primary sibling-shot navigator for Sequencer-backed
  contexts. It lists sibling Shot sections for the active LevelSequence in a
  searchable, track-grouped panel with current-shot checkmark and time/row
  suffixes; the editor does not keep a persistent left-side Shot outliner.
- task navigation sits below the host toolbar: Create / Edit / Sequence /
  Presets / Advanced. A flexible left slot holds compact task buttons; centered,
  fixed-size Level Preview and Follow playhead buttons stay at the row's right.
  Task buttons are 28 Slate units high, with 14-unit icons and 9-point labels;
  Edit subtabs are 26 units high with 9-point labels. Presets uses the native
  open-folder library icon rather than a save icon. Application/DPI scaling
  remains authoritative. Native Background/Secondary colors supply dark chrome;
  CameraNodeTitle (#14968C) marks selected primary navigation. Edit subtabs
  use a softer selection color mixed with native Secondary gray. Parameter-page
  actions use the native Unreal Button gray palette and hover/pressed states,
  fixed 156 x 24 size and centered captions; group headers
  use a lighter, subdued neutral gray, without a green fill. No task action uses the native blue PrimaryButton style.
- `SShotEditorPreviewLayout` is a native horizontal splitter with exactly two
  full-height columns: complete parameter pages on the left (initially 40%) and
  Preview on the right (60%). Users drag the divider to adjust widths. There is
  no lower parameter region or separate preview-side page. Create contains both
  template/assignment and subjects; Edit keeps behavior, anchor/screen zones,
  lens/focus or motion response together in each section; Sequence keeps
  destination/duration and clip actions together; Presets keeps apply/restore
  and save together. Each page retains its widgets and scroll state. Advanced
  keeps the full structure Details widget and its own scroll.
  `SShotEditorPreviewFrame` reserves a six-unit screen bezel around the actual
  camera image, then fits the native viewport at live camera aspect within the
  remaining space. The bezel has a thin gray outer highlight, dark body and
  subdued inner lip. It follows the fitted picture, not the entire Preview pane:
  the outer screen edge aligns right, with the image centered vertically and
  letterbox margins outside the bezel. Decoration is behind the image, uses
  no input widgets and does not obscure viewport pixels or capture mouse input.
  Window resizing and divider changes resize the same native viewport; when
  height limits the image, it remains fitted instead of distorting its aspect.
  Source, parameter pages and property bindings are retained during resizing.
  The former width/aspect-driven upper row and lower authoring reserve are
  removed so preview and parameters can use the full available height.
- Parameters use flat colored headers and 24-unit native label/value rows.
  Parameter groups stack in a normal left pane and share two columns above
  800 available units if the pane is widened.
  Each whole Subject is collapsible. Its body contains a full-width actor picker
  and four vertically stacked sections: Component / pivot, Pivot / orientation,
  Framing bounds / weight, and Preview model. All four always fill the same row
  width at every pane size and use matching neutral headers, including the
  expansion arrow. Component / pivot and Preview model start collapsed; whole
  Subjects start expanded. Whole-Subject and component fold states are retained
  per page and role/index across structural refreshes. The former up/down square
  buttons and fixed actor column are removed. Existing
  native vector/asset controls, transactions and local mode rebuilds remain.
  Scalar commits retain parameter containers and page scroll state.
  The previous Quick strip is removed.
- A camera-frame/composition-anchor SVG provides 16/20/64-unit Shot Editor
  brushes. Tab spawner, shared Tools menu, camera type asset toolbar and
  Sequencer Edit Shot action use that art. ShotAsset class icon and Content
  Browser thumbnail use the same art, so its double-click editor is recognizable.
- Create groups template selection and actor assignment. Successful assignment
  or template application opens Edit. Follow is the default Edit section.
  Follow exposes the complete Placement structure: AnchorOrbit (basis, both
  basis subjects, direction, distance and damping), AnchorAtScreen (screen
  coordinates, distance and zones), and FixedWorldPosition (world XYZ).
  Its anchor supports single subject, weighted centroid and world point.
  Aim exposes the complete Aim structure, including LookAtAnchor / NoOp,
  anchor, screen coordinates and zones, plus Roll / RollSpeed. Both sections
  place the Anchor group before behavior and screen zones, including when groups
  share columns in a wide parameter pane. They use the existing mode-sensitive
  property customizations; ignored fields hide
  while their serialized values remain intact. Parameters are generated from
  the whole Shot's reflected property tree, preserving target-index context.
  Lens & Focus exposes the full Lens and Focus structures: manual/bounds-fit FOV,
  fill ratio, FOV clamp, aperture, FOV response, all four focus modes and custom
  focus anchors. Motion gathers distance/FOV/roll response and the active
  Follow/Aim screen-zone X/Y response, using the same native editors and
  authoritative property ranges. Motion omits the extra Aim screen-response title
  and both explanatory paragraphs; its native response parameters remain. Subjects
  are collapsible role groups with binding-aware actor/component/bone controls. Their remaining reflected
  fields group into Pivot / orientation (offset, local space, bone enable and
  mesh-forward basis), Framing bounds / weight (shape, extent, cache policy,
  interval and continuous contribution weight), and a collapsed Preview model
  section (mesh, actor transform, mesh-relative transform). Runtime bounds caches
  are excluded. Component/bone controls collapse behind Component / pivot.
  Only the whole-Subject role header uses the primary muted-gray fill and bold
  text. All groups underneath use the native dark rounded expandable-row border
  and regular text, matching the nested preview-transform rows. The shared title
  box centers text at its intrinsic line height, aligning it with the native
  collapse arrow in both expanded and collapsed states. Each stays full
  width, with its existing expansion state preserved in both Create and Edit.
  Each whole-Subject header has a centered native trash-icon button and Delete
  tooltip, available even when folded. Subjects page roots, append actions, actor/component controls and
  surviving role cards are retained across collection edits. Append adds cards;
  shrink removes tail cards. All native parameter handles are rebound against
  current storage and their controls replaced in the same idle Slate tick,
  preserving card folds, page/scroll containers and avoiding a blank refresh frame.
  Sequence separates destination/duration, clip
  creation and current-clip actions. Presets separates apply/restore from save.
- The layout borrows task tabs, a dominant preview and grouped controls from
  BlackEyeCamera's read-only `BlackEyeTab/SBlackEyeTab.cpp` and
  `Panels/SBlackEyeFollowPanel.cpp`; its custom rotary widgets and artwork are
  not reused. Slate switchers and page scroll containers are built once and retain
  widget identity and tab choices across source refreshes. Structural refreshes
  update Subject list slots and native parameter handles; scalar commits
  update the existing controls and preview. Numeric, Boolean and Enum edits,
  including mode/basis/anchor/cache-policy changes, retain every parameter widget:
  inactive fields are constructed once and Slate visibility/native enabled
  attributes change in place. Generator-instance customizations apply only to
  Shot Editor parameter pages; global Details/Advanced customizations remain
  unchanged. Navigation does not write Shot data and cannot switch a numeric
  gesture away while either a session or native PropertyEditor transaction is
  active. Native parameter panels retain expansion state across source/array
  refreshes and regenerate their property handles against the current source.
- single close-up / medium / full-body, pair two-shot / left shoulder / right
  shoulder / reverse shoulder, and group wide templates write ordinary Shot
  values. Single templates require one subject, pair templates two, group at
  least two. Non-bone template pivots are derived afresh from current mesh bounds
  so repeated application does not accumulate offsets. Templates provide a
  starting composition; they do not model collision, shoulder occlusion, or
  automatic dialogue timing.
- Subjects show A/B roles, actor/component/bone/socket pickers and continuous framing
  weight. The compact up/down reorder and Swap actors A / B buttons are removed
  from both Create and Edit; the session retains its index-safe reorder and swap
  operations. Add Subject appends an empty slot; Add Selected Actors
  appends the current level selection. Both actions are available in Create and
  Edit / Subjects, including empty drafts/zero-subject Shots. Append preserves
  existing subjects, composition, anchor membership and binding indices; it does
  not apply a template. Empty slots can be configured with an actor or preview
  model. Actor batches preflight bindings and commit as one Undo action; referenced
  preset sections append only to their local copy. Reorder remaps anchors, weights, pair basis,
  and Section binding indices together; swap deliberately exchanges actor roles.
  Delete removes one subject with one Undo/Redo step, including the last slot.
  Surviving single/weighted anchor and primary/secondary basis indices shift with
  their subjects; references to the deleted slot become INDEX_NONE and weighted
  memberships are removed. Section overrides follow the same indices, discarding
  removed/stale entries. Shared presets and sequence object bindings stay owned by
  their existing assets/scene. Empty lists retain the Add actions. Locked sources
  and active native/session/viewport transactions disable deletion.
- Create and Edit / Subjects show each resolved subject's unoffset Pivot/Bone
  point, effective pivot, Offset connection and short placement-heading triad.
  RGB axis endpoints drag Offset in its authored world/local frame; the local
  frame comes from the same actor/component/socket resolver as runtime. Manual
  Bounds have six yellow face handles, adjusting one world-aligned half-extent
  symmetrically about the pivot with a nonnegative floor. Auto Bounds are visible
  and read-only; None draws no box. Subject pages show configured bounds even at
  Manual FOV or zero contribution weight (muted gray). Fit contributors keep
  green/in-front and yellow/partly-behind diagnostics; other pages retain only
  those existing fit diagnostics. Compact Subject names remain near visible
  effective pivots and match the parameter-page roles; pivot/bone, offset-space
  and weight summaries are omitted. XYZ axis labels remain. Gizmos belong to
  Guides, independently of engine component/collision widgets, and remain disabled in Inspect/read-only
  sources. Ctrl refines, Shift accelerates. A motionless click produces no commit;
  one drag produces one Undo without rebuilding parameter controls. Hidden or
  stale handles reject writes after task/source/identity/array/image-size changes.
  Axis labels stay inside the camera image. Axes
  nearly parallel to the camera ray have no reliable screen motion and cannot
  be grabbed; other axes or panel values remain available.
- `FComposableCameraShotAuthoringSession` resolves Section source mode each
  read and reflected Shot properties on node/assets. It owns a GC-tracked
  transient draft, transactions, change notifications, and paused preview
  refresh; it introduces no parallel serialized camera model. Numeric dragging
  writes the real Shot immediately, snapshots once, and posts one host commit
  on release. Widgets never keep stale Shot pointers across a source switch.
- Preview current level renders the editor/Sequencer world using game-view
  visibility in this viewport only. Selection, editor primitives, collision
  shapes and engine bounds are hidden; the isolated scene alone keeps its grid.
  Shot composition guides remain controlled by Guides. For an in-range
  Section, paused edits refresh its registered LS component and the viewport
  reads the final native CineCamera view, including overlap/patch/optics. Pinned
  inactive Sections and standalone sources use the solver. Template preview
  mode retains the independent scene/proxies. Explicit unresolved bindings show
  unresolved composition, never a different placeholder actor.
- Follow playhead is opt-in; off pins the edited source. Following selects a
  local Shot camera through Camera Cuts, or the current track when no local cut
  resolves. In overlaps it selects the incoming of the two lowest rows. It
  pauses during numeric/handle gestures and Inspect/Free-exit actions. Nested
  sequences must be focused before creating/editing their subject bindings.
  Spawnable subjects retain sequence-relative bindings within that hierarchy;
  subjects spawned by a foreign player are refused before creating bindings.
- Add Shot to Sequence creates a dedicated LS Shot Actor binding, Shot Track,
  finite labelled Inline section, subject bindings, camera Spawn coverage, and
  Camera Cuts. Spawnable is default; disabling it creates a level Possessable.
  Inside the current section, creation appends after it and reuses its camera.
  Dialogue Set creates two-shot / shoulder A / reverse B consecutively on one
  camera. Duplicate after current preserves full Section data and bindings.
- Creation preflights read-only/range/overlap conflicts. Existing cuts are
  preserved; an already covering cut for the same camera is reused. Camera
  spawn coverage is extended only for the new interval, leaving authored keys
  intact. Unlocked playback range expands to include new clips. Creation is one
  undo operation; failure reverts that operation rather than leaving partial
  tracks. Read-only/locked Section data cannot be edited through the panel or
  viewport tools.
- Save as Preset creates a ShotAsset through the native asset dialog, removes
  level actor identities, and captures skeletal preview mesh plus actor/relative
  mesh transforms. Applying/restoring a matching preset preserves local subject
  identities, selected components, bones, offsets and bindings. A populated
  source rejects a different preset subject count; assign subjects first.
- The full Details panel is mode-sensitive: Placement, Aim, Lens, Focus, and
  AnchorSpec rows that are ignored by the current mode collapse out of view
  instead of remaining as disabled clutter. Hidden values stay serialized and
  reappear when the user switches back to the relevant mode. Placement and Aim
  anchor specs remain visible because Focus follow modes can still consume them.
- preview image and its floating toolbar share an aspect-constrained native
  SBox, centered inside a black pane. Filmback sensor width/height, lens squeeze
  and optional Crop aspect determine its ratio in level and isolated previews,
  Compose / Inspect and on window/splitter resize. The local renderer
  also constrains that ratio; solver, projection and reverse solve use it rather
  than pane dimensions. Physical hit/drag coordinates use the engine's current
  constrained view rect. A pinned inactive spawnable can read its camera template
  configuration without evaluating it; active output still requires a live
  in-range binding. Detached drafts/presets use the native CineCamera default.
- viewport wheel, anchor/zone release, roll release and reverse-solve commits
  share the session's scalar commit path. They snapshot the host without Modify,
  emit one guarded ValueSet and mark dirty. Live drag writes only request preview.
  Scalar commits retain parameter controls; array edits, history and external
  source changes keep their deferred structural refresh behavior.
- viewport tools can adjust distance, roll, and anchor / zone handles in Drag.
  Handle drawing uses DPI-adjusted Canvas coordinates; cached hit rectangles
  and drag input use physical viewport pixels, including letterbox origin.
  Aim's marker, zones and projection appear only on Edit / Aim with
  LookAtAnchor active. Switching task/subtab disables stale hits immediately;
  visibility is editor view state, not serialized Shot data. The Follow handle
  appears only in AnchorAtScreen. AnchorOrbit/FixedWorldPosition hide the
  unused Follow screen handle; NoOp hides Aim. Edit / Follow with AnchorOrbit
  instead shows a compact latitude/longitude globe inside the image's lower-left.
  The resolved orbit center has no separate point in the 3D scene; the globe
  remains the position control without covering the subject with an Anchor dot.
  Its camera marker represents authored LocalCameraDirection in the selected
  BasisFrame. Dragging the globe adjusts yaw horizontally and pitch vertically,
  retaining distance and basis. Ctrl gives fine movement, Shift fast movement;
  yaw wraps and pitch stops at +/-89.5 degrees. A grab cursor, hover rim and hint
  identify the interaction. Guides off and other task/subtabs hide it; Inspect
  and locked/read-only sources display it disabled. Tiny images hide the
  control rather than clipping it. Mouse-down snapshots once without Modify;
  live motion requests preview only, release posts one scalar commit and one Undo.
  Clicking without motion creates no commit. Wheel editing waits until captured
  gestures end, avoiding nested transactions. Mode/source changes close the
  previous gesture; stale hidden hits cannot write. The underlying anchor
  parameters remain available for orbit/focus configuration. Inspect
  and read-only sources keep the relevant handles disabled and non-interactive.
  Drawing, stale-cache hit tests and drag writes use the same mode predicates.
  Free allows mouse camera inspection and can reverse-solve that pose back into
  Shot data when leaving the mode. Leaving Free for Drag queues the
  target mode and shows Save / Discard / Stay in the status bar instead of
  opening a modal dialog.

- Preview compatibility: the authoring layout retains the original
  `SShotEditorViewport` and `FComposableCameraShotEditorViewportClient`.
  Compose / Inspect are labels for Drag / Free, with
  1 / 2 shortcuts. The third Preview mode and its mouse-input branch are removed;
  key 3 falls through without changing mode. Compose retains distance wheel, Alt+RMB authored Roll,
  screen-anchor and zone-edge dragging (Shift mirrors the opposite zone edge).
  Inspect retains native orbit/pan/dolly, Reset and reverse solve; lens/focus and
  authored Roll remain live while position/yaw/pitch are user controlled. HUD,
  Guides, fit bounds, preview meshes, Ctrl+Alt+C view-transform copy, Ctrl+B
  asset browse, save and Undo/Redo remain available. Page-specific guide visibility
  follows the newer Follow/Aim/Subjects policy above.
  Locked sections and read-only sequences block every Shot writer, including
  Roll in Inspect. Mouse releases close captured writers before the read-only
  input guard; permission changes also close them on Tick. Invalid hosts cancel
  transactions before clearing Shot pointers. Keyboard routing remains native
  in every mode; read-only Inspect still permits camera navigation. Mode tooltips
  describe implemented controls, without advertising a nonexistent bone context menu.
  `ShotEditor.CameraModes` inspects the actual two-button mode control and checks
  that key 3 is unhandled in both modes, key 2 still enters Inspect, and key 1
  retains its Save / Discard / Stay exit flow.
  `ShotEditor.PreviewCompatibility` covers native Roll/wheel transaction paths,
  retained controls, Inspect optics, Reset/reverse solve and read-only boundaries.
  Attached-window mouse navigation, clipboard and keyboard routing remain manual
  integration checks.

The editor must not write back to a shared shot asset when editing a Sequencer
asset-reference section. It edits the section-local override copy.

## 15. Target Info Customization

`FComposableCameraTargetInfo` details customization supports:

- actor selection.
- named component selection (None retains the legacy auto selection).
- bone/socket selection for skeletal targets.
- preview mesh data.
- local/world offset controls.
- effective actor resolution for LS section overrides.

The effective actor can come from:

- direct target actor.
- Sequencer binding override.
- shot editor preview context.

Bone picker UX must degrade safely when the target is not a skeletal mesh.

## 16. Runtime Debug From Editor

### Composable Camera System Edit Window

- A single-instance global Nomad tab, registered by the editor module, appears
  in **Tools -> Composable Camera System** as
  **Composable Camera System Edit Window**. It can float or dock through the
  standard Unreal tab manager; its stable tab ID is
  `ComposableCameraSystemEditWindow`.
- A newly opened window starts on **Welcome**, with a short introduction,
  Documentation (`https://sulley.cc/ComposableCameraSystem-Docs/`), GitHub
  (`https://github.com/littlesulley/ComposableCameraSystem`), YouTube tutorial
  (`https://www.youtube.com/watch?v=yAWaHS36mmw`), and Bilibili tutorial
  (`https://www.bilibili.com/video/BV1s8EF6tEZp/`) resource links. Links open in
  the system browser only when clicked. Welcome also provides Open Shot Editor.
- **Welcome / Debugging / Live Editing** navigation switches between three pages. Only choosing
  Debugging reveals the console controls; returning to Welcome preserves the
  in-window search and section state. Each entry into Debugging rediscovers
  registered `CCS.*` console objects, replacing the former Refresh button.
  Boolean/legacy debug switches use checkboxes, integers/floats use numeric
  inputs, strings use text inputs, and one-shot commands use Run buttons with
  optional arguments. Variables use compact label/value rows; command rows
  wrap their argument input and Run button. Console names and full help live
  in row tooltips rather than repeating as a visible second line.
- Debugging includes **Show Mesh Layers** inside **Viewport visualization**,
  using the same compact label/On/Off checkbox row as other switches. It shares
  the Tools menu's preview action and reads its live state, so both entry points
  stay synchronized. Search and section counts include this row; its action
  toggles read-only visualization in the editor and all PIE worlds independently
  of the 3D viewport debug master switch or world selection.
- Six common controls appear first within their existing sections: 3D viewport
  debug, All node gizmos, All transition gizmos, Camera HUD, Pose History, and
  Freeze pose history. Their names use bold text with a small neutral **Common**
  badge. The badge denotes frequency, while the checkbox/On/Off retains state;
  node/transition colors remain reserved for matching runtime visualization.
- Search matches label, console name, and help. Collapsible groups separate
  viewport controls, camera HUD/history, node gizmos, transition gizmos, trace,
  runtime inspection, editor actions, and other controls. All groups start
  collapsed; searching expands matching groups without changing the user's
  stored expansion choices. Node/transition accents reuse the runtime viewport
  legend colors. Live group hints explain
  the 3D master gate and the All-gizmos override of individual Off switches.
- Values read directly from the live registry and writes use console priority.
  Console edits and UI edits stay synchronized. The UI exposes no per-variable
  Reset, bulk Reset, or bulk Disable actions.
- CVars affect the whole editor process, including all PIE instances. Command
  buttons use an Auto world (game/PIE first, editor fallback) or an explicitly
  selected game world. World selection lives inside Runtime Inspection and
  appears only with multiple game worlds or an existing explicit selection;
  a single game world uses Auto and displays its target name. Without PIE, the
  section provides a start-PIE hint. `CCS.Dump.*` requires a game world. An ended
  explicit PIE world remains unavailable rather than silently targeting
  another instance.
- The Debugging page retains owned console metadata and weak world references only.
  It resolves console objects by name on access, tolerating unregistration.
  Module shutdown clears/closes its live tab before unregistering the spawner.
  Explicit world resolution verifies that its engine world context still exists;
  tearing-down worlds cannot receive runtime dump commands, even before GC.
  Command feedback confirms invocation; Output Log reports the actual result.
- Open Shot Editor focuses its existing tab without clearing its active Shot.
  Output Log is also reachable directly. This global control window complements
  the context-bound runtime observers below.

### PIE Live Editing

- Select one camera from PIE-world sections. The asset editor Live Edit toolbar
  opens the same page for its selected Debug instance, using per-toolkit menu
  context. Pending asset changes require Apply or Reset before switching cameras.
- Two sections share a resizable vertical splitter. The upper **Node Chain**
  shows Start, executed camera nodes, and Output left to right using the same
  graph node widgets, title colors and exec connections as Camera Type Asset.
  It supports graph pan/zoom and single-node selection. The graph is a transient,
  read-only presentation with exec pins only; it has no owning type asset and
  never participates in authoring sync. FullExecChain supplies node order;
  legacy assets use ExecutionOrder. Off-chain dependencies and BeginPlay nodes
  do not appear.
- The lower **Runtime Parameters** section displays the selected node's native
  Details view. The first executed node is selected on camera binding; clicking
  Start/Output or empty space shows a selection hint. When selected, its header
  shows only the node display name, matching Node Chain. Switching nodes retains
  their typed proxies, trial overrides and pending defaults. Details scroll/search stays
  inside this section, and pending proxies remain viewable, read-only after PIE.
- Apply to Asset and Reset Trial share 132x28 dimensions, text styling and padding.
  Both explicitly center their content horizontally and vertically; container
  alignment alone does not center SButton's fill-aligned text content.
  Apply uses blue while Reset uses the normal button color. Both sit in the top
  action bar. Reset Trial stays available while any runtime trial override remains,
  including after Apply. Apply marks the source dirty; normal Save persists it.
- All editable authoring properties are discovered by reflection, including
  arrays, structures, curves, object references and inline/instanced subobjects.
  Base node metadata, transient state and read-only outputs are excluded.
  Edit conditions remain native Details behavior. No CCSLiveEdit opt-in is used.
- Native Details edit independent typed proxies. Nested object events resolve
  back to their root node parameter, including objects inside structs/containers.
  Accepted-value snapshots and canonical signatures compare owned subobject
  contents rather than proxy object paths. Runtime/source objects are never
  directly bound to the Details view.
- A dedicated trial layer overrides wire/variable, caller and Modifier inputs.
  Their underlying storage continues updating. Reset removes the trial and
  reveals the current driver. Apply writes authoring fallback values while
  preserving those drivers. Refresh rebuilds only the edited node's cached
  configuration; that node's temporal state may restart.
- Apply preflights all edited defaults against source changes, resolves graph
  nodes by template identity, and handles source-array reordering. Source graph
  nodes are modified in one transaction; instanced values receive fresh owned
  duplicates, with destination flags and sibling reference remapping. Direct and
  compound defaults are updated, pins reconstructed
  under the graph sync guard, then SyncToTypeAsset runs once. Existing node GUIDs
  remain stable. The whole runtime node is never copied into the asset.
- Preflight uses isolated candidates and remaps only edited root properties and
  their owned subobjects. Failure leaves source data and editing/accepted proxies
  intact. Source changes, unmappable edited references, and removal/retyping of
  wired or exposed pins reject the entire commit. Both open graphs and closed
  assets' serialized camera/variable connections are checked. An undriven schema
  change may proceed; only obsolete compound overrides under that edited root
  are pruned transactionally.
- Visible defaults populate DefaultValue/DefaultObject/DefaultTextValue from one
  authored string. Normal pin reconstruction retains persistent pin defaults;
  PostEditUndo reconstructs defaults from restored authoring data while preserving
  links. SyncToTypeAsset rejects callbacks during GIsTransacting, so node-by-node
  Undo/Redo reconstruction cannot overwrite restored durable asset state. Apply's
  final graph notification also runs under the sync guard. A successful full
  Reset captures fresh source conflict snapshots for subsequent trials.
- Actor references map between the selected PIE world and their editor
  counterparts. Trial writes reject references without a counterpart in the
  selected world, including actors from another PIE world. A spawned PIE-only
  actor in the selected world without a saved counterpart can be
  trialled but cannot be committed as an asset default. PrePIEEnded detaches
  runtime references and clears unmappable PIE references in every typed proxy.
  Pending defaults remain available while the window stays open.
- Closing the window drops the editor trial record; its independent runtime
  trial layer lasts until the camera is destroyed. FGCObject tracks editor
  proxies; the runtime layer owns its own transient reflected storage.
  The window also collects its transient presentation graph and its node proxies.
  Details/global delegates are cleared when the window is destroyed.
  Slate paint reads copied labels and cached editability.
- The update distinction follows UE5.6 GameplayCameras
  Public/IGameplayCamerasLiveEditManager.h: property edits and post-build reloads
  are separate notifications. CCS preserves its own evaluator and frame cache.

Type asset editor can inspect runtime instances.

Flow:

```text
selected PIE/world camera instance
  -> editor debug snapshot
  -> graph node overlay
  -> runtime debug tab
  -> runtime previewer tab
```

Rules:

- Do not keep strong refs to runtime cameras from editor UI unless intended.
- Clear debug state on PIE end and toolkit destruction.
- Snapshot values before painting.
- Do not deref stale runtime pointers from Slate paint.

Node hover debug:

- Every runtime node snapshot carries its current parameter values as owned
  strings. Declared input parameters appear first. Editable runtime properties
  not represented by pins follow, covering Details-only arrays, curves, and
  other node-specific settings. Shared node metadata such as `PaletteCategory`
  is excluded.
- Declared inputs read the resolved runtime data-block slot first, covering
  wires, exposed parameters, authored overrides, and nodes that opt out of
  automatic UPROPERTY resolution. If no slot exists, the live runtime property
  supplies its authored/current value. A PCM modifier-owned field deliberately
  reads the property instead because that layer outranks the data block.
- Toolkit copies these strings into transient graph-node debug state. During
  PIE, `SComposableCameraGraphNode` replaces the default bright documentation
  tooltip with an on-demand dark runtime card: node-color accent, Active/Idle
  badge, description/error area, alternating two-column parameter rows,
  monospaced accent values, and a height-limited interactive scroll view.
  Value text uses weak graph-node attributes, so it refreshes while the card is
  open; the card is rebuilt on the next hover when parameter-list shape changes.
  UE interactive tooltips otherwise persist after leaving their source, so the
  node widget explicitly closes the card after the cursor leaves both node and
  card, with a short grace interval for crossing between them. Its header Pin
  action detaches that exact card from Slate's reusable tooltip host and
  promotes it at the same screen position into one independent movable observer
  window for that graph node. Tooltip-host positions are already DPI-adjusted
  physical desktop coordinates, so the pinned `SWindow` disables its default
  initial DPI position adjustment. The pinned window survives hover exit,
  remains above its parent editor, keeps reading copied graph-node debug state,
  and reports `NO DATA` after runtime state clears. Repeated Pin actions reuse
  and foreground the existing window; closing the node widget closes its pinned
  observer so stale Slate windows cannot survive graph reconstruction.
  Outside runtime debugging, the standard graph tooltip remains unchanged.
  Slate never dereferences the runtime node.
- Runtime-data presence is separate from active-node glow. A skipped node can
  still expose its current parameter state while remaining visually inactive.

Runtime Debug panel:

- `SComposableCameraRuntimeDebugPanel` is default-open in the Camera Type
  editor's left stack. It reads the same transient graph-node debug copies as
  hover cards and never retains runtime camera or node objects.
- Only camera nodes with both runtime data and current active/ticked state are
  listed. Items are ordered by runtime node index, default expanded, and use a
  left disclosure button to collapse or expand the pose plus live parameter
  rows. Value attributes keep reading weak graph-node state; the list rebuilds
  only when active membership, search results, parameter-row shape, or item
  expansion changes.
  Item headers show node name and Active badge without a parameter-count number.
  After a non-empty rebuild, `OnItemsRebuilt` requests exactly one additional
  layout pass. This lets expanded rows settle width-dependent wrapped-text
  heights and lets collapsed rows update the list's scroll range plus lower-row
  virtualization, without requiring a user scroll or rebuilding continuously.
- The top search box matches node title, node class display name, and parameter
  labels. Filtering can never reveal an inactive node.
- Double-clicking an active graph node or choosing `Show Debug Information`
  from its node-body context menu invokes a transient graph-to-toolkit request.
  Toolkit opens/focuses Runtime Debug, clears a filter that could hide the
  target, expands its item, requests scroll into view, and runs a 1.25-second
  linear node-color fade across the whole item. A semi-transparent accent
  overlay plus content tint makes the target obvious; repeated navigation
  restarts the effect at full strength. The list keeps selection disabled, so
  navigation never leaves Unreal's blue selected-row background. The context action is disabled
  and double-click is a no-op without active runtime data. The request delegate
  is removed during toolkit teardown.
- Expandable debug groups follow GameplayCamerasEditor
  `Private/Debugger/SGameplayCamerasDebugger.cpp`; search/list scrolling follows
  `Private/Editors/SCameraVariableCollectionEditor.cpp`. The focus fade follows
  the `FCurveSequence` pulse pattern in PropertyEditor
  `Private/SDetailSingleItemRow.cpp`. CCS keeps its own copied graph-node data
  model rather than adopting those editors' runtime ownership.

Runtime Previewer:

- `SComposableCameraRuntimePreviewer` owns an `FAdvancedPreviewScene` and a
  `FComposableCameraRuntimePreviewerViewportClient`.
- The previewer receives slim `FComposableCameraRuntimePreviewData` from the
  toolkit after `SnapshotDebugState()` succeeds: controlled pawn weak pointer,
  visual subject transform, pawn velocity, camera position/rotation/FOV,
  context, and active-state only.
- Each data push immediately refreshes the preview scene and invalidates the
  `SEditorViewport`. The tab does not rely only on the viewport client's normal
  tick to stay synchronized with PIE.
- The toolkit resolves the controlled pawn from the debugged camera's
  `AComposableCameraPlayerCameraManager` and owning `APlayerController`.
- The visible subject transform is the preview reference frame. For skeletal
  pawns, the toolkit uses the root bone world transform so global root-bone yaw
  from animation/controller bookkeeping is removed from the proxy. It then
  falls back to a valid static mesh component, and finally the pawn actor
  transform. This keeps a visually stationary character stationary in the
  preview even if the pawn root/control frame rotates while the runtime camera
  orbits.
- The preview subject stays near origin; live world translation is removed, but
  live subject rotation is preserved. Character transform sync and camera
  transform sync are independent.
- The preview floor is offset down to the proxy bounds minimum Z. Character
  meshes whose component transform is below the pawn root therefore stand on
  the floor without changing subject-relative camera math.
- Skeletal pawns render through a preview `ASkeletalMeshActor`. The proxy copies
  source component-space bone transforms each editor tick and applies them with
  `ApplyEditedComponentSpaceTransforms()`.
- If source/proxy component-space transform arrays are empty or incompatible,
  the previewer destroys the skeletal proxy, falls back to a capsule-like static
  mesh, and remembers that source mesh so it does not rebuild every tick.
- The runtime camera is drawn with the same translation-relative rule as the
  subject proxy: remove subject translation, preserve source rotation. Its
  axes/frustum use the copied camera rotation from `Snapshot.FinalPose`.
  Character/root/pose rotation therefore cannot create fake camera rotation in
  the preview, and camera rotation cannot overwrite character rotation.
- Mouse input controls only the preview viewport observer camera. It never
  drives the PIE pawn or runtime camera.
- PIE end, camera unbind, invalid snapshots, and missing pawns clear the
  previewer to concise empty states while leaving the tab open.

Rewind Debugger trace ingestion:

- The editor module registers `FComposableCameraTraceModule`,
  `FComposableCameraRewindDebuggerExtension`, and
  `FComposableCameraRewindDebuggerTrackCreator` during startup and unregisters
  them during shutdown. This follows GameplayCamerasEditor
  `Private/GameplayCamerasEditorModule.cpp` and `Private/Trace/`.
- `FComposableCameraTraceAnalyzer` routes the runtime logger
  `ComposableCameraSystem` events `ActiveCamera` and `CCSEvaluation`.
- `FComposableCameraTraceProvider` stores two point timelines: rendered active
  camera frames and CCS evaluation frames. Both are keyed by trace event time
  converted from the recorded frame cycle.
- Analyzer writes happen under `FAnalysisSessionEditScope`; provider reads
  require the session read scope used by Rewind track code.
- The Rewind extension toggles `ComposableCameraSystemChannel` while recording.
  During playback it draws the selected pawn's historical active camera frustum
  at the same compact scale as CCS live camera debug, plus any matching CCS
  evaluation primitives and their short sphere labels into the visualized
  world. 3D primitives are submitted from a core ticker so line-batcher content
  reaches the current scene render. Sphere labels are drawn in the
  debug-draw-service Canvas pass, not through HUD debug strings.
- The Rewind track creator adds a `Composable Camera` child track under Pawn
  selections. The track is a selection / visibility affordance; drawing is owned
  by the extension.
- The provider owns only immutable trace-frame copies, not live world objects.

## 17. Asset and Factory Coverage

Editor asset tooling exists for:

- camera type asset.
- action type asset (inline Action template, Content Browser factory and asset definition).
- patch type asset.
- transition data asset.
- transition table.
- modifier asset.
- shot asset.
- Level Sequence shot actor / related helpers.

Modifier asset details use `FComposableCameraModifierDetails`, registered on
`UComposableCameraNodeModifierDataAsset`. The customization owns `Modifiers`
through `FDetailArrayBuilder`, because class-layout customizations are not
applied to `EditInlineNew` UObjects nested inside an array. Every element is an
exact base wrapper. Its first child row is the `Use Custom Modifier Class` bool:

- Unchecked: show `Node Type`, then the selected built-in or Blueprint node's
  editable instance properties. Each property keeps its native widget and an
  `Override` checkbox. Unchecked controls remain disabled.
- Checked: show `Custom Modifier Class`, then the selected user Blueprint/C++
  subclass's editable fields, including its legacy `NodeClass` configuration.
  The exact base class, abstract classes, and deprecated classes are filtered
  from this picker.

Switching modes preserves both branches' authored data; only the selected branch
is active at runtime. Class metadata (`EditDefaultsOnly`, including
`PaletteCategory`) and transient node fields do not render in Node Type mode.

Generic entries created through the pre-fix default inline layout can contain a
`NodeClass` but no `NodeTemplate`. Opening the asset repairs that state by
creating the missing template from the selected class, then shows its property
rows. Pre-wrapper assets that directly stored a custom Modifier subclass migrate
that object into the wrapper's Custom Modifier Class branch during `PostLoad`.

Changing node type creates a fresh template and clears checked property names.
This does not participate in graph `SyncToTypeAsset` / `RebuildFromTypeAsset`:
modifier assets are durable data assets and own their templates directly.
The same Details view exposes `CameraTagQuery` through UE's native
`FGameplayTagQuery` customization. Empty query means all cameras; authored
queries can nest ALL / ANY / NONE expressions against a camera type asset's
`CameraTags` container. Camera tags remain direct durable type-asset properties,
not graph state. Legacy single camera tags and modifier tag lists migrate on
load and remain hidden from new authoring. This follows GameplayCameras'
`GameplayCameras/Public/Transitions/GameplayTagTransitionConditions.h` pattern.

Modifier Details exposes `Application Mode` above the wrapper array:

- `Reactivate Camera` is the compatibility default. Existing pose Enter/Exit
  Transition fields are visible.
- `Modify Existing Instance` is explicit opt-in. Pose Transition fields are
  hidden and Modifier Value Enter/Replace/Exit Transition fields are visible.
  Replace applies only to properties checked by both the old and new winning
  Modifiers. Null Replace shows `Legacy`: desired Enter wins when desired
  priority is at least previous priority, otherwise previous Exit. Authors use
  a zero-duration Replace to request an explicitly immediate handoff.

In in-place mode, each Node Type property row carries one compact capability
label. `Blend` means a built-in continuous value blender exists. `Step` means
the property switches once at the transition threshold. `Unsupported` means
the property has neither a matching top-level input pin nor a node-provided
runtime-mutation opt-in; its value widget stays disabled without deleting the
authored legacy value. Custom Modifier Class wrappers produce an error banner
because arbitrary Blueprint side effects cannot be interpolated or reverted.
Switching application mode never clears either transition family, node
templates, checked-property names, or Custom Modifier data.

This state remains direct durable Modifier-asset data. It does not participate
in graph `SyncToTypeAsset` / `RebuildFromTypeAsset`.

When adding an asset class, update:

- factory.
- asset definition/actions.
- thumbnail/category if needed.
- editor open path.
- docs.

## 18. Mesh Camera Layer Tool

Current low-latency paths (2026-10-07): Show request state and Edit window/mode
activation/exit remain unchanged. Save reuses current exact version-1 editor coverage
into the hidden actor's WITH_EDITORONLY_DATA EditorPreview. Cell/patch positions,
normals and Layer GUIDs contain no Profiles or render resources. Opening Show/Edit
loads exact polygons on native workers and prepares the whole document in one
result, skipping spatial-tile clipping/scheduling. Edit installs this complete
resident display in one editor/scene update, with no across-frame initial Tile
publication. Internal regions remain for subsequent local edits. Show/PIE retain
bounded component/floor-fitting budgets. Legacy/invalid Edit caches resolve source
in the same resident job; Save upgrades legacy documents. Snapshot copies, uploads and PIE
collision fitting still cost time; zero delay is not guaranteed. Source rebuild
and actor Undo increment a transient content revision; Show/PIE replace obsolete
generations even when source counts match.

Save closes the current interaction, completes the resident opening job and exact
FIFO coverage before source compaction, then reuses these CPU polygons for persistence.
Source compaction remaps shared vertex IDs and removes invalid/orphaned triangles,
keeping triangle order, Shape ownership, controls and erasures. A no-op retains
the current index. A cache miss resolves once and keeps that exact cache for editing.
Storage retains indexed runtime geometry/BVH for unchanged geometry and Layer GUID
row order; Name/Color/Profile/Channel updates also retain valid stored coverage.
An allocation-free comparison against current exact polygons rejects same-version
corrupt/stale stored coverage before taking the Save shortcut.
Enabled changes invalidate coverage but reuse query geometry; reordered GUIDs
rebuild row mapping. The explicit input boundary can still finish pending display
work. Level/external-actor package scope, failed-save rollback and successful-save
checkpoint semantics remain unchanged. Save logs preparation, runtime, preview,
package and checkpoint durations separately; no measured speedup is claimed.

Brush/Erase coverage workers directly prepare complete touched display tiles,
including empty removal results. Each job owns at most eight waiting coverage
inputs. It executes them all in order and assembles each region only at its last
update in that batch; full batches emit the latest whole state. Native immutable
buffers include worker-computed bounds, and identical shared content/bounds retain
the component's existing buffer identity. QueuePreparedRegion coalesces waiting versions
and supersedes older active assembly. No second polygon snapshot/geometry worker
separates normal coverage from publication. Source samples keep FIFO order. A
second press before released source finishes queues a separate deferred stroke;
mouse input does not flush it. Each stroke retains its own Undo transaction.
Append-only rollback stores starting counts; the first Erase captures the original
source once. UObject transaction serialization remains at stroke start.

Shape drafts/control drags and Interactive Details changes show immediate planar
fill, then budgeted collision projection supplies temporary resolved coverage.
One immutable native base is captured per drag. Surface projection starts after
initial document/restoration publication completes; planar feedback stays visible
while that base becomes ready. Preparation covers whole dirty tiles,
old/new/prior-published footprints and retained Shape erasures. The
disjoint footprints are resolved separately, without processing the gap between
them. Candidate bounds cover whole dirty cells so edge neighbors survive. Temporary
projection caps at 4096 triangles; final commits retain the 16384 limit. Latest
input replaces the waiting request while active work finishes to avoid starvation.
Preview never changes WorkingData, revision, Save data or Undo history. Cancel
restores remembered display. Release/final Details commit uses the budgeted Shape
creation/replacement task; complete source/coverage apply together in one
transaction. Save remains unavailable during pending Shape completion. Unsupported
or density-rejected final geometry preserves existing source.

Name/Profile/Channel advance metadata without clipping, compaction or geometry
reconstruction. Color updates the existing component's material parameter during
Interactive notifications too. Channel applies prospectively and cancels pending
Shapes; it does not move saved triangles. Enabled/reorder/delete remain structural.
Property transactions capture preview history after completion, so color sliders
do not snapshot every intermediate value. Shape deletion cancels its queued
replacements and resolves only affected display regions.
Metadata history revisions share immutable coverage/index with separate exact
colors; the checkpoint's internal Revision records geometry provenance.

Edit proxies retain power-of-two vertex/index capacity and update used position,
tangent, UV, color and index ranges on the render thread. Actual draw counts differ
from allocated capacity. Growth or engine render-state invalidation recreates the
proxy. Stable Render submits descriptors only.
Color replacement relies on the old material proxy's destructor for cache release,
without an extra uniform-cache invalidation before destruction.
All Level viewports redraw during progress plus two tail frames, then stop.
Native Edit computation/retirement uses
a module-owned two-thread pool. Normal close cancels without joining. Module unload
stops producers, joins pools and drains render commands before code disappears.

Read-only UE5.6 references: LandscapeEditor/Private/LandscapeEdModePaintTools.cpp
and LandscapeEdModeTools.h (cached rectangular updates); MeshModelingTools/Private/
MeshVertexSculptTool.cpp (local updates); GeometryFramework/Private/Components/
MeshRenderBufferSet.cpp::TransferVertexUpdateToGPU (existing RHI buffers);
MeshPaintingToolset/Private/MeshTexturePaintingTool.cpp (preview versus persistence);
GeometryCore/Private/DynamicMesh/DynamicMesh3_Edits.cpp::CompactInPlace
(remap live vertex IDs while retaining sharing, without triangle expansion).
These supply update/lifetime principles. SavedPreview, FusedStrokePreview,
RealtimeEditing, AuthoringHierarchy, SavePreparation and existing BudgetedStroke/EditPreviewInvalidation
cover these paths. IDE/editor execution and dense-Level timing/pixel validation
remain required.

Layer Details exposes Channel next to its Profile. It is the Layer's serialized
`TEnumAsByte<ECollisionChannel>` TraceChannel, default Visibility, with engine and
project-defined collision channel names in the standard enum picker. Hover picks,
Brush ring samples, released Rectangle/Circle/Polygon creation and control-point
reprojection resolve the channel from the owning Layer GUID through one shared
trace helper. Missing Layers/invalid channels fail instead of falling back to a
different surface. Existing query-collision and minimum-normal filters remain.
Channel property edits cancel pending fills through the metadata callback,
preventing one Shape from mixing channels across projection ticks. Save/load,
Details transactions, Undo/Redo and Discard retain Channel with the Layer struct.
Changing it never automatically reprojects saved geometry; draw again or change
Shape controls to rebuild on the selected channel. Runtime membership uses baked
triangles; PIE occluder fitting keeps its separate scene-visibility policy.
`LayerTraceChannel` covers real StaticMesh collision, independent Layer routing,
Brush and all three Shape types, tagged serialization/legacy defaults and native
coverage. `DocumentUndoRedo` checks the actual Channel property handle, while
`DocumentDiscard` checks restoration of a changed Channel.

`FComposableCameraMeshLayerEdMode` owns the complete authoring workflow.

- Toolkit presents a selectable Layer list following Landscape Editor's
  `LandscapeEditorDetailCustomization_Layers.cpp` row-selection pattern.
  Clicking a row makes its stable GUID the current paint target; users never
  edit an active-layer index. Add/delete/reorder controls operate on the
  selected row. Selected Layer properties appear below it and remain available
  in every tool mode. Immediately below these properties, equal-width Draw,
  Select and Erase buttons share one horizontal row; exactly one mode is active.
- Draw defaults to Brush. Its Drawing Type menu contains only Brush, Rectangle,
  Circle and Polygon; returning from Select/Erase restores the last drawing type.
  Left mouse paints; Shift + left mouse temporarily erases in Brush mode.
  Shape and Surface settings remain together in the active tool's options panel.
  Draw shows relevant brush/shape and advanced projection/sampling fields.
  Select shows grid snapping plus the selected Shape's Details/delete controls;
  Delete Selected Shape is the first row inside Select Options, above snapping.
  Its centered label occupies a fixed 125 x 24 Slate-unit button in the left name
  column, aligned with the Shape Grid Size label below it. The numeric widget
  remains in the value column at the same fixed size.
  Erase shows radius and advanced projection depth. Operation instructions and
  shortcuts live in the mode buttons' tooltips. The Drawing Type button and each
  menu entry explain their specific gesture on hover; Layer controls, Shape
  deletion, Save and Discard also provide tooltips. Fields/panels for other modes are
  hidden. The old mixed Tool enum is absent from Details. Native Slate
  SSegmentedControl supplies the mode row, following the existing Shot Editor's
  mode-switch widget pattern. The footer shows only live draft measurements and
  validation/partial-floor feedback; an empty footer collapses. Saved/Unsaved state
  lives in tooltips. Successful edits add no
  instruction paragraph. Level path and Layer/triangle counts are available in
  the footer and Save tooltips. The footer action reads Save and uses a fixed
  125 x 24 Slate-unit green button with centered white text, 5-unit rounded corners
  and explicit normal/hover/pressed/disabled fills. Its style has static lifetime.
  Discard sits immediately to Save's right with an 8-unit gap, the same fixed
  dimensions, centered white text and rounded neutral fills. It restores all
  Layer properties/order and authored coverage/Shape controls/erasures to the
  latest successful Save, or the normalized opening document before the first
  Save. Tool preferences stay unchanged. Discard cancels active strokes/drafts
  and released creation jobs before restoring source, clears Shape selection
  and refreshes Layer/Shape Details plus viewport caches. Committed changes are
  discarded in one Undo step; draft-only cancellation adds no transaction.
  Both actions disable when there are no changes; Discard remains available
  while Shapes are completing. Its tooltip explains rollback and Ctrl+Z.
  The checkpoint is independent of the storage actor: a failed package Save may
  already have applied actor data. Discard restores that actor's previous source
  or removes the actor created by the failed attempt, without saving packages or
  clearing unrelated editor history/package dirty flags.
- A brush stamp projects a ring back onto compatible floor collision. Ring
  samples may cross component boundaries, so Landscape components and modular
  floor pieces do not create artificial paint gaps. The projection trace and
  minimum floor-normal test still reject missing or non-floor samples.
- Rectangle drags opposite corners; Circle drags center to edge. Releasing left
  mouse over valid floor queues the region for the captured Layer GUID. Polygon
  accepts successive floor clicks; Enter, double-click or an exact snapped click
  on the first point closes it. Backspace removes a point. Simple concave outlines
  are supported; crossing/touching edges and zero-area shapes are rejected.
- Shape Grid Size optionally snaps points to the document's XY grid, and snaps
  Circle radius too. Zero disables snapping. Rectangles follow the Level document's
  axes. The status displays width/height, radius or committed draft-point count.
  Cached outline previews mark the starting point and Polygon vertices on the
  initial hit's plane. Confirmation immediately adds a small triangulated fill on
  that captured plane. This disposable fill is provisional: exact floor height,
  gaps and Layer priority replace it when projection/coverage complete.
  New regions project along document Up in resumable batches, capped at 256 new
  queries / a soft 4 ms budget per editor frame (one collision query can exceed
  the time budget). Cached samples and partial leaves survive between batches.
  Multiple released regions retain independent fills and complete in order.
  Source and current coverage snapshots are processed on a worker; World collision,
  UObject access and transactions remain on the editor thread. Results install
  source and resolved coverage together in one creation transaction, avoiding a
  redundant coverage rebuild. Tick assembles and publishes that fill; Render never
  performs full-document resolution or uploads. Ready results wait for active drags/transactions.
  If an existing Shape changed, coverage is recomputed against the latest revision
  using the retained projected geometry; stale snapshots never restore old edits.
  Tool/options changes and panel focus preserve released regions. Esc/right mouse,
  Undo/Redo, Layer structural changes, lost World and mode exit discard pending
  work without waiting on its worker. Pending fill never enters saved source;
  Save and Brush/Erase mutation wait until the queue finishes. Existing Shape
  control/Details completion uses the same budgeted replacement task.
  This follows the preview/background-result separation in Epic's read-only
  ModelingComponents MeshOpPreviewHelpers.h, without moving World traces off-thread.
- Shape Sample Spacing bounds projected triangle edge lengths without rounding
  away the outline. Circle Segments defaults to 64 (12-128); its outline is an
  inscribed polygon. Projection checks vertices, edge midpoints and centroids,
  then compares those interior hits with the corner-interpolated surface. Errors
  above 1 world cm refine the longest edge, down to 2.5 cm / 16 levels;
  mixed valid/missing samples refine to 10 cm edges before dropping unsupported
  leaves. Divide error/edge thresholds by the anchor's maximum absolute scale
  for document-local Draw coordinates, keeping scaled Levels within that world
  bound. All-failed leaves remain absent. The final mesh uses projected edge
  samples and centroids on curved leaves, sharing cached edge subdivision with
  neighbors to avoid different-height T junctions. Planar leaves retain one
  triangle. Refinement and emitted geometry stay bounded by 16384 triangles;
  density failure preserves existing source rather than publishing an incomplete
  mesh. New creation retains the 256-query / soft 4 ms per-frame budget, including
  final emission. Small features missed by all samples remain approximate, and
  unresolved sharp discontinuities report partial coverage. Projection Distance
  still bounds the search around the initial floor plane; refinement never
  extends traces to another storey or bypasses Channel/floor-normal rules.
  Brush uses this same builder with a 4096-triangle stamp cap and world-centimeter
  error measurements relative to its center, preserving precision far from the
  origin. Its radius/segments define the tangent-plane outline; document-Up
  projection keeps XY stable and samples the interior instead of a center/rim
  fan. Brush projection now resumes across editor ticks with unchanged density,
  curvature tolerance and stamp cap; high-curvature stamps cost more total
  traces and an excessive stamp requests a smaller radius. There is no runtime
  camera work added. Existing triangle-only strokes need repainting to obtain
  denser source; loading does not manufacture coverage in previously omitted areas.
- Esc, right mouse, viewport focus loss, tool-setting changes, Layer selection
  changes and Layer edits cancel drafts. Preview/cancel never writes triangles
  or dirties the document. Shape validation feedback stays visible beside draft
  measurements. Failed rectangle/circle releases clear their draft; an invalid
  Polygon remains available for Backspace correction or cancellation.
- Shape building uses GeometryCore's simple-polygon triangulation, then bounded
  subdivision (256 outline points / 16384 triangles). Density is checked before
  collision projection. Invalid/oversized/entirely unsupported regions preserve
  existing authoring data. A successful commit retains a Shape GUID, controls,
  local projection plane/settings and triangle ownership in editor-only source.
  Active drafts and pending creation fills are transient. Save/reload preserves
  completed Shape editing. Legacy triangle-only data remains paintable/erasable, without invented
  controls for regions whose outlines were not retained.
  The outline/preview/commit separation follows the reference pattern in Epic's
  `MeshModelingTools/Private/DrawPolygonTool.cpp` (`OnBeginClickSequence`,
  `OnTerminateClickSequence`, `GenerateFixedPolygon`); its implementation is
  read-only reference material.
- Select ray-picks the active Layer's actual projected triangles on the nearest
  surface. Same-surface ties within 0.001 document units prefer the later
  retained Shape record, independent of triangle swaps or erase tessellation.
  ErasePickingOrder checks this tie and rejects a newer Shape on a lower floor.
  Drag the interior to translate a Shape, or a yellow control to adjust a rectangle corner,
  circle center/radius, or Polygon vertex. Moving the circle center preserves
  radius. The Selected Shape Details fields provide position, rectangle size,
  circle radius and Polygon vertices. Invalid edits retain the previous source.
  Shape presence/type controls field visibility automatically. Edit-condition
  toggles are hidden; users cannot change the internal bHasShape selection cache
  through a checkbox beside Shape Type, Position or dimensions.
  Delete or Delete Selected Shape removes that Shape's triangles and metadata.
  Control points use editor hit proxies; cached outline geometry is rebuilt on
  authoring changes, rather than allocated in ordinary viewport rendering.
- Erase clips triangles against a 32-sided circular prism, bounded by Brush
  Radius and Projection Distance. Cutting a small hole works even in a large
  triangle whose centroid lies outside the brush. Fragments interpolate heights
  and retain Layer/Shape GUIDs; other Layers survive. Shape erase masks persist
  at document-local authored positions and are reapplied after control edits.
  A disposable Edit index groups 128 source triangles per block and retains
  per-Layer bounds. Erase tests these boxes in brush coordinates, then retains
  the original descending triangle order and exact clipping for candidate blocks.
  Brush-space bounds reject distant triangles before polygon clipping; bounded
  inline polygon scratch avoids allocations at every clip plane. Triangles whose
  three brush-space corners lie safely inside the 32-gon's inscribed disk and
  depth slab skip all 34 plane splits. The apothem, rather than Brush Radius,
  bounds this conservative disk; a margin retains exact clipping near boundaries,
  including under scaled/sheared document axes. Candidate discovery precedes the
  first native rollback copy; an empty indexed region needs no such copy. Erase walks
  source triangles backward, swap-removes only real cuts from matching index/GUID
  arrays and appends their surviving fragments. Untouched/shared vertices and
  Shape/mask buffers stay in place; optional legacy Shape IDs remain optional.
  Unreferenced vertices remain until Save's existing source cleanup compacts them;
  removing all triangles clears the vertex array immediately.
- Brush/Erase stamps update only affected resolved visualization cells. Brush
  appends its new triangles into retained coverage in source order; old stamps
  are not rasterized again. Erase clears changed cells and resolves their coverage
  from indexed candidate blocks, preserving all Layer/height competition there.
  Block bounds also provide document bounds without a per-stamp vertex scan;
  append and swap-removal refresh only changed blocks. A retained grid-to-cell
  lookup preserves remote cells. Erase reports the removed polygons' XY bounds;
  surviving coverage elsewhere remains valid even when source tessellation changes.
  Grid resolution stays stable across release; substantial document growth can
  still rebuild it early. Mouse callbacks queue every spacing-qualified attempt
  with its captured Layer/channel, Brush/Shift-erase mode and geometry options.
  One Tick per editor frame advances projection, exact clipping and ready tile
  publication under a shared soft 4 ms budget. Source mutation never waits for
  cell coverage or GPU fill. After each complete mutation, FMeshLayerStrokeCoverage
  captures only new Brush triangles or indexed Erase candidates for whole dirty
  cells, plus original document bounds and grid resolution. A native worker owns
  the transferred coverage cache and runs the unchanged resolver outside the
  editor-thread budget. Consecutive append-only operations can merge in source
  order; mixed Brush/Erase retains each operation's own immutable source snapshot
  and exact order, preserving same-Layer height winners. Full invalidation/regrid
  replaces obsolete waiting coverage operations; transient grid growth is still
  recorded at every mutation. No whole-cache copy is made per stamp.
  These completed immutable inputs may start, finish and publish while a later
  Erase source task is partial. Never gate their start on the absence of a source
  task, and never snapshot the partial live source to obtain newer feedback.
  Coverage prepares complete touched native tiles directly and enqueues them before
  terminal completion when a region's last update is ready. Prepared jobs take at
  most eight waiting inputs to retain progress through long backlogs. Coverage
  itself retains every exact operation; only obsolete intermediate display assembly
  is skipped. A full/regrid batch emits its latest complete document at batch end.
  Explicit region keys retain empty removals and avoid assembling gaps between
  distant edits. Candidate snapshots filter enabled GUIDs and triangle XY bounds
  against complete dirty cells before reserving storage, preserving source order,
  edge-cell neighbors and lower floors. Both ordinary and opening coverage workers
  produce immutable shared geometry with precomputed bounds; publication compares
  actual vertex/index attributes and bounds to reuse identical geometry.
  Waiting display versions coalesce; source retains FIFO.
  Legacy snapshot/assembly workers remain fallback.
  Each Tick services ready publication before more source work, avoiding starvation
  behind a long drag queue. No accepted sample is dropped or reordered. Release
  lets outstanding source finish under the same budget, then closes the one stroke
  transaction. Derived coverage and tile publication continue independently in
  mode Tick; starting another stroke does not synchronously drain either. It retains
  the current coverage/controls
  and redraws other viewports. The document footer reports source completion while queued.
  Explicit Save/tool/focus/close boundaries flush pending stamps before continuing;
  Settings' native OnBeforeEdit also flushes before changing Layer arrays or opening
  add/delete/reorder/toggle transactions. Details changes use the same pre-edit
  boundary. Layer indices cannot move under pending coverage, and the stroke's Undo
  stays separate from the following Layer operation. Undo restoration bypasses it.
  Esc/right mouse and Discard cancel pending work and restore the entire checkpoint.
  Controls did not move during brush/erase. Cancel/Undo/Layer and existing Shape
  changes still invalidate the full cache and rebuild controls for restored/new
  source. Undo/cancel, document replacement, Shape changes and Save compaction
  also reset the native index, including rewrites with unchanged array counts.
  New Shape creation installs its worker-computed coverage cache instead.
  Empty-region attempts also respect brush spacing. `IncrementalVisualization`
  compares regional results with full builds; `EraseBroadPhase` checks expensive
  clip counts against distant geometry; `StrokeVisualizationRefresh` exercises
  the actual Erase input path, release and no-op spacing. IDE/editor execution
  and dense-Level brush/erase smoke testing remain required.
  EraseLocalVisualization checks bounded cell work for a small cut in a large
  triangle, retained remote coverage and lower-Layer reveal. DisjointPreviewPatches
  checks that separation tests prevent fragment growth in non-overlapping stamps.
  BrushAppendCoverage compares long append sequences, stacked/disabled Layers
  and grid growth with full builds. IndexedEraseCoverage checks local work,
  restored source and lower-Layer reveal; IndexedEraseEquivalence compares exact
  geometry/ownership/Shape erasures with the unindexed path after repeated cuts.
  Projection quality, spacing, exact clipping and visible coverage remain the same.
  BudgetedStroke checks captured options, mixed Brush/Erase FIFO, release completion,
  partial-cut cancellation, Undo/Redo and pre-reorder flushing against synchronous
  source arrays, including separate stroke/Layer transactions.
  ContinuousErasePreview holds the mouse while another source cut is partial,
  checks the earlier completed cut against an independent full preview, preserves
  the lower floor and verifies cancellation. EraseInteriorFastPath covers safe
  interior removal, 32-gon boundary slivers, slopes/affine axes, mask idempotence
  and empty indexed candidates. These tests require IDE/editor execution.
  ErasePreviewBatch checks bounded completed-prefix publication, mixed-operation
  ordering, one assembly per region per batch, exact native output/bounds, separate
  remote regions, duplicate/empty keys, local removal, cancellation and latest
  full-batch publication. RegionalSnapshotFiltering checks enabled local floors
  within mixed coarse leaves and native ownership after live-source replacement.
  BudgetedEditPreview also checks identical shared-buffer reuse and equal-count
  position changes.
  BudgetedCoverage compares resumable full/append/erase/regrid results with the
  independent complete resolver, including slopes, priority and stacked floors.
  AsyncStrokeCoverage checks immutable ownership across live source replacement,
  ordered mixed edits, merged append work, local snapshot size, empty-region
  removal, cancellation and transient grid growth. QueuedEditPreviewBatch checks
  four tiles dispatch together and retain exact vertex/index/normal/color output.
  The read-only UE5.6 references are LandscapeEditor/Private/LandscapeEdModeTools.h
  (MouseMove queues positions, Tick applies, EndTool flushes) and MeshModelingTools/
  Private/Sculpting/MeshSculptToolBase.cpp plus MeshVertexSculptTool.cpp (pending
  drag samples, Tick work and regional render updates). MeshVertexSculptTool's
  per-stamp ROI/precompute notification updates completed work during a held
  stroke; derived display is not delayed until the whole gesture ends.
  Unlike the sculpt tool's
  latest-ray policy, all existing spacing-qualified samples are retained here.
- Undo/Redo, Discard and stroke cancellation first try an exact remembered Edit
  checkpoint for the restored DocumentRevision. It includes both the immutable
  shared tile buffers/colors and the exact resolved coverage plus authoring index.
  Completed coverage is moved into the native checkpoint without copying its
  polygons on the editor thread; the native block hierarchy is copied.
  Only complete matching revisions are recorded, after publication and source
  transaction completion. Empty displays/caches are valid checkpoints too.
  Restoration queues only tiles whose buffers/color/presence changed; deleted
  regions clear in that publication pass, without waiting for whole-document
  coverage. Unchanged components keep their buffers and geometry revisions.
  Existing per-frame component publication budgets still apply. The saved checkpoint
  is pinned, plus at most 32 remembered revisions and a soft 128 MiB of unique
  historical buffers and coverage/index storage excluding the current revision;
  preserve the saved/current revisions even if those alone exceed the budget.
  Eviction/reset retires native
  memory off-thread. This is disposable native history, separate from Unreal Undo;
  it contains no UObject/asset references and never changes saved authoring data.
  A complete checkpoint restores editable coverage/index immediately, without a
  full FMeshLayerDocumentBuild. Its first Brush/Erase makes one linear mutable
  coverage copy on the native worker, then uses the existing append/regional
  resolver; subsequent queued stamps continue from that mutable result. Historical
  coverage stays immutable. A new stroke retains unfinished restored tiles,
  supersedes only touched restoration tiles and prioritizes its ready output over
  untouched restored tiles. Focus changes retain restoration; Undo/Discard and
  teardown cancel obsolete generations. A display-only checkpoint can still use
  cache-only reconstruction; uncached/evicted versions use resident rebuilding.
  RevisionEditPreviewHistory, ImmediateRestorationDisplay and
  RestoredPreviewImmediateStroke cover exact buffers/colors, untouched distant
  rows, real Discard/Undo/Redo followed immediately by Brush/Erase, native ownership
  and an append-only first stamp instead of a full-document snapshot. UE5.6
  MeshModelingTools/Private/MeshVertexSculptTool.cpp checks editable Undo state
  before OnBeginStroke and updates the changed triangle region. LandscapeEditor/
  Private/LandscapeEdModeTools.h keeps cached and actual regional edits consistent.
  These consistency/local-update principles are used without copying reference
  source or introducing a whole-document wait on mouse press.
- Opening legacy Edit and full invalidation without an exact remembered display use
  FMeshLayerDocumentBuild::StartResident. Tick captures original triangle arrays
  and GUID/enabled flags once. The worker builds the 128-triangle index, loads
  validated version-1 polygons or resolves complete legacy coverage, then prepares
  one immutable shared geometry document with native bounds. The early index is
  delivered separately so opening does not block source work behind full coverage.
  PublishPreparedDocument installs all ready regions in one editor/scene update,
  clears obsolete regions in one pass and leaves no initial publication cursor.
  Shared buffers avoid editor-thread vertex/bounds traversal; proxy initialization
  and copying run on the render thread. Direct region/Layer key cleanup avoids
  scanning all components for every region. No spatial view-order loading applies
  to the production Edit path. Explicit Start/StartSaved progressive APIs remain
  for compatibility/standalone tests, separately from production StartResident.
  The first scheduling call returns without publication. Tick polls while stationary;
  finite input never waits, while explicit source flushes may wait for the early
  index/base. Focus/tool boundaries retain an unchanged opening job.
  Brush/Erase do not cancel opening or require a whole-source coverage snapshot.
  Input stays queued until the early index is installed, including initially empty
  documents. Source stamps update that index; final base completion must not replace
  it with its older copy. Exact primary FIFO deltas await the complete base cache.
  A separate fixed-grid OpeningRegionCoverage resolves complete touched render
  regions against current source and publishes them immediately, retaining Layer
  priority, neighboring old patches and Erase holes. OpeningEditedRegions guards
  all mutated regions, even if their feedback is still pending. Base publication
  skips them, so old fill cannot overwrite Brush or resurrect Erase. Base completion
  cancels provisional work before later primary deltas publish; those deltas then
  converge from the base to current source. Metadata retags the load and applies
  current colors without resnapshotting geometry. Undo/Discard, structural source
  replacement and exit cancel both native opening pipelines. Save consumes the
  resident result before compaction; any remaining legacy snapshot is canceled
  without a revision change because compaction can alter index counts.
  No source snapshot is taken from an unfinished Erase mutation.
  Old native caches and canceled coverage/mesh jobs are retired on a worker;
  cancellation never waits or synchronously frees a whole unpublished document.
  Workers hold no UObject, Level, World, live source/mode or Profile references.
  Source copies, checkpoints, transaction serialization and initial component creation
  remain indivisible editor-thread operations; GPU initialization remains render work.
  This changes derived-cache work,
  not saved data, floor projection, Layer priority or Undo/Discard contents.
  Viewport Ctrl+Z cancels the latest unfinished stroke/Shape input first. For
  completed edits, Ctrl+Z/Ctrl+Y finishes source-only boundaries and closes the stroke
  transaction, but skips coverage, assembly and publication that restoration will
  invalidate. Other explicit editing/save/focus boundaries retain their flush
  behavior. AsyncDocumentPreviewBuild compares background/full coverage and
  ownership; RestoredDocumentPreview checks first/waiting Render, stationary
  progression, exact oriented triangle/render-attribute multisets, pending Discard,
  Undo/Redo, empty restoration, focus, compaction and source interaction.
  ResidentLoadingAndBrush checks cached/legacy single-document results, held Brush
  during opening, local Erase, stale-base protection, latest color, exact FIFO
  convergence, live scene proxies and cancellation. ProgressiveEditPreview checks
  the retained standalone progressive API. Cell/vertex ordering can differ; actual triangles,
  normals, linear color, UVs and indices within each oriented fan remain exact.
  BudgetedStroke covers source-only Undo
  preparation and exact Undo/Redo. Epic's read-only MeshVertexSculptTool.cpp
  also dispatches derived normal/octree/base-mesh rebuilds after Undo; this tool
  uses owned snapshots and revision rejection instead of live tool captures.
- Unreal transactions own Layer CRUD/properties, Shape create/edit/delete and
  brush/erase strokes. One mouse stroke groups all stamps into one transaction;
  Esc/right mouse reverts that unfinished stroke, while focus loss or switching
  tools completes it. Ctrl+Z / Ctrl+Y (also Ctrl+Shift+Z in the viewport) and
  editor/toolkit Undo/Redo refresh source, selection Details and visualization.
  A stable UObject Details proxy replaces direct pointers into the Layer array;
  PreEditChange records its document owner in the existing Details transaction.
  Final Undo/Redo callbacks refresh the proxy only after restoration completes.
  Layer and Shape Details use separate, root-property-filtered views of the same
  stable proxy; Layer properties never leak into the Select panel, and Shape
  properties never appear in Layer Details. Tool options use a filtered settings
  view with an instanced customization that labels the active mode's options.
  All three Details refreshes coalesce in a one-shot core ticker; its weak toolkit
  callback invokes `IDetailsView::ForceRefresh` after property/Undo callbacks end.
  Interactive numeric changes do not dispatch toolkit refreshes or cancel input.
  A queued refresh also waits while an editor transaction is active, including
  a request queued before slider capture. The numeric widget survives until its
  final commit closes the transaction; destroying it mid-drag could block all
  subsequent Undo/Redo, including Erase. No unrelated transaction is force-ended.
  Closing the toolkit cancels the pending ticker. `RequestForceRefresh` belongs
  to `IPropertyUtilities`, not the UE5.6 `IDetailsView` interface.
  `SelectionDetailsRefresh` covers this deferred rebuild; its Editor compilation
  uses the exported `DetailLayoutBuilder.h` and `PropertyHandle.h` headers.
  `ToolPanels` covers actual property filtering, panel visibility, draft cancellation,
  remembered Draw type and preservation of selection/source revision on mode switches.
  It also checks hidden selection-state toggles. `ToolSliderTransactions` drives
  numeric property handles using UE's slider Begin/Interactive/final-commit sequence
  across editor ticks in Draw, Select and Erase, checking stable layout and completed
  transactions. `DocumentUndoRedo` uses the actual PaintAtHover Erase mutation path,
  without manual revision changes, and covers Shape edits/deletion and every
  retained Shape creation type. Floor projection/physical mouse interaction still
  require the editor smoke test on an actual Level.
  Revision GUID comparison restores clean/dirty state across the Save checkpoint.
  PropertyEditor `PropertyHandleImpl.cpp` and UnrealEd `FScopedTransaction` /
  `FEditorUndoClient` provide the read-only UE5.6 transaction reference.
- Viewport visualization uses an anchor-local grid as a spatial update index.
  Triangles are clipped to each cell; boundary cells retain actual convex
  polygons rather than filling entire squares. One XY cell indexes patches at
  every elevation. Pairwise subtraction clips the cut polygon to the region
  where its plane height differs from the subject by at most 5 local units,
  using two linear half-planes. Neither a grid-center extrapolation nor one
  representative height selects a whole patch's surface. Repeated same-surface
  stamps emit no overlapping fill, and the first enabled Layer owns each covered
  point; separated floors retain their own coverage. Lower rows remain visible
  in uncovered parts of the same cell; disabling an upper row reveals the next
  one. Fully covered coplanar single-Layer cells collapse to one quad and skip
  redundant stamps only after all incoming vertices pass the height test.
  Coarsening the index does not round silhouettes or change height ownership.
  Convex separation checks reject disjoint/touching footprints before subtraction,
  preserving their original polygons rather than splitting on unrelated edge lines.
- Cached convex patches define filled meshes grouped by Layer color.
  Both editor and PIE preview use the same fan-triangulated polygons, with a
  document-Z offset that preserves shared XY boundaries. Clipping happens on
  authoring/cache rebuilds, never in ordinary viewport rendering. Circle/brush
  outlines still follow their authored polygon segmentation and floor sampling.
  VisualizationBoundary covers sloped oblique edges, winding, runtime preview,
  coarser grids and sub-cell erase holes; VisualizationPartialOverlap covers
  within-cell Layer priority and repeated-stamp de-duplication.
  VisualizationUnevenSurface covers extrapolated-center false separation,
  height bands crossing a cell, repeats, priority order, coarse grids and stacked
  floors. ShapeCurvatureAndSeams checks actual saved-query heights, complete area,
  shared-edge continuity, resumable budgets and failure preservation;
  UnevenBrushProjection exercises the real Brush/complex collision/storage path
  on a curved StaticMesh far from the world origin.
  They do not alter stored authoring/runtime triangle data. Edit mode rebuilds
  its coverage after source or structural Layer changes. Edit fill
  uses transient Level-owned UComposableCameraMeshLayerEditPreviewComponents,
  grouped by Layer and 32 x 32 coverage-cell tiles. Each scene proxy retains its
  vertex/index buffers. Ordinary Render checks readiness without walking cells,
  building FDynamicMeshBuilder geometry or uploading vertices. Brush/Erase
  assemble only complete tiles touching their dirty bounds, retaining remote
  components/buffers. During Brush/Erase, coverage workers directly assemble complete
  touched tiles. They retain no World, mode, Layer UObject or Profile. Ordinary Tick
  never waits. New ready geometry supersedes obsolete waiting/active assembly while
  retaining remote restore work. Full/regrid refresh cancels obsolete generations.
  The synchronous/resumable publisher remains for document
  boundaries and tests. Unchanged render attributes/color skip buffer replacement.
  Full and regional traversal retain their previous cell/patch order. Grid-size
  changes force a complete refresh. Undo/Redo,
  cancelled strokes, Discard and structural Layer edits
  restore or invalidate coverage and fill. Ready Shape results publish prepared fill
  together with their resolved coverage. The original PDI fill remains
  a fallback if component publication is unavailable.
  Geometry uses the same fan indices, packed patch normals/tangents, white
  vertex color, zero UV and document-Z offset. FColoredMaterialRenderProxy
  preserves floating-point Layer RGB/alpha and the existing alpha clamp, without
  an FColor round trip. Mesh batches disable backface culling rather than adding
  reverse triangles; original depth-tested GeomMaterial stays unchanged. The
  transient actor is hidden from the Outliner, cannot be picked or collide, is
  visible in Game View and is excluded from PIE duplication. Scene captures,
  shadow/ray-tracing/navigation and temporal primitive occlusion are excluded.
  Existing PDI draft fills, outlines, control hit proxies and hover circles stay
  on their original paths. Edit does not opt into Show's Landscape LOD override.
  Exit unregisters components through their actor; no strong references outlive
  their Level. EditPreviewPersistentBuffers, EditPreviewRegionalUpdates and
  EditPreviewInvalidation cover cache reuse, colors/heights/ownership, Game View,
  regional holes, cleanup and the actual mode's no-PDI-fill idle path.
  BudgetedEditPreview checks tiny slices, retained old fill, unchanged-buffer reuse,
  negative coordinates, exact colors and empty/cancelled updates.
  QueuedEditPreviewSnapshots checks coalescing, ownership across live cache mutation,
  worker dispatch without waiting, exact output and cancellation without resurrection.
  Saved-cache opening skips source resolution; legacy opening stays asynchronous.
  Transaction snapshots/checkpoints and explicit boundary flushes still cost time.
  Single queries, polygon operations and completed tile uploads can exceed soft
  budgets; target-Level timing remains required.
  Read-only Preview
  snapshots each loaded storage document and resolves its geometry on a worker.
  The worker retains no World, actor or Profile; it copies only vertices, indices,
  triangle Layer indices and Layer enabled/color/identity metadata, not the query
  BVH. One dedicated low-priority worker, created at module startup, prevents
  simultaneous documents/worlds saturating the shared engine pool. A cold build
  computes document bounds and bins source triangles into 32-by-32-cell tiles
  on the same global grid as full resolution. Tiles near the captured editor/PIE
  view resolve first; distance controls order only, never eligibility.
  Before PIE's first camera update, the possessed Pawn
  supplies the focus instead of an uninitialized camera origin. No runtime query
  is performed by this ordering policy. Each tile
  processes all candidate Layers in original source order before exporting
  final coverage, preserving slope heights, priority, holes and seams. Its final
  meshes enter a per-job SPSC queue immediately, without waiting for whole-document
  clipping/export or future completion. The first occupied tile tries an
  8-by-8-cell final region near the focus first, excluding those cells from the
  rest of that tile to avoid duplicate fill. Editor native
  chunks of at most 1024 triangles, counting both faces, are prepared there too;
  the large intermediate resolved grid is destroyed on the worker.
  Resolved local meshes are reused across editor/PIE and repeated Show requests
  when exact source arrays and Layer identity/enabled/color snapshots match.
  The worker owns a four-entry LRU with a 64 MiB retained-buffer budget; no World,
  actor, material or Profile enters that cache. Changed source values invalidate
  it even with unchanged counts. Tiled and complete-reference layouts have distinct
  cache identities; cached tiles copy/publish individually in the new view's order.
  No whole-output copy precedes the first cached batch. PIE fits its disposable copy to
  its actual World; world-specific fitted vertices are never shared.
  Enter/Render perform no clipping or complete-document mesh build. The core
  Show ticker and EdMode Tick call the same AdvancePreview, guarded once per
  GFrameCounter. Static or Slate-throttled viewport ticks do not gate discovery,
  batch adoption or publication. Ready results register under a soft 2 ms
  budget, with a finite safety cap of 16 chunks per frame across viewports. Editor
  previews use persistent source-Level-owned transient Actors and `GeomMaterial`;
  their components cannot be selected. Temporary editor Actors do not dirty the
  Level. They are visible in both ordinary editor views and Game View (G).
  Do not mark them Hidden In Game or editor-only: either flag makes the engine's
  scene-proxy Game View visibility gate reject an otherwise registered mesh.
  `RF_DuplicateTransient` and `bIgnoreInPIE` exclude the editor preview from PIE
  duplication instead; PIE builds its own depth-tested overlays.
  PDI authoring disabled backface culling, whereas a DynamicMesh component follows
  material culling. For one-sided GeomMaterial the worker therefore adds a
  disconnected reverse-winding copy at exactly the same positions. This preserves
  above/below visibility without moving source heights or changing PIE geometry.
  A two-sided editor material receives only the original faces, avoiding doubled
  translucent opacity. Material policy is captured on the game thread as a bool;
  the worker never reads the material UObject.
  Finished editor meshes reuse their buffers instead of rebuilding PDI geometry.
  Successful publication invalidates Level viewports so non-realtime views also
  display gradual progress. Two subsequent core frames also invalidate them to
  cover deferred render-state updates; completed stable caches stop redrawing.
  While PlayWorld exists, unfinished editor builds cancel so PIE jobs do not
  queue behind redundant editor resolution. Displayed editor components remain;
  interrupted documents restart on return. Completed editor caches survive.
  The PIE-ending gate cannot block editor resumption after PlayWorld clears.
  StationaryPreviewPublication checks real native publication and invalidation
  without viewport Tick/input, shared-frame guarding, complete tails and redraw
  quiescence. PreviewPublicationBudget checks spare-time throughput and expiry;
  PreviewGeometryCacheInvalidation checks reuse and exact-data invalidation.
  StreamingPreviewGeometry compares streamed triangles/heights/Layers/colors
  against full resolution on intersecting slopes, separate storeys, overlapping
  paint, disabled Layers and negative tile coordinates. It also checks focus
  ordering, first-tile cancellation, snapshots, terminal ordering and cache reuse.
- Show defaults to stable Landscape LOD in ordinary viewport families that have
  published preview Actors. A scene view extension sets LandscapeLODOverride=0
  just before building the renderer, preventing distant Landscape morphing from
  diverging from cached Layer surfaces. It works in editor, G view and PIE;
  asset previews, unrelated worlds, scene captures and reflection views retain
  their own policy. This affects all Landscape rendered in that eligible view,
  not only the Layer footprint; distant terrain costs more to draw. The user
  explicitly chose complete coverage as the default. No global r.ForceLOD,
  Landscape component property, source vertex or serialized Level value changes.
  `CCS.Editor.MeshLayers.StabilizeLandscapeLOD` defaults to 1; setting it to 0
  keeps Show active while restoring normal per-view LOD. Show off, last-preview
  removal and PIE teardown stop applying the override to subsequent families.
  Weak Actor registrations happen on publication/cleanup; the view callback
  allocates nothing, rebuilds no geometry and performs no floor traces. Module
  unload alone drains in-flight render families before releasing extension code.
- `Show Mesh Camera Layers` also covers every PIE world. The editor module
  converts the same resolved runtime patches into `UDynamicMeshComponent`s on
  one transient preview Actor per storage document. The Actor belongs to the
  source PIE Level and is visible independently of its hidden storage actor.
  Each visible chunk keeps one mesh and color material, with persistent render
  buffers and per-component bounds. Geometry is uploaded on creation, rather
  than rebuilt per view/frame through the debug LineBatcher. Collision, ticking,
  navigation, shadows and ray tracing are disabled for these preview meshes.
  PIE uses the depth-tested, two-sided `DebugMeshMaterial`, so characters occlude
  the overlay. No reverse-winding mesh copy is needed. Before upload, cached
  vertices are fitted to nearby upward-facing floor collision within 100 cm
  along either direction of document Up. An all-object multi query includes
  WorldDynamic/PhysicsBody Blueprint floors. Eligible hits either block Visibility
  or belong to a rendered StaticMesh with an opaque/masked material. Visibility
  is an authoring-pick channel; a visible slab can ignore it while still hiding
  the preview. Select the nearest eligible floor, then the highest rendered
  StaticMesh within 10 world cm above that fixed floor height. This clears a
  nearly coincident slab over Landscape without climbing to another storey or
  chaining successive lifts. The previous 5 cm clearance excluded the reported
  7.070 cm Landscape/slab separation, burying a fully submitted overlay.
  Hidden/fully translucent StaticMeshes do not get
  this promotion. Non-rendered Ignore/Overlap volumes, initial penetration and
  Pawns are excluded, including Pawns with a WorldStatic collision profile.
  Fitting changes only local Z and reapplies
  the shared 1.5 cm local offset once; XY boundaries, topology and Layer colors
  remain intact. No hit retains the original position. Editor preview still uses
  `GeomMaterial`; its authoring visibility does not require PIE's scene occlusion.
- Floor fitting is a disposable construction job. All documents and PIE worlds
  share a per-ticker soft budget of 4 ms of fitting work, with a 2048-query
  safety cap so cheap queries can use the available time,
  checked between vertices. World discovery, clipping and component upload do
  not consume that fitting budget. Discover and prune all documents before
  spending it. Resumable round-robin visits fit at most 64 queries per document,
  then move to another; if the time/query budget expires, the next tick starts
  at the unserved document. One document can take several visits when no other
  work is pending. Exact source XYZ keys reuse results within the current
  geometry batch while keeping stacked floors distinct. The next batch adopts
  after fitting/publication finish for this batch; its worker-preallocated hash
  replaces the old hash without a document-sized game-thread allocation.
  Ready triangle chunks appear
  while the rest of the document is still pending; Show no longer waits for
  every vertex before creating the first preview. The first chunk contains at
  most 64 triangles; later chunks contain at most 1024. All worlds share a soft
  2 ms publication budget and a 16-chunk safety cap per tick, with one chunk per visit
  and a saved position between ticks. A large first document cannot monopolize
  fitting/publication of other ready documents or Level Instances. A soft 2 ms upload
  budget is checked between chunk registrations. Background clipping/export and
  projection-hash preallocation happen per tile batch before game-thread fitting;
  later tiles can still be resolving. The 64-triangle startup size applies only
  to the first document publication, not again on each tile. A terminal build
  result follows all queued batches and carries complete triangle accounting.
  On fitting completion, remaining ready chunks continue to publish under the
  same budget; fitting completion is distinct from publication completion.
  Finished chunks remain in place, avoiding an end-of-load consolidation hitch.
  This retains extra components/draw calls in exchange for bounded uploads and
  per-chunk culling. Complete coverage still depends
  on document size and collision-query cost. Queries sample collision, so unsupported
  floors, render-only displacement and curvature between vertices still require
  visual inspection. Completed meshes perform no recurring floor queries.
- A lightweight ticker discovers newly streamed PIE storage actors, updates
  preview Actor transforms without rewriting geometry, and destroys previews
  no longer seen. Empty documents remain valid caches. Global caches hold only
  weak references; the Level owns the Actors and their components/materials.
  `PrePIEEnded` destroys every preview Actor before `EndPlayMap`
  starts releasing PIE scenes; the ticker rejects teardown work until
  `PostPIEStarted` begins the next session. Turning Show off, entering Edit
  mode, or unloading the editor module performs the same cleanup and cancels
  pending fitting and background geometry jobs without waiting. The module alone
  drains cancelled jobs and joins its owned worker pool before code unload.
  Future readiness alone precedes callable destruction and cannot guarantee safe
  module unloading. Source Level
  unload also owns the preview Actor, so editor state cannot retain a registered
  primitive after its Scene is released.
  The path lives only in the Editor module and never ships.
  PIEPreviewSurfaceProjection checks query budgets, shared-vertex reuse, retained
  XY/topology/colors, cancellation, Pawn rejection, stacked floors, transformed
  anchors, WorldDynamic/PhysicsBody floors, Visibility Ignore/Overlap rejection,
  construction statistics and complex collision on a StaticMesh child of an
  ordinary Actor with a separate SceneComponent root. PIEPreviewPersistentMeshes checks mesh/color fidelity, depth testing,
  two-sided material without mesh duplication, GC ownership, transform-only reuse
  independent component unregistration, and explicit point sampling of submitted,
  absent and buried coverage under a scaled, rotated, stacked document.
  PIEPreviewProgressiveMeshes checks
  registered geometry before document completion, bounded chunks/publication,
  coverage without duplication, short tails, persistent actor/component reuse
  and cancellation during construction. PIEPreviewScheduling checks a large
  first document and smaller later document share query work and publish early
  chunks, budget exhaustion resumes the unserved job, skipped jobs cannot spin,
  and empty/changed queues normalize saved positions. PIEPreviewOccludingFloor
  reproduces the user's visible Visibility-ignoring slab at Z=0 over authored
  ground at Z=-1.992, checks submitted coverage above the slab while native
  Layer queries retain their source height, and rejects promotion to a distinct
  upper storey, hidden owners/components and translucent slabs. The same test
  covers the deeper reported native height -7.070 against slab Z=0, checks submitted
  Z=1.5 and unchanged camera-query height. It also checks
  the fixed promotion band and standalone Visibility-ignoring floor eligibility.
  AsyncPreviewBuild checks worker execution, snapshot isolation, no Profile
  retention, replacement/cancellation, complete clipped coverage/colors, bounded
  native chunks and full PIE tail publication. EditorPreviewPublication checks
  world routing, temporary Level ownership, no package dirtiness, editor material,
  non-selectability, bounded publication and unregistration. It also queries the
  actual render proxy's draw relevance in normal editor and Game View families,
  reproduces both legacy hiding flags independently, and checks PIE-duplication
  exclusion separately from visibility. EditorPreviewBackfaces checks both
  windings, disconnected reverse vertices, unchanged heights/XY, a mirrored
  rotated document, bounded uploads and short tails. AsyncPreviewBuild and
  EditorPreviewPublication also check the actual editor material's face count.
  Frame times
  and visual appearance still require an IDE-built PIE smoke test.
  PreviewLandscapeLODStability tests the real extension callback with published
  editor/PIE components, near/far origins, editor Game View, unrelated/asset/Game
  worlds, capture exclusions, opt-out, Show off/on, PIE teardown/re-entry, last
  Actor removal and destruction. It also verifies unchanged mesh height, global
  r.ForceLOD and Level package cleanliness. Pixel coverage and terrain cost need
  visual verification with r.ForceLOD restored to -1 before testing the fix.
- `CCS.Editor.MeshLayers.DumpPIEPreview` logs each cached document's pending state,
  first-batch adoption delay (`FirstGeometrySec`, -1 until a tile is ready),
  background geometry state, registered component count, submitted/expected triangle counts, actual fitting
  queries, misses, accepted non-static floors and signed world-space floor
  correction range. Zero is included in the range. Completed previews should
  report `Pending=0` and matching triangle counts. Native triangle insertion
  failures also emit a construction-time warning. With a player Pawn, the command
  also samples its collision foot point: complex/simple floor fitting and raw
  complex hits, Actor/component identity, collision responses, mesh/material
  assets, native Layer heights and submitted preview height. Nearby StaticMesh
  bounds candidates are listed even if the collision query never hits them;
  candidates alone do not prove triangle coverage. Native Layer queries start
  100 world cm above the foot and search down 600 cm. Submitted mesh sampling
  intersects a world-Z segment extending 500 cm each side of the foot and chooses
  the nearest intersected triangle height; intersections are not unique floors.
  `DepthComparable=1` requires both submitted coverage and an accepted complex
  floor hit. Only then is `PreviewMinusComplexFloorCm` meaningful. No Pawn means
  point sampling is unavailable; aggregate statistics remain available. These
  explicit diagnostics perform no recurring work or runtime camera evaluation.
- The working document is transient. Existing serialized data is unchanged
  until Save.
- Save resolves the current Level, auto-creates the hidden
  `AComposableCameraMeshSurfaceStorageActor` when needed, copies authoring
  data, rebuilds runtime data, and saves the owning package.
- Exiting with dirty data asks whether to save. Declining discards the transient
  working document.
- Closing the mode panel exits the edit mode and immediately invalidates Level
  viewports. `Show Mesh Camera Layers` deterministically switches out of edit
  mode when necessary, then enables the read-only preview in one command.
- Show's checked state records user intent independently of mode activation.
  Entering Edit through either its menu or the mode selector pauses read-only
  editor/PIE previews without clearing that intent. Edit Enter/Exit notifications
  keep the pause active until the actual Exit finishes, including save/discard
  and fill teardown: UE5.6 DeactivateMode clears its active flag before deferred
  Exit. The existing preview ticker then restores the read-only mode from saved
  data using its ordinary progressive publication. No mode is activated inside
  Edit Exit. Explicitly turning Show off during Edit cancels the future resume
  and leaves Edit open. PreviewModeResumeAfterEdit tests the actual mode transitions.
- `FComposableCameraMeshLayerPreviewEdMode` renders all loaded runtime Layer
  surfaces as read-only filled overlays while the editing tool is closed.
- The visible edit mode uses `MeshCameraLayers.Mode` from
  `FComposableCameraEditorStyle`, with normal and small brushes for the Level
  Editor mode selector. Both Tools menu actions deliberately use an empty
  `FSlateIcon` to present text-only toggles.

`UComposableCameraMeshProfile` uses a dedicated Details customization with a
`Type` selector: CameraType, Modifier, Action, or Patch. Only the chosen family
appears. Switching Type retains the inactive families' serialized configuration.
Type edits and legacy-selection confirmation request a deferred full Details
rebuild (`RequestForceRefresh`) so the same open panel replaces the previous
family's fields and parameter schema on the next editor tick.

- `Camera`: directly expands an embedded
  `FComposableCameraParameterTableRow`, showing Camera Type, Transition
  Override, supported Activation fields, and the existing typed exposed
  parameter/variable override UI. Reusing the row and its
  `FComposableCameraExposedParameterValues` customization keeps DataTable and
  Mesh Profile authoring identical. The class customization hides the parent
  `Camera` struct row and its immediate children, then adds relevant child
  properties explicitly. `ShowOnlyInnerProperties` creates independent default
  child rows; hiding only the parent leaves those rows behind. Camera has one
  advanced `Activation` group; the default `Activation Params` row is suppressed.
  Transition settings are also advanced.
  The asset picker excludes Patch subclasses. Context Name stays in the shared row
  schema but is hidden here: runtime generates one readable, collision-free
  temporary Context from each Layer name and GUID. Transient/LifeTime are also
  hidden because Layer presence owns camera lifetime.
- `Modifier`: preserves the existing `ModifierAssets` array.
- `Action`: ActionAsset, bOnlyForCurrentCamera, and generated parameters from
  the Action template's exposable subclass properties. Exposure, display names,
  types and defaults follow the K2 Action node's shared reflection rule; base
  lifecycle settings remain authored on the asset. Actor/Delegate rows use
  per-player runtime source bindings; Delegate adds a function name. Object
  inputs use a class-filtered asset picker.
- `Patch`: PatchAsset, its standard ActivationParams, and generated exposed
  parameter/variable overrides using the shared Camera Type schema.

The parameter wrapper customization resolves CameraType, ActionAsset, or
PatchAsset from its parent. Asset selection/content changes rebuild the rows;
Action template property changes also refresh defaults. Unchecked overrides
display asset defaults; required Camera/Patch parameters remain enabled.
Removing a key from the selected asset schema prunes its stale overrides.
Different-family configurations are never pruned by Type switching.

Legacy mixed Camera+Modifier Profiles retain both configurations and show a
confirmation row. Selecting another Type or confirming the existing selection
acknowledges conversion; pending assets contribute no runtime effects.

The current Level is the storage scope. When editing a streamed Level or Level
Instance, the storage actor uses that Level transform as its local anchor. Users
never create, select, or edit the storage actor.

Authoring and runtime data must remain one-way:

```text
full authoring triangles + stable Layer GUIDs
  -> save/bake
  -> runtime triangles + compact Layer indices
  -> disposable native local-space BVH
```

Future mesh reduction belongs only on the right side of this boundary.
Storage rebuilds/load and both actor Undo hooks prepare query acceleration;
no BVH construction occurs inside queries or modifies authored Shapes.
PIE storage registration asynchronously
preloads only selected Profile families. Runtime uses explicit business-side
Query/Update/Clear calls with business-supplied GroundHit and GroundQueryParams.
Exact saved-triangle intersections at ImpactPoint XY must lie within ground Z
plus/minus SurfaceTolerance. Missing ground returns empty; no scene trace or
downward search for another painted floor is performed. An unpainted upper ground
cannot reveal a lower painted Layer outside tolerance. No SurfaceId/provenance
fields are added, and saved geometry needs no migration. The authoring Layer
Channel remains separate. Query never activates effects; Update handles entry/exit
and readiness only when business calls it. The current camera stays until a later
explicit Update revalidates membership and entry can proceed in order. Show Mesh
Layers remains visualization-only and does not enable automatic runtime detection.

## 19. Invariants

- `SyncToTypeAsset` and `RebuildFromTypeAsset` must stay inverse enough for
  save/load stability.
- Graph node GUIDs are durable identity.
- Runtime asset data, not transient graph data, is saved truth.
- Modifier application-mode authoring never enters the Camera Type graph
  round-trip.
- Build messages must point to authorable fixes.
- K2 generated pins must match runtime asset exposed surfaces.
- Sequencer sections must not block-load assets on eval path.
- Section-local shot overrides must not mutate shared shot assets.
- Slate/editor delegates must be unbound on teardown.
- Editor changes that alter schema, sync, serialization, or UX must update this
  document.
