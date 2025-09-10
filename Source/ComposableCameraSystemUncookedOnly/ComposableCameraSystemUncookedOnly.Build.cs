using UnrealBuildTool;

public class ComposableCameraSystemUncookedOnly : ModuleRules
{
    public ComposableCameraSystemUncookedOnly(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "ComposableCameraSystem",
                "BlueprintGraph",
                "Core",
                "CoreUObject",
                "Engine",
                "BlueprintGraph",
                "Kismet",
                "KismetCompiler",
                "SlateCore",
                "UnrealEd",
            }
        );
    }
}