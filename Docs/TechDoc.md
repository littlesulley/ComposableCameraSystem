# ComposableCameraSystem Tech Notes

Updated: 2026-10-11

Purpose: compact implementation reference. Keep this file current when code
patterns, public APIs, hot-path rules, node catalogs, or gotchas change.

## 1. Module Map

Runtime module: `Source/ComposableCameraSystem`

- `Core`: PCM, context stack, director, evaluation tree, runtime data block,
  parameter block, type-asset instantiation.
- `Cameras`: runtime camera actor and camera type asset interfaces.
- `Nodes`: camera and compute node implementations.
- `Transitions`: transition data asset and transition classes.
- `Modifiers`: PCM-level modifier manager.
- `Actions`, `AsyncActions`: Blueprint-facing camera actions.
- `DataAssets`: type assets, patch assets, shot assets, transition table.
- `Patches`: patch handle, manager, instance, envelope, activation params.
- `LevelSequence`, `MovieScene`: Sequencer component, actor, tracks, sections.
- `Debug`: runtime panel, dumps, viewport draw.
- `Math`, `Interpolator`, `Utils`, `EditorHooks`.
- `MeshCamera`: Level-local painted surface data, query actor, and world
  subsystem profile application.

Editor module: `Source/ComposableCameraSystemEditor`

- asset definitions, factories, graph editor, toolkit, schema.
- details customizations, widgets, Sequencer track editors, Shot Editor.

UncookedOnly module: `Source/ComposableCameraSystemUncookedOnly`

- custom K2 nodes.
- graph pin widgets and pin type helpers.

`UK2Node_AddCameraModifier` is a fixed-pin wrapper around the existing
`UComposableCameraBlueprintLibrary::AddModifier` function. It forwards exec,
PCM, and asset pins with `MovePinLinksToIntermediate`, preserving both literal
asset defaults and connected values; the intermediate function call resolves
WorldContext through its metadata. The raw function is `BlueprintInternalUseOnly`
to hide its menu entry without removing existing Blueprint calls. The wrapper
reads its title from the asset pin and needs no separate serialized asset cache.

## 2. Runtime Data Block

`FComposableCameraRuntimeDataBlock` is the shared storage layer used by nodes,
parameters, variables, Sequencer bags, and patches.

Storage shape:

- `Storage`: flat byte pool for POD-like values.
- `StructSlots`: `FInstancedStruct` pool for non-POD struct values.
- offset maps for node input pins, output pins, internal variables, and exposed
  values.
- shape descriptors that must match before read/write.
- actor/object mirrors for GC-visible references.

Read/write rules:

- Verify slot shape.
- Verify byte bounds in all builds.
- Verify object class compatibility for object/actor reads and writes.
- Use `Try*` accessors where offset validity is not guaranteed.
- Do not trust `check()` as a shipping guard.

## 3. Pin Types

Current `EComposableCameraPinType` coverage:

- Bool.
- Int32.
- Float.
- Double.
- Vector2D, Vector3D, Vector4.
- Rotator.
- Transform.
- Actor.
- Object.
- Struct.
- Name.
- Enum.
- Delegate.

Storage conventions:

- Enum values store as `int64`.
- Name values store as `FName`.
- Delegate values live in `FComposableCameraParameterBlock::DelegateValues`.
- Struct values use POD byte storage when safe, otherwise `FInstancedStruct`.
- Actor and Object values are mirrored for GC and class-checked.

String conversion:

- DataTable activation uses `FComposableCameraParameterBlock::ApplyStringValue`.
- Supported string targets include Bool, Int32, Float, Double, Vector2D/3D/4D,
  Rotator, Transform, Struct, Object soft path, Name, and Enum.
- Actor string conversion is not supported by the DataTable path.

## 4. Parameter Blocks

`FComposableCameraParameterBlock` carries activation-time and Sequencer-time
values.

Maps:

- `Values`: scalar / POD byte values.
- `ActorValues`.
- `ObjectValues`.
- `StructValues`.
- `DelegateValues`.

The struct opts into manual reference collection. Keep that trait if maps or
delegate targets change.

Blueprint wildcard setter caveat:

- `SetParameterBlockValue` is a custom thunk.
- Literal wildcard values can lose type information.
- Prefer generated typed K2 pins / typed setters for activation nodes.

## 5. Type Asset Build

`UComposableCameraTypeAsset` owns durable graph-derived data.

Build responsibilities:

- validate node templates.
- assign and deduplicate node GUIDs.
- build exposed parameter, internal variable, and exposed variable layouts.
- build main and compute runtime data layouts.
- build pin connection maps.
- build full execution chains.
- bind delegate pins.
- apply parameter block values.

Activation path:

```text
Blueprint/K2/DataTable
  -> PCM resolves context
  -> Director spawns camera
  -> Camera.Initialize
  -> ConstructCameraFromTypeAsset
       -> duplicate node templates
       -> build runtime data block
       -> apply params/delegates
       -> assign nodes/chains
  -> modifiers
  -> FinishSpawning
  -> evaluation tree activation
```

### Node Property Modifiers

Every `Modifiers` element serializes as an exact
`UComposableCameraModifierBase` wrapper. `bUseCustomModifierClass` selects one
of two retained branches, so toggling modes does not discard either branch:

- Node Type: `NodeTemplate` owns an instanced concrete camera node
  and `OverrideProperties` stores checked `FName` property names.
- Custom Modifier Class: `CustomModifier` owns an instanced user Blueprint/C++
  subclass. The wrapper targets that object's legacy `NodeClass`, classifies it
  as post-initialize, and invokes its Blueprint `ApplyModifier` event. Generic
  template state is never consulted in this branch.

`PostLoad` converts pre-wrapper derived array elements into exact base wrappers
and duplicates the old object beneath `CustomModifier`. Generic exact-base
entries need no migration. The custom-class picker excludes the base class,
abstract classes, deprecated classes, and superseded Blueprint classes.

Camera instances and type assets expose `FGameplayTagContainer CameraTags`.
Modifier assets expose `FGameplayTagQuery CameraTagQuery`, using UE's native
recursive ALL / ANY / NONE query editor and token-stream evaluator. Empty query
means all cameras; non-empty query calls `Matches` against the full camera tag
container. The manager stores every candidate in one node-class bucket and
filters queries only when rebuilding `EffectiveModifiers`; no global bucket or
tag sentinel exists. Effective Node Type selection is a nested
`NodeClass -> PropertyName -> FModifierEntry` map. Each checked property elects
its own matching winner by asset priority; equal priority uses monotonic
registration order, preserving later-registration-wins behavior. Disjoint
properties therefore compose even when their winners come from different
assets. The selected entry keeps its registration order in the non-reflected
manager data. Equal-priority transition summaries prefer entering edges over
exiting edges, then later registration, preserving the legacy
desired-wins-on-equal replacement rule.

`NAME_None` is reserved as the whole-node key. If the legacy node-class winner
is a Custom Modifier, the effective map contains only that key for the class.
This prevents arbitrary Blueprint side effects from being silently composed
with property entries whose overlap cannot be discovered.

`EComposableCameraModifierApplyMode` is serialized on the Modifier asset.
`ReactivateCamera = 0` is the compatibility default. It keeps
`ApplyModifierToNode`, `ModifierOverrideFieldOffsets`, and camera reactivation
unchanged. `ModifyExistingInstance` routes selection changes into the running
camera's transient `UComposableCameraModifierRuntimeState`. Its durable timing
fields are Enter, Replace, and Exit Value Transition. Null Enter/Exit means
immediate; null Replace preserves the legacy priority rule by selecting desired
Enter when desired priority is at least previous priority, otherwise previous
Exit. Duration zero expresses an explicitly immediate Replace.

The in-place state duplicates one baseline node per affected runtime node and
caches one binding per checked property. Binding construction performs all
reflection and pin discovery. Per-frame work uses cached `FProperty*`, field
offset, pin identity, blend kind, transition weight, and source/target node
snapshots. Each binding also stores its current winning Modifier and asset, so
one runtime node may be driven by several assets without sharing transition
state. Multiple nodes of the same exact class receive separate states.

Pin-backed bindings register an in-place pin/property identity on the node.
`ResolveAllInputPins` continues to skip the owned field, while explicit
`GetInputPinValue<T>` returns the current modifier-owned property. The lower
RuntimeDataBlock remains unchanged and is accessed through
`TryResolveUnderlyingInputPin` / `TryCopyUnderlyingInputPinToProperty`.
Removal unregisters ownership before the normal node tick resolves the latest
wire/exposed/default value.
Null and zero-duration runtime exits keep their pending binding until
`ApplyForNode` calls `ReleaseProperty`; only the camera-construction
`bImmediate` path may apply and prune that binding synchronously.

Continuous built-in types are Float, Double, Vector2D/3D/4D, Rotator,
Transform, and LinearColor. Rotator uses quaternion slerp; Transform uses
`FTransform::Blend`. Other supported pin-backed values switch once at
`DiscreteSwitchWeight`. Non-pin properties require the node's
`SupportsInPlaceModifierProperty` opt-in. `OnModifierPropertyChanged` is the
cache-rebuild hook for opted-in configuration. Initial camera construction
suppresses this hook because normal node initialization follows immediately.

`UComposableCameraModifierTransitionBase` is a stateless timing template with
duration, linear/smooth/smoother/ease/custom-curve weight, and a discrete switch
threshold. Each binding stores elapsed time, so disjoint or interrupted
property changes can coexist without mutable transition-template state.
`ReconcileNode` resolves the desired entry for each existing binding by property
name. New ownership selects desired Enter, a changed winner selects desired
Replace, and removed ownership selects previous Exit. Unowned bindings keep
their already-running exit unchanged. Newly selected properties create new
bindings with their own winning Modifier/asset pair. The PCM production path
calls `ReconcileInPlaceEffectiveModifiersFromAssets` with the
`T_EffectiveModifier` shape. The original
`ReconcileInPlaceModifiersFromAssets(T_NodeModifier)` and
`ReconcileInPlaceModifiers(T_NodeModifier, Transition)` signatures remain
unambiguous source-compatibility paths for focused tests and external C++.
`FComposableCameraModifierUpdateResult::ModifierTransition`
remains only a compatibility summary and no longer drives PCM evaluation.
Reflection, node duplication, and binding-array growth happen only on Modifier
selection edges, never inside camera/node evaluation.

Custom Modifier Class entries are invalid in `ModifyExistingInstance`: editor
shows an error and runtime skips the entry with a warning. A mixed selection
change uses legacy camera reactivation when any changed old/new asset requires
it.

Legacy single camera `CameraTag` properties remain hidden serialized fields.
Type-asset `PostLoad` and runtime construction migrate them into `CameraTags`.
Legacy modifier `CameraTags` containers remain hidden and migrate during
`PostLoad` through `MakeQuery_MatchAnyTags`, preserving their former OR intent.

In Node Type mode, `GetTargetNodeClass` prefers the template's class. In Custom
mode, target lookup uses the nested modifier's `NodeClass` and execution stays
post-initialize. Matching stays exact, same as node-scoped actions.
`ApplyModifierPropertyToNode` is the per-property construction path;
`ApplyModifierToNode` retains the whole-wrapper compatibility path. Reflection
runs only during camera construction / reactivation, never in the per-frame
tick. Each selected name is validated through `IsNodePropertyOverridable`, then
that one property is copied.
An instanced-object property duplicates its source subobject into the runtime
node; other property types use normal `FProperty` copy semantics. It then
registers the target field offset in the node's inline modifier-override list.
The PCM-only `ConstructCameraFromTypeAsset` overload invokes a synchronous
pre-initialize callback after node/data-block setup; generic modifiers run in
that callback, before `InitializeNodes` builds interpolator / solver caches.
Custom Blueprint `ApplyModifier` callbacks keep their original post-init timing.
`ApplyCustomModifierWithPinOwnership` snapshots the node's cached non-wired pin-backed
`FProperty` values at activation, executes the callback once, and registers
only changed property/pin identities. The snapshot uses reflected property
copy/identity/destruction so struct and object pins remain valid. This work
never runs in the evaluation hot path. A changed field then survives later
`ResolveAllInputPins` calls and explicit pin reads, while untouched inputs
remain live. Wired pins are excluded because BeginPlay compute outputs may be
uninitialized until after the callback. K2 activation
values and type-asset defaults are both valid bases for the one-shot callback;
later changes to an owned input do not reapply the callback.

`ResolveAllInputPins` skips registered modifier field offsets. This makes a
checked modifier property higher priority than the same node's graph wire or
exposed parameter without per-frame reflection or allocation. The offset list
uses `TInlineAllocator<4>` and is filled only during construction.

Eligible properties are editable instance properties only. `EditDefaultsOnly`,
transient, deprecated, and `NoModifierOverride` fields are excluded. This hides
node metadata such as `PaletteCategory` and prevents stale serialized names from
changing non-runtime state.

The editor customization is registered on the Modifier data asset, not on
`UComposableCameraModifierBase`. UE class-layout customizations do not drive
`EditInlineNew` UObject children nested in an array. An asset-level
`FDetailArrayBuilder` preserves normal array controls and normalizes new null
elements to base wrappers. The first child row is `Use Custom Modifier Class`.
Unchecked shows the node-class picker plus external node-template property rows;
checked shows a filtered custom-modifier class picker plus that instance's
editable fields. Both authored branches remain serialized while only one is
active.

UE5.6 `FPropertyHandleObject::SetValue` deliberately returns `Fail` for
`EditInlineNew` property nodes. Modifier array normalization therefore writes
the single customized asset's authoritative `Modifiers[ArrayIndex]` slot before
composing that element row. Using `SetValue` here leaks UE's default polymorphic
object picker for every null or derived entry that was not already a wrapper.
The asset's `CameraTagQuery` stays in its normal Details category and uses the
engine-provided Gameplay Tags query customization.

## 6. Camera Tick

`AComposableCameraCameraBase::TickCamera` is memoized per `GFrameCounter`.

Camera tick:

1. Pass the per-frame memoization guard.
2. Advance active Modifier value clocks once.
3. Start from current camera pose.
4. Walk `FullExecChain`.
5. Run node pre-actions.
6. Apply cached in-place Modifier operations for this node.
7. Tick node.
8. Run node post-actions.
9. Apply set-variable entries.
10. Store pose and frame cache.

No in-place state means one null/empty branch and the legacy order/result is
unchanged. A reference DAG that reaches the same camera twice still advances
Modifier clocks once because the existing camera memoization guard runs first.

`TickWithInputPose` is used by patches and Sequencer patch overlays. It lets a
patch node graph consume the upstream pose instead of synthesizing from the
camera actor's current pose.

`InvalidateTickCache` is required when an external system re-enters evaluation
in the same frame after changing inputs, such as first-frame Sequencer shot
override fixes.

## 7. Compute Nodes

Compute nodes derive from `UComposableCameraComputeNodeBase`.

Rules:

- Run on the camera BeginPlay/initialization chain.
- Editor sync serializes the BeginPlay exec path into `ComputeFullExecChain`,
  with `ComputeExecutionOrder` kept as a compute-node-only projection.
- Concrete compute node display names use `Begin Play:` as the editor-facing
  prefix so they stay distinct from per-frame camera nodes without implying
  they tick every frame.
- Write data into the runtime data block.
- Do not run per frame.
- Level Sequence compatibility defaults to compute-only; LS internal camera
  path skips compute nodes after spawn because BeginPlay happened before node
  construction in that path.

## 8. Evaluation Tree

`FComposableCameraEvaluationTreeNode` is a `TVariant` wrapper with:

- leaf camera.
- reference leaf captured subtree.
- inner transition.

Memoization exists at camera and wrapper level. Reference leaf snapshots are
`TSharedPtr` tree roots. They do not recurse through the source director.

Transition collapse:

- finished inner transition promotes its right child.
- left source subtree is destroyed or moved to pending destroy as needed.
- right target remains dominant.

When adding a new variant alternative, update every manual branch and debug
builder, not only `Visit()`.

## 9. Transitions

Transition duplication must be null-checked. Null means hard cut fallback.

Transition init uses:

- current source pose.
- previous source pose.
- delta time from a guarded `SafeDeltaSeconds`.
- a cached typed outer `AComposableCameraPlayerCameraManager`, used by
  transitions that need owner-aware actor input or temporary camera
  initialization.

Fallback rule: a transition that cannot compute should return one input pose,
usually target pose for activation-style transitions. Never return a default
constructed pose as an error fallback.

`UComposableCameraCompositionPreservingTransition` wraps a
`DrivingTransition`. On begin play it captures the subject actor offset from
the source pose in source-camera local space. If the wrapper transition's
`TransitionTime` is unset or zero, it adopts the `DrivingTransition` duration so
the wrapper does not finish before the driving transition can tick. Subject
lookup uses the base transition's cached PCM when `SubjectActorSource` is
`ControllerControlledPawn`, so split-screen or multi-controller worlds do not
fall back to the wrong first world controller. Each tick:

- evaluate `DrivingTransition(CurrentSourcePose, CurrentTargetPose)`.
- take the driving pose rotation as `R'` and the driving transition percentage
  as the blend weight.
- blend non-transform pose fields from current source pose to current target
  pose.
- recompute the live target-camera local offset from
  `CurrentTargetPose` and `SubjectLocation`.
- compute camera position as
  `SubjectLocation - R'.RotateVector(Lerp(CapturedSourceOffset, LiveTargetOffset, Percentage))`.
- overwrite output rotation with `R'`.

The target offset must be live, not captured at transition start. Otherwise a
moving subject can leave the near-final preserved pose offset from
`CurrentTargetPose`, and the evaluation tree collapse back to raw B will snap.

If the subject actor cannot be captured or disappears, the transition returns
the driving pose. If `DrivingTransition` is unset, it logs and falls back to the
target pose.

`UComposableCameraPathGuidedTransition` also uses the base cached PCM when it
initializes its temporary intermediate camera. It must not ask the Blueprint
library for player index 0, because the active PCM can belong to a different
local player.

## 10. Context Stack

`EnsureContext` can reorder stack entries. It moves an existing context to top.

Pop rules:

- base cannot pop.
- inactive pop destroys immediately.
- active pop resumes previous camera in place.
- transition pop holds popped context in pending destroy.
- transient camera finish triggers auto-pop.

`PendingDestroyEntries` directors may still be reachable through captured
reference subtrees. Do not destroy them before the owning transition finishes.

## 11. Camera Patches

Runtime path:

```text
AddPatch
  -> resolve activation params from per-call overrides + asset defaults
  -> spawn transient evaluator camera
  -> Initialize(nullptr)
  -> ConstructCameraFromTypeAsset
  -> insert by layer index

Director::Evaluate
  -> tree pose
  -> PatchManager::Apply
       -> advance envelope
       -> check expiration
       -> TickWithInputPose
       -> BlendBy alpha
       -> sweep expired
```

Activation params use paired override booleans. Unchecked means asset default.
Checked means caller value wins, including literal zero.

Expiration:

- Duration after Active phase starts.
- Manual through `ExpirePatch`.
- Condition through patch asset `CanRemain`.
- `bExpireOnCameraChange`.

`ExpirePatch` and `ExpireAll` share `ExpirePatchInstance`. Bulk cleanup walks
`ActivePatches` directly, even after a caller-owned weak handle is collected.
Already Exiting/Expired instances retain their current duration and clock;
non-negative overrides apply only when starting an exit. Removal and evaluator
destruction remain in `Apply`'s sweep.

Sequencer patch sections use a separate overlay map on
`UComposableCameraLevelSequenceComponent`. They sort by effective layer index,
apply latest parameter bags, tick evaluators, and blend by section envelope
alpha.

## 12. Level Sequence

Main types:

- `AComposableCameraLevelSequenceActor`.
- `UComposableCameraLevelSequenceComponent`.
- `FComposableCameraTypeAssetReference`.
- `UMovieSceneComposableCameraShotSection`.
- `UMovieSceneComposableCameraPatchSection`.
- corresponding tracks and track instances.

LS component rules:

- Creates an internal transient `AComposableCameraCameraBase`.
- Initializes with `Manager=nullptr`.
- Suppresses actor tick.
- Rebuilds parameter/variable bags from the TypeAsset.
- Reapplies bags to runtime data each tick.
- Applies shot overrides before `TickCamera`.
- Applies patch overlays after `TickCamera`.
- Projects final pose to `UCineCameraComponent`.
- Destroys internal camera and overlay evaluators on unregister/end play.

Hot-path asset resolution:

- Shot section soft references are cached off the eval path.
- Eval path does not call blocking `LoadSynchronous`.
- Null cached transition means hard cut / no blend.

## 13. Shot Solver

Core types:

- `FComposableCameraTargetInfo`.
- `FComposableCameraShotTarget`.
- `FComposableCameraShot`.
- `FComposableCameraShotSolveParams`.
- `FComposableCameraShotSolveResult`.
- `UComposableCameraShotSolver`.
- `UComposableCameraCompositionFramingNode`.

Pipeline:

```text
resolve targets
  -> anchor
  -> placement
  -> aim
  -> lens/FOV
  -> focus
  -> roll
```

The solver is mostly closed-form. Screen-space zones use prior pose/state for
damping. Keep target, placement, aim, lens, focus, and roll ownership separate.

## 14. Editor Round Trip

Graph source of truth in editor:

- Designers edit `UComposableCameraNodeGraph`.
- Save/build calls `SyncToTypeAsset`.
- Asset open/load calls `RebuildFromTypeAsset`.
- `EditorGraph` is transient.
- Runtime data lives on `UComposableCameraTypeAsset`.

Any new editor-side state needs both directions:

- Graph -> TypeAsset.
- TypeAsset -> Graph.

GUID stability matters. Never regenerate node identity unless the node truly is
new.

## 15. Debug

Global editor tools menu:

- `FComposableCameraEditorToolsMenu` owns the Level Editor's
  `Tools -> Composable Camera System` submenu and its two editor launchers.
  Other tool registrars extend its shared `MenuName` in dedicated sections,
  retaining their own command lists, checked/enabled predicates, menu owners,
  and startup callback handles. Register the shared menu before contributors;
  unregister contributors before the shared owner. Menu callbacks and entries
  are removed at module shutdown through ToolMenus ownership.
- Global Nomad tab spawners use Hidden menu type. Explicit ToolMenus actions
  call `TryInvokeTab`, preserving singleton tabs and the Shot Editor context.
