// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Animation/AnimationAdvancedTools.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/Skeleton.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "ReferenceSkeleton.h"
#include "ScopedTransaction.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> VectorToJsonArray(const FVector& Vector)
	{
		return {
			MakeShareable(new FJsonValueNumber(Vector.X)),
			MakeShareable(new FJsonValueNumber(Vector.Y)),
			MakeShareable(new FJsonValueNumber(Vector.Z))
		};
	}

	bool ReadVector(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FVector& OutVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Values) || !Values || Values->Num() == 0)
		{
			return false;
		}

		OutVector.X = static_cast<float>((*Values)[0]->AsNumber());
		OutVector.Y = Values->Num() > 1 ? static_cast<float>((*Values)[1]->AsNumber()) : 0.0f;
		OutVector.Z = Values->Num() > 2 ? static_cast<float>((*Values)[2]->AsNumber()) : 0.0f;
		return true;
	}

	TSharedPtr<FJsonObject> SerializeBlendSample(const FBlendSample& Sample, int32 Index)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetNumberField(TEXT("index"), Index);
		Object->SetStringField(TEXT("animation_path"), Sample.Animation ? Sample.Animation->GetPathName() : TEXT(""));
		Object->SetArrayField(TEXT("sample_value"), VectorToJsonArray(Sample.SampleValue));
		Object->SetNumberField(TEXT("rate_scale"), Sample.RateScale);
		Object->SetBoolField(TEXT("use_single_frame"), Sample.bUseSingleFrameForBlending);
		Object->SetNumberField(TEXT("frame_index_to_sample"), static_cast<double>(Sample.FrameIndexToSample));
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> SerializeBlendSamples(const UBlendSpace* BlendSpace)
	{
		TArray<TSharedPtr<FJsonValue>> Samples;
		if (!BlendSpace)
		{
			return Samples;
		}

		const TArray<FBlendSample>& BlendSamples = BlendSpace->GetBlendSamples();
		for (int32 Index = 0; Index < BlendSamples.Num(); ++Index)
		{
			Samples.Add(MakeShareable(new FJsonValueObject(SerializeBlendSample(BlendSamples[Index], Index))));
		}
		return Samples;
	}

	bool ApplyPropertyMap(UObject* Target, const TSharedPtr<FJsonObject>& PropertiesObject, bool bApply, FString& OutError)
	{
		if (!Target || !PropertiesObject.IsValid())
		{
			return true;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObject->Values)
		{
			FProperty* Property = nullptr;
			void* Container = nullptr;
			FString PropertyError;
			if (!FMcpAssetModifier::FindPropertyByPath(Target, Pair.Key, Property, Container, PropertyError))
			{
				OutError = FString::Printf(TEXT("Property '%s' not found: %s"), *Pair.Key, *PropertyError);
				return false;
			}
			if (bApply && !FMcpAssetModifier::SetPropertyFromJson(Property, Container, Pair.Value, PropertyError))
			{
				OutError = FString::Printf(TEXT("Failed to set property '%s': %s"), *Pair.Key, *PropertyError);
				return false;
			}
		}
		return true;
	}

	TSharedPtr<FJsonObject> SerializeNotify(const FAnimNotifyEvent& Notify, int32 Index)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		Object->SetNumberField(TEXT("index"), Index);
		Object->SetStringField(TEXT("event_name"), Notify.GetNotifyEventName().ToString());
		Object->SetStringField(TEXT("notify_name"), Notify.NotifyName.ToString());
		Object->SetNumberField(TEXT("time"), Notify.GetTime());
		Object->SetNumberField(TEXT("trigger_time"), Notify.GetTriggerTime());
		Object->SetNumberField(TEXT("duration"), Notify.GetDuration());
		Object->SetNumberField(TEXT("track_index"), Notify.TrackIndex);
		Object->SetStringField(TEXT("notify_class"), Notify.Notify ? Notify.Notify->GetClass()->GetPathName() : TEXT(""));
		Object->SetStringField(TEXT("notify_state_class"), Notify.NotifyStateClass ? Notify.NotifyStateClass->GetClass()->GetPathName() : TEXT(""));
		Object->SetNumberField(TEXT("trigger_chance"), Notify.NotifyTriggerChance);
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> SerializeNotifies(const UAnimSequenceBase* SequenceBase)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		if (!SequenceBase)
		{
			return Result;
		}

		for (int32 Index = 0; Index < SequenceBase->Notifies.Num(); ++Index)
		{
			Result.Add(MakeShareable(new FJsonValueObject(SerializeNotify(SequenceBase->Notifies[Index], Index))));
		}
		return Result;
	}

	UEdGraphNode* FindAnimGraphNode(UAnimBlueprint* AnimBlueprint, const TSharedPtr<FJsonObject>& Operation)
	{
		if (!AnimBlueprint || !Operation.IsValid())
		{
			return nullptr;
		}

		FString NodeGuidString;
		Operation->TryGetStringField(TEXT("node_guid"), NodeGuidString);
		FGuid NodeGuid;
		const bool bHasGuid = !NodeGuidString.IsEmpty() && FGuid::Parse(NodeGuidString, NodeGuid);

		FString NodeTitle;
		Operation->TryGetStringField(TEXT("node_title"), NodeTitle);

		FString GraphName;
		Operation->TryGetStringField(TEXT("graph_name"), GraphName);

		TArray<UEdGraph*> Graphs;
		AnimBlueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}
			if (!GraphName.IsEmpty() && !Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
				{
					continue;
				}
				if (bHasGuid && Node->NodeGuid == NodeGuid)
				{
					return Node;
				}
				if (!NodeTitle.IsEmpty() && Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Contains(NodeTitle))
				{
					return Node;
				}
			}
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> SerializeAnimGraphNode(const UEdGraphNode* Node)
	{
		TSharedPtr<FJsonObject> Object = MakeShareable(new FJsonObject);
		if (!Node)
		{
			return Object;
		}

		Object->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
		Object->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		Object->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
		Object->SetStringField(TEXT("graph"), Node->GetGraph() ? Node->GetGraph()->GetName() : TEXT(""));
		Object->SetStringField(TEXT("comment"), Node->NodeComment);
		return Object;
	}
}

