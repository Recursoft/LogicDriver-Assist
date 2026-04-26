// Copyright Recursoft LLC. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SMAssistTests : ModuleRules
{
	public SMAssistTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(ModuleDirectory, "Private"),
				Path.Combine(ModuleDirectory, "../SMAssist/Private"),
				Path.Combine(PluginDirectory, "../LogicDriver/Source/SMTests/Private"),
				Path.Combine(PluginDirectory, "../LogicDriver/Source/SMSystemEditor/Private")
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UnrealEd",
				"BlueprintGraph",
				"Kismet",
				"KismetCompiler",
				"SlateCore",
				"Slate",
				"InputCore",
				"PropertyEditor",
				"SMSystem",
				"SMSystemEditor",
				"SMExtendedRuntime",
				"SMExtendedEditor",
				"SMAssetTools",
				"SMSystemTests",
				"SMAssist",
				"Json"
			}
		);
	}
}
