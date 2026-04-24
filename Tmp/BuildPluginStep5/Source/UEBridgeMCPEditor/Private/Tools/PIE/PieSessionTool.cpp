// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/PIE/PieSessionTool.h"
#include "Utils/McpAssetModifier.h"
#include "UEBridgeMCPEditor.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "LevelEditorSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Guid.h"
#include "EngineUtils.h"

// 静态成员初始化
bool UPieSessionTool::bPIETransitioning = false;
FDelegateHandle UPieSessionTool::PIEStartedHandle;
FDelegateHandle UPieSessionTool::PIEEndedHandle;

void UPieSessionTool::RegisterPIECallbacks()
{
	// 避免重复注册
	UnregisterPIECallbacks();

	// PIE 启动完成回调：世界已完全创建，清除过渡标志
	PIEStartedHandle = FEditorDelegates::PostPIEStarted.AddLambda([](bool bIsSimulating)
	{
		UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-session: PIE started callback - clearing transition flag"));
		bPIETransitioning = false;
	});

	// PIE 结束完成回调：世界已完全销毁，清除过渡标志
	PIEEndedHandle = FEditorDelegates::EndPIE.AddLambda([](bool bIsSimulating)
	{
		UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-session: PIE ended callback - clearing transition flag"));
		bPIETransitioning = false;
	});
}

void UPieSessionTool::UnregisterPIECallbacks()
{
	if (PIEStartedHandle.IsValid())
	{
		FEditorDelegates::PostPIEStarted.Remove(PIEStartedHandle);
		PIEStartedHandle.Reset();
	}
	if (PIEEndedHandle.IsValid())
	{
		FEditorDelegates::EndPIE.Remove(PIEEndedHandle);
		PIEEndedHandle.Reset();
	}
}

FString UPieSessionTool::GetToolDescription() const
{
	return TEXT("Control PIE (Play-In-Editor) sessions. Actions: 'start', 'stop', 'pause', 'resume', 'get-state', 'wait-for' (non-blocking single check of condition, client polls).");
}

TMap<FString, FMcpSchemaProperty> UPieSessionTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	FMcpSchemaProperty Action;
	Action.Type = TEXT("string");
	Action.Description = TEXT("Action: 'start', 'stop', 'pause', 'resume', 'get-state', or 'wait-for'. Note: start/stop/wait-for are non-blocking (fire-and-forget), use get-state to poll.");
	Action.bRequired = true;
	Schema.Add(TEXT("action"), Action);

	FMcpSchemaProperty Mode;
	Mode.Type = TEXT("string");
	Mode.Description = TEXT("[start] PIE launch mode: 'viewport' (default), 'new_window', or 'standalone'");
	Mode.bRequired = false;
	Schema.Add(TEXT("mode"), Mode);

	FMcpSchemaProperty MapPath;
	MapPath.Type = TEXT("string");
	MapPath.Description = TEXT("[start] Optional map to load (e.g., '/Game/Maps/TestLevel')");
	MapPath.bRequired = false;
	Schema.Add(TEXT("map"), MapPath);

	FMcpSchemaProperty TimeoutSeconds;
	TimeoutSeconds.Type = TEXT("number");
	TimeoutSeconds.Description = TEXT("[start] Timeout in seconds to wait for PIE ready (default: 30)");
	TimeoutSeconds.bRequired = false;
	Schema.Add(TEXT("timeout"), TimeoutSeconds);

	FMcpSchemaProperty Include;
	Include.Type = TEXT("array");
	Include.Description = TEXT("[get-state] What to include: 'world', 'players'. Default: all.");
	Include.bRequired = false;
	Schema.Add(TEXT("include"), Include);

	// wait-for parameters
	FMcpSchemaProperty ActorName;
	ActorName.Type = TEXT("string");
	ActorName.Description = TEXT("[wait-for] Actor name to monitor");
	ActorName.bRequired = false;
	Schema.Add(TEXT("actor_name"), ActorName);

	FMcpSchemaProperty Property;
	Property.Type = TEXT("string");
	Property.Description = TEXT("[wait-for] Property name to check (e.g., 'Health', 'bIsDead')");
	Property.bRequired = false;
	Schema.Add(TEXT("property"), Property);

	FMcpSchemaProperty Operator;
	Operator.Type = TEXT("string");
	Operator.Description = TEXT("[wait-for] Comparison: 'equals', 'not_equals', 'less_than', 'greater_than', 'contains'");
	Operator.bRequired = false;
	Schema.Add(TEXT("operator"), Operator);

	FMcpSchemaProperty ExpectedValue;
	ExpectedValue.Type = TEXT("any");
	ExpectedValue.Description = TEXT("[wait-for] Expected value for comparison");
	ExpectedValue.bRequired = false;
	Schema.Add(TEXT("expected"), ExpectedValue);

	// wait_timeout 和 poll_interval 已移除：wait-for 现在是非阻塞的单次检查，客户端自行轮询并控制超时和间隔。

	return Schema;
}

