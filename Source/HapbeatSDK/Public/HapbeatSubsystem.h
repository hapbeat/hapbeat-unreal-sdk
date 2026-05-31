// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HapbeatSubsystem.generated.h"

class FSocket;
class FInternetAddr;

/**
 * Hapbeat game-instance subsystem — the level-1 "fire" surface, callable from
 * C++ and Blueprint. Sends Layer 1 commands over Wi-Fi UDP broadcast.
 *
 * Blueprint:  Get Hapbeat Subsystem -> Connect -> Play (event id, gain).
 * C++:        GetGameInstance()->GetSubsystem<UHapbeatSubsystem>()->Play(...);
 *
 * The fire side stays orthogonal to event tuning (default gains live in the kit
 * on the device / a future EventMap asset), matching the Hapbeat Unity SDK.
 */
UCLASS()
class HAPBEATSDK_API UHapbeatSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Open the UDP broadcast socket. AppName (<=16 chars) shows on the device OLED. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void Connect(int32 InPort = 7700, const FString& InAppName = TEXT(""));

	/** Play an event id present in the device kit. Gain is 0..1. Target "" = broadcast. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void Play(const FString& EventId, float Gain = 1.0f, const FString& Target = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void Stop(const FString& EventId, const FString& Target = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void StopAll(const FString& Target = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void Ping();

private:
	void SendPacket(const TArray<uint8>& Packet);
	uint16 NextSeq();

	FSocket* Socket = nullptr;
	TSharedPtr<FInternetAddr> BroadcastAddr;
	int32 Port = 7700;
	uint8 Group = 0;
	FString AppName;
	uint16 Seq = 0;
};
