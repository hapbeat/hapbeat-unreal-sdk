// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZ4StreamConsoleActor.h"

#include "HapbeatAddressOverridePanelComponent.h"
#include "HapbeatClip.h"
#include "HapbeatEventMap.h"
#include "HapbeatParameterBinding.h"
#include "HapbeatSampleLibrary.h"
#include "HapbeatStreamPlayback.h"
#include "HapbeatTriggerComponent.h"

#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "InputCoreTypes.h" // EKeys::*
#include "Kismet/GameplayStatics.h" // PlaySound2D
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "Styling/CoreStyle.h" // FCoreStyle::Get().GetBrush("WhiteBrush")
#include "Widgets/SBoxPanel.h"
#include "Widgets/SViewport.h" // SViewport (focus target after a slider drag)
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatShowcaseZ4, Log, All);

AHapbeatShowcaseZ4StreamConsoleActor::AHapbeatShowcaseZ4StreamConsoleActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// Nothing to look at in the world: this zone's whole interface is the Slate
	// panel it puts on screen, exactly as Unity's Z4 is UI and nothing else. The
	// root is just the transform the switcher spawns / places the zone at.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	LoopTrigger = CreateDefaultSubobject<UHapbeatTriggerComponent>(TEXT("LoopTrigger"));
	TickTrigger = CreateDefaultSubobject<UHapbeatTriggerComponent>(TEXT("TickTrigger"));

	// GainBinding: External source, input 0..1 Linear -> output 0..1 (StreamGain).
	// Matches ShowcaseEventMap.md's Z4_stream_loop binding #1 exactly.
	GainBinding = CreateDefaultSubobject<UHapbeatParameterBinding>(TEXT("GainBinding"));
	GainBinding->SourceProperty = EHapbeatBindingSource::External;
	GainBinding->InputMin = 0.0f;
	GainBinding->InputMax = 1.0f;
	GainBinding->CurveType = EHapbeatBindingCurve::Linear;
	GainBinding->OutputParameter = EHapbeatBindingOutput::StreamGain;
	GainBinding->OutputMin = 0.0f;
	GainBinding->OutputMax = 1.0f;

	// PanBinding: External source, input -1..1 Linear -> output -1..1 (StreamPan).
	// Matches ShowcaseEventMap.md's Z4_stream_loop binding #2 exactly.
	PanBinding = CreateDefaultSubobject<UHapbeatParameterBinding>(TEXT("PanBinding"));
	PanBinding->SourceProperty = EHapbeatBindingSource::External;
	PanBinding->InputMin = -1.0f;
	PanBinding->InputMax = 1.0f;
	PanBinding->CurveType = EHapbeatBindingCurve::Linear;
	PanBinding->OutputParameter = EHapbeatBindingOutput::StreamPan;
	PanBinding->OutputMin = -1.0f;
	PanBinding->OutputMax = 1.0f;

	// The SDK's address panel, in a strip across the top of the screen so it
	// cannot cover the sliders this zone is really about. Unity's Z4 persists the
	// choice (this demo exists for the one-build-many-seats case, where the point
	// is that the machine remembers which Hapbeat it is bound to), so this does
	// too. Shown / hidden with the zone rather than at BeginPlay -- see
	// OnZoneActivated.
	AddressPanel = CreateDefaultSubobject<UHapbeatAddressOverridePanelComponent>(TEXT("AddressPanel"));
	AddressPanel->bShowOnBeginPlay = false;
	AddressPanel->bPersistOnApply = true;
	AddressPanel->ViewportHAlign = HAlign_Center;
	AddressPanel->ViewportVAlign = VAlign_Top;
	// 8 px clear of the top edge, the same inset Unity's AddressOverrideDemo
	// gives its panel under the top anchor.
	AddressPanel->ViewportPadding = FMargin(0.0f, 8.0f, 0.0f, 0.0f);
	// Unity's panel footprint. The height is a minimum rather than a cap (see
	// the property), so the four buttons cannot end up clipped off the bottom.
	AddressPanel->ViewportSize = FVector2D(404.0f, 108.0f);

	// Default to the Showcase Event Map that ships with the plugin, so this zone
	// runs against the same authored asset a real project would edit -- gains and
	// modes visible in the editor rather than buried in the code below. Still a
	// UPROPERTY, so it can be pointed at a different map in the details panel;
	// the code-built fallback only runs if this asset ever goes missing.
	static ConstructorHelpers::FObjectFinder<UHapbeatEventMap> DefaultEventMap(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/EM_Showcase.EM_Showcase"));
	if (DefaultEventMap.Succeeded())
	{
		EventMapOverride = DefaultEventMap.Object;
	}
}