- The root Tools submenu resolves `ComposableCamera.Tools` and `.Small` from
  `FComposableCameraEditorStyle`, using `Icons/ComposableCamera-Tools.svg` at
  20/16 pixels. Its operator/camera/exhaust artwork uses basic vector shapes
  with a transparent background for compact menus.
- `FComposableCameraEditorStyle` registers `ComposableCamera.EditWindow` and
  `.Small` vector brushes from `Icons/ComposableCamera-EditWindow.svg` at 20/16
  pixels. Live Edit, the Tools Edit Window launcher and its Nomad tab resolve
  this same tuning-panel-and-pencil icon through the plugin style set.

Editor console controls:

- `FComposableCameraSystemEditWindow` registers a global Nomad tab through
  `FGlobalTabmanager`; `SComposableCameraSystemEditWindow` uses `SWidgetSwitcher`
  for Welcome (default) and opt-in Debugging pages. Welcome resource links use
  `SHyperlink` and `FPlatformProcess::LaunchURL` with owned fixed URLs. Browsing
  resources and switching pages do not modify CVars. Module shutdown
  clears/closes the live tab and unregisters its spawner, releasing value
  attributes before code unload. Its launcher belongs to the shared CCS Tools
  submenu.
- Debugging adds a Show Mesh Layers checkbox row to Viewport visualization,
  using the standard label/On/Off styling. The checked/text attributes read
  `FComposableCameraMeshLayerTool::IsPreviewModeActive`; a changed requested state
  calls `TogglePreviewMode`. Reuse the Tools menu action and live state rather
  than mirroring the preview flag or routing this editor mode through a CVar.
  Row construction filters its action name, label and help against search and
  includes it in section/total counts, including when no console rows match.
- `FComposableCameraConsoleControls` discovers `CCS.*` through
  `IConsoleManager::ForEachConsoleObjectThatStartsWith`. It stores names/help,
  never borrowed `IConsoleObject*` pointers. Reads, writes, resets, and command
  dispatch resolve a live object each time; unregistered and read-only variables
  cannot be edited. Values are not mirrored in a second settings object.
- Native bool variables are toggles. Existing int32 switch names plus the
  exported viewport legend supply legacy toggle metadata. Unknown int32/float
  variables remain numeric; they must not be guessed as booleans from their
  current value. Multi-color legend entries produce one control per CVar.
- UI writes use `ECVF_SetByConsole`. The adapter's programmatic ResetValue
  still reads `GetDefaultValue()`, but the window has no Reset or bulk actions.
  Every transition from Welcome into Debugging rediscovers controls; live
  values remain registry attributes. Collapsed section choices survive page
  switches and search, while matching search groups expand temporarily.
  Compact label/value rows keep console names/help in tooltips.
- The window keeps an exact-name presentation priority list for the six common
  debugging switches. It sorts its owned controls by group, common priority,
  then console name on discovery. The same list selects `NormalFontBold` and a
  small neutral Common badge; other labels use `NormalFont`. No control is
  duplicated, and priority/badges do not change registry values or legend colors.
- Commands execute their registered delegate via `IConsoleCommand::Execute`.
  `FParse::Token` preserves quoted arguments. A weak selected world or live
  Auto resolution provides world context; runtime dumps reject editor and
  tearing-down worlds. Explicit UI selections must still have an engine world
  context, so ending PIE does not silently retarget commands to another instance.
  The selector lives in Runtime Inspection and appears only for multiple game
  worlds or an explicit selection; a single game world resolves automatically.
  Execute's bool reports dispatch, not success of its void command delegate.
  Discovery and row construction occur on Debugging entry/search events, not in
  camera Tick. Slate value attributes keep existing rows live.

Runtime debug:

- `FComposableCameraContextStackSnapshot`.
- flattened DFS tree snapshots.
- patch snapshots from PCM path and Sequencer path.
- runtime panel and pose history panel.
- Modifier panel rows use one structured group per exact target node class.
  Candidates appear once and are sorted by status, priority, then asset name.
  `ACTIVE`, `PARTIAL`, `UNSUPPORTED`, `SHADOWED`, `FILTERED`, `NO NODE`, and
  `DESTROYED` expose selection outcome directly. `PARTIAL` means the candidate
  won only a subset of its authored properties. Each two-line card puts the compact
  TagQuery scope beside the Modifier name, then shows
  enter/replace/exit Blend on its own clipped full-width row. Apply mode and
  priority remain on the identity row. Below the candidate cards, active
  in-place bindings show one two-line row per property: owner and transition
  phase/time progress, then clipped current and target values. Exiting pin
  bindings label the target `Live Lower`; non-pin exits label it `Baseline`.
  Bindings remain visible during exit even after their last registered
  candidate is removed. A `Last Change` row reports the PCM's last selection
  decision for this exact camera; reactivation names the first changed legacy
  asset and property that required a new instance.
  Canvas labels use measured pixel width plus a fixed gap. Every right-aligned
  field shares a content-right safety inset so text shadows and glyph bearings
  cannot touch or cross the group/region edge;
  fixed-width label columns can overlap proportional-font values. Do not return
  to duplicated `Effective` plus `All` text lists: they hide the reason a
  candidate lost and waste vertical space.
- Action panel rows use compact two-line cards. Identity, camera scope, and
  optional node target share the first line with the execution phase; expiration
  rules use one measured label/value row below. The source `TSet` has no display
  order, so snapshots sort by execution phase, identity, then stable object key
  before drawing.
  Missing targets use problem coloring without changing action execution.
- Patch panel rows use lifecycle-colored cards. The identity row separates phase,
  asset/source/Sequencer host, and layer into clipped columns; Alpha and
  meaningful Time values keep their progress bars; expiration rules use a
  measured label/value row. Patch snapshots retain manager/Sequencer producer
  order rather than sorting by display text. Actions, Modifiers, and Patches
  share the same content-right safety inset.
- Current Pose groups use compact two-column cards; context headers and Running
  Camera section/node rows use tinted backgrounds without side rails. Warning
  entries keep their severity rail. Tree connectors and Legend swatches retain
  their specialized layouts.
  The common body line, title, margins, and gaps use a compact density. The
  height pass packs whole regions into viewport-height pages; select a page
  with `CCS.Debug.Panel.Page` (zero-based). A single region taller than the
  viewport is clipped inside its border and marked in the page footer rather
  than drawing beyond the screen. All region renderers must honor the supplied
  body height, including Legend rows.
- `CCS.Dump.*`.
- viewport debug draw CVars.
- viewport gizmo colors live in `FComposableCameraViewportDebugColors`; the
  panel Legend reads `FComposableCameraViewportDebug::GetLegendEntries()` so
  swatches and 3D markers share one source of truth. Legend rows still require
  the matching viewport debug CVar or `*.All` shortcut, but the panel filters
  them again through `ComposableCameraViewportDebugLegendUtils`: node rows must
  match a node class on the current `RunningCamera`, and transition rows must
  match an `InnerTransition` class in the active context tree snapshot.
- `FComposableCameraDebugDrawSink` is the primitive emission adapter. The live
  sink sends line / point / sphere / box / plane / frustum calls to Unreal debug
  draw helpers and keeps solid spheres routed through
  `FComposableCameraViewportDebug::DrawSolidDebugSphere` in non-shipping builds.
  The capture sink records the same calls as `FComposableCameraDebugPrimitive`
  values for rewind trace serialization. It also returns true from
  `ShouldForceDrawAllNodeGizmos` and
  `ShouldForceDrawAllTransitionGizmos`, so trace writers capture all 3D gizmos
  without enabling or refreshing live viewport CVars. The live sink keeps the
  default false values. It is a transient C++ adapter; its non-owning `UWorld*`
  is not a `UPROPERTY`. Sphere / solid-sphere primitives
  store segment count in `Size`, line thickness in `Thickness`, and optional
  marker text in `Label`; box primitives store line thickness in `Thickness`;
  plane primitives store center in `A`, normalized normal in `B`, and
  two-dimensional extents in `Extent.X/Y`. For `CameraFrustum` primitives only,
  `Radius` stores FOV, `Size` stores ortho width, and `Thickness` stores debug
  frustum scale. Raw default constructed primitives are not valid frustums; use
  `MakeCameraFrustum`, whose scale default is 1.0.
- Rewind trace emission is editor-only even though the frame data is produced
  by runtime classes. `UE_COMPOSABLE_CAMERA_TRACE` must include `WITH_EDITOR`,
  and `ComposableCameraSystem.Build.cs` must add `TraceLog` only when
  `Target.bBuildEditor` is true. Non-editor packaged targets should not compile
  `FComposableCameraTrace`, `CCS.Debug.Trace`, ObjectTrace includes, or
  PCM / LS trace writer bodies.
- Gameplay PCM trace capture lives in `AComposableCameraPlayerCameraManager`.
  `TraceCCSEvaluationFrame` must early-return on
  `FComposableCameraTrace::IsTraceEnabled()` before reserving primitive storage.
  It records the evaluated CCS pose, context, camera type asset name, owning
  PC/pawn/view target ids, and sink-captured camera / transition gizmos.
  `TraceActiveCameraFrame` records the final `FMinimalViewInfo` after
  `FillCameraCache`. Both records share one `FPlatformTime::Cycles64()` value
  sampled at the start of `DoUpdateCamera`.
- Level Sequence trace capture lives in
  `UComposableCameraLevelSequenceComponent`. It samples one
  `FPlatformTime::Cycles64()` value before ticking the internal camera, returns
  a projection status from `ProjectPoseToCineCamera`, and emits a
  `CCS_LevelSequence` evaluation frame after projection. The function must
  early-return on `FComposableCameraTrace::IsTraceEnabled()` before reserving
  primitive storage. It captures only internal-camera gizmos; there is no LS
  context stack / director transition tree to draw.
- `FComposableCameraViewportDebug::DrawSolidDebugSphere` accepts an optional
  short `Label`. Sink-routed sphere gizmos pass the same label through the live
  sink and capture sink, and primitive stream version 2 serializes it in
  `FComposableCameraDebugPrimitive::Label`. Labels use
  `GetSphereLabelDurationSeconds() == 0.f`; do not make them persistent, or HUD
  debug text will remain at stale world positions while the sphere moves.

Editor debug:

- FComposableCameraLiveEditSession is an Editor-module FGCObject. Runtime
  camera/nodes and source identities are weak; editing, baseline and accepted
  authoring proxies are manually collected. Copy editable fields only.
  CameraNodes contains legacy raw pointers; NodeTemplates uses TObjectPtr.
- Scope/order comes from CameraNode entries in FullExecChain, with ExecutionOrder
  as the legacy fallback. Do not treat SetVariable source indices as executed
  nodes. Binding eagerly creates all proxies so every Start node is available.
- `SComposableCameraLiveEditPanel` collects a transient `UComposableCameraNodeGraph`
  through FGCObject. Its read-only SGraphEditor reuses existing camera graph node
  factories but has no owning type asset, sync callback or authoring commands.
  Only exec pins connect the horizontal chain; measured title widths determine
  spacing. A vertical SSplitter separates this canvas from the selected native
  Details view. Selection maps through runtime node indices and never replaces
  editing proxies. Editability refresh restores the selected session index after
  scanning other nodes. After PIE, cached labels/proxies remain inspectable with
  property editing disabled. Selected-node headers use only the display name
  resolved by the shared graph-node naming helper, with no prefix, sequence or
  template index. Equal SBox dimensions and content padding give Apply
  and Reset matching geometry; only Apply adds the blue color.
  Set both SButton content HAlign/VAlign to Center: the default Fill alignment
  expands the generated text block, whose text remains left-aligned. Centering
  the containing horizontal-box slot only positions the button itself. Stable
  widget tags let layout automation locate these actions independently of labels.
- Editable authoring properties are discovered without metadata opt-in.
  Native Details handles nested structs/arrays/object pickers. CopyLiveEditProperty
  copies a root value and duplicates its owned instanced references with unique
  names, a duplication seed and destination propagation flags. Two archive passes
  remap root pointers, then owned sibling references. Ordinary InstanceSubobjects
  may reuse same-name objects without copying updated contents; never use it as
  a repeated configuration-copy contract. Canonical signatures recurse
  through owned objects, structs, arrays/maps/sets; unordered entries sort before
  comparison. Nested object events resolve their top-level authoring parameter.
- Each runtime node owns editor-only transient trial storage and precomputed
  property/pin bindings. Resolve the normal input layer, then exchange each
  overridden reflected value around node FirstTick/Tick and exchange it back.
  Memswap moves relocatable reflected storage without array/struct allocation;
  bitfield booleans require reflected Get/Set rather than swapping shared bytes.
  Explicit pin readers consult trial -> Modifier -> underlying pin storage.
  Compound pins resolve their trial subobject container through cached bindings.
  AutoApplySubobjectPinValues skips active trial roots during refresh; schema
  declaration still includes every compound pin.
- Editing/removing a trial refreshes cached configuration at event time. The
  default OnLiveEditRefresh invokes initialization; FirstTick is rearmed.
  Node temporal state may restart. MixingCamera only rebuilds its child camera
  list for Cameras changes, destroying previous children first. ImpulseResolution
  rebuilds its interpolator while retaining the existing collision component.
  ControlRotate BindActionValue already deduplicates bindings in UE5.6.
- Trial storage never writes source wire/parameter slots or Modifier-owned values.
  Reset removes it and reveals current lower values. Apply rebases pending-save
  state while retaining runtime overrides. Never invalidate gameplay DAG cache.
- Actor references remap to the selected PIE world and back to authoring actors.
  Reject trial writes for missing selected-world counterparts; reject Apply for
  unmapped PIE-only references; clear those references before
  teardown. Walk only owned proxy subobjects, never external asset/actor graphs.
- Validate references by serializing only the edited reflected root into an
  editor-only reference-remapping archive. Weak/lazy/soft references retain their
  reference kind; missing references clear on teardown. Rehash reflected maps
  and sets after replacement. Apply remaps isolated authoring candidates, never
  its live editing proxy during preflight. Unedited caller/runtime references
  must not enter authoring validation.
- Apply resolves source nodes by template identity, preflights conflicts, modifies
  asset/graph/template in one transaction, copies changed properties and owned
  subobjects, updates direct/compound defaults and reconstructs pins under the
  graph sync guard, then SyncToTypeAsset. Source wires and caller values survive.
- Compare prospective pin declarations with existing wired/exposed pin names,
  directions and complete FEdGraphPinType before any authoring mutation. If the
  graph is closed, inspect durable camera and variable connection records.
  Reject the whole commit for driver loss; prune obsolete undriven compound
  overrides only under the edited root, after GraphNode::Modify.
- Native object/text pin widgets use separate default fields. Populate them with
  UEdGraphSchema_K2::GetPinDefaultValuesFromString without a new edit event.
  ReconstructPins keeps its existing default-preservation behavior for ordinary
  callers; PostEditUndo passes false to use restored authored defaults instead.
  SyncToTypeAsset must skip GIsTransacting callbacks: transaction restoration
  restores both durable asset and transient graph, and per-node PostEditUndo
  notifications must not synchronize partially reconstructed pins. Apply's final
  notification holds the sync guard to avoid a redundant toolkit sync.
  Complete Reset rebases source conflict snapshots after Undo/concurrent edits.
- Live Edit toolbar handlers use FToolUIAction and per-toolkit menu context.
  The panel displays the ordered Start chain above one selected native Details
  view, with equal-size Apply to Asset / Reset Trial actions.
  Cache row editability outside Slate paint and refresh it when runtime identity
  changes; keep the selected index stable while refreshing other node models.

- selected runtime instance picker in type asset editor.
- graph overlay of live node data.
- node tooltips append live `Runtime Parameters` from copied graph-node debug
  state. `SnapshotDebugState` prefers each declared input's resolved data-block
  slot, including exact reflected export for struct slots. It falls back to the
  runtime UPROPERTY when no slot exists; modifier-owned fields intentionally use
  that property because modifiers outrank pins. Remaining editable node and
  subobject properties follow. Runtime-data presence is tracked independently
  of active-node glow so skipped nodes remain inspectable. Slate consumes
  strings only and never follows runtime node pointers.
- `SComposableCameraGraphNode::GetToolTip` conditionally supplies a lazy
  interactive `SToolTip` only while runtime debug data exists. It keeps the
  normal `SGraphNode` tooltip path for authoring mode, caches one card for the
  current hover, and drops it from `OnToolTipClosing`. Because UE interactive
  tooltips intentionally remain open after leaving their source, the node Tick
  closes it only after neither node nor card is hovered for a short grace
  interval. Parameter text attributes capture only a weak graph-node pointer
  plus row index. Theme-aware rounded brushes live in
  `FComposableCameraEditorStyle`; do not fall back to CoreStyle's bright
  `ToolTip.Background` for this card.
- Runtime hover card content is shared by its transient `SToolTip` and a pinned
  `SWindow`. Pin detaches the existing card widget, captures the tooltip host's
  screen position, closes the reusable tooltip host, and inserts the same card
  at that position into at most one native child observer per graph-node Slate
  widget. Tooltip-host positions are already physical desktop coordinates;
  disable initial `SWindow` DPI size/position adjustment or high-DPI desktops
  scale the position twice. Repeated requests foreground it; owner destruction
  closes it. Pinned attributes read only weak graph-node state;
  active/idle/no-data status stays live without retaining PIE runtime objects.
- `SComposableCameraRuntimeDebugPanel` provides the default-left aggregate
  view. It filters copied graph-node state to active camera nodes, keeps
  expansion state by weak graph-node identity, and rebuilds `SListView` rows
  only for membership/filter/parameter-count/expansion changes. Pose and
  parameter text attributes remain live weak reads. Search matches title, class display name,
  and parameter labels. List selection is disabled; programmatic navigation
  scrolls directly and drives an outer content tint plus hit-test-invisible
  node-color overlay through a 1.25-second linear `FCurveSequence`. Every
  navigation restarts the sequence, producing clear whole-item feedback without
  selected-row blue. Headers omit parameter-count text. Non-empty membership,
  parameter-shape, and expansion refreshes arm one post-generation layout pass;
  `OnItemsRebuilt` consumes that flag and calls `RequestListRefresh` once more.
  The second pass reuses generated rows after expanded/wrapped DesiredSize values
  stabilize, recalculates collapsed-row scroll range and lower-row
  virtualization, and clears the flag before requesting refresh, preventing
  callback loops.
- Runtime-debug navigation uses
  `UComposableCameraNodeGraph::RequestShowRuntimeDebug`, a non-serialized
  multicast delegate bound by the owning toolkit. Double-click and the active-
  only `Show Debug Information` context action route through this bridge. The
  toolkit invokes `RuntimeDebugTabId`, clears hiding search text, expands the
  target row, requests scroll into view, and starts the focus fade. Remove the
  delegate in toolkit teardown before releasing the rooted graph.
- runtime previewer tab showing visible-subject-local camera relation.
- Rewind Debugger trace ingestion through the editor `Trace` folder:
  `FComposableCameraTraceModule` registers a TraceServices module,
  `FComposableCameraTraceAnalyzer` decodes `ComposableCameraSystem` trace
  events, and `FComposableCameraTraceProvider` exposes active-camera and
  CCS-evaluation point timelines for Rewind tracks. The same folder also owns
  `FComposableCameraRewindDebuggerExtension`, which toggles the CCS trace
  channel during recording and draws the recorded active camera frustum plus
  matched CCS primitives during playback, and
  `FComposableCameraRewindDebuggerTrackCreator`, which exposes a Pawn child
  track named `Composable Camera`.
- `CCS.Editor.Dump.*`.

Snapshot rule: resolve runtime pointers to names and value copies early.

Rewind provider technique:

- Provider append functions must run under `FAnalysisSessionEditScope` and call
  `Session.WriteAccessCheck()`.
- Timeline reads must run under a session read scope and call
  `Session.ReadAccessCheck()`.
- Rewind Debugger target lookup is also a trace read in UE 5.6:
  `IRewindDebugger::GetTargetActorId()` reaches `IGameplayProvider`, so CCS
  playback code must call it only while holding `FAnalysisSessionReadScope`.
- Provider timeline getters store timelines as `TSharedRef<TPointTimeline<...>>`;
  `TSharedRef::Get()` returns a reference, so return `&Timeline.Get()` when the
  getter exposes `const ITimeline<...>*`.
- Event time comes from `Context.EventTime.AsSeconds(Cycle)` so active and
  evaluation frames with the same runtime cycle align in Rewind playback.
- Serialized primitive arrays are copied out of trace event storage, decoded
  through `DeserializeComposableCameraDebugPrimitives`, and kept empty if the
  stream is malformed.
- Rewind playback drawing uses the active-camera trace as the authoritative
  rendered pose. CCS evaluation primitives are drawn only when a matching
  evaluation frame is found. Gameplay PCM frames match by PCM id plus frame
  cycle; Level Sequence evaluation frames match the active view target actor so
  they still pair when the PCM active-camera source is `Unknown`.
- Rewind 3D primitive replay submits line-batcher primitives from an
  `FTSTicker` callback, not from `UDebugDrawService`. Game viewport rendering
  flushes non-persistent line batchers before the debug-draw service fires; if
  the service submits 3D primitives, they render a frame late and visibly jitter
  while scrubbing. The debug-draw service is kept only for Canvas text labels.
- Playback frame caches are keyed by trace time and target actor id; target
  selection changes must re-query even when the scrub time has not moved.
- Primitive replay must preserve the runtime primitive payload: sphere segment
  count from `Size` clamped to `[4, 32]`, sphere label from `Label`, line / box
  thickness from `Thickness`, frustum scale from `Thickness` with a fallback of
  1.0, and plane center / normal / extents from `A`, `B`, and `Extent.X/Y`.
  The active-camera frustum synthesized by the extension uses scale 1.0 to
  match `AComposableCameraCameraBase::DrawCameraDebug`; do not use the larger
  Blueprint camera helper scale. Rewind sphere labels are projected with
  `UCanvas::Project` and drawn with `FCanvasTextItem`; do not use
  `DrawDebugString` there because it writes through `AHUD::AddDebugText`, and
  Rewind's visualized world may not have a HUD/player-controller text path.

Runtime Previewer technique:

- Camera Type Asset layout v3 opens `RuntimeDebugTabId` in the left observer
  stack and keeps `RuntimePreviewerTabId` closed in that same stack.
- `SComposableCameraRuntimePreviewer` follows the Shot Editor viewport lifetime
  pattern: widget owns `FAdvancedPreviewScene`, viewport client borrows it, and
  widget destruction clears `ViewportClient->Viewport` before draining scene
  resources.
