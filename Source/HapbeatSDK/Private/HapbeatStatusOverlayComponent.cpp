// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatStatusOverlayComponent.h"

#include "HapbeatSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace
{
	/**
	 * Slots reserved per overlay instance: one status line plus room for the log.
	 * Fixed keys (rather than key -1) are what make the lines update in place
	 * instead of stacking a fresh copy every frame.
	 */
	constexpr uint64 KeysPerOverlay = 64;

	/** Counter handing each overlay its own block of message keys. */
	uint64 GNextOverlayKeyBase = 0x48'42'00'00; // 'HB' -- unlikely to collide with game code
}

UHapbeatStatusOverlayComponent::UHapbeatStatusOverlayComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UHapbeatStatusOverlayComponent::BeginPlay()
{
	Super::BeginPlay();

	DebugMessageKeyBase = GNextOverlayKeyBase;
	GNextOverlayKeyBase += KeysPerOverlay;

	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UHapbeatSubsystem* Subsystem = GameInstance->GetSubsystem<UHapbeatSubsystem>())
			{
				Subsystem->OnConnected.AddDynamic(this, &UHapbeatStatusOverlayComponent::HandleConnected);
				Subsystem->OnDisconnected.AddDynamic(this, &UHapbeatStatusOverlayComponent::HandleDisconnected);
				Subsystem->OnError.AddDynamic(this, &UHapbeatStatusOverlayComponent::HandleError);
				Subsystem->OnPong.AddDynamic(this, &UHapbeatStatusOverlayComponent::HandlePong);
				bWasStreaming = Subsystem->IsStreaming();
			}
		}
	}
}

void UHapbeatStatusOverlayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UHapbeatSubsystem* Subsystem = GameInstance->GetSubsystem<UHapbeatSubsystem>())
			{
				Subsystem->OnConnected.RemoveDynamic(this, &UHapbeatStatusOverlayComponent::HandleConnected);
				Subsystem->OnDisconnected.RemoveDynamic(this, &UHapbeatStatusOverlayComponent::HandleDisconnected);
				Subsystem->OnError.RemoveDynamic(this, &UHapbeatStatusOverlayComponent::HandleError);
				Subsystem->OnPong.RemoveDynamic(this, &UHapbeatStatusOverlayComponent::HandlePong);
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

FString UHapbeatStatusOverlayComponent::BuildStatusLine() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	UHapbeatSubsystem* Subsystem = GameInstance != nullptr ? GameInstance->GetSubsystem<UHapbeatSubsystem>() : nullptr;
	if (Subsystem == nullptr)
	{
		return TEXT("Hapbeat: no subsystem");
	}

	if (!Subsystem->IsConnected())
	{
		return TEXT("Hapbeat: Disconnected");
	}

	const int32 Player = Subsystem->GetOverridePlayer();
	const int32 Group = Subsystem->GetOverrideGroup();
	FString OverrideText;
	if (Player < 1 && Group < 1)
	{
		OverrideText = TEXT("override: off");
	}
	else
	{
		OverrideText = FString::Printf(TEXT("override: P%s / G%s"),
			Player >= 1 ? *FString::FromInt(Player) : TEXT("-"),
			Group >= 1 ? *FString::FromInt(Group) : TEXT("-"));
	}

	return FString::Printf(TEXT("Hapbeat: Connected (%s, %d device(s))%s"),
		*OverrideText,
		Subsystem->GetAliveDeviceCount(),
		Subsystem->IsStreaming() ? TEXT(" [STREAMING]") : TEXT(""));
}

void UHapbeatStatusOverlayComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bShowOverlay || GEngine == nullptr)
	{
		return;
	}

	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	if (UHapbeatSubsystem* Subsystem = GameInstance != nullptr ? GameInstance->GetSubsystem<UHapbeatSubsystem>() : nullptr)
	{
		// Surface stream start / stop so callers don't have to log it from every
		// place that starts one.
		const bool bNowStreaming = Subsystem->IsStreaming();
		if (bNowStreaming != bWasStreaming)
		{
			Log(bNowStreaming ? TEXT("Stream started") : TEXT("Stream stopped"));
			bWasStreaming = bNowStreaming;
		}
	}

	// Slightly longer than a frame so the line never blinks between ticks.
	const float Lifetime = FMath::Max(DeltaTime * 2.0f, 0.05f);

	GEngine->AddOnScreenDebugMessage(DebugMessageKeyBase, Lifetime, FColor::Cyan, BuildStatusLine());

	const int32 LineCount = FMath::Min(LogLines.Num(), FMath::Max(1, MaxLogLines));
	for (int32 Index = 0; Index < LineCount; ++Index)
	{
		GEngine->AddOnScreenDebugMessage(DebugMessageKeyBase + 1 + Index, Lifetime, FColor::White,
			LogLines[LogLines.Num() - LineCount + Index]);
	}
}

void UHapbeatStatusOverlayComponent::Log(const FString& Message)
{
	const UWorld* World = GetWorld();
	const float Timestamp = World != nullptr ? World->GetRealTimeSeconds() : 0.0f;
	LogLines.Add(FString::Printf(TEXT("[%.1f] %s"), Timestamp, *Message));

	const int32 Cap = FMath::Max(1, MaxLogLines);
	if (LogLines.Num() > Cap)
	{
		LogLines.RemoveAt(0, LogLines.Num() - Cap, EAllowShrinking::No);
	}
}

void UHapbeatStatusOverlayComponent::ClearLog()
{
	LogLines.Reset();
}

void UHapbeatStatusOverlayComponent::HandleConnected()
{
	Log(TEXT("Connected"));
}

void UHapbeatStatusOverlayComponent::HandleDisconnected()
{
	Log(TEXT("Disconnected"));
}

void UHapbeatStatusOverlayComponent::HandleError(const FString& Message)
{
	Log(FString::Printf(TEXT("Error: %s"), *Message));
}

void UHapbeatStatusOverlayComponent::HandlePong(const FString& Endpoint, int64 RttUs, const FString& DeviceName, const FString& /*Address*/, const FString& /*Firmware*/)
{
	Log(FString::Printf(TEXT("Pong %s (%s): RTT=%.1fms"),
		*DeviceName, *Endpoint, static_cast<double>(RttUs) / 1000.0));
}
