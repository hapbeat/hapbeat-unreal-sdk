// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatBasicExampleActor.h"

#include "HapbeatClip.h"
#include "HapbeatEventMap.h"
#include "HapbeatSampleLibrary.h"
#include "HapbeatSubsystem.h"
#include "HapbeatTriggerComponent.h"

#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h" // EKeys::*

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatBasicExample, Log, All);

AHapbeatBasicExampleActor::AHapbeatBasicExampleActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// Plain scene root so the actor has a placeable/transformable icon in the
	// editor viewport; none of the 3 trigger components need a transform of
	// their own (UActorComponent, not USceneComponent).
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	StreamOneShotTrigger = CreateDefaultSubobject<UHapbeatTriggerComponent>(TEXT("StreamOneShotTrigger"));
	StreamLoopTrigger = CreateDefaultSubobject<UHapbeatTriggerComponent>(TEXT("StreamLoopTrigger"));
	CommandTrigger = CreateDefaultSubobject<UHapbeatTriggerComponent>(TEXT("CommandTrigger"));
}

void AHapbeatBasicExampleActor::BeginPlay()
{
	Super::BeginPlay();

	BuildEventMap();
	BindInput();
}

void AHapbeatBasicExampleActor::BuildEventMap()
{
	EventMap = NewObject<UHapbeatEventMap>(this);

	// Shared by the one-shot and loop entries -- same WAV, matching Unity's
	// BasicExampleEventMap.asset (both entries reference the same streamClip guid).
	SharedStreamClip = FHapbeatSampleLibrary::LoadSampleClip(this,
		TEXT("BasicExample/Kit/basic-exam-kit/stream-clips/sine_100hz_1s.wav"));

	// Manifest intensity is authored in Content/HapbeatSamples/BasicExample/Kit/
	// basic-exam-kit/basic-exam-kit-manifest.json (schema 2.0.0): both the
	// "basic-exam-kit.sine_100hz_1s" stream_events entry and the
	// "basic-exam-kit.sine_200hz_1s" events entry set parameters.intensity =
	// 0.5. Samples are self-contained (no runtime JSON parsing per the design
	// doc), so the value is hardcoded here -- keep this comment's pointer in
	// sync if the shipped manifest is ever revised.
	constexpr float ManifestIntensity = 0.5f;

	const FHapbeatEventEntry StreamOneShotEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("basic-exam-kit"), TEXT("sine_100hz_1s"),
		/*Gain=*/1.0f, /*bLoop=*/false, ManifestIntensity, SharedStreamClip, TEXT("demo_stream_sine_100hz"));

	const FHapbeatEventEntry StreamLoopEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("basic-exam-kit"), TEXT("sine_100hz_1s_loop"),
		/*Gain=*/1.0f, /*bLoop=*/true, ManifestIntensity, SharedStreamClip, TEXT("demo_stream_loop_100hz"));

	const FHapbeatEventEntry CommandEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::Command, TEXT("basic-exam-kit"), TEXT("sine_200hz_1s"),
		/*Gain=*/1.0f, /*bLoop=*/false, ManifestIntensity, /*Clip=*/nullptr, TEXT("demo_command_sine_200hz"));

	EventMap->Entries.Reset(3);
	EventMap->Entries.Add(StreamOneShotEntry);
	EventMap->Entries.Add(StreamLoopEntry);
	EventMap->Entries.Add(CommandEntry);

	StreamOneShotTrigger->EventMap = EventMap;
	StreamOneShotTrigger->EntryId = StreamOneShotEntry.Id;

	StreamLoopTrigger->EventMap = EventMap;
	StreamLoopTrigger->EntryId = StreamLoopEntry.Id;

	CommandTrigger->EventMap = EventMap;
	CommandTrigger->EntryId = CommandEntry.Id;
}

void AHapbeatBasicExampleActor::BindInput()
{
	APlayerController* PC = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)
	{
		UE_LOG(LogHapbeatBasicExample, Warning,
			TEXT("AHapbeatBasicExampleActor: no PlayerController found; input not bound. Make sure the level has a PlayerController (the default GameMode spawns one for the local player)."));
		return;
	}

	EnableInput(PC);
	if (InputComponent == nullptr)
	{
		UE_LOG(LogHapbeatBasicExample, Warning,
			TEXT("AHapbeatBasicExampleActor: EnableInput did not create an InputComponent; input not bound."));
		return;
	}

	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AHapbeatBasicExampleActor::HandleSpaceKey);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AHapbeatBasicExampleActor::HandleRKey);
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AHapbeatBasicExampleActor::HandleFKey);
	InputComponent->BindKey(EKeys::S, IE_Pressed, this, &AHapbeatBasicExampleActor::HandleSKey);
	InputComponent->BindKey(EKeys::C, IE_Pressed, this, &AHapbeatBasicExampleActor::HandleCKey);
}

void AHapbeatBasicExampleActor::HandleSpaceKey()
{
	if (StreamOneShotTrigger != nullptr)
	{
		StreamOneShotTrigger->Fire();
	}
}

void AHapbeatBasicExampleActor::HandleRKey()
{
	if (StreamLoopTrigger != nullptr)
	{
		StreamLoopTrigger->Fire();
	}
}

void AHapbeatBasicExampleActor::HandleFKey()
{
	if (CommandTrigger != nullptr)
	{
		CommandTrigger->Fire();
	}
}

void AHapbeatBasicExampleActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Shared sample convention: never leave a looping stream running past the
	// actor's lifetime (the R-key loop would otherwise keep buzzing after
	// EndPIE / level change until the subsystem itself tears down).
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UHapbeatSubsystem* Subsystem = GameInstance->GetSubsystem<UHapbeatSubsystem>())
		{
			Subsystem->StopStream();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AHapbeatBasicExampleActor::HandleSKey()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UHapbeatSubsystem* Subsystem = GameInstance->GetSubsystem<UHapbeatSubsystem>())
		{
			// Stop the raw stream session AND any Command-mode event, matching the
			// Unity BasicExample's S key (StopStream + StopAll).
			Subsystem->StopStream();
			Subsystem->StopAll();
		}
	}
}

void AHapbeatBasicExampleActor::HandleCKey()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UHapbeatSubsystem* Subsystem = GameInstance->GetSubsystem<UHapbeatSubsystem>())
		{
			Subsystem->Ping();
		}
	}
}

void AHapbeatBasicExampleActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	HudRefreshTimer -= DeltaSeconds;
	if (HudRefreshTimer > 0.0f)
	{
		return;
	}
	HudRefreshTimer = HudRefreshIntervalSeconds;

	FHapbeatSampleLibrary::ShowHudLine(KeyGuideHudLineKey,
		TEXT("Hapbeat BasicExample -- Space: stream 1-shot | R: stream loop | F: command play | S: stop all | C: ping"),
		FColor::Cyan, HudRefreshIntervalSeconds * 2.0f);

	int32 AliveCount = 0;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UHapbeatSubsystem* Subsystem = GameInstance->GetSubsystem<UHapbeatSubsystem>())
		{
			AliveCount = Subsystem->GetAliveDeviceCount();
		}
	}
	FHapbeatSampleLibrary::ShowHudLine(StatusHudLineKey,
		FString::Printf(TEXT("Hapbeat devices reachable: %d"), AliveCount),
		AliveCount > 0 ? FColor::Green : FColor::Silver, HudRefreshIntervalSeconds * 2.0f);
}
