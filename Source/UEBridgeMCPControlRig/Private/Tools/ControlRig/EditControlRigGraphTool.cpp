// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/ControlRig/EditControlRigGraphTool.h"

#include "Utils/McpAssetModifier.h"

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
#include "ControlRigBlueprintLegacy.h"
#include "RigVMBlueprintLegacy.h"
#else
#include "ControlRigBlueprint.h"
#include "RigVMBlueprint.h"
#endif
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString NormalizeKey(const FString& InValue)
	{
		FString Result = InValue;
		Result.TrimStartAndEndInline();
		Result.ToLowerInline();
		return Result;
	}

	FString ResolveNodePath(const TSharedPtr<FJsonObject>& OperationObject, const TMap<FString, FString>& Aliases, const FString& NodeFieldName = TEXT("node_name"), const FString& AliasFieldName = TEXT("alias"))
	{
		if (!OperationObject.IsValid())
		{
			return FString();
		}

		FString NodeName;
		OperationObject->TryGetStringField(NodeFieldName, NodeName);
		if (!NodeName.IsEmpty())
		{
			return NodeName;
		}

		FString Alias;
		OperationObject->TryGetStringField(AliasFieldName, Alias);
		const FString* AliasNodePath = Aliases.Find(NormalizeKey(Alias));
		return AliasNodePath ? *AliasNodePath : FString();
	}

	bool TrySerializeJsonValue(const TSharedPtr<FJsonValue>& Value, FString& OutValue)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		switch (Value->Type)
		{
		case EJson::String:
			OutValue = Value->AsString();
			return true;
		case EJson::Number:
			OutValue = LexToString(Value->AsNumber());
			return true;
		case EJson::Boolean:
			OutValue = Value->AsBool() ? TEXT("True") : TEXT("False");
			return true;
		case EJson::Object:
		{
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutValue);
			return FJsonSerializer::Serialize(Value->AsObject().ToSharedRef(), Writer);
		}
		case EJson::Array:
		{
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutValue);
			return FJsonSerializer::Serialize(Value->AsArray(), Writer);
		}
		default:
			return false;
		}
	}

	FString ResolvePinPath(
		const TSharedPtr<FJsonObject>& OperationObject,
		const TMap<FString, FString>& Aliases,
		const FString& ExplicitFieldName,
		const FString& NodeFieldName,
		const FString& AliasFieldName,
		const FString& PinNameFieldName)
	{
		if (!OperationObject.IsValid())
		{
			return FString();
		}

		FString ExplicitPinPath;
		OperationObject->TryGetStringField(ExplicitFieldName, ExplicitPinPath);
		if (!ExplicitPinPath.IsEmpty())
		{
			return ExplicitPinPath;
		}

		const FString NodePath = ResolveNodePath(OperationObject, Aliases, NodeFieldName, AliasFieldName);
		FString PinName;
		OperationObject->TryGetStringField(PinNameFieldName, PinName);
		if (NodePath.IsEmpty() || PinName.IsEmpty())
		{
			return FString();
		}

		return FString::Printf(TEXT("%s.%s"), *NodePath, *PinName);
	}
}

FString UEditControlRigGraphTool::GetToolDescription() const
{
	return TEXT("Edit the main Control Rig graph by adding/removing units, setting pin defaults, linking pins, and laying out nodes.");
}

