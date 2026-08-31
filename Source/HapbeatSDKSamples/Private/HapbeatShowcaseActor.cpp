// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseActor.h"

#include "HapbeatClip.h"
#include "HapbeatEventMap.h"
#include "HapbeatSampleLibrary.h"
#include "HapbeatShowcaseCharacter.h"
#include "HapbeatShowcaseZone.h"
#include "HapbeatShowcaseZ1BowlingActor.h"
#include "HapbeatShowcaseZ3FishingActor.h"
#include "HapbeatShowcaseZ5ChargeShotActor.h"
#include "HapbeatSubsystem.h"
#include "SHapbeatShowcaseHud.h"

#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h" // TActorIterator (finding the zones placed in the level)
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h" // EKeys::*
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatShowcase, Log, All);

namespace
{
	/** Highest zone index reachable from the number-key row. */
	constexpr int32 MaxSwitchableZones = 9;

	FHapbeatShowcaseZoneEntry MakeZone(TSubclassOf<AActor> ZoneClass, const TCHAR* Label)
	{
		FHapbeatShowcaseZoneEntry Entry;
		Entry.ZoneClass = ZoneClass;
		Entry.Label = FText::FromString(Label);
		return Entry;
	}

	/**
	 * The event the Q key fires. Unity wires its "manual_fire" hotkey to one
	 * fixed EventMap entry -- z5_tar_hit_light, a short, unmistakable one-shot --
	 * rather than to anything zone-specific (Showcase.unity, HapbeatKeyDispatcher
	 * binding key 113 -> HapbeatUnityEventTrigger.Fire on entry
	 * c840e0dd... = showcase-kit / z5_tar_hit_light). Same here, so Q means "is
	 * the device answering?" in every zone.
	 */
	const TCHAR* ManualFireCategory = TEXT("showcase-kit");
	const TCHAR* ManualFireEventName = TEXT("z5_tar_hit_light");
	/** From showcase-kit-manifest.json, same value Z5 hardcodes for this event. */
	constexpr float ManualFireIntensity = 0.45f;

	/** Rows shown in every zone; the zone's own rows follow them. */
	TArray<FHapbeatShowcaseHudCommand> MakeGlobalHudCommands()
	{
		// Mirrors Unity HudGuide._globalHeader, plus Tab (UE needs the cursor
		// toggle spelled out because PIE starts with the mouse captured).
		TArray<FHapbeatShowcaseHudCommand> Commands;
		Commands.Add({ FText::FromString(TEXT("WASD")), FText::FromString(TEXT("move")) });
		Commands.Add({ FText::FromString(TEXT("Mouse")), FText::FromString(TEXT("look")) });
		Commands.Add({ FText::FromString(TEXT("1-5")), FText::FromString(TEXT("zone switch")) });
		Commands.Add({ FText::FromString(TEXT("Q")), FText::FromString(TEXT("manual fire")) });
		Commands.Add({ FText::FromString(TEXT("P")), FText::FromString(TEXT("ping")) });
		Commands.Add({ FText::FromString(TEXT("Tab")), FText::FromString(TEXT("cursor lock / release")) });
		return Commands;
	}
}

