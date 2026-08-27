// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HapbeatStreamGainMirror.h"
#include "HapbeatStreamPlayback.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EHapbeatStreamPlaybackStatus : uint8
{
	Deferred,
	Active,
	Stopped
};

UENUM(BlueprintType)
enum class EHapbeatStreamDeferredReason : uint8
{
	None,
	NoResolvedEndpoint
};

/**
 * Handle to an active StreamClip playback. The caller holds this to modulate the
 * stream in real time: write Gain (via ApplyGainModulation) and Pan each frame
 * and the streamer reads them per chunk, pre-multiplying the PCM samples before
 * they hit the wire. No protocol extension — dynamic volume / panning are
 * implemented entirely on the SDK side. This is the UE sibling of Unity's
 * Hapbeat.HapbeatStreamPlayback.
 *
 * Threading: the WRITER (a Phase-4 ParameterBinding TickComponent / a trigger's
 * GainMultiplier / Blueprint) is always GAME THREAD ONLY — this UObject itself
 * must never be touched off the game thread. The READER, however, is a
 * dedicated stream-send thread (FHapbeatStreamRunnable, since the 2026-07-25
 * thread migration — see unreal-sdk-v1-design.md §5) that must NOT dereference
 * this UObject either (GC safety). So every mutator here ALSO write-throughs to
 * GetMirror() — a plain (non-UObject) atomic value mirror the stream thread
 * reads instead. The plain Gain/Pan/bStopped fields below stay as the
 * BlueprintPure getters' backing store (fast game-thread reads); the mirror is
 * the cross-thread channel.
 *
 * Pan semantics: -1 = full left, 0 = centered, +1 = full right. A mono clip is
 * upmixed to stereo by the streamer when the pan is non-zero at stream start, so
 * it pans too — but only if the value was set BEFORE the session began (see
 * SetPan). LINEAR balance is used (center = passthrough,
 * gainL = gainR = 1.0) — NOT equal-power: Hapbeat's left / right actuators are
 * physically separate on the body and do not binaurally sum, so equal-power's
 * sqrt(1/2) compensation would silently attenuate every centered stereo clip by
 * ~3 dB versus the same clip as mono.
 */
UCLASS(BlueprintType)
class HAPBEATSDK_API UHapbeatStreamPlayback : public UObject
{
	GENERATED_BODY()

public:
	/** Stable logical-source identity owned by the StreamHub. */
	UPROPERTY(BlueprintReadOnly, Category = "Hapbeat")
	FGuid Id;

	/** Current endpoint-resolution state for this logical source. */
	UPROPERTY(BlueprintReadOnly, Category = "Hapbeat")
	EHapbeatStreamPlaybackStatus Status = EHapbeatStreamPlaybackStatus::Deferred;

	/** Reason for Deferred; None while Active or Stopped. */
	UPROPERTY(BlueprintReadOnly, Category = "Hapbeat")
	EHapbeatStreamDeferredReason DeferredReason = EHapbeatStreamDeferredReason::NoResolvedEndpoint;
	/**
	 * The gain the entry authored (entry.gain x manifest.intensity), captured at
	 * stream start. A bound stream is BaselineGain x bindingOutput so author
	 * intent ("full press = authored strength") is preserved regardless of which
	 * bindings are attached.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Hapbeat")
	float BaselineGain = 1.0f;

	/**
	 * Initialise the handle for a fresh stream. BaselineGain is frozen here;
	 * Gain starts at clamp(Baseline x InitialModulator, 0, 2); Pan resets to 0
	 * (center / passthrough); the stopped flag is cleared.
	 */
	void Init(float Baseline, float InitialModulator);

