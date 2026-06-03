# Runtime Previewer Window Design

Updated: 2026-06-03

Status: approved for implementation planning.

Target source:

- Project: `C:/Users/Sulley/Documents/Unreal Projects/UE5_6/UE5_6.uproject`
- Plugin: `C:/Users/Sulley/Documents/Unreal Projects/UE5_6/Plugins/ComposableCameraSystem`
- Editor module: `Source/ComposableCameraSystemEditor`

## Purpose

Add a Runtime Previewer dock tab to the Camera Type Asset editor. During PIE,
the previewer shows the controlled pawn fixed at preview origin while preserving
its current pose and facing, and shows the game camera's live position relative
to that pawn.

The window is for debugging camera-to-character spatial relationships. It does
not move the game pawn, does not drive the game camera, and does not alter the
runtime evaluation result.

## User Entry

The tab is registered by `FComposableCameraTypeAssetEditorToolkit`.

Entry path:

```text
Camera Type Asset Editor -> Window -> Runtime Previewer
```

The tab is not part of the default layout. Users open and close it through the
Window menu like the existing Node Graph, Details, and Build Messages tabs.

## Selected Visual Direction

Use a real 3D preview viewport.

Preview scene contents:

- controlled pawn at the preview origin.
- copied skeletal pose from the live PIE pawn when available.
- fallback capsule / direction marker when skeletal pose cannot be copied.
- live game camera marker at visible-subject-local position.
- live game camera frustum and forward vector.
- movement / velocity direction arrow.
- small textual overlay for bound camera, pawn, relative camera location, and
  FOV.

The user can drag inside the previewer to move an observer camera. This observer
camera belongs only to the editor preview viewport. It never changes the game
camera or the PIE pawn.

First implementation should support orbit / pan / dolly through
`FEditorViewportClient` defaults and a Frame Pawn command. Top / side view
buttons can be added after the first working viewport.

## Architecture

New editor-side pieces:

- `SComposableCameraRuntimePreviewer`
- `FComposableCameraRuntimePreviewerViewportClient`
- `RuntimePreviewerTabId`

The toolkit owns the dock tab registration and passes the currently debugged
camera to the previewer. The previewer owns an `FAdvancedPreviewScene`, preview
actors/components, and the editor viewport client.

The existing debug picker remains the source of the selected runtime camera.
The Runtime Previewer should not create a separate instance picker in the first
version. If no camera is bound, the previewer shows an empty state.

## Data Flow

The existing toolkit debug path already polls:

```text
DebuggedCamera -> SnapshotDebugState()
```

The previewer extends the same per-editor-tick flow:

```text
DebuggedCamera
  -> SnapshotDebugState().FinalPose
  -> GetOwningPlayerCameraManager()
  -> GetOwningPlayerController()
  -> GetPawn()
```

Each preview update:

1. Resolve the live pawn.
2. Capture the visible subject transform as the reference frame: skeletal root
   bone world transform, then valid static mesh component, then pawn actor
   transform.
3. Convert the final game camera pose into subject-local space.
4. Place the preview subject at origin using the subject's relative facing.
5. Place the camera marker and frustum at the subject-local camera pose.
6. Copy skeletal component-space bone transforms from the live pawn into a
   preview skeletal actor if the pawn exposes a compatible skeletal mesh.
7. Draw movement direction from pawn velocity or last-frame displacement.
8. Refresh the preview scene immediately and invalidate the `SEditorViewport`
   so the docked tab keeps following PIE even if the viewport active timer
   would otherwise sleep.

The preview transform origin is the live visible subject. World translation is
deliberately removed. Rotation and pose remain visible. This avoids the false
case where a rotating pawn root makes the preview character spin while the
runtime character is visually still.

## Pose Copy

Implementation:

- Spawn an `ASkeletalMeshActor` in the `FAdvancedPreviewScene` when the live
  pawn exposes a `USkeletalMeshComponent`.
- Mirror the source skeletal mesh asset onto the proxy skeletal component.
- Disable proxy animation and component ticking.
- Each editor tick, copy source `GetComponentSpaceTransforms()` into the
  proxy's editable component-space transform array and call
  `ApplyEditedComponentSpaceTransforms()`.
- Place the proxy actor at the source mesh component transform relative to the
  live pawn transform, so world translation is stripped while pose and facing
  remain visible.

Rationale: a cross-world `UPoseableMeshComponent` path would require a second
bone-copy convention and does not match the Shot Editor's proven proxy pattern.
The skeletal actor + component-space-transform copy keeps Runtime Previewer and
Shot Editor preview lifecycles aligned.

Fallback:

- If no skeletal mesh exists, show a capsule / axis marker.
- If the preview mesh cannot be created, show an empty state message and still
  draw the camera marker if camera data is valid.

The preview copy is editor-only and lives in the preview scene world. It must
not reference or move the live pawn component hierarchy.

## Interaction

Mouse input inside the tab controls the observer viewport camera.

Expected behavior:

- Drag / rotate / pan / dolly use `FEditorViewportClient` conventions.
- The preview pawn and camera marker keep updating while the user manipulates
  the observer view.
- A Frame Pawn button recenters on the fixed preview pawn and camera marker.

The observer camera state is local to the Runtime Previewer widget. It is not
serialized on the Camera Type Asset.

## Lifecycle

PIE starts:

- existing toolkit debug ticker starts.
- if exactly one matching camera exists, existing auto-bind behavior applies.
- previewer receives the bound camera and starts drawing once data is valid.

PIE ends:

- toolkit clears `DebuggedCamera`.
- previewer clears live references and preview actors.
- tab remains open but shows an empty state.

Camera unbound or destroyed:

- previewer clears pose data.
- graph debug overlays clear through existing toolkit path.

Multiple Camera Type Asset editor instances:

- each toolkit instance owns its own previewer.
- tab routing must use the toolkit-local `FToolMenuContext` / tab manager
  pattern already used by the Debug picker and Shot Editor button.

## Error Handling

Show concise empty states for:

- PIE not running.
- no debug camera bound.
- bound camera has no valid snapshot.
- no controlled pawn found.
- skeletal pose unavailable.

The previewer should continue to draw whatever valid partial data exists. For
example, if skeletal pose is unavailable but pawn transform and camera pose are
valid, draw fallback pawn marker plus camera marker.

## Testing

Editor automation:

- Register the Runtime Previewer tab spawner.
- Spawn the tab widget for a toolkit instance.
- Verify the widget can construct with no PIE session and no bound camera.

Manual IDE / Unreal verification:

- Open a Camera Type Asset.
- Start PIE with a matching runtime camera.
- Use the Debug picker to bind a camera.
- Open Window -> Runtime Previewer.
- Move the player pawn.
- Confirm preview pawn stays fixed at origin.
- Confirm skeletal pose changes with gameplay animation.
- Confirm game camera marker/frustum moves relative to the pawn.
- Drag in the previewer and confirm only the observer view moves.
- End PIE and confirm the tab clears without crash.

Build and Unreal automation must be run from Rider, Visual Studio, or Unreal
Editor. Do not invoke UBT, Build.bat, RunUBT.bat, dotnet, msbuild, the editor,
or automation tests from shell in this project.

## Documentation Impact

Implementation must update:

- `Docs/EditorDesignDoc.md` for the new editor tab, viewport, lifecycle, and
  Window menu behavior.
- `Docs/TechDoc.md` for the editor viewport technique and lifetime gotchas.

Runtime design docs are only needed if implementation adds runtime APIs beyond
editor-only debug access.
