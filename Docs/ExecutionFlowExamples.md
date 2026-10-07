# Execution Flow Examples

Updated: 2026-10-08

This file gives compact end-to-end flows. Keep examples current with source.

## 1. Gameplay Type Asset Activation

Intent: Blueprint activates a camera type asset in context `Gameplay`.

```text
BP K2 node / Blueprint library
  -> AComposableCameraPlayerCameraManager
  -> ContextStack.EnsureContext(Gameplay)
       -> create context or move existing context to top
  -> ResolveTransition
       -> override
       -> transition table
       -> source exit
       -> target enter
       -> hard cut
  -> Director.ActivateNewCamera
       -> SpawnActorDeferred(AComposableCameraCameraBase)
       -> Initialize(PCM)
       -> ConstructCameraFromTypeAsset
            -> copy camera tag container
            -> duplicate nodes
            -> build runtime data block
            -> apply parameter block
            -> bind delegates
            -> match modifier tag queries
            -> apply checked node overrides before node initialization
            -> initialize nodes and resolve input pins
       -> ApplyModifiers
            -> run Custom callbacks once against initialized pin values
            -> protect changed non-wired pin-backed fields
       -> FinishSpawning
  -> EvaluationTree.OnActivateNewCamera
       -> leaf if no old camera or no transition
       -> inner transition if blending
```

Next frame:

```text
PCM.UpdateCamera
  -> ContextStack.Evaluate
  -> active Director.Evaluate
  -> tree leaf ticks camera
  -> camera nodes run
       -> refresh unowned input pins; keep Custom-modified fields
  -> patches apply
  -> PCM modifiers apply
  -> pose projects to view
```

## 2. Push Cinematic Context, Then Pop

Intent: gameplay camera continues underneath cinematic blend.

Push:

```text
Activate cinematic type in context Cutscene
  -> EnsureContext(Cutscene)
       -> Cutscene becomes top
  -> source director = previous top Gameplay
  -> Director.ActivateNewCameraWithReferenceSource
       -> new Cutscene camera leaf
       -> reference leaf captures Gameplay tree root snapshot
       -> inner transition blends RefLeaf -> Cutscene leaf
```

During blend:

```text
ContextStack evaluates Cutscene only
  -> Cutscene tree evaluates RefLeaf
       -> captured Gameplay subtree ticks directly
  -> Cutscene target camera ticks
  -> transition blends source/target poses
```

Pop:

```text
PopActiveContext(Cutscene)
  -> remove Cutscene from live entries
  -> Gameplay is top again
  -> ResumeCurrentCameraWithReferenceSource
       -> keep existing Gameplay running camera and tree
       -> RefLeaf captures popped Cutscene tree snapshot
       -> pop transition blends Cutscene snapshot -> Gameplay tree
  -> Cutscene entry moves to PendingDestroyEntries
  -> transition finish destroys popped director/cameras
```

Key invariant: resumed Gameplay camera is not respawned.

## 3. Runtime Camera Patch

Intent: Blueprint adds a temporary FOV/offset/pose overlay.

```text
BP AddCameraPatch
  -> active context director
  -> PatchManager.AddPatch
       -> resolve override params + asset defaults
       -> spawn transient evaluator camera
       -> Initialize(nullptr)
       -> ConstructCameraFromTypeAsset(patch asset)
       -> sorted insert by LayerIndex
       -> return handle
```

Each frame:

```text
Director.Evaluate
  -> EvaluationTree pose
  -> PatchManager.Apply
       -> for each patch in layer order:
            -> advance enter/active/exit envelope
            -> check Duration / Condition / camera-change expiration
            -> evaluator.TickWithInputPose(upstream pose)
            -> upstream.BlendBy(evaluated pose, alpha)
       -> sweep expired patches
```

Manual expiry:

```text
BP ExpireCameraPatch(handle)
  -> patch flips to Exiting
  -> normal Apply pass fades out
  -> expired patch destroys evaluator
```

## 4. Sequencer Shot Section

Intent: Level Sequence drives a composition shot.

```text
Sequencer evaluates shot section
  -> ShotTrackInstance receives active sections
  -> each section builds effective shot
       -> InlineShot
       -> or ShotOverrides from asset reference
       -> apply target actor binding overrides
  -> entries sorted by row
  -> overlap alpha calculated
  -> LSComponent.SetSequencerShotOverride
```