AHapbeatShowcaseActor::AHapbeatShowcaseActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// Plain scene root: this actor is a switcher, not a visual -- the transform
	// exists purely as the spawn pose handed to whichever zone is active.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// Seed the five shipped zones so the actor is useful the moment it is
	// dropped into a level. Labels match the docs' zone table.
	Zones.Reset(5);
	Zones.Add(MakeZone(AHapbeatShowcaseZ1BowlingActor::StaticClass(), TEXT("Bowling")));
	// Z2 and Z4 are authored as ordinary Blueprint assets.  Referencing the
	// generated class here also keeps the spawn-only fallback equivalent to the
	// placed Showcase map, rather than silently reverting to a separate C++ demo.
	static ConstructorHelpers::FClassFinder<AActor> DoorBlueprint(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/BP_Z2_Door"));
	Zones.Add(MakeZone(DoorBlueprint.Class, TEXT("Door")));
	Zones.Add(MakeZone(AHapbeatShowcaseZ3FishingActor::StaticClass(), TEXT("Fishing")));
	static ConstructorHelpers::FClassFinder<AActor> StreamConsoleBlueprint(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/BP_Z4_StreamConsole"));
	Zones.Add(MakeZone(StreamConsoleBlueprint.Class, TEXT("Stream Console")));
	Zones.Add(MakeZone(AHapbeatShowcaseZ5ChargeShotActor::StaticClass(), TEXT("Charge Shot")));

	// Same authored asset the zones default to, so Q fires through the same
	// EventMap the rest of the Showcase does.
	static ConstructorHelpers::FObjectFinder<UHapbeatEventMap> DefaultEventMap(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/EM_Showcase.EM_Showcase"));
	if (DefaultEventMap.Succeeded())
	{
		ManualFireEventMapOverride = DefaultEventMap.Object;
	}
}

void AHapbeatShowcaseActor::BeginPlay()
{
	Super::BeginPlay();

	BuildManualFireEventMap();
	CollectPlacedZones();
	BindInput();
	CreateHud();

	if (UHapbeatSubsystem* Subsystem = ResolveSubsystem())
	{
		// The HUD's round-trip readout: Unity's GlobalHotkeys does the same,
		// subscribing to OnPong and showing the RTT the P key asked for.
		Subsystem->OnPong.AddDynamic(this, &AHapbeatShowcaseActor::HandlePong);
		bPongSubscribed = true;
	}

	// NOT ShowZone(InitialZone) here: the placed zones' BeginPlay is not ordered
	// against ours, and a zone that begins play after us would push its input
	// component back onto the player's stack, undoing the deactivation. Tick
	// applies it instead -- by then every actor's BeginPlay has run.
}

void AHapbeatShowcaseActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bPongSubscribed)
	{
		if (UHapbeatSubsystem* Subsystem = ResolveSubsystem())
		{
			Subsystem->OnPong.RemoveDynamic(this, &AHapbeatShowcaseActor::HandlePong);
		}
		bPongSubscribed = false;
	}

	// Take the guide down before the zone, so nothing is left drawing over a
	// stopped session.
	if (HudWidget.IsValid())
	{
		const UWorld* World = GetWorld();
		if (UGameViewportClient* Viewport = World != nullptr ? World->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(HudWidget.ToSharedRef());
		}
		HudWidget.Reset();
	}

	ClearActiveZone();
	PlacedZones.Reset();
	CurrentZone = 0;
	PlayerStateAppliedZone = 0;
	bInitialZoneApplied = false;

	Super::EndPlay(EndPlayReason);
}

void AHapbeatShowcaseActor::BindInput()
{
	APlayerController* PC = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)
	{
		UE_LOG(LogHapbeatShowcase, Warning,
			TEXT("AHapbeatShowcaseActor: no PlayerController found; zone switching keys not bound."));
		return;
	}

	EnableInput(PC);
	if (InputComponent == nullptr)
	{
		UE_LOG(LogHapbeatShowcase, Warning,
			TEXT("AHapbeatShowcaseActor: EnableInput did not create an InputComponent; keys not bound."));
		return;
	}

	// Bind only as many digits as there are zones, so an unused key stays free
	// for whatever else the level does with it.
	const int32 BindCount = FMath::Min(GetZoneCount(), MaxSwitchableZones);
	if (BindCount >= 1) { InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone1Key); }
	if (BindCount >= 2) { InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone2Key); }
	if (BindCount >= 3) { InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone3Key); }
	if (BindCount >= 4) { InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone4Key); }
	if (BindCount >= 5) { InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone5Key); }
	if (BindCount >= 6) { InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone6Key); }
	if (BindCount >= 7) { InputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone7Key); }
	if (BindCount >= 8) { InputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone8Key); }
	if (BindCount >= 9) { InputComponent->BindKey(EKeys::Nine, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone9Key); }

	// The two global hotkeys, bound here rather than on a zone because they mean
	// the same thing everywhere (Unity keeps them on a scene-level
	// [Hapbeat Event Router] object for the same reason).
	InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AHapbeatShowcaseActor::HandleManualFireKey);
	InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AHapbeatShowcaseActor::HandlePingKey);
}

