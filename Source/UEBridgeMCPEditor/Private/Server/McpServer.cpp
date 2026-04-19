// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Server/McpServer.h"
#include "UEBridgeMCPEditor.h"
#include "UEBridgeMCP.h"
#include "Tools/McpToolRegistry.h"
#include "HttpServerModule.h"
#include "HttpPath.h"
#include "Async/Async.h"
#include "Misc/Guid.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/ThreadSafeBool.h"
#include "Session/McpEditorSessionManager.h"

FMcpServer::FMcpServer()
{
	// Initialize capabilities
	Capabilities.bSupportsTools = true;
	Capabilities.bToolsListChanged = false;
	Capabilities.bSupportsResources = false;
	Capabilities.bSupportsPrompts = false;
	Capabilities.bSupportsLogging = false;

	// Initialize server info
	ServerInfo.Name = TEXT("ue-bridge-mcp");
	ServerInfo.Version = UEBRIDGEMCP_VERSION;
}

FMcpServer::~FMcpServer()
{
	Stop();
}

bool FMcpServer::Start(int32 Port, const FString& BindAddress)
{
	if (bIsRunning)
	{
		UE_LOG(LogUEBridgeMCPEditor, Warning, TEXT("MCP Server is already running"));
		return true;
	}

	ServerPort = Port;

	// Get HTTP server module
	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();

	// Get router for our port
	HttpRouter = HttpServerModule.GetHttpRouter(ServerPort);
	if (!HttpRouter.IsValid())
	{
		UE_LOG(LogUEBridgeMCPEditor, Error, TEXT("Failed to get HTTP router for port %d"), ServerPort);
		return false;
	}

	// Bind MCP route using /mcp endpoint (MCP Streamable HTTP standard)
	McpRouteHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_DELETE | EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateRaw(this, &FMcpServer::HandleMcpRequest)
	);

	if (!McpRouteHandle.IsValid())
	{
		UE_LOG(LogUEBridgeMCPEditor, Error, TEXT("Failed to bind MCP route"));
		return false;
	}

	// Start listener
	HttpServerModule.StartAllListeners();

	bIsRunning = true;
	ServerStatus = EMcpServerStatus::Running;
	LastErrorMessage.Empty();
	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("MCP Server started on http://%s:%d/mcp"), *BindAddress, ServerPort);

	return true;
}

void FMcpServer::Stop()
{
	if (!bIsRunning)
	{
		return;
	}

	// Unbind route
	if (HttpRouter.IsValid() && McpRouteHandle.IsValid())
	{
		HttpRouter->UnbindRoute(McpRouteHandle);
	}

	// Note: Don't stop all listeners as other modules might be using HTTP

	bIsRunning = false;
	bInitialized = false;
	if (!CurrentSessionId.IsEmpty())
	{
		FMcpEditorSessionManager::Get().ResetSession(CurrentSessionId);
	}
	CurrentSessionId.Empty();
	ServerStatus = EMcpServerStatus::Stopped;

	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("MCP Server stopped"));
}

void FMcpServer::SetError(const FString& Message)
{
	LastErrorMessage = Message;
	LastErrorTime = FDateTime::Now();
	ServerStatus = EMcpServerStatus::Error;
	UE_LOG(LogUEBridgeMCP, Error, TEXT("MCP Server Error: %s"), *Message);
}

void FMcpServer::ClearError()
{
	LastErrorMessage.Empty();
	if (bIsRunning)
	{
		ServerStatus = EMcpServerStatus::Running;
	}
}

