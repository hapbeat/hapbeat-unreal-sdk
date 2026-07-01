// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatSequenceComponent.h"

#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeat, Log, All);

UHapbeatSequenceComponent::UHapbeatSequenceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHapbeatSequenceComponent::Fire()
{
	if (bVerboseLog)
	{
		UE_LOG(LogHapbeat, Log, TEXT("Sequence Fire() on %s (start=%s loop=%s stop=%s)"),
			*GetNameSafe(GetOwner()), *StartEntryId.ToString(), *EntryId.ToString(), *StopEntryId.ToString());
	}

	// If a previous Stop scheduled an On-Stop one-shot and we re-Fire before it
	// elapses, drop it — firing it now would play the old release shot on top of
	// the new sequence (parity with Unity CancelPendingStopShot in Fire()).
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StopShotTimer);
	}

	// Phase 1: On-Start one-shot (impact). (none) if StartEntryId is invalid.
	FireEntryOneShot(StartEntryId, 1.0f);

	// Phase 2: start the loop on the inherited EntryId via the base pipeline
	// (a looping StreamClip entry; its handle is captured in StoredPlayback for
	// live modulation + Stop()).
	Super::Fire();
}

void UHapbeatSequenceComponent::Stop()
{
	if (bVerboseLog)
	{
		UE_LOG(LogHapbeat, Log, TEXT("Sequence Stop() on %s (stopShotDelay=%.3f)"),
			*GetNameSafe(GetOwner()), StopShotDelay);
	}

	// Phase 3a: stop the loop (base per-source stop -> the streamer sends
	// STREAM_END + ring-flush on the device).
	Super::Stop();

	// Phase 3b: fire the On-Stop one-shot after a short delay so its STREAM_BEGIN /
	// PLAY doesn't collide with the loop's flush burst. Skip the timer entirely
	// when there's no delay or no stop entry (parity with Unity).
	UWorld* World = GetWorld();
	if (World != nullptr && StopShotDelay > 0.0f && StopEntryId.IsValid())
	{
		// Drop any earlier pending stop-shot first (defensive: a rapid Stop/Stop).
		World->GetTimerManager().ClearTimer(StopShotTimer);
		World->GetTimerManager().SetTimer(
			StopShotTimer, this, &UHapbeatSequenceComponent::FireStopShot, StopShotDelay, /*bLoop=*/false);
	}
	else
	{
		FireEntryOneShot(StopEntryId, 1.0f);
	}
}

void UHapbeatSequenceComponent::FireStopShot()
{
	FireEntryOneShot(StopEntryId, 1.0f);
}

void UHapbeatSequenceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Don't leave the stop-shot timer dangling past teardown.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StopShotTimer);
	}
	Super::EndPlay(EndPlayReason);
}
