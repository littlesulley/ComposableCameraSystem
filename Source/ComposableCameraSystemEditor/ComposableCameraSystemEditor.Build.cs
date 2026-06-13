// Copyright 2026 Sulley. All Rights Reserved.
using UnrealBuildTool;

public class ComposableCameraSystemEditor : ModuleRules
{
    public ComposableCameraSystemEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "ComposableCameraSystem",
                "ComposableCameraSystemUncookedOnly",
                "AdvancedPreviewScene",
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
                "EditorWidgets",
                "Engine",
                "GraphEditor",
                "InputCore",
                "InteractiveToolsFramework",
                "Kismet",
                "KismetWidgets",
                "LevelEditor",
                "LevelSequence",
                "PropertyEditor",
                "MovieScene",
                "MovieSceneTools",
                "MovieSceneTracks",
                "Projects",
                "RenderCore",
                "RewindDebuggerInterface",
                "RHI",
                "Sequencer",
                // SequencerCore exports UE::Sequencer::MakeAddButton (used by
                // FComposableCameraPatchTrackEditor::BuildOutlinerEditWidget for
                // the section + button). The symbol's declaration lives in
                // MVVM/Views/ViewUtilities.h with SEQUENCERCORE_API export.
                "SequencerCore",
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
                "Blutility"
            }
        );
    }
}