bool FMcpServer::HandleMcpRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Log detailed request info
	FString VerbString;
	switch (Request.Verb)
	{
	case EHttpServerRequestVerbs::VERB_GET: VerbString = TEXT("GET"); break;
	case EHttpServerRequestVerbs::VERB_POST: VerbString = TEXT("POST"); break;
	case EHttpServerRequestVerbs::VERB_DELETE: VerbString = TEXT("DELETE"); break;
	case EHttpServerRequestVerbs::VERB_OPTIONS: VerbString = TEXT("OPTIONS"); break;
	default: VerbString = FString::Printf(TEXT("UNKNOWN(%d)"), static_cast<int32>(Request.Verb)); break;
	}

	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("=== MCP Request ==="));
	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("  Verb: %s"), *VerbString);
	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("  Path: %s"), *Request.RelativePath.GetPath());
	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("  Headers:"));
	for (const auto& Header : Request.Headers)
	{
		FString HeaderValues = FString::Join(Header.Value, TEXT(", "));
		UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("    %s: %s"), *Header.Key, *HeaderValues);
	}
	if (Request.Body.Num() > 0)
	{
		FString BodyString = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Request.Body.GetData())));
		UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("  Body: %s"), *BodyString);
	}

	// Handle CORS preflight
	if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(TEXT(""), TEXT("text/plain"));
		Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), {TEXT("*")});
		Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), {TEXT("GET, POST, DELETE, OPTIONS")});
		Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), {TEXT("Content-Type, Accept, Mcp-Session-Id, MCP-Protocol-Version")});
		Response->Code = EHttpServerResponseCodes::NoContent;
		OnComplete(MoveTemp(Response));
		return true;
	}

	// Handle DELETE (session termination)
	if (Request.Verb == EHttpServerRequestVerbs::VERB_DELETE)
	{
		FMcpEditorSessionManager::Get().ResetSession(CurrentSessionId);
		CurrentSessionId.Empty();
		bInitialized = false;

		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(TEXT(""), TEXT("text/plain"));
		Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), {TEXT("*")});
		Response->Code = EHttpServerResponseCodes::NoContent;
		OnComplete(MoveTemp(Response));
		return true;
	}

	// Handle GET (SSE stream or health check)
	if (Request.Verb == EHttpServerRequestVerbs::VERB_GET)
	{
		// Check if client expects SSE stream
		const TArray<FString>* AcceptHeaders = Request.Headers.Find(TEXT("Accept"));
		bool bExpectsSSE = AcceptHeaders && AcceptHeaders->ContainsByPredicate([](const FString& Value) {
			return Value.Contains(TEXT("text/event-stream"));
		});

		if (bExpectsSSE)
		{
			// SSE not supported - return 405 per MCP spec
			UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("MCP GET request - SSE not supported, returning 405"));

			TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(TEXT(""), TEXT("text/plain"));
			Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), {TEXT("*")});
			Response->Code = EHttpServerResponseCodes::BadMethod;
			OnComplete(MoveTemp(Response));
			return true;
		}

		// Non-SSE GET (browser health check) - return server info
		UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("MCP GET request - returning health check info"));

		TSharedPtr<FJsonObject> Info = MakeShareable(new FJsonObject);
		Info->SetStringField(TEXT("name"), ServerInfo.Name);
		Info->SetStringField(TEXT("version"), ServerInfo.Version);
		Info->SetStringField(TEXT("protocol"), MCP_PROTOCOL_VERSION);
		Info->SetBoolField(TEXT("running"), bIsRunning);

		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(Info.ToSharedRef(), Writer);

		UE_LOG(LogUEBridgeMCPEditor, Verbose, TEXT("MCP Health check response: %s"), *JsonString);

		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(JsonString, TEXT("application/json"));
		Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), {TEXT("*")});
		Response->Code = EHttpServerResponseCodes::Ok;
		OnComplete(MoveTemp(Response));
		return true;
	}

	// Handle POST (JSON-RPC requests)
	if (Request.Verb != EHttpServerRequestVerbs::VERB_POST)
	{
		SendErrorResponse(OnComplete, 405, EMcpErrorCode::InvalidRequest, TEXT("Method not allowed"));
		return true;
	}

	// Parse body
	FString Body;
	if (Request.Body.Num() > 0)
	{
		// Convert binary data to string with explicit length to avoid reading garbage bytes
		FUTF8ToTCHAR Convert(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		Body = FString(Convert.Length(), Convert.Get());
	}

	if (Body.IsEmpty())
	{
		SendErrorResponse(OnComplete, 400, EMcpErrorCode::ParseError, TEXT("Empty request body"));
		return true;
	}

	UE_LOG(LogUEBridgeMCPEditor, Verbose, TEXT("MCP Request: %s"), *Body);

	// Parse JSON-RPC request
	TOptional<FMcpRequest> ParsedRequest = FMcpRequest::FromJsonString(Body);
	if (!ParsedRequest.IsSet())
	{
		SendErrorResponse(OnComplete, 400, EMcpErrorCode::ParseError, TEXT("Invalid JSON-RPC request"));
		return true;
	}

	FMcpRequest McpRequest = ParsedRequest.GetValue();

	// Track pending requests
	int32 CurrentPending = PendingRequestCount.Increment();
	if (CurrentPending > MaxPendingRequests)
	{
		ServerStatus = EMcpServerStatus::Overloaded;
		UE_LOG(LogUEBridgeMCP, Warning, TEXT("MCP Server overloaded: %d pending requests"), CurrentPending);
	}

	// 判断是否需要在 GameThread 上执行：
	// - 工具调用：检查工具的 RequiresGameThread() 标志
	// - 协议请求（Initialize/ToolsList/Shutdown 等）：始终在 GameThread 上执行
	bool bNeedsGameThread = true;
	if (McpRequest.ParsedMethod == EMcpMethod::ToolsCall && McpRequest.Params.IsValid())
	{
		FString ToolName;
		if (McpRequest.Params->TryGetStringField(TEXT("name"), ToolName))
		{
			bNeedsGameThread = FMcpToolRegistry::Get().DoesToolRequireGameThread(ToolName);
			UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("  Tool '%s' RequiresGameThread=%d"), *ToolName, bNeedsGameThread);
		}
	}

	// 超时看门狗机制：防止工具执行卡死导致 HTTP 连接永久挂起
	// 使用 FEvent 在后台线程上等待，如果超时则强制返回错误响应
	static constexpr float ToolExecutionTimeoutSeconds = 120.0f;

	// 共享状态：用于协调工具执行线程和看门狗线程
	struct FRequestState
	{
		FEvent* CompletionEvent;
		FThreadSafeBool bResponseSent;
		FCriticalSection ResponseLock;

		FRequestState()
			: CompletionEvent(FPlatformProcess::GetSynchEventFromPool(false))
			, bResponseSent(false)
		{
		}

		~FRequestState()
		{
			if (CompletionEvent)
			{
				FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
				CompletionEvent = nullptr;
			}
		}
	};

	TSharedPtr<FRequestState> RequestState = MakeShared<FRequestState>();

	// 通用的请求处理 Lambda（在 GameThread 或后台线程中执行）
	auto ProcessAndRespond = [this, McpRequest, OnComplete, RequestState]()
	{
		FMcpResponse Response;

		// Wrap in try-catch to prevent crashes from propagating
#if PLATFORM_EXCEPTIONS_DISABLED
		Response = ProcessRequest(McpRequest);
#else
		try
		{
			Response = ProcessRequest(McpRequest);
		}
		catch (const std::exception& e)
		{
			FString ErrorMsg = FString::Printf(TEXT("Exception during request processing: %s"), ANSI_TO_TCHAR(e.what()));
			SetError(ErrorMsg);
			Response = FMcpResponse::Error(McpRequest.Id, EMcpErrorCode::InternalError, ErrorMsg);
		}
		catch (...)
		{
			FString ErrorMsg = TEXT("Unknown exception during request processing");
			SetError(ErrorMsg);
			Response = FMcpResponse::Error(McpRequest.Id, EMcpErrorCode::InternalError, ErrorMsg);
		}
#endif

		// Decrement pending count
		int32 Remaining = PendingRequestCount.Decrement();
		if (Remaining <= MaxPendingRequests && ServerStatus == EMcpServerStatus::Overloaded)
		{
			ServerStatus = EMcpServerStatus::Running;
		}

		// 通知看门狗线程：工具执行已完成
		RequestState->CompletionEvent->Trigger();

		// 尝试发送响应（如果看门狗还没有发送超时响应）
		{
			FScopeLock Lock(&RequestState->ResponseLock);
			if (RequestState->bResponseSent)
			{
				// 看门狗已经发送了超时响应，跳过
				UE_LOG(LogUEBridgeMCP, Warning, TEXT("Tool execution completed after timeout, response already sent"));
				return;
			}
			RequestState->bResponseSent = true;
		}

		// Handle notification (no response needed)
		if (McpRequest.IsNotification())
		{
			TUniquePtr<FHttpServerResponse> HttpResponse = FHttpServerResponse::Create(TEXT(""), TEXT("text/plain"));
			HttpResponse->Headers.Add(TEXT("Access-Control-Allow-Origin"), {TEXT("*")});
			HttpResponse->Code = EHttpServerResponseCodes::Accepted;
			OnComplete(MoveTemp(HttpResponse));
			return;
		}

		SendResponse(OnComplete, Response);
	};

	// 看门狗 Lambda：在后台线程上等待工具执行完成，超时则强制返回错误
	auto TimeoutWatchdog = [this, McpRequest, OnComplete, RequestState]()
	{
		uint32 TimeoutMs = static_cast<uint32>(ToolExecutionTimeoutSeconds * 1000.0f);
		bool bCompleted = RequestState->CompletionEvent->Wait(TimeoutMs);

		if (!bCompleted)
		{
			// 超时！尝试发送超时错误响应
			FScopeLock Lock(&RequestState->ResponseLock);
			if (!RequestState->bResponseSent)
			{
				RequestState->bResponseSent = true;

				FString ErrorMsg = FString::Printf(
					TEXT("Tool execution timed out after %.0f seconds. The editor GameThread may be blocked or frozen. "
					     "Try stopping PIE or restarting the editor."),
					ToolExecutionTimeoutSeconds);

				UE_LOG(LogUEBridgeMCP, Error, TEXT("MCP Watchdog: %s (method=%s)"), *ErrorMsg, *McpRequest.Method);

				FMcpResponse TimeoutResponse = FMcpResponse::Error(
					McpRequest.Id, EMcpErrorCode::InternalError, ErrorMsg);
				SendResponse(OnComplete, TimeoutResponse, 504);
			}
		}
	};

	if (bNeedsGameThread)
	{
		// 需要 GameThread 的请求（大多数工具 + 协议请求）
		AsyncTask(ENamedThreads::GameThread, MoveTemp(ProcessAndRespond));
		// 启动看门狗线程监控超时
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, MoveTemp(TimeoutWatchdog));
	}
	else
	{
		// 不需要 GameThread 的工具，在后台线程池执行，减轻 GameThread 压力
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, MoveTemp(ProcessAndRespond));
		// 同样启动看门狗
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, MoveTemp(TimeoutWatchdog));
	}

	return true;
}

