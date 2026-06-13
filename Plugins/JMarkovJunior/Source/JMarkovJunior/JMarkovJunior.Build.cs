using System;
using System.IO;
using UnrealBuildTool;

public class JMarkovJunior : ModuleRules
{
	public JMarkovJunior(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core"
		});
		PrivateDependencyModuleNames.AddRange(new string[] {
			"CoreUObject", "Engine",
			"Slate", "SlateCore",
			"Projects"
		});

        var binariesSrcDir = Path.Combine(PluginDirectory, "ThirdParty");
        var binariesDestDir = "$(PluginDir)/Binaries/ThirdParty/";
        var binariesSearchOpt = new EnumerationOptions { RecurseSubdirectories = true };
        foreach (var file in new DirectoryInfo(binariesSrcDir).GetFiles("*.*", binariesSearchOpt))
        {
            string relativeDestPath = Path.GetRelativePath(binariesSrcDir, file.FullName);
            RuntimeDependencies.Add(binariesDestDir + relativeDestPath, file.FullName);
        }
	}
}
