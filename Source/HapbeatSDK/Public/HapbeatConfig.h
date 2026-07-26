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
 * mirrors Hapbeat.HapbeatConfig (Unity SDK); the Bridge / latency live-flush
 * fields are intentionally dropped for v1 (see the design doc §3.3).
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
		meta = (Tooltip = "Send streamed clip packets (STREAM_BEGIN/DATA/END) directly to devices already known from a PONG response, instead of UDP broadcast. Wi-Fi AP power-save (DTIM) batching can hold broadcast frames for one beacon interval, showing up as periodic ~100-200 ms stutter in streamed haptics; unicast avoids that batching. Falls back to broadcast automatically when no device has responded yet. Other commands (Play/Stop/StopAll/PING/CONNECT_STATUS) are unaffected and always broadcast. Default: enabled."))
	bool bStreamUnicast = true;

	UPROPERTY(EditAnywhere, config, Category = "Behavior",
		meta = (Tooltip = "Send Play / Stop / StopAll directly to devices already known from a PONG response, instead of UDP broadcast. Same Wi-Fi AP power-save (DTIM) rationale as Stream Unicast, applied to one-shot commands: a broadcast can sit at the AP until the next beacon, delaying a single command by up to ~300 ms. Falls back to broadcast automatically when no device has responded yet, or when every known device's address mismatches the command's target -- the device applies the same target filter on receipt, so a broadcast can never actuate a device the target did not address. Default: enabled."))
	bool bCommandUnicast = true;

	UPROPERTY(EditAnywhere, config, Category = "Behavior",
		meta = (Tooltip = "Global delay (seconds) added to every Play / StreamClip to match audio output latency (e.g. Bluetooth headphones). Hapbeat haptics go out with ~10 ms latency, so on slow audio paths the haptic can arrive before the sound.",
			ClampMin = "0.0", ClampMax = "0.5"))
	float HapticDelaySeconds = 0.0f;

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