void AHapbeatShowcaseActor::CollectPlacedZones()
{
	PlacedZones.Reset();

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (IsValid(Actor) && Actor->Implements<UHapbeatShowcaseZone>())
		{
			PlacedZones.Add(Actor);
		}
	}
	if (PlacedZones.Num() == 0)
	{
		// Nothing placed -- the spawn fallback takes over. Not a warning: a bare
		// level with just this actor in it is a supported way to run the Showcase.
		return;
	}

	// Sorted by the zone's own number, so the keys mean Z1..Z5 regardless of the
	// order the actors happen to come out of the iterator in. An unnumbered zone
	// (GetZoneIndex() == 0) sorts last rather than stealing key 1.
	PlacedZones.Sort([](const TObjectPtr<AActor>& A, const TObjectPtr<AActor>& B)
	{
		const IHapbeatShowcaseZone* ZoneA = Cast<IHapbeatShowcaseZone>(A.Get());
		const IHapbeatShowcaseZone* ZoneB = Cast<IHapbeatShowcaseZone>(B.Get());
		const int32 IndexA = ZoneA != nullptr && ZoneA->GetZoneIndex() > 0 ? ZoneA->GetZoneIndex() : MAX_int32;
		const int32 IndexB = ZoneB != nullptr && ZoneB->GetZoneIndex() > 0 ? ZoneB->GetZoneIndex() : MAX_int32;
		return IndexA < IndexB;
	});

	UE_LOG(LogHapbeatShowcase, Log, TEXT("Showcase: driving %d zone actor(s) placed in the level."),
		PlacedZones.Num());
}

int32 AHapbeatShowcaseActor::GetZoneCount() const
{
	return PlacedZones.Num() > 0 ? PlacedZones.Num() : Zones.Num();
}

AActor* AHapbeatShowcaseActor::GetActiveZoneActor() const
{
	if (PlacedZones.Num() > 0)
	{
		return PlacedZones.IsValidIndex(CurrentZone - 1) ? PlacedZones[CurrentZone - 1].Get() : nullptr;
	}
	return SpawnedZoneActor.Get();
}

void AHapbeatShowcaseActor::ShowZone(int32 OneBasedIndex)
{
	const int32 ZoneCount = GetZoneCount();
	if (ZoneCount == 0)
	{
		UE_LOG(LogHapbeatShowcase, Warning, TEXT("Showcase: no zones placed or configured; nothing to show."));
		return;
	}

	// CurrentZone is 0 until the first call, and Index is always >= 1, so the
	// first call always goes through -- which is what deactivates the other four
	// placed zones.
	const int32 Index = FMath::Clamp(OneBasedIndex, 1, FMath::Min(ZoneCount, MaxSwitchableZones));
	if (Index == CurrentZone && IsValid(GetActiveZoneActor()))
	{
		return;
	}

	ClearActiveZone();
	CurrentZone = Index;

	if (PlacedZones.Num() > 0)
	{
		// Every zone off, then the chosen one on. Doing the whole set (rather
		// than just the outgoing one) makes the first call correct too, and
		// costs nothing at five zones.
		for (int32 ZoneIndex = 0; ZoneIndex < PlacedZones.Num(); ++ZoneIndex)
		{
			AActor* ZoneActor = PlacedZones[ZoneIndex].Get();
			if (!IsValid(ZoneActor) || ZoneIndex == CurrentZone - 1)
			{
				continue;
			}
			IHapbeatShowcaseZone::SetZoneSceneActive(ZoneActor, false);
			if (IHapbeatShowcaseZone* Zone = Cast<IHapbeatShowcaseZone>(ZoneActor))
			{
				Zone->OnZoneDeactivated();
			}
		}

		if (AActor* ActiveActor = GetActiveZoneActor())
		{
			IHapbeatShowcaseZone::SetZoneSceneActive(ActiveActor, true);
			if (IHapbeatShowcaseZone* Zone = Cast<IHapbeatShowcaseZone>(ActiveActor))
			{
				Zone->OnZoneActivated();
			}
		}
	}
	else
	{
		SpawnActiveZone();
	}

	// After the switch: the zone actor is what answers where the player stands,
	// whether it wants the cursor, and what its key rows are.
	ApplyZonePlayerState();
	RefreshHudContent();
}

