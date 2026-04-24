// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "Protocol/McpTypes.h"
#include "Protocol/McpCapabilities.h"

/** Server health status */
enum class EMcpServerStatus : uint8
{
	Stopped,
	Running,
	Error,
	Overloaded
};

/**
 * MCP HTTP Server
 * Handles incoming HTTP requests and dispatches to tools
 */
class UEBRIDGEMCPEDITOR_API FMcpServer
{
public:
	FMcpServer();
	~FMcpServer();

	/** Start the HTTP server on specified port */
	bool Start(int32 Port = 8080, const FString& BindAddress = TEXT("127.0.0.1"));

	/** Stop the HTTP server */
	void Stop();

	/** Check if server is running */
	bool IsRunning() const { return bIsRunning; }

	/** Get the port number */
	int32 GetPort() const { return ServerPort; }

	/** Get server capabilities */
	FMcpServerCapabilities GetCapabilities() const { return Capabilities; }

	/** Get server info */
	FMcpServerInfo GetServerInfo() const { return ServerInfo; }

	/** Get current server status */
	EMcpServerStatus GetStatus() const { return ServerStatus; }

	/** Get last error message */
	FString GetLastError() const { return LastErrorMessage; }

	/** Get number of pending requests */
	int32 GetPendingRequestCount() const { return PendingRequestCount.GetValue(); }

	/** Clear error state */
	void ClearError();

private:
	/** HTTP router handle */
	TSharedPtr<IHttpRouter> HttpRouter;

	/** Route handles */
	FHttpRouteHandle McpRouteHandle;

	/** Server state */
	bool bIsRunning = false;
	int32 ServerPort = 8080;
	bool bInitialized = false;

	/** Session management */
	FString CurrentSessionId;
	FMcpClientCapabilities ClientCapabilities;
	FMcpClientInfo ClientInfo;

	/** Server configuration */
	FMcpServerCapabilities Capabilities;
	FMcpServerInfo ServerInfo;

	/** Server health tracking */
	EMcpServerStatus ServerStatus = EMcpServerStatus::Stopped;
	FString LastErrorMessage;
	FThreadSafeCounter PendingRequestCount;
	FDateTime LastErrorTime;

	/** Maximum concurrent requests before marking as overloaded */
	static constexpr int32 MaxPendingRequests = 10;

	/** Set error state with message */
	void SetError(const FString& Message);

	/** Handle incoming MCP request */
	bool HandleMcpRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Process JSON-RPC message */
	FMcpResponse ProcessRequest(const FMcpRequest& Request);

	/** Handle specific methods */
	FMcpResponse HandleInitialize(const FMcpRequest& Request);
	FMcpResponse HandleToolsList(const FMcpRequest& Request);
	FMcpResponse HandleToolsCall(const FMcpRequest& Request);

	/** Generate unique session ID */
	FString GenerateSessionId() const;

	/** Send HTTP response */
	void SendResponse(const FHttpResultCallback& OnComplete, const FMcpResponse& Response, int32 StatusCode = 200);

	/** Send error HTTP response */
	void SendErrorResponse(const FHttpResultCallback& OnComplete, int32 HttpStatus, int32 JsonRpcCode, const FString& Message);
};
