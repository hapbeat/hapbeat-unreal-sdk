// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatTriggerComponent.h"
#include "HapbeatSequenceComponent.generated.h"

/**
 * Three-phase haptic trigger for grab / hold / release interactions. UE
 * counterpart of Hapbeat.HapbeatSequenceTrigger, simplified: the XRI re-entry
 * guard / post-stop cooldown / start-shot delay knobs are DROPPED for v1.
 *
 * Phases:
 *   1. On-Start one-shot (StartEntryId) — impact moment (grab click). Command or
 *      StreamClip; never loops.
 *   2. Loop (the inherited EntryId) — continuous sustain (held rumble / drag). A
 *      looping StreamClip entry. Attach a UHapbeatParameterBinding to modulate
 *      its gain / pan while it runs. Reuses the base FireInternal pipeline.
 *   3. On-Stop one-shot (StopEntryId) — release moment, fired StopShotDelay
 *      seconds after the loop stops (so its STREAM_BEGIN / PLAY doesn't collide
 *      with the loop's ring-flush burst on the device).
 *
 * Wire Fire() to the grab-begin event and Stop() to the grab-end event.
 */
UCLASS(ClassGroup = (Hapbeat), meta = (BlueprintSpawnableComponent))
class HAPBEATSDK_API UHapbeatSequenceComponent : public UHapbeatTriggerComponent
{
	GENERATED_BODY()

public:
	UHapbeatSequenceComponent();

	/** On-Start one-shot entry (impact moment). Invalid GUID = (none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Sequence")
	FGuid StartEntryId;

	/** On-Stop one-shot entry (release moment). Invalid GUID = (none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Sequence")
	FGuid StopEntryId;

	/**
	 * Delay (seconds) between the loop stop and the On-Stop one-shot. Default 0.05
	 * — barely perceptible (~1 audio chunk) but enough to isolate the loop's
	 * ring-flush packet burst from the stop shot on the device. 0 = fire
	 * immediately. Mirrors Unity's _stopShotDelay.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Sequence", meta = (UIMin = "0.0", UIMax = "0.5", ClampMin = "0.0"))
	float StopShotDelay = 0.05f;

	/**
	 * Phase 1+2: fire the On-Start one-shot (StartEntryId) then start the loop
	 * (the inherited EntryId, a looping StreamClip). Overrides the base Fire() so
	 * Blueprint / UnityEvent wiring to "Fire" gets the 3-phase behavior.
	 */
	virtual void Fire() override;

	/**
	 * Phase 3: stop the loop playback, then (after StopShotDelay) fire the On-Stop
	 * one-shot (StopEntryId). Overrides the base Stop().
	 */
	virtual void Stop() override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Timer callback that fires the delayed On-Stop one-shot. */
	void FireStopShot();

	/** Handle for the pending On-Stop one-shot (so EndPlay / a re-Fire can cancel it). */
	FTimerHandle StopShotTimer;
};
