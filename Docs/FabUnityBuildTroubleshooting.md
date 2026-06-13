# Fab Unity Build Troubleshooting

Updated: 2026-06-01

Purpose: short checklist for Fab/package build failures caused by unity-build
include order or stale generated output.

## 1. Scope

This note is for the ComposableCameraSystem plugin in:

```text
C:\Users\Sulley\Documents\Unreal Projects\UE5_6\Plugins\ComposableCameraSystem
```

Do not edit generated folders while diagnosing:

- `Intermediate`
- `Binaries`
- `Saved`
- `Cooked`
- `Temp`
- engine install directories

## 2. Common Symptoms

- Fab/package build fails, but IDE Live Coding looked fine.
- Compile error appears in a unity file instead of the source file you edited.
- Missing type/include error only appears during packaging.
- A stale generated file points at an old header layout.

## 3. First Checks

- Confirm exact plugin root. Multiple Unreal project copies may exist.
- Confirm target engine is UE 5.6.
- Confirm failing file belongs to `Plugins/ComposableCameraSystem`, not an
  engine plugin or another host-project copy.
- Check whether the error references generated code, reflection metadata, or a
  unity translation unit.
- Read the real first error. Later errors often cascade.

## 4. Likely Causes

Include dependency hidden by unity build:

- Source compiles because another file in the unity batch included a needed
  header first.
- Packaging changes unity grouping and exposes the missing include.

Forward declaration mismatch:

- Header uses a type by value but only forward-declares it.
- Fix by including the owning header in the header that needs full type.

Reflection change without full rebuild:

- `UCLASS`, `USTRUCT`, `UPROPERTY`, `UFUNCTION`, enum, or module changes need a
  full editor restart and IDE rebuild.

Wrong source tree:

- Editing a copied plugin or generated output can make the IDE look fixed while
  Fab packages a different tree.

## 5. Fix Pattern

1. Find the first compiler error in the package log.
2. Map it to the original source file, not the unity file.
3. Add the smallest correct include or declaration at the real dependency
   boundary.
4. Avoid speculative include spam.
5. Recheck all consumers when a public header changes.
6. Compile/package from Rider or Visual Studio/Fab tooling, not Codex shell.

## 6. Project Rule

Codex must not run UBT, Build.bat, RunUBT.bat, dotnet, msbuild, Unreal Editor,
or automation tests from shell for this project. User performs compile/package
verification in IDE or official packaging UI.

## 7. Prevention

- Header using a type by value includes that type's header.
- Header using `TObjectPtr<T>` can forward declare `T` when only pointer
  semantics are needed.
- `.cpp` includes its matching header first.
- Public API changes get a full consumer grep.
- Docs and comments must not claim a hot path blocks or loads synchronously
  unless source proves it.
