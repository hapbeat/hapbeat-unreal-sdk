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
 * Fired on the frame this trigger actually sent a haptic -- after every gate
 * (enabled / resolved / cooldown / contact filter) has passed, so a listener
 * never has to reproduce them.
 *
 * It exists so the cosmetic half of an impact (SFX, a flash) can hang off the
 * SAME event as the haptic instead of subscribing to the physics callback
 * separately and re-deriving its own threshold and cooldown -- two filters that
 * drift apart and end up clicking when nothing was felt, or the reverse.
 *
 * @param Other  The other actor involved, when the subclass knows one (a
 *               collision partner). Null for a plain Fire() from Blueprint.
 * @param Speed  Impact speed in cm/s for a collision fire, else 0.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHapbeatTriggerFired, AActor*, Other, float, Speed);

/**
 * Base Hapbeat trigger component — the "fire side" of the SDK. References a
 * UHapbeatEventMap and a specific entry by stable GUID (FHapbeatEventEntry::Id)
 * so reordering / inserting / duplicating entries cannot silently break wiring.
 *
 * This is the UE counterpart of Unity's HapbeatUnityEventTrigger + the code-first
 * Game/event-controller helpers: Blueprint or C++ routes gameplay events to
 * Fire() / FireWithGain() / Stop().
 * Subclasses (collision, sequence) reuse the same resolve + gain-composition +
 * dispatch core via FireInternal().
 *
 * Get the subsystem internally via:
 *   GetWorld()->GetGameInstance()->GetSubsystem<UHapbeatSubsystem>()
 *
 * Gain composition (matches Hapbeat.HapbeatTriggerBase verbatim): this component
 * only composes GainMultiplier x Multiplier and hands it to
 * UHapbeatSubsystem::PlayEntry / StopEntry — the one place an entry turns into a
 * send. PlayEntry folds it in as:
 *   Command    -> wireGain = entry.GetEffectiveGain() x (GainMultiplier x Multiplier)
 *   StreamClip -> baseline = entry.GetEffectiveGain(), initial modulator =
 *                 GainMultiplier x Multiplier
 * The StreamClip multiplier is the INITIAL MODULATOR (not baked into baseline) so
 * a ParameterBinding can modulate further: playback.Gain = baseline x modulator.
 *
 * Latency compensation (UHapbeatConfig::HapticDelaySeconds + the entry's
 * DelayOffsetSeconds) therefore applies to these triggers exactly as it does to
 * the "Play Hapbeat Event" node — it lives inside PlayEntry / StopEntry.
 *
 * NOT spawnable from Add Component on purpose (no BlueprintSpawnableComponent):
 * firing an entry from a graph is the "Play Hapbeat Event" node's job, and a
 * bare trigger component adds nothing over it except an extra place to look for
 * the entry reference. This class stays as the C++ base for the collision /
 * sequence components (which DO carry their own detection logic, and their own
 * BlueprintSpawnableComponent tag — UCLASS meta is not inherited) and for
 * components created from C++.
 */
UCLASS(ClassGroup = (Hapbeat))
class HAPBEATSDK_API UHapbeatTriggerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHapbeatTriggerComponent();

	// ---- Authoring ----

	/** The event map asset containing haptic event definitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat")
	TObjectPtr<UHapbeatEventMap> EventMap;

	// Phase 5: the HapbeatSDKEditor module's FHapbeatTriggerComponentCustomization
	// (a detail customization registered on this base class, which also covers
	// UHapbeatCollisionTriggerComponent / UHapbeatSequenceComponent) replaces this
	// row with a friendly dropdown of the assigned EventMap's entries (by
	// DisplayName, falling back to the event id, falling back to a short guid)
	// whenever EventMap is set. With no EventMap assigned there is nothing to
	// pick from, so this raw FGuid field is shown as-is -- still settable in
	// code, from the Phase-6 samples, or pasted from the EventMap entry's
	// VisibleAnywhere Id field.
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

	/**
	 * Broadcast on the frame a haptic was actually sent (see FHapbeatTriggerFired).
	 * Hang the impact SFX / VFX off this, so sound and haptic share one gate.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Hapbeat")
	FHapbeatTriggerFired OnFired;

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
	 * Stop the referenced event. Command -> Subsystem->StopEntry (same merge
	 * point as the fire, so the same haptic delay applies).
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
	 * Validate an already-resolved entry, compose the per-call multiplier and hand
	 * it to UHapbeatSubsystem::PlayEntry (never Play / StreamClip directly — one
	 * merge point, so the haptic delay and everything added there applies here
	 * too). Shared core for FireInternal (loop / single-shot triggers) and the
	 * sequence component's start/stop one-shots.
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

	/**
	 * Context for the next OnFired broadcast, set by a subclass that knows who it
	 * collided with just before it calls Fire() / FireWithGain(). Cleared by
	 * FireInternal once broadcast, so a later plain Fire() cannot report a stale
	 * partner. A member rather than a Fire() parameter because the fire surface
	 * (Fire / FireWithGain / FireScaled / FireWithCurve) is Blueprint-facing and
	 * must not grow a collision argument nobody outside a collision can supply.
	 */
	TWeakObjectPtr<AActor> FireContextOther;
	float FireContextSpeed = 0.0f;

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