void AHapbeatShowcaseZ4StreamConsoleActor::BeginPlay()
{
	Super::BeginPlay();

	TickSound = FHapbeatSampleLibrary::LoadShowcaseAsset<USoundBase>(TEXT("Sounds"), TEXT("S_z4_ui_tick"));

	BuildEventMap();
	BindInput();

	CreateSliderPanel();
	if (AddressPanel != nullptr)
	{
		// Up from the start, like the slider panel, so the zone works dropped
		// into a bare level where nothing ever calls OnZoneActivated. Under the
		// switcher, every zone but the active one is deactivated immediately
		// after BeginPlay, which takes it back down again.
		AddressPanel->Show();
	}

	// Push the initial Gain/Pan so a StreamClip started later (T) picks them up
	// immediately via PreSeedBindings() rather than defaulting to raw baseline.
	if (GainBinding != nullptr)
	{
		GainBinding->SetValue(GainValue);
	}
	if (PanBinding != nullptr)
	{
		PanBinding->SetValue(PanValue);
	}
}

void AHapbeatShowcaseZ4StreamConsoleActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnZoneDeactivated();
	Super::EndPlay(EndPlayReason);
}

void AHapbeatShowcaseZ4StreamConsoleActor::OnZoneActivated()
{
	// The panels belong to the visible zone only: a hidden Z4's sliders would
	// otherwise sit over whatever zone you switched to.
	CreateSliderPanel();
	if (AddressPanel != nullptr)
	{
		AddressPanel->Show();
	}
}

void AHapbeatShowcaseZ4StreamConsoleActor::OnZoneDeactivated()
{
	if (LoopTrigger != nullptr)
	{
		LoopTrigger->Stop();
	}
	DestroySliderPanel();
	if (AddressPanel != nullptr)
	{
		AddressPanel->Hide();
	}
}

void AHapbeatShowcaseZ4StreamConsoleActor::BuildEventMap()
{
	EventMap = EventMapOverride != nullptr ? ToRawPtr(EventMapOverride) : BuildFallbackEventMap();
	if (EventMap == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ4, Warning, TEXT("Z4: no EventMap available; the console's haptics will not fire."));
		return;
	}

	// Look the ids up by event name. The fallback map below authors the same
	// categories / names / modes, so both paths go through this one resolution
	// step instead of duplicating the wiring.
	const FGuid LoopId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z4_stream_loop"));
	const FGuid TickId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z4_slider_tick"));

	LoopTrigger->EventMap = EventMap;
	LoopTrigger->EntryId = LoopId;

	TickTrigger->EventMap = EventMap;
	TickTrigger->EntryId = TickId;
}

