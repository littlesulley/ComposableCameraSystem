# ComposableCameraSystem Tech Notes

Updated: 2026-10-05

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

Shot Editor quick-control technique:

- `SShotEditorRoot` keeps Quick controls in a collapsed-by-default strip as a
  mirror of selected `FComposableCameraShot` fields, not a parallel data model.
- Quick collapsed state and the viewport toolbar collapsed state are persisted
  in `GEditorPerProjectIni` under `ComposableCameraSystem.ShotEditorLayout`;
  defaults are Quick collapsed and viewport toolbar expanded.
- Quick numeric widgets use a per-widget `TOptional<float>` drag cache. The
  cache updates while editing; the Shot writes once on text commit or slider
  release through `FScopedTransaction`, host `Modify()`, and
  `PostEditChangeProperty(ValueSet)`.
- Quick controls must resolve the active Shot property the same way the Details
  bridge does: section inline shots post `InlineShot`, asset-reference override
  shots post `ShotOverrides`, and ShotAsset / node hosts post `Shot`.
- Do not fire per-tick `PostEditChangeProperty(Interactive)` from Quick
  controls. Sequencer-backed shot sections can respawn preview actors during
  those broadcasts.

Shot Editor status bar technique:

- `SShotEditorRoot::TrySetMode` classifies mode requests through
  `Widgets/ComposableCameraShotEditorModeSwitchUtils.h`; Free -> Drag / Lock
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
- `ComposableCameraMeshSurfaceTests.cpp`
- `ComposableCameraMeshProfileTests.cpp`
- `ComposableCameraMeshProfileCustomizationTests.cpp`
- `ComposableCameraMeshLayerToolSettingsTests.cpp`
- `ComposableCameraMeshLayerVisualizationTests.cpp`

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
  declarations. In particular, declare `FSpawnTabArgs` as `class`; MSVC C4099
  becomes a build failure when warnings are treated as errors.
- Lambdas returning a typed index in one branch and `INDEX_NONE` in another
  need an explicit `-> int32` return type. `INDEX_NONE` is an anonymous-enum
  sentinel, so implicit deduction fails with MSVC C3487.
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

Runtime types:

- `FComposableCameraMeshLayerDefinition`: GUID, name, profile, enabled state,
  debug color. Array index is editor display order; index zero is topmost.
- `FComposableCameraMeshSurfaceAuthoringData`: editor-only full source using
  one stable Layer GUID per triangle.
- `FComposableCameraMeshSurfaceRuntimeData`: cooked indexed triangles using
  one Layer array index per triangle.
- `AComposableCameraMeshSurfaceStorageActor`: hidden, `NotPlaceable`,
  Level-local serialization anchor.
- `UComposableCameraMeshWorldSubsystem`: loaded-storage registration,
  per-local-player query, profile switching.

MVP query:

```text
player world position
  -> each storage actor inverse transform
  -> local downward ray against indexed triangles
  -> nearest surface
  -> collect every enabled Layer within SameSurfaceTolerance
  -> top-to-bottom Layer-order results
```

No `UStaticMesh` or collision query mesh is involved. Query outputs use inline
capacity for 16 overlapping Layers; deeper overlap may allocate. The current
query uses two linear triangle passes. A later BVH/tile index may replace
traversal without changing actor, profile, or tool contracts.

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
Camera Type, Transition, Action and Patch soft references sync-load only on a Profile edge,
never on the per-frame unchanged fast path.

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
  and centroid must resolve compatible floor before a leaf is emitted. Document
  Up traces are bounded around the initial hit plane; collision component
  identity remains irrelevant. Fine curvature/gaps below spacing are sampled
  approximations. Partial floor coverage is reported; failure leaves OutData
  untouched. Successful commit saves FComposableCameraMeshAuthoredShape and fills
  TriangleShapeIds alongside triangle Layer GUIDs. Replacement removes only the
  matching Shape GUID's mesh. Empty ownership arrays are accepted for legacy source.
  Shapes and TriangleShapeIds live under WITH_EDITORONLY_DATA; baked runtime
  triangles and Layer indices continue through the existing storage/query contract.
  FProjectedShapeBuild separates bounded outline/subdivision preparation from
  resumable collision samples. A leaf can pause between any of its seven samples;
  successful and failed samples remain cached. BuildProjectedShape wraps this same
  builder synchronously for existing Shape edits and pure geometry consumers.
  New creation queues a small captured-plane outline fill immediately, then
  FEdMode::Tick advances only the head job once per GFrameCounter, capped at 256
  new queries / a soft 4 ms query-work budget. Collision remains on the game thread.
  Snapshot copy and final transaction serialization are additional bounded-event
  work, not covered by that query budget. Source/cache snapshots and GUID/enabled
  Layer flags go to Async(ThreadPool); no World, UObject or mode pointer goes to
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
- Brush-ring traces accept compatible floor hits across collision-component
  boundaries. Component identity is not surface identity; floor normal and
  bounded projection remain the geometric filters.
