// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Debug/CaptureViewportTool.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "UnrealClient.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

FString UCaptureViewportTool::GetToolDescription() const
{
	return TEXT("Capture the active editor viewport to a PNG or JPG file. Returns the file path and metadata.");
}

TMap<FString, FMcpSchemaProperty> UCaptureViewportTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	Schema.Add(TEXT("output_path"), FMcpSchemaProperty::Make(TEXT("string"),
		TEXT("Output file path (e.g. 'D:/Temp/capture.png'). If empty, uses project Saved/Screenshots."), true));

	Schema.Add(TEXT("format"), FMcpSchemaProperty::MakeEnum(
		TEXT("Image format: 'png' or 'jpg'"),
		{TEXT("png"), TEXT("jpg")}));

	return Schema;
}

TArray<FString> UCaptureViewportTool::GetRequiredParams() const
{
	return {TEXT("output_path")};
}

FMcpToolResult UCaptureViewportTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	FString OutputPath;
	if (!GetStringArg(Arguments, TEXT("output_path"), OutputPath))
	{
		// 默认路径
		OutputPath = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("mcp_capture.png");
	}

	const FString Format = GetStringArgOrDefault(Arguments, TEXT("format"), TEXT("png"));
	const FString NormalizedFormat = Format.Equals(TEXT("jpg"), ESearchCase::IgnoreCase)
		? TEXT("jpg")
		: TEXT("png");
	TArray<TSharedPtr<FJsonValue>> WarningsArray;

	const FString ExistingExtension = FPaths::GetExtension(OutputPath, false);
	if (ExistingExtension.IsEmpty())
	{
		OutputPath += FString::Printf(TEXT(".%s"), *NormalizedFormat);
	}
	else if (!ExistingExtension.Equals(NormalizedFormat, ESearchCase::IgnoreCase))
	{
		WarningsArray.Add(MakeShareable(new FJsonValueString(
			FString::Printf(TEXT("Output path extension '.%s' was normalized to '.%s' to match the requested format"), *ExistingExtension, *NormalizedFormat))));
		OutputPath = FPaths::ChangeExtension(OutputPath, NormalizedFormat);
	}

	// 获取活动视口
	FViewport* Viewport = GEditor ? GEditor->GetActiveViewport() : nullptr;
	if (!Viewport)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_VIEWPORT_CAPTURE_FAILED"), TEXT("No active viewport available"));
	}

	// 读取像素
	TArray<FColor> Bitmap;
	int32 Width = Viewport->GetSizeXY().X;
	int32 Height = Viewport->GetSizeXY().Y;

	if (Width <= 0 || Height <= 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_VIEWPORT_CAPTURE_FAILED"), TEXT("Viewport has zero size"));
	}

	bool bReadSuccess = Viewport->ReadPixels(Bitmap);
	if (!bReadSuccess || Bitmap.Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_VIEWPORT_CAPTURE_FAILED"), TEXT("Failed to read viewport pixels"));
	}

	// 确保输出目录存在
	FString Dir = FPaths::GetPath(OutputPath);
	if (!Dir.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*Dir, true);
	}

	// 保存图片
	TArray64<uint8> CompressedData;
	const FImageView ImageView((void*)Bitmap.GetData(), Width, Height, ERawImageFormat::BGRA8);
	if (!FImageUtils::CompressImage(CompressedData, *NormalizedFormat, ImageView))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_VIEWPORT_CAPTURE_FAILED"),
			FString::Printf(TEXT("Failed to encode viewport image as '%s'"), *NormalizedFormat));
	}

	const bool bSaved = FFileHelper::SaveArrayToFile(CompressedData, *OutputPath);
	if (!bSaved)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_VIEWPORT_CAPTURE_FAILED"),
			FString::Printf(TEXT("Failed to save image to '%s'"), *OutputPath));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), TEXT("capture-viewport"));
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("output_path"), OutputPath);
	Response->SetStringField(TEXT("format"), NormalizedFormat);
	Response->SetNumberField(TEXT("width"), Width);
	Response->SetNumberField(TEXT("height"), Height);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("modified_assets"), TArray<TSharedPtr<FJsonValue>>());
	Response->SetArrayField(TEXT("partial_results"), TArray<TSharedPtr<FJsonValue>>());

	return FMcpToolResult::StructuredJson(Response);
}