UHapbeatEventMap* AHapbeatShowcaseZ4StreamConsoleActor::BuildFallbackEventMap()
{
	UHapbeatEventMap* Fallback = NewObject<UHapbeatEventMap>(this);

	LoopClip = FHapbeatSampleLibrary::LoadSampleClip(this,
		TEXT("Showcase/Kit/showcase-kit/stream-clips/z4_stream_loop.wav"));
	TickClip = FHapbeatSampleLibrary::LoadSampleClip(this,
		TEXT("Showcase/Kit/showcase-kit/stream-clips/z4_slider_tick.wav"));

	// Intensities hardcoded from Content/HapbeatSamples/Showcase/Kit/showcase-kit/
	// showcase-kit-manifest.json (schema 2.0.0) stream_events section, matching
	// Samples~/Showcase/EventMaps/ShowcaseEventMap.md verbatim:
	//   z4_stream_loop  gain 1.00 x intensity 0.50 = effective 0.50 (loop)
	//   z4_slider_tick  gain 1.00 x intensity 0.30 = effective 0.30 (one-shot)
	const FHapbeatEventEntry LoopEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z4_stream_loop"),
		/*Gain=*/1.0f, /*bLoop=*/true, /*CachedIntensity=*/0.5f, LoopClip, TEXT("z4_stream_loop"));

	const FHapbeatEventEntry TickEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z4_slider_tick"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.3f, TickClip, TEXT("z4_slider_tick"));

	Fallback->Entries.Reset(2);
	Fallback->Entries.Add(LoopEntry);
	Fallback->Entries.Add(TickEntry);
	return Fallback;
}

void AHapbeatShowcaseZ4StreamConsoleActor::BindInput()
{
	APlayerController* PC = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ4, Warning,
			TEXT("AHapbeatShowcaseZ4StreamConsoleActor: no PlayerController found; input not bound."));
		return;
	}

	EnableInput(PC);
	if (InputComponent == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ4, Warning,
			TEXT("AHapbeatShowcaseZ4StreamConsoleActor: EnableInput did not create an InputComponent; input not bound."));
		return;
	}

	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AHapbeatShowcaseZ4StreamConsoleActor::HandleToggleKey);
}

void AHapbeatShowcaseZ4StreamConsoleActor::CreateSliderPanel()
{
	const UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World != nullptr ? World->GetGameViewport() : nullptr;
	if (Viewport == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ4, Warning,
			TEXT("Z4: no game viewport; the gain / pan sliders were not created."));
		return;
	}

	if (SliderPanel.IsValid())
	{
		return; // already up (re-entering the zone)
	}

	// An OPAQUE panel: SBorder's default brush is a rounded, mostly transparent
	// grey, so white-on-white text over a bright zone was unreadable. WhiteBrush
	// tinted near-black is a flat backing that works over anything.
	const FSlateBrush* PanelBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FLinearColor PanelColor(0.0f, 0.0f, 0.0f, 0.75f);

	// Value_Lambda reads the actor's field every frame so a slider stays right
	// even when something else moves the value; OnValueChanged_Lambda is the only
	// writer. The lambdas capture `this`, and the panel is removed in EndPlay, so
	// they cannot outlive the actor.
	SliderPanel =
		SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 48.0f))
		[
			SNew(SBorder)
			.BorderImage(PanelBrush)
			.BorderBackgroundColor(PanelColor)
			.Padding(FMargin(16.0f, 12.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FSlateColor(FLinearColor::White))
					.Text_Lambda([this]()
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
						.OnValueChanged_Lambda([this](float NewValue) { OnGainSliderChanged(NewValue); })
						// Without this the slider keeps keyboard focus after the
						// drag and Slate navigation eats the space bar.
						.OnMouseCaptureEnd_Lambda([this]() { ReturnFocusToGameViewport(); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FSlateColor(FLinearColor::White))
					.Text_Lambda([this]()
					{
						return FText::FromString(FString::Printf(TEXT("Pan  %+.2f"), PanValue));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SBox).WidthOverride(320.0f)
					[
						// SSlider's value range is 0..1, so pan rides through it as
						// 0..1 and is mapped back to -1..1 on the way out.
						SNew(SSlider)
						.Value_Lambda([this]() { return (PanValue + 1.0f) * 0.5f; })
						.OnValueChanged_Lambda([this](float NewValue)
						{
							OnPanSliderChanged(NewValue * 2.0f - 1.0f);
						})
						.OnMouseCaptureEnd_Lambda([this]() { ReturnFocusToGameViewport(); })
					]
				]
			]
		];

	Viewport->AddViewportWidgetContent(SliderPanel.ToSharedRef(), /*ZOrder=*/1);
}

void AHapbeatShowcaseZ4StreamConsoleActor::DestroySliderPanel()
{
	if (!SliderPanel.IsValid())
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (UGameViewportClient* Viewport = World != nullptr ? World->GetGameViewport() : nullptr)
	{
		Viewport->RemoveViewportWidgetContent(SliderPanel.ToSharedRef());
	}
	SliderPanel.Reset();
}

void AHapbeatShowcaseZ4StreamConsoleActor::ReturnFocusToGameViewport()
{
	const UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World != nullptr ? World->GetGameViewport() : nullptr;
	APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Viewport == nullptr || PC == nullptr)
	{
		return;
	}

	// Still GameAndUI (this zone keeps the cursor free -- WantsCursorUnlocked),
	// but with the VIEWPORT nominated as the focus widget, so key presses go to
	// the game's input stack instead of to the slider that was just released.
	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	if (const TSharedPtr<SViewport> ViewportWidget = Viewport->GetGameViewportWidget())
	{
		Mode.SetWidgetToFocus(ViewportWidget);
	}
	PC->SetInputMode(Mode);
}