FString UQuerySkeletonSummaryTool::GetToolDescription() const
{
	return TEXT("Return a skeleton summary including reference bones, sockets, virtual bones, and compatible skeleton references.");
}

TMap<FString, FMcpSchemaProperty> UQuerySkeletonSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Skeleton asset path"), true));
	Schema.Add(TEXT("include_bones"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include reference bone entries")));
	Schema.Add(TEXT("bone_limit"), FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Maximum bones to include when include_bones is true")));
	Schema.Add(TEXT("include_sockets"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Include skeleton sockets")));
	return Schema;
}

FMcpToolResult UQuerySkeletonSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bIncludeBones = GetBoolArgOrDefault(Arguments, TEXT("include_bones"), true);
	const bool bIncludeSockets = GetBoolArgOrDefault(Arguments, TEXT("include_sockets"), true);
	const int32 BoneLimit = FMath::Max(0, GetIntArgOrDefault(Arguments, TEXT("bone_limit"), 200));

	FString LoadError;
	USkeleton* Skeleton = FMcpAssetModifier::LoadAssetByPath<USkeleton>(AssetPath, LoadError);
	if (!Skeleton)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	TArray<TSharedPtr<FJsonValue>> BonesArray;
	if (bIncludeBones)
	{
		const int32 NumBonesToInclude = FMath::Min(RefSkeleton.GetNum(), BoneLimit);
		for (int32 BoneIndex = 0; BoneIndex < NumBonesToInclude; ++BoneIndex)
		{
			TSharedPtr<FJsonObject> BoneObject = MakeShareable(new FJsonObject);
			const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
			BoneObject->SetNumberField(TEXT("index"), BoneIndex);
			BoneObject->SetStringField(TEXT("name"), RefSkeleton.GetBoneName(BoneIndex).ToString());
			BoneObject->SetNumberField(TEXT("parent_index"), ParentIndex);
			BoneObject->SetStringField(TEXT("parent_name"), ParentIndex >= 0 ? RefSkeleton.GetBoneName(ParentIndex).ToString() : TEXT(""));
			BonesArray.Add(MakeShareable(new FJsonValueObject(BoneObject)));
		}
	}

	TArray<TSharedPtr<FJsonValue>> SocketsArray;
	if (bIncludeSockets)
	{
		for (const USkeletalMeshSocket* Socket : Skeleton->Sockets)
		{
			if (!Socket)
			{
				continue;
			}
			TSharedPtr<FJsonObject> SocketObject = MakeShareable(new FJsonObject);
			SocketObject->SetStringField(TEXT("socket_name"), Socket->SocketName.ToString());
			SocketObject->SetStringField(TEXT("bone_name"), Socket->BoneName.ToString());
			SocketObject->SetArrayField(TEXT("relative_location"), VectorToJsonArray(Socket->RelativeLocation));
			SocketObject->SetArrayField(TEXT("relative_scale"), VectorToJsonArray(Socket->RelativeScale));
			SocketsArray.Add(MakeShareable(new FJsonValueObject(SocketObject)));
		}
	}

	TArray<TSharedPtr<FJsonValue>> CompatibleSkeletonsArray;
	for (const TSoftObjectPtr<USkeleton>& CompatibleSkeleton : Skeleton->GetCompatibleSkeletons())
	{
		CompatibleSkeletonsArray.Add(MakeShareable(new FJsonValueString(CompatibleSkeleton.ToSoftObjectPath().ToString())));
	}

	TArray<TSharedPtr<FJsonValue>> VirtualBonesArray;
	for (const FVirtualBone& VirtualBone : Skeleton->GetVirtualBones())
	{
		TSharedPtr<FJsonObject> VirtualBoneObject = MakeShareable(new FJsonObject);
		VirtualBoneObject->SetStringField(TEXT("source_bone"), VirtualBone.SourceBoneName.ToString());
		VirtualBoneObject->SetStringField(TEXT("target_bone"), VirtualBone.TargetBoneName.ToString());
		VirtualBoneObject->SetStringField(TEXT("virtual_bone"), VirtualBone.VirtualBoneName.ToString());
		VirtualBonesArray.Add(MakeShareable(new FJsonValueObject(VirtualBoneObject)));
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, Skeleton->GetClass()->GetName()));
	Response->SetNumberField(TEXT("bone_count"), RefSkeleton.GetNum());
	Response->SetNumberField(TEXT("included_bone_count"), BonesArray.Num());
	Response->SetNumberField(TEXT("socket_count"), Skeleton->Sockets.Num());
	Response->SetNumberField(TEXT("virtual_bone_count"), Skeleton->GetVirtualBones().Num());
	Response->SetArrayField(TEXT("bones"), BonesArray);
	Response->SetArrayField(TEXT("sockets"), SocketsArray);
	Response->SetArrayField(TEXT("virtual_bones"), VirtualBonesArray);
	Response->SetArrayField(TEXT("compatible_skeletons"), CompatibleSkeletonsArray);
	return FMcpToolResult::StructuredJson(Response);
}

FString UCreateBlendSpaceTool::GetToolDescription() const
{
	return TEXT("Create a BlendSpace or BlendSpace1D asset for a target skeleton, with optional generic property patches.");
}

TMap<FString, FMcpSchemaProperty> UCreateBlendSpaceTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("BlendSpace asset path"), true));
	Schema.Add(TEXT("skeleton_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target skeleton asset path"), true));
	Schema.Add(TEXT("blendspace_type"), FMcpSchemaProperty::MakeEnum(TEXT("BlendSpace type"), { TEXT("2d"), TEXT("1d") }));
	Schema.Add(TEXT("properties"), FMcpSchemaProperty::Make(TEXT("object"), TEXT("Optional reflected property map to set after creation")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without creating the asset")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after creation. Default: true.")));
	return Schema;
}

