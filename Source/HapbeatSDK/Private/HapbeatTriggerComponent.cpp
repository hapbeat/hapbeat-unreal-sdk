// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatTriggerComponent.h"

#include "HapbeatEventMap.h"
#include "HapbeatSubsystem.h"
#include "HapbeatStreamPlayback.h"
#include "HapbeatClip.h"
#include "HapbeatParameterBinding.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h" // AActor::GetComponents in PreSeedBindings()
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/App.h" // FApp::GetCurrentTime() fallback in NowUnscaledSeconds()

DEFINE_LOG_CATEGORY_STATIC(LogHapbeat, Log, All);

UHapbeatTriggerComponent::UHapbeatTriggerComponent()
{
	// No per-frame tick: this base only fires on demand (Blueprint/C++ call or a
	// subclass's physics callback). Subclasses that need ticking opt in themselves.
	PrimaryComponentTick.bCanEverTick = false;
}

void UHapbeatTriggerComponent::Fire()
{
	FireInternal(1.0f);
}

void UHapbeatTriggerComponent::FireWithGain(float GainOverride)
{
	FireInternal(GainOverride);
}

void UHapbeatTriggerComponent::FireScaled(float Velocity, float MinVelocity, float MaxVelocity)
{
	// Normalize velocity into [0, 1] over [Min, Max]; guard a zero/inverted range.
	const float Range = MaxVelocity - MinVelocity;
	const float Multiplier = Range > KINDA_SMALL_NUMBER
		? FMath::Clamp((Velocity - MinVelocity) / Range, 0.0f, 1.0f)
		: 0.0f;
	FireInternal(Multiplier);
}

void UHapbeatTriggerComponent::FireWithCurve(float Value, UCurveFloat* Curve)
{
	const float Multiplier = Curve != nullptr ? Curve->GetFloatValue(Value) : 1.0f;
	FireInternal(Multiplier);
}

void UHapbeatTriggerComponent::FireInternal(float Multiplier)
{
	if (bVerboseLog)
	{
		UE_LOG(LogHapbeat, Log, TEXT("Fire() on %s (%s) entryId=%s mult=%.2f"),
			*GetNameSafe(GetOwner()), *GetClass()->GetName(), *EntryId.ToString(), Multiplier);
	}

	if (!bTriggerEnabled)
	{
		if (bVerboseLog)
		{
			UE_LOG(LogHapbeat, Log, TEXT("Fire rejected: trigger disabled on %s"), *GetNameSafe(GetOwner()));
		}
		return;
	}

	UHapbeatSubsystem* Subsystem = ResolveSubsystem();
	if (Subsystem == nullptr)
	{
		return; // warn-once already emitted by ResolveSubsystem
	}

	FHapbeatEventEntry Entry;
	if (!ResolveEntry(Entry))
	{
		return; // warn-once already emitted by ResolveEntry
	}

	// Cooldown gate (unscaled real time, matching Unity Time.unscaledTime). The
	// first fire is never blocked (bHasFired guards the very first call instead of
	// relying on a sentinel timestamp).
	const double Now = NowUnscaledSeconds();
	if (Cooldown > 0.0f && bHasFired && (Now - LastFireTime) < static_cast<double>(Cooldown))
	{
		if (bVerboseLog)
		{
			UE_LOG(LogHapbeat, Log, TEXT("Fire rejected: cooldown (%.2fs) on %s"), Cooldown, *GetNameSafe(GetOwner()));
		}
		return;
	}
	LastFireTime = Now;
	bHasFired = true;

	// Single-trigger fire: honor the entry's own loop flag and keep the resulting
	// StreamClip handle for live modulation / Stop().
	DispatchEntry(Subsystem, Entry, Multiplier, /*bForceNonLoop=*/false, /*bStorePlayback=*/true);
}

