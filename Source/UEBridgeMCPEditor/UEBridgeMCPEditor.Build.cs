// Copyright uuuuzz 2024-2026. All Rights Reserved.

using UnrealBuildTool;

public class UEBridgeMCPEditor : ModuleRules
{
	public UEBridgeMCPEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ApplicationCore",
			"UEBridgeMCP"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// HTTP Server
			"HTTP",
			"HTTPServer",

			// Editor Framework
			"UnrealEd",
			"EditorSubsystem",
			"ToolMenus",
			"Slate",
			"SlateCore",
			"StatusBar",

			// Blueprint/Kismet
			"Kismet",
			"KismetCompiler",
			"BlueprintGraph",
			"MaterialEditor",
			"StaticMeshEditor",

			// Animation Blueprint
			"AnimGraph",

			// Asset Management
			"AssetTools",
			"AssetRegistry",
			"ContentBrowser",

			// Level Editing
			"LevelEditor",
			"EditorScriptingUtilities",

			// UI/Widgets
			"UMG",
			"UMGEditor",
			"PropertyEditor",
			"PropertyPath",

			// Animation/Sequencer (for UMG animations)
			"MovieScene",
			"MovieSceneTracks",
			"LevelSequence",
			"Landscape",
			"Foliage",

			// Niagara
			"Niagara",
			"NiagaraEditor",

			// Audio
			"AudioEditor",

			// Physics
			"PhysicsCore",

			// MetaSound
			"MetasoundEngine",
			"MetasoundFrontend",
			"MetasoundEditor",

			// JSON
			"Json",
			"JsonUtilities",
			"ImageCore",

			// Input
			"InputCore",
			"EnhancedInput",

			// AI/Navigation (for PIE input tools)
			"AIModule",
			"NavigationSystem",

			// Project Settings
			"GameplayTags",
			"EngineSettings",
			"Projects",

			// StateTree
			"StateTreeModule",
			"StateTreeEditorModule",
			"GameplayStateTreeModule",
			"PropertyBindingUtils",

			// Gameplay
			"GameplayAbilities",
			"GameplayAbilitiesEditor",
			"GameplayTasks",

			// Python Scripting

			"PythonScriptPlugin",

			// Live Coding
			"LiveCoding",

			// Source Control (for SCM diff tool)
			"SourceControl"
		});

		PublicDefinitions.Add("HAS_GAMEPLAY_ABILITIES=1");
		PublicDefinitions.Add("HAS_GAMEPLAY_STATE_TREE=1");
	}
}
