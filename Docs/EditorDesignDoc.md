# ComposableCameraSystem Editor Design

Updated: 2026-10-05

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

Current behavior:

- one global tab instance.
- context swaps when user opens another shot source.
- edits write directly to the host object inside transactions.
- preview viewport resolves target actors, meshes, bounds, anchors, zones, and
  framing overlays.
- compact top bar combines asset commands, active host breadcrumb, Sequencer
  Shot dropdown, Recent dropdown, and the Drag / Free / Lock viewport mode
  selector.
- a unified status bar appears below the top bar only when the editor has
  something actionable or diagnostic to report. It currently owns Free-exit
  Save / Discard / Stay actions, reverse-solve unavailable reasons, no-Shot
  guidance, and stale-host guidance.
- viewport-local floating toolbar owns Reset, HUD, and Guides. It is anchored
  at the top-right of the viewport so the top-left diagnostic HUD remains
  readable, and it can collapse down to a small Tools button. That collapsed
  state persists per project. Reset is Free-mode-only and snaps the preview
  camera back to the current solved Shot pose without writing Shot data. HUD
  toggles diagnostic text; Guides toggles handles, framing zones, and bounds
  wireframes. The toolbar deliberately does not expose a separate Frame
  command.
- Shot dropdown is the primary sibling-shot navigator for Sequencer-backed
  contexts. It lists sibling Shot sections for the active LevelSequence in a
  searchable, track-grouped panel with current-shot checkmark and time/row
  suffixes; the editor does not keep a persistent left-side Shot outliner.
- main body is a two-pane splitter: large preview viewport plus a right-side
  pane containing a compact Quick strip above the full structure Details
  panel. Quick starts collapsed by default, then remembers its expanded /
  collapsed state per project.
- Quick strip mirrors common authoring fields only: distance, manual FOV, roll,
  placement screen position, and aim screen position. It writes to the same
  `FComposableCameraShot` data and remains a removable experiment, not a new
  data model. Labels use full readable field names rather than abbreviations.
- The full Details panel is mode-sensitive: Placement, Aim, Lens, Focus, and
  AnchorSpec rows that are ignored by the current mode collapse out of view
  instead of remaining as disabled clutter. Hidden values stay serialized and
  reappear when the user switches back to the relevant mode. Placement and Aim
  anchor specs remain visible because Focus follow modes can still consume them.
- viewport tools can adjust distance, roll, and anchor / zone handles in Drag;
  Free allows mouse camera inspection and can reverse-solve that pose back into
  Shot data when leaving the mode. Leaving Free for Drag / Lock queues the
  target mode and shows Save / Discard / Stay in the status bar instead of
  opening a modal dialog.

The editor must not write back to a shared shot asset when editing a Sequencer
asset-reference section. It edits the section-local override copy.

## 15. Target Info Customization

`FComposableCameraTargetInfo` details customization supports:

- actor selection.
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
  full rebuild in the next Render. Ready results wait for active drags/transactions.
  If an existing Shape changed, coverage is recomputed against the latest revision
  using the retained projected geometry; stale snapshots never restore old edits.
  Tool/options changes and panel focus preserve released regions. Esc/right mouse,
  Undo/Redo, Layer structural changes, lost World and mode exit discard pending
  work without waiting on its worker. Pending fill never enters saved source;
  Save and Brush/Erase mutation wait until the queue finishes. Existing Shape
  control/Details edits retain their synchronous transaction path.
  This follows the preview/background-result separation in Epic's read-only
  ModelingComponents MeshOpPreviewHelpers.h, without moving World traces off-thread.
