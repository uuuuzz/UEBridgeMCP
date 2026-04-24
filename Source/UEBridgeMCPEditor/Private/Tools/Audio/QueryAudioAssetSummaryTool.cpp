// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Audio/QueryAudioAssetSummaryTool.h"

#include "Tools/Audio/AudioToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "Sound/SoundBase.h"

FString UQueryAudioAssetSummaryTool::GetToolDescription() const
{
	return TEXT("Summarize SoundWave, SoundCue, and other SoundBase assets, including duration, attenuation/concurrency, and optional SoundCue node routing.");
}

TMap<FString, FMcpSchemaProperty> UQueryAudioAssetSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("SoundWave, SoundCue, or SoundBase asset path"), true));
	Schema.Add(TEXT("include_cue_nodes"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include SoundCue node tree when asset is a SoundCue")));
	Schema.Add(TEXT("max_node_depth"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum SoundCue node recursion depth")));
	return Schema;
}

FMcpToolResult UQueryAudioAssetSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bIncludeCueNodes = GetBoolArgOrDefault(Arguments, TEXT("include_cue_nodes"), true);
	const int32 MaxNodeDepth = GetIntArgOrDefault(Arguments, TEXT("max_node_depth"), 8);

	FString LoadError;
	USoundBase* Sound = nullptr;
	if (!AudioToolUtils::TryLoadSoundBase(AssetPath, Sound, LoadError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FJsonObject> Result = AudioToolUtils::SerializeSoundBase(Sound, bIncludeCueNodes, FMath::Clamp(MaxNodeDepth, 0, 32));
	Result->SetStringField(TEXT("tool"), GetToolName());
	return FMcpToolResult::StructuredSuccess(Result, TEXT("Audio asset summary ready"));
}
