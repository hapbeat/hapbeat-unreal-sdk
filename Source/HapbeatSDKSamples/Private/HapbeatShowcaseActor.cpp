// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseActor.h"

#include "HapbeatSampleLibrary.h"
#include "HapbeatShowcaseZ1BowlingActor.h"
#include "HapbeatShowcaseZ2DoorActor.h"
#include "HapbeatShowcaseZ3FishingActor.h"
#include "HapbeatShowcaseZ4StreamConsoleActor.h"
#include "HapbeatShowcaseZ5ChargeShotActor.h"
#include "HapbeatSubsystem.h"

#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h" // EKeys::*

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
	Zones.Add(MakeZone(AHapbeatShowcaseZ2DoorActor::StaticClass(), TEXT("Door")));
	Zones.Add(MakeZone(AHapbeatShowcaseZ3FishingActor::StaticClass(), TEXT("Fishing")));
	Zones.Add(MakeZone(AHapbeatShowcaseZ4StreamConsoleActor::StaticClass(), TEXT("Stream Console")));
	Zones.Add(MakeZone(AHapbeatShowcaseZ5ChargeShotActor::StaticClass(), TEXT("Charge Shot")));
}

void AHapbeatShowcaseActor::BeginPlay()
{
	Super::BeginPlay();

	BindInput();
	ShowZone(InitialZone);
}

void AHapbeatShowcaseActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearActiveZone();
	CurrentZone = 0;

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
	const int32 BindCount = FMath::Min(Zones.Num(), MaxSwitchableZones);
	if (BindCount >= 1) { InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone1Key); }
	if (BindCount >= 2) { InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone2Key); }
	if (BindCount >= 3) { InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone3Key); }
	if (BindCount >= 4) { InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone4Key); }
	if (BindCount >= 5) { InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone5Key); }
	if (BindCount >= 6) { InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone6Key); }
	if (BindCount >= 7) { InputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone7Key); }
	if (BindCount >= 8) { InputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone8Key); }
	if (BindCount >= 9) { InputComponent->BindKey(EKeys::Nine, IE_Pressed, this, &AHapbeatShowcaseActor::HandleZone9Key); }
}

void AHapbeatShowcaseActor::ShowZone(int32 OneBasedIndex)
{
	if (Zones.Num() == 0)
	{
		UE_LOG(LogHapbeatShowcase, Warning, TEXT("Showcase: no zones configured; nothing to show."));
		return;
	}

	const int32 Index = FMath::Clamp(OneBasedIndex, 1, FMath::Min(Zones.Num(), MaxSwitchableZones));
	if (Index == CurrentZone && IsValid(ActiveZoneActor))
	{
		return;
	}

	ClearActiveZone();
	CurrentZone = Index;
	SpawnActiveZone();
}

void AHapbeatShowcaseActor::ClearActiveZone()
{
	// The zone's own EndPlay stops its stream / loop and tears down the actors
	// it spawned (pins, shark, targets), so destroying it is the switch.
	if (IsValid(ActiveZoneActor))
	{
		ActiveZoneActor->Destroy();
	}
	ActiveZoneActor = nullptr;

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
	ActiveZoneActor = World->SpawnActor<AActor>(Entry.ZoneClass, GetActorLocation(), GetActorRotation(), Params);
	if (ActiveZoneActor == nullptr)
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

	HudRefreshTimer -= DeltaSeconds;
	if (HudRefreshTimer > 0.0f)
	{
		return;
	}
	HudRefreshTimer = HudRefreshIntervalSeconds;

	// One line listing every zone, the active one wrapped in asterisks --
	// each zone prints its own key guide on its own HUD line below this.
	FString Guide = TEXT("Showcase");
	const int32 ShownCount = FMath::Min(Zones.Num(), MaxSwitchableZones);
	for (int32 i = 0; i < ShownCount; ++i)
	{
		const FString Label = Zones[i].Label.IsEmpty()
			? FString::Printf(TEXT("Zone %d"), i + 1)
			: Zones[i].Label.ToString();
		Guide += (i + 1 == CurrentZone)
			? FString::Printf(TEXT("  *[%d] %s*"), i + 1, *Label)
			: FString::Printf(TEXT("  [%d] %s"), i + 1, *Label);
	}
	FHapbeatSampleLibrary::ShowHudLine(KeyGuideHudLineKey, Guide,
		FColor::Yellow, HudRefreshIntervalSeconds * 2.0f);

	FHapbeatSampleLibrary::ShowDeviceStatusLine(this, StatusHudLineKey, HudRefreshIntervalSeconds * 2.0f);
}
