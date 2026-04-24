#include "Tools/Blueprint/BlueprintCompileUtils.h"

#include "Tools/Blueprint/BlueprintToolUtils.h"
#include "Utils/McpAssetModifier.h"
#include "Utils/McpV2ToolUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphToken.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"

namespace
{
	FString SeverityToString(const EMessageSeverity::Type Severity)
	{
		switch (Severity)
		{
		case EMessageSeverity::Error:
			return TEXT("error");
		case EMessageSeverity::PerformanceWarning:
		case EMessageSeverity::Warning:
			return TEXT("warning");
		default:
			return TEXT("info");
		}
	}

	FString DefaultCodeForSeverity(const EMessageSeverity::Type Severity)
	{
		switch (Severity)
		{
		case EMessageSeverity::Error:
			return TEXT("UEBMCP_BLUEPRINT_COMPILE_ERROR");
		case EMessageSeverity::PerformanceWarning:
		case EMessageSeverity::Warning:
			return TEXT("UEBMCP_BLUEPRINT_COMPILE_WARNING");
		default:
			return TEXT("UEBMCP_BLUEPRINT_COMPILE_NOTE");
		}
	}

	bool ExtractTokenLocation(
		const TSharedPtr<IMessageToken>& Token,
		UEdGraph*& OutGraph,
		UEdGraphNode*& OutNode)
	{
		if (!Token.IsValid() || Token->GetType() != EMessageToken::EdGraph)
		{
			return false;
		}

		const TSharedPtr<FEdGraphToken> GraphToken = StaticCastSharedPtr<FEdGraphToken>(Token);
		if (!GraphToken.IsValid())
		{
			return false;
		}

		if (const UEdGraphPin* Pin = GraphToken->GetPin())
		{
			OutNode = Pin->GetOwningNode();
			OutGraph = OutNode ? OutNode->GetGraph() : nullptr;
			return OutNode != nullptr || OutGraph != nullptr;
		}

		const UObject* GraphObject = GraphToken->GetGraphObject();
		if (const UEdGraphNode* NodeObject = Cast<UEdGraphNode>(GraphObject))
		{
			OutNode = const_cast<UEdGraphNode*>(NodeObject);
			OutGraph = OutNode ? OutNode->GetGraph() : nullptr;
			return true;
		}

		if (const UEdGraph* GraphObjectAsGraph = Cast<UEdGraph>(GraphObject))
		{
			OutGraph = const_cast<UEdGraph*>(GraphObjectAsGraph);
			return true;
		}

		return false;
	}

