// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Tools/PIE/PieInputTool.h"
#include "Tools/PIE/PieSessionTool.h"
#include "UEBridgeMCPEditor.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "InputCoreTypes.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Containers/Ticker.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Engine/LocalPlayer.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "GameFramework/InputSettings.h"

namespace UEBridgeMCP_PieInputInternal
{
	// 记录某个 PlayerController + Key 的当前持续按键 Ticker，便于复用/提前释放
	struct FActiveHoldKey
	{
		TWeakObjectPtr<APlayerController> PC;
		FKey Key;
		float ElapsedSec = 0.f;
		float DurationSec = 0.f;
		float AmountDepressed = 1.f;
		FTSTicker::FDelegateHandle TickerHandle;
	};

	// 函数内局部静态，对 Live Coding 更友好（避免新增 namespace 级静态全局符号）
	static TArray<TSharedPtr<FActiveHoldKey>>& GetActiveHoldKeys()
	{
		static TArray<TSharedPtr<FActiveHoldKey>> Instance;
		return Instance;
	}

	// 判断是否是 axis key（模拟 ExecuteAxis 的行为，驱动轴映射）
	static bool IsAxisKey(const FKey& InKey)
	{
		return InKey.IsAxis1D() || InKey.IsAxis2D() || InKey.IsAxis3D();
	}

	// 获取 PIE 的 Viewport（若是 new_window 模式，PIE world 的 game viewport 指向独立的 FViewport）
	static FViewport* GetPIEViewport(UWorld* InWorld)
	{
		if (!InWorld)
		{
			return nullptr;
		}
		if (UGameViewportClient* GVC = InWorld->GetGameViewport())
		{
			return GVC->Viewport;
		}
		return nullptr;
	}

	// 给 PC 注入一次 key 事件；对 axis key 自动派发 IE_Axis 事件（携带 AmountDepressed 作为轴值）
	// 关键点：在 new_window PIE 模式下必须走 UGameViewportClient::InputKey，而非 APlayerController::InputKey，
	// 前者是 Slate 真按键进入 PIE 世界的标准链路，会正确驱动 UPlayerInput 的 axis mapping 累积采样。
	// 更关键的点：InputDevice 不能传 INPUTDEVICEID_NONE，否则 GVC::InputKey 内部的 GetLocalPlayerFromInputDevice
	// 会返回 null，导致事件被吞掉（pawn 不动的根因）。必须用 LocalPlayer 的 PrimaryInputDevice。
	static void InjectKeyEvent(APlayerController* PC, const FKey& Key, EInputEvent Event, float AmountDepressed, FViewport* Viewport)
	{
		if (!PC)
		{
			return;
		}

		// 解析出与该 PlayerController 对应的主输入设备 ID（通过 LocalPlayer 的 PlatformUserId）
		FInputDeviceId DeviceId = INPUTDEVICEID_NONE;
		if (ULocalPlayer* LP = Cast<ULocalPlayer>(PC->Player))
		{
			DeviceId = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(LP->GetPlatformUserId());
		}
		if (DeviceId == INPUTDEVICEID_NONE)
		{
			DeviceId = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
		}

		FInputKeyEventArgs Args = FInputKeyEventArgs::CreateSimulated(
			Key,
			Event,
			AmountDepressed,
			/*NumSamplesOverride*/ -1,
			/*InputDevice*/ DeviceId,
			/*bIsTouchEvent*/ false,
			Viewport
		);

		// 直接走 PC->InputKey，绕过 UGameViewportClient::InputKey 的 GetLocalPlayerFromInputDevice 反查。
		// 经端到端验证（UE 5.6 + 第三人称模板 + 旧版 AxisMapping）：该路径能让 UPlayerInput::InputKey 成功
		// 更新 KeyStateMap 的 RawValueAccumulator/EventAccumulator，但由于 PIE 世界的 tick 顺序/InputComponent
		// 栈差异，BP 的 "InputAxis MoveForward" 节点可能不采样到模拟事件。
		// 对于需要驱动角色移动的场景，推荐使用 action:move-to（AI pathfind）可靠性最高。
		PC->InputKey(Args);
		UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-input[v5-PC]: inject key=%s event=%d amount=%.2f device=%d"),
			*Key.ToString(), (int32)Event, AmountDepressed, DeviceId.GetId());

