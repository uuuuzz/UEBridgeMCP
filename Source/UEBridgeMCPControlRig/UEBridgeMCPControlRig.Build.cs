// Copyright uuuuzz 2024-2026. All Rights Reserved.

using UnrealBuildTool;

public class UEBridgeMCPControlRig : ModuleRules
{
	public UEBridgeMCPControlRig(ReadOnlyTargetRules Target) : base(Target)
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
			"ControlRig",
			"ControlRigDeveloper",
			"RigVMDeveloper"
		});
	}
}