	/**
	 * Apply an external gain modulator: Gain = clamp(BaselineGain x Modulator, 0, 2).
	 * Single shared entry point for both the imperative trigger path and the
	 * declarative Phase-4 ParameterBinding so the formula lives in one place.
	 * Typical modulator range is [0, 1] (= 0..authored); up to [0, 2] for boost.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void ApplyGainModulation(float Modulator);

	/**
	 * Set the pan, clamped to [-1, 1].
	 *
	 * Has no effect on a session that STARTED centred with a mono clip: the
	 * streamer only upmixes mono to stereo when the pan is already non-zero at
	 * STREAM_BEGIN, which fixes the channel count for the whole session. To pan a
	 * mono clip, give the pan before the stream starts (the entry's Pan, the Play
	 * node's Pan, or StreamClip's InitialPan).
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void SetPan(float NewPan);

	/**
	 * Request the stream to stop. The streamer notices this between chunks and
	 * sends STREAM_END shortly after. Idempotent.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void Stop();

	/** Current overall gain multiplier applied to every sample before sending. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	float GetGain() const { return Gain; }

	/** Current stereo pan, -1 (full left) .. +1 (full right). */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	float GetPan() const { return Pan; }

	/** True once Stop() has been called (or the clip finished on its own for non-loop). */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	bool IsStopped() const;

	/** True while the stream is still active (not stopped). */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	bool IsActive() const { return Status == EHapbeatStreamPlaybackStatus::Active && !IsStopped(); }

	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	EHapbeatStreamPlaybackStatus GetStatus() const { return IsStopped() ? EHapbeatStreamPlaybackStatus::Stopped : Status; }

	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	EHapbeatStreamDeferredReason GetDeferredReason() const { return DeferredReason; }

	/**
	 * Per-channel LINEAR balance coefficients derived from Pan. Returns
	 * (OutL, OutR). For mono paths the caller uses the overall Gain only.
	 *   pan = 0  : L = R = 1.0  (passthrough; no hidden sqrt(1/2) attenuation)
	 *   pan = -1 : L = 1.0, R = 0  (full left)
	 *   pan = +1 : L = 0,  R = 1.0 (full right)
	 * Deliberately NOT equal-power (see the class comment).
	 */
	void GetStereoChannelGains(float& OutL, float& OutR) const;

	/**
	 * Remember WHO started this playback -- the actor owning the trigger that
	 * fired it. Set by UHapbeatTriggerComponent right after the session starts;
	 * left unset for a playback started straight from the subsystem / Blueprint
	 * (there is no trigger to attribute it to).
	 *
	 * This is what scopes a UHapbeatParameterBinding to the stream it actually
	 * describes. Multiple sources may share one session, so without an origin an
	 * owner-scoped binding could write to the wrong source -- a Z4 slider
	 * binding, ticking in a zone nobody is looking at, silently overwriting the
	 * gain of the Z3 loop every frame. Unity scopes the same way (a binding is
	 * linked to its owner entry / preset owner); this is the UE equivalent of
	 * that LinkedOwnerEntryId scope.
	 */
	void SetOwnerActor(AActor* InOwner);

	/**
	 * The actor whose trigger started this playback, or null when unattributed
	 * (see SetOwnerActor). Defined in the .cpp because AActor is only
	 * forward-declared here.
	 */
	AActor* GetOwnerActor() const;

	/**
	 * Thread-safe atomic mirror of Gain/Pan/bStopped for the stream-send thread
	 * (FHapbeatStreamRunnable) to read WITHOUT ever touching this UObject off
	 * the game thread. Created on first access. C++-only (not BlueprintCallable
	 * — internal plumbing between the playback handle and the streamer); never
	 * returns null.
	 */
	TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> GetMirror();

	/** StreamHub game-thread state transitions. */
	void SetDeferredNoEndpoint();
	void SetActive();

private:
	/** Live gain (= BaselineGain x modulator), clamped to [0, 2]. */
	float Gain = 1.0f;
	/** Live pan, clamped to [-1, 1]. */
	float Pan = 0.0f;
	/** Set once the stream has been asked to stop / has finished. */
	bool bStopped = false;

	/** Lazily created in GetMirror(); every mutator write-throughs to it once created. */
	TSharedPtr<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror;

	/**
	 * Who started this playback (see SetOwnerActor). WEAK on purpose: the handle
	 * is GC-rooted by the subsystem for the stream's lifetime and would otherwise
	 * keep a destroyed actor alive; a binding that finds it stale simply stops
	 * matching, which is the right answer once the origin is gone.
	 */
	TWeakObjectPtr<AActor> OwnerActor;
};