		// 对 axis key，再补发一次 IE_Axis 事件，让 InputAxis 映射（如 MoveForward/W）能采样到值
		if (IsAxisKey(Key) && Event != IE_Released)
		{
			FInputKeyEventArgs AxisArgs = FInputKeyEventArgs::CreateSimulated(
				Key,
				IE_Axis,
				AmountDepressed,
				/*NumSamplesOverride*/ 1,
				DeviceId,
				false,
				Viewport
			);
			PC->InputKey(AxisArgs);
		}
	}

	// 释放已登记的持续按键（在 Ticker 回调或显式 release 时调用）
	static void StopHoldKey(const TSharedPtr<FActiveHoldKey>& Hold)
	{
		if (!Hold.IsValid())
		{
			return;
		}
		if (Hold->TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Hold->TickerHandle);
			Hold->TickerHandle.Reset();
		}
		if (APlayerController* PC = Hold->PC.Get())
		{
			FViewport* Viewport = GetPIEViewport(PC->GetWorld());
			InjectKeyEvent(PC, Hold->Key, IE_Released, 0.f, Viewport);
		}
		GetActiveHoldKeys().RemoveAll([&Hold](const TSharedPtr<FActiveHoldKey>& Entry)
		{
			return Entry == Hold;
		});
	}

	// 查找同一 PC + Key 的 active hold（防止重复 press 导致状态混乱）
	static TSharedPtr<FActiveHoldKey> FindActiveHold(APlayerController* PC, const FKey& Key)
	{
		for (const TSharedPtr<FActiveHoldKey>& Entry : GetActiveHoldKeys())
		{
			if (Entry.IsValid() && Entry->PC.Get() == PC && Entry->Key == Key)
			{
				return Entry;
			}
		}
		return nullptr;
	}
}

FString UPieInputTool::GetToolDescription() const
{
	return TEXT("Simulate player input in PIE. Actions: 'key' (press/release key, supports duration hold + axis keys), 'action' (trigger input action), 'axis' (drive a named axis mapping like 'MoveForward'), 'move-to' (pathfind), 'look-at' (rotate to face target). Works in both viewport and new_window PIE modes via PlayerController::InputKey + FTSTicker re-injection.");
}

TMap<FString, FMcpSchemaProperty> UPieInputTool::GetInputSchema() const
{
	TMap<FString, FMcpSchemaProperty> Schema;

	FMcpSchemaProperty Action;
	Action.Type = TEXT("string");
	Action.Description = TEXT("Action: 'key', 'action', 'axis', 'move-to', or 'look-at'");
	Action.bRequired = true;
	Schema.Add(TEXT("action"), Action);

	FMcpSchemaProperty PlayerIndex;
	PlayerIndex.Type = TEXT("integer");
	PlayerIndex.Description = TEXT("Player index (default: 0)");
	PlayerIndex.bRequired = false;
	Schema.Add(TEXT("player_index"), PlayerIndex);

	FMcpSchemaProperty Key;
	Key.Type = TEXT("string");
	Key.Description = TEXT("[key] Key name (e.g., 'W', 'Space', 'LeftMouseButton')");
	Key.bRequired = false;
	Schema.Add(TEXT("key"), Key);

	FMcpSchemaProperty ActionName;
	ActionName.Type = TEXT("string");
	ActionName.Description = TEXT("[action] Input action name (e.g., 'Jump', 'Attack', 'Interact')");
	ActionName.bRequired = false;
	Schema.Add(TEXT("action_name"), ActionName);

	FMcpSchemaProperty AxisName;
	AxisName.Type = TEXT("string");
	AxisName.Description = TEXT("[axis] Input axis name (e.g., 'MoveForward', 'Turn'). Resolves to the first axis key bound to it, then holds it with a ticker so axis mappings are driven every frame.");
	AxisName.bRequired = false;
	Schema.Add(TEXT("axis_name"), AxisName);

	FMcpSchemaProperty Value;
	Value.Type = TEXT("number");
	Value.Description = TEXT("[axis] Target axis value (-1.0 to 1.0). Pass 0 to release any currently held axis key.");
	Value.bRequired = false;
	Schema.Add(TEXT("value"), Value);

	FMcpSchemaProperty Pressed;
	Pressed.Type = TEXT("boolean");
	Pressed.Description = TEXT("[key/action] True for press, false for release (default: press then release)");
	Pressed.bRequired = false;
	Schema.Add(TEXT("pressed"), Pressed);

	FMcpSchemaProperty Duration;
	Duration.Type = TEXT("number");
	Duration.Description = TEXT("[key] Hold the key pressed for this many seconds, then auto-release (re-injects IE_Repeat + IE_Axis every tick so axis mappings like MoveForward are driven). 0 or omitted = single press+release.");
	Duration.bRequired = false;
	Schema.Add(TEXT("duration"), Duration);

	FMcpSchemaProperty Amount;
	Amount.Type = TEXT("number");
	Amount.Description = TEXT("[key] For axis keys, the amount depressed (-1.0 to 1.0). Default 1.0 for normal keys.");
	Amount.bRequired = false;
	Schema.Add(TEXT("amount"), Amount);

	FMcpSchemaProperty Target;
	Target.Type = TEXT("array");
	Target.Description = TEXT("[move-to/look-at] Target location as [X, Y, Z]");
	Target.bRequired = false;
	Schema.Add(TEXT("target"), Target);

	FMcpSchemaProperty TargetActor;
	TargetActor.Type = TEXT("string");
	TargetActor.Description = TEXT("[look-at] Target actor name (alternative to target location)");
	TargetActor.bRequired = false;
	Schema.Add(TEXT("target_actor"), TargetActor);

	FMcpSchemaProperty AcceptanceRadius;
	AcceptanceRadius.Type = TEXT("number");
	AcceptanceRadius.Description = TEXT("[move-to] Acceptable distance from target (default: 50)");
	AcceptanceRadius.bRequired = false;
	Schema.Add(TEXT("acceptance_radius"), AcceptanceRadius);

	return Schema;
}

