// Copyright uuuuzz 2024-2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"

/**
 * Structure representing a captured log entry.
 */
struct FMcpLogEntry
{
	FDateTime Timestamp;
	FName Category;
	ELogVerbosity::Type Verbosity;
	FString Message;
};

/**
 * Custom output device that captures UE log entries to a ring buffer.
 * Thread-safe for concurrent log capture and querying.
 */
class UEBRIDGEMCPEDITOR_API FMcpLogCapture : public FOutputDevice
{
public:
	/**
	 * Get the singleton instance.
	 */
	static FMcpLogCapture& Get();

	// FOutputDevice interface
	virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category, double Time) override;
	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

	/**
	 * Query captured logs with filtering.
	 * @param CategoryFilter Filter by category name (supports wildcards, "*" for all)
	 * @param MinVerbosity Minimum verbosity level to include
	 * @param Limit Maximum number of entries to return
	 * @param SearchFilter Optional substring to search for in messages
	 * @return Array of matching log entries (newest first)
	 */
	TArray<FMcpLogEntry> GetLogs(
		const FString& CategoryFilter = TEXT("*"),
		ELogVerbosity::Type MinVerbosity = ELogVerbosity::Log,
		int32 Limit = 100,
		const FString& SearchFilter = TEXT("")) const;

	/**
	 * Set the ring buffer capacity. Clears existing entries.
	 */
	void SetBufferCapacity(int32 Capacity);

	/**
	 * Get current buffer capacity.
	 */
	int32 GetBufferCapacity() const { return BufferCapacity; }

	/**
	 * Get total number of log entries captured since initialization.
	 */
	int32 GetTotalCaptured() const { return TotalCaptured.Load(); }

	/**
	 * Get current number of entries in buffer.
	 */
	int32 GetCurrentCount() const;

	/**
	 * Clear all captured logs.
	 */
	void Clear();

	/**
	 * Register with the global logging system.
	 */
	void Initialize();

	/**
	 * Unregister from the global logging system.
	 */
	void Shutdown();

	/**
	 * Check if log capture is active.
	 */
	bool IsInitialized() const { return bIsRegistered; }

private:
	FMcpLogCapture();
	~FMcpLogCapture();

	// Non-copyable
	FMcpLogCapture(const FMcpLogCapture&) = delete;
	FMcpLogCapture& operator=(const FMcpLogCapture&) = delete;

	static FMcpLogCapture* Instance;

	TArray<FMcpLogEntry> RingBuffer;
	int32 WriteIndex = 0;
	int32 BufferCapacity = 5000;
	TAtomic<int32> TotalCaptured{0};
	bool bIsRegistered = false;
	bool bBufferFull = false;

	mutable FCriticalSection Lock;

	/**
	 * Internal serialize implementation.
	 */
	void SerializeImpl(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category);
};
