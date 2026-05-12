// Copyright Recursoft LLC. All Rights Reserved.

using System.IO;
using EpicGames.Core;
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
				"SMSystem"
			}
		);

		// Optional ToolsetRegistry integration (engine-bundled experimental, UE 5.8+).
		// UHT rejects UCLASS source inside #if, so gating happens at the directory level:
		// the UCLASS-bearing files live under WithToolsetRegistry/Inner/ and a .ubtignore
		// marker in WithToolsetRegistry/ hides them from UBT's default walk. When the
		// engine ships ToolsetRegistry, ConditionalAddModuleDirectory opts them back in;
		// on older engines the module compiles as a no-op shell.
		string InnerDirPath = Path.Combine(ModuleDirectory, "WithToolsetRegistry", "Inner");
		string ToolsetRegistryDir = Path.Combine(
			EngineDirectory, "Plugins", "Experimental", "ToolsetRegistry");
		bool bHasToolsetRegistry = Directory.Exists(ToolsetRegistryDir);

		if (bHasToolsetRegistry)
		{
			ConditionalAddModuleDirectory(new DirectoryReference(InnerDirPath));
			PrivateDependencyModuleNames.Add("ToolsetRegistry");
			PublicDefinitions.Add("WITH_TOOLSET_REGISTRY=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_TOOLSET_REGISTRY=0");
		}
	}
}
