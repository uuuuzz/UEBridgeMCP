// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Scripting/RunPythonScriptTool.h"
#include "UEBridgeMCPEditor.h"
#include "IPythonScriptPlugin.h"
#include "Tools/PIE/PieSessionTool.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Editor.h"

FString URunPythonScriptTool::GetToolDescription() const
{
	return TEXT("Execute a Python script in Unreal Editor's Python environment. Requires PythonScriptPlugin to be enabled.");
}

TMap<FString, FMcpSchemaProperty> URunPythonScriptTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	FMcpSchemaProperty Script;
	Script.Type = TEXT("string");
	Script.Description = TEXT("Inline Python code to execute (mutually exclusive with script_path)");
	Script.bRequired = false;
	Schema.Add(TEXT("script"), Script);

	FMcpSchemaProperty ScriptPath;
	ScriptPath.Type = TEXT("string");
	ScriptPath.Description = TEXT("Path to a Python script file (mutually exclusive with script)");
	ScriptPath.bRequired = false;
	Schema.Add(TEXT("script_path"), ScriptPath);

	FMcpSchemaProperty PythonPaths;
	PythonPaths.Type = TEXT("array");
	PythonPaths.Description = TEXT("Additional directories to add to Python sys.path for module imports (array of strings)");
	PythonPaths.bRequired = false;
	Schema.Add(TEXT("python_paths"), PythonPaths);

	FMcpSchemaProperty Arguments;
	Arguments.Type = TEXT("object");
	Arguments.Description = TEXT("Arguments to pass to the script (accessible via unreal.get_mcp_args())");
	Arguments.bRequired = false;
	Schema.Add(TEXT("arguments"), Arguments);

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate the request and report what would run without executing Python")));
	Schema.Add(TEXT("confirm_execution"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Required for non-dry-run execution because Python can mutate editor state")));

	return Schema;
}

FMcpToolResult URunPythonScriptTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	(void)Context;
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bConfirmExecution = GetBoolArgOrDefault(Arguments, TEXT("confirm_execution"), false);

	FString Script = GetStringArgOrDefault(Arguments, TEXT("script"));
	FString ScriptPath = GetStringArgOrDefault(Arguments, TEXT("script_path"));

	if (Script.IsEmpty() && ScriptPath.IsEmpty())
	{
		return FMcpToolResult::Error(TEXT("Either 'script' or 'script_path' must be provided"));
	}

	if (!Script.IsEmpty() && !ScriptPath.IsEmpty())
	{
		return FMcpToolResult::Error(TEXT("Cannot specify both 'script' and 'script_path'. Use only one."));
	}

	TArray<FString> PythonPaths;
	if (Arguments->HasField(TEXT("python_paths")))
	{
		const TArray<TSharedPtr<FJsonValue>>* PathsArray;
		if (Arguments->TryGetArrayField(TEXT("python_paths"), PathsArray))
		{
			for (const TSharedPtr<FJsonValue>& PathValue : *PathsArray)
			{
				FString PathStr = PathValue->AsString();
				if (!PathStr.IsEmpty())
				{
					PythonPaths.Add(PathStr);
				}
			}
		}
	}

	TSharedPtr<FJsonObject> ExecutionPlan = MakeShareable(new FJsonObject);
	ExecutionPlan->SetStringField(TEXT("tool"), GetToolName());
	ExecutionPlan->SetBoolField(TEXT("dry_run"), bDryRun);
	ExecutionPlan->SetBoolField(TEXT("has_inline_script"), !Script.IsEmpty());
	if (!ScriptPath.IsEmpty())
	{
		ExecutionPlan->SetStringField(TEXT("script_path"), ScriptPath);
	}
	ExecutionPlan->SetNumberField(TEXT("python_paths_count"), PythonPaths.Num());

	if (bDryRun)
	{
		ExecutionPlan->SetBoolField(TEXT("success"), true);
		ExecutionPlan->SetBoolField(TEXT("would_execute"), true);
		return FMcpToolResult::StructuredJson(ExecutionPlan);
	}

	if (!bConfirmExecution)
	{
		return FMcpToolResult::StructuredError(
			TEXT("UEBMCP_CONFIRMATION_REQUIRED"),
			TEXT("run-python-script requires confirm_execution=true because Python can mutate editor state"),
			ExecutionPlan);
	}

	// Check if PythonScriptPlugin is available
	if (!FModuleManager::Get().IsModuleLoaded("PythonScriptPlugin"))
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("run-python-script: PythonScriptPlugin is not loaded"));
		return FMcpToolResult::Error(TEXT("PythonScriptPlugin is not enabled. Enable it in Edit > Plugins > Scripting > Python Editor Script Plugin"));
	}

	// 安全检查：PIE 正在启动或停止时，拒绝执行 Python 脚本
	// 避免在 PIE 世界创建/销毁过程中访问不稳定的对象导致编辑器死锁或崩溃
	if (UPieSessionTool::IsPIETransitioning())
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("run-python-script: Rejected - PIE is transitioning (starting or stopping)"));
		return FMcpToolResult::Error(TEXT("PIE is currently transitioning (starting or stopping). Wait for PIE to finish transitioning by polling pie-session get-state, then retry."));
	}

	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin)
	{
		return FMcpToolResult::Error(TEXT("Failed to get PythonScriptPlugin interface"));
	}

	// If script_path is provided, read the file
	if (!ScriptPath.IsEmpty())
	{
		FString ReadError;
		if (!ReadScriptFile(ScriptPath, Script, ReadError))
		{
			return FMcpToolResult::Error(ReadError);
		}
		UE_LOG(LogUEBridgeMCP, Log, TEXT("run-python-script: Loaded script from %s"), *ScriptPath);
	}

	// 安全检查：PIE 运行期间拒绝执行可能导致编辑器崩溃的危险操作
	// 在 PIE 期间编译蓝图会触发 CDO 重建和对象重新实例化，极易导致崩溃或卡死
	if (GEditor && GEditor->IsPlaySessionInProgress())
	{
		// 检测脚本中是否包含危险操作关键词
		static const TArray<FString> DangerousPatterns = {
			TEXT("compile_blueprint"),
			TEXT("CompileBlueprint"),
			TEXT("compile_all"),
			TEXT("CompileAll"),
			TEXT("hot_reload"),
			TEXT("HotReload"),
			TEXT("request_end_play"),
			TEXT("RequestEndPlayMap"),
			TEXT("open_level"),
			TEXT("OpenLevel"),
			TEXT("load_level"),
			TEXT("LoadLevel"),
			TEXT("garbage_collect"),
			TEXT("CollectGarbage"),
		};

		for (const FString& Pattern : DangerousPatterns)
		{
			if (Script.Contains(Pattern))
			{
				FString ErrorMsg = FString::Printf(
					TEXT("Dangerous operation '%s' detected in script while PIE is running. "
					     "This operation can cause editor crash or freeze during PIE. "
					     "Please stop PIE first (use pie-session stop), then retry."),
					*Pattern);
				UE_LOG(LogUEBridgeMCP, Warning, TEXT("run-python-script: %s"), *ErrorMsg);
				return FMcpToolResult::Error(ErrorMsg);
			}
		}
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("run-python-script: Adding %d Python path(s)"), PythonPaths.Num());

	// Build Python command with arguments and paths if provided
	FString PythonCommand = BuildPythonCommand(Script, Arguments, PythonPaths);

	UE_LOG(LogUEBridgeMCP, Log, TEXT("run-python-script: Executing Python code..."));

	// Execute Python command
	bool bSuccess = false;
	FString Error;
	FString Output = ExecutePython(PythonCommand, bSuccess, Error);

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), bSuccess);

	// Detect if script performed level loading operations
	bool bLevelLoadDetected = Output.Contains(TEXT("LoadMap")) ||
	                          Output.Contains(TEXT("load_level")) ||
	                          Output.Contains(TEXT("Loading map"));

	// 方案A修复：仅在检测到 level 加载操作时才执行 GC，避免异常路径下的 use-after-free
	if (bLevelLoadDetected)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	if (bSuccess)
	{
		Result->SetStringField(TEXT("output"), Output);
		if (!ScriptPath.IsEmpty())
		{
			Result->SetStringField(TEXT("script_path"), ScriptPath);
		}
		if (bLevelLoadDetected)
		{
			Result->SetStringField(TEXT("advisory"),
				TEXT("Level loading detected. World state may have changed. Verify editor state before continuing."));
		}
		UE_LOG(LogUEBridgeMCP, Log, TEXT("run-python-script: Execution completed successfully"));
	}
	else
	{
		Result->SetStringField(TEXT("error"), Error);
		if (!Output.IsEmpty())
		{
			Result->SetStringField(TEXT("output"), Output);
		}
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("run-python-script: Execution failed: %s"), *Error);
	}

	return FMcpToolResult::Json(Result);
}