void AHapbeatShowcaseZ4StreamConsoleActor::HandleToggleKey()
{
	if (LoopTrigger == nullptr)
	{
		return;
	}
	UHapbeatStreamPlayback* Pb = LoopTrigger->GetActivePlayback();
	const bool bPlaying = Pb != nullptr && Pb->IsActive();
	if (bPlaying)
	{
		LoopTrigger->Stop();
	}
	else
	{
		LoopTrigger->Fire();
	}
}

void AHapbeatShowcaseZ4StreamConsoleActor::DebugToggleStream()
{
	// Deliberately the key handler itself, not a copy of it: a capture that went
	// around it could pass while the space bar was broken.
	HandleToggleKey();
}

void AHapbeatShowcaseZ4StreamConsoleActor::OnGainSliderChanged(float NewValue)
{
	const float OldValue = GainValue;
	GainValue = FMath::Clamp(NewValue, 0.0f, 1.0f);
	if (GainBinding != nullptr)
	{
		GainBinding->SetValue(GainValue);
	}
	EmitDetentTicks(OldValue, GainValue);
}

void AHapbeatShowcaseZ4StreamConsoleActor::OnPanSliderChanged(float NewValue)
{
	const float OldValue = PanValue;
	PanValue = FMath::Clamp(NewValue, -1.0f, 1.0f);
	if (PanBinding != nullptr)
	{
		PanBinding->SetValue(PanValue);
	}
	EmitDetentTicks(OldValue, PanValue);
}

void AHapbeatShowcaseZ4StreamConsoleActor::EmitDetentTicks(float OldValue, float NewValue)
{
	if (TickThreshold <= 0.0f)
	{
		return;
	}
	// AbsolutePosition snap (Unity HapbeatTickEmitter): detents sit at fixed
	// multiples of the threshold, so the number of ticks is the difference
	// between the two quantised band indices -- a slow drag emits one per band
	// crossed, and a jump emits the bands it skipped, never one per pixel.
	const int32 OldBand = FMath::FloorToInt(OldValue / TickThreshold);
	const int32 NewBand = FMath::FloorToInt(NewValue / TickThreshold);
	// Same 64 cap Unity's emitter uses, so a click at the far end of the track
	// cannot spray events at the device.
	const int32 TicksToFire = FMath::Min(FMath::Abs(NewBand - OldBand), 64);
	for (int32 Index = 0; Index < TicksToFire; ++Index)
	{
		FireTick();
	}
}