void AHapbeatShowcaseActor::ClearActiveZone()
{
	// Fallback path: the zone was spawned, so destroying it is the teardown (its
	// own EndPlay stops that zone's haptics). Placed zones are hidden instead --
	// ShowZone does that for the whole set right after this call.
	if (PlacedZones.Num() == 0 && IsValid(SpawnedZoneActor))
	{
		SpawnedZoneActor->Destroy();
	}
	SpawnedZoneActor = nullptr;

	// Belt and braces: anything a zone left playing (or a device left ringing
	// mid-clip) stops here, before the next zone starts sending. Same pair
	// BasicExample's S key uses.
	if (UHapbeatSubsystem* Subsystem = ResolveSubsystem())
	{
		Subsystem->StopStream();
		Subsystem->StopAll();
	}
}

void AHapbeatShowcaseActor::SpawnActiveZone()
{
	UWorld* World = GetWorld();
	if (World == nullptr || !Zones.IsValidIndex(CurrentZone - 1))
	{
		return;
	}

	const FHapbeatShowcaseZoneEntry& Entry = Zones[CurrentZone - 1];
	if (Entry.ZoneClass == nullptr)
	{
		UE_LOG(LogHapbeatShowcase, Warning, TEXT("Showcase: zone %d has no ZoneClass set."), CurrentZone);
		return;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Spawned at THIS actor's pose, so every zone appears where the switcher was
	// placed (each zone lays its own geometry out relative to its transform).
	SpawnedZoneActor = World->SpawnActor<AActor>(Entry.ZoneClass, GetActorLocation(), GetActorRotation(), Params);
	if (SpawnedZoneActor == nullptr)
	{
		UE_LOG(LogHapbeatShowcase, Warning, TEXT("Showcase: failed to spawn zone %d (%s)."),
			CurrentZone, *Entry.ZoneClass->GetName());
	}
}

UHapbeatSubsystem* AHapbeatShowcaseActor::ResolveSubsystem() const
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		return GameInstance->GetSubsystem<UHapbeatSubsystem>();
	}
	return nullptr;
}

void AHapbeatShowcaseActor::HandleZone1Key() { ShowZone(1); }
void AHapbeatShowcaseActor::HandleZone2Key() { ShowZone(2); }
void AHapbeatShowcaseActor::HandleZone3Key() { ShowZone(3); }
void AHapbeatShowcaseActor::HandleZone4Key() { ShowZone(4); }
void AHapbeatShowcaseActor::HandleZone5Key() { ShowZone(5); }
void AHapbeatShowcaseActor::HandleZone6Key() { ShowZone(6); }
void AHapbeatShowcaseActor::HandleZone7Key() { ShowZone(7); }
void AHapbeatShowcaseActor::HandleZone8Key() { ShowZone(8); }
void AHapbeatShowcaseActor::HandleZone9Key() { ShowZone(9); }

void AHapbeatShowcaseActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The initial zone lands here rather than in BeginPlay: see
	// bInitialZoneApplied's comment (a placed zone that begins play after us
	// would otherwise re-enable its own input).
	if (!bInitialZoneApplied)
	{
		// Set first: a level with no zones at all makes ShowZone a warning-only
		// no-op, and this must not repeat that warning every frame.
		bInitialZoneApplied = true;
		ShowZone(InitialZone);
	}

	// The player pawn may not exist yet when the initial zone is shown
	// (spawning / possession is not ordered against actor BeginPlay), so keep
	// trying until it does -- otherwise the player would never leave the
	// PlayerStart. Costs one pawn lookup per frame and stops as soon as it
	// succeeds; with a non-Showcase pawn it simply never succeeds, which is the
	// same no-op as before.
	if (CurrentZone != 0 && PlayerStateAppliedZone != CurrentZone)
	{
		ApplyZonePlayerState();
	}

	HudRefreshTimer -= DeltaSeconds;
	if (HudRefreshTimer > 0.0f)
	{
		return;
	}
	HudRefreshTimer = HudRefreshIntervalSeconds;

	// Only the live number here -- the key rows change on zone switch, not on a
	// timer, so they are pushed from RefreshHudContent instead.
	if (HudWidget.IsValid())
	{
		const UHapbeatSubsystem* Subsystem = ResolveSubsystem();
		HudWidget->SetDeviceCount(Subsystem != nullptr ? Subsystem->GetAliveDeviceCount() : 0);
	}
}

