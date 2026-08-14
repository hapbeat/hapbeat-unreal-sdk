// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HapbeatShowcaseZ4StreamConsoleActor.generated.h"

class UHapbeatClip;
class UHapbeatEventMap;
class UHapbeatTriggerComponent;
class UHapbeatParameterBinding;
class UStaticMeshComponent;

/**
 * Z4 Stream Console -- keyboard-only demo of live StreamClip gain / pan
 * modulation via UHapbeatParameterBinding (External source). UE counterpart of
 * the Unity Showcase Z4 (StreamDemoController.cs + a UI Slider wired to 2
 * HapbeatParameterBinding components), reusing showcase-kit verbatim
 * (z4_stream_loop / z4_slider_tick, both StreamClip mode -- see
 * Samples~/Showcase/EventMaps/ShowcaseEventMap.md for the authoritative
 * gain/intensity table).
 *
 * A single cube pedestal represents the console -- there is no UMG slider
 * widget (a Slate/UMG asset would need editor authoring, and the design doc
 * favors a keyboard-only stand-in over binary UI assets). Haptics-only: game
 * SFX is intentionally out of scope (would need a USoundWave import).
 * Controls:
 *
 *   T     - toggle the z4_stream_loop StreamClip loop (Fire() / Stop() on
 *           LoopTrigger, a UHapbeatTriggerComponent).
 *   U / J - step the Gain modulator up / down by GainStep (default 0.1) in
 *           [0, 1] and push it to GainBinding (External source ->
 *           StreamGain). Also fires the z4_slider_tick one-shot for the
 *           "tick-detent" feel, mirroring Unity's HapbeatTickEmitter click.
 *   N / M - step the Pan modulator left / right by PanStep (default 0.2) in
 *           [-1, 1] and push it to PanBinding (External source ->
 *           StreamPan). Also fires the tick one-shot.
 *
 * IMPORTANT v1 caveat: the runtime supports a SINGLE active stream session
 * with REPLACE semantics (see unreal-sdk-v1-design.md Sec 3.5); Unity's
 * mixer instead truly overlaps concurrent sources. Firing the tick one-shot
 * WHILE the loop is playing therefore replaces (permanently stops) the loop
 * session -- press T again to restart it. This mirrors the sample spec's
 * explicit instruction ("call trigger->Fire() per step") rather than
 * silently gating the tick to avoid disrupting the loop; see the Z4/Z5
 * handoff note's "uncertainties" section for the full discussion.
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ4StreamConsoleActor : public AActor
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ4StreamConsoleActor();

	/**
	 * Local offset applied to this zone's own root (mesh + everything else in
	 * the zone) at BeginPlay, so a future master/layout actor can nudge each
	 * zone into a row slot without altering the zone actor's own placed
	 * transform.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	FVector FootprintOffset = FVector::ZeroVector;

	/** Gain step applied per U/J press, in [0, 1] units. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float GainStep = 0.1f;

	/** Pan step applied per N/M press, in [-1, 1] units. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float PanStep = 0.2f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Resolve the EventMap (asset or fallback) and wire the 2 trigger components to z4_stream_loop / z4_slider_tick. */
	void BuildEventMap();

	/** Build the transient EventMap used when no asset is assigned. */
	UHapbeatEventMap* BuildFallbackEventMap();

	/** EnableInput on the first PlayerController found, then BindKey the 5 demo keys. Warns (no-op) if none exists. */
	void BindInput();

	void HandleToggleKey();   // T
	void HandleGainUpKey();   // U
	void HandleGainDownKey(); // J
	void HandlePanLeftKey();  // N
	void HandlePanRightKey(); // M

	/** Clamp-step GainValue, push it to GainBinding, and fire the tick one-shot. */
	void StepGain(float Delta);
	/** Clamp-step PanValue, push it to PanBinding, and fire the tick one-shot. */
	void StepPan(float Delta);
	/** Fire TickTrigger (z4_slider_tick) for the detent-click feel of a step. */
	void FireTick();

	/** Fixed on-screen-message keys, offset into the 400s so they don't collide with other zones' HUD lines. */
	static constexpr int32 KeyGuideHudLineKey = 400;
	static constexpr int32 StatusHudLineKey = 401;
	static constexpr float HudRefreshIntervalSeconds = 0.1f;

	// Constructor-created default subobjects (VisibleAnywhere, not Transient --
	// these ARE part of the CDO / serialized instance, unlike the BeginPlay-time
	// EventMap/Clip data below).

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UStaticMeshComponent> ConsoleMesh;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatTriggerComponent> LoopTrigger;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatTriggerComponent> TickTrigger;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatParameterBinding> GainBinding;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatParameterBinding> PanBinding;

	// BeginPlay-time transient data (built fresh each Play session; not serialized).

	/**
	 * The EventMap this zone plays from. Defaults to the plugin's shipped
	 * EM_Showcase asset (assigned in the constructor), so the gains / modes the
	 * zone actually uses are visible and editable in the editor instead of being
	 * buried in code -- that is how a real project works. Point it at your own
	 * asset to re-author them; clear it and the zone builds an equivalent map in
	 * code, so the sample still runs if the asset ever goes missing.
	 *
	 * Entries are resolved by event name, not by order (see BuildEventMap).
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatEventMap> EventMapOverride;

	UPROPERTY(Transient)
	TObjectPtr<UHapbeatEventMap> EventMap;

	/**
	 * Strong references keeping the 2 stream-clip WAVs alive when the code-built
	 * fallback map is in use -- entries only hold them via a TSoftObjectPtr (not
	 * GC-strong; see FHapbeatSampleLibrary::LoadSampleClip's GC note), so these
	 * actor-owned UPROPERTYs are what actually keep them resident. Left null when
	 * the EM_Showcase asset supplies the clips.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UHapbeatClip> LoopClip;

	UPROPERTY(Transient)
	TObjectPtr<UHapbeatClip> TickClip;

	/** Last value pushed to GainBinding (0..1); also shown on the HUD. */
	float GainValue = 1.0f;
	/** Last value pushed to PanBinding (-1..1); also shown on the HUD. */
	float PanValue = 0.0f;

	/** Counts down to 0 to throttle the HUD refresh. */
	float HudRefreshTimer = 0.0f;
};
