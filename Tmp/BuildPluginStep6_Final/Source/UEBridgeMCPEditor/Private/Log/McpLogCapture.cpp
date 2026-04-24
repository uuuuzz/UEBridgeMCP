// Copyright uuuuzz 2024-2026. All Rights Reserved.

#include "Log/McpLogCapture.h"

FMcpLogCapture* FMcpLogCapture::Instance = nullptr;

FMcpLogCapture::FMcpLogCapture()
{
	// Pre-allocate buffer
	RingBuffer.Reserve(BufferCapacity);
}

FMcpLogCapture::~FMcpLogCapture()
{
	Shutdown();
}

FMcpLogCapture& FMcpLogCapture::Get()
{
	if (!Instance)
	{
		Instance = new FMcpLogCapture();
	}
	return *Instance;
}

void FMcpLogCapture::Initialize()
{
	FScopeLock ScopeLock(&Lock);

	if (!bIsRegistered)
	{
		GLog->AddOutputDevice(this);
		bIsRegistered = true;
	}
}

void FMcpLogCapture::Shutdown()
{
	FScopeLock ScopeLock(&Lock);

	if (bIsRegistered)
	{
		GLog->RemoveOutputDevice(this);
		bIsRegistered = false;
	}
}

void FMcpLogCapture::Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category)
{
	SerializeImpl(Message, Verbosity, Category);
}

void FMcpLogCapture::Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category, double Time)
{
	SerializeImpl(Message, Verbosity, Category);
}

void FMcpLogCapture::SerializeImpl(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category)
{
	FScopeLock ScopeLock(&Lock);

	// Ensure buffer is sized
	if (RingBuffer.Num() < BufferCapacity)
	{
		RingBuffer.AddDefaulted(BufferCapacity - RingBuffer.Num());
	}

	// Write to ring buffer
	FMcpLogEntry& Entry = RingBuffer[WriteIndex];
	Entry.Timestamp = FDateTime::Now();
	Entry.Category = Category;
	Entry.Verbosity = Verbosity;
	Entry.Message = Message;

	WriteIndex = (WriteIndex + 1) % BufferCapacity;
	TotalCaptured++;

	if (TotalCaptured.Load() >= BufferCapacity)
	{
		bBufferFull = true;
	}
}

TArray<FMcpLogEntry> FMcpLogCapture::GetLogs(
	const FString& CategoryFilter,
	ELogVerbosity::Type MinVerbosity,
	int32 Limit,
	const FString& SearchFilter) const
{
	FScopeLock ScopeLock(&Lock);

	TArray<FMcpLogEntry> Result;
	Result.Reserve(FMath::Min(Limit, RingBuffer.Num()));

	const bool bFilterAllCategories = CategoryFilter == TEXT("*") || CategoryFilter.IsEmpty();
	const bool bHasSearchFilter = !SearchFilter.IsEmpty();

	// Calculate how many entries we have
	const int32 EntryCount = bBufferFull ? BufferCapacity : WriteIndex;
	if (EntryCount == 0)
	{
		return Result;
	}

	// Iterate backwards from most recent to oldest
	for (int32 i = 0; i < EntryCount && Result.Num() < Limit; ++i)
	{
		// Calculate index going backwards from write position
		int32 Index = (WriteIndex - 1 - i + BufferCapacity) % BufferCapacity;
		const FMcpLogEntry& Entry = RingBuffer[Index];

		// Skip empty entries
		if (Entry.Message.IsEmpty())
		{
			continue;
		}

		// Filter by verbosity (lower enum value = higher severity)
		if (Entry.Verbosity > MinVerbosity)
		{
			continue;
		}

		// Filter by category
		if (!bFilterAllCategories)
		{
			FString CategoryName = Entry.Category.ToString();
			if (!CategoryName.MatchesWildcard(CategoryFilter))
			{
				continue;
			}
		}

		// Filter by search string
		if (bHasSearchFilter)
		{
			if (!Entry.Message.Contains(SearchFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}

		Result.Add(Entry);
	}

	return Result;
}

void FMcpLogCapture::SetBufferCapacity(int32 Capacity)
{
	FScopeLock ScopeLock(&Lock);

	BufferCapacity = FMath::Max(100, Capacity);
	RingBuffer.Empty();
	RingBuffer.Reserve(BufferCapacity);
	WriteIndex = 0;
	TotalCaptured = 0;
	bBufferFull = false;
}

int32 FMcpLogCapture::GetCurrentCount() const
{
	FScopeLock ScopeLock(&Lock);
	return bBufferFull ? BufferCapacity : WriteIndex;
}

void FMcpLogCapture::Clear()
{
	FScopeLock ScopeLock(&Lock);

	for (FMcpLogEntry& Entry : RingBuffer)
	{
		Entry = FMcpLogEntry();
	}
	WriteIndex = 0;
	TotalCaptured = 0;
	bBufferFull = false;
}
