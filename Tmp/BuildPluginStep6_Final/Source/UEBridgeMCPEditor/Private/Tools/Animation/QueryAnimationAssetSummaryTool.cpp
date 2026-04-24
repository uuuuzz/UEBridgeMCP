// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Animation/QueryAnimationAssetSummaryTool.h"

#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace.h"
#include "AnimationStateMachineGraph.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "Animation/AnimCurveTypes.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> ToStringArray(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FString& Value : Values)
		{
			Result.Add(MakeShareable(new FJsonValueString(Value)));
		}
		return Result;
	}

	TSharedPtr<FJsonObject> BuildAnimationAssetSummaryObject(UObject* Asset, const FString& AssetPath)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("asset_path"), AssetPath);
		Result->SetStringField(TEXT("asset_type"), Asset ? Asset->GetClass()->GetName() : TEXT("Unknown"));
		Result->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, Asset ? Asset->GetClass()->GetName() : TEXT("Unknown")));

		if (const UAnimationAsset* AnimationAsset = Cast<UAnimationAsset>(Asset))
		{
			Result->SetStringField(TEXT("skeleton_path"), AnimationAsset->GetSkeleton() ? AnimationAsset->GetSkeleton()->GetPathName() : TEXT(""));
		}

		if (const UAnimSequenceBase* SequenceBase = Cast<UAnimSequenceBase>(Asset))
		{
			Result->SetNumberField(TEXT("duration"), SequenceBase->GetPlayLength());
			Result->SetNumberField(TEXT("frame_count"), SequenceBase->GetNumberOfSampledKeys());

			TArray<FString> NotifyNames;
			for (const FAnimNotifyEvent& Notify : SequenceBase->Notifies)
			{
				NotifyNames.Add(Notify.GetNotifyEventName().ToString());
			}
			Result->SetArrayField(TEXT("notifies"), ToStringArray(NotifyNames));
			Result->SetNumberField(TEXT("notify_count"), NotifyNames.Num());

			const FRawCurveTracks& CurveData = SequenceBase->GetCurveData();
			TArray<FString> CurveNames;
			for (const FFloatCurve& Curve : CurveData.FloatCurves)
			{
				CurveNames.Add(Curve.GetName().ToString());
			}
			for (const FVectorCurve& Curve : CurveData.VectorCurves)
			{
				CurveNames.Add(Curve.GetName().ToString());
			}
			for (const FTransformCurve& Curve : CurveData.TransformCurves)
			{
				CurveNames.Add(Curve.GetName().ToString());
			}
			Result->SetArrayField(TEXT("curves"), ToStringArray(CurveNames));
			Result->SetNumberField(TEXT("curve_count"), CurveNames.Num());
		}

		if (const UAnimMontage* Montage = Cast<UAnimMontage>(Asset))
		{
			TArray<FString> SectionNames;
			for (const FCompositeSection& Section : Montage->CompositeSections)
			{
				SectionNames.Add(Section.SectionName.ToString());
			}
			Result->SetArrayField(TEXT("montage_sections"), ToStringArray(SectionNames));
			Result->SetNumberField(TEXT("montage_section_count"), SectionNames.Num());
		}

		if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(Asset))
		{
			TArray<TSharedPtr<FJsonValue>> AxisArray;
			for (int32 AxisIndex = 0; AxisIndex < 2; ++AxisIndex)
			{
				const FBlendParameter BlendParameter = BlendSpace->GetBlendParameter(AxisIndex);
				TSharedPtr<FJsonObject> AxisObject = MakeShareable(new FJsonObject);
				AxisObject->SetStringField(TEXT("display_name"), BlendParameter.DisplayName);
				AxisObject->SetNumberField(TEXT("min"), BlendParameter.Min);
				AxisObject->SetNumberField(TEXT("max"), BlendParameter.Max);
				AxisObject->SetNumberField(TEXT("grid_num"), BlendParameter.GridNum);
				AxisArray.Add(MakeShareable(new FJsonValueObject(AxisObject)));
			}
			Result->SetArrayField(TEXT("blend_parameters"), AxisArray);
			Result->SetNumberField(TEXT("sample_count"), BlendSpace->GetBlendSamples().Num());
		}

		if (const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Asset))
		{
			Result->SetStringField(TEXT("target_skeleton_path"), AnimBlueprint->TargetSkeleton ? AnimBlueprint->TargetSkeleton->GetPathName() : TEXT(""));
			Result->SetStringField(TEXT("parent_class"), AnimBlueprint->ParentClass ? AnimBlueprint->ParentClass->GetPathName() : TEXT(""));
			Result->SetBoolField(TEXT("template"), AnimBlueprint->bIsTemplate);
			Result->SetBoolField(TEXT("multi_threaded_update"), AnimBlueprint->bUseMultiThreadedAnimationUpdate);

			TArray<UEdGraph*> AllGraphs;
			const_cast<UAnimBlueprint*>(AnimBlueprint)->GetAllGraphs(AllGraphs);

			TArray<TSharedPtr<FJsonValue>> StateMachinesArray;
			for (UEdGraph* Graph : AllGraphs)
			{
				if (!Graph)
				{
					continue;
				}

				for (UEdGraphNode* Node : Graph->Nodes)
				{
					UAnimGraphNode_StateMachineBase* StateMachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
					if (!StateMachineNode || !StateMachineNode->EditorStateMachineGraph)
					{
						continue;
					}

					int32 StateCount = 0;
					int32 TransitionCount = 0;
					for (UEdGraphNode* StateMachineGraphNode : StateMachineNode->EditorStateMachineGraph->Nodes)
					{
						if (Cast<UAnimStateNode>(StateMachineGraphNode))
						{
							++StateCount;
						}
						else if (Cast<UAnimStateTransitionNode>(StateMachineGraphNode))
						{
							++TransitionCount;
						}
					}

					TSharedPtr<FJsonObject> StateMachineObject = MakeShareable(new FJsonObject);
					StateMachineObject->SetStringField(TEXT("name"), StateMachineNode->GetStateMachineName());
					StateMachineObject->SetNumberField(TEXT("state_count"), StateCount);
					StateMachineObject->SetNumberField(TEXT("transition_count"), TransitionCount);
					StateMachinesArray.Add(MakeShareable(new FJsonValueObject(StateMachineObject)));
				}
			}

			Result->SetArrayField(TEXT("state_machines"), StateMachinesArray);
			Result->SetNumberField(TEXT("state_machine_count"), StateMachinesArray.Num());
			Result->SetNumberField(TEXT("function_graph_count"), AnimBlueprint->FunctionGraphs.Num());
		}

		return Result;
	}
}

FString UQueryAnimationAssetSummaryTool::GetToolDescription() const
{
	return TEXT("Return compact animation asset summaries for AnimSequence, AnimMontage, BlendSpace, and AnimBlueprint assets.");
}

TMap<FString, FMcpSchemaProperty> UQueryAnimationAssetSummaryTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Animation asset path"), true));
	return Schema;
}

FMcpToolResult UQueryAnimationAssetSummaryTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));

	FString LoadError;
	UObject* Asset = FMcpAssetModifier::LoadAssetByPath<UObject>(AssetPath, LoadError);
	if (!Asset)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	if (!Cast<UAnimationAsset>(Asset) && !Cast<UAnimBlueprint>(Asset))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_TYPE"), TEXT("Asset is not a supported animation asset type"));
	}

	return FMcpToolResult::StructuredSuccess(BuildAnimationAssetSummaryObject(Asset, AssetPath), TEXT("Animation asset summary ready"));
}
