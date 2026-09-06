// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HapbeatShowcaseZ4ConsoleWidget.generated.h"

class UHapbeatParameterBinding;
class UHapbeatTriggerComponent;
class USoundBase;
class SWidget;

/**
 * Presentation-only base for the Blueprint-authored Z4 console.
 *
 * The Blueprint subclass receives slider values and routes them to its
 * Parameter Binding and Tick Trigger components. This class only supplies the
 * native Slate presentation and returns focus to the game viewport after a
 * drag. The presentation uses the same Slate controls as the original Z4
 * console so it is visually consistent with the address-override panel above it.
 */
UCLASS(Abstract, Blueprintable)
class HAPBEATSDKSAMPLES_API UHapbeatShowcaseZ4ConsoleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** References supplied by the zone Blueprint. The widget graph invokes the SDK components. */
	UPROPERTY(BlueprintReadWrite, Category = "Z4")
	TObjectPtr<UHapbeatParameterBinding> GainBinding;

	UPROPERTY(BlueprintReadWrite, Category = "Z4")
	TObjectPtr<UHapbeatParameterBinding> PanBinding;

	UPROPERTY(BlueprintReadWrite, Category = "Z4")
	TObjectPtr<UHapbeatTriggerComponent> TickTrigger;

	/** Retained while generated Showcase assets are rebuilt in place. */
	UPROPERTY(BlueprintReadWrite, Category = "Z4", meta = (DeprecatedProperty, DeprecationMessage = "The generated Widget Blueprint uses Fire Hapbeat Tick From Value."))
	TObjectPtr<USoundBase> TickSound;

	/**
	 * Assign the zone components to this presentation widget. This stores
	 * references only; the generated Widget Blueprint calls SetValue / Evaluate
	 * Now / Fire From Value.
	 */
	UFUNCTION(BlueprintCallable, Category = "Z4")
	void Configure(UHapbeatParameterBinding* InGainBinding, UHapbeatParameterBinding* InPanBinding,
		UHapbeatTriggerComponent* InTickTrigger, USoundBase* InTickSound = nullptr);

	UFUNCTION(BlueprintImplementableEvent, Category = "Z4")
	void HandleGainValueChanged(float Value);

	UFUNCTION(BlueprintImplementableEvent, Category = "Z4")
	void HandlePanValueChanged(float Value);

	/** Compatibility event for previous generated Showcase assets. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Z4", meta = (DeprecatedFunction, DeprecationMessage = "Use Fire Hapbeat Tick From Value in the Widget Blueprint."))
	void HandleTick();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
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
