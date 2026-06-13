using UnrealBuildTool;

public class JMarkovJuniorDemo : ModuleRules
{
	public JMarkovJuniorDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "InputCore", "EnhancedInput",
            "JMarkovJunior"
        });
	}
}