FMcpToolResult UPieSessionTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	FString Action;
	GetStringArg(Arguments, TEXT("action"), Action);
	Action = Action.ToLower();

	if (Action == TEXT("start"))
	{
		return ExecuteStart(Arguments);
	}
	else if (Action == TEXT("stop"))
	{
		return ExecuteStop(Arguments);
	}
	else if (Action == TEXT("pause"))
	{
		return ExecutePause(Arguments);
	}
	else if (Action == TEXT("resume"))
	{
		return ExecuteResume(Arguments);
	}
	else if (Action == TEXT("get-state") || Action == TEXT("state") || Action == TEXT("status"))
	{
		return ExecuteGetState(Arguments);
	}
	else if (Action == TEXT("wait-for") || Action == TEXT("wait"))
	{
		return ExecuteWaitFor(Arguments);
	}
	else
	{
		return FMcpToolResult::Error(FString::Printf(
			TEXT("Unknown action: '%s'. Valid: start, stop, pause, resume, get-state, wait-for"), *Action));
	}
}

FMcpToolResult UPieSessionTool::ExecuteStart(const TSharedPtr<FJsonObject>& Arguments)
{
	FString Mode = GetStringArgOrDefault(Arguments, TEXT("mode"), TEXT("viewport"));
	FString MapPath = GetStringArgOrDefault(Arguments, TEXT("map"));
	float Timeout = GetFloatArgOrDefault(Arguments, TEXT("timeout"), 30.0f);

	UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-session: Starting PIE (mode=%s, map=%s)"),
		*Mode, MapPath.IsEmpty() ? TEXT("current") : *MapPath);

	// Check if PIE is already running
	if (GEditor->IsPlaySessionInProgress())
	{
		UWorld* PIEWorld = GetPIEWorld();
		if (PIEWorld)
		{
			TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("session_id"), GenerateSessionId());
			Result->SetStringField(TEXT("world_name"), PIEWorld->GetName());
			Result->SetStringField(TEXT("state"), TEXT("already_running"));
			return FMcpToolResult::Json(Result);
		}
	}

	// Load specific map if requested
	if (!MapPath.IsEmpty())
	{
		ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
		if (LevelEditorSubsystem && !LevelEditorSubsystem->LoadLevel(MapPath))
		{
			return FMcpToolResult::Error(FString::Printf(TEXT("Failed to load map: %s"), *MapPath));
		}
	}

	// Configure PIE settings
	FRequestPlaySessionParams Params;
	Params.WorldType = EPlaySessionWorldType::PlayInEditor;

	if (Mode.Equals(TEXT("new_window"), ESearchCase::IgnoreCase) ||
		Mode.Equals(TEXT("standalone"), ESearchCase::IgnoreCase))
	{
		Params.DestinationSlateViewport = nullptr;
	}
	else // viewport (default)
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedPtr<IAssetViewport> ActiveViewport = LevelEditorModule.GetFirstActiveViewport();
		if (ActiveViewport.IsValid())
		{
			Params.DestinationSlateViewport = ActiveViewport;
		}
	}

	// 设置 PIE 过渡标志并注册回调
	bPIETransitioning = true;
	RegisterPIECallbacks();

	GEditor->RequestPlaySession(Params);

	// Fire-and-forget：立即返回，不阻塞 GameThread 等待 PIE 就绪。
	// 客户端应通过 get-state action 轮询 PIE 状态直到 state 变为 "running"。
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("session_id"), GenerateSessionId());
	Result->SetStringField(TEXT("state"), TEXT("starting"));
	Result->SetStringField(TEXT("message"), TEXT("PIE start requested. Use get-state action to poll until state becomes 'running'. Other tools that access PIE world will be blocked until PIE is ready."));

	UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-session: Start requested (mode=%s), transition flag set"), *Mode);
	return FMcpToolResult::Json(Result);
}