FString URunPythonScriptTool::ExecutePython(const FString& Command, bool& bOutSuccess, FString& OutError)
{
	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin)
	{
		bOutSuccess = false;
		OutError = TEXT("PythonScriptPlugin interface not available");
		return FString();
	}

	FPythonCommandEx PythonCommand;
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Private;
	PythonCommand.Command = Command;

	const bool bCommandSucceeded = PythonPlugin->ExecPythonCommandEx(PythonCommand);

	FString Output;
	for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
	{
		if (!Output.IsEmpty())
		{
			Output += TEXT("\n");
		}

		Output += Entry.Output;
	}

	bOutSuccess = bCommandSucceeded;
	if (bOutSuccess)
	{
		OutError = TEXT("");
	}
	else
	{
		OutError = PythonCommand.CommandResult;
		if (OutError.IsEmpty())
		{
			OutError = TEXT("Python execution failed. Check LogPython output for details.");
		}
	}

	return Output;
}

bool URunPythonScriptTool::ReadScriptFile(const FString& ScriptPath, FString& OutScript, FString& OutError)
{
	// Convert to absolute path if relative
	FString AbsolutePath = ScriptPath;
	if (FPaths::IsRelative(AbsolutePath))
	{
		AbsolutePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), ScriptPath);
	}

	// Check if file exists
	if (!FPaths::FileExists(AbsolutePath))
	{
		OutError = FString::Printf(TEXT("Script file not found: %s"), *AbsolutePath);
		return false;
	}

	// Read file contents
	if (!FFileHelper::LoadFileToString(OutScript, *AbsolutePath))
	{
		OutError = FString::Printf(TEXT("Failed to read script file: %s"), *AbsolutePath);
		return false;
	}

	return true;
}