void AHapbeatShowcaseActor::CreateHud()
{
	const UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World != nullptr ? World->GetGameViewport() : nullptr;
	if (Viewport == nullptr)
	{
		UE_LOG(LogHapbeatShowcase, Warning,
			TEXT("Showcase: no game viewport; the on-screen key guide will not be shown."));
		return;
	}

	HudWidget = SNew(SHapbeatShowcaseHud).GlobalCommands(MakeGlobalHudCommands());
	// ZOrder 0: this is ordinary HUD content, and must sit UNDER the runtime's
	// address-override panel (ZOrder 100) if a level shows both.
	Viewport->AddViewportWidgetContent(HudWidget.ToSharedRef(), /*ZOrder=*/0);
}

void AHapbeatShowcaseActor::RefreshHudContent()
{
	if (!HudWidget.IsValid())
	{
		return;
	}

	const IHapbeatShowcaseZone* Zone = Cast<IHapbeatShowcaseZone>(GetActiveZoneActor());

	// "[2] Door" -- Unity HudGuide builds the same "[Zone N] Label" title. The
	// zone's own label wins over the switcher's entry, so a zone dropped in by
	// hand (or an entry left unlabelled) still names itself correctly.
	FText Label = Zone != nullptr ? Zone->GetZoneLabel() : FText::GetEmpty();
	if (Label.IsEmpty() && Zones.IsValidIndex(CurrentZone - 1))
	{
		Label = Zones[CurrentZone - 1].Label;
	}
	const FText Title = FText::FromString(FString::Printf(TEXT("[%d] %s"),
		CurrentZone, Label.IsEmpty() ? TEXT("Zone") : *Label.ToString()));

	// One line listing every zone, the active one wrapped in asterisks. Each
	// label comes from the placed zone itself when there is one, so the list
	// describes what is actually in the level rather than the fallback table.
	FString ZoneList;
	const int32 ShownCount = FMath::Min(GetZoneCount(), MaxSwitchableZones);
	for (int32 i = 0; i < ShownCount; ++i)
	{
		FText EntryLabel = FText::GetEmpty();
		if (PlacedZones.IsValidIndex(i))
		{
			if (const IHapbeatShowcaseZone* PlacedZone = Cast<IHapbeatShowcaseZone>(PlacedZones[i].Get()))
			{
				EntryLabel = PlacedZone->GetZoneLabel();
			}
		}
		if (EntryLabel.IsEmpty() && Zones.IsValidIndex(i))
		{
			EntryLabel = Zones[i].Label;
		}
		const FString ZoneLabel = EntryLabel.IsEmpty()
			? FString::Printf(TEXT("Zone %d"), i + 1)
			: EntryLabel.ToString();
		ZoneList += (i + 1 == CurrentZone)
			? FString::Printf(TEXT("*[%d] %s*  "), i + 1, *ZoneLabel)
			: FString::Printf(TEXT("[%d] %s  "), i + 1, *ZoneLabel);
	}

	TArray<FHapbeatShowcaseHudCommand> ZoneCommands;
	if (Zone != nullptr)
	{
		ZoneCommands = Zone->GetHudCommands();
	}

	HudWidget->SetContent(Title, ZoneCommands, FText::FromString(ZoneList.TrimEnd()));
}

void AHapbeatShowcaseActor::ApplyZonePlayerState()
{
	AHapbeatShowcaseCharacter* Character = ResolveShowcaseCharacter();
	if (Character == nullptr)
	{
		// A DefaultPawn (or no pawn at all): the switcher still works, there is
		// just nobody to move. Not a warning -- placing this actor in a bare
		// level is a supported way to look at a zone.
		return;
	}

	AActor* ZoneActor = GetActiveZoneActor();
	const IHapbeatShowcaseZone* Zone = Cast<IHapbeatShowcaseZone>(ZoneActor);
	if (Zone == nullptr)
	{
		return;
	}

	// Zone-relative -> world, against the ZONE ACTOR's own transform: a placed
	// zone stands in its own room somewhere else in the map, so composing
	// against the switcher's transform (which is what the spawn-only version
	// did) would drop the player in the wrong room. The fallback path spawns the
	// zone at the switcher's transform, so the two agree there.
	FTransform Spawn = Zone->GetPlayerSpawnRelative() * ZoneActor->GetActorTransform();

	// The interface's Z is the player's FEET; a capsule is positioned by its
	// centre, so lift it by the half-height (see GetPlayerSpawnRelative).
	FVector Location = Spawn.GetLocation();
	if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		Location.Z += Capsule->GetScaledCapsuleHalfHeight();
	}
	Spawn.SetLocation(Location);

	Character->TeleportToSpawn(Spawn);
	Character->SetCursorUnlocked(Zone->WantsCursorUnlocked());

	// Applied: Tick stops retrying until the next zone change.
	PlayerStateAppliedZone = CurrentZone;
}

