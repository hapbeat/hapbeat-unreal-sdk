// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZ2DoorActor.h"

#include "HapbeatClip.h"
#include "HapbeatEventMap.h"
#include "HapbeatSampleLibrary.h"
#include "HapbeatTriggerComponent.h"

#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h" // EKeys::*
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatShowcaseZ2, Log, All);

namespace
{
	// Door dimensions, centimeters (UE's world unit). Purely cosmetic scene
	// layout -- no protocol/gain meaning. Engine basic shapes are 100 cm across
	// their default axis (Cube 100^3).
	constexpr float DoorWidth = 110.0f;
	constexpr float DoorHeight = 220.0f;
	constexpr float DoorThickness = 5.0f;
}

// =============================================================================
// AHapbeatShowcaseZ2DoorActor
// =============================================================================

AHapbeatShowcaseZ2DoorActor::AHapbeatShowcaseZ2DoorActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// Root IS the hinge pivot: rotating it (SetDoorYaw) swings DoorMesh, which
	// is offset by half its width so the hinge sits at one vertical edge, not
	// the door's center. Unlike Z4/Z5 (single mesh = root), this zone needs a
	// plain scene root distinct from its one mesh for exactly this reason.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(RootComponent);
	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		DoorMesh->SetStaticMesh(CubeMesh);
	}
	DoorMesh->SetRelativeLocation(FVector(DoorWidth * 0.5f, 0.0f, DoorHeight * 0.5f));
	DoorMesh->SetRelativeScale3D(FVector(DoorWidth / 100.0f, DoorThickness / 100.0f, DoorHeight / 100.0f));
	DoorMesh->SetCollisionProfileName(TEXT("BlockAll"));
	DoorMesh->SetMobility(EComponentMobility::Movable); // must stay Movable: it is rotated at runtime

	OpenTrigger = CreateDefaultSubobject<UHapbeatTriggerComponent>(TEXT("OpenTrigger"));
	CloseTrigger = CreateDefaultSubobject<UHapbeatTriggerComponent>(TEXT("CloseTrigger"));
	SlamTrigger = CreateDefaultSubobject<UHapbeatTriggerComponent>(TEXT("SlamTrigger"));
	LockTrigger = CreateDefaultSubobject<UHapbeatTriggerComponent>(TEXT("LockTrigger"));
	UnlockTrigger = CreateDefaultSubobject<UHapbeatTriggerComponent>(TEXT("UnlockTrigger"));
	RattleTrigger = CreateDefaultSubobject<UHapbeatTriggerComponent>(TEXT("RattleTrigger"));

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

void AHapbeatShowcaseZ2DoorActor::BeginPlay()
{
	Super::BeginPlay();

	if (RootComponent != nullptr)
	{
		RootComponent->SetRelativeLocation(FootprintOffset);
	}

	BuildEventMap();
	BindInput();

	SetDoorYaw(0.0f); // Closed
}

void AHapbeatShowcaseZ2DoorActor::BuildEventMap()
{
	EventMap = EventMapOverride != nullptr ? ToRawPtr(EventMapOverride) : BuildFallbackEventMap();
	if (EventMap == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ2, Warning, TEXT("Z2: no EventMap available; the door's haptics will not fire."));
		return;
	}

	// Look the ids up by event name. The fallback map below authors the same
	// categories / names / modes, so both paths go through this one resolution
	// step instead of duplicating the wiring.
	const FGuid OpenId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z2_door_open"));
	const FGuid CloseId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z2_door_close"));
	const FGuid SlamId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::Command, TEXT("showcase-kit"), TEXT("z2_door_slam"));
	const FGuid LockId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::Command, TEXT("showcase-kit"), TEXT("z2_door_lock"));
	const FGuid UnlockId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::Command, TEXT("showcase-kit"), TEXT("z2_door_unlock"));
	const FGuid RattleId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z2_door_rattle"));

	OpenTrigger->EventMap = EventMap;
	OpenTrigger->EntryId = OpenId;
	CloseTrigger->EventMap = EventMap;
	CloseTrigger->EntryId = CloseId;
	SlamTrigger->EventMap = EventMap;
	SlamTrigger->EntryId = SlamId;
	LockTrigger->EventMap = EventMap;
	LockTrigger->EntryId = LockId;
	UnlockTrigger->EventMap = EventMap;
	UnlockTrigger->EntryId = UnlockId;
	RattleTrigger->EventMap = EventMap;
	RattleTrigger->EntryId = RattleId;
}