FString URunPythonScriptTool::BuildPythonCommand(const FString& Script, const TSharedPtr<FJsonObject>& Arguments, const TArray<FString>& PythonPaths)
{
	FString FinalScript = Script;
	bool bNeedsWrapper = false;
	FString Preamble;
	FString CleanupScript;

	if (PythonPaths.Num() > 0)
	{
		bNeedsWrapper = true;
		Preamble += TEXT("import os\n");
		Preamble += TEXT("import sys\n");
		Preamble += TEXT("_mcp_added_python_paths = []\n");

		for (const FString& Path : PythonPaths)
		{
			FString AbsolutePath = Path;
			if (FPaths::IsRelative(AbsolutePath))
			{
				AbsolutePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
			}

			AbsolutePath = AbsolutePath.Replace(TEXT("\\"), TEXT("/"));
			const FString EscapedPath = AbsolutePath.Replace(TEXT("'"), TEXT("\\'"));

			Preamble += FString::Printf(TEXT("if os.path.exists(r'%s') and r'%s' not in sys.path:\n"), *EscapedPath, *EscapedPath);
			Preamble += FString::Printf(TEXT("    sys.path.insert(0, r'%s')\n"), *EscapedPath);
			Preamble += FString::Printf(TEXT("    _mcp_added_python_paths.append(r'%s')\n"), *EscapedPath);
		}
		Preamble += TEXT("\n");

		CleanupScript += TEXT("for _mcp_added_python_path in reversed(_mcp_added_python_paths):\n");
		CleanupScript += TEXT("    if _mcp_added_python_path in sys.path:\n");
		CleanupScript += TEXT("        sys.path.remove(_mcp_added_python_path)\n");
		CleanupScript += TEXT("_mcp_added_python_paths = []\n");
	}

	if (Arguments.IsValid() && Arguments->HasField(TEXT("arguments")))
	{
		const TSharedPtr<FJsonObject>* ArgsObject;
		if (Arguments->TryGetObjectField(TEXT("arguments"), ArgsObject))
		{
			bNeedsWrapper = true;

			FString ArgsJson;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ArgsJson);
			FJsonSerializer::Serialize(ArgsObject->ToSharedRef(), Writer);

			const FTCHARToUTF8 ArgsJsonUtf8(*ArgsJson);
			const FString EncodedArgsJson = FBase64::Encode(reinterpret_cast<const uint8*>(ArgsJsonUtf8.Get()), ArgsJsonUtf8.Length());

			Preamble += TEXT("import base64\n");
			Preamble += TEXT("import json\n");
			Preamble += TEXT("import unreal\n");
			Preamble += FString::Printf(TEXT("_mcp_args_json = base64.b64decode('%s').decode('utf-8')\n"), *EncodedArgsJson);
			Preamble += TEXT("_mcp_args = json.loads(_mcp_args_json)\n");
			Preamble += TEXT("_mcp_had_get_mcp_args = hasattr(unreal, 'get_mcp_args')\n");
			Preamble += TEXT("_mcp_previous_get_mcp_args = unreal.get_mcp_args if _mcp_had_get_mcp_args else None\n");
			Preamble += TEXT("unreal.get_mcp_args = lambda: _mcp_args\n");
			Preamble += TEXT("\n");

			CleanupScript += TEXT("if _mcp_had_get_mcp_args:\n");
			CleanupScript += TEXT("    unreal.get_mcp_args = _mcp_previous_get_mcp_args\n");
			CleanupScript += TEXT("elif hasattr(unreal, 'get_mcp_args'):\n");
			CleanupScript += TEXT("    delattr(unreal, 'get_mcp_args')\n");
			CleanupScript += TEXT("_mcp_previous_get_mcp_args = None\n");
			CleanupScript += TEXT("_mcp_args = None\n");
			CleanupScript += TEXT("_mcp_args_json = None\n");
		}
	}

	if (bNeedsWrapper)
	{
		FString WrappedBody = FinalScript;
		FString WrappedCleanup = CleanupScript;
		WrappedBody.TrimEndInline();
		WrappedCleanup.TrimEndInline();

		const FString IndentedBody = TEXT("    ") + WrappedBody.Replace(TEXT("\n"), TEXT("\n    "));
		const FString IndentedCleanup = TEXT("    ") + WrappedCleanup.Replace(TEXT("\n"), TEXT("\n    "));

		FinalScript = Preamble;
		FinalScript += TEXT("try:\n");
		FinalScript += IndentedBody;
		FinalScript += TEXT("\nfinally:\n");
		FinalScript += IndentedCleanup;
	}

	return FinalScript;
}