- Authoring and preview visualization clip saved triangles into a separate
  anchor-local cell cache. Normalize source winding to counterclockwise XY,
  clip against the four cell half-planes and interpolate XYZ at intersections.
  Each height bucket stores non-overlapping convex FResolvedSurfacePatch polygons.
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
  Cell center/normal/Layer fields summarize the height bucket for lookup/removal,
  not its entire visible ownership. Both FDynamicMeshBuilder and PIE mesh export
  iterate patches with variable vertex/triangle counts. Offset along document Z,
  rather than each patch normal, to keep adjacent XY edges coincident. No clipping
  or union work happens per render frame. Query/save structures remain untouched.
  VisualizationBoundary and VisualizationPartialOverlap cover boundary fidelity,
  sub-cell holes, both windings, runtime export and geometric Layer/alpha partition.
- Layer/existing-Shape changes invalidate the full visualization. New Shape
  creation computes regional coverage on snapshots and installs the ready cache.
  During brush
  strokes, UpdateAuthoringVisualization removes affected grid entries, repairs
  indices after dense-cell swap removal, and rasterizes only triangles/cells
  overlapping those entries. Existing same-surface tolerance and Layer ordering
  still resolve coverage at each height. A retained CellsByGrid map avoids
  recreating remote lookups; local/full equivalence compares per-Layer area and
  bidirectional interior probes within each height cell, with 1.e-3 cm^2 area
  and 1.e-3 cm height tolerances for FVector3f cut roundoff. Equivalent coverage
  need not retain the same tessellation. Cell size remains stable across release; desired growth above
  1.25x cached size can trigger an early regrid;
  full rebuild then restores the normal bounds-derived resolution. Invalid/empty
  source clears both cells and lookup. These allocations are editor authoring
  event work; runtime camera evaluation is unchanged.
  Initialize native FIntPoint dirty bounds explicitly: its default constructor
  leaves coordinates unspecified, and MSVC can report C4701 across the separate
  full/regional branches. Regional work overwrites both bounds before clamping;
  full rebuilds ignore them and retain distant coverage, including invalid-bound
  fallbacks checked by IncrementalVisualization.
  PaintAtHover spaces failed attempts too, updates revision immediately, refreshes
  the active viewport and defers global redraw to FinishStroke. Release
  retains the current visualization and controls, closes its transaction and
  redraws other viewports. Undo/cancellation and Layer/Shape edits still rebuild
  source-dependent caches; Save compacts unused source vertices. Read-only Preview
  builds one cache per storage actor and releases caches on exit.
  `IncrementalVisualization`, `EraseBroadPhase`
  and `StrokeVisualizationRefresh` cover equivalence, work bounds and lifecycle.
  EraseLocalVisualization checks cut-sized cell work and retained remote coverage;
  DisjointPreviewPatches checks bounded cache fragmentation. EraseBroadPhase also
  verifies reserved source buffers/vertices survive and no-op cuts preserve counts.
- PIE cannot use `FEdMode::Render`: that callback draws only Level Editor
  viewports and resolves the editor world. The Show command therefore builds
  the same resolved patch meshes into non-zero, per-storage BatchIDs on each PIE
  world's `WorldPersistent` `ULineBatchComponent`. One persistent submission
  per Layer avoids frame-over-frame alpha accumulation; `ClearBatch` removes
  only CCS-owned geometry. A ticker handles already-running PIE, multi-PIE
  worlds, streaming add/remove, and transform changes.
- Never register an ownerless editor-created `UPrimitiveComponent` into a PIE
  world and retain it through a global `TStrongObjectPtr`: `EndPlayMap` can
  release the world's `FScene` before a later ticker/GC pass drops that object.
  Use a world-owned renderer or tear it down on `PrePIEEnded`. Mesh preview
  does both: its batcher belongs to `UWorld`, `PrePIEEnded` clears all BatchIDs,
  and `PostPIEStarted` re-enables routing. Editor-module placement excludes
  Shipping.
- Closing the mode's primary tab routes back to `FEdMode::RequestDeletion`.
  Exit guards against close-callback re-entry and explicitly redraws Level
  viewports. Preview toggle first deactivates edit mode, then activates preview.
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