FMcpToolResult UCreateBlendSpaceTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString SkeletonPath = GetStringArgOrDefault(Arguments, TEXT("skeleton_path"));
	const FString BlendSpaceType = GetStringArgOrDefault(Arguments, TEXT("blendspace_type"), TEXT("2d")).ToLower();
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	FString ValidateError;
	if (!FMcpAssetModifier::ValidateAssetPath(AssetPath, ValidateError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_PATH"), ValidateError);
	}
	if (FMcpAssetModifier::AssetExists(AssetPath))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_ALREADY_EXISTS"), FString::Printf(TEXT("Asset already exists: %s"), *AssetPath));
	}

	FString SkeletonError;
	USkeleton* Skeleton = FMcpAssetModifier::LoadAssetByPath<USkeleton>(SkeletonPath, SkeletonError);
	if (!Skeleton)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), SkeletonError);
	}

	UClass* BlendSpaceClass = BlendSpaceType == TEXT("1d") ? UBlendSpace1D::StaticClass() : UBlendSpace::StaticClass();
	const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
	Arguments->TryGetObjectField(TEXT("properties"), PropertiesObject);

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
		Response->SetStringField(TEXT("tool"), GetToolName());
		Response->SetBoolField(TEXT("success"), true);
		Response->SetBoolField(TEXT("dry_run"), true);
		Response->SetStringField(TEXT("asset_path"), AssetPath);
		Response->SetStringField(TEXT("skeleton_path"), SkeletonPath);
		Response->SetStringField(TEXT("class"), BlendSpaceClass->GetPathName());
		return FMcpToolResult::StructuredJson(Response);
	}

	TSharedPtr<FScopedTransaction> Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Create BlendSpace")));
	const FString AssetName = FPackageName::GetShortName(AssetPath);
	UPackage* Package = CreatePackage(*AssetPath);
	UBlendSpace* BlendSpace = Package ? NewObject<UBlendSpace>(Package, BlendSpaceClass, *AssetName, RF_Public | RF_Standalone | RF_Transactional) : nullptr;
	if (!BlendSpace)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_CREATE_FAILED"), TEXT("Failed to create BlendSpace asset"));
	}

	BlendSpace->Modify();
	BlendSpace->SetSkeleton(Skeleton);

	FString PropertyError;
	if (PropertiesObject && PropertiesObject->IsValid() && !ApplyPropertyMap(BlendSpace, *PropertiesObject, true, PropertyError))
	{
		Transaction->Cancel();
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_PROPERTY_SET_FAILED"), PropertyError);
	}

	BlendSpace->ValidateSampleData();
	FAssetRegistryModule::AssetCreated(BlendSpace);
	FMcpAssetModifier::MarkPackageDirty(BlendSpace);

	if (bSave)
	{
		FString SaveError;
		if (!FMcpAssetModifier::SaveAsset(BlendSpace, false, SaveError))
		{
			Transaction->Cancel();
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetStringField(TEXT("skeleton_path"), SkeletonPath);
	Response->SetStringField(TEXT("class"), BlendSpace->GetClass()->GetPathName());
	Response->SetNumberField(TEXT("sample_count"), BlendSpace->GetBlendSamples().Num());
	return FMcpToolResult::StructuredJson(Response);
}

FString UEditBlendSpaceSamplesTool::GetToolDescription() const
{
	return TEXT("Batch add, remove, move, or replace samples in a BlendSpace asset.");
}

TMap<FString, FMcpSchemaProperty> UEditBlendSpaceSamplesTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("BlendSpace asset path"), true));
	Schema.Add(TEXT("operations"), FMcpSchemaProperty::MakeArray(TEXT("Sample edit operations"), TEXT("object")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying changes")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the asset after edits. Default: true.")));
	return Schema;
}