- Shape Sample Spacing bounds projected triangle edge lengths without rounding
  away the outline. Circle Segments defaults to 64 (12-128); its outline is an
  inscribed polygon. Projection checks vertices, edge midpoints and centroids,
  omitting leaves with missing/non-floor samples and reporting partial coverage.
  Small gaps/curvature below sample resolution are approximate. Projection Distance
  controls the search range around the initial floor plane.
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
  Brush-space bounds reject distant triangles before polygon clipping; bounded
  inline polygon scratch avoids allocations at every clip plane. Erase walks
  source triangles backward, swap-removes only real cuts from matching index/GUID
  arrays and appends their surviving fragments. Untouched/shared vertices and
  Shape/mask buffers stay in place; optional legacy Shape IDs remain optional.
  Unreferenced vertices remain until Save's existing source cleanup compacts them;
  removing all triangles clears the vertex array immediately.
- Brush/Erase stamps update only affected resolved visualization cells. A retained
  grid-to-cell lookup preserves remote cells and recomputes all Layer/height
  coverage in the changed region. Erase reports the removed polygons' XY bounds;
  surviving coverage elsewhere remains valid even when source tessellation changes.
  Grid resolution stays stable across release; substantial document growth can
  still rebuild it early. Release retains the already updated coverage cache and
  Shape controls, closes the stroke transaction and redraws other viewports.
  Controls did not move during brush/erase. Cancel/Undo/Layer and existing Shape
  changes still invalidate the full cache and rebuild controls for restored/new
  source. New Shape creation installs its worker-computed cache instead.
  Empty-region attempts also respect brush spacing. `IncrementalVisualization`
  compares regional results with full builds; `EraseBroadPhase` checks expensive
  clip counts against distant geometry; `StrokeVisualizationRefresh` exercises
  the actual Erase input path, release and no-op spacing. IDE/editor execution
  and dense-Level brush/erase smoke testing remain required.
  EraseLocalVisualization checks bounded cell work for a small cut in a large
  triangle, retained remote coverage and lower-Layer reveal. DisjointPreviewPatches
  checks that separation tests prevent fragment growth in non-overlapping stamps.
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
  polygons rather than filling entire squares. Coverage is partitioned within
  each surface-height bucket: repeated stamps emit no overlapping fill, and the
  first enabled Layer in list order owns each covered point. Lower rows remain
  visible in uncovered parts of the same cell; disabling an upper row reveals
  the next one. Fully covered coplanar single-Layer cells collapse to one quad
  and skip redundant stamps. Coarsening the index does not round the silhouette.
  Convex separation checks reject disjoint/touching footprints before subtraction,
  preserving their original polygons rather than splitting on unrelated edge lines.
- Cached convex patches build disposable filled meshes grouped by Layer color.
  Both editor and PIE preview use the same fan-triangulated polygons, with a
  document-Z offset that preserves shared XY boundaries. Clipping happens on
  authoring/cache rebuilds, never in ordinary viewport rendering. Circle/brush
  outlines still follow their authored polygon segmentation and floor sampling.
  VisualizationBoundary covers sloped oblique edges, winding, runtime preview,
  coarser grids and sub-cell erase holes; VisualizationPartialOverlap covers
  within-cell Layer priority and repeated-stamp de-duplication.
  They do not alter stored authoring/runtime triangle data. Edit mode rebuilds
  its cache only after paint, erase, or Layer data changes; Preview caches one
  resolved grid per loaded storage actor for its mode lifetime.
- `Show Mesh Camera Layers` also covers every PIE world. The editor module
  converts the same resolved runtime patches into uniquely identified batches on
  that world's persistent `ULineBatchComponent`. Meshes are submitted once, so
  transparent color cannot accumulate per frame. World-owned batches remain
  visible even though the tool-owned storage actor is hidden in game.
- A lightweight ticker discovers newly streamed PIE storage actors, rebuilds a
  storage Batch only when its transform changes, and clears batches no longer
  seen. `PrePIEEnded` clears every preview BatchID before `EndPlayMap`
  starts releasing PIE scenes; the ticker rejects teardown work until
  `PostPIEStarted` begins the next session. Turning Show off, switching to Edit
  mode, or unloading the editor module performs the same batch cleanup. The
  batcher itself remains owned by `UWorld`, so editor state cannot outlive its
  Scene.
  The path lives only in the Editor module and never ships.
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
```

Future mesh reduction belongs only on the right side of this boundary.

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