void UHapbeatTriggerComponent::DispatchEntry(UHapbeatSubsystem* Subsystem, const FHapbeatEventEntry& Entry,
	float Multiplier, bool bForceNonLoop, bool bStorePlayback)
{
	const FString Target = Entry.Target; // "" = broadcast; passed through verbatim

	switch (Entry.Mode)
	{
	case EHapticMode::Command:
	{
		const FString EventId = Entry.GetEventId();
		if (EventId.IsEmpty())
		{
			if (bVerboseLog)
			{
				UE_LOG(LogHapbeat, Log, TEXT("Fire rejected: Command mode but event id is empty on %s"), *GetNameSafe(GetOwner()));
			}
			return;
		}

		// Device is a pure executor (plays req.gain verbatim, never reads the
		// manifest), so the SDK pre-multiplies intensity here. GetEffectiveGain()
		// = entry.Gain x CachedManifestIntensity (or plain Gain when unresolved).
		if (Entry.CachedManifestIntensity < 0.0f && !bWarnedMissingIntensity)
		{
			UE_LOG(LogHapbeat, Warning,
				TEXT("Command entry on %s has no cached manifest intensity; firing at plain gain=%.2f (intensity factor skipped). Run 'Refresh Intensities' on the EventMap and confirm the Kit is deployed."),
				*GetNameSafe(GetOwner()), Entry.Gain);
			bWarnedMissingIntensity = true;
		}

		if (bVerboseLog)
		{
			UE_LOG(LogHapbeat, Log, TEXT("Fire Command: eventId='%s' target='%s' gain=%.2f x triggerMult=%.2f x callMult=%.2f = %.2f"),
				*EventId, Target.IsEmpty() ? TEXT("(broadcast)") : *Target,
				Entry.GetEffectiveGain(), GainMultiplier, Multiplier,
				Entry.GetEffectiveGain() * GainMultiplier * Multiplier);
		}
		// Through PlayEntry, not Play(): it is the single place where an entry
		// turns into a send, so the haptic delay (and anything added there later)
		// applies to trigger components too. It composes the same wire gain
		// (effGain x multiplier) from the entry itself.
		Subsystem->PlayEntry(EventMap, Entry.Id, GainMultiplier * Multiplier);
		break;
	}

	case EHapticMode::StreamClip:
	{
		UHapbeatClip* Clip = Entry.StreamClip.LoadSynchronous();
		if (Clip == nullptr)
		{
			if (!bWarnedNullClip)
			{
				UE_LOG(LogHapbeat, Warning,
					TEXT("StreamClip entry on %s has no clip assigned (or it failed to load); nothing streamed."),
					*GetNameSafe(GetOwner()));
				bWarnedNullClip = true;
			}
			return;
		}

		if (Entry.CachedManifestIntensity < 0.0f && !bWarnedMissingIntensity)
		{
			UE_LOG(LogHapbeat, Warning,
				TEXT("StreamClip entry on %s has no cached manifest intensity; baseline uses plain gain=%.2f (intensity factor skipped). Run 'Refresh Intensities' on the EventMap."),
				*GetNameSafe(GetOwner()), Entry.Gain);
			bWarnedMissingIntensity = true;
		}

		// baseline = author intent (entry.gain x manifest.intensity), frozen at
		// stream start. The per-trigger multiplier x call multiplier is the
		// INITIAL MODULATOR (not baked into baseline) so a ParameterBinding can
		// modulate further: playback.Gain = baseline x modulator. PlayEntry keeps
		// exactly that split and computes the initial Gain internally (Init()).
		// One-shots force non-loop (Unity DispatchOneShot passes loop:false).
		const bool bLoop = bForceNonLoop ? false : Entry.bLoop;
		const float InitialMod = GainMultiplier * Multiplier;
		if (bVerboseLog)
		{
			UE_LOG(LogHapbeat, Log, TEXT("Fire StreamClip: clip='%s' target='%s' baseline=%.2f initialMod=%.2f loop=%d store=%d"),
				*GetNameSafe(Clip), Target.IsEmpty() ? TEXT("(broadcast)") : *Target,
				Entry.GetEffectiveGain(), InitialMod, bLoop ? 1 : 0, bStorePlayback ? 1 : 0);
		}
		UHapbeatStreamPlayback* Handle = Subsystem->PlayEntry(EventMap, Entry.Id, InitialMod, bForceNonLoop);
		if (bStorePlayback)
		{
			StoredPlayback = Handle;
		}
		// Pre-seed any ParameterBinding on this actor so the FIRST chunks already
		// carry its value (else the stream plays at full baseline for up to one
		// send-ahead window -> audible burst). Parity with Unity's EvaluateNow.
		if (Handle != nullptr)
		{
			PreSeedBindings();
		}
		break;
	}
	}
}

void UHapbeatTriggerComponent::FireEntryOneShot(const FGuid& Id, float Multiplier)
{
	if (!Id.IsValid())
	{
		return; // (none) — phase deliberately unset
	}
	if (!bTriggerEnabled)
	{
		return;
	}

	UHapbeatSubsystem* Subsystem = ResolveSubsystem();
	if (Subsystem == nullptr || EventMap == nullptr)
	{
		return;
	}

	FHapbeatEventEntry Entry;
	if (!EventMap->FindById(Id, Entry))
	{
		if (bVerboseLog)
		{
			UE_LOG(LogHapbeat, Warning, TEXT("Sequence one-shot: entry id '%s' not found in EventMap '%s' on %s"),
				*Id.ToString(), *GetNameSafe(EventMap), *GetNameSafe(GetOwner()));
		}
		return;
	}

	// One-shot: never loop, never store the handle (must not clobber the loop's
	// StoredPlayback). Composition is identical to a normal fire.
	DispatchEntry(Subsystem, Entry, Multiplier, /*bForceNonLoop=*/true, /*bStorePlayback=*/false);
}