FMcpToolResult UPieSessionTool::ExecuteStop(const TSharedPtr<FJsonObject>& Arguments)
{
	if (!GEditor->IsPlaySessionInProgress())
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("state"), TEXT("not_running"));
		return FMcpToolResult::Json(Result);
	}

	// 设置 PIE 过渡标志并注册回调
	bPIETransitioning = true;
	RegisterPIECallbacks();

	GEditor->RequestEndPlayMap();

	// Fire-and-forget：立即返回，不阻塞 GameThread 等待 PIE 完全停止。
	// 客户端应通过 get-state action 轮询 PIE 状态直到 state 变为 "not_running"。
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("state"), TEXT("stopping"));
	Result->SetStringField(TEXT("message"), TEXT("PIE stop requested. Use get-state action to poll until state becomes 'not_running'. Other tools that access PIE world will be blocked until PIE cleanup completes."));

	UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-session: Stop requested, transition flag set"));
	return FMcpToolResult::Json(Result);
}

FMcpToolResult UPieSessionTool::ExecutePause(const TSharedPtr<FJsonObject>& Arguments)
{
	if (!GEditor->IsPlaySessionInProgress())
	{
		return FMcpToolResult::Error(TEXT("No PIE session running"));
	}

	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		return FMcpToolResult::Error(TEXT("PIE world not found"));
	}

	if (PIEWorld->IsPaused())
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("paused"), true);
		Result->SetStringField(TEXT("message"), TEXT("Already paused"));
		return FMcpToolResult::Json(Result);
	}

	if (GEditor->PlayWorld)
	{
		GEditor->PlayWorld->bDebugPauseExecution = true;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("paused"), true);

	UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-session: Paused"));
	return FMcpToolResult::Json(Result);
}

FMcpToolResult UPieSessionTool::ExecuteResume(const TSharedPtr<FJsonObject>& Arguments)
{
	if (!GEditor->IsPlaySessionInProgress())
	{
		return FMcpToolResult::Error(TEXT("No PIE session running"));
	}

	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		return FMcpToolResult::Error(TEXT("PIE world not found"));
	}

	if (!PIEWorld->IsPaused())
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("paused"), false);
		Result->SetStringField(TEXT("message"), TEXT("Already running"));
		return FMcpToolResult::Json(Result);
	}

	if (GEditor->PlayWorld)
	{
		GEditor->PlayWorld->bDebugPauseExecution = false;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("paused"), false);

	UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-session: Resumed"));
	return FMcpToolResult::Json(Result);
}

FMcpToolResult UPieSessionTool::ExecuteGetState(const TSharedPtr<FJsonObject>& Arguments)
{
	TSet<FString> IncludeSet;
	if (Arguments->HasField(TEXT("include")))
	{
		const TArray<TSharedPtr<FJsonValue>>* IncludeArray;
		if (Arguments->TryGetArrayField(TEXT("include"), IncludeArray))
		{
			for (const TSharedPtr<FJsonValue>& Val : *IncludeArray)
			{
				IncludeSet.Add(Val->AsString().ToLower());
			}
		}
	}

	bool bIncludeWorld = IncludeSet.Num() == 0 || IncludeSet.Contains(TEXT("world"));
	bool bIncludePlayers = IncludeSet.Num() == 0 || IncludeSet.Contains(TEXT("players"));

	bool bRunning = GEditor->IsPlaySessionInProgress();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("running"), bRunning);

	if (!bRunning)
	{
		Result->SetStringField(TEXT("state"), TEXT("not_running"));
		return FMcpToolResult::Json(Result);
	}

	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		Result->SetStringField(TEXT("state"), TEXT("initializing"));
		return FMcpToolResult::Json(Result);
	}

	Result->SetStringField(TEXT("state"), TEXT("running"));
	Result->SetBoolField(TEXT("paused"), PIEWorld->IsPaused());

	if (bIncludeWorld)
	{
		Result->SetObjectField(TEXT("world"), GetWorldInfo(PIEWorld));
	}

	if (bIncludePlayers)
	{
		Result->SetArrayField(TEXT("players"), GetPlayersInfo(PIEWorld));
	}

	return FMcpToolResult::Json(Result);
}

FString UPieSessionTool::GenerateSessionId() const
{
	return FString::Printf(TEXT("pie_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short));
}

UWorld* UPieSessionTool::GetPIEWorld() const
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::PIE && WorldContext.World())
		{
			return WorldContext.World();
		}
	}
	return nullptr;
}

// WaitForPIEReady 已移除：该函数在 GameThread 上使用 while+Sleep 阻塞循环，
// 会导致编辑器卡死。ExecuteStart 已改为 Fire-and-forget 模式，不再需要此函数。
// 客户端应通过 get-state action 轮询 PIE 状态。

