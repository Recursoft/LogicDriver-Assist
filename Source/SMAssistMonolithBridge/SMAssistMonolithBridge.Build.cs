// Copyright Recursoft LLC. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class SMAssistMonolithBridge : ModuleRules
{
	public SMAssistMonolithBridge(ReadOnlyTargetRules Target) : base(Target)
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
				"EditorSubsystem",
				"Json",
				"SMAssist"
			}
		);

		// Optional Monolith integration. When the Monolith plugin is present in
		// the host project, link MonolithCore and compile the bridge body. When
		// absent, the module still builds but its body compiles to a no-op stub.
		// (Module-level Build.cs throws are not tolerated by UBT 5.7's
		// RulesAssembly even with Optional=true on the .uplugin entry, so we
		// always compile and gate the implementation with WITH_MONOLITH.)
		bool bHasMonolith = Target.ProjectFile != null
			&& Directory.Exists(Path.Combine(
				Target.ProjectFile.Directory.FullName, "Plugins", "Monolith"));

		if (bHasMonolith)
		{
			PrivateDependencyModuleNames.Add("MonolithCore");
			PublicDefinitions.Add("WITH_MONOLITH=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_MONOLITH=0");
		}
	}
}