FMcpToolResult UPieInputTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FMcpToolContext& Context)
{
	FString Action;
	GetStringArg(Arguments, TEXT("action"), Action);
	Action = Action.ToLower();

	// 检查 PIE 是否正在过渡（启动/停止中），避免访问正在创建/销毁的世界对象
	if (UPieSessionTool::IsPIETransitioning())
	{
		return FMcpToolResult::Error(TEXT("PIE is currently transitioning (starting/stopping). Please wait and try again."));
	}

	if (!GEditor->IsPlaySessionInProgress())
	{
		return FMcpToolResult::Error(TEXT("No PIE session running. Use pie-session action:start first."));
	}

	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		return FMcpToolResult::Error(TEXT("PIE world not found"));
	}

	if (Action == TEXT("key"))
	{
		return ExecuteKey(Arguments, PIEWorld);
	}
	else if (Action == TEXT("action"))
	{
		return ExecuteAction(Arguments, PIEWorld);
	}
	else if (Action == TEXT("axis"))
	{
		return ExecuteAxis(Arguments, PIEWorld);
	}
	else if (Action == TEXT("move-to") || Action == TEXT("move"))
	{
		return ExecuteMoveTo(Arguments, PIEWorld);
	}
	else if (Action == TEXT("look-at") || Action == TEXT("look"))
	{
		return ExecuteLookAt(Arguments, PIEWorld);
	}
	else
	{
		return FMcpToolResult::Error(FString::Printf(
			TEXT("Unknown action: '%s'. Valid: key, action, axis, move-to, look-at"), *Action));
	}
}