TSharedPtr<FJsonObject> UPieSessionTool::GetWorldInfo(UWorld* PIEWorld) const
{
	TSharedPtr<FJsonObject> WorldInfo = MakeShareable(new FJsonObject);
	WorldInfo->SetStringField(TEXT("name"), PIEWorld->GetName());
	WorldInfo->SetStringField(TEXT("map_name"), PIEWorld->GetMapName());
	WorldInfo->SetNumberField(TEXT("time_seconds"), PIEWorld->GetTimeSeconds());

	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(PIEWorld); It; ++It) { ActorCount++; }
	WorldInfo->SetNumberField(TEXT("actor_count"), ActorCount);

	return WorldInfo;
}

TArray<TSharedPtr<FJsonValue>> UPieSessionTool::GetPlayersInfo(UWorld* PIEWorld) const
{
	TArray<TSharedPtr<FJsonValue>> PlayersArray;

	int32 PlayerIndex = 0;
	for (FConstPlayerControllerIterator It = PIEWorld->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC) continue;

		TSharedPtr<FJsonObject> PlayerInfo = MakeShareable(new FJsonObject);
		PlayerInfo->SetNumberField(TEXT("player_index"), PlayerIndex);
		PlayerInfo->SetStringField(TEXT("controller_name"), PC->GetName());

		if (APawn* Pawn = PC->GetPawn())
		{
			PlayerInfo->SetStringField(TEXT("pawn_name"), Pawn->GetName());
			PlayerInfo->SetStringField(TEXT("pawn_class"), Pawn->GetClass()->GetName());

			FVector Loc = Pawn->GetActorLocation();
			TArray<TSharedPtr<FJsonValue>> LocArray;
			LocArray.Add(MakeShareable(new FJsonValueNumber(Loc.X)));
			LocArray.Add(MakeShareable(new FJsonValueNumber(Loc.Y)));
			LocArray.Add(MakeShareable(new FJsonValueNumber(Loc.Z)));
			PlayerInfo->SetArrayField(TEXT("location"), LocArray);

			PlayerInfo->SetNumberField(TEXT("speed"), Pawn->GetVelocity().Size());
		}

		PlayersArray.Add(MakeShareable(new FJsonValueObject(PlayerInfo)));
		PlayerIndex++;
	}

	return PlayersArray;
}

FMcpToolResult UPieSessionTool::ExecuteWaitFor(const TSharedPtr<FJsonObject>& Arguments)
{
	// Non-blocking 实现：只执行一次条件检查并立即返回当前状态。
	// 客户端应自行轮询此 action 直到 condition_met 为 true 或自行判断超时。
	// 这避免了在 GameThread 上使用 while+Sleep 阻塞循环导致编辑器卡死。

	if (!GEditor->IsPlaySessionInProgress())
	{
		return FMcpToolResult::Error(TEXT("No PIE session running"));
	}

	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		return FMcpToolResult::Error(TEXT("PIE world not found"));
	}

	FString ActorName = GetStringArgOrDefault(Arguments, TEXT("actor_name"));
	FString PropertyName = GetStringArgOrDefault(Arguments, TEXT("property"));
	FString Operator = GetStringArgOrDefault(Arguments, TEXT("operator"), TEXT("equals"));

	if (ActorName.IsEmpty())
	{
		return FMcpToolResult::Error(TEXT("actor_name is required for wait-for action"));
	}
	if (PropertyName.IsEmpty())
	{
		return FMcpToolResult::Error(TEXT("property is required for wait-for action"));
	}

	// Get expected value
	TSharedPtr<FJsonValue> ExpectedJson = Arguments->TryGetField(TEXT("expected"));
	if (!ExpectedJson.IsValid())
	{
		return FMcpToolResult::Error(TEXT("expected value is required for wait-for action"));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonValue> ActualValue;
	bool bConditionMet = false;

	// 查找 Actor
	AActor* Actor = FMcpAssetModifier::FindActorByName(PIEWorld, ActorName);
	if (!Actor)
	{
		// Actor 尚未存在，返回 not_found 状态，客户端可继续轮询
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("condition_met"), false);
		Result->SetStringField(TEXT("status"), TEXT("actor_not_found"));
		Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Actor '%s' not found yet. Poll again to retry."), *ActorName));
		return FMcpToolResult::Json(Result);
	}

	// 获取属性值
	ActualValue = GetActorProperty(Actor, PropertyName);
	if (!ActualValue.IsValid())
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("condition_met"), false);
		Result->SetStringField(TEXT("status"), TEXT("property_not_found"));
		Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Property '%s' not found on actor '%s'. Poll again to retry."), *PropertyName, *ActorName));
		return FMcpToolResult::Json(Result);
	}

	// 比较值（单次检查）
	bool bMatch = CompareJsonValues(ActualValue, ExpectedJson, Operator);

	if (bMatch)
	{
		bConditionMet = true;
		UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-session: wait-for condition met (%s.%s %s)"),
			*ActorName, *PropertyName, *Operator);
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("condition_met"), bConditionMet);
	Result->SetStringField(TEXT("status"), bConditionMet ? TEXT("condition_met") : TEXT("pending"));

	if (ActualValue.IsValid())
	{
		Result->SetField(TEXT("actual_value"), ActualValue);
	}

	if (!bConditionMet)
	{
		Result->SetStringField(TEXT("message"), TEXT("Condition not met yet. Poll again to retry."));
	}

	return FMcpToolResult::Json(Result);
}

