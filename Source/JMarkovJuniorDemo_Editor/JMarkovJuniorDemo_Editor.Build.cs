using UnrealBuildTool;

public class JMarkovJuniorDemo_Editor : ModuleRules
{
    public JMarkovJuniorDemo_Editor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "JMarkovJuniorDemo"
        });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "CoreUObject", "Engine", "UnrealEd",
            "Slate", "SlateCore"
        });
    }
}