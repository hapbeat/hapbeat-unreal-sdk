// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HapbeatParameterBinding.generated.h"

class UCurveFloat;
class UHapbeatSubsystem;
class UHapbeatStreamPlayback;

/**
 * Input source kind for a parameter binding. Picks which value is read from the
 * owning actor each tick (or pushed externally). Mirrors Hapbeat.BindingSourceProperty
 * (Unity SDK) minus the Unity-only LocalScale* / SliderValue sources.
 */
UENUM(BlueprintType)
enum class EHapbeatBindingSource : uint8
{
	/** Owner root component relative location X. */
	LocalPositionX UMETA(DisplayName = "Local Position X"),
	/** Owner root component relative location Y. */
	LocalPositionY UMETA(DisplayName = "Local Position Y"),
	/** Owner root component relative location Z. */
	LocalPositionZ UMETA(DisplayName = "Local Position Z"),
	/** Magnitude of the owner primitive's physics linear velocity (0 if not simulating). */
	VelocityMagnitude UMETA(DisplayName = "Velocity Magnitude"),
	/** Magnitude of the owner primitive's physics angular velocity, in degrees/s. */
	AngularVelocityMagnitude UMETA(DisplayName = "Angular Velocity Magnitude"),
	/**
	 * World-space speed estimated from frame-to-frame actor-location delta
	 * (|cur - prev| / dt). Use this instead of VelocityMagnitude when the body
	 * is kinematic / moved by code (the physics velocity stays 0 even while the
	 * actor visibly moves).
	 */
	PositionDeltaMagnitude UMETA(DisplayName = "Position Delta Magnitude"),
	/**
	 * Value pushed from script / Blueprint via SetValue(). The primary UE path —
	 * route a UMG slider's OnValueChanged to SetValue. No owner component needed.
	 */
	External UMETA(DisplayName = "External"),
};

/** Input-to-output mapping curve. Mirrors Hapbeat.BindingCurveType (Unity SDK). */
UENUM(BlueprintType)
enum class EHapbeatBindingCurve : uint8
{
	Linear UMETA(DisplayName = "Linear"),
	EaseIn UMETA(DisplayName = "Ease In"),
	EaseOut UMETA(DisplayName = "Ease Out"),
	Exponential UMETA(DisplayName = "Exponential"),
	Custom UMETA(DisplayName = "Custom"),
};

/** Which parameter on the active StreamClip playback this binding writes. */
UENUM(BlueprintType)
enum class EHapbeatBindingOutput : uint8
{
	/** Overall gain multiplier (0..2). Applied to every sample before sending. */
	StreamGain UMETA(DisplayName = "Stream Gain"),
	/** Stereo pan (-1..+1). Ignored for mono clips. */
	StreamPan UMETA(DisplayName = "Stream Pan"),
};

/**
 * Maps a runtime variable (owner position / velocity / an external pushed value)
 * onto the subsystem's active StreamClip playback for continuous haptic
 * modulation. Each tick it reads the source, normalizes it into 0..1 against
 * [InputMin, InputMax], applies the curve, lerps into [OutputMin, OutputMax],
 * and writes the result to Gain (ApplyGainModulation) or Pan (SetPan) on the
 * active UHapbeatStreamPlayback. No engine audio is involved — modulation
 * happens entirely on the SDK side just before samples hit the wire.
 *
 * Write one parameter per component; attach two (one StreamGain + one StreamPan)
 * to control both at once. The source must be in StreamClip mode for there to be
 * an active playback to modulate.
 *
 * UE counterpart of Hapbeat.HapbeatParameterBinding (Unity SDK). v1 streams a
 * SINGLE active session, so the binding writes to the subsystem's single
 * GetActivePlayback() directly — the Unity per-event-id / linked-preset scoping
 * (which only matters with multiple concurrent streams) is intentionally dropped.
 */
UCLASS(ClassGroup = (Hapbeat), meta = (BlueprintSpawnableComponent))
class HAPBEATSDK_API UHapbeatParameterBinding : public UActorComponent
{
	GENERATED_BODY()

public:
	UHapbeatParameterBinding();

