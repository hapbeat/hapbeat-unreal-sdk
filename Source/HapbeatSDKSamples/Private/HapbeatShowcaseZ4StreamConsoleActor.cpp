// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZ4StreamConsoleActor.h"

#include "HapbeatClip.h"
#include "HapbeatEventMap.h"
#include "HapbeatParameterBinding.h"
#include "HapbeatSampleLibrary.h"
#include "HapbeatStreamPlayback.h"
#include "HapbeatSubsystem.h"
#include "HapbeatTriggerComponent.h"

#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "InputCoreTypes.h" // EKeys::*
#include "Kismet/GameplayStatics.h" // PlaySound2D
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatShowcaseZ4, Log, All);

AHapbeatShowcaseZ4StreamConsoleActor::AHapbeatShowcaseZ4StreamConsoleActor()
{
	PrimaryActorTick.bCanEverTick = true;

	ConsoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ConsoleMesh"));
	RootComponent = ConsoleMesh;
	// Movable: the zone is spawned at runtime (Hapbeat Showcase zone switching) and this root
	// is moved by FootprintOffset in BeginPlay. Lighting is dynamic.
	// Mobility is set before the mesh assignment so SetStaticMesh never runs on a Static component.
	ConsoleMesh->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		ConsoleMesh->SetStaticMesh(CubeMesh);
	}
	// A squat pedestal rather than a plain cube -- purely cosmetic, no gameplay meaning.
	ConsoleMesh->SetRelativeScale3D(FVector(1.0f, 0.6f, 1.2f));

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

	if (RootComponent != nullptr)
	{
		RootComponent->SetRelativeLocation(FootprintOffset);
	}

	if (UMaterialInterface* ConsoleMaterial =
		FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("MI_Floor")))
	{
		if (ConsoleMesh != nullptr)
		{
			ConsoleMesh->SetMaterial(0, ConsoleMaterial);
		}
	}
	TickSound = FHapbeatSampleLibrary::LoadShowcaseAsset<USoundBase>(TEXT("Sounds"), TEXT("S_z4_ui_tick"));

	BuildEventMap();
	BindInput();
	CreateSliderPanel();

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
	if (LoopTrigger != nullptr)
	{
		LoopTrigger->Stop();
	}
	DestroySliderPanel();
	Super::EndPlay(EndPlayReason);
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
						.OnValueChanged_Lambda([this](float NewValue) { OnGainSliderChanged(NewValue); })
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
						// SSlider's value range is 0..1, so pan rides through it as
						// 0..1 and is mapped back to -1..1 on the way out.
						SNew(SSlider)
						.Value_Lambda([this]() { return (PanValue + 1.0f) * 0.5f; })
						.OnValueChanged_Lambda([this](float NewValue)
						{
							OnPanSliderChanged(NewValue * 2.0f - 1.0f);
						})
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

	HudRefreshTimer -= DeltaSeconds;
	if (HudRefreshTimer > 0.0f)
	{
		return;
	}
	HudRefreshTimer = HudRefreshIntervalSeconds;

	// The Showcase switcher draws a shared Slate key guide covering this, so
	// only print the line when this zone is running on its own.
	if (!IsOwnedByShowcaseSwitcher(this))
	{
		FHapbeatSampleLibrary::ShowHudLine(KeyGuideHudLineKey,
			TEXT("Z4 Stream Console -- Space: toggle loop | drag the on-screen Gain / Pan sliders"),
			FColor::Cyan, HudRefreshIntervalSeconds * 2.0f);
	}

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
	// Same reason: the shared HUD has a device / ping footer.
	if (!IsOwnedByShowcaseSwitcher(this))
	{
		FHapbeatSampleLibrary::ShowDeviceStatusLine(this, StatusHudLineKey + 1, HudRefreshIntervalSeconds * 2.0f);
	}
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
	return Commands;
}

FTransform AHapbeatShowcaseZ4StreamConsoleActor::GetPlayerSpawnRelative() const
{
	// Unity puts this spawn at the zone origin because its console stands
	// elsewhere in the zone; the UE console IS at the zone origin, so back off
	// far enough to see it.
	return FTransform(FRotator::ZeroRotator, FVector(-250.0f, 0.0f, 0.0f));
}

bool AHapbeatShowcaseZ4StreamConsoleActor::WantsCursorUnlocked() const
{
	// The one UI zone (Unity ZoneEntry.unlockCursorOnEnter). Its interaction is
	// on-screen, not in the world, so the mouse belongs to the UI here. The
	// character switches to Game-and-UI input, so this zone's keys keep working
	// while the cursor is free.
	return true;
}
