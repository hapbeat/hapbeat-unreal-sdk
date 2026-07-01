// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HapbeatEventEntry.h"
#include "HapbeatTriggerComponent.generated.h"

class UHapbeatEventMap;
class UHapbeatSubsystem;
class UHapbeatStreamPlayback;
class UCurveFloat;

/**
 * Base Hapbeat trigger component — the "fire side" of the SDK. References a
 * UHapbeatEventMap and a specific entry by stable GUID (FHapbeatEventEntry::Id)
 * so reordering / inserting / duplicating entries cannot silently break wiring.
 *
 * This is the UE counterpart of Unity's HapbeatUnityEventTrigger + the code-first
 * "Bridge" helpers (which in Unity lived on a separate HapbeatBridge): Blueprint
 * or C++ wires a UnityEvent-style call to Fire() / FireWithGain() / Stop().
 * Subclasses (collision, sequence) reuse the same resolve + gain-composition +
 * dispatch core via FireInternal().
 *
 * Get the subsystem internally via:
 *   GetWorld()->GetGameInstance()->GetSubsystem<UHapbeatSubsystem>()
 *
 * Gain composition (matches Hapbeat.HapbeatTriggerBase verbatim):
 *   Command    -> wireGain = entry.GetEffectiveGain() x GainMultiplier x Multiplier;
 *                 Subsystem->Play(eventId, wireGain, target).
 *   StreamClip -> baseline   = entry.GetEffectiveGain();
 *                 initialMod = GainMultiplier x Multiplier;
 *                 Subsystem->StreamClip(clip, baseline, initialMod, target, entry.bLoop).
 * The StreamClip multiplier is the INITIAL MODULATOR (not baked into baseline) so
 * a ParameterBinding can modulate further: playback.Gain = baseline x modulator.
 *
 * Latency compensation (Unity's hapticDelaySeconds / DelayOffsetSeconds deferral)
 * is DROPPED for v1 — every fire goes out immediately. Revisit in L2.
 */