- Toolkit `DebugTick` pushes slim `FComposableCameraRuntimePreviewData` after a
  valid `SnapshotDebugState()` call. It copies only pawn weak pointer, pawn
  velocity, visual subject transform, camera position/rotation/FOV, context,
  and active-state; it does not store full `FComposableCameraPose` in Slate
  because that pose owns post-process settings with UObject references.
- `SetPreviewData` actively refreshes proxy pose, camera markers, floor offset,
  and viewport invalidation on the same Slate/game-thread handoff. Do not make
  runtime sync depend only on `FEditorViewportClient::Tick`; docked editor
  viewports may otherwise sleep between invalidations.
- Pawn proxy transforms are translation-relative via
  `MakeTranslationRelativeTransform`: subtract the visual subject translation
  but preserve each source transform's world rotation and scale.
  Subject transform selection prefers the skeletal root bone world transform,
  then a valid static mesh component, then the pawn actor transform. The
  skeletal root-bone anchor supplies the origin location for the copied pose.
  Do not apply the subject inverse rotation to proxy transforms; doing so eats
  the character's real runtime rotation.
- Runtime camera markers use `MakeCameraPreviewTransform`: subtract subject
  translation only and preserve the runtime camera rotation from
  `Snapshot.FinalPose`. Do not transform camera rotation through the subject
  rotation; character facing, root motion, or strafe pose changes must not
  create fake camera rotation.
- The preview floor offset is derived from proxy bounds with
  `ComputeFloorOffsetForBounds`, so a Character mesh whose root sits below the
  capsule/pawn origin is not clipped by the default `FAdvancedPreviewScene`
  floor.
- Skeletal pawn proxies use `ASkeletalMeshActor` plus direct
  component-space-transform copy:
  source `GetComponentSpaceTransforms()` -> proxy
  `GetEditableComponentSpaceTransforms()` ->
  `ApplyEditedComponentSpaceTransforms()`.
- If the source/proxy transform arrays are empty or have different counts, the
  previewer destroys the skeletal proxy, switches to the static fallback marker,
  and tracks the failed skeletal mesh so it does not rebuild into the same bad
  proxy every tick.
- Proxy animation and component ticking stay disabled so the copied pose is not
  overwritten by an editor-preview animation tick.
- The observer camera is the normal `FEditorViewportClient` camera. It is not
  coupled to runtime camera data.

Shot Editor V1 authoring technique:

- `SShotEditorAuthoringPanel` and Advanced Details edit the same host Shot;
  `FComposableCameraShotAuthoringSession` re-resolves reflected storage and
  Section source modes. Its transient draft is retained through `FGCObject`.
- Root constructs authoring pages and the native viewport once, then attaches
  navigation and `SShotEditorPreviewLayout`. The layout derives from SSplitter:
  two named slots, Authoring and Preview, use native horizontal allocation with
  initial coefficients .4/.6 and minimum widths 420/280. Both use full available
  height. Native splitter allocation handles narrow panes and user dragging.
  Follow/Aim behavior, anchors/zones, Lens/Focus and Motion responses are attached
  to their parameter panel's single retained Body. Template, destination/duration
  and preset apply/restore controls are local widgets attached to their matching
  authoring pages. No PreviewSidebar argument or detached PreviewBody remains.
  Generated rows keep the whole-Shot property root and original NotifyHook;
  native field-path tags allow tests to verify attachment in the real widget tree.
  Scalar commits rebuild no container. Source/array changes refresh handles and
  subject boxes while retaining pages. Navigation rejects edit/transaction gestures.
- `ShotEditorStyle` retains native Background/Secondary chrome and shared
  CameraNodeTitle accent. Group headers use muted neutral gray (.085, .09, .10
  linear RGB), independent of the camera accent. Primary selected
  navigation uses the full accent; SecondaryTabColor mixes 65% accent into native
  Secondary gray. ActionStyle returns the native AppStyle Button style, using
  its gray palette and standard hover/pressed/disabled feedback. Native unselected
  subtabs and primary-row preview toggles keep their original palette.
  Static brushes avoid per-frame
  allocation. Tasks are 28 units high (14-unit icons, 9-point labels), Edit tabs
  are 26 units high (9-point labels), native parameter rows are 24 units high.
  Level Preview / Follow playhead are centered 104/112 x 24 buttons in reserved
  AutoWidth slots at the navigation's right. Stable tags support layout tests.
  Actions use an outer left-aligned box plus a fixed 156 x 24 inner box; a lone
  WidthOverride under a Fill slot would still stretch the button. Compose /
  Inspect / Preview copies native segmented-control shapes with themed states.
- `SShotEditorPreviewLayout` uses native SSplitter arrangement and input, with no
  custom height fractions, cached geometry probe or authoring-height reserve.
  Keep its default Visible state: SelfHitTestInvisible would disable splitter
  handle input. Skip arrangement when available width cannot fit the five-unit
  divider or height is nonpositive, avoiding negative viewport extents during
  tab restore. Native viewport pixels follow the camera-aspect frame; no SScaleBox
  scaling, page reconstruction or source refresh occurs on resize.
  AdaptivePreviewResize tests the production horizontal layout/frame hierarchy,
  full-height columns, shrinking/growth, divider allocation, DPI, live aspect and
  degenerate sizes. Invalidate(EInvalidateWidgetReason::Layout) and SlatePrepass
  process live native SBox aspect attributes before test arrangement. Native
  splitter origins round to logical pixels, so window-bound checks allow a
  one-pixel remainder while image alignment to its own preview slot stays exact.
  ParameterPageContents checks generated rows are attached to real authoring
  panels and Create/Sequence/Presets configurations remain reachable.
- `SShotEditorPreviewFrame` keeps SBox Fill alignment and reserves six-unit
  padding before camera-aspect fitting. It right-aligns the outer bezel and
  centers the actual image vertically. Native SBox fits its child after padding,
  so adding chrome does not change the camera image ratio. HAlign_Right on the
  fitting box itself can start from a zero-desired-size viewport and lose the fit.
  OnPaint draws three nested dark/highlight rectangles around current arranged
  image geometry, then paints the native image/overlays on higher layers. Use
  ToPaintGeometry(size, layout transform), respecting the widget tint and enabled
  state. One FSlateColorBrush lives with the widget; a one-slot FArrangedChildren
  probe is reserved at construction and reset/reused during paint. No cached
  last-frame geometry, brush/widget creation or source refresh occurs in paint.
  Arrangement reuses the caller's child list and skips panes no larger than the
  total bezel padding. SelfHitTestInvisible keeps the decoration out of mouse
  input. Renderer and anchor hits use the inner viewport's native view extents.
  CameraAspect and AdaptivePreviewResize check the image ratio, external bezel
  clearance, right alignment, vertical centering, DPI and tiny pane behavior.
- Subjects use native expandable whole-role areas and a full-width actor picker.
  Component / pivot and the generated Pivot / Bounds / Preview groups occupy
  vertical AutoHeight, HAlign_Fill slots with identical side padding, without
  fixed-width boxes or half-width wrap calculations. Only the whole-role header
  uses GroupHeader's gray fill/bold text. Lower groups keep SExpandableArea's
  native ExpandableArea.Border and 4/2 header padding, with SubsectionHeader's
  regular text in the same 24-unit wrapper as nested native struct/array rows.
  Body borders stay transparent. Whole-role
  and component expansion state lives in the authoring widget, keyed by page and
  role/index, so structural refreshes preserve folding without serializing Shot
  state. Native field-group folds remain in the parameter panel. The up/down
  square actions and the final Swap actors A / B action are removed in both
  Create and Edit; session reorder/swap operations remain available.
  SubjectLayout checks real production widths at small/large sizes and folding
  across refresh. Motion removes only its Aim response heading and two help
  paragraphs, retaining all native speed/enabled fields. Actor drift
  monitoring is populated once per source in Rebuild, not separately in Create
  and Edit, preserving the no-refresh scalar path. Advanced owns one Details view.
- `ComposableCamera.ShotEditor`, `.Small` and `.Thumbnail` brushes share
  Resources/Content/Icons/ComposableCamera-ShotEditor.svg; class ShotAsset
  icon/thumbnail and every existing editor launch entry use the same resource.
- `SShotEditorParameterPanel` uses `IPropertyRowGenerator` with a non-owning
  `FStructOnScope` over the full host Shot. It renders generated Placement/Aim
  and Lens/Focus child nodes with `CreateNodeWidgets`, retaining native value
  editors, edit conditions and array actions. Generator-instance customizations
  for Placement, Aim, Lens, Focus and AnchorSpec construct all mode fields without
  tree-level visibility bindings. The shared ShotDetailsVisibility predicates
  control retained Slate wrappers instead, including ancestor mode conditions.
  Global Details/Advanced customizations remain unchanged. The whole Shot
  root is required: detached Placement/Aim structs break target-index pickers'
  upward lookup of Targets. Every mode's child rows are constructed; there is no
  Orbit-only field whitelist. Follow/Aim insert the Anchor group first, before
  behavior and screen zones, using the same wrap layout and property widgets.
  Lens & Focus covers all four focus modes, custom
  anchors, FOV limits and the full aperture range. Native metadata is the only
  numeric-range authority; the old semantic limits (aperture 0.5..32, response
  0..30, manual focus <=100000) must not be reintroduced. Motion collects generated
  Speed rows and enabled switches once, showing only currently used screen zones. It preserves
  native edit conditions rather than duplicating scalar transaction code.
  Vectors stay inline; structs/arrays expand.
  Expanded paths persist in each retained parameter panel. Structural refresh
  regenerates handles through SetStructure; OnRowsRefreshed defers widget rebuild.
  Boolean/Enum/numeric edits never rebind the source or poll a layout key. Native
  tree-level mode visibility would refresh PropertyRowGenerator rows, so simply
  ignoring OnRowsRefreshed is unsafe: its previous nodes may be invalid. Real
  source/array/external/history invalidation still regenerates handles. An empty
  source stays idle until a real refresh event.
  Authoring collection refresh passes RefreshSource(true): UE5.6
  PropertyRowGenerator::SetStructure/PostSetObject rebuilds its tree synchronously,
  allowing safe widget replacement in the same tick after transactions finish.
  Keep deferred refresh for ordinary generator notifications and other callers.
  Never preserve stale property handles merely to prevent visible flashing.
  Visibility caches sibling Mode/Basis handles and ancestor conditions during
  construction; querying it creates no handles, arrays or formatted strings.
  A direct field's parent must be a matching FStructProperty: an array element
  can report the array property's owner without being that struct's direct field.
  The parent check preserves weighted-centroid entry visibility.
  Root NotifyHook snapshots the real host and refreshes preview on Interactive,
  but posts host ValueSet only on commit. PropertyEditor owns those transactions.
  Navigation, structural refresh and playhead following also gate on
  GEditor::IsTransactionActive, beyond the session's own gesture flag.
- Custom basis/anchor index rows call `PropertyHandleList` when constructed.
  UE5.6 DetailItemNode::CreatePropertyHandle otherwise cannot recover the
  custom row's property identity, even though its picker edits the correct field.
  This follows the PropertyEditor public tree API and the native value-widget
  approach in GameplayCamerasEditor/Customizations/CameraParameterDetailsCustomizations.cpp.
- Subject parameter panels locate an array element by `IPropertyHandleArray`
  and `IPropertyRowGenerator::FindTreeNode`; array position, not the repeated
  property name, identifies the correct target. All editable bounds and target
  settings are rendered, with editor-preview metadata grouped separately.
  Actor, ComponentName and BoneName remain semantic session controls so native
  soft-reference editing cannot bypass Sequencer binding creation or selection
  reset rules. BoundsContributionWeight stays a float rather than a 0/1 checkbox.
  Transient caches and diagnostic custom rows have no authoring control.
  Create and Edit keep separate retained subject panels, preserving expansion
  while handles regenerate after reorder/source refresh. Subjects roots and action rows
  are constructed once; SyncSubjects retains role cards, removes only excess tail
  slots and appends missing cards. Actor/component controls continue reading the
  current session/index. Native handles rebind immediately after collection
  changes; whole-card/component folds and scroll containers survive. The fixed
  28 x 24 native trash-icon button in each primary header calls the session removal
  transaction and remains accessible while folded, with a Delete tooltip. Its
  16-unit icon is centered with four-unit content padding and zero additional
  normal/pressed style padding: SButton otherwise adds both padding sources.
  SubjectDeleteIcon arranges real Create/Edit headers across folded/open states,
  widths and DPI, checking complete intrinsic icon size, centering and containment.
  SubjectCollection activates the real Add/Delete buttons, verifies widget identity
  and same-tick rebinding,
  writes through shifted native handles and checks history/empty-state behavior.
  Automation records
  paths/visibility when actual widgets are built, so an available-but-unrendered
  field fails coverage. RetainedBooleanEnumRows writes real native handles,
  ticks the editor generators, checks control identity across mode/Boolean
  changes and checks Undo/Redo refresh. Native controls across all Edit sections use Root NotifyHook;
  session transactions remain for semantic actor/component/bone/reorder actions.
- Subject subsection headers use the shared SubsectionHeader SBox with
  VAlign_Center. Native SExpandableArea already centers its arrow and header box,
  but a minimum-height box with Fill alignment stretches the text geometry and
  leaves its glyphs near the top. Center the text inside that box; keep its
  intrinsic line height, symmetric padding and the existing native arrow.
  SubjectHeaderAlignment arranges all four production groups in Create/Edit,
  open/closed, at narrow/wide widths and 100/150/200 percent layout scales. It
  checks both matching arrow/text centers and intrinsic text height, because
  matching the center of a stretched text box alone can conceal this bug.
- Subject removal runs in one session transaction. ShotAuthoring::RemoveTarget
  remaps all three anchor TargetIndex fields and weighted memberships plus
  BasisActorIndex/BasisSecondaryTargetIndex. Deleted direct references become
  INDEX_NONE; surviving valid indices shift, keeping actor identities and weights.
  Session removal prunes deleted/out-of-range Section overrides and shifts their
  surviving indices without removing scene object bindings or modifying shared
  presets. CanRemoveTarget checks source locks, array bounds and transaction
  idle state. SubjectRemoval covers first/middle/last slots, all reference roles,
  bindings on Inline/AssetReference sections, one-step history and rejected edits.
- Viewport commits must go through `NotifyCommittedEdit` and the session's
  `NotifyViewportValueCommit`, not broadcast the host event directly. The shared
  native scalar path guards its outer ValueSet using `bNotifyingHost`, so the
  session cannot classify its own release event as an external structural edit.
  Wheel and reverse solve use SaveToTransactionBuffer just like anchor/roll
  gestures; one undo snapshot, one release event, no OnObjectModified broadcast.
  ViewportValueCommits exercises the real wheel and EndDrag callbacks, widget
  identity/control counts, Undo and Edit/task guide selection.
- Shot viewport modes are only Compose/Drag and Inspect/Free. The native mode
  enum is transient and has no reflection or serialized representation. The
  segmented control, HUD labels, keyboard shortcuts and input handling share
  these two modes; key 3 is unhandled. CameraModes builds the real Root, verifies
  its two native radio buttons/labels and exercises the remaining shortcuts.
- Separate viewport authoring permissions from navigation: read-only Compose
  consumes mouse events, while keyboard events keep the original
  FEditorViewportClient routing. Inspect continues native mouse navigation, but
  cannot bypass CanEdit to author Roll. Validate source lifetime/permissions
  at StartRollDrag and ApplyRollDrag, not only InputKey. Process captured mouse
  releases before permission guards; Tick closes writers after a source locks,
  and host invalidation cancels transactions before dropping raw Shot access.
  Preserve pre-lock edits as one transaction with the same scalar commit guard.
  PreviewCompatibility tests production Roll/wheel input and Undo/Redo,
  retained native rows, Inspect optics/pose separation, Reset, reverse solve,
  locked section/read-only sequence writes and release after locking.
  Do not synthesize unattached keyboard/native camera navigation through
  Internal_InputKey: it constructs a scene view from real viewport geometry and
  mode tools. Verify those paths and clipboard routing in an attached UE window.
- `GetPreviewAspectRatio` reads live camera filmback/squeeze/crop or its spawnable
  template, with native CineCamera defaults for detached sources. Root binds the
  same ratio to SBox Min/MaxAspectRatio. Solver, bounds classification, anchor
  projection and reverse solve use that canonical ratio. The renderer constrains
  it locally regardless of the source camera's bConstrainAspectRatio. Pixel
  conversion derives a current view rect via FViewport::CalculateViewExtents in
  both scene modes, avoiding a stale previous-frame rectangle after resizing.
  GetOutputCamera rejects inactive/out-of-range sources and template fallback;
  reading template configuration must not activate a camera. CameraAspect tests
  crop precedence, squeeze, portrait/square/wide SBox layout and invalid filmback.
- UE5.6 `FNotifyHook` provides virtual callbacks without a virtual destructor.
  A test adapter retaining non-trivial members declares its own virtual default
  destructor (without `override`) to avoid MSVC C4265. PersistentParameterPages
  asserts that destructor contract at compile time. The adapter remains
  stack-owned; PropertyEditor receives only a borrowed notification pointer.
- Subjects append controls sit outside the enabled existing-card list and before
  the empty-state branch, so no-source/zero-target views can add their first slot.
  `AppendTargets` creates a GC-tracked scratch source when needed, validates the
  complete input batch and focused Sequencer bindings before starting a single
  transaction, then creates/reuses bindings and appends prepared target records.
  Existing target values, anchor membership and indices are left intact. Empty
  slots need no open Sequencer; actor assignments do. Existing valid binding IDs
  survive; stale overrides in newly occupied indices are removed so an empty
  slot cannot inherit an unrelated actor. Binding creation failure reverts the
  entire operation. Native/session gestures and read-only/locked hosts reject
  append. No template application is implicit in these incremental actions.
- Value updates and structure updates are distinct. Root NotifyHook delegates
  native commits to `NotifyNativePropertyChange`: Interactive writes only request
  preview; commit posts one outer host event under a scoped self-notification
  guard. That forwarded event must not be mistaken for an external replacement.
  Finalized/Snapshot transaction events request preview only; UndoRedo requests
  a deferred handle refresh. External host changes and array operations remain
  structural. Root rebinds the viewport only for an actual Shot/host change.
  Other structural edits refresh Details and authoring handles after gestures.
  Every parameter section retains all mode fields; shared Slate visibility
  predicates and native enabled attributes update Boolean/Enum/numeric controls
  in place. No layout key or mode-triggered source rebind remains.
  View-only toggles do not dirty assets or create Undo entries. Source swaps end
  viewport gestures against the previous host before rebinding.
- Paused Section preview copies effective bindings into a reused editor buffer,
  replaces only an existing component override's Shot value and zero-delta
  evaluates the complete LS pipeline. Row, transition and alpha survive; the
  operation cannot revive inactive sections. Pending edits while playing are
  consumed by normal Sequencer evaluation. The gameplay evaluation DAG and
  live-edit trial machinery remain independent.
- Level preview uses `FEditorViewportClient::GetWorld` to render the real scene
  and the native camera's `FMinimalViewInfo`. The Shot client enters game view
  and applies `ShotViewportDisplay::ConfigurePreviewFlags` locally: no editor
  primitives, collision, engine bounds or selection overlay. Only isolated
  preview keeps the reference grid. Composition Canvas/PDI guides are independent
  of these engine helpers; the diagnostic HUD starts off and remains opt-in. The same Follow mode and Aim mode-plus-selected-subtab predicates gate drawing,
  stale hit-cache entries and drag writes; ignored screen controls are hidden. Letterboxed handle coordinates
  use `FSceneView::UnscaledViewRect` size and origin. Isolated template proxies
  retain actor root and captured relative mesh transform, keeping ACharacter
  mesh offsets, bone pivots and actor basis distinct.
- Canvas draws through a DPI-scaled base transform (`Engine/Private/UnrealClient.cpp`
  and `UserInterface/Canvas.cpp` in UE5.6). Convert projected physical positions
  to logical Canvas coordinates before drawing anchor/zone/projection guides;
  convert their hit rectangles back to physical pixels for FSceneViewport input.
  Zone padding uses render size divided by Canvas DPI. Drag normalization stays
  in physical render coordinates. Do not scale a physical draw position twice.
- `ShotViewportOverlay::OrbitLayout` and `HudLayout` arrange Canvas-space overlays
  inside the current constrained camera rect. The AnchorOrbit globe belongs only
  to Edit / Follow and Guides; it has latitude circles, longitude great circles,
  a local-basis camera marker and a circular hit area. Recompute its layout at
  input time so resizing cannot leave a stale hit rectangle. Drawing, start and
  live-write predicates agree on task/subtab, placement mode, editability and
  Compose mode. Read-only/Inspect globes are muted and non-interactive.
  Native GetCursor returns grab/closed-grab only for an editable orbit control.
- `StartHandleDrag` centralizes transaction setup for screen/zone/orbit controls.
  Orbit saves LocalCameraDirection through the same snapshot/guarded commit path
  as other viewport scalar gestures. Captured physical deltas divide by the
  gesture's Canvas DPI, then change local yaw/pitch at .45 degrees per Slate unit.
  Ctrl multiplies speed by .2, Shift by 5, both revert to 1. Yaw wraps, pitch
  clamps to +/-89.5 to avoid the pole singularity. Distance and BasisFrame stay
  authored. A motionless click cancels its transaction. Task/mode/source changes
  stop stale writes; hide Guides ends a captured handle gesture. Distance-wheel
  editing rejects an active handle/roll gesture rather than nesting an Undo
  transaction and sending a premature scalar commit.
- Follow/AnchorOrbit has no separate PDI orbit-center point. Keep its small
  Canvas globe and native orbit/distance input; Subject-page pivot guides remain
  independent. SubjectGizmos invokes production 3D Draw and subtracts native
  viewport primitives, checking that SingleTarget/component/local-offset,
  weighted and fixed anchors add no point in Compose or Inspect. It also confirms
  the Canvas orbit-control predicate remains active on Follow.