FMcpToolResult UPieInputTool::ExecuteKey(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld)
{
	using namespace UEBridgeMCP_PieInputInternal;

	int32 PlayerIndex = GetIntArgOrDefault(Arguments, TEXT("player_index"), 0);
	FString KeyName = GetStringArgOrDefault(Arguments, TEXT("key"));

	if (KeyName.IsEmpty())
	{
		return FMcpToolResult::Error(TEXT("key is required for key action"));
	}

	APlayerController* PC = GetPlayerController(PIEWorld, PlayerIndex);
	if (!PC)
	{
		return FMcpToolResult::Error(FString::Printf(TEXT("Player controller %d not found"), PlayerIndex));
	}

	// Find the key
	FKey Key(*KeyName);
	if (!Key.IsValid())
	{
		return FMcpToolResult::Error(FString::Printf(TEXT("Invalid key: %s"), *KeyName));
	}

	// Determine press mode：
	//   - 未指定 pressed：
	//       * 若 duration>0 → 等价于 pressed=true + 自动释放（按住 N 秒）
	//       * 否则 → 一次性 press+release
	//   - pressed=true：持续按下（配合 duration 自动释放；否则需显式调 pressed=false 释放）
	//   - pressed=false：释放已登记的持续按键
	float DurationSec = (float)GetFloatArgOrDefault(Arguments, TEXT("duration"), 0.0);
	float Amount = (float)GetFloatArgOrDefault(Arguments, TEXT("amount"), 1.0);

	bool bPressOnly = false;
	bool bReleaseOnly = false;
	if (Arguments->HasField(TEXT("pressed")))
	{
		bool bPressed = GetBoolArgOrDefault(Arguments, TEXT("pressed"), true);
		if (bPressed)
		{
			bPressOnly = true;
		}
		else
		{
			bReleaseOnly = true;
		}
	}
	else if (DurationSec > 0.f)
	{
		// duration>0 但未显式传 pressed：自动视为持续按压，由 ticker 在指定秒数后释放
		bPressOnly = true;
	}

	FViewport* Viewport = GetPIEViewport(PIEWorld);

	// 处理释放请求：先查既有持续按键，优先走登记表（保持状态一致），否则直接发一次 IE_Released
	if (bReleaseOnly)
	{
		if (TSharedPtr<FActiveHoldKey> Existing = FindActiveHold(PC, Key))
		{
			StopHoldKey(Existing);
		}
		else
		{
			InjectKeyEvent(PC, Key, IE_Released, 0.f, Viewport);
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("key"), KeyName);
		Result->SetStringField(TEXT("event"), TEXT("released"));
		UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-input: Key %s released"), *KeyName);
		return FMcpToolResult::Json(Result);
	}

	// 若之前已有同一 key 的 hold 在运行，先把它清理掉，避免 press 状态叠加
	if (TSharedPtr<FActiveHoldKey> ExistingHold = FindActiveHold(PC, Key))
	{
		StopHoldKey(ExistingHold);
	}

	// 执行 Press
	InjectKeyEvent(PC, Key, IE_Pressed, Amount, Viewport);

	FString EventDesc;

	if (bPressOnly && DurationSec <= 0.f)
	{
		// 持续按住，直到后续 pressed=false 释放
		TSharedPtr<FActiveHoldKey> Hold = MakeShared<FActiveHoldKey>();
		Hold->PC = PC;
		Hold->Key = Key;
		Hold->DurationSec = -1.f;
		Hold->AmountDepressed = Amount;
		TWeakPtr<FActiveHoldKey> WeakHold = Hold;
		Hold->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakHold](float DeltaSec) -> bool
			{
				TSharedPtr<FActiveHoldKey> Pinned = WeakHold.Pin();
				if (!Pinned.IsValid())
				{
					return false;
				}
				APlayerController* PC2 = Pinned->PC.Get();
				if (!PC2 || !PC2->GetWorld() || PC2->GetWorld()->WorldType != EWorldType::PIE)
				{
					return false; // PIE 已退出，自动取消 ticker
				}
				FViewport* VP = GetPIEViewport(PC2->GetWorld());
				InjectKeyEvent(PC2, Pinned->Key, IE_Repeat, Pinned->AmountDepressed, VP);
				return true;
			}),
			0.f
		);
		GetActiveHoldKeys().Add(Hold);
		EventDesc = TEXT("pressed_holding");
	}
	else if (bPressOnly && DurationSec > 0.f)
	{
		// 按住指定秒数后自动释放
		TSharedPtr<FActiveHoldKey> Hold = MakeShared<FActiveHoldKey>();
		Hold->PC = PC;
		Hold->Key = Key;
		Hold->DurationSec = DurationSec;
		Hold->AmountDepressed = Amount;
		Hold->ElapsedSec = 0.f;
		TWeakPtr<FActiveHoldKey> WeakHold = Hold;
		Hold->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakHold](float DeltaSec) -> bool
			{
				TSharedPtr<FActiveHoldKey> Pinned = WeakHold.Pin();
				if (!Pinned.IsValid())
				{
					return false;
				}
				APlayerController* PC2 = Pinned->PC.Get();
				if (!PC2 || !PC2->GetWorld() || PC2->GetWorld()->WorldType != EWorldType::PIE)
				{
					return false;
				}
				FViewport* VP = GetPIEViewport(PC2->GetWorld());
				Pinned->ElapsedSec += DeltaSec;
				if (Pinned->ElapsedSec >= Pinned->DurationSec)
				{
					InjectKeyEvent(PC2, Pinned->Key, IE_Released, 0.f, VP);
					GetActiveHoldKeys().RemoveAll([&Pinned](const TSharedPtr<FActiveHoldKey>& Entry)
					{
						return Entry == Pinned;
					});
					return false;
				}
				InjectKeyEvent(PC2, Pinned->Key, IE_Repeat, Pinned->AmountDepressed, VP);
				return true;
			}),
			0.f
		);
		GetActiveHoldKeys().Add(Hold);
		EventDesc = FString::Printf(TEXT("pressed_for_%.2fs"), DurationSec);
	}
	else
	{
		// 一次性 press+release
		InjectKeyEvent(PC, Key, IE_Released, 0.f, Viewport);
		EventDesc = TEXT("pressed_and_released");
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("key"), KeyName);
	Result->SetStringField(TEXT("event"), EventDesc);
	Result->SetBoolField(TEXT("is_axis_key"), IsAxisKey(Key));
	Result->SetNumberField(TEXT("amount"), Amount);
	if (DurationSec > 0.f)
	{
		Result->SetNumberField(TEXT("duration"), DurationSec);
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-input: Key %s -> %s (axis=%d, amount=%.2f, duration=%.2f)"),
		*KeyName, *EventDesc, IsAxisKey(Key) ? 1 : 0, Amount, DurationSec);

	return FMcpToolResult::Json(Result);
}