UCLASS(ClassGroup = (Hapbeat), meta = (BlueprintSpawnableComponent))
class HAPBEATSDK_API UHapbeatTriggerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHapbeatTriggerComponent();

	// ---- Authoring ----

	/** The event map asset containing haptic event definitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat")
	TObjectPtr<UHapbeatEventMap> EventMap;

	// TODO(Phase 5): a friendly entry-name dropdown via a detail customization /
	// meta=(GetOptions=...) so designers pick the event by DisplayName instead of
	// pasting a raw GUID. For now EntryId is set in code, by the Phase-6 samples,
	// or pasted from the EventMap entry's VisibleAnywhere Id field.
	/** Stable GUID of the referenced entry. Invalid (all-zero) = unassigned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat")
	FGuid EntryId;

	// ---- Trigger settings ----

	/** Enable or disable this trigger. When false, Fire()/Stop() are no-ops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat")
	bool bTriggerEnabled = true;

	/** Minimum time between firings (seconds). 0 = no cooldown. Measured on unscaled real time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat", meta = (ClampMin = "0.0"))
	float Cooldown = 0.0f;

	/**
	 * Per-trigger gain multiplier. Composes on top of the entry gain so the same
	 * EventMap entry can drive different intensities across actors without
	 * authoring extra entries. Final command gain = entry.GetEffectiveGain() x
	 * GainMultiplier (x any FireWithGain / scaled override). Range [0, 2].
	 *
	 * While a StreamClip is playing, the setter pushes the new modulator straight
	 * to the active playback (playback.Gain = baseline x GainMultiplier), matching
	 * Unity's GainMultiplier property for live script-driven modulation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat", meta = (UIMin = "0.0", UIMax = "2.0", ClampMin = "0.0", ClampMax = "2.0"))
	float GainMultiplier = 1.0f;

	/** Log every Fire()/Stop() call and early-return reason to the output log. Debugging only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat")
	bool bVerboseLog = false;

	// ---- Fire surface (Blueprint / C++ / UnityEvent-equivalent) ----

	/**
	 * Fire the referenced event at the entry's effective gain x GainMultiplier.
	 * Virtual so UHapbeatSequenceComponent can override with its 3-phase behavior
	 * while keeping a single "Fire" entry point for Blueprint / UnityEvent wiring.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	virtual void Fire();

	/**
	 * Fire with an explicit gain override that multiplies into the composition
	 * (Command: x GainMultiplier x Override; StreamClip: initial modulator =
	 * GainMultiplier x Override). Useful for animation-event float params.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void FireWithGain(float GainOverride);

	/**
	 * Fire scaled by a velocity, normalized to [0, 1] over [MinVelocity, MaxVelocity]
	 * and used as the multiplier (parity with the code-first FireScaled helper).
	 * Values at/below Min map to 0; at/above Max map to 1.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void FireScaled(float Velocity, float MinVelocity = 0.0f, float MaxVelocity = 10.0f);

	/**
	 * Fire with the multiplier sampled from a curve at Value. If Curve is null the
	 * multiplier is 1.0 (plain fire). The curve output is used verbatim as the
	 * multiplier (clamping, if any, is the curve author's choice; the composed
	 * gain is clamped by the subsystem / playback ceilings).
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void FireWithCurve(float Value, UCurveFloat* Curve);

	/**
	 * Stop the referenced event. Command -> Subsystem->Stop(eventId, target).
	 * StreamClip -> stop the active playback this trigger started (per-source,
	 * never the whole stream session).
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	virtual void Stop();

	/**
	 * Set GainMultiplier AND, if a StreamClip is currently playing, push the new
	 * value to the active playback live (playback.Gain = baseline x GainMultiplier).
	 * Use this for per-frame script-driven gain modulation — a plain assignment to
	 * the GainMultiplier field does NOT push to the playback (UProperties have no
	 * setter hook), unlike Unity's GainMultiplier property. Clamped to [0, 2].
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void SetGainMultiplier(float NewMultiplier);

	/**
	 * Set the stereo pan of the active StreamClip playback live, [-1, 1]
	 * (imperative counterpart to a Pan ParameterBinding). No-op if nothing is
	 * streaming. Ignored for mono clips by the playback handle.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void SetStreamPan(float NewPan);

	/** Active StreamClip playback handle this trigger started, or nullptr. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	UHapbeatStreamPlayback* GetActivePlayback() const;

protected:
	/**
	 * Resolve + cooldown-gate the inherited EntryId, then compose gain and dispatch
	 * Command / StreamClip. Multiplier is the per-call factor (1 for Fire(), the
	 * override for FireWithGain, the normalized velocity for FireScaled, the curve
	 * sample for FireWithCurve). Returns silently (warn-once) on disabled / missing
	 * map / stale id / cooldown. The resulting StreamClip handle (if any) is stored
	 * in StoredPlayback.
	 */
	void FireInternal(float Multiplier);

	/**
	 * Compose gain for an already-resolved entry and dispatch it through the
	 * subsystem. Shared core for FireInternal (loop / single-shot triggers) and the
	 * sequence component's start/stop one-shots so the gain formula lives in one
	 * place.
	 *
	 * @param Subsystem      Resolved subsystem (non-null).
	 * @param Entry          Resolved entry.
	 * @param Multiplier     Per-call factor folded as: Command wire = effGain x
	 *                       GainMultiplier x Multiplier; StreamClip initialMod =
	 *                       GainMultiplier x Multiplier.
	 * @param bForceNonLoop  When true, a StreamClip entry is streamed as a one-shot
	 *                       regardless of its bLoop flag (used for sequence start /
	 *                       stop shots, mirroring Unity's DispatchOneShot loop:false).
	 * @param bStorePlayback When true, a StreamClip handle is captured into
	 *                       StoredPlayback (the loop phase / single triggers); false
	 *                       for fire-and-forget one-shots that must not clobber the
	 *                       loop handle.
	 */
	void DispatchEntry(UHapbeatSubsystem* Subsystem, const FHapbeatEventEntry& Entry,
		float Multiplier, bool bForceNonLoop, bool bStorePlayback);

	/**
	 * Resolve an arbitrary entry id (not the inherited EntryId) and fire it as a
	 * one-shot (StreamClip forced non-loop, handle not stored). No-op for an
	 * invalid / unset / unknown id. Used by UHapbeatSequenceComponent for its
	 * On-Start / On-Stop phases.
	 */
	void FireEntryOneShot(const FGuid& Id, float Multiplier);

	/**
	 * After a StreamClip starts, run EvaluateNow() on every UHapbeatParameterBinding
	 * on this actor so the first sent chunks already carry the binding's value (else
	 * the stream plays at full baseline for up to one send-ahead window -> audible
	 * burst). Parity with Unity FireHapticImmediate's EvaluateNow pre-seed.
	 */
	void PreSeedBindings();

	/** Resolve the owning subsystem, or nullptr (warn-once) if unavailable. */
	UHapbeatSubsystem* ResolveSubsystem();

	/**
	 * Resolve the referenced entry into Out by stable GUID. Returns false (warn-
	 * once on a stale/unknown id) when EventMap is null or the id is invalid /
	 * not present.
	 */
	bool ResolveEntry(FHapbeatEventEntry& Out);

	/** Unscaled wall-clock seconds for cooldown (parity with Unity Time.unscaledTime). */
	double NowUnscaledSeconds() const;

	/** Handle to the StreamClip playback started by this trigger (cleared when it stops). */
	TWeakObjectPtr<UHapbeatStreamPlayback> StoredPlayback;

	/** Unscaled real-time seconds of the last successful fire (valid only when bHasFired). */
	double LastFireTime = 0.0;
	/** False until the first successful fire, so the cooldown gate never blocks the first one. */
	bool bHasFired = false;

	// One-shot warning gates so misconfiguration prints once, not every frame.
	bool bWarnedNoSubsystem = false;
	bool bWarnedNoEventMap = false;
	bool bWarnedStaleId = false;
	bool bWarnedMissingIntensity = false;
	bool bWarnedNullClip = false;
};
