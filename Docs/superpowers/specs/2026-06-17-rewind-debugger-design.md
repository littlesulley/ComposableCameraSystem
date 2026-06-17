# CCS Rewind Debugger Design

Updated: 2026-06-17

Status: approved by user for written spec review. Pending implementation plan.

Target source:

- Project: `C:/Users/Sulley/Documents/Unreal Projects/UE5_6/UE5_6.uproject`
- Plugin: `C:/Users/Sulley/Documents/Unreal Projects/UE5_6/Plugins/ComposableCameraSystem`
- Runtime module: `Source/ComposableCameraSystem`
- Editor module: `Source/ComposableCameraSystemEditor`

## Purpose

Add Rewind Debugger support for CCS camera debugging.

The user workflow:

1. Select the controlled character in Rewind Debugger.
2. Start recording.
3. Play normally. Camera source may change between gameplay CCS, Level Sequence
   CCS, and native Unreal cameras.
4. Stop recording.
5. Scrub playback.
6. For every recorded frame, draw the exact historical rendered camera pose in
   3D space and draw that frame's CCS node gizmos when the active camera came
   from CCS.

The feature does not take over the editor viewport camera during playback. It
draws historical camera markers and gizmos into the debug scene.

## Reference Behavior

Epic's GameplayCameras plugin records camera-system evaluation data through a
runtime trace channel, consumes the trace with an editor analyzer/provider, and
draws playback debug data through a Rewind Debugger extension. Playback uses
serialized trace data. It does not read live evaluator objects.

CCS should follow that separation:

- recording writes immutable frame data.
- playback reads trace timelines.
- debug drawing never dereferences live camera, node, director, or Level
  Sequence evaluator objects.

## Selected Architecture

Use two trace streams:

```text
ActiveCamera Trace
= what the selected player/character actually rendered this frame

CCSEvaluation Trace
= how a CCS evaluator produced its pose and debug gizmos this frame
```

The Rewind track shown under the selected character is driven by ActiveCamera
Trace. CCS gizmos are attached by matching the active camera frame to a CCS
evaluation frame from the same recording frame.

This keeps the user model simple: select one character and see the camera
history for that character, even if the active camera source was a Level
Sequence camera actor for part of the recording.

## Scope

First implementation supports:

- single selected character / player.
- historical rendered camera pose and frustum.
- 3D CCS node gizmos.
- 3D CCS transition gizmos where available.
- CCS gameplay path through `AComposableCameraPlayerCameraManager`.
- CCS Level Sequence path through `UComposableCameraLevelSequenceComponent`.
- non-CCS active cameras as pose-only frames.

First implementation does not support:

- split-screen or multiple simultaneous selected characters.
- forcing the editor viewport to look through historical camera poses.
- replaying 2D screen overlays.
- reconstructing live camera objects.

## Runtime Recording Flow

Gameplay CCS path:

```text
AComposableCameraPlayerCameraManager::DoUpdateCamera
  -> ContextStack->Evaluate
  -> write CCSEvaluation Trace(source = CCS_PCM)
  -> FillCameraCache(DesiredView)
  -> write ActiveCamera Trace for owning player/character
```

Level Sequence CCS path:

```text
UComposableCameraLevelSequenceComponent::EvaluateOnce
  -> InternalCamera->TickCamera
  -> ApplySequencerPatchOverlays
  -> ProjectPoseToCineCamera
  -> write CCSEvaluation Trace(source = CCS_LevelSequence)

PlayerCameraManager
  -> reads the LS actor's UCineCameraComponent through normal UE view-target flow
  -> writes ActiveCamera Trace for owning player/character
```

Native camera path:

```text
PlayerCameraManager
  -> reads active native camera/view target
  -> writes ActiveCamera Trace(source = Native_Camera or Unknown)
```

ActiveCamera Trace is authoritative for the displayed historical camera pose.
CCSEvaluation Trace is authoritative for CCS node/transition gizmos.

## ActiveCamera Trace Data

One active frame records:

- trace time and frame/cycle id.
- world id.
- player/controller id.
- selected pawn/character object id when available.
- player camera manager object id.
- active view target actor id.
- active camera component id when available.
- source kind:
  - `CCS_PCM`
  - `CCS_LevelSequence`
  - `Native_Camera`
  - `Unknown`
- final rendered pose:
  - location.
  - rotation.
  - FOV.
  - projection mode and ortho fields.
  - aspect-ratio flags needed to draw the frustum.
- matched CCS evaluation key when the writer can compute it cheaply; otherwise
  the editor analyzer/provider performs the match from ids and frame timing.

The final rendered pose comes from the PCM camera cache after CCS and engine
view-target plumbing have resolved the actual view for the frame.

## CCSEvaluation Trace Data

One CCS evaluation frame records:

- trace time and frame/cycle id.
- world id.
- source kind:
  - `CCS_PCM`
  - `CCS_LevelSequence`
- source object id:
  - PCM object id for gameplay CCS.
  - Level Sequence actor/component id for LS CCS.
- owner pawn/player id when known.
- view target actor id when known.
- camera type asset path/name.
- context name for PCM frames.
- final CCS pose before projection/writeback.
- projection/writeback status:
  - projected to PCM cache.
  - projected to CineCamera.
  - skipped because framing failed.
  - skipped because output component was missing.
- serialized debug primitive stream.

The trace stores debug data as primitives, not live node references.

## Debug Primitive Stream

Introduce a serializable primitive model for current 3D debug drawing:

- line.
- point.
- sphere.
- solid sphere.
- box.
- camera frustum.

Text labels are outside the first implementation. They can be added later as a
new primitive kind if labels prove necessary during playback.

Each primitive stores the minimum draw data:

- transform or endpoints.
- color.
- size/radius/thickness.
- depth priority where needed.
- source label/category as compact names where useful.

Existing node and transition debug drawing should route through a shared debug
draw sink:

```text
Live sink     -> DrawDebug* / line batcher
Trace sink    -> append debug primitives
Rewind sink   -> draw serialized primitives during playback
```

This avoids duplicate "live gizmo" and "rewind gizmo" logic.

## Playback Flow

When Rewind Debugger playback is active:

1. The CCS Rewind extension gets the selected character/object from Rewind
   Debugger.
2. It queries ActiveCamera Trace for the current scrub time.
3. It draws the active frame's historical rendered camera pose/frustum.
4. If the active frame references or can match a CCSEvaluation frame, it reads
   that frame's primitive stream.
5. It draws the CCS primitives in world space.
6. If no matching CCS frame exists, it still draws the historical camera
   pose/frustum and omits CCS gizmos.

Playback must not call `TickCamera`, `Evaluate`, `ProjectPoseToCineCamera`, or
any live debug functions on active runtime objects.

## Matching Rules

Gameplay CCS:

```text
ActiveCamera(source = CCS_PCM, PCM id)
  -> match CCSEvaluation(source = CCS_PCM, same PCM id, same frame/cycle)
```

Level Sequence CCS:

```text
ActiveCamera(view target actor id = LS actor id)
  -> match CCSEvaluation(source = CCS_LevelSequence, same LS actor id, nearest same-frame event)
```

Fallback:

```text
ActiveCamera(source = Native_Camera or Unknown)
  -> no CCS match
  -> draw pose/frustum only
```

If several CCS evaluation frames match a single active frame, prefer the one
whose source actor/component equals the active view target. If still ambiguous,
prefer the latest evaluation event before the active camera trace event within
the same engine frame.

## Level Sequence Notes

Level Sequence evaluation does not run through CCS PCM evaluation. It evaluates
inside `UComposableCameraLevelSequenceComponent`, writes its result to a
`UCineCameraComponent`, and Unreal's normal view-target path later lets PCM
read that camera as `FMinimalViewInfo`.

Therefore:

- ActiveCamera Trace captures the exact rendered LS camera pose from PCM.
- CCSEvaluation Trace captures LS internal CCS pose and node gizmos from the LS
  component.
- Rewind playback combines them by LS actor/view-target identity.