Component tick:

```text
LSComponent.TickComponent
  -> EnsureInternalCamera
  -> rebuild/apply type asset parameter and variable bags
  -> ApplyActiveSequencerShotOverride
       -> find first CompositionFramingNode
       -> push primary shot
       -> push secondary shot + incoming transition if overlap
  -> InternalCamera.TickCamera
       -> CompositionFramingNode runs shot solver
  -> Sequencer patch overlays
  -> ProjectPoseToCineCamera
```

First section frame can arrive after component tick. The component invalidates
the internal camera tick cache and re-evaluates at zero delta when a new shot
entry first appears.

## 5. Sequencer Patch Section

Intent: timeline applies a patch overlay to an LS actor.

```text
Patch section in range
  -> PatchTrackInstance samples parameter channels/bags
  -> computes section envelope alpha
  -> LSComponent.SetSequencerPatchOverlay
       -> spawn evaluator if needed
       -> Initialize(nullptr)
       -> ConstructCameraFromTypeAsset(patch asset)
       -> store latest parameter block + alpha
```

Then:

```text
LSComponent.ApplySequencerPatchOverlays
  -> sort live overlays by effective layer
  -> apply latest params to evaluator runtime data block
  -> evaluator.TickWithInputPose(current pose)
  -> current pose BlendBy evaluator output
  -> write patched FOV to CineCamera when needed
```

Sequencer patch overlays are component-local. They do not use the gameplay
director's patch manager.

## 6. Type Asset Editor Save/Load

Save/build:

```text
Graph edit
  -> SyncToTypeAsset
       -> node templates
       -> pin overrides
       -> connections
       -> execution order
       -> full exec chains
       -> variable nodes
       -> editor positions
  -> asset saved
```

Open/rebuild:

```text
Asset load/open
  -> RebuildFromTypeAsset
       -> transient editor graph
       -> stable node GUIDs
       -> pins
       -> wires
       -> positions
```

Rule: add new persisted editor state only with both directions implemented.

## 7. Mesh Layer Authoring and Runtime Application

Authoring:

