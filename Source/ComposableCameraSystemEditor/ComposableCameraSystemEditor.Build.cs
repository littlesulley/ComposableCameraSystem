using UnrealBuildTool;

public class ComposableCameraSystemEditor : ModuleRules
{
    public ComposableCameraSystemEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "ApplicationCore",
                "AssetDefinition",
                "AssetRegistry",
                "AssetTools",
                "BlueprintGraph",
                "CinematicCamera",
                "ContentBrowser",
                "Core",
                "CoreUObject",
                "CurveEditor",
                "DeveloperSettings",
                "EditorFramework",
                "EditorSubsystem",
                "Engine",
                "GraphEditor",
                "InputCore",
                "InteractiveToolsFramework",
                "Kismet",
                "KismetWidgets",
                "ComposableCameraSystem",
                "LevelEditor",
                "MovieScene",
                "MovieSceneTools",
                "MovieSceneTracks",
                "Projects",
                "RenderCore",
                "RewindDebuggerInterface",
                "RHI",
                "Sequencer",
                "Slate",
                "SlateCore",
                "StructUtilsEditor",
                "StructViewer",
                "TimeManagement",
                "ToolMenus",
                "ToolWidgets",
                "TraceAnalysis",
                "TraceInsights",
                "TraceLog",
                "TraceServices",
                "UnrealEd",
            }
        );
    }
}