void UHapbeatTriggerComponent::PreSeedBindings()
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}
	// v1 is single-active-stream, so every binding on this actor targets the one
	// active playback. EvaluateNow() makes each write its current value before the
	// streamer's first chunk goes out.
	TArray<UHapbeatParameterBinding*> Bindings;
	Owner->GetComponents<UHapbeatParameterBinding>(Bindings);
	for (UHapbeatParameterBinding* Binding : Bindings)
	{
		if (Binding != nullptr)
		{
			Binding->EvaluateNow();
		}
	}
}

void UHapbeatTriggerComponent::Stop()
{
	UHapbeatSubsystem* Subsystem = ResolveSubsystem();
	if (Subsystem == nullptr)
	{
		return;
	}

	FHapbeatEventEntry Entry;
	if (!ResolveEntry(Entry))
	{
		return;
	}

	switch (Entry.Mode)
	{
	case EHapticMode::Command:
	{
		const FString EventId = Entry.GetEventId();
		if (EventId.IsEmpty())
		{
			return;
		}
		// Through StopEntry (the Play counterpart's merge point) so this stop is
		// held back by the SAME haptic delay the fire was — otherwise a delayed
		// Play followed by an immediate Stop would shorten, or even overtake,
		// the event on the device. It reads the target off the entry itself.
		Subsystem->StopEntry(EventMap, Entry.Id);
		break;
	}

	case EHapticMode::StreamClip:
	{
		// Per-source stop only (never StopStream on the whole session — parity
		// with Unity, where the mixer auto-removes the source on IsStopped).
		if (UHapbeatStreamPlayback* Playback = StoredPlayback.Get())
		{
			if (Playback->IsActive())
			{
				Playback->Stop();
			}
		}
		StoredPlayback.Reset();
		break;
	}
	}
}

void UHapbeatTriggerComponent::SetGainMultiplier(float NewMultiplier)
{
	GainMultiplier = FMath::Clamp(NewMultiplier, 0.0f, 2.0f);
	// Push live to the active StreamClip playback via its single ApplyGainModulation
	// entry point (shared with the ParameterBinding so the gain formula lives in
	// one place). Parity with Unity's GainMultiplier setter.
	if (UHapbeatStreamPlayback* Playback = StoredPlayback.Get())
	{
		if (Playback->IsActive())
		{
			Playback->ApplyGainModulation(GainMultiplier);
		}
	}
}

void UHapbeatTriggerComponent::SetStreamPan(float NewPan)
{
	if (UHapbeatStreamPlayback* Playback = StoredPlayback.Get())
	{
		if (Playback->IsActive())
		{
			Playback->SetPan(NewPan);
		}
	}
}

UHapbeatStreamPlayback* UHapbeatTriggerComponent::GetActivePlayback() const
{
	return StoredPlayback.Get();
}

UHapbeatSubsystem* UHapbeatTriggerComponent::ResolveSubsystem()
{
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	UHapbeatSubsystem* Subsystem = GameInstance != nullptr
		? GameInstance->GetSubsystem<UHapbeatSubsystem>()
		: nullptr;

	if (Subsystem == nullptr && !bWarnedNoSubsystem)
	{
		UE_LOG(LogHapbeat, Warning,
			TEXT("No UHapbeatSubsystem available for the trigger on %s (no GameInstance / running outside PIE?); fire ignored."),
			*GetNameSafe(GetOwner()));
		bWarnedNoSubsystem = true;
	}
	return Subsystem;
}

bool UHapbeatTriggerComponent::ResolveEntry(FHapbeatEventEntry& Out)
{
	if (EventMap == nullptr)
	{
		if (!bWarnedNoEventMap)
		{
			UE_LOG(LogHapbeat, Warning,
				TEXT("Trigger on %s has no EventMap assigned; fire ignored. Assign a UHapbeatEventMap in the Details panel."),
				*GetNameSafe(GetOwner()));
			bWarnedNoEventMap = true;
		}
		return false;
	}

	if (EventMap->FindById(EntryId, Out))
	{
		return true;
	}

	// Stale / unassigned id — entry was deleted or never set. Warn once.
	if (!bWarnedStaleId)
	{
		UE_LOG(LogHapbeat, Warning,
			TEXT("Trigger on %s: entry id '%s' not found in EventMap '%s' (%d entries). Re-assign the entry in the Details panel."),
			*GetNameSafe(GetOwner()), *EntryId.ToString(), *GetNameSafe(EventMap), EventMap->Entries.Num());
		bWarnedStaleId = true;
	}
	return false;
}

double UHapbeatTriggerComponent::NowUnscaledSeconds() const
{
	// Unscaled wall-clock seconds (independent of time dilation / pause), the UE
	// equivalent of Unity's Time.unscaledTime used for cooldown. Falls back to the
	// app clock when no world is available.
	if (const UWorld* World = GetWorld())
	{
		return World->GetRealTimeSeconds();
	}
	return FApp::GetCurrentTime();
}