void AHapbeatShowcaseActor::DebugPlacePlayer(FVector WorldLocation, float Yaw, float Pitch)
{
	AHapbeatShowcaseCharacter* Character = ResolveShowcaseCharacter();
	if (Character == nullptr)
	{
		UE_LOG(LogHapbeatShowcase, Warning,
			TEXT("DebugPlacePlayer: no Showcase character possessed; nobody to move."));
		return;
	}

	// Feet -> capsule centre, the same correction ApplyZonePlayerState makes, so
	// a caller can pass zone-floor coordinates either way round the two paths.
	if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		WorldLocation.Z += Capsule->GetScaledCapsuleHalfHeight();
	}

	// TeleportToSpawn levels the pitch (it is the zone-entry path), so the pitch
	// is applied after it rather than folded into the transform.
	Character->TeleportToSpawn(FTransform(FRotator(0.0f, Yaw, 0.0f), WorldLocation));
	Character->SetViewPitchForCapture(Pitch);
}

AHapbeatShowcaseCharacter* AHapbeatShowcaseActor::ResolveShowcaseCharacter() const
{
	return Cast<AHapbeatShowcaseCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
}

void AHapbeatShowcaseActor::BuildManualFireEventMap()
{
	ManualFireEventMap = ManualFireEventMapOverride;

	if (ManualFireEventMap == nullptr)
	{
		// Same shape as each zone's fallback: load the WAV, keep it alive in a
		// UPROPERTY (the entry's soft pointer is not a strong reference), and
		// author the single entry Q needs.
		ManualFireClip = FHapbeatSampleLibrary::LoadSampleClip(this,
			TEXT("Showcase/Kit/showcase-kit/stream-clips/z5_tar_hit_light.wav"));

		UHapbeatEventMap* Fallback = NewObject<UHapbeatEventMap>(this);
		Fallback->Entries.Reset(1);
		Fallback->Entries.Add(FHapbeatSampleLibrary::MakeEntry(
			EHapticMode::StreamClip, ManualFireCategory, ManualFireEventName,
			1.0f, /*bLoop=*/false, ManualFireIntensity, ManualFireClip, ManualFireEventName));
		ManualFireEventMap = Fallback;
	}

	ManualFireEntryId = FHapbeatSampleLibrary::FindEntryId(
		ManualFireEventMap, EHapticMode::StreamClip, ManualFireCategory, ManualFireEventName);
}

void AHapbeatShowcaseActor::HandleManualFireKey()
{
	UHapbeatSubsystem* Subsystem = ResolveSubsystem();
	if (Subsystem == nullptr || ManualFireEventMap == nullptr || !ManualFireEntryId.IsValid())
	{
		UE_LOG(LogHapbeatShowcase, Warning,
			TEXT("Showcase: manual fire (Q) has no event to play."));
		return;
	}
	Subsystem->PlayEntry(ManualFireEventMap, ManualFireEntryId);
}

void AHapbeatShowcaseActor::HandlePingKey()
{
	if (UHapbeatSubsystem* Subsystem = ResolveSubsystem())
	{
		Subsystem->Ping();
		if (HudWidget.IsValid())
		{
			// Shows "ping: ..." until the PONG lands, so a dead link is visible
			// as a reading that never resolves (Unity NotifyPingSent).
			HudWidget->SetPingPending();
		}
	}
}

void AHapbeatShowcaseActor::HandlePong(const FString& Endpoint, int64 RttUs, const FString& DeviceName,
	const FString& Address, const FString& Firmware)
{
	if (HudWidget.IsValid())
	{
		HudWidget->SetRoundTripMs(static_cast<float>(RttUs) / 1000.0f);
	}
}
