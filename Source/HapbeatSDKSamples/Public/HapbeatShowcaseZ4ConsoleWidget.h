// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HapbeatShowcaseZ4ConsoleWidget.generated.h"

class UHapbeatTickEmitterComponent;
class USoundBase;
class SWidget;

/** Native slider input forwarded to the owning Z4 Actor Blueprint. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHapbeatShowcaseSliderValueChanged, float, Value);

/**
 * Presentation-only base for the Blueprint-authored Z4 console.
 *
 * This class supplies the native Slate presentation and forwards slider input
 * to the owning Actor Blueprint through delegates. The Actor owns every
 * Hapbeat SDK call, so the haptic wiring is readable in one Event Graph.
 */
UCLASS(Abstract, Blueprintable)
class HAPBEATSDKSAMPLES_API UHapbeatShowcaseZ4ConsoleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Gain slider's normalized 0..1 input, after Z4's display rounding. */
	UPROPERTY(BlueprintAssignable, Category = "Z4|Input", meta = (DisplayName = "On Gain Slider Changed"))
	FHapbeatShowcaseSliderValueChanged OnGainSliderChanged;

	/** Pan slider's -1..+1 input, after conversion from the Slate slider range. */
	UPROPERTY(BlueprintAssignable, Category = "Z4|Input", meta = (DisplayName = "On Pan Slider Changed"))
	FHapbeatShowcaseSliderValueChanged OnPanSliderChanged;

	/**
	 * Assign presentation-only collaborators. The owning Actor Blueprint handles
	 * Gain / Pan Binding writes after receiving the slider delegates above.
	 */
	UFUNCTION(BlueprintCallable, Category = "Z4", meta = (DisplayName = "Configure Stream Console UI"))
	void Configure(UHapbeatTickEmitterComponent* InTickEmitter, USoundBase* InTickSound = nullptr);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TObjectPtr<UHapbeatTickEmitterComponent> TickEmitter;
	TObjectPtr<USoundBase> TickSound;
	enum class EActiveSlider : uint8 { None, Gain, Pan };

	void OnGainChanged(float Value);
	void OnPanChanged(float NormalizedValue);
	void PrepareTickFor(EActiveSlider Slider);
	UFUNCTION()
	void HandleTickFired(AActor* Other, float Speed);
	void ReturnFocusToGameViewport();

	float GainValue = 0.5f;
	float PanValue = 0.0f;
	EActiveSlider ActiveSlider = EActiveSlider::None;
};
