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
                "MovieSceneTracks"
            }
			);
	}
}
