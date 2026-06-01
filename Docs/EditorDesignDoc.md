# ComposableCameraSystem Editor Design

Updated: 2026-06-01

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

UncookedOnly module adds K2 nodes and graph pin widgets used in editor/PIE.

## 3. Asset Editor

`FComposableCameraTypeAssetEditorToolkit` is the main editor for:

- `UComposableCameraTypeAsset`.
- `UComposableCameraPatchTypeAsset`.

Current surfaces:

- graph tab.
- details tab.
- build messages tab.
- debug instance picker / graph overlay.
- toolbar command to open Shot Editor for selected composition framing node.

The toolkit uses `FBaseAssetToolkit`. It owns graph commands, selection sync,
save/build hooks, property-change hooks, debug ticker, and selected-instance
tracking.

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
- `UK2Node_PlayCutsceneSequence`.

These nodes generate typed pins from selected assets. They must refresh pins
when the asset changes and compile to runtime Blueprint library calls.

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
- shot outliner exposes shot targets.
- viewport tools can adjust distance, roll, anchors, and reverse-solve some
  shot fields.

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

Type asset editor can inspect runtime instances.

Flow:

```text
selected PIE/world camera instance
  -> editor debug snapshot
  -> graph node overlay
  -> details/debug panels
```

Rules:

- Do not keep strong refs to runtime cameras from editor UI unless intended.
- Clear debug state on PIE end and toolkit destruction.
- Snapshot values before painting.
- Do not deref stale runtime pointers from Slate paint.

## 17. Asset and Factory Coverage

Editor asset tooling exists for:

- camera type asset.
- patch type asset.
- transition data asset.
- transition table.
- modifier asset.
- shot asset.
- Level Sequence shot actor / related helpers.

When adding an asset class, update:

- factory.
- asset definition/actions.
- thumbnail/category if needed.
- editor open path.
- docs.

## 18. Invariants

- `SyncToTypeAsset` and `RebuildFromTypeAsset` must stay inverse enough for
  save/load stability.
- Graph node GUIDs are durable identity.
- Runtime asset data, not transient graph data, is saved truth.
- Build messages must point to authorable fixes.
- K2 generated pins must match runtime asset exposed surfaces.
- Sequencer sections must not block-load assets on eval path.
- Section-local shot overrides must not mutate shared shot assets.
- Slate/editor delegates must be unbound on teardown.
- Editor changes that alter schema, sync, serialization, or UX must update this
  document.