FMcpToolResult UEditBlendSpaceSamplesTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	UBlendSpace* BlendSpace = FMcpAssetModifier::LoadAssetByPath<UBlendSpace>(AssetPath, LoadError);
	if (!BlendSpace)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit BlendSpace Samples")));
		BlendSpace->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	bool bChanged = false;
	bool bAnyFailed = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !OperationObject->IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		FString Action;
		(*OperationObject)->TryGetStringField(TEXT("action"), Action);
		Action = Action.ToLower();

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);
		ResultObject->SetStringField(TEXT("action"), Action);

		bool bOperationSuccess = false;
		FString OperationError;

		if (Action == TEXT("add_sample"))
		{
			FVector SampleValue;
			if (!ReadVector(*OperationObject, TEXT("sample_value"), SampleValue))
			{
				OperationError = TEXT("'sample_value' array is required for add_sample");
			}
			else
			{
				FString AnimationPath;
				(*OperationObject)->TryGetStringField(TEXT("animation_path"), AnimationPath);
				UAnimSequence* AnimSequence = nullptr;
				if (!AnimationPath.IsEmpty())
				{
					FString AnimError;
					AnimSequence = FMcpAssetModifier::LoadAssetByPath<UAnimSequence>(AnimationPath, AnimError);
					if (!AnimSequence)
					{
						OperationError = AnimError;
					}
				}

				if (OperationError.IsEmpty())
				{
					if (!bDryRun)
					{
						const int32 NewSampleIndex = AnimSequence ? BlendSpace->AddSample(AnimSequence, SampleValue) : BlendSpace->AddSample(SampleValue);
						ResultObject->SetNumberField(TEXT("sample_index"), NewSampleIndex);
					}
					bOperationSuccess = true;
				}
			}
		}
		else if (Action == TEXT("remove_sample"))
		{
			const int32 SampleIndex = static_cast<int32>((*OperationObject)->GetIntegerField(TEXT("sample_index")));
			if (!BlendSpace->IsValidBlendSampleIndex(SampleIndex))
			{
				OperationError = TEXT("Invalid sample_index");
			}
			else
			{
				if (!bDryRun)
				{
					BlendSpace->DeleteSample(SampleIndex);
				}
				bOperationSuccess = true;
			}
		}
		else if (Action == TEXT("move_sample"))
		{
			const int32 SampleIndex = static_cast<int32>((*OperationObject)->GetIntegerField(TEXT("sample_index")));
			FVector SampleValue;
			if (!BlendSpace->IsValidBlendSampleIndex(SampleIndex))
			{
				OperationError = TEXT("Invalid sample_index");
			}
			else if (!ReadVector(*OperationObject, TEXT("sample_value"), SampleValue))
			{
				OperationError = TEXT("'sample_value' array is required for move_sample");
			}
			else
			{
				if (!bDryRun && !BlendSpace->EditSampleValue(SampleIndex, SampleValue))
				{
					OperationError = TEXT("BlendSpace rejected the sample value");
				}
				else
				{
					bOperationSuccess = true;
				}
			}
		}
		else if (Action == TEXT("replace_animation"))
		{
			const int32 SampleIndex = static_cast<int32>((*OperationObject)->GetIntegerField(TEXT("sample_index")));
			const FString AnimationPath = (*OperationObject)->GetStringField(TEXT("animation_path"));
			FString AnimError;
			UAnimSequence* AnimSequence = FMcpAssetModifier::LoadAssetByPath<UAnimSequence>(AnimationPath, AnimError);
			if (!BlendSpace->IsValidBlendSampleIndex(SampleIndex))
			{
				OperationError = TEXT("Invalid sample_index");
			}
			else if (!AnimSequence)
			{
				OperationError = AnimError;
			}
			else
			{
				if (!bDryRun && !BlendSpace->ReplaceSampleAnimation(SampleIndex, AnimSequence))
				{
					OperationError = TEXT("BlendSpace rejected the replacement animation");
				}
				else
				{
					bOperationSuccess = true;
				}
			}
		}
		else
		{
			OperationError = FString::Printf(TEXT("Unsupported action '%s'"), *Action);
		}

		if (bOperationSuccess)
		{
			bChanged = true;
		}
		else
		{
			bAnyFailed = true;
			ResultObject->SetStringField(TEXT("error"), OperationError);
		}

		ResultObject->SetBoolField(TEXT("success"), bOperationSuccess);
		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	if (!bDryRun && bChanged)
	{
		BlendSpace->ValidateSampleData();
		BlendSpace->ResampleData();
		FMcpAssetModifier::MarkPackageDirty(BlendSpace);
		if (bSave)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(BlendSpace, false, SaveError))
			{
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
			}
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("samples"), SerializeBlendSamples(BlendSpace));
	Response->SetNumberField(TEXT("sample_count"), BlendSpace->GetBlendSamples().Num());
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}

