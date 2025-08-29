// Copyright Sulley. All Rights Reserved.

using UnrealBuildTool;

public class ComposableCameraSystem : ModuleRules
{
	public ComposableCameraSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				
			}
			);
			
		
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
                "MovieScene",
                "GameplayTags",
                "MovieSceneTracks"
            }
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				
			}
			);
	}
}