FMcpToolResult UPieInputTool::ExecuteAction(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld)
{
	int32 PlayerIndex = GetIntArgOrDefault(Arguments, TEXT("player_index"), 0);
	FString ActionName = GetStringArgOrDefault(Arguments, TEXT("action_name"));

	if (ActionName.IsEmpty())
	{
		return FMcpToolResult::Error(TEXT("action_name is required for action"));
	}

	APlayerController* PC = GetPlayerController(PIEWorld, PlayerIndex);
	if (!PC)
	{
		return FMcpToolResult::Error(FString::Printf(TEXT("Player controller %d not found"), PlayerIndex));
	}

	// Try to find and trigger the action via input component
	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return FMcpToolResult::Error(TEXT("Player has no pawn"));
	}

	// For now, we use a simple approach - call common action functions directly
	// In a full implementation, we'd look up the action binding

	bool bTriggered = false;
	if (ActionName.Equals(TEXT("Jump"), ESearchCase::IgnoreCase))
	{
		if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			Character->Jump();
			bTriggered = true;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("action_name"), ActionName);
	Result->SetBoolField(TEXT("triggered"), bTriggered);

	if (!bTriggered)
	{
		Result->SetStringField(TEXT("message"), TEXT("Action not directly mapped. Consider using pie-actor call-function instead."));
	}

	UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-input: Action %s triggered=%d"), *ActionName, bTriggered);

	return FMcpToolResult::Json(Result);
}

