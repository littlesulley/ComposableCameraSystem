// Copyright 2026 Sulley. All Rights Reserved.
using UnrealBuildTool;

public class ComposableCameraSystemUncookedOnly : ModuleRules
{
    public ComposableCameraSystemUncookedOnly(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "BlueprintGraph"
            });

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "ComposableCameraSystem",
                "Core",
                "CoreUObject",
                "Engine",
                "LevelSequence",
                "MovieScene",
                "BlueprintGraph",
                "Kismet",
                "KismetCompiler",
                "SlateCore",
                "ToolMenus",
                "UnrealEd",
                "GraphEditor",
                "Slate",
                "ToolWidgets",
                "ContentBrowser"
            }
        );
    }
}