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
		if (TObjectPtr<UMcpToolBase>* OldInstancePtr = ToolInstances.Find(ToolName))
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
	UE_LOG(LogUEBridgeMCP, Log, TEXT("Registered MCP tool: %s"), *ToolName);
}

void FMcpToolRegistry::UnregisterTool(const FString& ToolName)
{
	FScopeLock ScopeLock(&Lock);

	// P0-H4: 移除实例前必须 RemoveFromRoot，否则 UObject 永远驻留根集造成泄漏
	if (TObjectPtr<UMcpToolBase>* InstancePtr = ToolInstances.Find(ToolName))
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

	UE_LOG(LogUEBridgeMCP, Log, TEXT("Unregistered MCP tool: %s"), *ToolName);
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
	ToolInstances.Empty();

	UE_LOG(LogUEBridgeMCP, Log, TEXT("Cleared all MCP tools"));
}

TArray<FMcpToolDefinition> FMcpToolRegistry::GetAllToolDefinitions() const
{
	FScopeLock ScopeLock(&Lock);

	TArray<FMcpToolDefinition> Definitions;
	Definitions.Reserve(ToolClasses.Num());

	for (const auto& Pair : ToolClasses)
	{
		UMcpToolBase* CDO = Pair.Value->GetDefaultObject<UMcpToolBase>();
		if (CDO)
		{
			Definitions.Add(CDO->GetDefinition());
		}
	}

	return Definitions;
}

TArray<FString> FMcpToolRegistry::GetAllToolNames() const
{
	FScopeLock ScopeLock(&Lock);

	TArray<FString> Names;
	ToolClasses.GetKeys(Names);
	return Names;
}

UMcpToolBase* FMcpToolRegistry::FindTool(const FString& ToolName)
{
	FScopeLock ScopeLock(&Lock);

	// Check if already instantiated
	if (TObjectPtr<UMcpToolBase>* ExistingTool = ToolInstances.Find(ToolName))
	{
		return ExistingTool->Get();
	}

	// Check if class is registered
	UClass** ToolClassPtr = ToolClasses.Find(ToolName);
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
	return ToolClasses.Contains(ToolName);
}

int32 FMcpToolRegistry::GetToolCount() const
{
	FScopeLock ScopeLock(&Lock);
	return ToolClasses.Num();
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

	UClass* const* ToolClassPtr = ToolClasses.Find(ToolName);
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
	UE_LOG(LogUEBridgeMCP, Log, TEXT("Executing tool: %s (Thread: %s)"),
		*ToolName, IsInGameThread() ? TEXT("GameThread") : TEXT("Background"));

	UMcpToolBase* Tool = FindTool(ToolName);
	if (!Tool)
	{
		UE_LOG(LogUEBridgeMCP, Error, TEXT("Tool not found: %s"), *ToolName);
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("tool_name"), ToolName);
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
	FMcpToolResult Result = Tool->Execute(Arguments, Context);
	double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	Result.SetTimingMs(ElapsedMs);

	TSharedPtr<FJsonObject> Stats = MakeShareable(new FJsonObject);
	Stats->SetStringField(TEXT("tool_name"), ToolName);
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
