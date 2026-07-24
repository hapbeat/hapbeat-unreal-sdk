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
		meta = (Tooltip = "Reserved (kept for Unity SDK config parity; not consumed by the runtime). The device OLED group display tracks SetAddressOverride exclusively, and routing-group filtering is a trailing /group_N target segment.",
			ClampMin = "-1", ClampMax = "254"))
	int32 Group = -1;

	UPROPERTY(EditAnywhere, config, Category = "Connection",
		meta = (Tooltip = "Shown on the Hapbeat device display. Max 16 chars; the default app_name element shows the first 8. Empty = use the project name."))
	FString AppName;

	UPROPERTY(EditAnywhere, config, Category = "Connection",
		meta = (Tooltip = "Discovery timeout in milliseconds.", ClampMin = "1000", ClampMax = "10000"))
	int32 DiscoveryTimeoutMs = 3000;

	// ---- Behavior ----

	UPROPERTY(EditAnywhere, config, Category = "Behavior",
		meta = (Tooltip = "Interval in seconds between keep-alive PING / CONNECT_STATUS beacons.", ClampMin = "1.0", ClampMax = "60.0"))
	float PingInterval = 5.0f;

	UPROPERTY(EditAnywhere, config, Category = "Behavior",
		meta = (Tooltip = "Audio data the SDK keeps queued ahead of real-time while streaming a clip. Smaller = faster stop after StopStream() but more stutter risk on slow links. Typical LAN: 30-60 ms.",
			ClampMin = "0.01", ClampMax = "0.2"))
	float StreamSendAheadSeconds = 0.05f;

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