void AHapbeatShowcaseZ4StreamConsoleActor::FireTick()
{
	// The SFX is under no such constraint, so the detent still clicks audibly
	// even when the haptic tick below has to stand down.
	if (TickSound != nullptr)
	{
		UGameplayStatics::PlaySound2D(this, TickSound);
	}

	// v1 single-active-stream REPLACE model: firing the tick StreamClip while
	// the loop is streaming would permanently steal (kill) the loop session.
	// Unity's runtime mixes the two; v1 has no mixing, so prefer keeping the
	// loop alive and skip the detent tick during streaming (documented
	// limitation — the gain/pan change itself is still audible in the loop).
	if (LoopTrigger != nullptr)
	{
		if (UHapbeatStreamPlayback* LoopPb = LoopTrigger->GetActivePlayback())
		{
			if (LoopPb->IsActive())
			{
				return;
			}
		}
	}
	if (TickTrigger != nullptr)
	{
		TickTrigger->Fire();
	}
}

void AHapbeatShowcaseZ4StreamConsoleActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The Showcase switcher draws a shared Slate HUD covering the key guide, the
	// zone's own state and the device footer, so a zone under it prints none of
	// this. ONE EARLY RETURN, not a guard around each line: the Gain / Pan line
	// below used to sit outside the per-line guard and showed on top of the
	// shared HUD. Everything below here is HUD-only.
	if (IsOwnedByShowcaseSwitcher(this))
	{
		return;
	}

	HudRefreshTimer -= DeltaSeconds;
	if (HudRefreshTimer > 0.0f)
	{
		return;
	}
	HudRefreshTimer = HudRefreshIntervalSeconds;

	FHapbeatSampleLibrary::ShowHudLine(KeyGuideHudLineKey,
		TEXT("Z4 Stream Console -- Space: toggle loop | drag the on-screen Gain / Pan sliders"),
		FColor::Cyan, HudRefreshIntervalSeconds * 2.0f);

	bool bStreaming = false;
	if (LoopTrigger != nullptr)
	{
		UHapbeatStreamPlayback* Pb = LoopTrigger->GetActivePlayback();
		bStreaming = Pb != nullptr && Pb->IsActive();
	}
	FHapbeatSampleLibrary::ShowHudLine(StatusHudLineKey,
		FString::Printf(TEXT("Gain=%.2f Pan=%.2f Streaming=%s"),
			GainValue, PanValue, bStreaming ? TEXT("Yes") : TEXT("No")),
		bStreaming ? FColor::Green : FColor::Silver, HudRefreshIntervalSeconds * 2.0f);
	FHapbeatSampleLibrary::ShowDeviceStatusLine(this, StatusHudLineKey + 1, HudRefreshIntervalSeconds * 2.0f);
}

FText AHapbeatShowcaseZ4StreamConsoleActor::GetZoneLabel() const
{
	return FText::FromString(TEXT("Stream Console"));
}

TArray<FHapbeatShowcaseHudCommand> AHapbeatShowcaseZ4StreamConsoleActor::GetHudCommands() const
{
	TArray<FHapbeatShowcaseHudCommand> Commands;
	Commands.Add({ FText::FromString(TEXT("Space")), FText::FromString(TEXT("toggle the looping stream")) });
	Commands.Add({ FText::FromString(TEXT("Mouse")),
		FText::FromString(TEXT("drag the Gain / Pan sliders (one tick per detent)")) });
	Commands.Add({ FText::FromString(TEXT("Top panel")),
		FText::FromString(TEXT("set Player / Group, then Apply, to bind this build to one device")) });
	return Commands;
}

FTransform AHapbeatShowcaseZ4StreamConsoleActor::GetPlayerSpawnRelative() const
{
	// Unity Z4_Stream/PlayerSpawn is at the zone origin, and there is nothing in
	// the world to stand back from -- the whole zone is the on-screen panel.
	return FTransform::Identity;
}

bool AHapbeatShowcaseZ4StreamConsoleActor::WantsCursorUnlocked() const
{
	// The one UI zone (Unity ZoneEntry.unlockCursorOnEnter). Its interaction is
	// on-screen, not in the world, so the mouse belongs to the UI here. The
	// character switches to Game-and-UI input, so this zone's keys keep working
	// while the cursor is free.
	return true;
}