- Subject gizmos share `ResolvePivotTransform` with `ResolveWorldPoint`; the
  unoffset frame has unit scale, preserving the existing quaternion-only Offset
  convention. Component failure and bone/socket fallback are not duplicated in
  drawing/input. `ShotSubjectGizmo::Resolve` derives the base, effective pivot,
  Offset rotation and separate placement-heading quaternion. Create/Edit Subjects
  supply one view-only session flag. PDI draws pivot/offset/heading and configured
  bounds through foreground primitives; Canvas supplies Subject names, XYZ labels, RGB Offset
  axes and six ManualExtent face handles. Foreground RGB axes follow the reference
  pattern in GameplayCameras/Private/Debug/CameraDebugRenderer.cpp's
  DrawCoordinateSystem, using this viewport's existing PDI/Canvas path.
  Manual FOV/zero-weight boxes show gray only on subject pages; positive fit
  contributors retain green/yellow diagnostics. Auto bounds refresh only the
  editor's effective value cache, never authored/runtime data. No additional Shot
  or soft-path copies are created for this draw pass. Other existing consumers
  can still request independent effective-shot copies.
  Axis projections/hit areas use physical constrained-camera pixels, converting
  to Canvas coordinates only for paint. A 36-Slate-unit Offset triad scales with
  camera depth/DPI. Gesture-start pixels-per-world-unit remain fixed while the
  solver camera follows the subject, preventing projection feedback. Incremental
  mouse deltas project onto that axis, with Ctrl .2 / Shift 5 multipliers. Offset
  writes its authored component; face sign adjusts one nonnegative world-aligned
  half-extent. Both box faces resize symmetrically because the runtime format is
  center + half-extent, not independent min/max. Auto bounds cannot be dragged.
  Weak actor/mesh refs, names, flags/transforms, authored values, image rect and subject count
  guard stale handles without copying soft-path strings. Hit-test/start/live-write
  agree on page, source identity, shape, editability and Compose state; unresolved
  or camera-facing axes cannot write. Resize/hide/swap/invalid host closes a gesture.
  Visible effective pivots carry only the Subject name, using the parameter-page
  A/B/numbered roles. Numbered names use a fixed stack buffer, without adding
  per-frame FString formatting. Pivot/bone, offset-space and weight summaries
  remain omitted. Subject/axis label positions and maximum widths stay inside
  the camera image.
  Hit capacity reserves 19 + 9 per subject at bind/count changes; paint does not
  grow arrays. Numeric labels use stack buffers. Canvas/PDI retain their native
  batching allocations. SubjectGizmos tests production PDI output and actual
  transaction/hit/write paths, half-extent signs/floor, local/world semantics,
  no-refresh/no-op commits, history, stale indices/resize and DPI math. PivotTransform
  tests actor/component/socket frames and existing point-resolution semantics.
- HUD uses structured Camera / Composition cards with alternating rows, aligned
  labels/values and resolution state colors. HudLayout takes only the actual
  camera image: Camera and Composition always stack at the same left edge with
  an eight-unit upper-left inset and a scaled eight-unit inter-card gap. A uniform
  scale of 0.85 * min(width/1280, height/720) applies to rectangles,
  headers, row heights, padding, the inter-card gap and Canvas text; small images
  additionally clamp the 348 x 362 base stack to remaining width/height and the
  possible bottom-left Orbit footprint. That footprint is reserved regardless
  of Guides state, keeping HUD geometry stable during guide/task switching. Camera retains
  eight rows and Composition nine; Rows tolerates scaled-boundary roundoff.
  The HUD no longer moves beside the orbit globe, wraps, or drops lower rows.
  Task/guide state cannot affect its geometry. No bottom plain-text strip remains. Fixed stack
  TCHAR buffers and UE5.6 `FCanvasTextStringViewItem` avoid allocating FString/FText
  for per-frame numeric formatting; `Canvas.DrawItem` consumes each view before
  its buffer changes. Values can shrink further to fit their scaled column. Canvas
  rendering still owns its normal internal batches/glyph resources. Geometry
  uses fixed loops without temporary point arrays, and CachedHandles reserves
  all 19 possible controls once in the viewport constructor.
- `EnsureEffectiveShotCache` keeps resolved Shot data in the reused per-frame
  buffer. HUD borrows that read-only value, avoiding an extra Targets-array copy.
  `BuildEffectiveShotForPreview` keeps its existing independent-copy contract
  for solver and other consumers that need mutable bounds caches.
  ShotEditor.OrbitControl exercises production start/move/release callbacks,
  solver basis/radius, one-step Undo, scalar notifications and retained controls,
  wheel exclusion, mode/task selection and locked/read-only sources.
  ShotEditor.PreviewOverlayLayout covers yaw wrap/pole clamps, DPI, circular
  hit areas, fixed upper-left positioning, proportional shrink/growth, complete
  row coverage, vertical card order/gap, card/orbit non-overlap down to the
  minimum Orbit height, and degenerate camera geometry.
- Named target components restrict pivot/bone/basis/bounds consistently. The
  bounds cache key includes ComponentName, even None; source-mesh proxy caches
  include component identity. Component walks avoid temporary component arrays.
- Templates derive two-person yaw from a world-up A->B axis. Reorder applies
  the same index permutation to every anchor, centroid entry, basis index and
  Section binding. Mirror also exchanges asymmetric left/right zone padding.
- Sequence authoring preflights conflicts, then owns one transaction for actor,
  bindings, tracks, sections, spawn coverage and cuts. Failure ends and undoes
  that transaction: `FScopedTransaction::Cancel` alone does not revert writes.
  New cameras have no competing Transform track; existing camera tracks are
  preserved. Spawn Track ObjectId is set explicitly. Additional spawn coverage
  uses a higher-priority true section in only the requested interval; existing
  keys/ranges survive. This follows UE5.6 MovieSceneTrack's default high-pass
  per-row population and MovieSceneSpawnTrack's multiple-section support.
- Subject binding preflight preserves Spawnable annotations as full relative
  IDs within this Sequencer hierarchy. A foreign player's spawned instance is
  refused rather than persisted as a level Possessable. Binding failures revert
  their own transaction; subject actions reject an already-running gesture.
- Native `CreateAssetWithDialog` captures reusable Shot presets; Section edits
  and preset restore never mutate a shared referenced asset. Preview mesh and
  transforms are editor-only; cooked evaluation still uses actor bindings.
- `ComposableCameraShotAuthoringRuntimeTests.cpp` and
  `ComposableCameraShotAuthoringTests.cpp` cover pair basis, component cache
  recovery, live write/one-gesture Undo, presets, template idempotence, index
  remapping, spawn extension, cut preflight and active overlap preview.
  Source checks do not replace IDE compilation or Slate/Sequencer smoke tests.
  `ComposableCameraShotParameterPanelTests.cpp` checks actual generated Follow
  rows across all placement modes/bases, Aim rows across both aim modes,
  hidden-value preservation and source clear/rebind.

Shot Editor status bar technique:

- `SShotEditorRoot::TrySetMode` classifies mode requests through
  `Widgets/ComposableCameraShotEditorModeSwitchUtils.h`; Free -> Drag
  does not apply immediately.
- `Widgets/ComposableCameraShotEditorStatusBarUtils.h` keeps status-bar
  priority and action mapping pure and testable. Clean active shots hide the
  bar, no-shot states show info, stale hosts show warning, and active Free-exit
  requests show warning plus Free-exit actions.
- Liveness states win over pending actions. A stale host or missing Shot must
  suppress Save / Discard / Stay, because Save writes through the active host.
- Free-exit requests cache the target mode and reverse-solve status, then show
  Save / Discard / Stay in the unified status bar below the top bar. Save
  calls `ReverseSolveCurrentCameraToShot()` before applying the pending mode;
  Discard applies the pending mode without writing Shot data; Stay clears the
  pending request and remains in Free.
- Free-exit actions are hidden when no pending request exists or the viewport
  has already left Free. Active Shot context swaps clear pending requests to
  avoid applying a stale Free camera pose to a different host.

Shot Editor mode-sensitive Details technique:

- Shot Details keeps the runtime structs unchanged and applies editor-only
  `IPropertyTypeCustomization` visibility gates for `FShotPlacement`,
  `FShotAim`, `FShotLens`, `FShotFocus`, and `FComposableCameraAnchorSpec`.
- Visibility rules live in `ComposableCameraShotModeVisibility.h` so Slate
  customizations and automation tests share the same mapping. Unknown fields
  default to visible; explicit mode branches only collapse rows that the solver
  ignores for the active mode.
- Keep cross-layer anchor dependencies visible. `Focus.FollowPlacementAnchor`
  can consume `Placement.PlacementAnchor`, and `Focus.FollowAimAnchor` can
  consume `Aim.AimAnchor` even when the corresponding Placement / Aim mode
  does not use that anchor locally.
- Hidden rows are not reset or rewritten. The values stay serialized and
  become editable again when the relevant mode is selected.

Shot Editor Sequencer-shot menu technique:

- The `Shots` dropdown is custom Slate content, not a plain `FMenuBuilder`
  list. It uses `SSearchBox` plus `SScrollBox` so filtering happens in place
  without closing the combo menu.
- Search matching lives in `Widgets/ComposableCameraShotMenuUtils.h` and is
  covered by automation tests. It tokenizes the user filter and requires every
  token to match the combined track label, shot title, or time/row suffix.
- The menu walks object-binding tracks and root tracks, de-duplicates sections,
  groups visible rows by track label, marks the active section with a check
  icon, and shows the same time/row suffix used by the Shot Editor breadcrumb.

## 16. Built-In Camera Nodes

Current node classes:

- `UComposableCameraAutoRotateNode`
- `UComposableCameraBeginPlaySetRotationNode`
- `UComposableCameraBlueprintCameraNode`
- `UComposableCameraCameraOffsetNode`
- `UComposableCameraCollisionPushNode`
- `UComposableCameraCompositionFramingNode`
- `UComposableCameraComputeDistanceToActorNode`
- `UComposableCameraComputePositionBetweenActorsNode`
- `UComposableCameraControlRotateNode`
- `UComposableCameraDirectionalMoveNode`
- `UComposableCameraExposureNode`
- `UComposableCameraFieldOfViewNode`
- `UComposableCameraFilmbackNode`
- `UComposableCameraFocusPullNode`
- `UComposableCameraHitchcockZoomNode`
- `UComposableCameraImpulseResolutionNode`
- `UComposableCameraLensNode`
- `UComposableCameraLockOnAimPointNode`
- `UComposableCameraLookAtNode`
- `UComposableCameraMixingCameraNode`
- `UComposableCameraOcclusionFadeNode`
- `UComposableCameraOrthographicNode`
- `UComposableCameraPivotDampingNode`
- `UComposableCameraPivotLookAheadNode`
- `UComposableCameraPivotOffsetNode`
- `UComposableCameraPivotRotateNode`
- `UComposableCameraPostProcessNode`
- `UComposableCameraReceivePivotActorNode`
- `UComposableCameraRelativeFixedPoseNode`
- `UComposableCameraRotationConstraints`
- `UComposableCameraScreenSpaceConstraintsNode`
- `UComposableCameraScreenSpacePivotNode`
- `UComposableCameraSetRotationNode`
- `UComposableCameraSplineNode`
- `UComposableCameraSpiralNode`
- `UComposableCameraTwoPointMoveNode`
- `UComposableCameraViewTargetProxyNode`
- `UComposableCameraVolumeConstraintNode`

Node notes:

- `UComposableCameraCameraOffsetNode` applies `CameraOffset` in camera-local
  space (`X=forward, Y=right, Z=up`). Its inline
  `ForwardOffsetDeltaByPitchCurve` samples X as current pitch in degrees and
  adds Y as a camera-forward delta in cm before building the final position.
- `UComposableCameraComputePositionBetweenActorsNode` runs on the BeginPlay
  compute chain. It resolves both actor endpoints through
  `EComposableCameraActorInputSource`, clamps `Alpha` to `[0, 1]`, linearly
  interpolates world locations, then adds `HeightOffset` on world Z before
  publishing `Position`.
- `UComposableCameraSetRotationNode` and
  `UComposableCameraBeginPlaySetRotationNode` share the same resolver. Rotation
  source values are `FromActor`, `FromVector`, `FromRotator`, and
  `FromTwoActors`. `FromTwoActors` resolves both endpoints through
  `EComposableCameraActorInputSource`, builds a rotation from first actor to
  second actor, then all source modes apply `RotationOffset` as
  `WorldYaw * Base * LocalPitchRoll`. This matches `ControlRotate` semantics:
  yaw is around world Z, while pitch / roll are authored in the resolved local
  camera frame.

Base classes:

- `UComposableCameraCameraNodeBase`
- `UComposableCameraComputeNodeBase`

## 17. Built-In Transitions

- `UComposableCameraLinearTransition`
- `UComposableCameraSmoothTransition`
- `UComposableCameraEaseTransition`
- `UComposableCameraCubicTransition`
- `UComposableCameraInertializedTransition`
- `UComposableCameraCylindricalTransition`
- `UComposableCameraSplineTransition`
- `UComposableCameraPathGuidedTransition`
- `UComposableCameraDynamicDeocclusionTransition`
- `UComposableCameraCompositionPreservingTransition`
- `UComposableCameraViewTargetTransition`

Modifier-value timing:

- `UComposableCameraModifierTransitionBase`

## 18. Built-In Actions and Interpolators

Actions:

- `UComposableCameraActionTypeAsset` duplicates an instanced Blueprint or C++
  Action template before registration. Pin-compatible subclass properties with
  both Edit and BlueprintVisible flags become K2 inputs; base lifecycle fields stay on
  the asset. ParameterBlock values are checked against reflected field types,
  then written once to the duplicate. Object references use reflected setters
  and remain GC-visible through the Action's UPROPERTY fields. The K2 node
  returns the instance handle; `RemoveActionInstance` removes that one Action
  even when other assets instantiate the same class.
- `UComposableCameraMoveToAction`
- `UComposableCameraResetPitchAction`
- `UComposableCameraRotateToAction`

Action lifetime is checked once per PCM update before context evaluation.
Instant consumes one update frame; Duration accumulates update DeltaTime even
when no target executes. `AddCameraAction` rejects a Duration-enabled action
whose authored Duration is non-positive. `OnCanExecute` rejects the same invalid
value if C++ changes it after registration.
`OnCanExecute` handles only Instant, Duration, and Manual. Camera-local weak
action lists dispatch the four execution stages with mutation-safe snapshots.
At the running camera's first matching hook each PCM update,
`ExecuteForCamera` calls the Blueprint `CanExecute` Condition with that stage's
local pose. False removes the Action immediately and skips `OnExecute`.
Source-camera hooks during a blend cannot expire a global Action; the running
camera controls completion. A current-camera-only Action instead checks its
bound camera, including when that camera becomes a transition source. A
Condition Action with no matching hook is not condition-checked until it can
execute.

Interpolators:

- `UComposableCameraInterpolatorBase`
- `UComposableCameraIIRInterpolator`
- `UComposableCameraSimpleSpringInterpolator`
- `UComposableCameraSpringDamperInterpolator`

## 19. Current Automation Tests

Existing test files include:

