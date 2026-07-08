// Copyright Recursoft LLC. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SMAssist : ModuleRules
{
	public SMAssist(ReadOnlyTargetRules Target) : base(Target)
	{
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(ModuleDirectory, "Private")
			});

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
				"EditorSubsystem",
				"Json",
				"AssetRegistry",
				"BlueprintGraph",
				"GraphEditor",
				"Kismet",
				"Slate",
				"SlateCore",
				"SMSystem",
				"SMSystemEditor",
				"SMAssetTools"
			}
		);
	}
}