	void PopulateLocationFields(
		const FString& AssetPath,
		const FString& SessionId,
		UEdGraph* Graph,
		UEdGraphNode* Node,
		TSharedPtr<FJsonObject>& Diagnostic)
	{
		if (Node)
		{
			const FString NodeGuid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
			Diagnostic->SetStringField(TEXT("node_guid"), NodeGuid);
			if (Node->GetGraph())
			{
				Diagnostic->SetStringField(TEXT("graph_name"), Node->GetGraph()->GetName());
			}
			if (!SessionId.IsEmpty())
			{
				Diagnostic->SetObjectField(
					TEXT("handle"),
					McpV2ToolUtils::MakeEntityHandle(
						TEXT("blueprint_node"),
						SessionId,
						AssetPath,
						NodeGuid,
						Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
			}
			return;
		}

		if (Graph)
		{
			Diagnostic->SetStringField(TEXT("graph_name"), Graph->GetName());
		}
	}

	FString GetFirstMessageBySeverity(const FCompilerResultsLog& Results, const EMessageSeverity::Type Severity)
	{
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			if (Message->GetSeverity() == Severity)
			{
				return Message->ToText().ToString();
			}
		}

		return FString();
	}

	void AddFallbackDiagnostic(
		const FString& AssetPath,
		const FString& Severity,
		const FString& Code,
		const FString& Message,
		BlueprintCompileUtils::FCompileReport& OutReport)
	{
		TSharedPtr<FJsonObject> Diagnostic = MakeShareable(new FJsonObject);
		Diagnostic->SetStringField(TEXT("severity"), Severity);
		Diagnostic->SetStringField(TEXT("code"), Code);
		Diagnostic->SetStringField(TEXT("message"), Message);
		Diagnostic->SetStringField(TEXT("asset_path"), AssetPath);
		OutReport.Diagnostics.Add(MakeShareable(new FJsonValueObject(Diagnostic)));
	}

	void SetTouchedGraphs(const TSet<FString>& GraphNames, TSharedPtr<FJsonObject>& OutActionResult)
	{
		TArray<FString> SortedGraphs = GraphNames.Array();
		SortedGraphs.Sort();

		TArray<TSharedPtr<FJsonValue>> TouchedGraphs;
		for (const FString& GraphName : SortedGraphs)
		{
			TouchedGraphs.Add(MakeShareable(new FJsonValueString(GraphName)));
		}

		OutActionResult->SetArrayField(TEXT("touched_graphs"), TouchedGraphs);
	}

	TMap<FString, TSet<FString>> CaptureInterfaceGraphs(const UBlueprint* Blueprint)
	{
		TMap<FString, TSet<FString>> Result;
		if (!Blueprint)
		{
			return Result;
		}

		for (const FBPInterfaceDescription& InterfaceDescription : Blueprint->ImplementedInterfaces)
		{
			const UClass* InterfaceClass = InterfaceDescription.Interface.Get();
			if (!InterfaceClass)
			{
				continue;
			}

			TSet<FString>& Graphs = Result.FindOrAdd(InterfaceClass->GetPathName());
			for (const TObjectPtr<UEdGraph>& Graph : InterfaceDescription.Graphs)
			{
				if (Graph)
				{
					Graphs.Add(Graph->GetName());
				}
			}
		}

		return Result;
	}

	void MarkBlueprintChanged(UBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FMcpAssetModifier::MarkPackageDirty(Blueprint);
	}

	bool ApplyRefreshAllNodes(UBlueprint* Blueprint, TSharedPtr<FJsonObject>& OutActionResult)
	{
		FMcpAssetModifier::RefreshBlueprintNodes(Blueprint);
		MarkBlueprintChanged(Blueprint);

		OutActionResult = MakeShareable(new FJsonObject);
		OutActionResult->SetStringField(TEXT("action"), TEXT("refresh_all_nodes"));
		OutActionResult->SetBoolField(TEXT("success"), true);
		return true;
	}

	bool ApplyReconstructInvalidNodes(UBlueprint* Blueprint, TSharedPtr<FJsonObject>& OutActionResult)
	{
		int32 ReconstructedNodeCount = 0;
		TArray<UEdGraph*> AllGraphs;
		FMcpAssetModifier::GetAllSearchableGraphs(Blueprint, AllGraphs);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node || !Node->HasDeprecatedReference())
				{
					continue;
				}

				Node->Modify();
				Node->ReconstructNode();
				++ReconstructedNodeCount;
			}
		}

		if (ReconstructedNodeCount > 0)
		{
			MarkBlueprintChanged(Blueprint);
		}

		OutActionResult = MakeShareable(new FJsonObject);
		OutActionResult->SetStringField(TEXT("action"), TEXT("reconstruct_invalid_nodes"));
		OutActionResult->SetBoolField(TEXT("success"), true);
		OutActionResult->SetNumberField(TEXT("reconstructed_nodes"), ReconstructedNodeCount);
		return true;
	}

	bool ApplyRemoveOrphanPins(UBlueprint* Blueprint, TSharedPtr<FJsonObject>& OutActionResult)
	{
		int32 RemovedPinCount = 0;
		TArray<UEdGraph*> AllGraphs;
		FMcpAssetModifier::GetAllSearchableGraphs(Blueprint, AllGraphs);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node)
				{
					continue;
				}

				TArray<UEdGraphPin*> PinsToRemove;
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin && Pin->bOrphanedPin)
					{
						PinsToRemove.Add(Pin);
					}
				}

				for (UEdGraphPin* Pin : PinsToRemove)
				{
					Node->Modify();
					Node->RemovePin(Pin);
					++RemovedPinCount;
				}
			}
		}

		if (RemovedPinCount > 0)
		{
			MarkBlueprintChanged(Blueprint);
		}

		OutActionResult = MakeShareable(new FJsonObject);
		OutActionResult->SetStringField(TEXT("action"), TEXT("remove_orphan_pins"));
		OutActionResult->SetBoolField(TEXT("success"), true);
		OutActionResult->SetNumberField(TEXT("removed_orphan_pins"), RemovedPinCount);
		return true;
	}

	bool ApplyRecompileDependencies(
		UBlueprint* Blueprint,
		const FString& SessionId,
		int32 MaxDiagnostics,
		TSharedPtr<FJsonObject>& OutActionResult,
		TArray<TSharedPtr<FJsonValue>>& OutDiagnostics,
		TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets)
	{
		TArray<UBlueprint*> DependentBlueprints;
		FBlueprintEditorUtils::GetDependentBlueprints(Blueprint, DependentBlueprints);

		TArray<TSharedPtr<FJsonValue>> DependencyResults;
		bool bAllDependenciesCompiled = true;

		for (UBlueprint* DependentBlueprint : DependentBlueprints)
		{
			if (!DependentBlueprint)
			{
				continue;
			}

			FMcpAssetModifier::MarkModified(DependentBlueprint);

			BlueprintCompileUtils::FCompileReport CompileReport;
			const bool bCompileSuccess = BlueprintCompileUtils::CompileBlueprintWithReport(
				DependentBlueprint,
				DependentBlueprint->GetPathName(),
				SessionId,
				MaxDiagnostics,
				CompileReport);

			TSharedPtr<FJsonObject> DependencyResult = MakeShareable(new FJsonObject);
			DependencyResult->SetStringField(TEXT("asset_path"), DependentBlueprint->GetPathName());
			DependencyResult->SetBoolField(TEXT("success"), bCompileSuccess);
			DependencyResult->SetNumberField(TEXT("warning_count"), CompileReport.WarningCount);
			DependencyResult->SetNumberField(TEXT("error_count"), CompileReport.ErrorCount);
			DependencyResult->SetArrayField(TEXT("diagnostics"), CompileReport.Diagnostics);
			if (!CompileReport.ErrorMessage.IsEmpty())
			{
				DependencyResult->SetStringField(TEXT("error"), CompileReport.ErrorMessage);
			}

			for (const TSharedPtr<FJsonValue>& DiagnosticValue : CompileReport.Diagnostics)
			{
				OutDiagnostics.Add(DiagnosticValue);
			}

			if (bCompileSuccess)
			{
				OutModifiedAssets.Add(MakeShareable(new FJsonValueString(DependentBlueprint->GetPathName())));
			}
			else
			{
				bAllDependenciesCompiled = false;
			}

			DependencyResults.Add(MakeShareable(new FJsonValueObject(DependencyResult)));
		}

		OutActionResult = MakeShareable(new FJsonObject);
		OutActionResult->SetStringField(TEXT("action"), TEXT("recompile_dependencies"));
		OutActionResult->SetBoolField(TEXT("success"), bAllDependenciesCompiled);
		OutActionResult->SetArrayField(TEXT("dependencies"), DependencyResults);
		return bAllDependenciesCompiled;
	}

	bool ApplyConformImplementedInterfaces(UBlueprint* Blueprint, TSharedPtr<FJsonObject>& OutActionResult)
	{
		for (const FBPInterfaceDescription& InterfaceDescription : Blueprint->ImplementedInterfaces)
		{
			if (UClass* InterfaceClass = InterfaceDescription.Interface.Get())
			{
				if (UBlueprint* InterfaceBlueprint = Cast<UBlueprint>(InterfaceClass->ClassGeneratedBy))
				{
					BlueprintCompileUtils::FCompileReport InterfaceCompileReport;
					BlueprintCompileUtils::CompileBlueprintWithReport(
						InterfaceBlueprint,
						InterfaceBlueprint->GetPathName(),
						FString(),
						0,
						InterfaceCompileReport);
				}
			}
		}

		const TMap<FString, TSet<FString>> GraphsBefore = CaptureInterfaceGraphs(Blueprint);

		FBlueprintEditorUtils::ConformImplementedInterfaces(Blueprint);
		MarkBlueprintChanged(Blueprint);

		const TMap<FString, TSet<FString>> GraphsAfter = CaptureInterfaceGraphs(Blueprint);
		TSet<FString> TouchedGraphs;
		int32 ConformedInterfaceCount = 0;

		for (const TPair<FString, TSet<FString>>& Pair : GraphsAfter)
		{
			const TSet<FString>* BeforeGraphs = GraphsBefore.Find(Pair.Key);
			bool bInterfaceConformed = false;

			for (const FString& GraphName : Pair.Value)
			{
				if (!BeforeGraphs || !BeforeGraphs->Contains(GraphName))
				{
					TouchedGraphs.Add(GraphName);
					bInterfaceConformed = true;
				}
			}

			if (bInterfaceConformed)
			{
				++ConformedInterfaceCount;
			}
		}

		OutActionResult = MakeShareable(new FJsonObject);
		OutActionResult->SetStringField(TEXT("action"), TEXT("conform_implemented_interfaces"));
		OutActionResult->SetBoolField(TEXT("success"), true);
		OutActionResult->SetNumberField(TEXT("conformed_interface_count"), ConformedInterfaceCount);
		SetTouchedGraphs(TouchedGraphs, OutActionResult);
		OutActionResult->SetArrayField(TEXT("implemented_interfaces"), BlueprintToolUtils::BuildImplementedInterfacesArray(Blueprint));
		return true;
	}
}