```text
Tools -> Edit Mesh Camera Layers
  -> current Level selected as document scope
  -> load hidden storage actor into transient working document
  -> StartResident captures original source + GUID/enabled flags on editor thread
       -> native worker builds authoring index -> early index/grid-size delivery
       -> saved version-1 preview: validate/load exact polygons; skip spatial clipping
       -> legacy/invalid cache: resolve complete exact coverage with original triangle order
       -> prepare whole shared native document, including bounds
       -> native canceled jobs are freed off the editor thread
       -> stationary Ticks install early index, then one whole-scene document update
       -> no spatial view ordering or per-frame Tile publication for initial Edit load
       -> preserve opening-edited regions; remove untouched obsolete regions in one pass
       -> consume complete base cache; keep incrementally edited early index
       -> panel focus and Brush/Erase retain loading
       -> Undo/Discard/structural replacement/exit cancel base and local feedback together
       -> Save consumes resident coverage before compaction; remaining legacy snapshots cancel
       -> Render never resolves or uploads the full document; drafts/controls remain interactive
  -> click Layer row
       -> stable Layer GUID becomes current paint target
       -> selected Layer struct appears in Details
  -> below Layer properties: one horizontal Draw / Select / Erase button row
  -> Draw: Drawing Type menu = Brush / Rectangle / Circle / Polygon
       -> restore the last drawing type when returning from Select/Erase
       -> show only its brush/shape/surface options below the buttons
       -> Brush: paint / temporary Shift erase
       -> Rectangle/Circle: drag -> immediate planar fill + budgeted temporary projection -> release
       -> Polygon: click vertices -> preview -> Enter/close/double-click
       -> optional document XY grid snap + live dimensions
       -> Esc / Backspace / tool or Layer change / focus loss manages draft
  -> shape confirmation
       -> pick/project through owning Layer Channel (default Visibility)
       -> Layer edits cancel pending fills; Channel changes affect later authoring
       -> validate simple outline -> triangulate -> bounded subdivision
       -> reject excessive density before tracing
       -> show small captured-plane filled preview immediately
       -> project vertices/midpoints/centroids in resumable per-frame batches
       -> compare interior hits with corner interpolation; refine curvature/support
       -> emit curved leaves using shared edge samples and projected centroids
       -> compute resolved coverage from source/cache snapshots on a worker
       -> if source revision changed, rebase coverage without repeating projection
       -> retain Shape GUID + controls + local projection settings
       -> install successful source + ready coverage in one creation transaction
       -> report omitted floor samples; failed commits preserve existing data
       -> tool/focus changes preserve released regions; explicit cancel/Undo/Layer
          structural changes/exit discard pending fills without waiting on workers
  -> Select: ray-pick active Layer Shape -> drag interior / control or edit Details
       -> show edit snapping and Shape Details/delete; hide Draw menu/Erase options
       -> Interactive temporary fill; source/history unchanged -> final budgeted replacement -> replay erasures
       -> replace matching Shape geometry, preserving identity
  -> Erase: circular-prism subtraction on active Layer triangles
       -> show radius/depth options; hide Draw menu and selected Shape panel
       -> reject distant triangles in brush coordinates before clipping
       -> retain fragment heights and ownership -> remember cuts on affected Shapes
  -> drag a numeric option in Draw / Select / Erase
       -> PropertyEditor opens slider transaction
       -> interactive values update without rebuilding Details
       -> any queued Details refresh waits while transaction is active
       -> release/final commit closes transaction -> refresh once
       -> source Undo/Redo remains available after subsequent authoring operations
  -> Delete: cancel queued replacements for selected Shape identity
       -> remove its source and owned triangles
       -> fused regional coverage/publication removes fill; retain remote geometry
  -> Layer CRUD / projected brush paint
       -> hover and interior projection use the same owning Layer Channel
       -> retain tangent-plane outline; trace document-Up across component seams
       -> refine curved brush interior; cap stamp work and retain relative-world precision
       -> mouse callbacks queue each spacing-qualified attempt with captured options/Shift state
       -> process all accepted attempts FIFO, including no-ops; do not replace with the latest cursor
       -> once per editor frame: share soft 4 ms across projection/cut/ready publication
       -> prewarm disposable 128-triangle block bounds after source invalidation
       -> Brush appends only new stamp triangles into retained resolved coverage
       -> Erase rejects remote blocks; empty indexed candidates skip the extra native rollback copy
       -> Erase visits candidates in original descending order; safely contained triangles skip plane splitting
       -> near-boundary triangles retain the exact 32-gon/depth subtraction and height interpolation
       -> Erase replaces cut triangles in place; dirty cells follow the removed footprint
       -> refresh removal/swap-tail/fragment block bounds
       -> capture new Brush tail or whole-cell Erase candidates; never snapshot a partial mutation
       -> filter coarse regional candidates by enabled GUID and triangle XY bounds before snapshot allocation
       -> while opening: wait for early native index, including empty source; keep base job alive
       -> queue primary exact FIFO deltas to await base, never full-rebuild merely because loading is dirty
       -> independent fixed-grid feedback resolves current complete touched regions, including old neighbors/Erase holes
       -> publish feedback immediately; mark touched regions so late base cannot overwrite them
       -> base complete: stop feedback job, retain visible buffers, resume exact primary FIFO from base
       -> transfer the retained coverage cache to one native worker; no whole-cache clone
       -> process immutable mixed/Erase inputs in original order; merge adjacent append-only work
       -> prepared jobs own at most eight waiting inputs; keep the remaining queue intact
       -> start/publish completed coverage inputs while the next Erase source task is partial
       -> coverage never snapshots or reads that partial live source; checkpoint capture still waits
       -> resolve whole dirty cells on the worker, retaining remote cells and per-mutation grid policy
       -> keep original sampling, height bands, Layer priority and clipping precision
       -> yield between scene queries/original cut triangles; background coverage checks cancellation between work batches
       -> Render retains the last complete fill while source/coverage is in progress
       -> release continues queued work, then retains cache/controls, ends transaction and redraws
       -> second press while source pending queues separate deferred stroke with its own Undo
       -> coverage/publication progresses independently during the held stroke and after release
       -> coverage worker prepares touched tiles directly; no second worker gap
       -> assemble each region at its last input in the owned batch; publish completed prefixes through long backlogs
       -> full/regrid batch publishes its latest complete document; exact region keys avoid distant gaps
       -> workers prepare shared vertex/index buffers and bounds; identical content retains existing GPU geometry
       -> Save/tool/focus/close boundaries flush accepted stamps before continuing
       -> pre-edit callbacks flush before Layer arrays/options change or Layer transactions begin
       -> reordering/deleting/toggling never moves indices under pending coverage
       -> stroke and subsequent Layer change retain separate Undo entries
       -> Esc/right mouse/Discard drop tasks and restore the whole stroke checkpoint
       -> Ctrl+Z drops latest deferred stroke/pending Shape or cancels live draft/unfinished stroke
       -> completed edits use engine Undo
       -> Ctrl+Y preserves source-only boundary completion
       -> close stroke transaction -> engine Undo/Redo -> cancel old native generation
       -> Undo/Redo/Discard/cancel first try exact editable history for restored DocumentRevision
       -> complete checkpoints share immutable tile buffers/colors and own exact coverage/index
       -> record by moving polygons and copying the small native block index, no whole coverage copy on the editor thread
       -> compare tile presence/buffer/color -> queue changed tiles immediately under the existing publication budget
       -> empty remembered source clears old tiles in that pass; unchanged remote buffers stay intact
       -> restore editable coverage/index immediately; a complete checkpoint needs no document rebuild
       -> next Brush/Erase retains pending restore tiles and uses the restored base
       -> one native worker copy of resolved coverage -> existing append/regional edits; history stays immutable
       -> changed brush tiles supersede their old restore tiles and publish ahead of remaining restoration
       -> untouched distant restore tiles/removals remain queued; no partial-tile replacement or duplicate full upload
       -> display-only checkpoint fallback rebuilds index/coverage without republishing meshes
       -> history miss/eviction falls back to the opening-Edit progressive rebuild flow
       -> keep saved checkpoint/current revision; cap other history to 32 revisions and soft 128 MiB retained buffers/coverage/index
       -> cancel obsolete streams/jobs; retired native buffers/history free off-thread
       -> structural Layer changes invalidate; Name/Profile/Channel reuse geometry; Color updates material
       -> new Shape creation installs its already computed coverage cache
       -> clip source footprints to grid cells; retain actual boundary polygons
       -> index all elevations in each XY cell
       -> clip pairwise overlap by actual plane-height band, independent of cell center
       -> first enabled Layer owns each covered point; lower rows fill uncovered parts
       -> collapse fully covered coplanar single-Layer cells to quads
       -> persistent Edit fill grouped by Layer and 32 x 32 coverage-cell tiles
       -> mutation uploads vertices/indices; ordinary viewport frames reuse buffers
       -> coverage worker directly prepares complete touched tiles, including empty removal
       -> coalesce display versions without dropping FIFO source samples
       -> ready regions supersede older assembly; retain remote restoration/buffers
       -> same-capacity topology/position updates reuse GPU buffers; growth recreates
       -> color updates material parameters without geometry replacement
       -> document-wide changes / grid resize invalidate all tiles
       -> ready worker-installed Shape coverage also invalidates its GPU fill
       -> preserve float color/alpha, fan geometry, offset and disabled backface culling
       -> PDI draws only drafts, outlines and controls (fill fallback on unavailable publication)
  -> Ctrl+Z / Ctrl+Y
       -> restore transactional document (one whole stroke / committed edit)
       -> final Undo client refreshes Layer/Shape Details and viewport caches
       -> reset source block index, including restoration with unchanged counts
       -> compare document revision with last successful Save checkpoint
  -> Save
       -> finish accepted interaction; complete resident opening / exact FIFO coverage
       -> create hidden storage actor if missing
       -> compact unused/orphaned source through shared vertex-ID remapping
       -> keep no-op source/index; invalidate index only if geometry changes
       -> copy authoring triangles, Shape controls/ownership and retained erasures
       -> unchanged geometry/GUID rows: reuse runtime triangles/BVH
       -> otherwise remap shared vertices, resolve runtime Layer rows and select BVH medians
       -> unchanged geometry/rows/enabled policy: compare exact cache without allocation, retain matching stored preview
       -> otherwise serialize current exact coverage; cache miss resolves once and retains it
       -> editor coverage remains stripped from cooked packages
       -> save Level / external actor package
       -> on success, replace independent saved-document checkpoint
       -> log finalize/runtime/preview/packages/checkpoint milliseconds
  -> Discard (right of Save)
       -> revert unfinished stroke and cancel draft / queued creation jobs
       -> restore latest successful Save (opening document if never saved)
       -> restore failed-save actor data / remove failed-attempt-created actor
       -> clear Shape selection and refresh Details / viewport caches
       -> one Undo restores committed discarded edits; Redo discards again
       -> no package Save and no editor-wide history reset

close Edit mode tab
  -> request edit-mode deletion
  -> discard uncommitted shape draft
  -> destroy transient Level-owned fill actor/components; engine releases render buffers
  -> optional dirty-data save prompt
  -> invalidate Level viewports
  -> notify preview coordinator that actual Edit Exit has finished
  -> Show requested: next preview ticker restores read-only mode from saved data
  -> Show off: no Layer overlay remains

Show already on -> open Edit (menu or mode selector)
  -> retain checked Show intent
  -> suspend read-only editor/PIE fill and release its caches
  -> Edit alone displays its transient working document
  -> close Edit: wait for actual deferred Exit and save/discard handling
  -> next preview ticker restores read-only mode, using saved coverage or legacy progressive fallback
  -> explicitly turning Show off during Edit cancels this restoration and keeps Edit open

Tools -> Show Mesh Camera Layers
  -> deactivate Edit mode if active
  -> record Show-on intent; activate read-only Preview mode after actual Edit Exit
  -> snapshot triangle arrays/Layer metadata per loaded storage actor
  -> worker loads saved coverage in one batch without spatial resolution
  -> legacy/invalid cache: worker clips/resolves/exports; no World or UObject access
       -> compute global bounds/cell size; bin triangles into 32x32-cell tiles
       -> resolve tiles near captured view first; no distance exclusion
       -> try an 8x8-cell final startup region; exclude it from the remaining regular tile
       -> resolve all competing Layers per tile, then enqueue final geometry immediately
       -> game thread adopts batches while remaining tiles are still resolving
       -> terminal marker follows all queued batches, with total triangle accounting
       -> exact snapshots reuse bounded local-geometry cache across Show/editor/PIE
       -> cached tiles copy/publish individually in the new view's order
       -> same-count source/Layer changes miss cache; fitted World vertices never enter it
  -> Level Editor: worker prepares bounded native meshes
       -> one-sided editor material: disconnected reverse faces at identical heights/XY
       -> <=1024 total triangles per chunk including both windings; no duplicate for two-sided material
       -> core ticker / Preview EdMode Tick share one AdvancePreview per engine frame
       -> stationary/throttled views do not gate ready results; never wait
       -> soft 2 ms checked between registrations, <=16 chunks/frame safety cap
       -> source-Level temporary Actor uses non-selectable persistent GeomMaterial meshes
       -> visible in normal editor and Game View (G)
       -> RF_DuplicateTransient + bIgnoreInPIE prevent editor copies entering PIE
       -> invalidate static views on publication and two following frames, then stop
       -> while PlayWorld exists: cancel unfinished editor builds, retain displayed components
       -> on return: restart interrupted documents; completed caches survive
       -> PIE-ending cannot block editor resumption after PlayWorld clears
  -> PIE: worker exports each tile mesh and reserves that batch's projection-cache capacity
  -> queue disposable PIE floor fitting per storage document
       -> reuse source-XYZ samples within each batch; retain XY boundaries and source data
       -> fit/publish current batch before adopting its successor, without waiting for the document future
       -> all-object hits -> nearest eligible upward-facing floor
         -> Visibility-blocking collision or rendered opaque/masked StaticMesh
         -> include WorldDynamic/PhysicsBody; reject non-rendered volumes and Pawns
         -> clear highest rendered StaticMesh within 10 world cm above that fixed floor
         -> preserve distinct storeys; no chained lifts or authored-data changes
       -> all PIE worlds share soft 4 ms fitting / 2048-query safety cap per tick
       -> discovery/upload cannot spend that fitting budget
       -> discover/prune all documents, then resume round-robin fitting
         -> <=64 queries per document visit; next tick starts at unserved job
       -> publish fitted chunks before whole-document completion
         -> first document chunk <=64 triangles, later chunks <=1024 across tile batches
         -> soft 2 ms / 16-chunk safety cap globally, one per round-robin visit
         -> separate saved publication position prevents document starvation
         -> source PIE Level owns construction Actor and persistent components
         -> depth-tested, two-sided fill; opaque characters occlude it
       -> fitting completes; continue bounded publication until all tails appear
         -> keep existing chunks; no whole-document consolidation upload
         -> initialize persistent mesh buffers once; ordinary drawing reuses them
         -> streaming ticker maintains weak per-storage cache
         -> transform changes move Actor without rebuilding geometry
  -> after any editor/PIE preview chunk is published, weak-register its Actor for view routing
       -> ordinary view families in that world use LandscapeLODOverride=0 by default
       -> near/far terrain stays at the same LOD; cached mesh height/depth policy stays unchanged
       -> captures/unrelated worlds opt out; no global CVar or Landscape asset mutation
       -> Show off / last Actor removal / PIE ending leaves later view-family LOD untouched
       -> CCS.Editor.MeshLayers.StabilizeLandscapeLOD=0 opts out while keeping Show on
  -> CCS.MeshLayers.DebugNextQuery in the PIE game console on request
       -> arm one weak world-scoped request; do not query or read a Pawn
       -> next actual business Query/Update reports caller policy/origin and failure reason
       -> NativeRay reports original-segment saved intersection and signed blocker separation
       -> compare actual blocking surface with nearby enabled native source heights
       -> report loaded-but-unregistered documents without repairing their registration
       -> consume once; later calls resume silent operation; Show is not required
  -> CCS.Editor.MeshLayers.DumpPIEPreview on request
       -> log pending state, triangle counts and floor-fitting statistics
       -> log geometry reuse and elapsed worker preparation/adoption wait
       -> if a player Pawn exists, compare source/submitted/floor heights at its feet
         -> complex/simple hits, Actor/component identities and StaticMesh candidates
         -> submitted triangle intersections distinguish absent from buried coverage
  -> PrePIEEnded / Show off / Edit mode / module unload
       -> cancel background geometry/fitting and destroy preview Actors; toggles never wait
       -> module unload drains cancelled jobs and joins its owned pool before unloading code
       -> module unload also drains in-flight render families before releasing the view extension
       -> unregister owned primitives before PIE Scene release
       -> teardown routing stays disabled until next PostPIEStarted
```

