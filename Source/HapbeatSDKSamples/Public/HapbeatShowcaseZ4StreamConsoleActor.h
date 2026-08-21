// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HapbeatShowcaseZone.h" // IHapbeatShowcaseZone: the switcher asks the zone for its label / keys / spawn
#include "HapbeatShowcaseZ4StreamConsoleActor.generated.h"

class UHapbeatClip;
class UHapbeatEventMap;
class UHapbeatTriggerComponent;
class UHapbeatParameterBinding;
class USoundBase;
class UStaticMeshComponent;
class SWidget;

/**
 * Z4 Stream Console -- live StreamClip gain / pan modulation driven by two
 * ON-SCREEN SLIDERS, the UE counterpart of the Unity Showcase Z4
 * (StreamDemoController.cs + a UI Slider wired to two HapbeatParameterBinding
 * components). Reuses showcase-kit verbatim (z4_stream_loop / z4_slider_tick,
 * both StreamClip mode -- see Samples~/Showcase/EventMaps/ShowcaseEventMap.md
 * for the authoritative gain/intensity table).
 *
 * Controls (Unity parity):
 *   SPACE          - toggle the z4_stream_loop loop (Fire() / Stop()).
 *   Gain slider    - drag with the mouse; pushes 0..1 into GainBinding
 *                    (External source -> StreamGain).
 *   Pan slider     - drag with the mouse; pushes -1..1 into PanBinding
 *                    (External source -> StreamPan).
 *
 * Each slider fires the z4_slider_tick one-shot ONCE PER DETENT rather than on
 * every pixel of drag, using the same snap rule as Unity's HapbeatTickEmitter /
 * TickAudioEmitter in AbsolutePosition mode: quantise the value by
 * TickThreshold and fire only when the quantised index changes. Without that a
 * drag would emit hundreds of events a second.
 *
 * The sliders are Slate built in code (SSlider), added straight to the viewport
 * -- same reasoning as the shared Showcase HUD: no UI .uasset to author, and it
 * still draws in a packaged build. The zone reports WantsCursorUnlocked() so the
 * switcher frees the mouse on entry.
 *
 * IMPORTANT v1 caveat: the runtime supports a SINGLE active stream session with
 * REPLACE semantics (see unreal-sdk-v1-design.md Sec 3.5); Unity's mixer instead
 * truly overlaps concurrent sources. Firing the haptic tick WHILE the loop is
 * playing would replace (permanently stop) the loop, so the haptic tick is
 * skipped while the loop streams -- the gain/pan change itself is still felt in
 * the loop, and the tick's SFX still plays. Local stream mixing is the real fix
 * and is scheduled as its own phase.
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ4StreamConsoleActor : public AActor, public IHapbeatShowcaseZone
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ4StreamConsoleActor();

	// ---- IHapbeatShowcaseZone ----
	virtual FText GetZoneLabel() const override;
	virtual TArray<FHapbeatShowcaseHudCommand> GetHudCommands() const override;
	virtual FTransform GetPlayerSpawnRelative() const override;
	virtual bool WantsCursorUnlocked() const override;

	/**
	 * Local offset applied to this zone's own root (mesh + everything else in
	 * the zone) at BeginPlay, so a future master/layout actor can nudge each
	 * zone into a row slot without altering the zone actor's own placed
	 * transform.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	FVector FootprintOffset = FVector::ZeroVector;

	/**
	 * Detent spacing for both sliders, in slider units. One tick event per
	 * crossed multiple of this value -- Unity HapbeatTickEmitter /
	 * TickAudioEmitter _tickThreshold, AbsolutePosition mode.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float TickThreshold = 0.1f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Resolve the EventMap (asset or fallback) and wire the 2 trigger components to z4_stream_loop / z4_slider_tick. */
	void BuildEventMap();

	/** Build the transient EventMap used when no asset is assigned. */
	UHapbeatEventMap* BuildFallbackEventMap();

	/** EnableInput on the first PlayerController found, then bind Space. Warns (no-op) if none exists. */
	void BindInput();

	void HandleToggleKey(); // Space

	/** Build the two sliders and add them to the viewport; removed again in EndPlay. */
	void CreateSliderPanel();
	/** Take the slider panel back out of the viewport. Safe to call when it was never created. */
	void DestroySliderPanel();

	/** Slider callbacks: store the value, push it to its binding, and emit any detents crossed. */
	void OnGainSliderChanged(float NewValue);
	void OnPanSliderChanged(float NewValue);

	/**
	 * Emit one tick per detent between OldValue and NewValue (see TickThreshold).
	 * Returns nothing -- the count is deliberately capped so a jump (click on the
	 * far end of the track) cannot spray events.
	 */
	void EmitDetentTicks(float OldValue, float NewValue);

	/** Fire TickTrigger (z4_slider_tick) + its SFX for one detent. */
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

	/** Optional imported detent SFX (S_z4_ui_tick); null = silent. */
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> TickSound;

	/** The viewport-hosted slider panel, owned for this zone's lifetime. */
	TSharedPtr<SWidget> SliderPanel;

	/** Last value pushed to GainBinding (0..1); also shown on the HUD. */
	float GainValue = 1.0f;
	/** Last value pushed to PanBinding (-1..1); also shown on the HUD. */
	float PanValue = 0.0f;

	/** Counts down to 0 to throttle the HUD refresh. */
	float HudRefreshTimer = 0.0f;
};
