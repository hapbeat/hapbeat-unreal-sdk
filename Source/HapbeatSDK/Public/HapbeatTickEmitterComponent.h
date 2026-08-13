// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatTriggerComponent.h"
#include "HapbeatTickEmitterComponent.generated.h"

/** Which component of a 2D input to track. */
UENUM(BlueprintType)
enum class EHapbeatTickAxis : uint8
{
	/** Track X (horizontal scroll, range-slider min handle). */
	X UMETA(DisplayName = "X"),
	/** Track Y (vertical scroll, range-slider max handle). */
	Y UMETA(DisplayName = "Y"),
	/** Track the vector length. */
	Magnitude UMETA(DisplayName = "Magnitude"),
};

/** Tick detection algorithm. */
UENUM(BlueprintType)
enum class EHapbeatTickMode : uint8
{
	/**
	 * Fixed marks at multiples of the threshold, anchored to zero. One tick per
	 * mark crossing, in either direction. Mark positions do not depend on where
	 * the input started, so every wired control shares the same marks.
	 */
	AbsolutePosition UMETA(DisplayName = "Absolute Position"),
	/**
	 * The anchor moves by +/- the threshold per tick, so marks follow the drag.
	 * One tick per threshold of accumulated motion; small wiggles inside a
	 * single span never accumulate into a tick. The "wheel detent" feel.
	 */
	AccumulatedMotion UMETA(DisplayName = "Accumulated Motion"),
};

/**
 * Fires one haptic per fixed step of a continuous input -- a detent / tick trigger.
 *
 * Wiring a slider's value-changed event straight to Fire() sounds right but
 * fires on every minute jiggle, and the usual patch (a cooldown timer) ties the
 * rate to the wall clock instead of to how far the user actually moved. This
 * component instead emits one fire per TickThreshold units of motion, so a slow
 * drag ticks a few times and a fast drag ticks many.
 *
 * Port of Hapbeat.HapbeatTickEmitter (Unity SDK) -- same two algorithms, same
 * threshold semantics, same 64-tick-per-call safety cap.
 */
UCLASS(ClassGroup = (Hapbeat), meta = (BlueprintSpawnableComponent, DisplayName = "Hapbeat Tick Trigger"))
class HAPBEATSDK_API UHapbeatTickEmitterComponent : public UHapbeatTriggerComponent
{
	GENERATED_BODY()

public:
	UHapbeatTickEmitterComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Tick",
		meta = (Tooltip = "Absolute Position: fixed marks at 0, threshold, 2x threshold ... one tick per crossing.\nAccumulated Motion: the anchor moves per tick, so marks follow the drag."))
	EHapbeatTickMode TickMode = EHapbeatTickMode::AbsolutePosition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Tick",
		meta = (ClampMin = "0.0", Tooltip = "Tick interval, in input units. 0 = fire on any change."))
	float TickThreshold = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Tick",
		meta = (Tooltip = "Which axis of a 2D input to track. Ignored for scalar input."))
	EHapbeatTickAxis Axis = EHapbeatTickAxis::Y;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Tick",
		meta = (Tooltip = "Fire once when the very first value arrives. Usually off, so the enable-time value does not tick."))
	bool bEmitOnInitialValue = false;

	/** Scalar input handler. Bind a slider's value-changed delegate here. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void FireFromValue(float Value);

	/** 2D input handler. Bind a scroll box / range slider here; reads the configured Axis. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void FireFromVector2D(FVector2D Value);

	/** Fire once, bypassing tick detection entirely. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void FireNow();

	/**
	 * Forget the tick anchor. Call after the input jumps discontinuously (a
	 * programmatic snap, say) so the jump does not emit a flurry of ticks.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void ResetReference();

protected:
	virtual void OnUnregister() override;

private:
	/** Shared tick detection for both input handlers. */
	void Process(float Value);

	/** Last observed input value; the anchor both algorithms measure from. */
	float LastValue = 0.0f;
	bool bHasReference = false;
};
