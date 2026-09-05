// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZ4ConsoleWidget.h"

#include "HapbeatParameterBinding.h"
#include "HapbeatTriggerComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Slider.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Sound/SoundBase.h"
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
	BuildConsoleLayout();
	return Super::RebuildWidget();
}

void UHapbeatShowcaseZ4ConsoleWidget::BuildConsoleLayout()
{
	// This runs before UUserWidget creates the Slate tree.  Doing the same work
	// in NativeConstruct is too late: the old designer root has already become
	// the widget displayed by AddToViewport.
	if (WidgetTree == nullptr || GainSlider != nullptr)
	{
		return;
	}

	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ConsoleRoot"));
	WidgetTree->RootWidget = Canvas;
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ConsolePanel"));
	Panel->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f));
	Panel->SetPadding(FMargin(16.0f, 12.0f));
	UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
	PanelSlot->SetAnchors(FAnchors(0.5f, 1.0f));
	PanelSlot->SetAlignment(FVector2D(0.5f, 1.0f));
	PanelSlot->SetPosition(FVector2D(0.0f, -48.0f));
	PanelSlot->SetSize(FVector2D(352.0f, 148.0f));

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ConsoleContent"));
	Panel->SetContent(Content);
	GainLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GainLabel"));
	GainSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("GainSlider"));
	PanLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PanLabel"));
	PanSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("PanSlider"));
	USizeBox* GainSliderBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("GainSliderBox"));
	GainSliderBox->SetWidthOverride(320.0f);
	GainSliderBox->SetHeightOverride(16.0f);
	GainSliderBox->SetContent(GainSlider);
	USizeBox* PanSliderBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanSliderBox"));
	PanSliderBox->SetWidthOverride(320.0f);
	PanSliderBox->SetHeightOverride(16.0f);
	PanSliderBox->SetContent(PanSlider);
	Content->AddChildToVerticalBox(GainLabel)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
	Content->AddChildToVerticalBox(GainSliderBox)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	Content->AddChildToVerticalBox(PanLabel)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
	Content->AddChildToVerticalBox(PanSliderBox)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
	GainSlider->SetValue(GainValue);
	PanSlider->SetValue((PanValue + 1.0f) * 0.5f);
	GainSlider->OnValueChanged.AddDynamic(this, &UHapbeatShowcaseZ4ConsoleWidget::OnGainChanged);
	PanSlider->OnValueChanged.AddDynamic(this, &UHapbeatShowcaseZ4ConsoleWidget::OnPanChanged);
	GainSlider->OnMouseCaptureEnd.AddDynamic(this, &UHapbeatShowcaseZ4ConsoleWidget::ReturnFocusToGameViewport);
	PanSlider->OnMouseCaptureEnd.AddDynamic(this, &UHapbeatShowcaseZ4ConsoleWidget::ReturnFocusToGameViewport);
	UpdateLabels();
}

void UHapbeatShowcaseZ4ConsoleWidget::OnGainChanged(float Value)
{
	const float OldValue = GainValue;
	GainValue = FMath::Clamp(Value, 0.0f, 1.0f);
	HandleGainValueChanged(GainValue);
	EmitDetents(OldValue, GainValue);
	UpdateLabels();
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

void UHapbeatShowcaseZ4ConsoleWidget::UpdateLabels()
{
	if (GainLabel != nullptr) { GainLabel->SetText(FText::FromString(FString::Printf(TEXT("Gain  %.2f"), GainValue))); }
	if (PanLabel != nullptr) { PanLabel->SetText(FText::FromString(FString::Printf(TEXT("Pan  %+.2f"), PanValue))); }
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
