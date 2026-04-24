// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/McpToolRegistry.h"
#include "UEBridgeMCP.h"

FMcpToolRegistry* FMcpToolRegistry::Instance = nullptr;

FMcpToolRegistry& FMcpToolRegistry::Get()
{
	if (!Instance)
	{
		Instance = new FMcpToolRegistry();
	}
	return *Instance;
}

FMcpToolRegistry::~FMcpToolRegistry()
{
	ClearAllTools();
}

void FMcpToolRegistry::RegisterToolClass(UClass* ToolClass)
{
	if (!ToolClass || !ToolClass->IsChildOf(UMcpToolBase::StaticClass()))
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("Attempted to register invalid tool class"));
		return;
	}

	FScopeLock ScopeLock(&Lock);

	// Get tool name from CDO
	UMcpToolBase* CDO = ToolClass->GetDefaultObject<UMcpToolBase>();
	if (!CDO)
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("Failed to get CDO for tool class: %s"), *ToolClass->GetName());
		return;
	}

	FString ToolName = CDO->GetToolName();
	if (ToolName.IsEmpty())
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("Tool class %s has empty name"), *ToolClass->GetName());
		return;
	}

	if (ToolClasses.Contains(ToolName))
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("Tool '%s' is already registered, replacing"), *ToolName);
		// P0-H4: 替换前释放老实例的根引用
		if (TWeakObjectPtr<UMcpToolBase>* OldInstancePtr = ToolInstances.Find(ToolName))
		{
			if (UMcpToolBase* OldInstance = OldInstancePtr->Get())
			{
				if (OldInstance->IsRooted())
				{
					OldInstance->RemoveFromRoot();
				}
			}
		}
		ToolInstances.Remove(ToolName);
	}

	ToolClasses.Add(ToolName, ToolClass);
	ToolAliases.Remove(ToolName);
	ToolAliasArgumentAdapters.Remove(ToolName);
	UE_LOG(LogUEBridgeMCP, Log, TEXT("Registered MCP tool: %s"), *ToolName);
}

void FMcpToolRegistry::RegisterToolAlias(const FString& AliasName, const FString& TargetToolName)
{
	const FString CleanAlias = AliasName.TrimStartAndEnd();
	const FString CleanTarget = TargetToolName.TrimStartAndEnd();

	if (CleanAlias.IsEmpty() || CleanTarget.IsEmpty())
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("Attempted to register empty MCP tool alias '%s' -> '%s'"), *AliasName, *TargetToolName);
		return;
	}

	FScopeLock ScopeLock(&Lock);

	if (ToolClasses.Contains(CleanAlias))
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("MCP tool alias '%s' conflicts with a canonical tool name"), *CleanAlias);
		return;
	}

	const FString ResolvedTarget = ResolveToolNameNoLock(CleanTarget);
	if (CleanAlias == ResolvedTarget)
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("MCP tool alias '%s' resolves to itself"), *CleanAlias);
		return;
	}

	if (!ToolClasses.Contains(ResolvedTarget))
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("MCP tool alias '%s' target is not registered: %s"), *CleanAlias, *ResolvedTarget);
		return;
	}

	if (const FString* ExistingTarget = ToolAliases.Find(CleanAlias))
	{
		if (*ExistingTarget != ResolvedTarget)
		{
			UE_LOG(LogUEBridgeMCP, Warning, TEXT("MCP tool alias '%s' is already registered for '%s', replacing with '%s'"),
				*CleanAlias, **ExistingTarget, *ResolvedTarget);
		}
	}

	ToolAliases.Add(CleanAlias, ResolvedTarget);
	UE_LOG(LogUEBridgeMCP, Log, TEXT("Registered MCP tool alias: %s -> %s"), *CleanAlias, *ResolvedTarget);
}

void FMcpToolRegistry::RegisterToolAliasArgumentAdapter(const FString& AliasName, FAliasArgumentAdapter Adapter)
{
	const FString CleanAlias = AliasName.TrimStartAndEnd();
	if (CleanAlias.IsEmpty() || !Adapter)
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("Attempted to register invalid MCP tool alias adapter: %s"), *AliasName);
		return;
	}

	FScopeLock ScopeLock(&Lock);
	if (!ToolAliases.Contains(CleanAlias))
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("MCP tool alias adapter '%s' has no registered alias"), *CleanAlias);
		return;
	}

	ToolAliasArgumentAdapters.Add(CleanAlias, MoveTemp(Adapter));
	UE_LOG(LogUEBridgeMCP, Log, TEXT("Registered MCP tool alias argument adapter: %s"), *CleanAlias);
}