TSharedPtr<FJsonValue> UPieSessionTool::GetActorProperty(AActor* Actor, const FString& PropertyName) const
{
	if (!Actor) return nullptr;

	FProperty* Property = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Property) return nullptr;

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Actor);

	if (FNumericProperty* NumProp = CastField<FNumericProperty>(Property))
	{
		if (NumProp->IsFloatingPoint())
		{
			double Value = 0;
			NumProp->GetValue_InContainer(Actor, &Value);
			return MakeShareable(new FJsonValueNumber(Value));
		}
		else
		{
			int64 Value = 0;
			NumProp->GetValue_InContainer(Actor, &Value);
			return MakeShareable(new FJsonValueNumber(static_cast<double>(Value)));
		}
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		return MakeShareable(new FJsonValueBoolean(BoolProp->GetPropertyValue(ValuePtr)));
	}
	else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		return MakeShareable(new FJsonValueString(StrProp->GetPropertyValue(ValuePtr)));
	}

	// Fallback
	FString ExportedText;
	Property->ExportTextItem_Direct(ExportedText, ValuePtr, nullptr, nullptr, PPF_None);
	return MakeShareable(new FJsonValueString(ExportedText));
}

bool UPieSessionTool::CompareJsonValues(const TSharedPtr<FJsonValue>& ActualValue, const TSharedPtr<FJsonValue>& ExpectedJson, const FString& Operator) const
{
	if (!ActualValue.IsValid() || !ExpectedJson.IsValid())
	{
		return false;
	}

	if (Operator == TEXT("equals") || Operator == TEXT("eq") || Operator == TEXT("=="))
	{
		if (ActualValue->Type == EJson::Boolean && ExpectedJson->Type == EJson::Boolean)
		{
			return ActualValue->AsBool() == ExpectedJson->AsBool();
		}
		else if (ActualValue->Type == EJson::Number && ExpectedJson->Type == EJson::Number)
		{
			return FMath::IsNearlyEqual(ActualValue->AsNumber(), ExpectedJson->AsNumber(), 0.001);
		}
		else if (ActualValue->Type == EJson::String && ExpectedJson->Type == EJson::String)
		{
			return ActualValue->AsString().Equals(ExpectedJson->AsString(), ESearchCase::IgnoreCase);
		}
		else
		{
			return ActualValue->AsString().Equals(ExpectedJson->AsString());
		}
	}
	else if (Operator == TEXT("not_equals") || Operator == TEXT("ne") || Operator == TEXT("!="))
	{
		if (ActualValue->Type == EJson::Boolean && ExpectedJson->Type == EJson::Boolean)
		{
			return ActualValue->AsBool() != ExpectedJson->AsBool();
		}
		else if (ActualValue->Type == EJson::Number && ExpectedJson->Type == EJson::Number)
		{
			return !FMath::IsNearlyEqual(ActualValue->AsNumber(), ExpectedJson->AsNumber(), 0.001);
		}
		else
		{
			return !ActualValue->AsString().Equals(ExpectedJson->AsString());
		}
	}
	else if (Operator == TEXT("less_than") || Operator == TEXT("lt") || Operator == TEXT("<"))
	{
		return ActualValue->AsNumber() < ExpectedJson->AsNumber();
	}
	else if (Operator == TEXT("greater_than") || Operator == TEXT("gt") || Operator == TEXT(">"))
	{
		return ActualValue->AsNumber() > ExpectedJson->AsNumber();
	}
	else if (Operator == TEXT("less_equal") || Operator == TEXT("le") || Operator == TEXT("<="))
	{
		return ActualValue->AsNumber() <= ExpectedJson->AsNumber();
	}
	else if (Operator == TEXT("greater_equal") || Operator == TEXT("ge") || Operator == TEXT(">="))
	{
		return ActualValue->AsNumber() >= ExpectedJson->AsNumber();
	}
	else if (Operator == TEXT("contains"))
	{
		return ActualValue->AsString().Contains(ExpectedJson->AsString());
	}

	return false;
}