namespace BlueprintCompileUtils
{
	bool CompileBlueprintWithReport(
		UBlueprint* Blueprint,
		const FString& AssetPath,
		const FString& SessionId,
		int32 MaxDiagnostics,
		FCompileReport& OutReport)
	{
		OutReport = FCompileReport();
		OutReport.bAttempted = true;

		if (!Blueprint)
		{
			OutReport.bSuccess = false;
			OutReport.ErrorCount = 1;
			OutReport.ErrorMessage = TEXT("Cannot compile null Blueprint");
			if (MaxDiagnostics != 0)
			{
				AddFallbackDiagnostic(
					AssetPath,
					TEXT("error"),
					TEXT("UEBMCP_BLUEPRINT_COMPILE_ERROR"),
					OutReport.ErrorMessage,
					OutReport);
			}
			return false;
		}

		FCompilerResultsLog Results;
		Results.bSilentMode = true;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Results);

		OutReport.ErrorCount = Results.NumErrors;
		OutReport.WarningCount = Results.NumWarnings;

		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			if (MaxDiagnostics >= 0 && OutReport.Diagnostics.Num() >= MaxDiagnostics)
			{
				break;
			}

			const EMessageSeverity::Type Severity = Message->GetSeverity();
			TSharedPtr<FJsonObject> Diagnostic = MakeShareable(new FJsonObject);
			Diagnostic->SetStringField(TEXT("severity"), SeverityToString(Severity));
			Diagnostic->SetStringField(TEXT("code"), DefaultCodeForSeverity(Severity));
			Diagnostic->SetStringField(TEXT("message"), Message->ToText().ToString());
			Diagnostic->SetStringField(TEXT("asset_path"), AssetPath);

