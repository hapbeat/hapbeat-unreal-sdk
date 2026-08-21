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
#include "Kismet/GameplayStatics.h" // PlaySoundAtLocation
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
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
	DoorMesh->SetMobility(EComponentMobility::Movable); // must stay Movable: it is rotated at runtime
	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		DoorMesh->SetStaticMesh(CubeMesh);
	}
	DoorMesh->SetRelativeLocation(FVector(DoorWidth * 0.5f, 0.0f, DoorHeight * 0.5f));
	DoorMesh->SetRelativeScale3D(FVector(DoorWidth / 100.0f, DoorThickness / 100.0f, DoorHeight / 100.0f));
	DoorMesh->SetCollisionProfileName(TEXT("BlockAll"));

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

	ApplyShowcaseAssets();
	BuildEventMap();
	BindInput();

	SetDoorYaw(0.0f); // Closed
}

void AHapbeatShowcaseZ2DoorActor::ApplyShowcaseAssets()
{
	if (UStaticMesh* ImportedDoor =
		FHapbeatSampleLibrary::LoadShowcaseAsset<UStaticMesh>(TEXT("Meshes"), TEXT("SM_Door")))
	{
		if (DoorMesh != nullptr)
		{
			// SM_Door is one combined mesh (frame + leaf) whose pivot is wherever
			// the source model put it, so the hinge line is recovered from bounds:
			// park the mesh's -X edge on the hinge (Root, the component that is
			// rotated), centre it on Y, and sit its bottom on the floor. Scale
			// stays 1 -- the import is already ~175 x 33 x 314 cm, a real door.
			const FBoxSphereBounds Bounds = ImportedDoor->GetBounds();
			DoorMesh->SetStaticMesh(ImportedDoor);
			DoorMesh->SetRelativeScale3D(FVector::OneVector);
			DoorMesh->SetRelativeLocation(FVector(
				-(Bounds.Origin.X - Bounds.BoxExtent.X),
				-Bounds.Origin.Y,
				-(Bounds.Origin.Z - Bounds.BoxExtent.Z)));

			if (UMaterialInterface* DoorMaterial =
				FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("MI_DefaultMaterial")))
			{
				DoorMesh->SetMaterial(0, DoorMaterial);
			}
		}
	}

	OpenSound = FHapbeatSampleLibrary::LoadShowcaseAsset<USoundBase>(TEXT("Sounds"), TEXT("S_z2_door_open"));
	CloseSound = FHapbeatSampleLibrary::LoadShowcaseAsset<USoundBase>(TEXT("Sounds"), TEXT("S_z2_door_close"));
	SlamSound = FHapbeatSampleLibrary::LoadShowcaseAsset<USoundBase>(TEXT("Sounds"), TEXT("S_z2_door_slam"));
	LockSound = FHapbeatSampleLibrary::LoadShowcaseAsset<USoundBase>(TEXT("Sounds"), TEXT("S_z2_door_lock"));
	UnlockSound = FHapbeatSampleLibrary::LoadShowcaseAsset<USoundBase>(TEXT("Sounds"), TEXT("S_z2_door_unlock"));
	RattleSound = FHapbeatSampleLibrary::LoadShowcaseAsset<USoundBase>(TEXT("Sounds"), TEXT("S_z2_door_rattle"));
}