Runtime:

```text
storage actor BeginPlay
  -> prepare local-space BVH if load did not already build it
  -> register with MeshWorldSubsystem
  -> preload each enabled Layer's selected Profile family asynchronously

business obtains its current supporting ground
  -> ordinary Character Walking: CurrentFloor.HitResult, after IsWalkableFloor
  -> custom movement/NavWalking: business-selected valid ground FHitResult
  -> GroundQueryParams: SurfaceTolerance only (default 5 cm)
business -> MeshWorldSubsystem.QueryMeshLayer / QueryMeshLayers
  -> validate blocking nonpenetrating ground and live same-world Component
  -> use ImpactPoint XY, not capsule center Location
  -> exact saved-triangle/BVH query within ground Z +/- tolerance
  -> validate each Layer's own intersection; never search outside the ground band
  -> report saved triangle SurfacePosition and absolute ground height separation
  -> return data only; no effects or scene traces

business -> MeshWorldSubsystem.UpdateMeshLayers(explicit local PC, GroundHit, GroundQueryParams)
  -> same ground matching; invalid/missed ground exits old membership
  -> diff current membership against per-player ActiveLayers
  -> ready new Layers enter bottom-to-top; pending lower Layers delay higher entries
  -> recheck membership on each business call; leaving cancels pending entry
  -> each ready entered Layer dispatches its Profile.Type exactly once
       -> CameraType: capture currently active Director
            -> push unique temporary Context from Mesh + Layer name + GUID
            -> activate CameraType + Transition + supported Activation fields
            -> reference source = captured current Director
            -> construct Camera, resolve active Modifier candidates by tags
            -> apply exposed parameter/variable overrides
       -> Modifier: duplicate templates under PCM
            -> PCM.ReplaceModifiers(..., true) once
       -> Action: reflect K2-exposable template properties
            -> parse overrides and resolve per-player Actor/Delegate sources
            -> PCM.AddCameraActionFromAsset; record exact instance
       -> Patch: parse exposed parameter/variable overrides
            -> active Director.PatchManager.AddPatch; record manager + handle
  -> unchanged membership: no reactivation, loads, or retrigger
  -> pending legacy mixed Profile: warn, skip until Type confirmed
```

