// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/Blueprint/EditBlueprintGraphTool.h"

#include "Session/McpEditorSessionManager.h"
#include "Tools/Write/AddGraphNodeTool.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"

namespace
{
	FString JsonValueToLiteral(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid() || Value->IsNull())
		{
			return TEXT("");
		}

		switch (Value->Type)
		{
		case EJson::String:
			return Value->AsString();
		case EJson::Number:
			return FString::SanitizeFloat(Value->AsNumber());
		case EJson::Boolean:
			return Value->AsBool() ? TEXT("true") : TEXT("false");
		default:
			{
				FString Serialized;
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
				FJsonSerializer::Serialize(Value.ToSharedRef(), TEXT("value"), Writer);
				return Serialized;
			}
		}
	}

	bool ResolveNodeToken(
		const FString& Token,
		const TMap<FString, FGuid>& AliasMap,
		FGuid& OutGuid,
		FString& OutError)
	{
		if (Token.IsEmpty())
		{
			OutError = TEXT("Node reference is empty");
			return false;
		}

		if (const FGuid* AliasGuid = AliasMap.Find(Token))
		{
			OutGuid = *AliasGuid;
			return true;
		}

		if (FGuid::Parse(Token, OutGuid))
		{
			return true;
		}

		OutError = FString::Printf(TEXT("Unknown node reference or invalid GUID: %s"), *Token);
		return false;
	}

	UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction = EGPD_MAX)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}
			if (!Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (Direction != EGPD_MAX && Pin->Direction != Direction)
			{
				continue;
			}
			return Pin;
		}
		return nullptr;
	}

	void AddOperationSummary(
		TArray<TSharedPtr<FJsonValue>>& ResultsArray,
		int32 Index,
		const FString& Op,
		bool bSuccess,
		const FString& ErrorMessage,
		const TSharedPtr<FJsonObject>& Extra = nullptr)
	{
		TSharedPtr<FJsonObject> Result = Extra.IsValid() ? Extra : MakeShareable(new FJsonObject);
		Result->SetNumberField(TEXT("index"), Index);
		Result->SetStringField(TEXT("op"), Op);
		Result->SetBoolField(TEXT("success"), bSuccess);
		if (!bSuccess && !ErrorMessage.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), ErrorMessage);
		}
		ResultsArray.Add(MakeShareable(new FJsonValueObject(Result)));
	}
}

FString UEditBlueprintGraphTool::GetToolDescription() const
{
	return TEXT("Transactional Blueprint graph editing with batched operations, alias support, dry-run validation, and optional compile/save.");
}

