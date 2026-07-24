// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HapbeatBasicExampleActor.generated.h"

class UHapbeatClip;
class UHapbeatEventMap;
class UHapbeatTriggerComponent;

/**
 * Minimal, code-only Hapbeat demo: drop ONE actor into an empty level and hit
 * Play. Builds a transient 3-entry UHapbeatEventMap at BeginPlay -- byte-for-
 * byte parity with the Unity SDK's BasicExample
 * (Samples~/BasicExample/BasicExampleEventMap.asset + basic-exam-kit-manifest.json)
 * -- and wires 5 keys through 3 UHapbeatTriggerComponents plus the raw
 * subsystem API:
 *
 *   Space - stream one-shot  (StreamClip, sine_100hz_1s.wav,      gain 1 x intensity 0.5)
 *   R     - stream loop      (same clip, looping)
 *   F     - command play     ("basic-exam-kit.sine_200hz_1s",     gain 1 x intensity 0.5)
 *   S     - stop everything  (UHapbeatSubsystem::StopStream() + StopAll())
 *   C     - ping             (UHapbeatSubsystem::Ping())
 *
 * All three entries target "" (broadcast) and reuse basic-exam-kit, shipped
 * raw under Content/HapbeatSamples/BasicExample/Kit/basic-exam-kit/. Deploy
 * that Kit to a device with Hapbeat Studio for F to produce haptics --
 * Space/R stream raw PCM16 directly and need no Kit installed on the device.
 *
 * Audio is intentionally out of scope: the Unity sample's .ogg SFX would need
 * a USoundWave import (a binary asset), which this C++-only, no-import sample
 * avoids. This demo is haptics only.
 *
 * Usage: create an empty level, place this actor anywhere in it, hit Play,
 * and use the keys above (no pawn possession needed -- BeginPlay calls
 * EnableInput on the first PlayerController it finds). An on-screen HUD shows
 * the key guide plus a live "devices reachable: N" liveness line, refreshed
 * at ~2 Hz from UHapbeatSubsystem::GetAliveDeviceCount().
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatBasicExampleActor : public AActor
{
	GENERATED_BODY()

public:
	AHapbeatBasicExampleActor();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Build the transient 3-entry EventMap and assign EventMap+EntryId on the 3 trigger components. */
	void BuildEventMap();

	/** EnableInput on the first PlayerController found, then BindKey the 5 demo keys. Warns (no-op) if none exists. */
	void BindInput();

	void HandleSpaceKey();
	void HandleRKey();
	void HandleFKey();
	void HandleSKey();
	void HandleCKey();

	/** Fixed on-screen-message keys so the HUD lines update in place instead of stacking. */
	static constexpr int32 KeyGuideHudLineKey = 0;
	static constexpr int32 StatusHudLineKey = 1;
	/** HUD refresh cadence (~2 Hz per the design doc). */
	static constexpr float HudRefreshIntervalSeconds = 0.5f;

	UPROPERTY(Transient)
	TObjectPtr<UHapbeatEventMap> EventMap;

	/**
	 * Strong reference keeping the shared stream-clip WAV alive. Entries only
	 * hold it via a TSoftObjectPtr (not GC-strong -- see
	 * FHapbeatSampleLibrary::LoadSampleClip's GC note), so this actor-owned
	 * UPROPERTY is what actually keeps it resident between BeginPlay and
	 * whenever the player next presses Space/R.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UHapbeatClip> SharedStreamClip;

	// VisibleAnywhere only (no BlueprintReadOnly) -- these are private implementation
	// fields; UHT rejects BlueprintReadOnly on a private member unless it also carries
	// meta=(AllowPrivateAccess="true"), and there is no need to expose them to Blueprint.
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatTriggerComponent> StreamOneShotTrigger;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatTriggerComponent> StreamLoopTrigger;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatTriggerComponent> CommandTrigger;

	/** Counts down to 0 to throttle the HUD refresh to ~2 Hz; fires on the first Tick (starts at 0). */
	float HudRefreshTimer = 0.0f;
};
