// Copyright uuuuzz 2024-2026. All Rights Reserved.

using UnrealBuildTool;

public class UEBridgeMCPExternalAI : ModuleRules
{
	public UEBridgeMCPExternalAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UEBridgeMCP"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"HTTP",
			"Json",
			"JsonUtilities"
		});
	}
}
