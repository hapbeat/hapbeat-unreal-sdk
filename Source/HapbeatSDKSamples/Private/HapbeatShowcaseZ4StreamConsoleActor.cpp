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
#include "InputCoreTypes.h" // EKeys::*

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatShowcaseZ4, Log, All);

AHapbeatShowcaseZ4StreamConsoleActor::AHapbeatShowcaseZ4StreamConsoleActor()
{
	PrimaryActorTick.bCanEverTick = true;

	ConsoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ConsoleMesh"));
	RootComponent = ConsoleMesh;
	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		ConsoleMesh->SetStaticMesh(CubeMesh);
	}
	// A squat pedestal rather than a plain cube -- purely cosmetic, no gameplay meaning.
	ConsoleMesh->SetRelativeScale3D(FVector(1.0f, 0.6f, 1.2f));
	ConsoleMesh->SetMobility(EComponentMobility::Static);

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
}

void AHapbeatShowcaseZ4StreamConsoleActor::BeginPlay()
{
	Super::BeginPlay();

	if (RootComponent != nullptr)
	{
		RootComponent->SetRelativeLocation(FootprintOffset);
	}

	BuildEventMap();
	BindInput();

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
	Super::EndPlay(EndPlayReason);
}

void AHapbeatShowcaseZ4StreamConsoleActor::BuildEventMap()
{
	EventMap = NewObject<UHapbeatEventMap>(this);

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

	EventMap->Entries.Reset(2);
	EventMap->Entries.Add(LoopEntry);
	EventMap->Entries.Add(TickEntry);

	LoopTrigger->EventMap = EventMap;
	LoopTrigger->EntryId = LoopEntry.Id;

	TickTrigger->EventMap = EventMap;
	TickTrigger->EntryId = TickEntry.Id;
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

	InputComponent->BindKey(EKeys::T, IE_Pressed, this, &AHapbeatShowcaseZ4StreamConsoleActor::HandleToggleKey);
	InputComponent->BindKey(EKeys::U, IE_Pressed, this, &AHapbeatShowcaseZ4StreamConsoleActor::HandleGainUpKey);
	InputComponent->BindKey(EKeys::J, IE_Pressed, this, &AHapbeatShowcaseZ4StreamConsoleActor::HandleGainDownKey);
	InputComponent->BindKey(EKeys::N, IE_Pressed, this, &AHapbeatShowcaseZ4StreamConsoleActor::HandlePanLeftKey);
	InputComponent->BindKey(EKeys::M, IE_Pressed, this, &AHapbeatShowcaseZ4StreamConsoleActor::HandlePanRightKey);
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

void AHapbeatShowcaseZ4StreamConsoleActor::HandleGainUpKey()
{
	StepGain(GainStep);
}

void AHapbeatShowcaseZ4StreamConsoleActor::HandleGainDownKey()
{
	StepGain(-GainStep);
}

void AHapbeatShowcaseZ4StreamConsoleActor::HandlePanLeftKey()
{
	StepPan(-PanStep);
}

void AHapbeatShowcaseZ4StreamConsoleActor::HandlePanRightKey()
{
	StepPan(PanStep);
}

void AHapbeatShowcaseZ4StreamConsoleActor::StepGain(float Delta)
{
	GainValue = FMath::Clamp(GainValue + Delta, 0.0f, 1.0f);
	if (GainBinding != nullptr)
	{
		GainBinding->SetValue(GainValue);
	}
	FireTick();
}

void AHapbeatShowcaseZ4StreamConsoleActor::StepPan(float Delta)
{
	PanValue = FMath::Clamp(PanValue + Delta, -1.0f, 1.0f);
	if (PanBinding != nullptr)
	{
		PanBinding->SetValue(PanValue);
	}
	FireTick();
}

void AHapbeatShowcaseZ4StreamConsoleActor::FireTick()
{
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

	FHapbeatSampleLibrary::ShowHudLine(KeyGuideHudLineKey,
		TEXT("Z4 Stream Console -- T: toggle loop | U/J: gain +/- | N/M: pan +/-"),
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
