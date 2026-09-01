// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HapbeatShowcaseZ4ConsoleWidget.generated.h"

class USlider;
class UTextBlock;
class UHapbeatParameterBinding;
class UHapbeatTriggerComponent;
class USoundBase;

/**
 * Presentation-only base for the Blueprint-authored Z4 console.
 *
 * The Blueprint subclass receives value and detent events and performs every
 * Hapbeat call itself. This class only supplies the two standard UMG sliders
 * and returns focus to the game viewport after a drag.
 */
UCLASS(Abstract, Blueprintable)
class HAPBEATSDKSAMPLES_API UHapbeatShowcaseZ4ConsoleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Z4", meta = (ExposeOnSpawn = true))
	float TickThreshold = 0.1f;

	/** References supplied by the zone Blueprint. The widget never fires haptics itself. */
	UPROPERTY(BlueprintReadWrite, Category = "Z4")
	TObjectPtr<UHapbeatParameterBinding> GainBinding;

	UPROPERTY(BlueprintReadWrite, Category = "Z4")
	TObjectPtr<UHapbeatParameterBinding> PanBinding;

	UPROPERTY(BlueprintReadWrite, Category = "Z4")
	TObjectPtr<UHapbeatTriggerComponent> TickTrigger;

	UPROPERTY(BlueprintReadWrite, Category = "Z4")
	TObjectPtr<USoundBase> TickSound;

	/**
	 * Assign the zone components to this presentation widget. This stores
	 * references only; the generated Widget Blueprint calls SetValue / Fire.
	 */
	UFUNCTION(BlueprintCallable, Category = "Z4")
	void Configure(UHapbeatParameterBinding* InGainBinding, UHapbeatParameterBinding* InPanBinding,
		UHapbeatTriggerComponent* InTickTrigger, USoundBase* InTickSound);

	UFUNCTION(BlueprintImplementableEvent, Category = "Z4")
	void HandleGainValueChanged(float Value);

	UFUNCTION(BlueprintImplementableEvent, Category = "Z4")
	void HandlePanValueChanged(float Value);

	UFUNCTION(BlueprintImplementableEvent, Category = "Z4")
	void HandleTick();

protected:
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void OnGainChanged(float Value);
	UFUNCTION()
	void OnPanChanged(float NormalizedValue);
	UFUNCTION()
	void ReturnFocusToGameViewport();

	void EmitDetents(float OldValue, float NewValue);
	void UpdateLabels();

	UPROPERTY(Transient)
	TObjectPtr<USlider> GainSlider;
	UPROPERTY(Transient)
	TObjectPtr<USlider> PanSlider;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GainLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PanLabel;

	float GainValue = 0.5f;
	float PanValue = 0.0f;
};