TMap<FString, FMcpSchemaProperty> UEditBlueprintGraphTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Blueprint asset path"), true));

	TSharedPtr<FMcpSchemaProperty> PositionSchema = MakeShared<FMcpSchemaProperty>();
	PositionSchema->Type = TEXT("array");
	PositionSchema->ItemsType = TEXT("number");
	PositionSchema->Description = TEXT("Node position [x, y]");

	TSharedPtr<FMcpSchemaProperty> PropertiesSchema = MakeShared<FMcpSchemaProperty>();
	PropertiesSchema->Type = TEXT("object");
	PropertiesSchema->Description = TEXT("Property map");
	PropertiesSchema->bAdditionalProperties = true;

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->NestedRequired = {TEXT("op")};
	OperationSchema->Properties.Add(TEXT("op"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Graph operation"),
		{TEXT("add_node"), TEXT("remove_node"), TEXT("connect"), TEXT("disconnect"), TEXT("set_pin_default"), TEXT("move_node"), TEXT("set_node_property")},
		true)));
	OperationSchema->Properties.Add(TEXT("alias"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Alias for add_node and later references"))));
	OperationSchema->Properties.Add(TEXT("graph_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target graph name"))));
	OperationSchema->Properties.Add(TEXT("node_class"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Node class for add_node"))));
	OperationSchema->Properties.Add(TEXT("node"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Existing node GUID or alias"))));
	OperationSchema->Properties.Add(TEXT("source_node"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Source node GUID or alias"))));
	OperationSchema->Properties.Add(TEXT("source_pin"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Source pin name"))));
	OperationSchema->Properties.Add(TEXT("target_node"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target node GUID or alias"))));
	OperationSchema->Properties.Add(TEXT("target_pin"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target pin name"))));
	OperationSchema->Properties.Add(TEXT("pin_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Pin name for set_pin_default/disconnect"))));
	OperationSchema->Properties.Add(TEXT("position"), PositionSchema);
	OperationSchema->Properties.Add(TEXT("properties"), PropertiesSchema);
	OperationSchema->Properties.Add(TEXT("pin_defaults"), PropertiesSchema);
	OperationSchema->Properties.Add(TEXT("property_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Property path for set_node_property"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Items = OperationSchema;
	OperationsSchema.Description = TEXT("Batched graph operations");
	OperationsSchema.bRequired = true;
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on first error")));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Compile after apply")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save after apply")));
	Schema.Add(TEXT("transaction_label"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Transaction label")));
	return Schema;
}

TArray<FString> UEditBlueprintGraphTool::GetRequiredParams() const
{
	return {TEXT("asset_path"), TEXT("operations")};
}

FMcpToolResult UEditBlueprintGraphTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const bool bCompile = GetBoolArgOrDefault(Arguments, TEXT("compile"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const FString TransactionLabel = GetStringArgOrDefault(Arguments, TEXT("transaction_label"), TEXT("Edit Blueprint Graph"));

	const TArray<TSharedPtr<FJsonValue>>* OperationsArray = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), OperationsArray) || !OperationsArray || OperationsArray->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	UBlueprint* Blueprint = FMcpEditorSessionManager::Get().ResolveAsset<UBlueprint>(Context.SessionId, AssetPath, LoadError);
	if (!Blueprint)
	{
		TSharedPtr<FJsonObject> Details = MakeShareable(new FJsonObject);
		Details->SetStringField(TEXT("asset_path"), AssetPath);
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError, Details);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TransactionLabel));
		FMcpAssetModifier::MarkModified(Blueprint);
	}

	TMap<FString, FGuid> AliasMap;
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;

	// P1-S2: dry-run 时对 alias 分配的是年空的伪 GUID，后续许多 op 引用它时（remove_node/connect/
	// disconnect/set_pin_default/set_node_property/move_node）都会走到 ResolveBlueprintNode——它肯定找不到这
	// 个伪 GUID。为了让 dry-run 能真正验证整个 batch，我们先收集 dry-run 模式下负责创建节点的 alias
	// 集合，在 op 里遇到属于该集合的 alias 时跳过实际解析，直接视为成功。
	TSet<FString> DryRunFakeAliases;

	for (int32 Index = 0; Index < OperationsArray->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*OperationsArray)[Index]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			AddOperationSummary(ResultsArray, Index, TEXT("unknown"), false, TEXT("Invalid operation object"));
			bAnyFailed = true;
			continue;
		}

		const TSharedPtr<FJsonObject>& Operation = *OperationObject;
		const FString Op = GetStringArgOrDefault(Operation, TEXT("op"));
		FString ErrorMessage;
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetStringField(TEXT("op"), Op);
		bool bSuccess = false;

		if (Op == TEXT("add_node"))
		{
			const FString GraphName = GetStringArgOrDefault(Operation, TEXT("graph_name"), TEXT("EventGraph"));
			const FString NodeClass = GetStringArgOrDefault(Operation, TEXT("node_class"));
			const FString Alias = GetStringArgOrDefault(Operation, TEXT("alias"));

			UEdGraph* TargetGraph = FMcpAssetModifier::FindGraphByName(Blueprint, GraphName);
			if (!TargetGraph)
			{
				ErrorMessage = FString::Printf(TEXT("Graph not found: %s"), *GraphName);
			}
			else if (NodeClass.IsEmpty())
			{
				ErrorMessage = TEXT("'node_class' is required for add_node");
			}
			else
			{
				FVector2D Position(0.0f, 0.0f);
				const TArray<TSharedPtr<FJsonValue>>* PositionArray = nullptr;
				if (Operation->TryGetArrayField(TEXT("position"), PositionArray) && PositionArray && PositionArray->Num() >= 2)
				{
					Position.X = (*PositionArray)[0]->AsNumber();
					Position.Y = (*PositionArray)[1]->AsNumber();
				}

				TSharedPtr<FJsonObject> Properties;
				const TSharedPtr<FJsonObject>* PropertiesPtr = nullptr;
				if (Operation->TryGetObjectField(TEXT("properties"), PropertiesPtr))
				{
					Properties = *PropertiesPtr;
				}

				TSharedPtr<FJsonObject> PinDefaults;
				const TSharedPtr<FJsonObject>* PinDefaultsPtr = nullptr;
				if (Operation->TryGetObjectField(TEXT("pin_defaults"), PinDefaultsPtr))
				{
					PinDefaults = *PinDefaultsPtr;
				}

				if (bDryRun)
				{
					if (!Alias.IsEmpty())
					{
						// P1-S2: dry-run 下用真 GUID 占位（不是零 GUID），并记录到 DryRunFakeAliases。
						AliasMap.Add(Alias, FGuid::NewGuid());
						DryRunFakeAliases.Add(Alias);
					}
					bSuccess = true;
				}
				else
				{
					UEdGraphNode* NewNode = UAddGraphNodeTool::CreateBlueprintNode(Blueprint, TargetGraph, NodeClass, Position, Properties, PinDefaults, ErrorMessage);
					if (NewNode)
					{
						FMcpEditorSessionManager::Get().RememberBlueprintNode(Context.SessionId, AssetPath, NewNode);
						if (!Alias.IsEmpty())
						{
							AliasMap.Add(Alias, NewNode->NodeGuid);
							ResultObject->SetStringField(TEXT("alias"), Alias);
						}
						ResultObject->SetStringField(TEXT("node_guid"), NewNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
						ResultObject->SetObjectField(TEXT("handle"), McpV2ToolUtils::MakeEntityHandle(TEXT("blueprint_node"), Context.SessionId, AssetPath, NewNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens), NewNode->GetNodeTitle(ENodeTitleType::ListView).ToString()));
						bSuccess = true;
					}
				}
			}
		}
		else if (Op == TEXT("remove_node"))
		{
			const FString NodeToken = GetStringArgOrDefault(Operation, TEXT("node"));
			FGuid NodeGuid;
			if (ResolveNodeToken(NodeToken, AliasMap, NodeGuid, ErrorMessage))
			{
				if (bDryRun)
				{
					bSuccess = true;
				}
				else
				{
					UEdGraph* FoundGraph = nullptr;
					UEdGraphNode* Node = FMcpEditorSessionManager::Get().ResolveBlueprintNode(Context.SessionId, Blueprint, NodeGuid, &FoundGraph);
					if (!Node || !FoundGraph)
					{
						ErrorMessage = TEXT("Node not found");
					}
					else
					{
						FoundGraph->RemoveNode(Node);
						FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
						FMcpAssetModifier::MarkPackageDirty(Blueprint);
						bSuccess = true;
					}
				}
			}
		}
		else if (Op == TEXT("move_node"))
		{
			const FString NodeToken = GetStringArgOrDefault(Operation, TEXT("node"));
			FGuid NodeGuid;
			if (ResolveNodeToken(NodeToken, AliasMap, NodeGuid, ErrorMessage))
			{
				const TArray<TSharedPtr<FJsonValue>>* PositionArray = nullptr;
				if (!Operation->TryGetArrayField(TEXT("position"), PositionArray) || !PositionArray || PositionArray->Num() < 2)
				{
					ErrorMessage = TEXT("'position' must be [x, y]");
				}
				else if (bDryRun)
				{
					bSuccess = true;
				}
				else
				{
					UEdGraphNode* Node = FMcpEditorSessionManager::Get().ResolveBlueprintNode(Context.SessionId, Blueprint, NodeGuid);
					if (!Node)
					{
						ErrorMessage = TEXT("Node not found");
					}
					else
					{
						Node->Modify();
						Node->NodePosX = static_cast<int32>((*PositionArray)[0]->AsNumber());
						Node->NodePosY = static_cast<int32>((*PositionArray)[1]->AsNumber());
						FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
						FMcpAssetModifier::MarkPackageDirty(Blueprint);
						bSuccess = true;
					}
				}
			}
		}
		else if (Op == TEXT("set_node_property"))
		{
			const FString NodeToken = GetStringArgOrDefault(Operation, TEXT("node"));
			const FString PropertyPath = GetStringArgOrDefault(Operation, TEXT("property_path"));
			const TSharedPtr<FJsonValue>* ValuePtr = Operation->Values.Find(TEXT("value"));
			FGuid NodeGuid;
			if (PropertyPath.IsEmpty())
			{
				ErrorMessage = TEXT("'property_path' is required");
			}
			else if (!ValuePtr || !(*ValuePtr).IsValid())
			{
				ErrorMessage = TEXT("'value' is required");
			}
			else if (ResolveNodeToken(NodeToken, AliasMap, NodeGuid, ErrorMessage))
			{
				if (bDryRun)
				{
					bSuccess = true;
				}
				else
				{
					UEdGraphNode* Node = FMcpEditorSessionManager::Get().ResolveBlueprintNode(Context.SessionId, Blueprint, NodeGuid);
					if (!Node)
					{
						ErrorMessage = TEXT("Node not found");
					}
					else
					{
						FProperty* Property = nullptr;
						void* Container = nullptr;
						if (!FMcpAssetModifier::FindPropertyByPath(Node, PropertyPath, Property, Container, ErrorMessage))
						{
							ErrorMessage = FString::Printf(TEXT("Failed to find property '%s': %s"), *PropertyPath, *ErrorMessage);
						}
						else if (!FMcpAssetModifier::SetPropertyFromJson(Property, Container, *ValuePtr, ErrorMessage))
						{
							ErrorMessage = FString::Printf(TEXT("Failed to set property '%s': %s"), *PropertyPath, *ErrorMessage);
						}
						else
						{
							FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
							FMcpAssetModifier::MarkPackageDirty(Blueprint);
							bSuccess = true;
						}
					}
				}
			}
		}
		else if (Op == TEXT("set_pin_default"))
		{
			const FString NodeToken = GetStringArgOrDefault(Operation, TEXT("node"));
			const FString PinName = GetStringArgOrDefault(Operation, TEXT("pin_name"));
			const TSharedPtr<FJsonValue>* ValuePtr = Operation->Values.Find(TEXT("value"));
			FGuid NodeGuid;
			if (PinName.IsEmpty())
			{
				ErrorMessage = TEXT("'pin_name' is required");
			}
			else if (!ValuePtr || !(*ValuePtr).IsValid())
			{
				ErrorMessage = TEXT("'value' is required");
			}
			else if (ResolveNodeToken(NodeToken, AliasMap, NodeGuid, ErrorMessage))
			{
				if (bDryRun)
				{
					bSuccess = true;
				}
				else
				{
					UEdGraphNode* Node = FMcpEditorSessionManager::Get().ResolveBlueprintNode(Context.SessionId, Blueprint, NodeGuid);
					if (!Node)
					{
						ErrorMessage = TEXT("Node not found");
					}
					else
					{
						TSharedPtr<FJsonObject> PinDefaults = MakeShareable(new FJsonObject);
						PinDefaults->SetStringField(PinName, JsonValueToLiteral(*ValuePtr));
						UAddGraphNodeTool::ApplyPinDefaults(Node, PinDefaults);
						FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
						FMcpAssetModifier::MarkPackageDirty(Blueprint);
						bSuccess = true;
					}
				}
			}
		}
		else if (Op == TEXT("connect") || Op == TEXT("disconnect"))
		{
			const FString SourceToken = GetStringArgOrDefault(Operation, TEXT("source_node"));
			const FString SourcePinName = GetStringArgOrDefault(Operation, TEXT("source_pin"), GetStringArgOrDefault(Operation, TEXT("pin_name")));
			const FString TargetToken = GetStringArgOrDefault(Operation, TEXT("target_node"));
			const FString TargetPinName = GetStringArgOrDefault(Operation, TEXT("target_pin"));
			FGuid SourceGuid;
			FGuid TargetGuid;

			if (!ResolveNodeToken(SourceToken, AliasMap, SourceGuid, ErrorMessage))
			{
				// Error already set
			}
			else if (SourcePinName.IsEmpty())
			{
				ErrorMessage = TEXT("'source_pin' (or 'pin_name') is required");
			}
			else if (Op == TEXT("connect") && (!ResolveNodeToken(TargetToken, AliasMap, TargetGuid, ErrorMessage) || TargetPinName.IsEmpty()))
			{
				if (ErrorMessage.IsEmpty())
				{
					ErrorMessage = TEXT("'target_node' and 'target_pin' are required");
				}
			}
			else if (bDryRun)
			{
				bSuccess = true;
			}
			else
			{
				UEdGraphNode* SourceNode = FMcpEditorSessionManager::Get().ResolveBlueprintNode(Context.SessionId, Blueprint, SourceGuid);
				UEdGraphNode* TargetNode = Op == TEXT("connect") || !TargetToken.IsEmpty()
					? FMcpEditorSessionManager::Get().ResolveBlueprintNode(Context.SessionId, Blueprint, TargetGuid)
					: nullptr;

				UEdGraphPin* SourcePin = FindPinByName(SourceNode, SourcePinName);
				if (!SourcePin)
				{
					ErrorMessage = TEXT("Source pin not found");
				}
				else if (Op == TEXT("connect"))
				{
					UEdGraphPin* TargetPin = FindPinByName(TargetNode, TargetPinName);
					if (!TargetPin)
					{
						ErrorMessage = TEXT("Target pin not found");
					}
					else
					{
						const UEdGraphSchema* Schema = SourceNode && SourceNode->GetGraph() ? SourceNode->GetGraph()->GetSchema() : nullptr;
						if (!Schema)
						{
							ErrorMessage = TEXT("Graph schema not available");
						}
						else
						{
							FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
							if (Response.Response == CONNECT_RESPONSE_DISALLOW)
							{
								ErrorMessage = Response.Message.ToString();
							}
							else
							{
								bSuccess = Schema->TryCreateConnection(SourcePin, TargetPin);
								if (!bSuccess)
								{
									ErrorMessage = TEXT("Failed to create pin connection");
								}
								else
								{
									FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
									FMcpAssetModifier::MarkPackageDirty(Blueprint);
								}
							}
						}
					}
				}
				else
				{
					if (TargetNode && !TargetPinName.IsEmpty())
					{
						UEdGraphPin* TargetPin = FindPinByName(TargetNode, TargetPinName);
						if (!TargetPin)
						{
							ErrorMessage = TEXT("Target pin not found");
						}
						else
						{
							SourcePin->BreakLinkTo(TargetPin);
							FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
							FMcpAssetModifier::MarkPackageDirty(Blueprint);
							bSuccess = true;
						}
					}
					else
					{
						SourcePin->BreakAllPinLinks();
						FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
						FMcpAssetModifier::MarkPackageDirty(Blueprint);
						bSuccess = true;
					}
				}
			}
		}
		else
		{
			ErrorMessage = FString::Printf(TEXT("Unsupported op: %s"), *Op);
		}

		ResultObject->SetBoolField(TEXT("success"), bSuccess);
		ResultObject->SetNumberField(TEXT("index"), Index);
		if (!bSuccess)
		{
			ResultObject->SetStringField(TEXT("error"), ErrorMessage);
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
		}
		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));

		if (bAnyFailed && bRollbackOnError)
		{
			if (Transaction.IsValid())
			{
				Transaction->Cancel();
				Transaction.Reset();
			}

			TSharedPtr<FJsonObject> PartialResult = MakeShareable(new FJsonObject);
			PartialResult->SetStringField(TEXT("asset_path"), AssetPath);
			PartialResult->SetArrayField(TEXT("results"), ResultsArray);
			PartialResult->SetArrayField(TEXT("partial_results"), PartialResultsArray);
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_GRAPH_EDIT_FAILED"), ErrorMessage, nullptr, PartialResult);
		}
	}

	if (!bDryRun && !bAnyFailed)
	{
		if (bCompile)
		{
			FString CompileError;
			if (!FMcpAssetModifier::CompileBlueprint(Blueprint, CompileError))
			{
				// P1-S3: 编译失败时主动 Cancel 事务，以免节点变动被默认提交留下难以滯销的 side-effects。
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
					Transaction.Reset();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_COMPILE_FAILED"), CompileError);
			}
		}

		if (bSave)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(Blueprint, false, SaveError))
			{
				// P1-S3: 保存失败时同样 Cancel 事务。
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
					Transaction.Reset();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_SAVE_FAILED"), SaveError);
			}
		}
	}

	TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
	Summary->SetNumberField(TEXT("total_operations"), OperationsArray->Num());
	Summary->SetNumberField(TEXT("failed_operations"), PartialResultsArray.Num());
	Summary->SetBoolField(TEXT("compiled"), !bDryRun && bCompile && !bAnyFailed);
	Summary->SetBoolField(TEXT("saved"), !bDryRun && bSave && !bAnyFailed);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetObjectField(TEXT("asset_handle"), McpV2ToolUtils::MakeAssetHandle(AssetPath, Blueprint->GetClass()->GetName()));
	Result->SetArrayField(TEXT("results"), ResultsArray);
	Result->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Result->SetObjectField(TEXT("summary"), Summary);
	return FMcpToolResult::StructuredSuccess(Result, bDryRun ? TEXT("Blueprint graph dry-run complete") : TEXT("Blueprint graph edit complete"));
}