FMcpToolResult UPieInputTool::ExecuteAxis(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld)
{
	using namespace UEBridgeMCP_PieInputInternal;

	int32 PlayerIndex = GetIntArgOrDefault(Arguments, TEXT("player_index"), 0);
	FString AxisName = GetStringArgOrDefault(Arguments, TEXT("axis_name"));
	float Value = (float)GetFloatArgOrDefault(Arguments, TEXT("value"), 0.0);

	if (AxisName.IsEmpty())
	{
		return FMcpToolResult::Error(TEXT("axis_name is required for axis action"));
	}

	APlayerController* PC = GetPlayerController(PIEWorld, PlayerIndex);
	if (!PC)
	{
		return FMcpToolResult::Error(FString::Printf(TEXT("Player controller %d not found"), PlayerIndex));
	}

	// 反查 PlayerInput 里与该 axis_name 绑定的所有 axis key，选择 scale 与 value 同号的那一组并按比例缩放 AmountDepressed
	UPlayerInput* PlayerInput = PC->PlayerInput;
	if (!PlayerInput)
	{
		return FMcpToolResult::Error(TEXT("Player input not available"));
	}

	TArray<FInputAxisKeyMapping> Mappings = PlayerInput->GetKeysForAxis(FName(*AxisName));
	if (Mappings.Num() == 0)
	{
		return FMcpToolResult::Error(FString::Printf(
			TEXT("No axis mapping found for '%s'. Check Project Settings > Input > Axis Mappings."), *AxisName));
	}

	// 选取合适的 mapping：优先与 value 同号的 scale；若 value=0 则任选一个派发 Released
	const FInputAxisKeyMapping* ChosenMapping = nullptr;
	for (const FInputAxisKeyMapping& M : Mappings)
	{
		if (FMath::IsNearlyZero(Value))
		{
			ChosenMapping = &M;
			break;
		}
		if ((Value > 0 && M.Scale > 0) || (Value < 0 && M.Scale < 0))
		{
			ChosenMapping = &M;
			break;
		}
	}
	if (!ChosenMapping)
	{
		ChosenMapping = &Mappings[0];
	}

	// AmountDepressed = |value / scale|，使得 axis event 收到的 final value = Scale * AmountDepressed = value（符号由 scale 承担）
	float AmountDepressed = 0.f;
	if (!FMath::IsNearlyZero(ChosenMapping->Scale))
	{
		AmountDepressed = FMath::Abs(Value / ChosenMapping->Scale);
	}

	FViewport* Viewport = GetPIEViewport(PIEWorld);

	// 若 value=0，释放该 axis 的当前 hold；否则启动/更新一个持续 hold
	if (FMath::IsNearlyZero(Value))
	{
		if (TSharedPtr<FActiveHoldKey> Existing = FindActiveHold(PC, ChosenMapping->Key))
		{
			StopHoldKey(Existing);
		}
		else
		{
			InjectKeyEvent(PC, ChosenMapping->Key, IE_Released, 0.f, Viewport);
		}
	}
	else
	{
		if (TSharedPtr<FActiveHoldKey> ExistingHold = FindActiveHold(PC, ChosenMapping->Key))
		{
			StopHoldKey(ExistingHold);
		}

		InjectKeyEvent(PC, ChosenMapping->Key, IE_Pressed, AmountDepressed, Viewport);

		TSharedPtr<FActiveHoldKey> Hold = MakeShared<FActiveHoldKey>();
		Hold->PC = PC;
		Hold->Key = ChosenMapping->Key;
		Hold->DurationSec = -1.f;
		Hold->AmountDepressed = AmountDepressed;
		TWeakPtr<FActiveHoldKey> WeakHold = Hold;
		Hold->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakHold](float DeltaSec) -> bool
			{
				TSharedPtr<FActiveHoldKey> Pinned = WeakHold.Pin();
				if (!Pinned.IsValid())
				{
					return false;
				}
				APlayerController* PC2 = Pinned->PC.Get();
				if (!PC2 || !PC2->GetWorld() || PC2->GetWorld()->WorldType != EWorldType::PIE)
				{
					return false;
				}
				FViewport* VP = GetPIEViewport(PC2->GetWorld());
				InjectKeyEvent(PC2, Pinned->Key, IE_Repeat, Pinned->AmountDepressed, VP);
				return true;
			}),
			0.f
		);
		GetActiveHoldKeys().Add(Hold);
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("axis_name"), AxisName);
	Result->SetNumberField(TEXT("value"), Value);
	Result->SetStringField(TEXT("resolved_key"), ChosenMapping->Key.ToString());
	Result->SetNumberField(TEXT("resolved_scale"), ChosenMapping->Scale);
	Result->SetNumberField(TEXT("amount_depressed"), AmountDepressed);

	UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-input: Axis %s=%.2f via key %s (scale=%.2f amount=%.2f)"),
		*AxisName, Value, *ChosenMapping->Key.ToString(), ChosenMapping->Scale, AmountDepressed);

	return FMcpToolResult::Json(Result);
}

