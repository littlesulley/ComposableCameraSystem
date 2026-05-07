// Copyright Sulley. All Rights Reserved.

using UnrealBuildTool;

public class ComposableCameraSystem : ModuleRules
{
	public ComposableCameraSystem(ReadOnlyTargetRules Target) : base(Target)
	{		
		PublicDependencyModuleNames.AddRange(
			new string[] {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "RenderCore",
                "CinematicCamera",
                "EngineCameras",
                "EnhancedInput",
                "ActorSequence",
                "LevelSequence",
                "MovieScene",
                "GameplayTags",
                "MovieSceneTracks",
                "DeveloperSettings",
                // FInstancedPropertyBag used by FComposableCameraTypeAssetReference
                // (the Level Sequence component's designer-facing parameter / variable surface).
                "StructUtils",
                // FSlateApplication used by the debug panel's mouse-hover
                // fallback when the DebugDrawService callback doesn't supply
                // a PlayerController (it never does — engine passes nullptr).
                "Slate",
                "SlateCore",
                // FPlatformApplicationMisc::ClipboardCopy — used by the
                // CCS.Dump.* console commands to make bug-report pastes trivial.
                "ApplicationCore"
            }
			);

		// Editor-only: `FComposableCameraViewportDebug` needs `GEditor->bIsSimulatingInEditor`
		// to auto-hide the viewport frustum gizmo while the player is possessing the camera.
		// UnrealEd is editor-only; the dependency is conditional on the build target so
		// shipping / cooked game targets still link cleanly without it.
		PrivateDependencyModuleNames.Add("PhysicsCore");

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