FMcpResponse FMcpServer::ProcessRequest(const FMcpRequest& Request)
{
	switch (Request.ParsedMethod)
	{
	case EMcpMethod::Initialize:
		return HandleInitialize(Request);

	case EMcpMethod::Initialized:
		// Notification, no response needed
		bInitialized = true;
		return FMcpResponse::Success(Request.Id, MakeShareable(new FJsonObject));

	case EMcpMethod::ToolsList:
		return HandleToolsList(Request);

	case EMcpMethod::ToolsCall:
		return HandleToolsCall(Request);

	case EMcpMethod::Shutdown:
		bInitialized = false;
		if (!CurrentSessionId.IsEmpty())
		{
			FMcpEditorSessionManager::Get().ResetSession(CurrentSessionId);
		}
		CurrentSessionId.Empty();
		return FMcpResponse::Success(Request.Id, MakeShareable(new FJsonObject));

	default:
		return FMcpResponse::Error(Request.Id, EMcpErrorCode::MethodNotFound,
			FString::Printf(TEXT("Unknown method: %s"), *Request.Method));
	}
}

FMcpResponse FMcpServer::HandleInitialize(const FMcpRequest& Request)
{
	// Parse client info
	if (Request.Params.IsValid())
	{
		const TSharedPtr<FJsonObject>* ClientInfoObj;
		if (Request.Params->TryGetObjectField(TEXT("clientInfo"), ClientInfoObj))
		{
			ClientInfo = FMcpClientInfo::FromJson(*ClientInfoObj);
		}

		const TSharedPtr<FJsonObject>* CapabilitiesObj;
		if (Request.Params->TryGetObjectField(TEXT("capabilities"), CapabilitiesObj))
		{
			ClientCapabilities = FMcpClientCapabilities::FromJson(*CapabilitiesObj);
		}
	}

	// Generate session ID
	CurrentSessionId = GenerateSessionId();
	FMcpEditorSessionManager::Get().ResetSession(CurrentSessionId);

	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("MCP Initialize from client: %s %s"),
		*ClientInfo.Name, *ClientInfo.Version);

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("protocolVersion"), MCP_PROTOCOL_VERSION);
	Result->SetObjectField(TEXT("capabilities"), Capabilities.ToJson());
	Result->SetObjectField(TEXT("serverInfo"), ServerInfo.ToJson());

	return FMcpResponse::Success(Request.Id, Result);
}