Layer exit:

```text
CameraType -> pop only owned temporary Context; resume lower Camera
Modifier -> remove only owned duplicates; refresh active selection
Action -> remove only recorded instance, if still registered
Patch -> original manager.ExpirePatch(recorded handle); retain exit envelope
Natural Action/Patch expiry -> no retrigger while Layer remains active
business stops integration -> ClearMeshLayers(explicit PC); no polling resumes it
PC/PCM EndPlay / document unregister -> release owned scopes without new entries
Last storage using a Profile unregisters -> cancel/release its preload resources
```

Nested Camera Layers:

```text
enter outer red Layer
  -> push red temporary Context and activate red CameraType
enter inner yellow Layer
  -> keep red Layer active
  -> yellow CameraType Profile pushes yellow temporary Context
exit yellow
  -> pop only yellow Context
  -> resume original red camera instance
  -> refresh ModifierManager selection without rebuilding red
exit red
  -> pop red Context
  -> resume original gameplay camera instance

Camera construction failure
  -> immediately pop empty temporary Context
  -> leave gameplay camera unchanged

Independent Action/Patch Layers overlapping red
  -> trigger on entry against current PCM/Director
  -> do not push Camera Contexts
  -> exit removes exact owned effects; other same-class/asset effects survive
```

Starting inside works on the first explicit Update. The subsystem has no Tick.
Streamed storage registers/unregisters with its Level lifecycle. An upper GroundHit
cannot match a lower painted floor outside SurfaceTolerance, even if the upper
floor has no Layer. Each Layer must intersect the short segment at ground XY.
No SurfaceId is added and saved geometry does not change.

