// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "HapbeatConfig.generated.h"

/**
 * Hapbeat SDK connection + behaviour settings, surfaced in
 * Project Settings > Plugins > Hapbeat (and persisted to DefaultGame.ini via
 * config=Game / defaultconfig).
 *
 * The subsystem reads GetDefault<UHapbeatConfig>() in Initialize() to seed the
 * port / app name / ping interval / group before auto-connecting. Field set
 * mirrors Hapbeat.HapbeatConfig (Unity SDK); legacy relay and live-flush
 * fields are intentionally absent.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Hapbeat"))
class HAPBEATSDK_API UHapbeatConfig : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// Group the settings under "Plugins" in the Project Settings tree.
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }

	// ---- Connection ----

	UPROPERTY(EditAnywhere, config, Category = "Connection",
		meta = (Tooltip = "UDP port for communication with Hapbeat devices.", ClampMin = "1", ClampMax = "65535"))
	int32 Port = 7700;

	UPROPERTY(EditAnywhere, config, Category = "Connection",
		meta = (Tooltip = "Shown on the Hapbeat device display. Max 16 chars; the default app_name element shows the first 8. Empty = use the project name."))
	FString AppName;

	// ---- Behavior ----

	UPROPERTY(EditAnywhere, config, Category = "Behavior",
		meta = (Tooltip = "Interval in seconds between keep-alive PING / CONNECT_STATUS beacons.", ClampMin = "1.0", ClampMax = "60.0"))
	float PingInterval = 5.0f;

	UPROPERTY(EditAnywhere, config, Category = "Behavior",
		meta = (Tooltip = "Audio data the SDK keeps queued ahead of real-time while streaming a clip. Smaller = faster stop after StopStream() but more stutter risk on slow links. Typical LAN: 30-60 ms.",
			ClampMin = "0.01", ClampMax = "0.2"))
	float StreamSendAheadSeconds = 0.05f;

	UPROPERTY(EditAnywhere, config, Category = "Behavior",
		meta = (Tooltip = "Send Play / Stop / StopAll directly to devices already known from a PONG response, instead of UDP broadcast. AP power-save (DTIM) can hold broadcast frames until the next beacon. Falls back to broadcast automatically when no device has responded yet, or when every known device's address mismatches the command target. Default: enabled."))
	bool bCommandUnicast = true;

	UPROPERTY(EditAnywhere, config, Category = "Behavior",
		meta = (Tooltip = "Global delay (seconds) added to every Play / StreamClip to match audio output latency (e.g. Bluetooth headphones). Hapbeat haptics go out with ~10 ms latency, so on slow audio paths the haptic can arrive before the sound.",
			ClampMin = "0.0", ClampMax = "0.5"))
	float HapticDelaySeconds = 0.0f;

	// ---- Address override pinned by the build ----
	//
	// A build shipped for one seat / one booth station wants its player (or
	// group) fixed, with no way for the wearer to wander off it. A pinned axis
	// is applied at startup ahead of any persisted value, and the in-game panel
	// dims that axis rather than pretending it can be edited.

	UPROPERTY(EditAnywhere, config, Category = "Addressing",
		meta = (Tooltip = "Player number every command is forced to, for builds pinned to one seat. -1 = not pinned (the wearer may choose).",
			ClampMin = "-1", ClampMax = "99"))
	int32 ForcedOverridePlayer = -1;

	UPROPERTY(EditAnywhere, config, Category = "Addressing",
		meta = (Tooltip = "Group number every command is forced to. -1 = not pinned (the wearer may choose).",
			ClampMin = "-1", ClampMax = "99"))
	int32 ForcedOverrideGroup = -1;

	// ---- Logging ----

	UPROPERTY(EditAnywhere, config, Category = "Logging",
		meta = (Tooltip = "Enable logging to the output log (Play, Stop, Connect, errors)."))
	bool bEnableLogging = true;

	UPROPERTY(EditAnywhere, config, Category = "Logging",
		meta = (Tooltip = "Enable verbose logging (PONG, keep-alive, protocol details). Noisy — debugging only."))
	bool bVerboseLogging = false;

#if WITH_EDITOR
	// Clamp AppName to the device OLED grid width (16). UE meta has no string
	// length cap, so enforce it here on edit.
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