FMcpResponse FMcpServer::HandleToolsList(const FMcpRequest& Request)
{
	TArray<FMcpToolDefinition> Tools = FMcpToolRegistry::Get().GetAllToolDefinitions();

	TArray<TSharedPtr<FJsonValue>> ToolsArray;
	for (const FMcpToolDefinition& Tool : Tools)
	{
		ToolsArray.Add(MakeShareable(new FJsonValueObject(Tool.ToJson())));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetArrayField(TEXT("tools"), ToolsArray);

	return FMcpResponse::Success(Request.Id, Result);
}

FMcpResponse FMcpServer::HandleToolsCall(const FMcpRequest& Request)
{
	if (!Request.Params.IsValid())
	{
		return FMcpResponse::Error(Request.Id, EMcpErrorCode::InvalidParams, TEXT("Missing params"));
	}

	FString ToolName;
	if (!Request.Params->TryGetStringField(TEXT("name"), ToolName))
	{
		return FMcpResponse::Error(Request.Id, EMcpErrorCode::InvalidParams, TEXT("Missing tool name"));
	}

	// Get arguments (optional)
	TSharedPtr<FJsonObject> Arguments;
	const TSharedPtr<FJsonObject>* ArgsObj;
	if (Request.Params->TryGetObjectField(TEXT("arguments"), ArgsObj))
	{
		Arguments = *ArgsObj;
	}
	else
	{
		Arguments = MakeShareable(new FJsonObject);
	}

	// Execute tool
	FMcpToolContext Context;
	// P2-N8: CurrentSessionId 为空时使用固定 "default"，而不是每次 request 的 id，
	// 否则 SessionManager 的缓存每次都换 key，永远命中率为 0。
	Context.SessionId = CurrentSessionId.IsEmpty() ? TEXT("default") : CurrentSessionId;
	Context.RequestId = Request.Id;

	FMcpToolResult ToolResult = FMcpToolRegistry::Get().ExecuteTool(ToolName, Arguments, Context);

	return FMcpResponse::Success(Request.Id, ToolResult.ToJson());
}

FString FMcpServer::GenerateSessionId() const
{
	return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
}

void FMcpServer::SendResponse(const FHttpResultCallback& OnComplete, const FMcpResponse& Response, int32 StatusCode)
{
	FString JsonString = Response.ToJsonString();

	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("=== MCP Response ==="));
	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("  Status Code: %d"), StatusCode);
	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("  Body: %s"), *JsonString);

	TUniquePtr<FHttpServerResponse> HttpResponse = FHttpServerResponse::Create(JsonString, TEXT("application/json"));
	HttpResponse->Headers.Add(TEXT("Access-Control-Allow-Origin"), {TEXT("*")});

	if (!CurrentSessionId.IsEmpty())
	{
		HttpResponse->Headers.Add(TEXT("Mcp-Session-Id"), {CurrentSessionId});
		UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("  Session-Id: %s"), *CurrentSessionId);
	}

	HttpResponse->Code = static_cast<EHttpServerResponseCodes>(StatusCode);
	OnComplete(MoveTemp(HttpResponse));
}

void FMcpServer::SendErrorResponse(const FHttpResultCallback& OnComplete, int32 HttpStatus, int32 JsonRpcCode, const FString& Message)
{
	FMcpResponse Response = FMcpResponse::Error(TEXT(""), JsonRpcCode, Message);
	SendResponse(OnComplete, Response, HttpStatus);
}
