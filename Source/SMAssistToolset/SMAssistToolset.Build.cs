// Copyright Recursoft LLC. All Rights Reserved.

using UnrealBuildTool;

public class SMAssistToolset : ModuleRules
{
	public SMAssistToolset(ReadOnlyTargetRules Target) : base(Target)
	{
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"CoreUObject",
				"UnrealEd",
				"Json",
				"SMAssist",
				"SMSystem",
				"ToolsetRegistry"
			}
		);
	}
}
