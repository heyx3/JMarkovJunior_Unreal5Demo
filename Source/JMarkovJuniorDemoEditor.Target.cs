// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class JMarkovJuniorDemoEditorTarget : TargetRules
{
	public JMarkovJuniorDemoEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		bOverrideBuildEnvironment = true;
        ExtraModuleNames.Add("JMarkovJuniorDemo");
		RegisterModulesCreatedByRider();
	}

	private void RegisterModulesCreatedByRider()
	{
		ExtraModuleNames.AddRange(new string[] { "JMarkovJuniorDemo_Editor" });
	}
}