FString UEditAnimationNotifiesTool::GetToolDescription() const
{
	return TEXT("Batch add, remove, rename, or retime named and class-based animation notifies.");
}

TMap<FString, FMcpSchemaProperty> UEditAnimationNotifiesTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("AnimSequenceBase asset path"), true));
	Schema.Add(TEXT("operations"), FMcpSchemaProperty::MakeArray(TEXT("Notify edit operations"), TEXT("object")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying changes")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the asset after edits. Default: true.")));
	return Schema;
}

FMcpToolResult UEditAnimationNotifiesTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	UAnimSequenceBase* SequenceBase = FMcpAssetModifier::LoadAssetByPath<UAnimSequenceBase>(AssetPath, LoadError);
	if (!SequenceBase)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Animation Notifies")));
		SequenceBase->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	bool bChanged = false;
	bool bAnyFailed = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !OperationObject->IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		FString Action;
		(*OperationObject)->TryGetStringField(TEXT("action"), Action);
		Action = Action.ToLower();

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);
		ResultObject->SetStringField(TEXT("action"), Action);

		bool bOperationSuccess = false;
		FString OperationError;

		if (Action == TEXT("add_notify"))
		{
			const FString NotifyName = GetStringArgOrDefault(*OperationObject, TEXT("notify_name"));
			const FString NotifyClassPath = GetStringArgOrDefault(*OperationObject, TEXT("notify_class_path"));
			const FString NotifyStateClassPath = GetStringArgOrDefault(*OperationObject, TEXT("notify_state_class_path"));
			const float Time = FMath::Clamp(GetFloatArgOrDefault(*OperationObject, TEXT("time"), 0.0f), 0.0f, SequenceBase->GetPlayLength());
			const float Duration = FMath::Max(0.0f, GetFloatArgOrDefault(*OperationObject, TEXT("duration"), 0.0f));
			const int32 TrackIndex = FMath::Max(0, GetIntArgOrDefault(*OperationObject, TEXT("track_index"), 0));

			if (NotifyName.IsEmpty() && NotifyClassPath.IsEmpty() && NotifyStateClassPath.IsEmpty())
			{
				OperationError = TEXT("One of notify_name, notify_class_path, or notify_state_class_path is required");
			}
			else if (!bDryRun)
			{
				FAnimNotifyEvent NewNotify;
				NewNotify.NotifyName = NotifyName.IsEmpty() ? NAME_None : FName(*NotifyName);
				NewNotify.TrackIndex = TrackIndex;
				NewNotify.Link(SequenceBase, Time);
				NewNotify.SetTime(Time);
				NewNotify.TriggerTimeOffset = GetTriggerTimeOffsetForType(SequenceBase->CalculateOffsetForNotify(Time));
				NewNotify.NotifyTriggerChance = FMath::Clamp(GetFloatArgOrDefault(*OperationObject, TEXT("trigger_chance"), 1.0f), 0.0f, 1.0f);

				if (!NotifyClassPath.IsEmpty())
				{
					UClass* NotifyClass = LoadClass<UAnimNotify>(nullptr, *NotifyClassPath);
					if (!NotifyClass)
					{
						OperationError = FString::Printf(TEXT("Could not load notify class: %s"), *NotifyClassPath);
					}
					else
					{
						NewNotify.Notify = NewObject<UAnimNotify>(SequenceBase, NotifyClass, NAME_None, RF_Transactional);
					}
				}
				if (OperationError.IsEmpty() && !NotifyStateClassPath.IsEmpty())
				{
					UClass* NotifyStateClass = LoadClass<UAnimNotifyState>(nullptr, *NotifyStateClassPath);
					if (!NotifyStateClass)
					{
						OperationError = FString::Printf(TEXT("Could not load notify state class: %s"), *NotifyStateClassPath);
					}
					else
					{
						NewNotify.NotifyStateClass = NewObject<UAnimNotifyState>(SequenceBase, NotifyStateClass, NAME_None, RF_Transactional);
						NewNotify.SetDuration(Duration);
						NewNotify.EndLink.Link(SequenceBase, FMath::Clamp(Time + Duration, 0.0f, SequenceBase->GetPlayLength()));
					}
				}
				if (OperationError.IsEmpty())
				{
					const int32 NewIndex = SequenceBase->Notifies.Add(NewNotify);
					ResultObject->SetNumberField(TEXT("notify_index"), NewIndex);
					bOperationSuccess = true;
				}
			}
			else
			{
				bOperationSuccess = true;
			}
		}
		else if (Action == TEXT("remove_notify"))
		{
			const int32 NotifyIndex = GetIntArgOrDefault(*OperationObject, TEXT("notify_index"), INDEX_NONE);
			const FString NotifyName = GetStringArgOrDefault(*OperationObject, TEXT("notify_name"));
			if (NotifyIndex != INDEX_NONE)
			{
				if (!SequenceBase->Notifies.IsValidIndex(NotifyIndex))
				{
					OperationError = TEXT("Invalid notify_index");
				}
				else
				{
					if (!bDryRun)
					{
						SequenceBase->Notifies.RemoveAt(NotifyIndex);
					}
					bOperationSuccess = true;
				}
			}
			else if (!NotifyName.IsEmpty())
			{
				if (!bDryRun)
				{
					bOperationSuccess = SequenceBase->RemoveNotifies({ FName(*NotifyName) });
				}
				else
				{
					bOperationSuccess = true;
				}
				if (!bOperationSuccess)
				{
					OperationError = TEXT("No notify matched notify_name");
				}
			}
			else
			{
				OperationError = TEXT("notify_index or notify_name is required");
			}
		}
		else if (Action == TEXT("rename_notify"))
		{
			const FString OldName = GetStringArgOrDefault(*OperationObject, TEXT("old_name"));
			const FString NewName = GetStringArgOrDefault(*OperationObject, TEXT("new_name"));
			if (OldName.IsEmpty() || NewName.IsEmpty())
			{
				OperationError = TEXT("old_name and new_name are required");
			}
			else
			{
				if (!bDryRun)
				{
					SequenceBase->RenameNotifies(FName(*OldName), FName(*NewName));
				}
				bOperationSuccess = true;
			}
		}
		else if (Action == TEXT("set_time"))
		{
			const int32 NotifyIndex = GetIntArgOrDefault(*OperationObject, TEXT("notify_index"), INDEX_NONE);
			const float Time = FMath::Clamp(GetFloatArgOrDefault(*OperationObject, TEXT("time"), 0.0f), 0.0f, SequenceBase->GetPlayLength());
			if (!SequenceBase->Notifies.IsValidIndex(NotifyIndex))
			{
				OperationError = TEXT("Valid notify_index is required");
			}
			else
			{
				if (!bDryRun)
				{
					SequenceBase->Notifies[NotifyIndex].SetTime(Time);
					SequenceBase->Notifies[NotifyIndex].TriggerTimeOffset = GetTriggerTimeOffsetForType(SequenceBase->CalculateOffsetForNotify(Time));
				}
				bOperationSuccess = true;
			}
		}
		else
		{
			OperationError = FString::Printf(TEXT("Unsupported action '%s'"), *Action);
		}

		if (bOperationSuccess)
		{
			bChanged = true;
		}
		else
		{
			bAnyFailed = true;
			ResultObject->SetStringField(TEXT("error"), OperationError);
		}

		ResultObject->SetBoolField(TEXT("success"), bOperationSuccess);
		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	if (!bDryRun && bChanged)
	{
		SequenceBase->SortNotifies();
		SequenceBase->ClampNotifiesAtEndOfSequence();
		SequenceBase->RefreshCacheData();
		FMcpAssetModifier::MarkPackageDirty(SequenceBase);
		if (bSave)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(SequenceBase, false, SaveError))
			{
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
			}
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("notifies"), SerializeNotifies(SequenceBase));
	Response->SetNumberField(TEXT("notify_count"), SequenceBase->Notifies.Num());
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}