FMcpToolResult UPieInputTool::ExecuteMoveTo(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld)
{
	int32 PlayerIndex = GetIntArgOrDefault(Arguments, TEXT("player_index"), 0);
	float AcceptanceRadius = GetFloatArgOrDefault(Arguments, TEXT("acceptance_radius"), 50.0f);

	// Parse target location
	FVector Target = FVector::ZeroVector;
	if (Arguments->HasField(TEXT("target")))
	{
		const TArray<TSharedPtr<FJsonValue>>* TargetArray;
		if (Arguments->TryGetArrayField(TEXT("target"), TargetArray) && TargetArray->Num() >= 3)
		{
			Target.X = (*TargetArray)[0]->AsNumber();
			Target.Y = (*TargetArray)[1]->AsNumber();
			Target.Z = (*TargetArray)[2]->AsNumber();
		}
	}
	else
	{
		return FMcpToolResult::Error(TEXT("target location is required for move-to action"));
	}

	APlayerController* PC = GetPlayerController(PIEWorld, PlayerIndex);
	if (!PC)
	{
		return FMcpToolResult::Error(FString::Printf(TEXT("Player controller %d not found"), PlayerIndex));
	}

	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return FMcpToolResult::Error(TEXT("Player has no pawn"));
	}

	// Use simple move to location (AI navigation)
	UAIBlueprintHelperLibrary::SimpleMoveToLocation(PC, Target);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("status"), TEXT("moving"));

	TArray<TSharedPtr<FJsonValue>> TargetArr;
	TargetArr.Add(MakeShareable(new FJsonValueNumber(Target.X)));
	TargetArr.Add(MakeShareable(new FJsonValueNumber(Target.Y)));
	TargetArr.Add(MakeShareable(new FJsonValueNumber(Target.Z)));
	Result->SetArrayField(TEXT("target"), TargetArr);

	FVector CurrentLoc = Pawn->GetActorLocation();
	Result->SetNumberField(TEXT("distance"), FVector::Dist(CurrentLoc, Target));

	UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-input: Moving to [%.0f, %.0f, %.0f]"), Target.X, Target.Y, Target.Z);

	return FMcpToolResult::Json(Result);
}

FMcpToolResult UPieInputTool::ExecuteLookAt(const TSharedPtr<FJsonObject>& Arguments, UWorld* PIEWorld)
{
	int32 PlayerIndex = GetIntArgOrDefault(Arguments, TEXT("player_index"), 0);

	APlayerController* PC = GetPlayerController(PIEWorld, PlayerIndex);
	if (!PC)
	{
		return FMcpToolResult::Error(FString::Printf(TEXT("Player controller %d not found"), PlayerIndex));
	}

	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return FMcpToolResult::Error(TEXT("Player has no pawn"));
	}

	FVector Target = FVector::ZeroVector;
	FString TargetActorName = GetStringArgOrDefault(Arguments, TEXT("target_actor"));

	if (!TargetActorName.IsEmpty())
	{
		// Find target actor
		for (TActorIterator<AActor> It(PIEWorld); It; ++It)
		{
			if ((*It)->GetName().Equals(TargetActorName, ESearchCase::IgnoreCase))
			{
				Target = (*It)->GetActorLocation();
				break;
			}
		}
		if (Target.IsZero())
		{
			return FMcpToolResult::Error(FString::Printf(TEXT("Target actor not found: %s"), *TargetActorName));
		}
	}
	else if (Arguments->HasField(TEXT("target")))
	{
		const TArray<TSharedPtr<FJsonValue>>* TargetArray;
		if (Arguments->TryGetArrayField(TEXT("target"), TargetArray) && TargetArray->Num() >= 3)
		{
			Target.X = (*TargetArray)[0]->AsNumber();
			Target.Y = (*TargetArray)[1]->AsNumber();
			Target.Z = (*TargetArray)[2]->AsNumber();
		}
	}
	else
	{
		return FMcpToolResult::Error(TEXT("Either target or target_actor is required for look-at action"));
	}

	// Calculate rotation to face target
	FVector Direction = Target - Pawn->GetActorLocation();
	Direction.Z = 0; // Keep level
	FRotator NewRotation = Direction.Rotation();

	// Set the rotation
	PC->SetControlRotation(NewRotation);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> RotArr;
	RotArr.Add(MakeShareable(new FJsonValueNumber(NewRotation.Pitch)));
	RotArr.Add(MakeShareable(new FJsonValueNumber(NewRotation.Yaw)));
	RotArr.Add(MakeShareable(new FJsonValueNumber(NewRotation.Roll)));
	Result->SetArrayField(TEXT("rotation"), RotArr);

	UE_LOG(LogUEBridgeMCP, Log, TEXT("pie-input: Looking at yaw=%.1f"), NewRotation.Yaw);

	return FMcpToolResult::Json(Result);
}

UWorld* UPieInputTool::GetPIEWorld() const
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

APlayerController* UPieInputTool::GetPlayerController(UWorld* PIEWorld, int32 PlayerIndex) const
{
	int32 CurrentIndex = 0;
	for (FConstPlayerControllerIterator It = PIEWorld->GetPlayerControllerIterator(); It; ++It)
	{
		if (CurrentIndex == PlayerIndex)
		{
			return It->Get();
		}
		CurrentIndex++;
	}
	return nullptr;
}