void FMcpToolRegistry::UnregisterTool(const FString& ToolName)
{
	FScopeLock ScopeLock(&Lock);

	if (ToolAliases.Remove(ToolName) > 0)
	{
		ToolAliasArgumentAdapters.Remove(ToolName);
		UE_LOG(LogUEBridgeMCP, Log, TEXT("Unregistered MCP tool alias: %s"), *ToolName);
		return;
	}

	// P0-H4: 移除实例前必须 RemoveFromRoot，否则 UObject 永远驻留根集造成泄漏
	if (TWeakObjectPtr<UMcpToolBase>* InstancePtr = ToolInstances.Find(ToolName))
	{
		if (UMcpToolBase* ToolInstance = InstancePtr->Get())
		{
			if (ToolInstance->IsRooted())
			{
				ToolInstance->RemoveFromRoot();
			}
		}
	}

	ToolClasses.Remove(ToolName);
	ToolInstances.Remove(ToolName);
	for (auto It = ToolAliases.CreateIterator(); It; ++It)
	{
		if (It.Value() == ToolName)
		{
			UE_LOG(LogUEBridgeMCP, Log, TEXT("Unregistered MCP tool alias because target was removed: %s -> %s"), *It.Key(), *ToolName);
			ToolAliasArgumentAdapters.Remove(It.Key());
			It.RemoveCurrent();
		}
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("Unregistered MCP tool: %s"), *ToolName);
}

void FMcpToolRegistry::UnregisterToolAlias(const FString& AliasName)
{
	FScopeLock ScopeLock(&Lock);
	if (ToolAliases.Remove(AliasName) > 0)
	{
		ToolAliasArgumentAdapters.Remove(AliasName);
		UE_LOG(LogUEBridgeMCP, Log, TEXT("Unregistered MCP tool alias: %s"), *AliasName);
	}
}

void FMcpToolRegistry::ClearAllTools()
{
	FScopeLock ScopeLock(&Lock);

	// P0-H4: 清空前遍历 RemoveFromRoot，避免 UObject 泄漏
	for (auto& Pair : ToolInstances)
	{
		if (UMcpToolBase* ToolInstance = Pair.Value.Get())
		{
			if (ToolInstance->IsRooted())
			{
				ToolInstance->RemoveFromRoot();
			}
		}
	}

	ToolClasses.Empty();
	ToolAliases.Empty();
	ToolAliasArgumentAdapters.Empty();
	ToolInstances.Empty();

	UE_LOG(LogUEBridgeMCP, Log, TEXT("Cleared all MCP tools"));
}

TArray<FMcpToolDefinition> FMcpToolRegistry::GetAllToolDefinitions() const
{
	FScopeLock ScopeLock(&Lock);

	TArray<FMcpToolDefinition> Definitions;
	Definitions.Reserve(ToolClasses.Num() + ToolAliases.Num());

	for (const auto& Pair : ToolClasses)
	{
		UMcpToolBase* CDO = Pair.Value->GetDefaultObject<UMcpToolBase>();
		if (CDO)
		{
			Definitions.Add(CDO->GetDefinition());
		}
	}

	for (const auto& Pair : ToolAliases)
	{
		UClass* const* ToolClassPtr = ToolClasses.Find(Pair.Value);
		if (!ToolClassPtr || !*ToolClassPtr)
		{
			continue;
		}

		UMcpToolBase* CDO = (*ToolClassPtr)->GetDefaultObject<UMcpToolBase>();
		if (CDO)
		{
			FMcpToolDefinition Definition = CDO->GetDefinition();
			Definition.Name = Pair.Key;
			Definition.Description = FString::Printf(TEXT("[Compatibility alias for '%s'] %s"), *Pair.Value, *Definition.Description);
			Definitions.Add(Definition);
		}
	}

	return Definitions;
}