Example business-side C++ integration, after movement, for ordinary Character Walking:

```cpp
// GroundQueryParams is configured once and reused.
MeshGroundQueryParams.SurfaceTolerance = 5.0;

if (Movement->MovementMode == MOVE_Walking && Movement->CurrentFloor.IsWalkableFloor())
{
    MeshSubsystem->UpdateMeshLayers(LocalPlayerController,
        Movement->CurrentFloor.HitResult, MeshGroundQueryParams);
}
else
{
    // This business policy exits on jumping/losing ground.
    MeshSubsystem->ClearMeshLayers(LocalPlayerController);
}

// Also Clear on disable/unpossess when this integration stops making calls.
```

QueryMeshLayers uses the same GroundHit/params and only returns Layer data.
GroundHit.ImpactPoint is the sample location. The caller supplies current contact,
walkability and any custom movement/airborne behavior. A business scene Trace/Sweep
can use its own Channel/Profile, distance, complex collision and ignored Actors;
those are not fields of the Layer query. The plugin adds no implicit scene trace.

Blueprint: after movement, inspect CurrentFloor/WalkableFloor, pass its HitResult
to Update Mesh Layers with local PlayerController and Make Mesh Ground Query Params.
Clear on loss of ground or integration shutdown according to the business policy.
Old WorldPosition and collision-policy pins/Make struct nodes need refreshing or
recreation after a full IDE build and editor restart. SurfaceTolerance bounds height
matching; large values can merge nearby floors. Existing documents and Layer
authoring Channel remain compatible and receive no per-triangle fields.