TMap<FString, FMcpSchemaProperty> UEditControlRigGraphTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), FMcpSchemaProperty::Make(TEXT("string"), TEXT("Control Rig Blueprint asset path"), true));

	TSharedPtr<FMcpSchemaProperty> OperationSchema = MakeShared<FMcpSchemaProperty>();
	OperationSchema->Type = TEXT("object");
	OperationSchema->Description = TEXT("Control Rig graph edit operation");
	OperationSchema->NestedRequired = { TEXT("action") };
	OperationSchema->Properties.Add(TEXT("action"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::MakeEnum(
		TEXT("Control Rig graph action"),
		{ TEXT("add_unit"), TEXT("remove_unit"), TEXT("set_pin_default"), TEXT("connect_pins"), TEXT("disconnect_pins"), TEXT("layout_nodes") },
		true)));
	OperationSchema->Properties.Add(TEXT("alias"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Per-call alias for a node"))));
	OperationSchema->Properties.Add(TEXT("node_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Existing or requested node name"))));
	OperationSchema->Properties.Add(TEXT("unit_struct_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Rig unit struct path for add_unit"))));
	OperationSchema->Properties.Add(TEXT("method_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Optional unit method name"))));
	OperationSchema->Properties.Add(TEXT("x"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Optional node X position"))));
	OperationSchema->Properties.Add(TEXT("y"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Optional node Y position"))));
	OperationSchema->Properties.Add(TEXT("pin_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Explicit target pin path"))));
	OperationSchema->Properties.Add(TEXT("pin_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Pin name when composing a target pin path from node_name/alias"))));
	OperationSchema->Properties.Add(TEXT("value"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Pin default value; numbers and booleans are also accepted"))));
	OperationSchema->Properties.Add(TEXT("from_pin_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Explicit output pin path"))));
	OperationSchema->Properties.Add(TEXT("from_alias"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Alias of the source node"))));
	OperationSchema->Properties.Add(TEXT("from_node_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Source node name"))));
	OperationSchema->Properties.Add(TEXT("from_pin_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Source pin name"))));
	OperationSchema->Properties.Add(TEXT("to_pin_path"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Explicit input pin path"))));
	OperationSchema->Properties.Add(TEXT("to_alias"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Alias of the target node"))));
	OperationSchema->Properties.Add(TEXT("to_node_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target node name"))));
	OperationSchema->Properties.Add(TEXT("to_pin_name"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("string"), TEXT("Target pin name"))));
	OperationSchema->Properties.Add(TEXT("columns"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("integer"), TEXT("Grid column count for layout_nodes"))));
	OperationSchema->Properties.Add(TEXT("spacing_x"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Horizontal spacing for layout_nodes"))));
	OperationSchema->Properties.Add(TEXT("spacing_y"), MakeShared<FMcpSchemaProperty>(FMcpSchemaProperty::Make(TEXT("number"), TEXT("Vertical spacing for layout_nodes"))));

	FMcpSchemaProperty OperationsSchema;
	OperationsSchema.Type = TEXT("array");
	OperationsSchema.Description = TEXT("Control Rig graph edit operations");
	OperationsSchema.Items = OperationSchema;
	Schema.Add(TEXT("operations"), OperationsSchema);
	Schema.Add(TEXT("dry_run"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Validate only")));
	Schema.Add(TEXT("save"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Save the Control Rig Blueprint asset")));
	Schema.Add(TEXT("compile"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Compile the Control Rig Blueprint after edits")));
	Schema.Add(TEXT("rollback_on_error"), FMcpSchemaProperty::Make(TEXT("boolean"), TEXT("Cancel the transaction on the first failure")));
	return Schema;
}

FMcpToolResult UEditControlRigGraphTool::Execute(const TSharedPtr<FJsonObject>& Arguments, const FMcpToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const bool bDryRun = GetBoolArgOrDefault(Arguments, TEXT("dry_run"), false);
	const bool bSave = GetBoolArgOrDefault(Arguments, TEXT("save"), true);
	const bool bCompile = GetBoolArgOrDefault(Arguments, TEXT("compile"), true);
	const bool bRollbackOnError = GetBoolArgOrDefault(Arguments, TEXT("rollback_on_error"), true);

	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Arguments->TryGetArrayField(TEXT("operations"), Operations) || !Operations || Operations->Num() == 0)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_MISSING_REQUIRED_FIELD"), TEXT("'operations' array is required"));
	}

	FString LoadError;
	UControlRigBlueprint* ControlRigBlueprint = FMcpAssetModifier::LoadAssetByPath<UControlRigBlueprint>(AssetPath, LoadError);
	if (!ControlRigBlueprint)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_ASSET_NOT_FOUND"), LoadError);
	}

	URigVMGraph* Graph = ControlRigBlueprint->GetDefaultModel();
	URigVMController* Controller = Graph ? ControlRigBlueprint->GetOrCreateController(Graph) : nullptr;
	if (!Graph || !Controller)
	{
		return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ASSET_TYPE"), TEXT("Failed to resolve the main Control Rig graph/controller"));
	}

	TSharedPtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = FMcpAssetModifier::BeginTransaction(FText::FromString(TEXT("Edit Control Rig Graph")));
		ControlRigBlueprint->UBlueprint::Modify();
	}

	TMap<FString, FString> Aliases;
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TArray<TSharedPtr<FJsonValue>> WarningsArray;
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedAssetsArray;
	TArray<TSharedPtr<FJsonValue>> PartialResultsArray;
	bool bAnyFailed = false;
	bool bAnyChanged = false;

	for (int32 OperationIndex = 0; OperationIndex < Operations->Num(); ++OperationIndex)
	{
		const TSharedPtr<FJsonObject>* OperationObject = nullptr;
		if (!(*Operations)[OperationIndex].IsValid() || !(*Operations)[OperationIndex]->TryGetObject(OperationObject) || !OperationObject || !(*OperationObject).IsValid())
		{
			return FMcpToolResult::StructuredError(TEXT("UEBMCP_INVALID_ARG"), FString::Printf(TEXT("operations[%d] must be an object"), OperationIndex));
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetNumberField(TEXT("index"), OperationIndex);

		FString Action = GetStringArgOrDefault(*OperationObject, TEXT("action")).ToLower();
		ResultObject->SetStringField(TEXT("action"), Action);
		ResultObject->SetBoolField(TEXT("changed"), false);

		FString ErrorMessage;
		bool bOperationChanged = false;

		if (Action == TEXT("add_unit"))
		{
			const FString UnitStructPath = GetStringArgOrDefault(*OperationObject, TEXT("unit_struct_path"));
			if (UnitStructPath.IsEmpty())
			{
				ErrorMessage = TEXT("'unit_struct_path' is required for add_unit");
			}
			else
			{
				const FVector2D Position(
					GetFloatArgOrDefault(*OperationObject, TEXT("x"), 0.0f),
					GetFloatArgOrDefault(*OperationObject, TEXT("y"), 0.0f));
				const FString RequestedNodeName = GetStringArgOrDefault(*OperationObject, TEXT("node_name"));
				const FString MethodName = GetStringArgOrDefault(*OperationObject, TEXT("method_name"), TEXT("Execute"));
				FString ResolvedNodePath = RequestedNodeName.IsEmpty()
					? FString::Printf(TEXT("RigUnit_%d"), OperationIndex)
					: RequestedNodeName;

				if (!bDryRun)
				{
					URigVMUnitNode* Node = Controller->AddUnitNodeFromStructPath(UnitStructPath, FName(*MethodName), Position, RequestedNodeName, true, false);
					if (!Node)
					{
						ErrorMessage = FString::Printf(TEXT("Failed to add Control Rig unit from '%s'"), *UnitStructPath);
					}
					else
					{
						ResolvedNodePath = Node->GetNodePath();
					}
				}

				if (ErrorMessage.IsEmpty())
				{
					const FString Alias = GetStringArgOrDefault(*OperationObject, TEXT("alias"));
					if (!Alias.IsEmpty())
					{
						Aliases.Add(NormalizeKey(Alias), ResolvedNodePath);
					}

					ResultObject->SetStringField(TEXT("node_path"), ResolvedNodePath);
					bOperationChanged = true;
				}
			}
		}
		else if (Action == TEXT("remove_unit"))
		{
			const FString NodePath = ResolveNodePath(*OperationObject, Aliases);
			if (NodePath.IsEmpty())
			{
				ErrorMessage = TEXT("'node_name' or 'alias' is required for remove_unit");
			}
			else if (!bDryRun)
			{
				URigVMNode* Node = Graph->FindNode(NodePath);
				if (!Node)
				{
					Node = Graph->FindNodeByName(FName(*NodePath));
				}

				if (!Node || !Controller->RemoveNode(Node, true, false))
				{
					ErrorMessage = FString::Printf(TEXT("Failed to remove node '%s'"), *NodePath);
				}
			}

			if (ErrorMessage.IsEmpty())
			{
				ResultObject->SetStringField(TEXT("node_path"), NodePath);
				bOperationChanged = true;
			}
		}
		else if (Action == TEXT("set_pin_default"))
		{
			const FString PinPath = ResolvePinPath(*OperationObject, Aliases, TEXT("pin_path"), TEXT("node_name"), TEXT("alias"), TEXT("pin_name"));
			FString DefaultValue;
			const TSharedPtr<FJsonValue>* ValuePtr = (*OperationObject)->Values.Find(TEXT("value"));
			if (PinPath.IsEmpty())
			{
				ErrorMessage = TEXT("'pin_path' or ('node_name'/'alias' + 'pin_name') is required for set_pin_default");
			}
			else if (!ValuePtr || !TrySerializeJsonValue(*ValuePtr, DefaultValue))
			{
				ErrorMessage = TEXT("'value' is required for set_pin_default");
			}
			else if (!bDryRun && !Controller->SetPinDefaultValue(PinPath, DefaultValue, true, true, false, false, true))
			{
				ErrorMessage = FString::Printf(TEXT("Failed to set pin default for '%s'"), *PinPath);
			}

			if (ErrorMessage.IsEmpty())
			{
				ResultObject->SetStringField(TEXT("pin_path"), PinPath);
				ResultObject->SetStringField(TEXT("value"), DefaultValue);
				bOperationChanged = true;
			}
		}
		else if (Action == TEXT("connect_pins") || Action == TEXT("disconnect_pins"))
		{
			const FString FromPinPath = ResolvePinPath(*OperationObject, Aliases, TEXT("from_pin_path"), TEXT("from_node_name"), TEXT("from_alias"), TEXT("from_pin_name"));
			const FString ToPinPath = ResolvePinPath(*OperationObject, Aliases, TEXT("to_pin_path"), TEXT("to_node_name"), TEXT("to_alias"), TEXT("to_pin_name"));

			if (FromPinPath.IsEmpty() || ToPinPath.IsEmpty())
			{
				ErrorMessage = TEXT("Both source and target pin paths are required");
			}
			else if (!bDryRun)
			{
				const bool bSuccess = (Action == TEXT("connect_pins"))
					? Controller->AddLink(FromPinPath, ToPinPath, true, false)
					: Controller->BreakLink(FromPinPath, ToPinPath, true, false);
				if (!bSuccess)
				{
					ErrorMessage = FString::Printf(TEXT("Failed to %s '%s' -> '%s'"), Action == TEXT("connect_pins") ? TEXT("connect") : TEXT("disconnect"), *FromPinPath, *ToPinPath);
				}
			}

			if (ErrorMessage.IsEmpty())
			{
				ResultObject->SetStringField(TEXT("from_pin_path"), FromPinPath);
				ResultObject->SetStringField(TEXT("to_pin_path"), ToPinPath);
				bOperationChanged = true;
			}
		}
		else if (Action == TEXT("layout_nodes"))
		{
			const int32 Columns = FMath::Max(1, GetIntArgOrDefault(*OperationObject, TEXT("columns"), 4));
			const float SpacingX = GetFloatArgOrDefault(*OperationObject, TEXT("spacing_x"), 360.0f);
			const float SpacingY = GetFloatArgOrDefault(*OperationObject, TEXT("spacing_y"), 220.0f);

			if (!bDryRun)
			{
				const TArray<URigVMNode*>& Nodes = Graph->GetNodes();
				for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
				{
					URigVMNode* Node = Nodes[NodeIndex];
					if (!Node)
					{
						continue;
					}

					const FVector2D Position(
						static_cast<float>(NodeIndex % Columns) * SpacingX,
						static_cast<float>(NodeIndex / Columns) * SpacingY);
					Controller->SetNodePosition(Node, Position, true, NodeIndex > 0, false);
				}
			}

			ResultObject->SetNumberField(TEXT("node_count"), Graph->GetNodes().Num());
			ResultObject->SetNumberField(TEXT("columns"), Columns);
			bOperationChanged = Graph->GetNodes().Num() > 0;
		}
		else
		{
			ErrorMessage = FString::Printf(TEXT("Unsupported action: '%s'"), *Action);
		}

		if (!ErrorMessage.IsEmpty())
		{
			ResultObject->SetStringField(TEXT("error"), ErrorMessage);
			bAnyFailed = true;
			PartialResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));

			if (bRollbackOnError && !bDryRun && Transaction.IsValid())
			{
				Transaction->Cancel();
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_OPERATION_FAILED"), ErrorMessage, ResultObject);
			}
		}
		else
		{
			ResultObject->SetBoolField(TEXT("changed"), bOperationChanged);
			bAnyChanged |= bOperationChanged;
		}

		ResultsArray.Add(MakeShareable(new FJsonValueObject(ResultObject)));
	}

	if (!bDryRun && bAnyChanged)
	{
		ControlRigBlueprint->RecompileVMIfRequired();

		if (bCompile)
		{
			FString CompileError;
			if (!FMcpAssetModifier::CompileBlueprint(ControlRigBlueprint, CompileError))
			{
				if (Transaction.IsValid() && bRollbackOnError)
				{
					Transaction->Cancel();
				}
				return FMcpToolResult::StructuredError(TEXT("UEBMCP_BLUEPRINT_COMPILE_FAILED"), CompileError);
			}
		}

		FMcpAssetModifier::MarkPackageDirty(ControlRigBlueprint);

		TSharedPtr<FJsonObject> ModifiedAsset = MakeShareable(new FJsonObject);
		ModifiedAsset->SetStringField(TEXT("asset_path"), AssetPath);
		ModifiedAsset->SetStringField(TEXT("class_name"), ControlRigBlueprint->GetClass()->GetName());
		ModifiedAssetsArray.Add(MakeShareable(new FJsonValueObject(ModifiedAsset)));

		if (bSave)
		{
			FString SaveError;
			if (!FMcpAssetModifier::SaveAsset(ControlRigBlueprint, false, SaveError))
			{
				if (Transaction.IsValid() && bRollbackOnError)
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
	Response->SetArrayField(TEXT("results"), ResultsArray);
	Response->SetArrayField(TEXT("warnings"), WarningsArray);
	Response->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	Response->SetArrayField(TEXT("modified_assets"), ModifiedAssetsArray);
	Response->SetArrayField(TEXT("partial_results"), PartialResultsArray);
	Response->SetBoolField(TEXT("dry_run"), bDryRun);
	return FMcpToolResult::StructuredJson(Response, bAnyFailed);
}
