// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/McpToolBase.h"

FMcpToolDefinition UMcpToolBase::GetDefinition() const
{
	FMcpToolDefinition Definition;
	Definition.Name = GetToolName();
	Definition.Description = GetToolDescription();
	Definition.InputSchema = GetInputSchema();
	Definition.Required = GetRequiredParams();
	Definition.Kind = GetToolKind();
	Definition.ResourceScope = GetResourceScope();
	Definition.bMutates = MutatesState();
	Definition.bSupportsBatch = SupportsBatch();
	Definition.bSupportsDryRun = SupportsDryRun();
	Definition.bSupportsCompile = SupportsCompile();
	Definition.bSupportsSave = SupportsSave();
	return Definition;
}

bool UMcpToolBase::GetStringArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, FString& OutValue)
{
	if (!Args.IsValid())
	{
		return false;
	}
	return Args->TryGetStringField(Key, OutValue);
}

FString UMcpToolBase::GetStringArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, const FString& Default)
{
	FString Value;
	if (GetStringArg(Args, Key, Value))
	{
		return Value;
	}
	return Default;
}

bool UMcpToolBase::GetBoolArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, bool& OutValue)
{
	if (!Args.IsValid())
	{
		return false;
	}
	return Args->TryGetBoolField(Key, OutValue);
}

bool UMcpToolBase::GetBoolArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, bool Default)
{
	bool Value;
	if (GetBoolArg(Args, Key, Value))
	{
		return Value;
	}
	return Default;
}

bool UMcpToolBase::GetIntArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, int32& OutValue)
{
	if (!Args.IsValid())
	{
		return false;
	}

	int64 Value64;
	if (Args->TryGetNumberField(Key, Value64))
	{
		OutValue = static_cast<int32>(Value64);
		return true;
	}
	return false;
}

int32 UMcpToolBase::GetIntArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, int32 Default)
{
	int32 Value;
	if (GetIntArg(Args, Key, Value))
	{
		return Value;
	}
	return Default;
}

bool UMcpToolBase::GetFloatArg(const TSharedPtr<FJsonObject>& Args, const FString& Key, float& OutValue)
{
	if (!Args.IsValid())
	{
		return false;
	}
	double Value;
	if (Args->TryGetNumberField(Key, Value))
	{
		OutValue = static_cast<float>(Value);
		return true;
	}
	return false;
}

float UMcpToolBase::GetFloatArgOrDefault(const TSharedPtr<FJsonObject>& Args, const FString& Key, float Default)
{
	float Value;
	if (GetFloatArg(Args, Key, Value))
	{
		return Value;
	}
	return Default;
}
