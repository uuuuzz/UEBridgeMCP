// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Subsystem/McpEditorSubsystem.h"
#include "Server/McpServer.h"
#include "UEBridgeMCPEditor.h"
#include "Log/McpLogCapture.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "UEBridgeMCP"

namespace
{
	bool ShouldSuppressMcpServerStart()
	{
		return IsRunningCommandlet();
	}
}

void UMcpEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("MCP Editor Subsystem initializing"));

	// Initialize log capture before anything else
	FMcpLogCapture::Get().Initialize();

	LoadConfiguration();

	// Create server instance
	Server = MakeUnique<FMcpServer>();

	// Auto-start if configured
	if (Settings && Settings->bAutoStartServer)
	{
		if (ShouldSuppressMcpServerStart())
		{
			UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("Skipping MCP Server auto-start while running a commandlet"));
		}
		else
		{
			StartServer();
		}
	}
}

void UMcpEditorSubsystem::Deinitialize()
{
	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("MCP Editor Subsystem deinitializing"));

	StopServer();
	Server.Reset();

	// Shutdown log capture after server
	FMcpLogCapture::Get().Shutdown();

	Super::Deinitialize();
}

void UMcpEditorSubsystem::LoadConfiguration()
{
	Settings = GetMutableDefault<UMcpServerSettings>();

	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("MCP Settings - Port: %d, AutoStart: %s, BindAddress: %s"),
		Settings->ServerPort,
		Settings->bAutoStartServer ? TEXT("true") : TEXT("false"),
		*Settings->BindAddress);
}

bool UMcpEditorSubsystem::IsServerRunning() const
{
	return Server.IsValid() && Server->IsRunning();
}

bool UMcpEditorSubsystem::StartServer()
{
	if (ShouldSuppressMcpServerStart())
	{
		UE_LOG(LogUEBridgeMCPEditor, Warning, TEXT("MCP Server start skipped while running a commandlet"));
		return false;
	}

	if (!Server.IsValid())
	{
		Server = MakeUnique<FMcpServer>();
	}

	if (Server->IsRunning())
	{
		UE_LOG(LogUEBridgeMCPEditor, Warning, TEXT("MCP Server is already running"));
		return true;
	}

	const int32 Port = Settings ? Settings->ServerPort : 8080;
	const FString BindAddress = Settings ? Settings->BindAddress : TEXT("127.0.0.1");

	UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("Starting MCP Server on %s:%d"), *BindAddress, Port);

	if (Server->Start(Port, BindAddress))
	{
		UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("MCP Server started successfully on port %d"), Port);
		return true;
	}

	UE_LOG(LogUEBridgeMCPEditor, Error, TEXT("Failed to start MCP Server on port %d (port may be in use)"), Port);
	return false;
}

void UMcpEditorSubsystem::StopServer()
{
	if (Server.IsValid())
	{
		Server->Stop();
		UE_LOG(LogUEBridgeMCPEditor, Log, TEXT("MCP Server stopped"));
	}
}

void UMcpEditorSubsystem::RestartServer()
{
	StopServer();
	StartServer();
}

FString UMcpEditorSubsystem::GetServerStatus() const
{
	if (!Server.IsValid())
	{
		return TEXT("Server not initialized");
	}

	if (Server->IsRunning())
	{
		return FString::Printf(TEXT("Running on port %d"), Server->GetPort());
	}

	return TEXT("Stopped");
}

UMcpServerSettings* UMcpEditorSubsystem::GetSettings() const
{
	return GetMutableDefault<UMcpServerSettings>();
}

int32 UMcpEditorSubsystem::GetActualPort() const
{
	if (Server.IsValid() && Server->IsRunning())
	{
		return Server->GetPort();
	}
	return 0;
}

#undef LOCTEXT_NAMESPACE
