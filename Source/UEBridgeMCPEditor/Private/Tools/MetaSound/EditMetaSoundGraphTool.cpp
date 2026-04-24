// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/MetaSound/EditMetaSoundGraphTool.h"

#include "Tools/MetaSound/MetaSoundToolUtils.h"
#include "Utils/McpAssetModifier.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MetasoundBuilderBase.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundSource.h"
#include "ScopedTransaction.h"

namespace
{
	TSharedPtr<FMcpSchemaProperty> MakeOperationSchema()
	{
		TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
		OperationSchema->Type = TEXT("object");
		OperationSchema->Description = TEXT("Restricted MetaSound graph operation");
		OperationSchema->NestedRequired = { TEXT("action") };
		OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
			TEXT("Graph operation"),
			{
				TEXT("add_graph_input"),
				TEXT("remove_graph_input"),
				TEXT("add_graph_output"),
				TEXT("remove_graph_output"),
				TEXT("add_node_by_class_name"),
				TEXT("connect_nodes"),
				TEXT("connect_graph_input_to_node"),
				TEXT("connect_node_to_graph_output"),
				TEXT("set_node_input_default"),
				TEXT("layout_graph")
			},
			true)));
		OperationSchema->Properties.Add(TEXT("name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Input/output name"))));
		OperationSchema->Properties.Add(TEXT("type"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(TEXT("Data type"), { TEXT("bool"), TEXT("int32"), TEXT("float"), TEXT("string"), TEXT("trigger"), TEXT("audio") })));
		OperationSchema->Properties.Add(TEXT("default"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("object"), TEXT("Default literal value"))));
		OperationSchema->Properties.Add(TEXT("node_id"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Node GUID"))));
		OperationSchema->Properties.Add(TEXT("source_node_id"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Source node GUID"))));
		OperationSchema->Properties.Add(TEXT("target_node_id"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target node GUID"))));
		OperationSchema->Properties.Add(TEXT("output_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Node output name"))));
		OperationSchema->Properties.Add(TEXT("input_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Node input name"))));
		OperationSchema->Properties.Add(TEXT("graph_input_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Graph input name"))));
		OperationSchema->Properties.Add(TEXT("graph_output_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Graph output name"))));
		OperationSchema->Properties.Add(TEXT("class_namespace"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("MetaSound node class namespace"))));
		OperationSchema->Properties.Add(TEXT("class_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("MetaSound node class name"))));
		OperationSchema->Properties.Add(TEXT("class_variant"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("MetaSound node class variant"))));
		OperationSchema->Properties.Add(TEXT("major_version"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("integer"), TEXT("MetaSound node class major version"))));
		return OperationSchema;
	}

	bool ParseNodeHandle(const FString& NodeId, FMetaSoundNodeHandle& OutHandle, FString& OutError)
	{
		FGuid Guid;
		if (NodeId.IsEmpty() || !FGuid::Parse(NodeId, Guid) || !Guid.IsValid())
		{
			OutError = FString::Printf(TEXT("Invalid MetaSound node_id '%s'"), *NodeId);
			return false;
		}
		OutHandle = FMetaSoundNodeHandle(Guid);
		return true;
	}
}

FString UEditMetaSoundGraphTool::GetToolDescription() const
{
	return TEXT("Perform restricted v1 MetaSound Source graph edits via the official builder API: graph I/O, class-name nodes, connections, input defaults, and layout.");
}

TMap<FString, FMcpSchemaProperty> UEditMetaSoundGraphTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("MetaSound Source asset path"), true));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("MetaSound graph operations");
	OperationsSchema.bRequired = true;
	OperationsSchema.Items = MakeOperationSchema();
	Schema.Add(TEXT("operations"), OperationsSchema);

	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only without mutating the asset")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on first failed operation")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the asset after successful edits")));
	return Schema;
}

FMcpToolResult UEditMetaSoundGraphTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	UMetaSoundSource* Source = nullptr;
	if (!MetaSoundToolUtils::TryLoadSource(AssetPath, Source, LoadError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	UMetaSoundBuilderBase* Builder = nullptr;
	FString BuildError;
	if (!MetaSoundToolUtils::TryBeginBuilding(Source, Builder, BuildError))
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_METASOUND_BUILDER_FAILED"), BuildError);
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit MetaSound Graph")));
		Source->Modify();
	}

	TArray<TSharedPtr<FJsonValue>> Results;
	TArray<TSharedPtr<FJsonValue>> Warnings;
	bool bAnyFailed = false;
	bool bAnyChanged = false;
	int32 Succeeded = 0;
	int32 Failed = 0;

	for (int32 Index = 0; Index < Operations->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* Operation = nullptr;
		if (!(*Operations)[Index].IsValid() || !(*Operations)[Index]->TryGetObject(Operation) || !Operation || !(*Operation).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), Index));
		}

		FString Action;
		(*Operation)->TryGetStringField(TEXT("action"), Action);

		TSharedPtr<FJsonObject> ItemResult = MakeShareable(new FJsonObject);
		ItemResult->SetNumberField(TEXT("index"), Index);
		ItemResult->SetStringField(TEXT("action"), Action);

		bool bSuccess = false;
		bool bChanged = false;
		FString Error;

		if (Action == TEXT("add_graph_input") || Action == TEXT("add_graph_output"))
		{
			FString Name;
			FString TypeName;
			(*Operation)->TryGetStringField(TEXT("name"), Name);
			(*Operation)->TryGetStringField(TEXT("type"), TypeName);
			ItemResult->SetStringField(TEXT("name"), Name);
			ItemResult->SetStringField(TEXT("type"), TypeName);

			FName DataType;
			FMetasoundFrontendLiteral DefaultLiteral;
			if (Name.IsEmpty() || TypeName.IsEmpty())
			{
				Error = TEXT("'name' and 'type' are required");
			}
			else if (!MetaSoundToolUtils::TryResolveDataType(TypeName, DataType, Error))
			{
				// Error populated.
			}
			else if (!MetaSoundToolUtils::TryReadLiteral(TypeName, (*Operation)->TryGetField(TEXT("default")), DefaultLiteral, Error))
			{
				// Error populated.
			}
			else if (bDryRun)
			{
				bSuccess = true;
			}
			else
			{
				EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
				if (Action == TEXT("add_graph_input"))
				{
					FMetaSoundBuilderNodeOutputHandle Handle = Builder->AddGraphInputNode(FName(*Name), DataType, DefaultLiteral, Result);
					ItemResult->SetObjectField(TEXT("output_handle"), MetaSoundToolUtils::SerializeOutputHandle(Handle));
				}
				else
				{
					FMetaSoundBuilderNodeInputHandle Handle = Builder->AddGraphOutputNode(FName(*Name), DataType, DefaultLiteral, Result);
					ItemResult->SetObjectField(TEXT("input_handle"), MetaSoundToolUtils::SerializeInputHandle(Handle));
				}
				bSuccess = Result == EMetaSoundBuilderResult::Succeeded;
				bChanged = bSuccess;
				ItemResult->SetStringField(TEXT("builder_result"), MetaSoundToolUtils::BuilderResultToString(Result));
				if (!bSuccess)
				{
					Error = FString::Printf(TEXT("MetaSound %s failed for '%s'"), *Action, *Name);
				}
			}
		}
		else if (Action == TEXT("remove_graph_input") || Action == TEXT("remove_graph_output"))
		{
			FString Name;
			(*Operation)->TryGetStringField(TEXT("name"), Name);
			ItemResult->SetStringField(TEXT("name"), Name);
			if (Name.IsEmpty())
			{
				Error = TEXT("'name' is required");
			}
			else if (bDryRun)
			{
				bSuccess = true;
			}
			else
			{
				EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
				if (Action == TEXT("remove_graph_input"))
				{
					Builder->RemoveGraphInput(FName(*Name), Result);
				}
				else
				{
					Builder->RemoveGraphOutput(FName(*Name), Result);
				}
				bSuccess = Result == EMetaSoundBuilderResult::Succeeded;
				bChanged = bSuccess;
				ItemResult->SetStringField(TEXT("builder_result"), MetaSoundToolUtils::BuilderResultToString(Result));
				if (!bSuccess)
				{
					Error = FString::Printf(TEXT("MetaSound %s failed for '%s'"), *Action, *Name);
				}
			}
		}
		else if (Action == TEXT("add_node_by_class_name"))
		{
			FString ClassNamespace;
			FString ClassName;
			FString ClassVariant;
			(*Operation)->TryGetStringField(TEXT("class_namespace"), ClassNamespace);
			(*Operation)->TryGetStringField(TEXT("class_name"), ClassName);
			(*Operation)->TryGetStringField(TEXT("class_variant"), ClassVariant);
			int32 MajorVersion = 1;
			(*Operation)->TryGetNumberField(TEXT("major_version"), MajorVersion);
			MajorVersion = FMath::Max(1, MajorVersion);

			if (ClassName.IsEmpty())
			{
				Error = TEXT("'class_name' is required for add_node_by_class_name");
			}
			else if (bDryRun)
			{
				bSuccess = true;
			}
			else
			{
				FMetasoundFrontendClassName FrontendClassName;
				FrontendClassName.Namespace = FName(*ClassNamespace);
				FrontendClassName.Name = FName(*ClassName);
				FrontendClassName.Variant = FName(*ClassVariant);

				EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
				FMetaSoundNodeHandle Handle = Builder->AddNodeByClassName(FrontendClassName, Result, MajorVersion);
				bSuccess = Result == EMetaSoundBuilderResult::Succeeded;
				bChanged = bSuccess;
				ItemResult->SetObjectField(TEXT("node_handle"), MetaSoundToolUtils::SerializeNodeHandle(Handle));
				ItemResult->SetStringField(TEXT("builder_result"), MetaSoundToolUtils::BuilderResultToString(Result));
				if (!bSuccess)
				{
					Error = FString::Printf(TEXT("Failed to add MetaSound node class '%s:%s:%s'"), *ClassNamespace, *ClassName, *ClassVariant);
				}
			}
		}
		else if (Action == TEXT("connect_nodes"))
		{
			FString SourceNodeId;
			FString TargetNodeId;
			FString OutputName;
			FString InputName;
			(*Operation)->TryGetStringField(TEXT("source_node_id"), SourceNodeId);
			(*Operation)->TryGetStringField(TEXT("target_node_id"), TargetNodeId);
			(*Operation)->TryGetStringField(TEXT("output_name"), OutputName);
			(*Operation)->TryGetStringField(TEXT("input_name"), InputName);

			FMetaSoundNodeHandle SourceHandle;
			FMetaSoundNodeHandle TargetHandle;
			if (!ParseNodeHandle(SourceNodeId, SourceHandle, Error) || !ParseNodeHandle(TargetNodeId, TargetHandle, Error))
			{
				// Error populated.
			}
			else if (OutputName.IsEmpty() || InputName.IsEmpty())
			{
				Error = TEXT("'output_name' and 'input_name' are required");
			}
			else if (bDryRun)
			{
				bSuccess = Builder->ContainsNode(SourceHandle) && Builder->ContainsNode(TargetHandle);
				if (!bSuccess)
				{
					Error = TEXT("Source or target node was not found");
				}
			}
			else
			{
				EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
				Builder->ConnectNodes(SourceHandle, FName(*OutputName), TargetHandle, FName(*InputName), Result);
				bSuccess = Result == EMetaSoundBuilderResult::Succeeded;
				bChanged = bSuccess;
				ItemResult->SetStringField(TEXT("builder_result"), MetaSoundToolUtils::BuilderResultToString(Result));
				if (!bSuccess)
				{
					Error = TEXT("Failed to connect MetaSound nodes");
				}
			}
		}
		else if (Action == TEXT("connect_graph_input_to_node"))
		{
			FString GraphInputName;
			FString TargetNodeId;
			FString InputName;
			(*Operation)->TryGetStringField(TEXT("graph_input_name"), GraphInputName);
			(*Operation)->TryGetStringField(TEXT("target_node_id"), TargetNodeId);
			(*Operation)->TryGetStringField(TEXT("input_name"), InputName);
			FMetaSoundNodeHandle TargetHandle;
			if (!ParseNodeHandle(TargetNodeId, TargetHandle, Error))
			{
				// Error populated.
			}
			else if (GraphInputName.IsEmpty() || InputName.IsEmpty())
			{
				Error = TEXT("'graph_input_name' and 'input_name' are required");
			}
			else if (bDryRun)
			{
				bSuccess = Builder->ContainsNode(TargetHandle);
			}
			else
			{
				EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
				Builder->ConnectGraphInputToNode(FName(*GraphInputName), TargetHandle, FName(*InputName), Result);
				bSuccess = Result == EMetaSoundBuilderResult::Succeeded;
				bChanged = bSuccess;
				ItemResult->SetStringField(TEXT("builder_result"), MetaSoundToolUtils::BuilderResultToString(Result));
				if (!bSuccess)
				{
					Error = TEXT("Failed to connect MetaSound graph input to node");
				}
			}
		}
		else if (Action == TEXT("connect_node_to_graph_output"))
		{
			FString SourceNodeId;
			FString OutputName;
			FString GraphOutputName;
			(*Operation)->TryGetStringField(TEXT("source_node_id"), SourceNodeId);
			(*Operation)->TryGetStringField(TEXT("output_name"), OutputName);
			(*Operation)->TryGetStringField(TEXT("graph_output_name"), GraphOutputName);
			FMetaSoundNodeHandle SourceHandle;
			if (!ParseNodeHandle(SourceNodeId, SourceHandle, Error))
			{
				// Error populated.
			}
			else if (OutputName.IsEmpty() || GraphOutputName.IsEmpty())
			{
				Error = TEXT("'output_name' and 'graph_output_name' are required");
			}
			else if (bDryRun)
			{
				bSuccess = Builder->ContainsNode(SourceHandle);
			}
			else
			{
				EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
				Builder->ConnectNodeToGraphOutput(SourceHandle, FName(*OutputName), FName(*GraphOutputName), Result);
				bSuccess = Result == EMetaSoundBuilderResult::Succeeded;
				bChanged = bSuccess;
				ItemResult->SetStringField(TEXT("builder_result"), MetaSoundToolUtils::BuilderResultToString(Result));
				if (!bSuccess)
				{
					Error = TEXT("Failed to connect MetaSound node to graph output");
				}
			}
		}
		else if (Action == TEXT("set_node_input_default"))
		{
			FString NodeId;
			FString InputName;
			FString TypeName;
			(*Operation)->TryGetStringField(TEXT("node_id"), NodeId);
			(*Operation)->TryGetStringField(TEXT("input_name"), InputName);
			(*Operation)->TryGetStringField(TEXT("type"), TypeName);
			FMetaSoundNodeHandle NodeHandle;
			FMetasoundFrontendLiteral Literal;
			if (!ParseNodeHandle(NodeId, NodeHandle, Error))
			{
				// Error populated.
			}
			else if (InputName.IsEmpty() || TypeName.IsEmpty())
			{
				Error = TEXT("'input_name' and 'type' are required");
			}
			else if (!MetaSoundToolUtils::TryReadLiteral(TypeName, (*Operation)->TryGetField(TEXT("default")), Literal, Error))
			{
				// Error populated.
			}
			else if (bDryRun)
			{
				bSuccess = Builder->ContainsNode(NodeHandle);
			}
			else
			{
				EMetaSoundBuilderResult FindResult = EMetaSoundBuilderResult::Failed;
				FMetaSoundBuilderNodeInputHandle InputHandle = Builder->FindNodeInputByName(NodeHandle, FName(*InputName), FindResult);
				if (FindResult == EMetaSoundBuilderResult::Succeeded)
				{
					EMetaSoundBuilderResult SetResult = EMetaSoundBuilderResult::Failed;
					Builder->SetNodeInputDefault(InputHandle, Literal, SetResult);
					bSuccess = SetResult == EMetaSoundBuilderResult::Succeeded;
					bChanged = bSuccess;
					ItemResult->SetStringField(TEXT("builder_result"), MetaSoundToolUtils::BuilderResultToString(SetResult));
				}
				if (!bSuccess)
				{
					Error = TEXT("Failed to set MetaSound node input default");
				}
			}
		}
		else if (Action == TEXT("layout_graph"))
		{
			if (!bDryRun)
			{
				Builder->InitNodeLocations();
			}
			bSuccess = true;
			bChanged = !bDryRun;
		}
		else
		{
			Error = FString::Printf(TEXT("Unsupported MetaSound graph operation '%s'"), *Action);
		}

		ItemResult->SetBoolField(TEXT("success"), bSuccess);
		ItemResult->SetBoolField(TEXT("changed"), bChanged);
		if (!Error.IsEmpty())
		{
			ItemResult->SetStringField(TEXT("error"), Error);
		}
		Results.Add(MakeShareable(new FJsonValueObject(ItemResult)));

		if (bSuccess)
		{
			++Succeeded;
			bAnyChanged = bAnyChanged || bChanged;
		}
		else
		{
			++Failed;
			bAnyFailed = true;
			if (bRollbackOnError)
			{
				if (Transaction.IsValid())
				{
					Transaction->Cancel();
					Transaction.Reset();
				}
				TSharedPtr<FJsonObject> Partial = MakeShareable(new FJsonObject);
				Partial->SetStringField(TEXT("tool"), GetToolName());
				Partial->SetArrayField(TEXT("results"), Results);
				return FMcpToolResult::StructuredError(
					TEXT("UEBMCP_METASOUND_GRAPH_EDIT_FAILED"),
					Error.IsEmpty() ? TEXT("MetaSound graph operation failed") : Error,
					nullptr,
					Partial);
			}
		}
	}

	if (!bDryRun && bAnyChanged)
	{
		FString FinalizeError;
		if (!MetaSoundToolUtils::BuildExistingSource(Source, Builder, FinalizeError))
		{
			if (Transaction.IsValid() && bRollbackOnError)
			{
				Transaction->Cancel();
				Transaction.Reset();
			}
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_METASOUND_BUILD_FAILED"), FinalizeError);
		}
		if (bSave)
		{
			MetaSoundToolUtils::SaveAsset(Source, Warnings);
		}
	}

	TSharedPtr<FJsonObject> Summary = MakeShareable(new FJsonObject);
	Summary->SetNumberField(TEXT("total"), Operations->Num());
	Summary->SetNumberField(TEXT("succeeded"), Succeeded);
	Summary->SetNumberField(TEXT("failed"), Failed);

	TSharedPtr<FJsonObject> Response = MetaSoundToolUtils::SerializeSourceSummary(Source, true);
	Response->SetStringField(TEXT("tool"), GetToolName());
	Response->SetBoolField(TEXT("success"), !bAnyFailed);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	Response->SetStringField(TEXT("asset_path"), AssetPath);
	Response->SetArrayField(TEXT("results"), Results);
	Response->SetObjectField(TEXT("summary"), Summary);
	Response->SetBoolField(TEXT("saved"), !bDryRun && bSave && Warnings.Num() == 0);
	Response->SetArrayField(TEXT("warnings"), Warnings);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
