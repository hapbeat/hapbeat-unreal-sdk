// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZ4ConsoleWidget.h"

#include "HapbeatParameterBinding.h"
#include "HapbeatTriggerComponent.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Sound/SoundBase.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SViewport.h"

void UHapbeatShowcaseZ4ConsoleWidget::Configure(UHapbeatParameterBinding* InGainBinding,
	UHapbeatParameterBinding* InPanBinding, UHapbeatTriggerComponent* InTickTrigger, USoundBase* InTickSound)
{
	GainBinding = InGainBinding;
	PanBinding = InPanBinding;
	TickTrigger = InTickTrigger;
	TickSound = InTickSound;
}

TSharedRef<SWidget> UHapbeatShowcaseZ4ConsoleWidget::RebuildWidget()
{
	return SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 48.0f))
		[
			SNew(SBorder)
			.Padding(FMargin(16.0f, 12.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						return FText::FromString(FString::Printf(TEXT("Gain  %.2f"), GainValue));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SBox).WidthOverride(320.0f)
					[
						SNew(SSlider)
						.Value_Lambda([this]() { return GainValue; })
						.OnValueChanged_Lambda([this](float Value) { OnGainChanged(Value); })
						.OnMouseCaptureEnd_Lambda([this]() { ReturnFocusToGameViewport(); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						return FText::FromString(FString::Printf(TEXT("Pan  %+.2f"), PanValue));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SBox).WidthOverride(320.0f)
					[
						SNew(SSlider)
						.Value_Lambda([this]() { return (PanValue + 1.0f) * 0.5f; })
						.OnValueChanged_Lambda([this](float Value) { OnPanChanged(Value); })
						.OnMouseCaptureEnd_Lambda([this]() { ReturnFocusToGameViewport(); })
					]
				]
			]
		];
}

void UHapbeatShowcaseZ4ConsoleWidget::OnGainChanged(float Value)
{
	const float OldValue = GainValue;
	GainValue = FMath::Clamp(Value, 0.0f, 1.0f);
	HandleGainValueChanged(GainValue);
	EmitDetents(OldValue, GainValue);
}

void UHapbeatShowcaseZ4ConsoleWidget::OnPanChanged(float NormalizedValue)
{
	const float OldValue = PanValue;
	PanValue = FMath::Clamp(NormalizedValue * 2.0f - 1.0f, -1.0f, 1.0f);
	HandlePanValueChanged(PanValue);
	EmitDetents(OldValue, PanValue);
	UpdateLabels();
}

void UHapbeatShowcaseZ4ConsoleWidget::EmitDetents(float OldValue, float NewValue)
{
	if (TickThreshold <= 0.0f)
	{
		return;
	}
	const int32 OldBand = FMath::FloorToInt(OldValue / TickThreshold);
	const int32 NewBand = FMath::FloorToInt(NewValue / TickThreshold);
	for (int32 Index = 0; Index < FMath::Min(FMath::Abs(NewBand - OldBand), 64); ++Index)
	{
		HandleTick();
	}
}

void UHapbeatShowcaseZ4ConsoleWidget::ReturnFocusToGameViewport()
{
	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		if (UWorld* World = PlayerController->GetWorld())
		{
			if (UGameViewportClient* Viewport = World->GetGameViewport())
			{
				if (const TSharedPtr<SViewport> ViewportWidget = Viewport->GetGameViewportWidget())
				{
					Mode.SetWidgetToFocus(ViewportWidget);
				}
			}
		}
		PlayerController->SetInputMode(Mode);
	}
}