If `ProjectPoseToCineCamera` skips projection, ActiveCamera Trace remains the
truth for the rendered pose. The CCSEvaluation frame records skipped projection
status so playback and diagnostics can explain mismatches without guessing.

## Editor Trace Pieces

Editor module pieces:

- `FComposableCameraTraceAnalyzer`
- `FComposableCameraTraceProvider`
- `FComposableCameraTraceModule`
- `FComposableCameraRewindDebuggerExtension`
- `FComposableCameraRewindDebuggerTrackCreator`
- track/view model for the selected character row if the Rewind UI needs a
  visible CCS camera child row

The extension toggles the CCS trace channel when Rewind recording starts and
stops, then draws playback frames through `UDebugDrawService`.

The track shown to the user should live under the selected character/pawn. It
represents the active camera history for that character, not a separate track
per LS actor.

## Runtime Module Pieces

Runtime module pieces:

- trace macro and channel guard, stripped from shipping/test builds.
- active-camera trace writer.
- CCS-evaluation trace writer.
- serializable debug primitive structs.
- debug draw sink interface.
- adapters from existing viewport debug helpers to live/trace sinks.

The runtime module will need trace logging support. Editor-only analyzer,
provider, and Rewind integration stay in the editor module.

## Performance Rules

Trace capture is disabled unless the CCS trace channel or debug cvar is enabled.

When disabled:

- no primitive arrays are built.
- no serialized archives are allocated.
- hot path pays only a cheap branch.

When enabled:

- primitive buffers are reused where practical.
- serialization happens once per recorded evaluation frame.
- labels use compact names; avoid per-frame formatted strings in hot paths.

This follows CCS hot-path rules and keeps normal gameplay evaluation unaffected.

## Error Handling

Playback should degrade gracefully:

- missing ActiveCamera frame: draw nothing for that scrub time.
- ActiveCamera frame without CCS match: draw pose/frustum only.
- corrupt primitive stream: skip gizmos for that frame and keep drawing pose.
- unknown source kind: draw pose/frustum only.
- LS projection skipped: draw active rendered pose and preserve the projection
  status for diagnostics.

Trace playback should never crash because a recorded object no longer exists.

## Testing

Automation or focused tests:

- primitive serialization round trip.
- active frame to CCS evaluation matching:
  - PCM match.
  - LS actor match.
  - no CCS match fallback.
- malformed primitive stream is ignored safely.

Manual IDE / Unreal verification:

- Start PIE.
- Select the controlled character in Rewind Debugger.
- Record gameplay with a normal CCS gameplay camera.
- Stop and scrub.
- Confirm historical camera frustum follows the recorded rendered pose.
- Confirm CCS node gizmos appear in the correct historical world positions.
- Record gameplay that triggers an LS camera.
- Stop and scrub through the LS range.
- Confirm the character track still shows the LS camera pose and LS node gizmos.
- Record a native non-CCS camera.
- Confirm pose/frustum appears and CCS gizmos are omitted.

Build and Unreal automation must be run from Rider, Visual Studio, or Unreal
Editor. Do not invoke UBT, Build.bat, RunUBT.bat, dotnet, msbuild, the editor,
or automation tests from shell in this project.

## Documentation Impact

Implementation must update:

- `Docs/DesignDoc.md` for the Rewind/trace architecture and Level Sequence
  matching model.
- `Docs/TechDoc.md` for trace primitives, hot-path gating, and draw-sink
  technique.
- `Docs/ExecutionFlowExamples.md` with gameplay and Level Sequence recording
  and playback examples if the implementation introduces reusable flow patterns.

## Implementation Order

Recommended implementation order:

1. Add trace data model and serialization tests.
2. Add editor analyzer/provider/module registration.
3. Add ActiveCamera Trace from PCM camera cache.
4. Add CCSEvaluation Trace for gameplay CCS.
5. Add debug primitive sink and convert current 3D gizmo helpers.
6. Add Rewind extension drawing for gameplay CCS.
7. Add CCSEvaluation Trace for Level Sequence CCS.
8. Add LS matching and playback.
9. Update docs.
10. Run IDE-side build and manual Rewind verification.
