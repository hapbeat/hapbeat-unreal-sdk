// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZ4ConsoleWidget.h"

#include "HapbeatParameterBinding.h"
#include "HapbeatTickEmitterComponent.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SViewport.h"

void UHapbeatShowcaseZ4ConsoleWidget::Configure(UHapbeatParameterBinding* InGainBinding,
	UHapbeatParameterBinding* InPanBinding, UHapbeatTickEmitterComponent* InTickEmitter, USoundBase* InTickSound)
{
	if (TickEmitter != nullptr)
	{
		TickEmitter->OnFired.RemoveDynamic(this, &UHapbeatShowcaseZ4ConsoleWidget::HandleTickFired);
	}
	GainBinding = InGainBinding;
	PanBinding = InPanBinding;
	TickEmitter = InTickEmitter;
	TickSound = InTickSound;
	ActiveSlider = EActiveSlider::None;
	if (TickEmitter != nullptr)
	{
		TickEmitter->OnFired.AddDynamic(this, &UHapbeatShowcaseZ4ConsoleWidget::HandleTickFired);
	}
}

TSharedRef<SWidget> UHapbeatShowcaseZ4ConsoleWidget::RebuildWidget()
{
	return SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 48.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f))
			.Padding(FMargin(16.0f, 12.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						return FText::FromString(FString::Printf(TEXT("Gain  %.2f"), GainValue));
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
					.ColorAndOpacity(FLinearColor::White)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SBox).WidthOverride(320.0f)
					[
						SNew(SSlider)
						.Value_Lambda([this]() { return GainValue; })
						.StepSize(0.01f)
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
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
					.ColorAndOpacity(FLinearColor::White)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SBox).WidthOverride(320.0f)
					[
						SNew(SSlider)
						.Value_Lambda([this]() { return (PanValue + 1.0f) * 0.5f; })
						.StepSize(0.005f)
						.OnValueChanged_Lambda([this](float Value) { OnPanChanged(Value); })
						.OnMouseCaptureEnd_Lambda([this]() { ReturnFocusToGameViewport(); })
					]
				]
			]
		];
}

void UHapbeatShowcaseZ4ConsoleWidget::OnGainChanged(float Value)
{
	GainValue = FMath::Clamp(FMath::RoundToFloat(Value * 100.0f) / 100.0f, 0.0f, 1.0f);
	PrepareTickFor(EActiveSlider::Gain);
	HandleGainValueChanged(GainValue);
}

void UHapbeatShowcaseZ4ConsoleWidget::OnPanChanged(float NormalizedValue)
{
	const float RawPan = NormalizedValue * 2.0f - 1.0f;
	PanValue = FMath::Clamp(FMath::RoundToFloat(RawPan * 100.0f) / 100.0f, -1.0f, 1.0f);
	PrepareTickFor(EActiveSlider::Pan);
	HandlePanValueChanged(PanValue);
}

void UHapbeatShowcaseZ4ConsoleWidget::PrepareTickFor(EActiveSlider Slider)
{
	if (TickEmitter != nullptr && ActiveSlider != Slider)
	{
		// One emitter owns one scalar reference. Reset only when control changes,
		// so the first value from the other slider establishes a fresh reference
		// rather than producing a false cross-control detent.
		TickEmitter->ResetReference();
		ActiveSlider = Slider;
	}
}

void UHapbeatShowcaseZ4ConsoleWidget::HandleTickFired(AActor* Other, float Speed)
{
	if (TickSound != nullptr)
	{
		UGameplayStatics::PlaySound2D(this, TickSound);
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