- `ComposableCameraBugFixTests.cpp`
- `ComposableCameraActionTests.cpp` (non-positive Duration guard; Condition uses
  the executing camera's local pose; Action asset parameters stay instance-local).
  Class-based default-value tests use dedicated reflected fixture classes;
  mutating an initialized CDO does not reliably simulate authored defaults
  on instances made by `NewObject`.
- `ComposableCameraCompositionPreservingTransitionTests.cpp`
- `ComposableCameraComputePositionBetweenActorsNodeTests.cpp`
- `ComposableCameraShotSolverTests.cpp`
- `ComposableCameraPivotLookAheadNodeTests.cpp`
- `ComposableCameraLockOnAimPointNodeTests.cpp`
- `ComposableCameraModifierPropertyOverrideTests.cpp`
  - legacy property-copy/pin priority.
  - in-place live lower-layer enter/exit.
  - every matching same-class node instance.
  - apply-mode compatibility default, mixed-mode reactivation, and null-safe
    reflection classification.
  - property-local Enter/Replace/Exit routing across partially overlapping
    override sets.
  - per-property winner composition, overlap priority, removal fallback,
    construction application, and Custom whole-node compatibility.
- `ComposableCameraDebugSnapshotTests.cpp`
- `ComposableCameraNodeGraphSyncTests.cpp`
- `ComposableCameraNodeRuntimeTooltipTests.cpp`
- `ComposableCameraRuntimeDebugPanelTests.cpp`
- `ComposableCameraConsoleControlsTests.cpp` (full registry discovery/type
  coverage, console/UI synchronization, registered-default reset, read-only
  and unregistered-object handling, quoted arguments and world-aware dispatch).
- `ComposableCameraLiveEditTests.cpp` (independent trial storage, current-driver
  Reset, wire/Modifier override priority, Start-only scope and exec order,
  all editable property types, owned subobject/array/curve persistence,
  component reuse during refresh, instance isolation, actor world validation,
  GC/PIE teardown, read-only chain topology, selected Details binding and trial
  preservation across node selection, enabled/disabled action text centering at
  multiple layout scales,
  atomic source conflict, scoped-reference atomic save, driven-pin schema-loss
  rejection, obsolete undriven override cleanup, same-class node identity after
  reordering, frame memoization, asset Undo/Redo and subsequent trials, object pin
  picker Undo/rebuild consistency, durable graph/variable/compute-chain
  preservation with toolkit-equivalent sync callbacks, and vector/rotator/enum
  default round trips). LiveEdit tests are added but await IDE compilation and
  editor execution; source inspection does not certify compatibility.
- `ComposableCameraSetRotationNodeTests.cpp`
- `ComposableCameraPatchTests.cpp` (bulk expiration after handle GC,
  duration override, repeated expiration, partial enter, and immediate exit).
- `ComposableCameraMeshSurfaceTests.cpp` (BVH/linear equivalence, disabled and
  invalid Layers, nearest-floor overlap, edge tolerance, root-bounds rejection,
  triangle-predicate pruning, rebuild/reset, transformed documents, and real
  Undo/Redo with an isolated transaction buffer).
- `ComposableCameraMeshProfileEffectsTests.cpp` (selected-family preload,
  pending entry/leave and overlap order, one-time dispatch, shared document
  ownership, GC retention/release). The 2026-10-05 editor report passed Patch
  expiration, BVH equivalence/pruning, transformed document Undo/Redo,
  SurfaceLayerSet, and ExclusiveProfileDispatchAndCleanup. The corrected
  preload fixture awaits IDE compilation and another editor run.
- `ComposableCameraMeshProfileTests.cpp`
- `ComposableCameraMeshProfileCustomizationTests.cpp`
- `ComposableCameraMeshLayerToolSettingsTests.cpp`
- `ComposableCameraMeshLayerVisualizationTests.cpp` (resolved coverage, budgeted
  PIE floor fitting, Pawn exclusion, stacked-floor isolation, Visibility response
  filtering, WorldDynamic/PhysicsBody floor support, complex collision on an
  ordinary Actor's StaticMesh child, construction statistics, cancellation,
  persistent mesh fidelity, depth testing, two-sided rendering without duplicate
  geometry, GC ownership, transform-only reuse, document scheduling, explicit
  point diagnostics for submitted/absent/buried scaled stacked coverage and cleanup).
  PIEPreviewOccludingFloor reproduces visible Visibility-ignoring ground above
  painted collision, verifies submitted height and unchanged native camera data,
  and covers hidden/translucent rejection, stacked-floor isolation and the fixed
  occluder promotion band.
  The user's 2026-10-05 editor log reports success for PIEPreviewSurfaceProjection,
  PIEPreviewProgressiveMeshes, PIEPreviewPersistentMeshes and PIEPreviewWorldRouting
  at 16:35 Hong Kong time, after the collision-eligibility fix. Runtime diagnostic
  command output also confirmed preview submission in the main Level and a
  starved Level Instance. The user subsequently identified the missing patch on
  LevelBlock in the main Level, so starvation does not establish that patch's
  cause. The new PIEPreviewScheduling fixture and expanded StaticMesh/point
  sampling cases await IDE compilation/editor execution; frame-time
  improvement requires a PIE measurement on the affected Level.

Codex must not invoke Unreal automation from shell in this project. Run tests
inside Rider or Visual Studio / Unreal Editor.

## 20. Hot-Path Rules

Assume these are hot:

- PCM update.
- context stack evaluation.
- director evaluation.
- evaluation tree walk.
- camera node tick.
- patch apply.
- Sequencer component tick.
- shot solver.
- mesh surface query and stable-profile subsystem tick.

Rules:

- No heap allocation unless pre-reserved or justified.
- No `LoadSynchronous`.
- No FString formatting in per-frame loops.
- No container mutation that can reallocate during iteration.
- In-place Modifier binding creation, reflection, and node snapshots happen on
  selection edges. Per-frame value application uses preallocated arrays and
  cached property operations.
- Snapshot mutable callback lists before invoking Blueprint callbacks.
- Use weak pointers in snapshots that can survive arbitrary Blueprint work.
- Cache soft object resolution outside the eval path.

## 21. Gotchas

- `EnsureContext` means "exists and top", not merely "exists".
- `ReferenceLeaf` captures tree topology. It is not a live director pointer.
- Same camera UObject can be reached twice in one frame through snapshots.
- Modifier value clocks must advance after the camera memoization guard, not
  before it.
- Removing an in-place pin override must unregister node ownership before the
  normal pin resolver runs; copying a type-asset value back is incorrect for
  K2/wire-driven inputs.
- A pending in-place Modifier exit is work, not dead state. Do not erase its
  binding during normal reconcile; `ApplyForNode` must first restore the lower
  layer and unregister ownership.
- If neither the previous nor desired effective Modifier owns a still-bound
  property, keep its existing Exit state. Restarting it from an unrelated
  selection edge changes both duration and source snapshot.
- Do not collapse effective Node Type entries back to one winner per node
  class. Registration is node-class-bucketed, but selection and runtime
  ownership are keyed by `(exact node class, property name)`.
- Custom Modifier callbacks use the reserved `NAME_None` whole-node lane. They
  cannot safely participate in property composition because Blueprint side
  effects do not declare the fields they mutate.
- A patch evaluator is a transient camera actor, not a node grafted into the
  main camera.
- Patch activation override booleans are semantic. Zero is a valid value.
- Sequencer shot override can arrive after component tick; first-entry path must
  invalidate tick cache and evaluate at zero delta.
- `AddRaw` delegates owned by Slate widgets must be explicitly unbound in the
  toolkit destructor.
- Runtime data-block shape checks and byte-bounds checks are independent.
- Object/Actor pin class constraints still need earlier layout-time diagnostics;
  runtime guards prevent corruption but do not give the best authoring message.
- Local-player subsystem caches need weak pointers plus parent identity checks.
- Mesh active Layer identity is `(StorageActor, LayerGuid)`, not GUID alone:
  Level Instance copies can contain identical serialized Layer GUIDs. Cleanup
  must remove each Layer's duplicated Modifiers and its own temporary Context.
- FOV may be stored as FieldOfView or FocalLength. Use pose helper methods for
  effective FOV.
- Focus distance uses sentinel behavior. Do not blend invalid focus distance as
  a real distance.
- UE automation `UTEST_EQUAL` has no `FName` overload in UE 5.6. Use
  `UTEST_TRUE(NameA == NameB)` or compare strings when testing `FName`.
- UE5.6 LWC math aliases such as `FVector`, `FVector2D`, `FVector4`,
  `FRotator`, and `FTransform` do not expose a member `T::StaticStruct()`.
  Generic reflection code for built-in structs must use
  `TBaseStructure<T>::Get()`.
- Interpolator `Run()` returns an absolute value, not a delta. If a scalar
  damping helper computes only `Target - Current` progress, add it back to the
  current value before returning; Spline, FocusPull, and VolumeConstraint reset
  double interpolators from their last smoothed output each frame.
- Viewport Legend `Nodes.All` / `Transitions.All` means "show all relevant
  current-camera / active-transition legend rows", not the entire palette. Keep
  legend filtering tied to the same runtime classes that can actually draw this
  frame.
- Automation-test helpers in anonymous namespaces still need file-specific
  names. UE unity builds can concatenate multiple test `.cpp` files into one
  translation unit, where two same-signature anonymous-namespace helpers with
  the same name become duplicate definitions.
- A virtualized `SListView` does not automatically revise cached variable row
  heights when a nested `SExpandableArea` changes state. Route visible
  expansion changes through list refresh plus post-rebuild measurement.
- A non-owning `FStructOnScope` bound to a `TArray` element becomes invalid when
  add/remove/reorder relocates or replaces that element. Clear the structure
  Details view before mutation, then bind a fresh scope afterward.
- Forward declarations must use the same class-key as existing UE/project
  declarations. In particular, declare `FSpawnTabArgs` and
  `FTransactionObjectEvent` as `class`; MSVC C4099
  becomes a build failure when warnings are treated as errors.
- Lambdas returning a typed index in one branch and `INDEX_NONE` in another
  need an explicit `-> int32` return type. `INDEX_NONE` is an anonymous-enum
  sentinel, so implicit deduction fails with MSVC C3487.
- `Editor.h` / `EditorEngine.h` only forward-declare `USelection`. Include
  UnrealEd's `Selection.h` directly where calling selection methods such as
  `GEditor->GetSelectedActors()->Num()`. UE5.6's `Engine/Selection.h` is a
  compatibility adapter, not the defining header. An incomplete selection type
  inside a Slate predicate causes C2027 and a downstream C2664 lambda-conversion
  error; fix type completeness before changing lambda signatures. Do not rely
  on another unity translation unit to supply this include.
- Do not mix `TObjectPtr<T>` and raw `T*` in a conditional expression. Call
  `.Get()` first, or use explicit branches when returning `TSubclassOf<T>` from
  a `UClass*`. In UE 5.6, include `PropertyHandle.h` for `IPropertyHandle`.
- Type-asset identity fields are copied after camera `Initialize()`. Any cache
  derived from `CameraTags` must refresh at the copy site; use
  `AComposableCameraCameraBase::RefreshCameraTags()` rather than updating the
  container and cached trace label independently.
- UE module dependencies are not linker-transitive. A module that directly
  calls exported `FGameplayTagContainer` / `FGameplayTagQuery` methods must list
  `GameplayTags` in its own Build.cs, even when a depended-on runtime module
  already lists it.
- Editor-module shutdown runs after UE's global Level Editor mode manager can
  be destroyed. Gate every shutdown-time `GLevelEditorModeTools()` access with
  `!IsEngineExitRequested()`; otherwise the accessor emits an ensure and
  recreates a mode manager during teardown.

## 22. Build and Verification

For this project:

- Do not invoke UBT, Build.bat, RunUBT.bat, dotnet, msbuild, Unreal Editor, or
  automation tests from Codex shell.
- Compile inside Rider or Visual Studio.
- Header/reflection/module changes require full editor restart, not Live Coding.
- Docs-only changes do not need a compile, but a non-trivial code-adjacent doc
  sweep should still be reviewed against source.
- For graph exec-chain serialization, variable identity and variable-node
  identity are different. `SetVariable` entries must store the exact graph
  node GUID and use variable GUID only as legacy fallback; otherwise a
  same-variable Get node can capture the rebuild lookup and drop exec wires
  after save/reopen.

## 23. Mesh Camera Surface Query

2026-10-07 editor latency update: Save builds FComposableCameraMeshSurfaceEditorPreview
version 1 from already resolved exact editing coverage, avoiding a redundant full
clipping pass. An unchanged actor preview survives metadata saves; missing exact
coverage resolves once and remains available after Save. MatchesEditorPreview
checks grid/bounds/cell positions/normals/GUIDs/full-coverage flags and exact patch
vertices against the current CPU cache without allocation, at Save only. A version
number alone cannot preserve corrupt/stale actor coverage. Actor storage is WITH_EDITORONLY_DATA;
stable GUIDs map back to current Layer rows. LoadEditorPreview validates version,
finite grid/bounds/vertices and enabled patch ownership before replacing output.
Production Edit uses StartResident: build the authoring index first, emit it early,
then prepare one complete shared geometry document. Valid saved coverage skips
clipping; legacy coverage reuses the already built index for bounds. Saved and
legacy documents use the same whole-scene installation, with no initial Tile cursor.
Show's saved-cache path exports one document batch and terminal accounting;
non-streaming callers receive one complete result. Legacy/invalid cache retains
source-resolution fallback. Show/PIE component publication stays budgeted; no cold spatial
clipping is needed for cached documents. SetAuthoringData clears stored coverage
when geometry/GUID row order/enabled policy changes. Other metadata retains it.
RebuildRuntimeData always clears it. All mutations increment EditorDataRevision;
both PostEditUndo overloads invalidate
Show/PIE through that transient counter. Cached pixels cannot override query data.

FMeshLayerStrokeCoverage::EnablePreparedPreview prepares native complete touched
tiles in bounded owned batches, including empty results for Erase. Every coverage
input still executes; each region is assembled after its final input in that batch.
The maximum eight waiting inputs bounds the delay before another completed prefix
can publish. A full batch emits its latest whole state. Mode consumes
these publications before retiring their future. QueuePreparedRegion supersedes
older waiting/active assembly for the same tile while preserving remote restore
work. This removes the second tile snapshot/worker gap from Brush/Erase. Append
merge size is capped at 4096 triangles on this path; FIFO source and exact height/
priority operations remain unchanged. The legacy QueueUpdate worker is fallback.

FMeshLayerAuthoringIndex uses a hierarchy over 128-triangle source-order blocks.
Nodes union per-Layer 3D/projected bounds. Append/Erase refresh affected leaves and
ancestors; root bounds reproduce enabled source bounds. Pruned DFS yields ascending
visual candidates and descending Erase candidates, preserving exact source order.
Interleaved wide bounds can still degrade traversal. Count guards cannot detect
same-count rewrites; replacement/Undo and geometry-changing compaction reset the index. Native checkpoint
copies now include the hierarchy, not only flat blocks.

Edit proxies allocate power-of-two vertex/index capacity (minimum 64). Same-capacity
updates retain proxy/factory/resources, copy changed native attributes into CPU
buffers and LockBuffer/Memcpy/UnlockBuffer only used RHI ranges. DrawVertexCount and
DrawIndexCount exclude padding. Capacity growth requests render-state recreation;
dynamic updates skip dirty render state to avoid sending larger data to an old proxy.
SetFillColor updates only the colored material proxy. GeometryRevision changes only
for geometry. Replacing the colored proxy lets FMaterialRenderProxy's destructor
release its uniform cache; do not explicitly invalidate it immediately before
destruction. UE5.6's InvalidateUniformExpressionCache requires a boolean argument;
there is no zero-argument overload. Alpha matches the existing 0.12..0.5 clamp.
Stable rendering traverses no vertices. Reference: GeometryFramework/Private/Components/MeshRenderBufferSet.cpp::
TransferVertexUpdateToGPU and Engine/Private/StaticMesh.cpp::InitFromDynamicVertex.
Resident preparation moves arrays into FEditPreviewGeometry and computes bounds
on the worker. FSharedEditPreviewGeometry::bUseLayerColor selects current metadata
at publication; historical captures default false and retain exact remembered colors.
Proxy creation captures shared native geometry; CreateRenderThreadResources copies/pads CPU
vertex/index buffers and calls the UE5.6 public InitFromDynamicVertex(RHICmdList,
VertexFactory, Vertices) overload plus IndexBuffer.InitResource(RHICmdList).
The engine invokes this before adding the primitive to its render scene. Updates
and destruction follow engine command ordering. The editor thread owns
component registration; no initial vertex traversal remains there. Component row
removal looks up region/Layer keys up to the maximum observed row count, avoiding
O(region-count squared) scans while still removing rows after structural deletions.

Shape live preview has one immutable source/coverage base per drag, one active
projection and latest waiting input. Immediate bounded planar fill follows input;
initial document/restoration publication finishes before temporary surface tiles
replace it. Planar feedback stays visible while the authoritative base loads.
Temporary surface projection allows 4096 triangles, with editor-thread queries
limited to 256/3 ms per advance. Native workers resolve complete dirty tiles from
the original base, excluding replaced Shape triangles and reapplying erasures.
Old/new/previously published footprints clear stale temporary fill. WorkingData,
DocumentRevision and history remain untouched. Cancel restores document buffers;
release/final Details uses the existing budgeted final Shape builder (16384 limit)
and commits source/coverage/prepared native fill together.
Dirty footprints are resolved separately; candidates cover complete edge cells,
including neighbors outside the outline. Sparse scratch bounds never reserve the
empty gap between old/new positions, including an empty original document.
Name/Profile/Channel reuse geometry; Color updates material parameters during Interactive.
Metadata document GUIDs alias the same checkpoint; checkpoint Revision represents
geometry provenance, not every alias's document GUID.
Intermediate property transactions do not create per-tick preview histories;
the completed bookend shares coverage once. Channel cancels pending
projection. Repeated Details pre-edit events retain the live drag base.
Delete cancels queued replacements for its Shape identity before source removal;
its affected coverage uses fused regional publication instead of a full rebuild.

Rapid released-then-pressed strokes queue separate captured inputs; source FIFO and
transactions finish in Tick, with no normal-press unlimited drain. Append-only
cancel truncates to starting counts; first Erase makes one original-source snapshot.
Ctrl+Z removes the latest deferred stroke or pending Shape, cancels a live draft,
or cancels an unfinished active stroke before ordinary engine Undo.
Explicit Save/focus/tool/close retain flush semantics.
Save completes resident loading and exact FIFO coverage before compaction can
invalidate their index-count snapshot. RemoveOrphanedTriangles uses a vertex-ID
remap, preserving existing sharing rather than expanding every triangle; source
order, Layer GUIDs, Shape GUIDs/controls/erasures and exact positions persist.
No-op compaction retains source allocation and authoring index. Runtime baking
uses the same remap principle; no positional welding or reflected format change.
SetAuthoringData compares geometry and GUID row order once at this mutation
boundary. Consistent source with unchanged geometry/rows and an existing index reuses runtime triangles
and BVH; a genuinely empty source may reuse empty output. Nonempty source with
missing runtime output rebuilds; inconsistent ownership retains the prior empty-output fallback. Enabled changes retain BVH, but invalidate saved
coverage; query filtering reads live enabled state. Shape controls/ownership copy
independently of geometry. Native queries still see current Name/Profile metadata.
PrepareSavePreview reads immutable checkpoint or mutable exact coverage directly,
without copying/reclipping the whole editable cache. A miss resolves once, keeps
the result and invalidates only display/history labeling until publication finishes.
CCS_MeshLayers_Save and CCS_MeshLayers_SavePreview expose CPU scopes. The Save ms
log separates finalize (interaction completion/normalization/coverage/compaction),
runtime (actor lookup/Modify/bake), preview (saved polygon conversion), packages
(dirty notification, UE checkout/serialization/I/O, including any modal wait),
and checkpoint (successful baseline copy/history). Pending source/proxy work,
transaction snapshots, package writes and checkpoint copies can still cost time.
Package scope and failed-save/Discard/Undo checkpoint behavior remain unchanged.
Regressions: SavePreparation and MedianPartitionEquivalence; IDE execution pending.
All Level views redraw during asynchronous progress and two following frames.
Edit compute and native retirement share a module-owned two-thread pool. Normal
close cancels without waiting; unregister stops mode producers, joins pools and
flushes rendering before DLL unload. The same-DLL unload regression needs an
external host harness. Unreal transaction serialization, source/cache snapshots,
checkpoint memory accounting, indivisible queries/uploads and PIE fitting remain
costs outside hard latency guarantees. New regressions: SavedPreview,
FusedStrokePreview, RealtimeEditing and AuthoringHierarchy; IDE execution pending.

Runtime types:

- FComposableCameraMeshLayerDefinition: GUID, name, Profile, authoring Channel,
  enabled state and debug color. Row zero is topmost.
- FComposableCameraMeshGroundQueryParams: SurfaceTolerance only, default 5 cm.
  The former trace mode, Channels/Profile, distance, complex and ignored-Actor
  fields are removed. Business owns ground discovery and collision policy.
- FComposableCameraMeshSurfaceAuthoringData: full source with one Layer GUID per triangle.
- FComposableCameraMeshSurfaceRuntimeData: cooked triangles with one Layer index per triangle.
- AComposableCameraMeshSurfaceStorageActor: hidden Level-local serialization anchor.
- UComposableCameraMeshWorldSubsystem: non-ticking registration, passive queries
  and explicit per-local-player Update/Clear. No SurfaceId/provenance arrays are added.

Ground matching:

```text
business ground FHitResult + GroundQueryParams
  -> require non-penetrating blocking hit and live same-world Component
  -> validate finite ImpactPoint and nonnegative finite SurfaceTolerance
  -> original ground XY, world-Z ray from Z+tolerance to Z-tolerance
  -> per-document inverse transform, bounds/BVH pruning and exact triangle intersection
  -> native QueryLayers uses entire segment as its overlap interval
  -> independently require abs(each Layer hit Z - ground Z) <= tolerance
  -> deduplicate by storage actor + Layer GUID; sort top row first
  -> return saved SurfacePosition and absolute ground separation as VerticalDistance
```

The caller's physical ground selects the admissible height band even when that
floor has no painted Layer. Missing/penetrating/expired/wrong-world ground returns
empty; there is no geometry-only fallback or downward search for another floor.
No scene collision query or Pawn lookup is performed. GroundHit.ImpactPoint is
used rather than capsule sweep Location. Business checks walkability and freshness,
and chooses handling for jumping or custom movement. Ordinary Character Walking
can reuse CurrentFloor.HitResult after IsWalkableFloor; NavWalking needs its own
appropriate ground source. This validates coverage and height, not source Component
ownership. An incorrect supplied floor or large tolerance can still select another
storey; simple collision can differ from the authored complex/render surface.

Each document's QueryLayers is called once, with origin ground+tolerance and both
length and native overlap tolerance equal to 2*tolerance. This groups every enabled
Layer in the bounded ground interval, including Layers on opposite sides of ground,
rather than pruning at the first Layer plus only one tolerance. Each Layer retains
its own nearest triangle hit from the upper endpoint and is explicitly height-checked.
Zero tolerance is an exact point-height match, using the native zero-length ray
predicate. Nonfinite/overflowed interval endpoints fail. SurfacePosition is never
replaced by the supplied ground point; VerticalDistance is abs(sourceZ-groundZ).
Low-level storage QueryLayers still reports distance from its own ray origin.

Native acceleration still uses two passes internally. RebuildSpatialIndex creates
a balanced native-only BVH with up to eight triangles per leaf, median splits and
preorder subtree escape indices. std::nth_element selects the median using cached
triangle-box centers and the original triangle-ID tie-break, replacing complete
sorting at every subtree. Expected comparison cost becomes O(N log N), rather
than repeated full-sort O(N log-squared N); no timing or worst-case guarantee is claimed.
The native permutation changes only traversal order, never serialized topology.
Traversal uses no heap-backed stack. Optional
OutLayerRayDistances records each Layer's minimum ray distance, independent of
visitation order, and remains aligned with sorted Layer indices. This prevents one
Layer borrowing another Layer's intersection. Bounds padding matches the triangle
predicate's barycentric edge tolerance. Enabled state is read from live Layer data.

Storage PostLoad, missing-index BeginPlay, RebuildRuntimeData and both PostEditUndo
hooks prepare acceleration outside queries. Same-count Undo must rebuild; count
guards alone cannot validate an old cache. Direct geometry edits rebuild the index.
Standalone data without an index retains a linear reference path. Serialized field layout,
exact triangle positions/order, local/document transforms and cooked format remain compatible; old documents need
no reauthoring or additional per-triangle memory. OutTriangleTests and
CCS_MeshSurface_Query remain the pruning/Insights diagnostics.

QueryMeshLayersInline and Update reuse inline capacity for 16 Layers. Deeper overlap
may allocate; Blueprint QueryMeshLayers reuses caller output capacity, with first/
growth copies allowed to allocate. Stable ownership introduces no new heap work.
No physics query parameters or collision Profile response arrays are created.

CCS.MeshLayers.DebugNextQuery in a Game/PIE console arms one weak-world request.
Only the next real business call in that world consumes it, including invalid input.
Reports show GroundHit component/ImpactPoint, tolerance, registration and outcomes:
InvalidGroundHit, StartPenetrating, GroundComponentUnavailable, GroundWorldMismatch,
InvalidNumericInput, NoRegisteredDocuments, NoLayerOnGround or MatchedGround.
Each Match retains its saved SurfaceZ and signed SourceMinusGroundCm. A wide
300-10000 cm native document probe can show a different storey; it explains source
heights only and never changes membership, registration or geometry. Invalid Update
ownership is diagnosed before query. Formatting/probes run only when explicitly
armed and are omitted in Shipping. Show visualization is independent.

UpdateMeshLayers takes an explicit local PlayerController, GroundHit and the same
GroundQueryParams, then diffs membership and manages Profile effects. Invalid/missed
ground exits old scopes. Query creates no player state/effects. True reports
geometric membership even while assets are pending. ClearMeshLayers is idempotent
and removes only owned effects. Stopping Updates without Clear leaves effects active.
PC/PCM EndPlay, document unregister and teardown retain their existing cleanup;
manager replacement unbinds the old owner. No automatic ground polling is added.
Blueprint callers must refresh/recreate old Query/Update and Make QueryParams nodes
after a full IDE build/restart, reconnect GroundHit and use Make Mesh Ground Query
Params. No deprecated world-position query wrapper silently restores old semantics.

Registration/rebuild preloads the selected Profile family's soft asset paths
through `UAssetManager::GetStreamableManager().RequestAsyncLoad`. One cache
entry per Profile retains its handle; a manually GC-tracked `TObjectPtr` mirror
also retains already-resident assets. Disabled Layers and inactive families do
not request assets. Shared documents retain the entry until the last user
unregisters; world teardown cancels handles and clears retained references.
Dedicated servers retain geometric queries but skip these presentation preloads.

An isolated automation world without a GameMode does not dispatch Actor
BeginPlay merely by calling `UWorld::BeginPlay`. The preload test therefore
calls storage `DispatchBeginPlay`, verifies registration before direct cache
helpers, and checks that live rebuilding does not duplicate registration.
Its Camera asset contains a valid node; both Camera and Action soft references
must survive GC through the preload cache, and entry must actually own a
temporary Camera Context rather than only record Layer membership.

`UpdatePlayerLayers` checks readiness only for newly entering Layers. A pending
lower Layer delays higher new entries so bottom-to-top dispatch stays stable.
Each explicit Update revalidates spatial membership and then uses soft-pointer `Get`, never
`LoadSynchronous`. Failed completed requests follow the normal one-time failure
path. No completion delegate captures a player/actor or activates an effect.
Request/reconciliation allocations occur only at registration/configuration
edges. Stable active membership never requests or resolves assets again.

`UComposableCameraMeshProfile` stores an embedded
`FComposableCameraParameterTableRow Camera`, the existing Modifier asset array,
and Action/Patch configuration structs, gated by one serialized Type enum.
`FComposableCameraParameterTableRow::BuildParameterBlock` is the shared
string-to-typed block path for DataTable, Mesh Camera and Mesh Patch activation;
do not duplicate Camera/Patch schema handling in new callers.
Action uses `IsExposableProperty` + `TryMapPropertyToPinType`, identical to K2
Action exposure. Unspecified properties remain on the duplicated template.
String-compatible overrides use `ApplyStringValue`; Actor/Delegate use a
separate serialized binding map resolved at Layer entry, with Actor class and
Delegate signature checks. Explicit None clears the respective value.

Layer Profile Modifier assets are templates. The subsystem duplicates them with the
player camera manager as Outer. This prevents mesh removal from unregistering
the same asset instance owned by another gameplay system. Only Modifier Profiles
install candidates: entry uses `ReplaceModifiers(..., true)` once, exit removes
its duplicated instances. A Camera Profile cannot also install Modifiers.

The subsystem stores an active set keyed by storage actor plus Layer GUID. Each
active Layer records only its selected effect's ownership. Every Camera-bearing Layer
owns a separate temporary Context; entering a nested Layer pushes above
the outer Context, and exit pops only that Layer. Lower camera instances retain
their Director/tree state and resume in place. A Camera-less Layer creates no
Context. Debug hints use `Mesh_<LayerName>_<LayerGuid>`; the context stack
sanitizes and preserves the readable hint while adding a collision-free serial.
The shared row's authored `ContextName` is ignored for Mesh. Mesh forces
`bIsTransient=false` because Layer presence owns lifetime. Each entry
transaction captures the source Director before pushing, then activates with a
reference source. Activation failure rolls back only that empty Layer Context.
Exits process in reverse entry order. Simultaneous first hits process bottom
list rows first, making the top row the deterministic top Context.
When an active Layer Context pops, the resumed lower camera already contains
the correct pre-entry properties. `RefreshEffectiveModifierSelection` updates
only ModifierManager bookkeeping so removed duplicated assets are released;
calling `OnModifierChanged` there would rebuild and reset the resumed camera.
Action ownership is weak because the PCM's registered set owns the instance.
PatchManager ownership is weak because the original Director owns it. The
Patch handle is `TObjectPtr` inside a non-reflected active-state struct and is
traced through the subsystem's `AddReferencedObjects`; Patch instances hold
only weak handles. Exit calls original manager `ExpirePatch` and releases the
handle, preserving exit duration and unrelated instances. Already expired
Actions/Patches clean up idempotently and never re-enter on unchanged membership.
Camera Type, Transition, Action and Patch assets are preloaded asynchronously; entry
uses resident soft-pointer Get. No asynchronous completion dispatches an effect.

Regression tests: GroundHitQuery covers real Pawn ground traces, unpainted/painted
upper floors, capsule sweep contact versus center, retained hit without scene
retrace, public/inline agreement, result reset, penetrating/missing/expired/
wrong-world ground and invalid/overflowed numerics. GroundHitGeometry covers both
tolerance endpoints, outside-band exclusion, zero tolerance, exact coverage rather
than AABB overlap, slopes, disabled Layers, repeated-GUID documents and transformed
storage. NativeRayPrecision retains per-Layer nearest intersections, indexed/linear
equivalence, pruning and miss-output reset. NextQueryDiagnostics checks one-shot/
cross-world consumption, height evidence and read-only document discovery.
ManualUpdateAndClear retains passive querying, no automatic Tick/Pawn lookup,
stable effect identity, external-effect preservation, pending preload cancellation,
invalid tolerance/missing-ground exit, unload and PC/PCM lifetime cleanup.
Existing Profile dispatch/preload/context tests retain their ownership implementation.

`CCSMeshProfile` custom version 1 preserves original Camera/Modifier field names.
Pre-versioned assets select their sole configured family automatically. Mixed
legacy configuration sets serialized `bNeedsTypeSelection`; changing Type or
the transactional confirmation button clears it. Pending Profiles are skipped
once on entry with a warning; both configurations remain available.

Editor technique:

- Mesh Profile Details hides every config parent and each struct's immediate
  children before explicitly adding only the selected Type's relevant rows.
  `ShowOnlyInnerProperties` promotes children into default categories independently
  of their parent; `HideProperty(parent)` alone does not suppress those rows.
  Do not recursively hide array elements or nested value fields: selected
  Modifier arrays and InitialTransform still need their default child layouts.
  Camera's existing exposed-value
  customization still reaches the sibling CameraType without emitting a second
  trailing Camera field. ContextName is omitted because Layer identity creates
  the runtime Context. bIsTransient/LifeTime are omitted because Mesh forces
  non-transient unlimited activation. Transition and supported Activation fields
  are advanced. Only the custom `Activation` group appears for Camera; the
  default `ActivationParams` row is hidden. Camera picker rejects Patch subclasses.
- Type change and legacy confirmation use `RequestForceRefresh`, which rebuilds
  the real Details view on the next editor tick. `RequestRefresh` may only redraw
  its tree and leave class customization tied to the previous Type. A property
  row generator rebuilds for either request, so fresh row-generator tests alone
  cannot cover an open panel's Type switching. `ProfileTypeRefresh` edits through
  the real Details view's Type handle and waits for normal editor ticks; it
  never forces refresh itself. Its latent state traces the transient Profile
  through `FGCObject` and keeps callbacks weak to avoid lifetime cycles.
- The wrapper customization finds the parent's CameraType, PatchAsset or
  ActionAsset. Action reflection supplies a temporary schema/default map; all
  families reuse typed widgets and transactions. Action binding maps have
  `EditAnywhere` so PropertyEditor builds handles, but the raw map row is never
  added. Unchecked bindings display template defaults, not a runtime source.
  When a same-name Action property changes between a literal and Actor/Delegate,
  prune the old representation as well as unknown keys so override toggles cannot
  stay checked because of a stale value in the other map.
- Parameter rows provide filter labels and stable row tags. Source asset or
  Action template changes refresh weakly captured PropertyUtilities; destruction
  removes the global property-change delegate. Multi-selection never edits an
  arbitrary first object's parameters.
- `ProfileTypeMigration`, `ProfileSerialization`, `ActionProfileParameters`,
  `ExclusiveProfileDispatchAndCleanup`, `ProfileDetailsLayout`,
  `ProfileTypeRefresh`, and `GeneratedProfileParameters` automation cover migration, serialization,
  default/override behavior, exact cleanup after Context switches, handle GC,
  selected-family UI, and generated schema. Run in the IDE/editor after compilation.
- Named Context test setup uses `PCM.ActivateNewCamera(..., ContextName)`, which
  routes through `ContextStack.EnsureContext`; PCM has no PushCameraContext API.
  Object-picker `AllowedClass` conditionals explicitly use `PropertyClass.Get()`
  so both operands are UClass pointers rather than mixing TObjectPtr and raw
  pointers. `ActionObjectParameterPicker` checks the actual generated widget.
- Tool works on an RF_Transactional transient settings UObject. UPROPERTY source,
  Layer array, active index and revision GUID restore together. Tool preferences
  are NonTransactional. Successful Save records a revision checkpoint outside
  the Undo data; dirty state compares the restored revision against that checkpoint.
- Layer TraceChannel is an EditAnywhere/BlueprintReadWrite `TEnumAsByte<ECollisionChannel>`
  with Channel as its display name and ECC_Visibility as the native initializer.
  Include Engine/EngineTypes.h before the generated header. This is the same field
  pattern as Epic GameplayCameras/Public/Nodes/Collision/CollisionPushCameraNode.h;
  Engine/Private/Collision/CollisionProfile.cpp assigns project channel display
  names and unhides configured enum entries. No custom picker or enum remapping
  is needed. UE tagged-property loading leaves the Visibility initializer intact
  when reading older records without TraceChannel; no custom version is required.
  TraceLayerSurface resolves the owning Layer GUID and validates the channel, then
  forwards to the World's native channel trace without allocating. All four editor
  pick/projection sites use it, including deferred creation and edited controls.
  Preserve per-task normal/depth settings. Layer edits already cancel pending jobs,
  so changing Channel cannot mix old/new projections in one released Shape.
  Whole-struct proxy/storage/checkpoint copies preserve the field automatically;
  plain worker snapshots omit it because those workers never perform physics queries.
  Changing Channel does not reconstruct existing geometry or change runtime BVH
  membership. PIE floor occlusion is separately filtered by rendered surface/Visibility.
  LayerTraceChannel uses real complex StaticMesh traces above a different floor,
  real Brush/Shape projection, tagged round-trip and a writer omitting the new field
  to reproduce legacy data. DocumentUndoRedo/DocumentDiscard cover Details and rollback.
- `EComposableCameraMeshDrawTool` remains the viewport dispatch state for
  Brush/Rectangle/Circle/Polygon/Select/Erase. Native EComposableCameraMeshToolMode
  groups it into Draw/Select/Erase for a uniform SSegmentedControl below Layer
  properties. The Draw menu has only the four drawing types; settings remembers
  the last drawing type across Select/Erase. Switching modes cancels drafts/ends
  strokes before changing dispatch state and does not modify document revision.
  Tool/surface options retain Category=Drawing internally; an instanced Details
  customization labels it Draw/Select/Erase Options. A root-property visibility
  filter removes the old Tool enum and unrelated options; EditConditionHides
  still gates drawing variants. Select exposes snapping; Erase exposes radius/depth.
  Select Options adds Delete Selected Shape as its first custom row. NameContent
  places it in the left label column, aligned with Shape Grid Size's label.
  SBox fixes it to 125 x 24 Slate units with centered text; the same-sized numeric
  widget stays in ValueContent. The property handle's native value
  widget retains its normal transaction/slider callbacks. Delete captures the
  toolkit weakly. Save uses the same dimensions and a static-lifetime FButtonStyle
  with FSlateRoundedBoxBrush green fills, 5-unit corners and explicit hover/pressed/
  disabled states. White text stays centered; no brush allocation happens per UI
  tick. An adjacent Discard uses the same sizing/corner radius with neutral fills
  and an 8-unit horizontal gap. Selection-only EditConditions use HideEditConditionToggle to prevent
  PropertyNode from exposing the hidden bHasShape cache as a user checkbox.
  SSegmentedControl slot ToolTip attributes contain mode help; Drawing Type's
  ToolTipText and menu-entry tooltips use explicit draw-tool instructions, so an
  inactive option explains its own gesture. The compact footer joins only
  nonempty draft measurements and validation feedback, collapsing when empty.
  Saved/Unsaved state and document path/counts live in footer/Save tooltips;
  successful shape edits clear feedback. `OnToolSettingsChanged` cancels an
  active draft but preserves already released creation jobs without dirtying source;
  actual Layer edits still use `OnLayerDataChanged`. All delegates unbind on exit.
- Discard uses a separate nontransactional transient ToolSettings UObject as the
  normalized opening/latest-successful-Save checkpoint. The mode's
  AddReferencedObjects keeps its reflected Layer Profile references alive even
  when current edits remove those assets. Capture copies only Layers, source,
  active Layer and revision; it runs on initialization and successful package
  Save, never failed Save or Undo. Discard reverts an active stroke before closing
  interaction, drops queued jobs without waiting, then records committed source
  restoration in one scoped transaction. Brush/grid/projection preferences remain
  untouched. No-op/draft-only Discard creates no transaction; final refresh
  rebuilds overlays/proxy and invalidates disposable coverage. A failed Save's
  exact actor identity is retained weakly, with a flag recording whether that
  attempt created it. Discard restores existing actor source/runtime data or uses
  UWorld::EditorDestroyActor for the newly created actor, in the same transaction.
  Undo/Redo compares the retained actor's reflected Layer/source structs against
  the checkpoint only in the final Undo callback, updating the pending-rollback
  flag. No deep comparison runs in button enablement or viewport rendering.
  This keeps Discard available if Undo restores clean source while failed-save
  actor data remains applied, and prevents repeated no-op actor transactions.
  Editor-wide Undo and unrelated package dirty state are never reset.
- `ComposableCameraMeshLayerShapes.cpp` validates simple outlines, removes
  redundant collinear points, normalizes winding, and calls GeometryCore's
  `PolygonTriangulation::TriangulateSimplePolygon(..., false)`. GeometryCore is
  an editor-private module dependency. Longest-edge subdivision preserves the
  outline and bounds sampling edges by ShapeSampleSpacing. A preflight limit of
  16384 leaves rejects excessive work before collision traces; outlines cap at
  256 points. Allocation occurs on authoring events/pending creation work, outside
  runtime camera evaluation. Render traverses cached preview outlines/fills.
- Projection caches samples by document XY. Copy cached values before inserting
  further entries: TMap insertion may relocate its storage. Vertices, midpoints
  and centroid must resolve compatible floor before a leaf is emitted. Compare
  all three edge-midpoint hits and the centroid against corner interpolation:
  positional error above 1 world cm bisects the longest edge until 2.5 cm edges /
  16 refinement levels. Draw divides its local error and minimum-edge thresholds
  by the anchor's maximum absolute scale, a conservative world-error bound under
  nonuniform scale. Mixed-support leaves refine to 10 cm edges; completely
  missing samples are dropped. Unresolved error reports partial support, never a
  bridge across a discontinuity. All successes/failures remain in the sample map.
  Store accepted leaves until every query finishes, then recursively walk their
  cached dyadic edge samples and fan curved boundaries around the projected
  centroid. Both sides use the same edge points even after unequal refinement;
  otherwise moving a hanging vertex to the floor creates a vertical crack.
  Coplanar boundaries retain the original triangle. Final emission is resumable
  under the same soft time budget; output and refinement queues have explicit
  triangle/work caps. New topology can increase authored/baked triangle memory
  in curved areas, without adding per-triangle fields or SurfaceId.
  This uses the split/reproject separation described by Epic's read-only
  GeometryProcessing DynamicMesh/Public/Remesher.h; no engine implementation is
  copied and all World callbacks remain on the game thread. Document-Up traces
  are bounded around the initial hit plane; collision component identity remains
  irrelevant. Features not observed by the samples remain approximate.
  Partial floor coverage is reported; failure leaves OutData
  untouched. Successful commit saves FComposableCameraMeshAuthoredShape and fills
  TriangleShapeIds alongside triangle Layer GUIDs. Replacement removes only the
  matching Shape GUID's mesh. Empty ownership arrays are accepted for legacy source.
  Shapes and TriangleShapeIds live under WITH_EDITORONLY_DATA; baked runtime
  triangles and Layer indices continue through the existing storage/query contract.
  FProjectedShapeBuild separates bounded outline/subdivision preparation from
  resumable collision samples. A leaf can pause between any of its seven samples;
  successful and failed samples remain cached. BuildProjectedShape wraps this same
  builder synchronously for explicit geometry consumers/tests. Normal Shape
  control/Details completion uses the budgeted replacement task.
  New creation queues a small captured-plane outline fill immediately, then
  FEdMode::Tick advances only the head job once per GFrameCounter, capped at 256
  new queries / a soft 4 ms query-work budget. Collision remains on the game thread.
  Snapshot copy and final transaction serialization are additional bounded-event
  work, not covered by that query budget. Source/cache snapshots and GUID/enabled
  Layer flags go to the owned Edit pool; no World, UObject or mode pointer goes to
  the worker. Only a ready future is consumed; cancellation discards it without
  joining. Atomic cancellation skips queued work before it starts. A completed
  worker can finish on its owned snapshots after cancellation, with no callback
  into the mode. Source revision mismatch reruns coverage on current source using
  retained projected geometry. Ready results wait for active drags/transactions;
  completion installs source/cache in one creation Undo step on the editor thread.
  Queued fills survive tool changes and focus loss; explicit cancellation, Undo,
  Layer structural changes, lost World and exit discard them. Save and Brush/Erase
  mutation wait for completion. Preview fill is provisional on the captured plane;
  sampled holes, terrain heights and resolved Layer priority become visible at
  completion. ShapeProjectionBudget checks query bounds, cache reuse, leaf resumes
  and floor gaps. ShapeCreationPreview checks immediate fills for all three types,
  queue/cancel/focus/settings behavior, Save/mutation gating and atomic Undo/Redo.
  Actual viewport timing and worker rebase/cancel races require editor smoke tests.
- Rectangle/Circle drafts use drag/release; Polygon uses clicks/Enter or closure,
  with Backspace/Esc. Tool/Layer changes and LostFocus cancel drafts. Mouse release
  ends captured Brush input before checking Alt, so modifiers cannot strand a
  stroke. Dimensions and errors are separate status fields. `ShapeGeometry`,
  `ShapeProjectionAndLimits` and `ShapeInteraction` cover actual runtime boundary
  queries, concave winding, terrain samples, density rejection before projection,
  grid/anchor mapping and draft lifecycle. Compile/run in the IDE/editor.
- Layer selection is GUID-based. A selected `SListView` row updates the private
  paint index; the index is not exposed in Details.
- Selected Layer/Shape properties use separate filtered views of
  UComposableCameraMeshLayerSelection, a stable UObject proxy. Root-property
  filtering includes parent chains so nested Layer and Shape fields cannot leak
  into each other's panels. Shape Details/delete controls appear only in Select.
  The tool-options visibility delegate captures settings weakly. PreEditChange
  calls Modify on the settings document
  inside PropertyEditor's transaction; PostEditChangeProperty applies completed
  values to source. It never references array-element memory. Final FEditorUndoClient
  callbacks repopulate it after restoration. Toolkit refresh requests coalesce
  into a weakly bound one-shot FTSTicker callback calling IDetailsView::ForceRefresh
  for the Layer, Shape and tool-options views together;
  the ticker returns true while GEditor::IsTransactionActive, leaving the request
  pending until slider/stroke completion. ToolSettings PostEditChangeProperty skips
  Interactive changes; the final commit dispatches its normal callback. This also
  protects a refresh requested before capture. UE5.6 SPropertyEditorNumeric opens
  one transaction, sets InteractiveChange|NotTransactable while dragging, and
  closes it on final SetValue/release. Replacing its widget during those updates
  can lose the final callback. UTransBuffer::CanUndo refuses any active transaction,
  so the resulting symptom can appear in Draw, Select or Erase. Never work around
  that by force-closing arbitrary editor transactions.
  toolkit destruction removes pending work. UE5.6 IDetailsView does not expose
  IPropertyUtilities::RequestForceRefresh. Use PropertyHandle.h for IPropertyHandle.
  IDetailLayoutBuilder is declared by DetailLayoutBuilder.h; its interface name
  is not a valid header filename. Audit all includes in newly added tests together
  because C1083 stops compilation before later missing headers can be reported.
- Select ray-tests actual triangles to resolve Shape GUID. Editor hit proxies
  identify individual controls. Nearest ray-hit distance wins; hits within 0.001
  document units prefer the later retained Shape record, rather than triangle
  position. This preserves overlap selection after erase swap-removal and float
  retessellation. ErasePickingOrder covers this tie, hole picking and nearest-floor
  precedence. Dragging changes only cached preview controls;
  release validates/projects, preserves GUID and erase masks, then transactionally
  replaces geometry. Cached Shape overlays avoid ordinary Render allocations;
  hit-proxy allocation occurs only in the editor picking pass.
- BeginStroke keeps one FScopedTransaction until release/focus loss, calls Modify
  once and captures cancellation source/revision. No-op strokes cancel their
  transaction; Esc/right mouse restores the capture and cancels it. Layer CRUD,
  toggles and Shape commits use independent scoped transactions. Details commits
  use their existing transaction. Undo/Redo refreshes caches without reauthoring
  source during restoration, consistent with the graph Undo reentry invariant.
  Both source-settings and selection-proxy property callbacks reject GIsTransacting:
  UObject PostEditUndo can otherwise route back through ordinary PostEditChangeProperty
  and generate a new revision or reproject partially restored controls.
- EraseShapeGeometry subtracts a 32-sided circular prism by convex half-space
  splitting, triangulates surviving fragments and preserves interpolated Z and
  both ownership GUIDs. Local affine axes preserve world brush dimensions under
  the document anchor. Affected Shapes retain document-local erasure stamps;
  rebuild reapplies them. Identical retained cuts are skipped to prevent float
  roundoff from repeatedly shaving a boundary. Other Layer geometry survives.
  Brush-coordinate min/max tests reject far triangles before polygon scratch.
  Reuse those corner dot products for conservative full containment: all corners
  must lie inside the disk bounded by Radius*cos(pi/32) minus a numerical margin,
  and inside the depth slab minus that margin. This disk fits every radial plane,
  even with nonorthogonal affine axes; convexity then contains the entire triangle.
  Using Radius itself would erase retained slivers between the circle and 32-gon.
  Safely contained triangles skip plane splitting but retain the same removed-area
  threshold, dirty footprint, descending swap-removal and ownership/mask updates.
  InteriorTriangles counts these real removals and is included in ClippedTriangles.
  Original vertices use a fixed array; at most 37 clipped vertices fit inline
  capacity 40, and at most 34 outside pieces fit inline scratch. Walk original
  triangles downward so swap-removal moves only already processed source or new
  fragments into the current slot; never recut appended fragments in that stamp.
  Remove index triplets and Layer/optional Shape GUID entries together, then append
  surviving fragments. Rejected/uncut triangles and existing vertex/Shape buffers
  are not copied. Shared vertex indices remain valid. Unused vertices accumulate
  during interaction and are compacted by Save's existing RemoveOrphanedTriangles
  path; erase-to-empty clears them. This trades transient source capacity for
  avoiding document-wide allocation/copy on each stamp, not a serialized-format change.
  Optional work statistics count candidate/rejected triangles without timing
  thresholds. Changed bounds use the actual removed polygon footprint. Outside
  the cut the cached union remains valid, even though source triangulation differs;
  float source emission can introduce submillimeter boundary roundoff.
  FEraseGeometryBuild::Begin returns false for invalid source/stamps or empty
  indexed candidates and leaves a finished no-op builder. The queued painting
  path captures the native stroke rollback source only after successful Begin,
  before any Advance mutation. This avoids that extra full copy in empty regions;
  Settings::Modify transaction serialization and the first candidate-bearing
  stroke snapshot remain event costs. CCS_MeshLayers_EraseBegin and
  CCS_MeshLayers_EraseSourceSnapshot separate setup/copy from EraseGeometry.
  ContinuousErasePreview and EraseInteriorFastPath cover held-stroke scheduling,
  immutable completed input, boundary preservation, affine axes and empty candidates.
- `ShapeEditingAndErase` covers real ray picking/runtime coverage, small eraser
  holes in large triangles, Layer isolation, reflected source save/reload, mask
  replay, control editing and deletion. `DocumentUndoRedo` exercises real editor
  transactions, save checkpoints, multi-stamp erasure, Layer deletion and the
  actual Details property handle. Compile and run in the IDE/editor.
  `SelectionDetailsRefresh` exercises the real Details view and toolkit refresh,
  checks deferred/coalesced layout rebuilding, and closes with a pending request.
  `ToolPanels` checks actual Details properties and mode-specific widget visibility,
  draft cancellation, preserved Shape/revision, and restored drawing type.
  `ToolSliderTransactions` uses real numeric property handles and editor ticks to
  reproduce the native slider lifecycle in all three modes, including a refresh
  queued before capture, one final rebuild and no abandoned transaction.
  `DocumentUndoRedo` now erases through PaintAtHover instead of manually changing
  revision after a geometry-helper call; it also checks edited controls, deletion
  with retained cuts and independent Circle/Polygon creation Undo/Redo.
- Save copies source into the hidden Level actor.
- Bake drops orphaned Layer GUID triangles and resolves remaining GUIDs to
  compact indices.
- Brush constructs its existing tangent-plane radius/segment outline, then uses
  the same bounded adaptive builder. Initial spacing is half-radius clamped to
  10..100 world cm; each stamp caps emitted triangles at 4096 and reports density
  failure instead of adding a truncated mesh. Document-Up traces stabilize XY on
  curved floors and accept compatible hits across collision-component boundaries.
  Project callbacks return positions relative to the world-space center for
  centimeter error checks and FVector3f emission; convert to document local only
  after restoring that center in double precision. Absolute world float emission
  would lose small height details far from the origin. Captured Brush stamps use
  FProjectedShapeBuild::Advance under the stroke's shared frame budget; the legacy
  synchronous geometry entry points remain for existing callers/tests. Pending Draw
  creation retains its existing per-frame query/time budget. Floor normal,
  owning Layer Channel and bounded projection remain the geometric filters.
- Authoring and preview visualization clip saved triangles into a separate
  anchor-local cell cache. Normalize source winding to counterclockwise XY,
  clip against the four cell half-planes and interpolate XYZ at intersections.
  Each XY cell stores convex FResolvedSurfacePatch polygons at every elevation.
  Before subtraction, derive the subject-minus-cut plane-height gradient and
  clip the cut footprint to abs(height difference) <= 5 local units. Constant
  height differences fast-reject separated storeys. The overlap band is evaluated
  in the actual footprint, never by extrapolating planes to the grid center;
  a band crossing a cell subtracts only its matching portion.
  Incoming coverage subtracts earlier/same-priority footprints; a higher Layer
  subtracts its footprint from lower patches. Repeated stamps therefore do not
  deepen alpha, while partially covered cells can show multiple Layers without
  whole-square expansion. Single-Layer coplanar full cells collapse to one quad
  and skip redundant coverage. Cached boundary polygons preserve the silhouette
  regardless of cell size; there is no centroid fallback.
  Convex separating-edge checks precede subtraction for disjoint/touching pairs,
  retaining polygons without needless splits along unrelated supporting lines.
  Inline polygon/patch storage favors common cells; complex boundaries can grow
  on authoring events.
  Cell center/normal/Layer fields summarize a dominant patch for lookup/removal,
  not its entire visible ownership. Both FDynamicMeshBuilder and PIE mesh export
  iterate patches with variable vertex/triangle counts. Offset along document Z,
  rather than each patch normal, to keep adjacent XY edges coincident. No clipping
  or union work happens per render frame. Query/save structures remain untouched.
  VisualizationBoundary and VisualizationPartialOverlap cover boundary fidelity,
  sub-cell holes, both windings, runtime export and geometric Layer/alpha partition.
  VisualizationUnevenSurface adds grid-center false separation and band crossing,
  repeat/priority/coarse-grid checks and stacked coverage. ShapeCurvatureAndSeams
  and UnevenBrushProjection check saved intersections and shared-edge continuity,
  including real curved StaticMesh collision and far-origin Brush precision.
- Structural Layer changes invalidate full visualization; Shape replacements update affected coverage. New Shape
  creation computes regional coverage on snapshots and installs the ready cache.
  During Brush strokes, AppendAuthoringVisualization adds only new source-tail
  triangles into retained resolved cells in original source order. Its precondition
  is an already-current cache for the unchanged prefix and Layer settings; full
  invalidation/regrid falls back to the original complete resolver. During Erase,
  UpdateAuthoringVisualization removes affected grid entries, repairs
  indices after dense-cell swap removal, and rasterizes only triangles/cells
  overlapping those entries. Indexed candidate bounds cover whole dirty cells,
  rather than just the cut footprint, so surviving edges and lower Layers return.
  Pairwise surface-height tolerance and Layer ordering
  still resolve coverage at each height. A retained CellsByGrid map avoids
  recreating remote lookups; local/full equivalence compares per-Layer area and
  bidirectional interior probes within each height cell, with 1.e-3 cm^2 area
  and 1.e-3 cm height tolerances for FVector3f cut roundoff. Equivalent coverage
  need not retain the same tessellation. Cell size remains stable across release; desired growth above
  1.25x cached size can trigger an early regrid;
  full rebuild then restores the normal bounds-derived resolution. Invalid/empty
  source clears both cells and lookup. These allocations are editor authoring
  event work; runtime camera evaluation is unchanged.
  FMeshLayerAuthoringIndex is a disposable native Edit cache, never serialized
  or copied into runtime data. Each 128-triangle block stores per-Layer 3D and
  nondegenerate projected bounds. Hierarchical traversal prunes nonintersecting Layer bounds,
  followed by original exact tests on candidate triangles; scattered/interleaved
  geometry can still approach a full scan. Enabled projected boxes reproduce
  full resolver bounds without walking all source vertices on each stamp.
  Brush refreshes the tail/new blocks. Erase visits candidates in descending
  source order, marks removal and swap-tail blocks plus appended fragments, then
  refreshes those blocks after mutation. Conservative brush-space AABB rejection
  handles rotated/scaled/sheared axes; exact prism clipping is unchanged.
  Counts are a cheap validity guard, not a content hash: same-count rewrites,
  source replacement and compaction must Reset the index. One Edit mode owns
  one source/index pair; worker snapshots and other callers use the default
  unindexed path. Source work builds the index on invalidation. Its allocations
  and candidate arrays belong to editor authoring events, outside camera evaluation.
  SourceTriangleTests and ConsideredTriangles expose bounded work to regressions;
  BrushAppendCoverage, IndexedEraseCoverage and IndexedEraseEquivalence compare
  against the original full/unindexed results. Insights scopes separate
  CCS_MeshLayers_BrushProjection, EraseGeometry, CoverageUpdate and
  AuthoringIndexBuild/AuthoringIndexRefresh (all with CCS_MeshLayers_ prefix).
  MouseMove queues every spacing-qualified stamp, retaining its point/normal,
  Layer/channel, Shift mode, radius, segments and projection/floor-normal options.
  AdvancePainting runs once per GFrameCounter across viewports, with a shared soft
  4 ms budget for projection, clipping and ready preview publication. Samples run
  FIFO with no overwrite, interpolation or spacing change. FEraseGeometryBuild
  yields between descending original candidates; index refresh and Shape masks
  finalize only after all candidates. Coverage is independently owned by
  FMeshLayerStrokeCoverage: completed source mutations capture compact new-triangle
  tails or indexed regional candidates in ascending original order, with full
  document bounds and planned cell size. Regional snapshot capture retains
  only enabled triangles whose XY bounds intersect the complete dirty cells:
  filter the coarse leaf candidates before Reserve/copy, so
  remote/disabled members do not inflate allocation or worker replay. Keep
  inclusive bounds and original order; do not use a centroid or cut-only footprint.
  FAuthoringVisualizationUpdate accepts
  these bounds separately from snapshot geometry, so an empty local snapshot
  clears only dirty cells, and a small snapshot cannot shrink the document grid.
  Record the original grid growth/empty-document policy per source mutation.
  The worker receives the coverage cache by move, without copying all cells per
  stamp. It runs the same clear/RasterizeTriangle/ResolveCoverage operations with
  cancellation checks every 256 operations, outside the editor's 4 ms budget.
  Only adjacent append-only inputs at one grid resolution merge. Erase and mixed
  operations keep separate immutable inputs in exact order: replaying their dirty
  union against only the latest source could change same-Layer height winners.
  AdvanceStrokeCoverage starts pending completed inputs even while StrokeTask
  contains the next partial Erase. Advance never reads live authoring data/index;
  queued inputs were captured after the preceding mutation and index refresh.
  Removing this start barrier does not permit partial-source snapshots or change
  source FIFO, grid policy, layer identity or the completed-history guards.
  A full rebuild replaces obsolete waiting inputs. Layer snapshots contain only
  GUID/enabled state; no asset references or UObject/World/index accesses occur on
  this worker. Do not let Render replace a worker-owned cache or partial source.
  Prepared jobs consume at most eight pending coverage inputs via a queue cursor;
  no repeated front-removal or loss of later inputs. A per-job region plan records
  each region's final input. Resolve every operation, then assemble only regions
  reaching that point. Disjoint regions publish independently; full/regrid jobs
  emit their latest complete cache at the job's end. This coalesces display assembly,
  not source edits or coverage replay, and bounds long-backlog publication gaps.
  PrepareEditPreviewTiles accepts exact region keys, deduplicates them and retains
  empty removals, without building the rectangle between distant footprints.
  Normal and opening prepared outputs use immutable shared geometry with bounds
  computed on the worker. SetSharedGeometry compares bounds plus exact indices,
  positions, tangents, normals, colors and UVs for unchanged content; it retains
  the existing identity/revision and retires redundant native buffers off-thread.
  Changed content still updates capacity-reusing GPU buffers. No hash-only shortcut
  can discard a real edit. Coverage prepares native complete touched tiles in the
  same worker. No polygon
  copy/second-worker stage precedes normal Brush/Erase publication. Regional
  publication coalesces waiting versions, ignores superseded active assembly and
  retains remote restoration. Legacy QueueUpdate/LaunchQueuedTileWork is fallback.
  Ordinary AdvanceQueuedUpdates never waits; explicit zero-budget boundaries may
  drain legacy assembly.
  A shared atomic lifetime flag stops detached workers between cells/patches, so
  repeated cancellation/regrid does not finish obsolete large tile builds.
  ToolSettings::OnBeforeEdit runs before native Layer mutation/transactions and
  Details PreEditChange (suppressed during GIsTransacting). The mode flushes the
  pending stroke first; toolkit enabled toggles invoke the same boundary. Never
  rely only on post-change delegates: reordered/removed Layer indices would already
  invalidate resumable coverage, and nested transactions would merge two user edits.
  Native authoring tasks/scratch allocate on mutation only, never camera evaluation.
  Zero budgets mean unlimited: exhausted finite budgets must remain positive on
  builder calls, including when setup consumed the final fraction of a frame.
  Release finishes queued source samples over subsequent ticks and closes one
  transaction independently of derived coverage/tile publication. Mode Tick also
  services both queues when not painting; a new stroke keeps the pending jobs and
  does not wait for it. Explicit boundaries still drain both source and display.
  Save/focus/tool/close boundaries flush; Esc/Discard restore the checkpoint and
  cancel coverage/tile assembly. PostUndo discards tasks without overwriting restored source.
  BudgetedStroke, BudgetedCoverage and BudgetedEditPreview check exact source,
  independent full coverage, release/cancel/flush and buffer reuse. UE5.6 references:
  LandscapeEdModeTools.h's MouseMove/Tick/EndTool and MeshSculptToolBase.cpp /
  MeshVertexSculptTool.cpp's pending stamps and regional render notification.
  Its completed per-stamp TriangleROI/precompute notification during a held stroke
  provides the display scheduling reference; engine code remains read-only.
  Checkpoints, candidate/index setup, single queries/polygon operations and completed
  source/tile snapshot capture and upload remain indivisible. Coverage snapshots
  duplicate local source triangles; mixed/Erase inputs retain operation order and
  their memory depends on the outstanding backlog. Adjacent Brush inputs merge,
  while full invalidation removes obsolete waiting operations. Tile snapshots
  duplicate touched resolved polygons; assembly is editor-only background work.
  AsyncStrokeCoverage and QueuedEditPreviewBatch test ownership/order and exact
  batch output; BudgetedStroke checks source completion without waiting for either
  display stage. Insights adds CCS_MeshLayers_StrokeCoverageSnapshot and
  CCS_MeshLayers_StrokeCoverageWorker to isolate capture versus background time.
  CCS_MeshLayers_StrokePreviewPlan isolates batch planning. Completed native
  CoverageUpdates/PreparedTiles counters test reduced assembly without flaky timing
  assertions. ErasePreviewBatch, RegionalSnapshotFiltering and BudgetedEditPreview
  cover bounded coalescing, source ownership/order, exact output/bounds, empty/full
  replacement and shared geometry reuse. Single coverage operations, source
  clipping/copies, exact unchanged-buffer comparison and uploads remain costs.
  Explicit boundaries can take longer if a backlog
  exists. Validate actual Level timings; the soft budget is not a hard frame bound.
  Initialize native FIntPoint dirty bounds explicitly: its default constructor
  leaves coordinates unspecified, and MSVC can report C4701 across the separate
  full/regional branches. Regional work overwrites both bounds before clamping;
  full rebuilds ignore them and retain distant coverage, including invalid-bound
  fallbacks checked by IncrementalVisualization.
  QueuePaintAtHover spaces failed attempts too. Each completed source mutation
  updates revision; Tick refreshes all Level viewports during progress and two tail frames. Release retains visualization/controls,
  closes its transaction after every accepted stamp and
  redraws other viewports. Undo/cancellation restore a matching complete native
  checkpoint or invalidate caches; Layer/Shape edits invalidate their derived
  state. Save compacts unused source vertices. Read-only Preview
  builds one cache per storage actor and releases caches on exit.
- Edit fill retains GPU buffers through an editor-only UMeshComponent scene
  proxy, rather than calling FDynamicMeshBuilder per Layer/view/frame. Its
  FStaticMeshVertexBuffers, FDynamicMeshIndexBuffer32 and FLocalVertexFactory are
  initialized on capacity growth, updated in place for smaller changes,
  and released with proxy teardown on the render thread. The installed UE5.6 Engine/Private/StaticMesh.cpp::InitFromDynamicVertex
  and read-only ProceduralMeshComponent scene-proxy resource lifecycle provide
  the engine API reference; no reference-plugin source is copied.
  Ordinary GetDynamicMeshElements only creates engine-owned mesh batch/uniform
  descriptors referencing those buffers; these small per-view engine allocations
  remain necessary, with no cell traversal, vertex copies or GPU mesh upload.
  Include PrimitiveUniformShaderParametersBuilder.h directly when constructing
  the builder: PrimitiveSceneProxy.h and SceneManagement.h only forward-declare
  it in UE5.6. Component locals must avoid inherited names such as Bounds;
  use VertexBounds for the temporary geometry box to avoid C4458.
  FMeshLayerEditPreview partitions resolved patches by Layer row and 32-cell XY
  tiles. Full updates walk cells once. Regional updates look up every cell in
  touched tiles through CellsByGrid, remove emptied components and retain remote
  buffers. Negative coordinates use floor, not integer truncation. Grid resize,
  invalid bounds and document-wide changes refresh all tiles.
  Keep bEditPreviewDirty separate from bVisualizationDirty: a worker-installed
  new-Shape coverage cache still needs uploading. Brush/Erase queue complete tile
  snapshots and coalesce pending publication independently from source edits. The
  legacy BeginUpdate/AdvanceUpdate path retains fixed-coverage resumable assembly
  for synchronous callers; queued workers reuse its complete-tile publisher after
  native vertex/index construction. Exact render-attribute/color
  comparison skips unchanged buffers, without changing 32-cell tile size/draw count.
  Full updates bin cell indices in original source-cache order; regional updates
  retain their row-major grid lookup. Undo/cancellation/Discard restore matching
  coverage and buffers together, or invalidate uncached versions. Layer/Shape
  changes invalidate obsolete coverage/publication.
  Empty coverage is a completed cache. Check Level/actor readiness without
  allocating, and preserve PDI fallback if publication fails.
  AppendVisualizationPatch retains the PDI fan, document-Z offset, packed tangent
  basis, zero UVs and white vertices. FColoredMaterialRenderProxy receives the
  exact clamped FLinearColor, avoiding gamma/byte conversion and MID shader
  changes. Backface culling remains disabled; no extra geometry doubles opacity.
  Engine-owned scene-proxy teardown releases render resources; no per-toggle
  FlushRenderingCommands or manually retained raw render pointers are used.
  Ownerless components and global strong UObject references are forbidden.
  The Level-owned actor is transient/duplicate-transient, ignores PIE duplication,
  remains visible in G view and creates no collision/picking/nav/shadow/ray-tracing
  work. Capture views and temporal primitive occlusion are excluded. It does not
  register with Show's Landscape LOD extension. Existing small PDI drafts and
  controls remain unchanged. Insights scopes CCS_MeshLayers_EditPreviewUpdate
  and CCS_MeshLayers_EditBufferBuild occur only on mutations, not stable frames.
  EditPreviewPersistentBuffers/RegionalUpdates/Invalidation cover actual component
  and proxy identity, linear color, local heights, Layer ordering, empty cells,
  mode-render resource counts and cleanup. Real pixels/FPS require Editor checks;
  opening Edit and uncached Undo/Redo/Discard resolution run on native workers,
  as do interactive coverage/assembly. Each completed component upload remains indivisible.
  Edit restoration first tries native editable history keyed by DocumentRevision.
  Components own shared immutable FEditPreviewGeometry (vertices, indices, local
  bounds); SetGeometry preserves identical buffers and moves changed arrays into
  a new buffer. RememberRevision stores shared references, component keys,
  cell size and exact linear colors, plus an immutable FEditPreviewCheckpoint with
  the complete resolved polygons/grid and authoring broad phase. RememberPreview
  moves the coverage cache, copies the native block hierarchy and accounts for
  nested native allocations once per completed mutation. Reset the moved-from
  visualization's scalar bounds/cell size too: moving its arrays alone leaves old
  valid bounds, which must not mask the retained immutable base. Only a complete
  display/cache of the matching source revision is eligible, including explicit
  focus/save stroke completion. Never
  capture partially published/worker-owned coverage or relabel old display after
  Settings has already been changed by engine Undo. PrepareUndo and PostUndo
  invalidate provenance before clearing interactions/source-only stamp completion.
  QueueRestoreRevision compares pointers/colors/presence and queues changed spatial
  tiles with all their saved Layer rows; absent rows clear during the same budgeted
  publication. Reuse native buffers and their bounds directly, retaining remote
  proxies and exact render attributes. No coverage clipping, scene tracing,
  geometry copying or mesh worker sits ahead of that restoration. Allocations here
  are one-time mutation/checkpoint metadata and necessary component/proxy updates,
  not stable frame evaluation. Keep at most 32 checkpoints and a soft 128 MiB unique
  historical-buffer/coverage/index budget, excluding the current checkpoint and
  live buffers. Saved/current revisions are protected; their irreducible memory
  may exceed the soft limit. Shared tiles count
  once. Retire evicted history and replaced native buffers off-thread; no UObject/
  render resources go with them. Cache misses retain the progressive rebuild fallback.
  A complete restored checkpoint installs coverage/index before accepting Brush,
  with no document rebuild. GetVisualization reads its immutable cache until a
  stroke result replaces it. FMeshLayerStrokeCoverage::UseCheckpoint hands only
  native shared data to its worker. The first mutation copies resolved cells/grid
  there (linear in cells/patches, no scene trace or full clipping), applies the
  existing append/regional operation and returns a mutable cache; following queued
  operations reuse it. No checkpoint is modified by worker or source edits. Cancel,
  eviction, replacement and teardown retire large native data off-thread. Stable
  frames neither capture nor copy caches; editor mutation snapshots/index metadata
  and necessary component publication are the allocation exceptions here.
  BeginStroke retains unfinished historical publication when its editable base is
  available. QueueUpdate removes only unpublished restore versions of touched
  tiles, retaining distant restore/removal rows. Those tile workers can start while
  restoration is still ready, and completed stroke tiles publish ahead of remaining
  history. Full/regridded coverage still supersedes the whole queue.
  Display-only checkpoints retain FMeshLayerDocumentBuild's bPublishPreview=false
  cache reconstruction fallback; tiny startup regions never replace complete
  historical buffers. Saved-source compaction can invalidate the checkpoint index
  counts without altering coverage; the existing IsCurrent check rebuilds the
  broad phase before the next source edit. Uncached/evicted versions still rebuild.
  RevisionEditPreviewHistory, ImmediateRestorationDisplay and
  RestoredPreviewImmediateStroke cover restoration plus subsequent real editing,
  exact full-resolver equivalence and no whole-document first-Brush snapshot.
  Read-only UE5.6 reference: MeshModelingTools/Private/MeshVertexSculptTool.cpp,
  OnBeginStroke/WaitForPendingUndoRedo, EndChange, OnDynamicMeshComponentChanged
  and FastNotifyTriangleVerticesUpdated; LandscapeEditor/Private/
  LandscapeEdModeTools.h, TLandscapeEditCache::SetCachedData.
  The plugin keeps its existing UObject source transaction; it applies the regional
  display-update principle without adopting Modeling's source-change format/waits.
  FMeshLayerDocumentBuild owns original triangle arrays (retaining index vertex
  counts) and only Layer GUID/enabled metadata. Production StartResident builds
  the index, emits it early, then validates saved polygons or resolves complete
  legacy coverage. One whole shared native geometry document includes prepared
  bounds. PublishPreparedDocument installs all regions in one editor/scene update
  and removes absent regions in one component pass. Internal 32-cell regions remain
  for local updates, not progressive initial load. Start/StartSaved and progressive
  queues remain for explicit compatibility consumers/tests.
  During opening, BeginStroke retains the base. Input waits unconditionally for
  IndexReady, even when an empty/equal-count index appears current. Append/Erase
  update the installed index; final completion cannot replace it with an older copy.
  Primary FIFO inputs capture append tails or whole-cell candidates but do not launch
  until the complete base cache arrives. Loading's bVisualizationDirty must not force
  a full-source snapshot per stamp. OpeningRegionCoverage runs independently with
  bRegionOnly, fixed OpeningCellSize, complete render-region bounds and INDEX_NONE
  replacement. It captures current candidates, including old neighbors, and never
  regrids or copies the missing base. Shared prepared regions publish immediately;
  empty output clears Erase regions. OpeningEditedRegions records every mutated
  region before worker completion. Base publication skips all these regions,
  including erased/empty ones; untouched regions appear together. Consume the base,
  cancel provisional work before primary deltas launch, and move its cache into
  Visualization. Primary FIFO then converges to current source. A touched region
  whose feedback was still pending waits for its primary delta rather than showing
  stale base. Metadata retags the load and selects current colors; structural
  replacement, Undo/Discard/exit cancel base and feedback together.
  Retire unused terminal index copies and skipped stale prepared regions on the
  native pool too; otherwise their final release can recreate a completion hitch.
  Retire old coverage caches and canceled native futures/snapshots/ready geometry
  on a worker to avoid replacing compute stalls with bulk-deallocation stalls.
  Workers hold native source/options/geometry only. Engine components own geometry
  and proxy lifecycle; resource initialization/update/release run on the render thread.
  AdvanceDocumentPreview schedules/polls in Tick, independently of view movement.
  Render never builds the index/cache or calls full Update. Brush source work and
  native opening coexist; snapshots follow complete mutations. Unchanged tool/focus
  boundaries retain opening. Cancel/Undo/Discard and structural replacement detach
  obsolete generations; normal Brush/Erase does not.
  Save first consumes resident coverage, then compacts source. Any remaining
  legacy pending snapshot is canceled, preserving an unchanged historical display
  restoration if present; no-op compaction keeps the current index.
  Compaction may rewrite index counts without touching the revision, so revision
  validation alone is insufficient at that boundary. Viewport Undo/Redo
  uses PrepareUndo: finish every accepted stamp without queuing/consuming derived
  coverage or meshes, close the transaction, then restore source and rebuild.
  Checkpoints, Settings::Modify serialization and linear triangle snapshot copies
  remain editor-thread work. Allocation is confined to document mutation, native
  workers and necessary component publication, not stable per-frame camera evaluation.
  ResidentLoadingAndBrush covers withheld base during Brush/Erase, cached/legacy
  whole publication, stale-base protection, FIFO convergence, metadata, proxies and
  cancellation. AsyncDocumentPreviewBuild/ProgressiveEditPreview cover standalone
  progressive APIs. RestoredDocumentPreview/BudgetedStroke cover exact attributes,
  native ownership, compaction and source-only Undo. Region preparation can reorder cells
  and buffer indices; it does not change triangles, normals, UVs, color or coverage.
  Read-only Preview's `FPreviewGeometryBuild` snapshots only triangle arrays and enabled/color/identity
  Layer metadata; no query BVH, Profile, World, Actor or mode pointer reaches the
  dedicated low-priority ThreadPool worker. Create this single-thread pool during
  module registration, keeping thread creation off Show activation and avoiding
  shared-pool saturation across many documents. `BuildRuntimeVisualizationTiles`
  computes the same global cell size as full resolution, bins triangles into
  32-by-32-cell tiles and sorts by captured local view origin. Before PIE's first camera-cache update,
  use the possessed Pawn location as the ordering focus. No business Query/Update
  call runs automatically. Each tile resolves
  all candidates in source order through the shared RasterizeTriangle/ResolveCoverage
  path, then exports final meshes immediately. No approximate first-pass overlay
  or density change is involved. A per-job SPSC queue carries completed batches
  before the worker future is ready. First try an 8-by-8-cell region near the
  focus; if occupied, publish it and exclude its cells from the remainder of the
  regular tile. No provisional geometry or overlap is introduced. Editor
  jobs also build native topology in chunks of at most 1024 triangles; PIE jobs
  preallocate the source-XYZ projection hash instead. Destroy intermediate grid
  storage on the worker, and move only ready outputs to the game thread.
  `TakeResult` drains queued batches before consuming its terminal future.
  Recheck the queue after future readiness: the worker can finish/enqueue between
  the initial dequeue and readiness check. `bComplete=false` keeps the handle
  alive; the terminal marker closes it with total triangle accounting. Editor
  adoption appends rather than overwrites unsubmitted native chunks. PIE fits/
  publishes each batch before adopting its successor, with per-batch hash
  preallocation. Carry the already-published flag across batches so only the
  first document chunk uses the 64-triangle size. Normal Reset/toggle never Wait.
  Cancellation checks between triangles/grid rows/export cells/native chunks let
  abandoned work exit. A main-thread registry owns orphan workers until ready;
  once a cancelled job is ready, dispose its abandoned output queue on the owned
  worker so large native batches are not freed inside the preview Tick.
  module shutdown cancels and drains it after both modes/ticker release caches,
  then joins/destroys the owned pool. Future readiness alone happens before
  AsyncPool destroys its callable, so waiting on futures without joining the pool
  can still leave plugin template/lambda code executing during DLL unload.
  A new generation owns a distinct handle and cannot publish a cancelled result.
  Initial array snapshot copies remain game-thread construction work; the expensive
  clipping and topology pass does not. AsyncPreviewBuild compares both outputs
  against synchronous clipping and checks cancellation/replacement/coverage.
  `IncrementalVisualization`, `EraseBroadPhase`
  and `StrokeVisualizationRefresh` cover equivalence, work bounds and lifecycle.
  EraseLocalVisualization checks cut-sized cell work and retained remote coverage;
  DisjointPreviewPatches checks bounded cache fragmentation. EraseBroadPhase also
  verifies reserved source buffers/vertices survive and no-op cuts preserve counts.
- PIE cannot use `FEdMode::Render`: that callback draws only Level Editor
  viewports and resolves the editor world. The Show command therefore builds
  the same resolved patch meshes into a transient Actor in each source PIE
  Level. Each visible chunk has a `UDynamicMeshComponent` and a persistent MID
  using DebugMeshMaterial's Color parameter. This material depth-tests and is
  two-sided; opaque characters occlude the preview, with no duplicated backface
  geometry. Read-only editor preview uses persistent native chunks with GeomMaterial,
  which disables depth testing; Edit retains persistent fill with PDI drafts/controls.
  PDI explicitly disables backface culling, but GeometryFramework's persistent
  DrawBatch follows the material's one-sided culling. Capture GeomMaterial's
  IsTwoSided on the game thread and pass only a bool to the worker. When needed,
  BuildNativePreviewMeshes appends identical disconnected vertices and triangles
  with reversed winding; sharing those vertices can reject non-manifold faces.
  Halve the source chunk allowance so both windings together remain at most 1024
  triangles. Expected/rejected native counts include both faces; resolved source
  counts remain unchanged. Keep reverse faces off for two-sided editor materials
  and for PIE, avoiding double translucency. No vertex-height correction, shader
  compilation or per-frame geometry rebuild is needed for this visibility fix.
  EditorPreviewBackfaces checks paired positions/opposite normals with slopes,
  both source windings, shared vertices, mirrored transforms and short tails;
  AsyncPreviewBuild/EditorPreviewPublication cover worker/publication face counts.
  Matching the editor depth policy in PIE hides the floor-fitting defect by
  drawing over characters.
  Instead, fit a disposable copy of cached vertices before uploading it. The Editor
  module privately depends on GeometryFramework; no authored asset or runtime
  camera API changes. Editor publication runs in AdvancePreview from the core
  Show ticker and EdMode Tick, guarded once per GFrameCounter rather than once
  per viewport. A soft 2 ms budget and a 16-chunk safety cap are shared across
  loaded editor documents. UEditorEngine::Tick skips viewport Tick while Slate
  throttles expensive tasks unless bNeedsRedraw is already set, so worker
  completion must not depend solely on FEditorViewportClient::Tick. Successful
  publication plus two following core frames invalidate static Level viewports;
  completed stable caches stop redrawing. While PlayWorld exists, unfinished
  editor builds cancel and retain displayed components; interrupted documents
  restart on return. PIE geometry avoids queuing behind redundant editor work.
  Completed caches survive; PIE-ending cannot block resumption after PlayWorld clears.
  Prepared native meshes move into components; no full Layer native build runs on
  the game thread. Temporary editor Actors are transient, non-duplicatable and
  have non-selectable components. Keep Hidden In Game and bIsEditorOnlyActor false
  so an explicitly requested Show overlay also appears in editor Game View (G).
  FPrimitiveSceneProxy::IsShown rejects Hidden In Game through DrawInGame, and
  rejects editor-only owners whenever the view family's Game show flag is enabled.
  Visibility is separate from PIE duplication: RF_DuplicateTransient causes the
  engine duplicate writer to serialize a null reference, while bIgnoreInPIE also
  rejects PIE actor/Level streaming inclusion. Do not use a visibility flag to
  prevent duplicated editor overlays over the separately fitted PIE preview.
  EditorPreviewPublication tests actual render-proxy draw relevance in ordinary
  editor/Game View families, with negative controls for both old flags, plus
  duplication policy, ownership, package dirtiness, material/color and teardown.
  Rendering flushes are confined to tests and module unload; ordinary preview
  work keeps its asynchronous budgets.
  The dedicated single worker retains at most four resolved local-mesh snapshots
  under a 64 MiB buffer budget, evicting least-recently used entries. Exact vertex,
  index, triangle-Layer and Layer identity/enabled/color comparisons happen there,
  outside the game thread. Same-count changes cannot falsely reuse old output.
  Tiled vs complete layouts also participate in identity. Tiled entries retain
  mesh centers; hits sort indices near the new view and copy/publish each mesh
  independently rather than copying the entire output before the first batch.
  Editor native conversion and PIE projection-cache reserve still run per request;
  expensive coverage resolution/export are reused. Each caller receives a mesh
  copy, so PIE fitting never changes the cached master. Transform/World/collision
  are intentionally excluded from local geometry identity and remain caller-owned.
  Snapshot creation copies only geometry/metadata, with Profile fields left null.
  Normal toggle/streaming cleanup cancels handles but keeps bounded native reuse;
  unload joins the pool before clearing this worker-only cache. Cold or oversize
  documents still need bounds/binning and first-tile preparation before visibility,
  but never wait for whole-document clipping/export. One serial worker can still
  queue separate documents; camera distance never culls a tile.
  AsyncPreviewBuild checks editor-to-PIE reuse; PreviewGeometryCacheInvalidation
  checks vertex/index/ownership/color/enabled changes against full reference
  meshes. StationaryPreviewPublication checks no-camera-input publication and
  redraw quiescence; PreviewPublicationBudget uses simulated elapsed work to
  distinguish cheap-loop safety caps from expensive-upload time expiry.
  StreamingPreviewGeometry checks triangle multiset equivalence on slopes,
  overlaps, distinct storeys, negative coordinates and disabled bounds, plus
  first-tile cancellation/focus, batch-before-terminal ordering, snapshots and
  editor-to-PIE cached streaming. PIEPreviewProgressiveMeshes also verifies startup
  chunk size is not repeated on later geometry batches.
- Landscape LOD morphing is view-dependent, whereas saved triangles and fitted
  preview chunks are fixed. The user's r.ForceLOD 0 comparison restores distant
  coverage. FMeshLayerPreviewViewExtension therefore sets each eligible family's
  LandscapeLODOverride to 0 in BeginRenderViewFamily on the game thread. UE5.6
  Renderer/Private/SceneRenderBuilder.cpp calls this before creating the scene
  renderer; Landscape/Private/LandscapeRender.cpp::GetViewLodOverride and
  FLandscapeComponentSceneProxy::ComputeLODForView consume that family override.
  This retains scene depth/character occlusion without moving the preview or
  changing Landscape UPROPERTYs, r.ForceLOD or StaticMesh LOD. A fresh family
  naturally preserves normal LOD after Show off, without save/restore mutations.
  The extension is initialized in the Editor module's PostEngineInit phase.
  It is active only while Show is requested, the opt-out CVar remains enabled,
  and a live published preview belongs to the actual Editor/PIE scene world.
  PIE ending, asset/Game worlds and scene/reflection/planar captures are excluded.
  A weak Actor list updates/prunes only at publication/cleanup; view callbacks
  reuse it without allocation, queries, resource uploads or render-thread UObject
  access. In-flight families retain extension references, so module Unregister
  disables it and FlushRenderingCommands before releasing the instance. Ordinary
  Show/PIE teardown never flush. The default-1 editor control is
  CCS.Editor.MeshLayers.StabilizeLandscapeLOD; 0 opts out while leaving Show on.
  All Landscape in an eligible family uses LOD 0, increasing distant terrain
  rendering cost. PreviewLandscapeLODStability checks real callback routing,
  capture opt-out, lifecycle, near/far origins, unchanged height/global CVar and
  no package dirtiness. It cannot establish pixel output or frame times; test
  the user's terrain after restoring the global diagnostic r.ForceLOD to -1.
- `FPIEPreviewSurfaceProjection` advances only during construction. A source-XYZ
  map is reserved before processing, shares samples across meshes and preserves
  stacked-height identity. `ProjectPIEPreviewVertex` queries all collision object
  types along document Up, within 100 world cm of the approximate floor, then
  selects the nearest eligible upward-facing hit. It accepts Visibility-blocking
  collision and rendered StaticMesh collision with at least one opaque/masked
  material, even if Visibility is Ignore/Overlap. Component/owner visibility and
  ShouldRender gate the rendered-mesh case; null material slots use the engine's
  opaque fallback. Non-rendered Ignore/Overlap volumes, initial penetration and
  Pawns remain excluded. A second pass promotes to the highest rendered StaticMesh
  at most 10 world cm above the nearest floor. Anchor that pass to the original
  nearest height so hit order cannot chain several small lifts across storeys.
  Merely accepting a slab is insufficient: underlying Landscape may still be
  nearer to the authored source and bury the overlay. Authoring picks and PIE
  scene occlusion are different policies; never mutate source Layers or floor
  collision responses to reconcile them. The 10 cm band handles nearby overlapping
  surfaces, not arbitrary upper floors. A fully submitted overlay can still be
  buried: the newer point log has native Z=-7.070, submitted Z=-5.570 and slab Z=0,
  beyond the former 5 cm limit. PIEPreviewOccludingFloor covers both 1.992 and
  7.070 cm gaps, unchanged native query height, a rejected 60 cm storey and
  out-of-band promotion. Raising this preview limit must not expand runtime Layer
  membership or disable scene depth. Restricting PIE to WorldStatic silently
  misses WorldDynamic/PhysicsBody Blueprint floors. Keep object multi queries
  plus eligibility filtering, because a channel
  multi query stops at its first blocking hit and can miss the nearest stacked
  floor below a Pawn or another floor. It changes only local Z and adds
  `VisualizationSurfaceOffset` once. Misses retain source positions; exact XY
  boundaries, indices, colors and source data are preserved. UE5.6 object multi
  queries return all matching hits, allowing nearest-floor selection even below
  another floor. CollisionQueryFilterCallback::CalcQueryHitType implements this
  object-query policy. Query buffers are reused.
- All pending documents/worlds share a 2048-query safety cap and a soft 4 ms fitting
  budget per ticker invocation, checked between vertices. Charge elapsed time
  around Advance only; a deadline started before world discovery can starve
  fitting even when it performs no work. A single physics query, initial
  clipping/allocation or mesh upload is outside that soft bound. A captured
  anchor transform keeps a pending job consistent; the finished Actor uses the
  latest anchor transform. Fitting buffers are released after upload or
  cancellation. Stable frames do no projection work. Collision fitting
  cannot guarantee contact with shader displacement or curvature between
  vertices, and does not track moving floors after completion.
- Discover/prune every loaded document before spending fitting/publication
  budgets. `RunPIEPreviewWorkRoundRobin` retains independent positions for the
  two phases. Fitting uses at most 64 actual queries per visit; publication
  consumes at most one ready chunk. On budget exhaustion retain the unserved
  index for the next tick. Skip inactive/unfinished-chunk jobs; stop after a
  complete skipped cycle instead of spinning. A single pending document can
  use repeated visits up to the existing global budget. Sequential Actor-order
  processing can consume every frame's budget on one large document and keep
  later Levels at zero queries/components despite working progressive output.
  Reuse a weak-key scratch array; reserve extra capacity only when document
  count grows, and never retain TMap value pointers across discovery/pruning.
  Stable ticks reuse capacity and issue no floor queries. Clearing caches also
  clears scratch storage and both saved positions. Streaming changes normalize
  indices to the current job count. PIEPreviewScheduling exercises real fitting
  and chunk extraction across unequal documents plus resumable/empty queues.
- `TakeReadyMeshes` consumes fitted triangle prefixes without waiting for the
  whole document. It remaps vertex IDs into an initial chunk of at most 64
  triangles, then chunks of at most 1024. Incomplete chunks wait until full,
  while completed Layer meshes flush short
  tails. A consumed cursor prevents repeat geometry. Across all worlds, at most
  a soft 2 ms budget and a 16-chunk safety cap bound component publication. Previously
  published components remain intact while later chunks fit. On completion,
  continue draining ready tails under the same soft 2 ms / 16-chunk publication
  budget, then release fitting buffers. `IsPublicationComplete` must be checked
  separately from `IsComplete`: the final fitting visit can finish while many
  ready chunks still await upload. Keep completed chunks persistent; do not replace
  them with a synchronous whole-Layer upload that stalls at load completion.
  More components/draw calls remain, but each upload and its culling bounds stay
  small. Chunk/remap/component allocations are construction-only. Keep
  an existing construction Actor weakly tracked if append fails so cache release
  still unregisters it. Reset discards publication cursors along with fitting.
  C++ condition declarations remain in scope in both branches of an if/else.
  Use distinct names for a remap lookup pointer and a newly inserted vertex ID;
  redeclaring the condition name in the else body causes MSVC C2373.
  PIEPreviewSurfaceProjection covers both remap insertion and shared-ID reuse.
- `CCS.Editor.MeshLayers.DumpPIEPreview` inspects cached preview components only on
  explicit command execution. It logs pending state, submitted/expected triangles,
  ReusedGeometry, FirstGeometrySec (first nonempty batch adoption, -1 while waiting)
  and GeometryWaitSec (elapsed until all batches are adopted, including worker
  queue/preparation and intervening fitting/publication, not CPU-only timing),
  query/miss counts, accepted non-static floors and signed floor correction in
  world cm (range includes zero). A completed cache should have `Pending=0` and
  matching triangle counts; misses retain source positions and can still be
  buried by rendered ground. Count rejected FDynamicMesh3::AppendTriangle results
  and log a warning during component construction rather than silently discarding
  topology errors. Diagnostic allocations and mesh inspection are outside Tick.
  TMap range-for elements are TPair values with Key/Value fields; only explicit
  map iterators expose Key()/Value(). Calling Pair.Key() treats the weak key as
  a function and causes C2064; inside UE_LOG, format-validation template errors
  then cascade from that invalid argument. Fix the argument before changing the
  format string. ConsoleControls.MeshLayerPreviewDump checks diagnostic command
  registration, editor classification and dispatch without a selected world;
  compilation guards the argument expressions in the populated-cache log.
- Aggregate document statistics cannot localize a missing patch. On that same
  explicit command, sample the first player Pawn's collision foot point in each
  cached PIE world. Log accepted complex/simple fitting results and raw complex
  hit identities/responses; also enumerate nearby StaticMesh bounds candidates
  to expose components that collision queries never hit. These bounds are only
  candidates, not proof of rendered triangle coverage. Compare native Layer
  heights (query origin foot +100 cm, downward range 600 cm) with submitted
  DynamicMesh triangle intersections (world-Z segment foot +/-500 cm, nearest
  height). `DepthComparable` guards the floor-height delta when either coverage
  or an accepted complex hit is absent. Mesh diagnostics include material blend
  mode and ShouldRender state. A submitted but buried mesh differs from
  missing submitted geometry; `Pending=1` may explain the latter during loading.
  `SamplePIEPreviewActor` reads component transforms and mesh data without editing
  them. Its O(triangle count) scan, candidate enumeration, logs and temporary
  arrays execute only on request, never in the preview ticker. The regression
  fixture checks scaled/rotated stacked surfaces, absent coverage and buried
  coverage. The floor fixture uses an ordinary Actor, SceneComponent root and
  scaled child StaticMesh with complex queries. Asset names or Blueprint node
  references alone do not establish active PIE transforms, collision settings
  or connected material displacement; diagnose the actual point first.
- A persistent debug batch is only persistent CPU data, not persistent GPU mesh
  buffers. UE5.6 `Engine/Private/Components/LineBatchComponent.cpp` rebuilds an
  FDynamicMeshBuilder for every mesh/view/frame. GeometryFramework's
  `Private/Components/DynamicMeshSceneProxy.h` initializes/uploads render buffer
  sets on mesh changes; `BaseDynamicMeshSceneProxy.cpp::DrawBatch` references
  those buffers during drawing. PIE preview uses this engine-owned component
  path to avoid repeated vertex/index construction and uploads. Geometry/MID
  allocations occur on cache creation; stable drawing reuses them. Movement
  updates only the Actor transform, including per-component culling bounds.
  Collision/cooking updates, navigation, component ticking, shadows and ray
  tracing are disabled. Creation/build/ticker Insights scopes use the
  CCS_MeshLayers_PIEPreview prefix.
- Never register an ownerless editor-created `UPrimitiveComponent` into a PIE
  world and retain it through a global `TStrongObjectPtr`: `EndPlayMap` can
  release the world's `FScene` before a later ticker/GC pass drops that object.
  PIE previews use Level-owned transient Actors, actor-owned instance components
  and component-owned material references; global caches are weak only.
  `PrePIEEnded` destroys preview Actors before scene release, while
  `PostPIEStarted` re-enables routing. Show off/Edit entry/module unload and disappeared
  storage also cancel pending fitting and destroy only the associated preview
  Actors. Source Level unload
  owns them independently of polling. Existing external debug batches are never
  cleared. Empty documents are initialized caches rather than repeated builds.
  Editor-module placement and PIE-only creation exclude Shipping. Do not attach
  these meshes to the hidden storage actor: owner hidden state would suppress
  the preview.
- Closing the mode's primary tab routes back to `FEdMode::RequestDeletion`.
  Exit guards against close-callback re-entry and explicitly redraws Level
  viewports. Keep Show intent separate from the read-only mode's active flag.
  Edit Enter notifies the tool coordinator, disables preview view routing and
  releases PIE caches while retaining requested intent. Actual Edit Exit clears
  the pause only after document handling and render cleanup. The existing
  preview ticker restores a missing read-only mode outside that Exit stack,
  guarded by both the Enter/Exit pause and active Edit flag, editor-world cleanup
  and engine shutdown. UE5.6 EditorModeManager.cpp::DeactivateModeAtIndex removes
  ActiveScriptableModes immediately, while Tick::ExitAllModesPendingDeactivate
  runs Exit later; testing only IsModeActive is insufficient. Preview-on still
  deactivates Edit in one click, with activation deferred until cleanup finishes.
  Show-off during Edit clears intent without closing Edit or resuming later.
  PreviewModeResumeAfterEdit exercises real menu, selector and toolkit-close
  paths, the pending-Exit interval and explicit off; the test requires an idle
  Level Editor outside PIE and restores its created modes on completion.
- Register custom Level Editor modes with a resolved normal/small icon pair
  from `FComposableCameraEditorStyle`. Passing default `FSlateIcon()` to mode
  registration leaves the active-mode icon slot blank even when the mode name
  renders correctly. The Edit/Show Mesh Camera Layers ToolMenus entries use
  `FSlateIcon()` intentionally for text-only toggles; their mode registrations
  keep the resolved icon pair.
- Read-only preview is a separate legacy editor mode and never exposes storage
  actor details. Its PIE companion is rendering-only and never changes data.
- Geometry optimization must consume authoring data and emit runtime data. It
  must not round-trip simplified geometry back into authoring state.

## 24. Maintenance Rule

Update this document when:

- adding/removing node, transition, patch, modifier, action, or interpolator.
- changing runtime data layout.
- changing graph sync/rebuild mechanics.
- changing hot-path behavior.
- discovering a new recurring gotcha.
- making stale comments or docs materially wrong.
