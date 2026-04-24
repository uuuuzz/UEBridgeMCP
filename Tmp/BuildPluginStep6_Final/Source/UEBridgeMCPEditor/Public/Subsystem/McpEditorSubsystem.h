// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Server/McpServer.h"
#include "McpEditorSubsystem.generated.h"

/**
 * Settings for MCP Server
 */
UCLASS(config = UEBridgeMCP, defaultconfig)
class UEBRIDGEMCPEDITOR_API UMcpServerSettings : public UObject
{
	GENERATED_BODY()

public:
	/** HTTP server port for MCP connections */
	UPROPERTY(config, EditAnywhere, Category = "Server", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 ServerPort = 8080;

	/** Auto-start the MCP server when the editor opens */
	UPROPERTY(config, EditAnywhere, Category = "Server")
	bool bAutoStartServer = true;

	/** Bind address for HTTP server (127.0.0.1 for localhost only) */
	UPROPERTY(config, EditAnywhere, Category = "Server")
	FString BindAddress = TEXT("127.0.0.1");
};

/**
 * Editor subsystem managing MCP server lifecycle
 */
UCLASS()
class UEBRIDGEMCPEDITOR_API UMcpEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	//~ UEditorSubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Get the MCP server instance (C++ only) */
	FMcpServer* GetServer() const { return Server.Get(); }

	/** Check if server is running */
	UFUNCTION(BlueprintCallable, Category = "MCP")
	bool IsServerRunning() const;

	/** Start the MCP server */
	UFUNCTION(BlueprintCallable, Category = "MCP")
	bool StartServer();

	/** Stop the MCP server */
	UFUNCTION(BlueprintCallable, Category = "MCP")
	void StopServer();

	/** Restart the server */
	UFUNCTION(BlueprintCallable, Category = "MCP")
	void RestartServer();

	/** Get server status information */
	UFUNCTION(BlueprintCallable, Category = "MCP")
	FString GetServerStatus() const;

	/** Get settings */
	UFUNCTION(BlueprintCallable, Category = "MCP")
	UMcpServerSettings* GetSettings() const;

	/** Get the port the server is bound to (0 if not running) */
	UFUNCTION(BlueprintCallable, Category = "MCP")
	int32 GetActualPort() const;

private:
	/** MCP server instance */
	TUniquePtr<FMcpServer> Server;

	/** Settings */
	UPROPERTY()
	TObjectPtr<UMcpServerSettings> Settings;

	/** Load configuration from ini file */
	void LoadConfiguration();
};
