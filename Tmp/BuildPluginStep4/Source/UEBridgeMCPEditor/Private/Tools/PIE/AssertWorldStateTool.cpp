// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/PIE/AssertWorldStateTool.h"

#include "Tools/PIE/WaitForWorldConditionTool.h"
#include "Tools/McpToolRegistry.h"
#include "UEBridgeMCP.h"

FString UAssertWorldStateTool::GetToolDescription() const
{
	return TEXT("Evaluate structured world assertions in editor or PIE and return machine-readable pass/fail results. "
		"Single-shot evaluation (internally forces 'check' mode). Use 'wait-for-world-condition' directly for future async waiting.");
}

TMap<FString, FMcpSchemaProperty> UAssertWorldStateTool::GetInputSchema() const
{
	// 复用 wait-for-world-condition 的 schema（condition 描述完全一致），但隐藏 mode 字段。
	TMap<FString, FMcpSchemaProperty> Schema = GetDefault<UWaitForWorldConditionTool>()->GetInputSchema();
	Schema.Remove(TEXT("mode"));
	return Schema;
}

TArray<FString> UAssertWorldStateTool::GetRequiredParams() const
{
	return GetDefault<UWaitForWorldConditionTool>()->GetRequiredParams();
}

FMcpToolResult UAssertWorldStateTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	// P0-H3: 使用 Registry 中已预热、已 AddToRoot 的单例，避免 NewObject 后被 GC。
	UMcpToolBase* InnerTool = FMcpToolRegistry::Get().FindTool(TEXT("wait-for-world-condition"));
	if (!InnerTool)
	{
		UE_LOG(LogUEBridgeMCP, Error, TEXT("assert-world-state: inner tool 'wait-for-world-condition' not registered"));
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INNER_TOOL_MISSING"),
			TEXT("Inner tool 'wait-for-world-condition' is not registered"));
	}

	// P1-S1: 强制 mode=check，保证 assert 的"单次评估"语义；
	// 即使客户端误传 mode=wait 也在此覆盖。
	TSharedPtr<FJsonObject> PatchedArgs = Arguments.IsValid()
		? MakeShared<FJsonObject>(*Arguments)
		: MakeShared<FJsonObject>();
	PatchedArgs->SetStringField(TEXT("mode"), TEXT("check"));

	return InnerTool->Execute(PatchedArgs, Context);
}