			UEdGraph* Graph = nullptr;
			UEdGraphNode* Node = nullptr;
			if (!ExtractTokenLocation(Message->GetMessageLink(), Graph, Node))
			{
				for (const TSharedRef<IMessageToken>& Token : Message->GetMessageTokens())
				{
					if (ExtractTokenLocation(Token, Graph, Node))
					{
						break;
					}
				}
			}

			PopulateLocationFields(AssetPath, SessionId, Graph, Node, Diagnostic);
			OutReport.Diagnostics.Add(MakeShareable(new FJsonValueObject(Diagnostic)));
		}

		if (Blueprint->Status == BS_UpToDateWithWarnings && OutReport.WarningCount == 0)
		{
			OutReport.WarningCount = 1;
			if (MaxDiagnostics != 0)
			{
				AddFallbackDiagnostic(
					AssetPath,
					TEXT("warning"),
					TEXT("UEBMCP_BLUEPRINT_COMPILE_WARNING"),
					TEXT("Blueprint compiled with warnings"),
					OutReport);
			}
		}

		OutReport.bSuccess = Blueprint->Status != BS_Error && OutReport.ErrorCount == 0;
		if (!OutReport.bSuccess)
		{
			OutReport.ErrorMessage = GetFirstMessageBySeverity(Results, EMessageSeverity::Error);
			if (OutReport.ErrorMessage.IsEmpty())
			{
				OutReport.ErrorMessage = TEXT("Blueprint compilation failed with errors");
			}

			if (OutReport.Diagnostics.Num() == 0 && MaxDiagnostics != 0)
			{
				AddFallbackDiagnostic(
					AssetPath,
					TEXT("error"),
					TEXT("UEBMCP_BLUEPRINT_COMPILE_ERROR"),
					OutReport.ErrorMessage,
					OutReport);
			}
		}

		return OutReport.bSuccess;
	}

	TSharedPtr<FJsonObject> MakeCompileReportJson(const FCompileReport& Report)
	{
		TSharedPtr<FJsonObject> CompileObject = MakeShareable(new FJsonObject);
		CompileObject->SetBoolField(TEXT("attempted"), Report.bAttempted);
		CompileObject->SetBoolField(TEXT("success"), Report.bSuccess);
		CompileObject->SetNumberField(TEXT("warning_count"), Report.WarningCount);
		CompileObject->SetNumberField(TEXT("error_count"), Report.ErrorCount);
		CompileObject->SetArrayField(TEXT("diagnostics"), Report.Diagnostics);
		if (!Report.ErrorMessage.IsEmpty())
		{
			CompileObject->SetStringField(TEXT("error"), Report.ErrorMessage);
		}
		return CompileObject;
	}

	bool IsSupportedFixupAction(const FString& Action, const bool bAllowConformInterfaces)
	{
		return Action == TEXT("refresh_all_nodes")
			|| Action == TEXT("reconstruct_invalid_nodes")
			|| Action == TEXT("remove_orphan_pins")
			|| Action == TEXT("recompile_dependencies")
			|| (bAllowConformInterfaces && Action == TEXT("conform_implemented_interfaces"));
	}

	bool ApplyFixupAction(
		UBlueprint* Blueprint,
		const FString& Action,
		const FString& AssetPath,
		const FString& SessionId,
		const int32 MaxDiagnostics,
		TSharedPtr<FJsonObject>& OutActionResult,
		FString& OutError,
		TArray<TSharedPtr<FJsonValue>>& OutDiagnostics,
		TArray<TSharedPtr<FJsonValue>>& OutModifiedAssets)
	{
		if (!Blueprint)
		{
			OutError = TEXT("Blueprint is null");
			return false;
		}

		bool bSuccess = false;
		if (Action == TEXT("refresh_all_nodes"))
		{
			bSuccess = ApplyRefreshAllNodes(Blueprint, OutActionResult);
		}
		else if (Action == TEXT("reconstruct_invalid_nodes"))
		{
			bSuccess = ApplyReconstructInvalidNodes(Blueprint, OutActionResult);
		}
		else if (Action == TEXT("remove_orphan_pins"))
		{
			bSuccess = ApplyRemoveOrphanPins(Blueprint, OutActionResult);
		}
		else if (Action == TEXT("recompile_dependencies"))
		{
			bSuccess = ApplyRecompileDependencies(Blueprint, SessionId, MaxDiagnostics, OutActionResult, OutDiagnostics, OutModifiedAssets);
		}
		else if (Action == TEXT("conform_implemented_interfaces"))
		{
			bSuccess = ApplyConformImplementedInterfaces(Blueprint, OutActionResult);
		}
		else
		{
			OutError = FString::Printf(TEXT("Unsupported action '%s'"), *Action);
			return false;
		}

		if (bSuccess
			&& Action != TEXT("recompile_dependencies")
			&& !AssetPath.IsEmpty())
		{
			OutModifiedAssets.Add(MakeShareable(new FJsonValueString(AssetPath)));
		}

		return bSuccess;
	}
}
