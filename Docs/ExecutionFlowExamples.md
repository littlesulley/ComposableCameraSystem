# Execution Flow Examples

Updated: 2026-07-22

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
       -> ApplyModifiers
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
  -> click Layer row
       -> stable Layer GUID becomes current paint target
       -> selected Layer struct appears in Details
  -> Layer CRUD / projected brush paint / erase
       -> ring projection accepts compatible floor across component seams
       -> invalidate resolved visualization cache
       -> collapse repeat coverage to one surface cell
       -> first enabled Layer in top-to-bottom list order wins each visual cell
       -> disposable filled-color viewport overlay without alpha stacking
  -> Save
       -> create hidden storage actor if missing
       -> copy full authoring triangles with stable Layer GUIDs
       -> resolve GUIDs to runtime Layer indices
       -> save Level / external actor package

close Edit mode tab
  -> request edit-mode deletion
  -> optional dirty-data save prompt
  -> invalidate Level viewports
  -> no Layer overlay remains

Tools -> Show Mesh Camera Layers
  -> deactivate Edit mode if active
  -> activate read-only Preview mode
  -> build one resolved cache per loaded storage actor
  -> Level Editor viewport: Preview EdMode draws filled overlays
  -> each PIE world's persistent LineBatchComponent
       -> submit the same resolved Layer meshes once under unique BatchIDs
       -> streaming/transform ticker maintains per-actor cache
  -> PrePIEEnded / Show off / Edit mode / module unload clears PIE batches
       -> teardown routing stays disabled until next PostPIEStarted
```

Runtime:

```text
storage actor BeginPlay
  -> register with MeshWorldSubsystem

MeshWorldSubsystem.Tick
  -> each local PlayerController + CCS PCM
  -> pawn position downward query across loaded storage actors
  -> nearest surface; return every enabled Layer covering that surface
  -> diff current membership against per-player ActiveLayers
  -> each entered Layer duplicates its Modifier templates under PCM
  -> PCM.ReplaceModifiers(..., false)
       -> outer and inner candidates remain registered together
  -> each entered Camera Layer captures the currently active Director
  -> push unique temporary Context named from Mesh + Layer name + GUID
  -> activate that Layer's CameraType + Transition + ActivationParams there
       -> reference source = captured current Director
       -> construct camera from Type Asset
       -> resolve new effective Modifiers by camera tags
       -> apply typed exposed parameter/variable overrides
  -> Action / Patch reserved: no runtime calls yet
```

Layer Profile has no CameraType:

```text
duplicate modifier templates
  -> PCM.ReplaceModifiers(..., false)
  -> PCM.OnModifierChanged once
```

Nested Camera Layers:

```text
enter outer red Layer
  -> add red Modifiers
  -> push red temporary Context and activate red CameraType
enter inner yellow Layer
  -> keep red Layer active; add yellow Modifiers
  -> if yellow has CameraType, push yellow temporary Context
  -> otherwise keep red Context active
exit yellow
  -> remove only yellow Modifiers
  -> pop only yellow Context, if any
  -> resume original red camera instance
  -> refresh ModifierManager selection without rebuilding red
exit red
  -> remove red Modifiers
  -> pop red Context
  -> resume original gameplay camera instance

Camera construction failure
  -> immediately pop empty temporary Context
  -> leave gameplay camera unchanged
```

Starting inside works through the same first subsystem tick. Streamed storage
actors register and unregister with their owning Level lifecycle.

## 8. When To Add Examples

Add a new flow when a feature crosses at least two major systems, for example:

- context stack + evaluation tree.
- editor graph + runtime data block.
- Sequencer + gameplay runtime.
- shot solver + patch overlay.
- K2 node + parameter block.