	// ---- Source ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Source",
		meta = (Tooltip = "Which value to read from the owning actor each tick.\nExternal = pushed via SetValue() (route a UMG slider's OnValueChanged here)."))
	EHapbeatBindingSource SourceProperty = EHapbeatBindingSource::External;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Source",
		meta = (Tooltip = "Input value mapped to OutputMin (the bottom of the source range)."))
	float InputMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Source",
		meta = (Tooltip = "Input value mapped to OutputMax (the top of the source range)."))
	float InputMax = 1.0f;

	// ---- Mapping ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Mapping",
		meta = (Tooltip = "Curve applied to the normalized 0..1 input before the output range lerp."))
	EHapbeatBindingCurve CurveType = EHapbeatBindingCurve::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Mapping",
		meta = (Tooltip = "Custom curve (used when Curve Type = Custom). X: 0-1 normalized input, Y: 0-1 output. Null => linear."))
	TObjectPtr<UCurveFloat> CustomCurve = nullptr;

	// ---- Output ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Output",
		meta = (Tooltip = "Which stream parameter to write.\nStream Gain: overall volume multiplier (0..2).\nStream Pan: stereo pan (-1..+1, mono clips ignore it)."))
	EHapbeatBindingOutput OutputParameter = EHapbeatBindingOutput::StreamGain;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Output",
		meta = (Tooltip = "Output value when the input is at InputMin."))
	float OutputMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Output",
		meta = (Tooltip = "Output value when the input is at InputMax."))
	float OutputMax = 1.0f;

	// ---- API ----

	/**
	 * Push a value into this binding from script / Blueprint (used when
	 * SourceProperty = External). The typical setup routes a UMG slider's
	 * OnValueChanged event here. Stored and read on the next tick.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void SetValue(float Value);

	/**
	 * Run one read -> normalize -> curve -> lerp -> write immediately, outside the
	 * tick. Call this right after a StreamClip starts to pre-seed the first chunk
	 * so the stream does not emit ~100 ms of un-modulated (full-baseline) audio
	 * before the first TickComponent writes a value. Returns the output value.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	float EvaluateNow();

	/** Current raw input value read last tick (before mapping). */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	float GetCurrentInput() const { return CurrentInput; }

	/** Current normalized value (0..1 after the input-range mapping). */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	float GetCurrentNormalized() const { return CurrentNormalized; }

	/** Current output value last written to the playback parameter. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	float GetCurrentOutput() const { return CurrentOutput; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Read the raw source value for SourceProperty. DeltaTime is used by PositionDeltaMagnitude. */
	float ReadSourceValue(float DeltaTime);

	/** raw -> clamp01((raw-InputMin)/(InputMax-InputMin)) -> curve -> lerp(OutputMin,OutputMax). Updates Current*. */
	float ComputeOutput(float Raw);

	/** Apply Out to the subsystem's active playback (StreamGain => ApplyGainModulation, StreamPan => SetPan). */
	void WriteToActivePlayback(float Out);

	/** GetWorld()->GetGameInstance()->GetSubsystem<UHapbeatSubsystem>(), null-guarded (same idiom as the triggers). */
	UHapbeatSubsystem* ResolveSubsystem() const;

	/** Apply the configured curve to a normalized 0..1 input. Mirrors Unity ApplyCurve verbatim. */
	float ApplyCurve(float T) const;

	/** Seed PrevWorldPos + bReset so the first PositionDeltaMagnitude read returns 0 (no spike). */
	void ResetPositionDeltaState();

	/** Value pushed by SetValue(); read when SourceProperty == External. */
	float LastExternalValue = 0.0f;

	/** Previous owner world location for PositionDeltaMagnitude (seeded on BeginPlay / first tick). */
	FVector PrevWorldPos = FVector::ZeroVector;

	/**
	 * True until PrevWorldPos has a valid seed. While set, the first
	 * PositionDeltaMagnitude read seeds PrevWorldPos and returns 0 so re-enabling
	 * the component never emits a spurious spike from a stale position.
	 */
	bool bResetPositionDelta = true;

	// Last-evaluated values (exposed via the BlueprintPure getters for debugging).
	float CurrentInput = 0.0f;
	float CurrentNormalized = 0.0f;
	float CurrentOutput = 0.0f;
};
