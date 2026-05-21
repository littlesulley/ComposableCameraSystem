# Composable Camera System

![](Resources/Icon128.png)

A modular, composable camera framework for Unreal Engine 5.6 and above.

Instead of subclassing a monolithic camera class, cameras are assembled from reusable single-responsibility **nodes**, blended through a tree-based **evaluation tree**, and orchestrated across independent **contexts**. Camera types are data-driven assets authored in a visual graph editor — no Blueprint subclass per camera.

## Documentation

**Full docs:** https://sulley.cc/ComposableCameraSystem-Docs/

**Demo Project:** https://drive.google.com/file/d/15tfMGTo7HvptKyLwlOwtDqn4Gr-tLGg0/view?usp=drive_link

**Fab Page:** https://www.fab.com/listings/7a7e0805-247c-4630-b961-791811c8ebbd

The public site covers concepts, node/transition/modifier reference, editor workflows, and the C++ API.

## Highlights

- **Composability over inheritance.** Cameras are a lightweight container of nodes; new behaviors come from composing different node combinations, not from writing new camera subclasses.
- **Two-tier architecture.** Tier 1 *context stack* handles macro mode switching (gameplay vs. cinematic vs. UI). Tier 2 *evaluation tree* handles micro blending between cameras within a context.
- **Pose-only transitions.** Transitions never reference cameras or directors — they receive two poses each frame and output a blended pose. Fully reusable across any camera pair.
- **Visual editor.** Designers compose cameras, wire typed pins, expose parameters, and author internal variables in a dedicated graph editor.
- **Type-safe Blueprint activation.** A custom K2 node generates pins matching the selected camera type's exposed parameters; opt-in override pins avoid visual clutter.
- **Seamless inter-context blending.** Pushing a new context doesn't freeze the old one — a reference-leaf keeps the previous director live during the blend.

## Engine version

Unreal Engine >= 5.6.

## Status

Under active development. The codebase is pre-1.0 and may be refactored substantially between versions.
