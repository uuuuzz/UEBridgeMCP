// Copyright uuuuzz 2024-2026. All Rights Reserved.

using UnrealBuildTool;

public class UEBridgeMCPPCG : ModuleRules
{
	public UEBridgeMCPPCG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UEBridgeMCP",
			"UEBridgeMCPEditor"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"Json",
			"JsonUtilities",
			"AssetRegistry",
			"PCG"
		});
	}
}