void AHapbeatShowcaseZ2DoorActor::PlayDoorSound(USoundBase* Sound) const
{
	if (Sound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation());
	}
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
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z2_door_slam"));
	const FGuid LockId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z2_door_lock"));
	const FGuid UnlockId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z2_door_unlock"));
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
	SlamClip = FHapbeatSampleLibrary::LoadSampleClip(this, TEXT("Showcase/Kit/showcase-kit/stream-clips/z2_door_slam.wav"));
	LockClip = FHapbeatSampleLibrary::LoadSampleClip(this, TEXT("Showcase/Kit/showcase-kit/stream-clips/z2_door_lock.wav"));
	UnlockClip = FHapbeatSampleLibrary::LoadSampleClip(this, TEXT("Showcase/Kit/showcase-kit/stream-clips/z2_door_unlock.wav"));
	RattleClip = FHapbeatSampleLibrary::LoadSampleClip(this, TEXT("Showcase/Kit/showcase-kit/stream-clips/z2_door_rattle.wav"));

	// Modes + intensities taken from the Unity Showcase's ShowcaseEventMap.asset
	// and Content/HapbeatSamples/Showcase/Kit/showcase-kit/showcase-kit-manifest.json
	// (schema 2.0.0) stream_events[...].parameters.intensity, all gain 1.00 --
	// every entry is StreamClip, so this zone needs no Kit deployed:
	//   z2_door_open    StreamClip  intensity 0.30 (one-shot, tracks the Opening tween)
	//   z2_door_close   StreamClip  intensity 0.20 (one-shot, tracks the Closing tween)
	//   z2_door_slam    StreamClip  intensity 0.25 (one-shot)
	//   z2_door_lock    StreamClip  intensity 0.30 (one-shot)
	//   z2_door_unlock  StreamClip  intensity 0.30 (one-shot)
	//   z2_door_rattle  StreamClip  intensity 0.25 (one-shot, tracks the rattle shake)
	// The sibling Samples~/Showcase/EventMaps/ShowcaseEventMap.md called slam /
	// lock / unlock "Command", but it is stale -- the .asset is the source of truth.
	const FHapbeatEventEntry OpenEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z2_door_open"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.30f, OpenClip, TEXT("z2_door_open"));
	const FHapbeatEventEntry CloseEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z2_door_close"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.20f, CloseClip, TEXT("z2_door_close"));
	const FHapbeatEventEntry SlamEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z2_door_slam"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.25f, SlamClip, TEXT("z2_door_slam"));
	const FHapbeatEventEntry LockEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z2_door_lock"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.30f, LockClip, TEXT("z2_door_lock"));
	const FHapbeatEventEntry UnlockEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z2_door_unlock"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.30f, UnlockClip, TEXT("z2_door_unlock"));
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
	// Shared sample convention: stop any in-flight StreamClip (every entry in this
	// zone is stream-mode) so nothing keeps buzzing past the actor's lifetime.
	for (UHapbeatTriggerComponent* Trigger : { OpenTrigger.Get(), CloseTrigger.Get(), SlamTrigger.Get(),
		LockTrigger.Get(), UnlockTrigger.Get(), RattleTrigger.Get() })
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
		PlayDoorSound(OpenSound);
		break;

	case EHapbeatZ2DoorState::Open:
		State = EHapbeatZ2DoorState::Closing;
		StateElapsedSeconds = 0.0f;
		if (CloseTrigger != nullptr)
		{
			CloseTrigger->Fire();
		}
		PlayDoorSound(CloseSound);
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
		PlayDoorSound(RattleSound);
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
		PlayDoorSound(SlamSound);
		break;

	case EHapbeatZ2DoorState::Locked:
		bRattling = true;
		RattleElapsedSeconds = 0.0f;
		if (RattleTrigger != nullptr)
		{
			RattleTrigger->Fire();
		}
		PlayDoorSound(RattleSound);
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
		PlayDoorSound(LockSound);
		break;

	case EHapbeatZ2DoorState::Locked:
		State = EHapbeatZ2DoorState::Closed;
		bRattling = false;
		SetDoorYaw(0.0f); // in case L lands mid-rattle-shake
		if (UnlockTrigger != nullptr)
		{
			UnlockTrigger->Fire();
		}
		PlayDoorSound(UnlockSound);
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

	// The Showcase switcher draws a shared Slate key guide covering this, so
	// only print the line when this zone is running on its own.
	if (!IsOwnedByShowcaseSwitcher(this))
	{
		FHapbeatSampleLibrary::ShowHudLine(KeyGuideHudLineKey,
			TEXT("Z2 Door -- F: open/close (rattles when locked)  G: slam (open)  L: lock/unlock"),
			FColor::Cyan, HudRefreshIntervalSeconds * 2.0f);
	}

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
	// Same reason: the shared HUD has a device / ping footer.
	if (!IsOwnedByShowcaseSwitcher(this))
	{
		FHapbeatSampleLibrary::ShowDeviceStatusLine(this, StatusHudLineKey + 1, HudRefreshIntervalSeconds * 2.0f);
	}
}

FText AHapbeatShowcaseZ2DoorActor::GetZoneLabel() const
{
	return FText::FromString(TEXT("Door"));
}

TArray<FHapbeatShowcaseHudCommand> AHapbeatShowcaseZ2DoorActor::GetHudCommands() const
{
	TArray<FHapbeatShowcaseHudCommand> Commands;
	Commands.Add({ FText::FromString(TEXT("F")), FText::FromString(TEXT("open / close (rattles when locked)")) });
	Commands.Add({ FText::FromString(TEXT("G")), FText::FromString(TEXT("slam (while open)")) });
	Commands.Add({ FText::FromString(TEXT("L")), FText::FromString(TEXT("lock / unlock")) });
	return Commands;
}

FTransform AHapbeatShowcaseZ2DoorActor::GetPlayerSpawnRelative() const
{
	// Unity Showcase.unity: Z2_Door/PlayerSpawn at (0.2, 0.2, -4) m. Unity
	// (x, y, z) m -> UE (z, x, y) cm, so 4 m back, 20 cm right, 20 cm up.
	return FTransform(FRotator::ZeroRotator, FVector(-400.0f, 20.0f, 20.0f));
}