## 8. In-Place Modifier Value Transition

```text
PCM.AddModifier / RemoveModifier
  -> ModifierManager scans candidates bucketed by exact Node Class
  -> Node Type branch elects one winner per checked Property
       -> overlapping Property: Priority, then registration order
       -> disjoint Properties: different Modifier assets may coexist
  -> Custom branch keeps one legacy whole-node winner
  -> diff old/new winners by (exact Node Class, Property)
  -> if any changed asset uses ReactivateCamera
       -> existing ReactivateCurrentCamera path
       -> Evaluation Tree receives the normal pose transition
  -> otherwise
       -> RunningCamera.ReconcileInPlaceEffectiveModifiersFromAssets
       -> scan every matching runtime node once
       -> resolve previous vs. desired winner for every Property
            -> new owner: desired asset Enter Value Transition
            -> changed owner: desired asset Replace Value Transition
                 -> null Replace preserves legacy priority selection
                      -> desired priority >= previous: desired Enter
                      -> desired priority < previous: previous Exit
            -> removed owner: previous asset Exit Value Transition
            -> no owner: preserve an already-running Exit unchanged
       -> cache property/pin bindings and live-node baselines

next Camera.TickCamera
  -> existing per-frame memoization guard
  -> advance cached Modifier value clocks once
  -> before each affected node tick
       -> read current lower wire/K2/default value when needed
       -> blend or step the checked property
       -> ResolveAllInputPins skips the owned field
       -> node evaluates normally into a new live pose

exit completes
  -> restore opted-in non-pin baseline, or read current lower pin value
  -> unregister in-place property ownership
  -> normal pin resolver owns the field again
```