TArray<FString> FMcpToolRegistry::GetAllToolNames() const
{
	FScopeLock ScopeLock(&Lock);

	TArray<FString> Names;
	ToolClasses.GetKeys(Names);
	TArray<FString> AliasNames;
	ToolAliases.GetKeys(AliasNames);
	Names.Append(AliasNames);
	return Names;
}

TMap<FString, FString> FMcpToolRegistry::GetToolAliases() const
{
	FScopeLock ScopeLock(&Lock);
	return ToolAliases;
}

FString FMcpToolRegistry::ResolveToolName(const FString& ToolName) const
{
	FScopeLock ScopeLock(&Lock);
	return ResolveToolNameNoLock(ToolName);
}

UMcpToolBase* FMcpToolRegistry::FindTool(const FString& ToolName)
{
	FScopeLock ScopeLock(&Lock);
	const FString ResolvedToolName = ResolveToolNameNoLock(ToolName);

	// Check if already instantiated
	if (TWeakObjectPtr<UMcpToolBase>* ExistingTool = ToolInstances.Find(ResolvedToolName))
	{
		if (UMcpToolBase* ExistingInstance = ExistingTool->Get())
		{
			return ExistingInstance;
		}

		ToolInstances.Remove(ResolvedToolName);
	}

	// Check if class is registered
	UClass** ToolClassPtr = ToolClasses.Find(ResolvedToolName);
	if (!ToolClassPtr || !*ToolClassPtr)
	{
		return nullptr;
	}

	// Create instance
	return CreateToolInstance(*ToolClassPtr);
}

bool FMcpToolRegistry::HasTool(const FString& ToolName) const
{
	FScopeLock ScopeLock(&Lock);
	return ToolClasses.Contains(ToolName) || ToolAliases.Contains(ToolName);
}

int32 FMcpToolRegistry::GetToolCount() const
{
	FScopeLock ScopeLock(&Lock);
	return ToolClasses.Num() + ToolAliases.Num();
}

void FMcpToolRegistry::WarmupAllTools()
{
	// 必须在 GameThread 上调用；UObject 的 NewObject + AddToRoot 不是线程安全的。
	check(IsInGameThread());

	TArray<FString> ToolNames;
	{
		FScopeLock ScopeLock(&Lock);
		ToolClasses.GetKeys(ToolNames);
	}

	int32 WarmupCount = 0;
	for (const FString& ToolName : ToolNames)
	{
		// FindTool 内部若工具未实例化会调 CreateToolInstance，这里保证它总在 GameThread 发生
		if (FindTool(ToolName))
		{
			++WarmupCount;
		}
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("Warmed up %d MCP tool instances"), WarmupCount);
}

bool FMcpToolRegistry::DoesToolRequireGameThread(const FString& ToolName) const
{
	FScopeLock ScopeLock(&Lock);

	const FString ResolvedToolName = ResolveToolNameNoLock(ToolName);
	UClass* const* ToolClassPtr = ToolClasses.Find(ResolvedToolName);
	if (!ToolClassPtr || !*ToolClassPtr)
	{
		// 未知工具默认需要 GameThread（安全起见）
		return true;
	}

	UMcpToolBase* CDO = (*ToolClassPtr)->GetDefaultObject<UMcpToolBase>();
	if (!CDO)
	{
		return true;
	}

	return CDO->RequiresGameThread();
}