UHapbeatEventMap* AHapbeatShowcaseZ2DoorActor::BuildFallbackEventMap()
{
	UHapbeatEventMap* Fallback = NewObject<UHapbeatEventMap>(this);
	Fallback->Entries.Reset(6);

	OpenClip = FHapbeatSampleLibrary::LoadSampleClip(this, TEXT("Showcase/Kit/showcase-kit/stream-clips/z2_door_open.wav"));
	CloseClip = FHapbeatSampleLibrary::LoadSampleClip(this, TEXT("Showcase/Kit/showcase-kit/stream-clips/z2_door_close.wav"));
	RattleClip = FHapbeatSampleLibrary::LoadSampleClip(this, TEXT("Showcase/Kit/showcase-kit/stream-clips/z2_door_rattle.wav"));

	// Modes + intensities hardcoded from Samples~/Showcase/EventMaps/ShowcaseEventMap.md
	// (which mirrors Content/HapbeatSamples/Showcase/Kit/showcase-kit/showcase-kit-manifest.json,
	// schema 2.0.0) verbatim, all gain 1.00:
	//   z2_door_open    StreamClip  intensity 0.30 (one-shot, tracks the Opening tween)
	//   z2_door_close   StreamClip  intensity 0.20 (one-shot, tracks the Closing tween)
	//   z2_door_slam    Command     intensity 0.25 (one-shot)
	//   z2_door_lock    Command     intensity 0.30 (one-shot)
	//   z2_door_unlock  Command     intensity 0.30 (one-shot)
	//   z2_door_rattle  StreamClip  intensity 0.25 (one-shot, tracks the rattle shake)
	const FHapbeatEventEntry OpenEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z2_door_open"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.30f, OpenClip, TEXT("z2_door_open"));
	const FHapbeatEventEntry CloseEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z2_door_close"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.20f, CloseClip, TEXT("z2_door_close"));
	const FHapbeatEventEntry SlamEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::Command, TEXT("showcase-kit"), TEXT("z2_door_slam"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.25f, /*Clip=*/nullptr, TEXT("z2_door_slam"));
	const FHapbeatEventEntry LockEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::Command, TEXT("showcase-kit"), TEXT("z2_door_lock"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.30f, /*Clip=*/nullptr, TEXT("z2_door_lock"));
	const FHapbeatEventEntry UnlockEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::Command, TEXT("showcase-kit"), TEXT("z2_door_unlock"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.30f, /*Clip=*/nullptr, TEXT("z2_door_unlock"));
	const FHapbeatEventEntry RattleEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z2_door_rattle"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.25f, RattleClip, TEXT("z2_door_rattle"));

	Fallback->Entries.Add(OpenEntry);
	Fallback->Entries.Add(CloseEntry);
	Fallback->Entries.Add(SlamEntry);
	Fallback->Entries.Add(LockEntry);
	Fallback->Entries.Add(UnlockEntry);
	Fallback->Entries.Add(RattleEntry);
	return Fallback;
}

void AHapbeatShowcaseZ2DoorActor::BindInput()
{
	APlayerController* PC = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ2, Warning,
			TEXT("AHapbeatShowcaseZ2DoorActor: no PlayerController found; input not bound."));
		return;
	}

	EnableInput(PC);
	if (InputComponent == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ2, Warning,
			TEXT("AHapbeatShowcaseZ2DoorActor: EnableInput did not create an InputComponent; input not bound."));
		return;
	}

	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AHapbeatShowcaseZ2DoorActor::HandleToggleKey);
	InputComponent->BindKey(EKeys::G, IE_Pressed, this, &AHapbeatShowcaseZ2DoorActor::HandleActionKey);
	InputComponent->BindKey(EKeys::L, IE_Pressed, this, &AHapbeatShowcaseZ2DoorActor::HandleLockKey);
}