No camera is spawned, destroyed, activated, or added to the Evaluation Tree on
the all-in-place branch. New camera construction still applies final effective
values before node initialization and starts with no value-transition history.

## 9. Action Condition During a Camera Blend

```text
PCM.UpdateActions
  -> advance Instant / Duration / Manual once using DeltaTime
  -> do not test pose-dependent Condition against prior blended output
ContextStack.Evaluate
  -> target Camera.TickCamera
       -> node chain produces a camera-local pose
       -> PostCamera MoveTo checks Condition against that local pose
       -> if still short of target, MoveTo changes it
  -> source camera may also tick through a transition reference leaf
       -> its hook cannot expire a persistent Action for the running target camera
       -> a current-camera-only Action bound to this source checks its own hook
  -> transition blends source and target poses
  -> PCM stores the final blended pose for rendering/debug
```

If source is at X=-50 and target remains at X=+50, a 0.5 blend renders X=0.
MoveTo targeting X=0 remains active because its target camera has not arrived.

## 10. Add a Parameterized Action

```text
Action Type Asset
  -> Action template stores Blueprint logic and authored defaults
K2 Add Camera Action
  -> connected TargetActor pin writes Actor into ParameterBlock
  -> AddActionFromAsset duplicates template, applies TargetActor, registers Action
Camera Tick
  -> CanExecute / OnExecute read instance TargetActor and local pose
```

Another call site can pass a different Actor to the same asset. Each Action
instance keeps its own value and execution state; the asset remains unchanged.

## 11. PIE Camera Trial To Asset Default

```text
Debug picker -> Live Edit toolbar (or global Live Editing page)
  -> select PIE world/camera -> snapshot connected Start execution order
  -> display read-only horizontal Node Chain: Start -> nodes -> Output
  -> select node -> lower Runtime Parameters section shows its native Details
  -> switching nodes retains prior trials and pending defaults
Native Details edit (scalar / array / struct / owned subobject)
  -> route event to root authoring parameter
  -> update separate per-node trial storage -> refresh that node's caches
Next normal node evaluation
  -> normal Modifier/wire/input resolution continues
  -> exchange trial values in -> FirstTick/Tick -> exchange lower values back
  -> preserve camera/transition instances and same-frame DAG cache
Apply to Asset
  -> create isolated candidates -> remap edited roots only
  -> preflight source changes, authoring references, wired/exposed pin survival
       -> any failure retains source and trial unchanged
  -> copy edited fields/owned objects into original graph templates
  -> update direct/compound defaults -> prune obsolete undriven compound overrides
  -> reconstruct pins under sync guard -> SyncToTypeAsset once
  -> normal Save; live trial remains active
Reset Trial
  -> remove trial bindings -> rebuild node caches from current drivers
  -> panel shows current driver values -> rebase source conflict snapshots
Undo/Redo
  -> restore source asset + graph state -> rebuild visible pin defaults/links
  -> graph notifications cannot sync while GIsTransacting
```

BeginPlay and disconnected/data-only nodes are excluded. Editing a cached node
may restart that node's temporal state; it does not recreate the camera.
Apply stores authoring defaults, preserving wires, caller overrides and Modifier
configuration. Pending-save state and active-trial state are independent.
PrePIEEnded detaches runtime targets; typed pending defaults remain until Apply,
Reset or window close. PIE-only actor references cannot be saved as defaults.

## 12. When To Add Examples

Add a new flow when a feature crosses at least two major systems, for example:

- context stack + evaluation tree.
- editor graph + runtime data block.
- Sequencer + gameplay runtime.
- shot solver + patch overlay.
- K2 node + parameter block.
