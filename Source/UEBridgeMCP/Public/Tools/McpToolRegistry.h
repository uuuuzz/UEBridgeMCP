// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/McpToolBase.h"

/**
 * Central registry for MCP tools
 * Singleton pattern for global tool discovery
 */
class UEBRIDGEMCP_API FMcpToolRegistry
{
public:
	using FAliasArgumentAdapter = TFunction<TSharedPtr<FJsonObject>(const TSharedPtr<FJsonObject>& Arguments)>;

	/** Get singleton instance */
	static FMcpToolRegistry& Get();

	/** Destructor */
	~FMcpToolRegistry();

	/**
	 * Register a tool class
	 * Tool instances are created lazily on first use
	 */
	template<typename T>
	void RegisterToolClass()
	{
		static_assert(TIsDerivedFrom<T, UMcpToolBase>::Value,
			"Tool must derive from UMcpToolBase");
		RegisterToolClass(T::StaticClass());
	}

	/** Register tool class by UClass */
	void RegisterToolClass(UClass* ToolClass);

	/** Register a compatibility alias that resolves to an existing canonical tool name */
	void RegisterToolAlias(const FString& AliasName, const FString& TargetToolName);

	/** Register an optional argument adapter for a compatibility alias */
	void RegisterToolAliasArgumentAdapter(const FString& AliasName, FAliasArgumentAdapter Adapter);

	/** Unregister a tool by name */
	void UnregisterTool(const FString& ToolName);

	/** Unregister a compatibility alias by name */
	void UnregisterToolAlias(const FString& AliasName);

	/** Clear all registered tools */
	void ClearAllTools();

	/** Get all registered tool definitions */
	TArray<FMcpToolDefinition> GetAllToolDefinitions() const;

	/** Get all tool names */
	TArray<FString> GetAllToolNames() const;

	/** Get alias -> canonical tool mappings */
	TMap<FString, FString> GetToolAliases() const;

	/** Resolve a tool name through the compatibility alias map */
	FString ResolveToolName(const FString& ToolName) const;

	/** Find tool by name */
	UMcpToolBase* FindTool(const FString& ToolName);

	/** Check if tool exists */
	bool HasTool(const FString& ToolName) const;

	/** 检查工具是否需要在 GameThread 上执行 */
	bool DoesToolRequireGameThread(const FString& ToolName) const;

	/** Get number of registered tools */
	int32 GetToolCount() const;

	/**
	 * 预实例化所有已注册的工具类。
	 * UObject 的 NewObject / AddToRoot 必须在 GameThread 上执行，而 FindTool / ExecuteTool
	 * 有可能被在后台线程（对应 RequiresGameThread()==false 的工具）调用，
	 * 因此需要在模块加载阶段统一预热起来，避免首次实例化落到后台线程发生崩溃。
	 * 应在 RegisterBuiltInTools 末尾或明确的 GameThread 路径调用。
	 */
	void WarmupAllTools();

	/** Execute a tool by name */
	FMcpToolResult ExecuteTool(
		const FString& ToolName,
		const TSharedPtr<FJsonObject>& Arguments,
		const FMcpToolContext& Context);

private:
	FMcpToolRegistry() = default;

	// Non-copyable
	FMcpToolRegistry(const FMcpToolRegistry&) = delete;
	FMcpToolRegistry& operator=(const FMcpToolRegistry&) = delete;

	/** Tool class registry */
	TMap<FString, UClass*> ToolClasses;

	/** Compatibility aliases. Alias name -> canonical tool name. */
	TMap<FString, FString> ToolAliases;

	/** Optional argument adapters for compatibility aliases. Alias name -> adapted arguments. */
	TMap<FString, FAliasArgumentAdapter> ToolAliasArgumentAdapters;

	/** Instantiated tool objects (lazy initialization) */
	TMap<FString, TWeakObjectPtr<UMcpToolBase>> ToolInstances;

	/** Lock for thread-safe access */
	mutable FCriticalSection Lock;

	/** Create tool instance from class */
	UMcpToolBase* CreateToolInstance(UClass* ToolClass);

	/** Resolve aliases without locking. Caller must hold Lock. */
	FString ResolveToolNameNoLock(const FString& ToolName) const;

	/** Singleton instance */
	static FMcpToolRegistry* Instance;
};