void AHapbeatShowcaseZ2DoorActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Shared sample convention: stop any in-flight StreamClip (open/close/rattle
	// are stream-mode) so nothing keeps buzzing past the actor's lifetime.
	for (UHapbeatTriggerComponent* Trigger : { OpenTrigger.Get(), CloseTrigger.Get(), RattleTrigger.Get() })
	{
		if (Trigger != nullptr)
		{
			Trigger->Stop();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AHapbeatShowcaseZ2DoorActor::HandleToggleKey()
{
	switch (State)
	{
	case EHapbeatZ2DoorState::Closed:
		State = EHapbeatZ2DoorState::Opening;
		StateElapsedSeconds = 0.0f;
		if (OpenTrigger != nullptr)
		{
			OpenTrigger->Fire();
		}
		break;

	case EHapbeatZ2DoorState::Open:
		State = EHapbeatZ2DoorState::Closing;
		StateElapsedSeconds = 0.0f;
		if (CloseTrigger != nullptr)
		{
			CloseTrigger->Fire();
		}
		break;

	case EHapbeatZ2DoorState::Locked:
		// Unity parity (DoorController.cs: Closed -> LockedRattle on DoorAction
		// with IsLocked): trying to open a LOCKED door rattles it. G also
		// rattles when locked (kept as the "aggressive action" alias).
		bRattling = true;
		RattleElapsedSeconds = 0.0f;
		if (RattleTrigger != nullptr)
		{
			RattleTrigger->Fire();
		}
		break;

	default:
		// Mid-tween (Opening/Closing): F is a no-op; keys are rejected until settled.
		break;
	}
}

void AHapbeatShowcaseZ2DoorActor::HandleActionKey()
{
	switch (State)
	{
	case EHapbeatZ2DoorState::Open:
		// Slam: abrupt by definition -- snap straight to Closed, skipping the
		// graceful Closing tween entirely (no StateElapsedSeconds ramp).
		State = EHapbeatZ2DoorState::Closed;
		StateElapsedSeconds = 0.0f;
		SetDoorYaw(0.0f);
		if (SlamTrigger != nullptr)
		{
			SlamTrigger->Fire();
		}
		break;

	case EHapbeatZ2DoorState::Locked:
		bRattling = true;
		RattleElapsedSeconds = 0.0f;
		if (RattleTrigger != nullptr)
		{
			RattleTrigger->Fire();
		}
		break;

	default:
		// Closed(unlocked) has nothing to slam or rattle; Opening/Closing ignore input.
		break;
	}
}

void AHapbeatShowcaseZ2DoorActor::HandleLockKey()
{
	switch (State)
	{
	case EHapbeatZ2DoorState::Closed:
		State = EHapbeatZ2DoorState::Locked;
		if (LockTrigger != nullptr)
		{
			LockTrigger->Fire();
		}
		break;

	case EHapbeatZ2DoorState::Locked:
		State = EHapbeatZ2DoorState::Closed;
		bRattling = false;
		SetDoorYaw(0.0f); // in case L lands mid-rattle-shake
		if (UnlockTrigger != nullptr)
		{
			UnlockTrigger->Fire();
		}
		break;

	default:
		// Mirrors Unity DoorController: LockToggle only ever transitions out of Closed.
		break;
	}
}

void AHapbeatShowcaseZ2DoorActor::SetDoorYaw(float Degrees)
{
	if (RootComponent != nullptr)
	{
		RootComponent->SetRelativeRotation(FRotator(0.0f, Degrees, 0.0f));
	}
}

void AHapbeatShowcaseZ2DoorActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	switch (State)
	{
	case EHapbeatZ2DoorState::Opening:
	{
		StateElapsedSeconds += DeltaSeconds;
		const float Alpha = FMath::Clamp(StateElapsedSeconds / OpenDurationSeconds, 0.0f, 1.0f);
		SetDoorYaw(FMath::Lerp(0.0f, OpenYawDegrees, Alpha));
		if (Alpha >= 1.0f)
		{
			State = EHapbeatZ2DoorState::Open;
		}
		break;
	}

	case EHapbeatZ2DoorState::Closing:
	{
		StateElapsedSeconds += DeltaSeconds;
		const float Alpha = FMath::Clamp(StateElapsedSeconds / CloseDurationSeconds, 0.0f, 1.0f);
		SetDoorYaw(FMath::Lerp(OpenYawDegrees, 0.0f, Alpha));
		if (Alpha >= 1.0f)
		{
			State = EHapbeatZ2DoorState::Closed;
		}
		break;
	}

	case EHapbeatZ2DoorState::Locked:
	{
		if (bRattling)
		{
			RattleElapsedSeconds += DeltaSeconds;
			if (RattleElapsedSeconds >= RattleDurationSeconds)
			{
				bRattling = false;
				SetDoorYaw(0.0f);
			}
			else
			{
				// Damped sine shake, settling back to 0 by RattleDurationSeconds.
				const float Damping = 1.0f - (RattleElapsedSeconds / RattleDurationSeconds);
				const float Shake = FMath::Sin(RattleElapsedSeconds * 2.0f * PI * RattleFrequencyHz)
					* RattleAmplitudeDegrees * Damping;
				SetDoorYaw(Shake);
			}
		}
		break;
	}

	default:
		break; // Closed, Open -- static, nothing to tween.
	}

	HudRefreshTimer -= DeltaSeconds;
	if (HudRefreshTimer > 0.0f)
	{
		return;
	}
	HudRefreshTimer = HudRefreshIntervalSeconds;

	FHapbeatSampleLibrary::ShowHudLine(KeyGuideHudLineKey,
		TEXT("Z2 Door -- F: open/close (rattles when locked)  G: slam (open)  L: lock/unlock"),
		FColor::Cyan, HudRefreshIntervalSeconds * 2.0f);

	FString StateName;
	switch (State)
	{
	case EHapbeatZ2DoorState::Closed: StateName = TEXT("Closed"); break;
	case EHapbeatZ2DoorState::Opening: StateName = TEXT("Opening"); break;
	case EHapbeatZ2DoorState::Open: StateName = TEXT("Open"); break;
	case EHapbeatZ2DoorState::Closing: StateName = TEXT("Closing"); break;
	case EHapbeatZ2DoorState::Locked: StateName = bRattling ? TEXT("Locked (rattling)") : TEXT("Locked"); break;
	}
	FHapbeatSampleLibrary::ShowHudLine(StatusHudLineKey,
		FString::Printf(TEXT("Z2 state: %s"), *StateName),
		FColor::Silver, HudRefreshIntervalSeconds * 2.0f);
	FHapbeatSampleLibrary::ShowDeviceStatusLine(this, StatusHudLineKey + 1, HudRefreshIntervalSeconds * 2.0f);
}