FMcpToolResult FMcpToolRegistry::ExecuteTool(
	const FString& ToolName,
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	FString ResolvedToolName;
	FAliasArgumentAdapter AliasArgumentAdapter;
	{
		FScopeLock ScopeLock(&Lock);
		ResolvedToolName = ResolveToolNameNoLock(ToolName);
		if (ResolvedToolName != ToolName)
		{
			if (const FAliasArgumentAdapter* Adapter = ToolAliasArgumentAdapters.Find(ToolName))
			{
				AliasArgumentAdapter = *Adapter;
			}
		}
	}

	TSharedPtr<FJsonObject> EffectiveArguments = Arguments;
	bool bArgumentsAdapted = false;
	if (AliasArgumentAdapter)
	{
		EffectiveArguments = AliasArgumentAdapter(Arguments);
		if (!EffectiveArguments.IsValid())
		{
			EffectiveArguments = Arguments;
		}
		bArgumentsAdapted = EffectiveArguments != Arguments;
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("Executing tool: %s (Thread: %s)"),
		*ToolName, IsInGameThread() ? TEXT("GameThread") : TEXT("Background"));
	if (ResolvedToolName != ToolName)
	{
		UE_LOG(LogUEBridgeMCP, Log, TEXT("Resolved MCP compatibility alias: %s -> %s"), *ToolName, *ResolvedToolName);
		if (bArgumentsAdapted)
		{
			UE_LOG(LogUEBridgeMCP, Log, TEXT("Adapted MCP compatibility alias arguments for: %s"), *ToolName);
		}
	}

	UMcpToolBase* Tool = FindTool(ResolvedToolName);
	if (!Tool)
	{
		UE_LOG(LogUEBridgeMCP, Error, TEXT("Tool not found: %s"), *ToolName);
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("tool_name"), ToolName);
		Details->SetStringField(TEXT("resolved_tool_name"), ResolvedToolName);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_TOOL_NOT_FOUND"), FString::Printf(TEXT("Tool not found: %s"), *ToolName), Details);
	}

	// Check cancellation before execution
	if (Context.IsCancelled())
	{
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("Tool %s: Request cancelled"), *ToolName);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_REQUEST_CANCELLED"), TEXT("Request cancelled"));
	}

	// Execute with timing
	double StartTime = FPlatformTime::Seconds();
	FMcpToolResult Result = Tool->Execute(EffectiveArguments, Context);
	double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	Result.SetTimingMs(ElapsedMs);

	TSharedPtr<FJsonObject> Stats = MakeShareable(new FJsonObject);
	Stats->SetStringField(TEXT("tool_name"), ResolvedToolName);
	Stats->SetStringField(TEXT("requested_tool_name"), ToolName);
	Stats->SetBoolField(TEXT("compatibility_alias"), ResolvedToolName != ToolName);
	Stats->SetBoolField(TEXT("compatibility_arguments_adapted"), bArgumentsAdapted);
	Stats->SetBoolField(TEXT("mutates"), Tool->MutatesState());
	Stats->SetBoolField(TEXT("supports_batch"), Tool->SupportsBatch());
	Stats->SetBoolField(TEXT("requires_game_thread"), Tool->RequiresGameThread());
	Stats->SetNumberField(TEXT("content_items"), Result.Content.Num());
	Result.SetStats(Stats);

	if (Result.bIsError)
	{
		// Extract error message from Content if available
		FString ErrorText = TEXT("Unknown error");
		if (Result.Content.Num() > 0 && Result.Content[0].IsValid())
		{
			Result.Content[0]->TryGetStringField(TEXT("text"), ErrorText);
		}
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("Tool %s failed (%.1fms): %s"),
			*ToolName, ElapsedMs, *ErrorText);
	}
	else
	{
		UE_LOG(LogUEBridgeMCP, Log, TEXT("Tool %s completed successfully (%.1fms)"), *ToolName, ElapsedMs);
	}

	return Result;
}

UMcpToolBase* FMcpToolRegistry::CreateToolInstance(UClass* ToolClass)
{
	// Create the tool instance - use NewObject without outer to make it persistent
	UMcpToolBase* Tool = NewObject<UMcpToolBase>(GetTransientPackage(), ToolClass);
	if (!Tool)
	{
		UE_LOG(LogUEBridgeMCP, Error, TEXT("Failed to create tool instance for class: %s"), *ToolClass->GetName());
		return nullptr;
	}

	// Prevent garbage collection
	Tool->AddToRoot();

	// Cache the instance
	FString ToolName = Tool->GetToolName();
	ToolInstances.Add(ToolName, Tool);

	UE_LOG(LogUEBridgeMCP, Log, TEXT("Created MCP tool instance: %s"), *ToolName);
	return Tool;
}

FString FMcpToolRegistry::ResolveToolNameNoLock(const FString& ToolName) const
{
	const FString* TargetToolName = ToolAliases.Find(ToolName);
	return TargetToolName ? *TargetToolName : ToolName;
}