FString UEditAnimGraphNodeTool::GetToolDescription() const
{
	return TEXT("Batch edit AnimBlueprint graph node titles, comments, or reflected node properties.");
}

TMap<FString, FMcpSchemaProperty> UEditAnimGraphNodeTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("AnimBlueprint asset path"), true));
	Schema.Add(TEXT("operations"), FMcpSchemaProperty::MakeArray(TEXT("AnimGraph node edit operations"), TEXT("object")));
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate without applying changes")));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Compile the AnimBlueprint after edits. Default: true.")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the asset after edits. Default: true.")));
	return Schema;
}

FMcpToolResult UEditAnimGraphNodeTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	(void)Context;

	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bCompile = GetBoolArgOrDefault(Arguments, TEXT("compile"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	UAnimBlueprint* AnimBlueprint = FMcpAssetModifier::LoadAssetByPath<UAnimBlueprint>(AssetPath, LoadError);
	if (!AnimBlueprint)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit AnimGraph Node")));
		AnimBlueprint->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	bool bChanged = false;
	bool bAnyFailed = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !OperationObject->IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		FString Action;
		(*OperationObject)->TryGetStringField(TEXT("action"), Action);
		Action = Action.ToLower();

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);
		ResultObject->SetStringField(TEXT("action"), Action);

		UEdGraphNode* Node = FindAnimGraphNode(AnimBlueprint, *OperationObject);
		if (!Node)
		{
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), TEXT("AnimGraph node not found"));
			bAnyFailed = true;
			ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
			continue;
		}

		bool bOperationSuccess = false;
		FString OperationError;

		if (Action == TEXT("set_title"))
		{
			const FString NewTitle = GetStringArgOrDefault(*OperationObject, TEXT("title"));
			if (NewTitle.IsEmpty())
			{
				OperationError = TEXT("'title' is required");
			}
			else
			{
				if (!bDryRun)
				{
					Node->Modify();
					Node->OnRenameNode(NewTitle);
				}
				bOperationSuccess = true;
			}
		}
		else if (Action == TEXT("set_comment"))
		{
			const FString NewComment = GetStringArgOrDefault(*OperationObject, TEXT("comment"));
			if (!bDryRun)
			{
				Node->Modify();
				Node->NodeComment = NewComment;
			}
			bOperationSuccess = true;
		}
		else if (Action == TEXT("set_properties"))
		{
			const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
			if (!(*OperationObject)->TryGetObjectField(TEXT("properties"), PropertiesObject) || !PropertiesObject || !PropertiesObject->IsValid())
			{
				OperationError = TEXT("'properties' object is required");
			}
			else if (!ApplyPropertyMap(Node, *PropertiesObject, !bDryRun, OperationError))
			{
				// OperationError already populated.
			}
			else
			{
				if (!bDryRun)
				{
					Node->Modify();
				}
				bOperationSuccess = true;
			}
		}
		else
		{
			OperationError = FString::Printf(TEXT("Unsupported action '%s'"), *Action);
		}

		if (bOperationSuccess)
		{
			bChanged = true;
			ResultObject->SetObjectField(TEXT("node"), SerializeAnimGraphNode(Node));
		}
		else
		{
			bAnyFailed = true;
			ResultObject->SetStringField(TEXT("error"), OperationError);
		}

		ResultObject->SetBoolField(TEXT("success"), bOperationSuccess);
		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	if (!bDryRun && bChanged)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
		if (bCompile)
		{
			FString CompileError;
			if (!FMcpAssetModifier::CompileBlueprint(AnimBlueprint, CompileError))
			{
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_COMPILE_FAILED"), CompileError);
			}
		}

		FMcpAssetModifier::MarkPackageDirty(AnimBlueprint);
		if (bSave)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(AnimBlueprint, false, SaveError))
			{
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
			}
		}
	}

	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetArrayField(TEXT("results"), ResultsArray);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
