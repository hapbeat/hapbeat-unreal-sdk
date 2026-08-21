// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZ1BowlingActor.h"

#include "HapbeatClip.h"
#include "HapbeatCollisionTriggerComponent.h"
#include "HapbeatEventMap.h"
#include "HapbeatSampleLibrary.h"

#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h" // EKeys::*
#include "PhysicsEngine/BodyInstance.h" // BodyInstance.bNotifyRigidBodyCollision (pre-BeginPlay flag set)
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatShowcaseZ1, Log, All);

namespace
{
	// Lane / rack layout constants, centimeters (UE's world unit). Purely
	// cosmetic scene layout (approximate real-world scale) -- no protocol/gain
	// meaning. Engine basic shapes are 100 cm across their default axis (Cube
	// 100^3, Sphere/Cylinder 100 cm diameter x 100 cm tall).
	constexpr float LaneLength = 800.0f;
	constexpr float LaneWidth = 200.0f;
	constexpr float LaneThickness = 20.0f;
	constexpr float BallDiameter = 22.0f;
	constexpr float PinDiameter = 12.0f;
	constexpr float PinHeight = 38.0f;
	constexpr float PinRowSpacing = 50.0f;
	constexpr float PinLaneSpacing = 45.0f;
	constexpr float RackApexX = 650.0f; // leaves a 50 cm margin to the lane's far edge (800)
}

// =============================================================================
// AHapbeatShowcaseZ1BowlingActor
// =============================================================================

AHapbeatShowcaseZ1BowlingActor::AHapbeatShowcaseZ1BowlingActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// Plain scene root: unlike Z4/Z5 (single mesh = root), this zone has TWO
	// sibling meshes (lane + ball), so neither can be the actor's root on its own.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	LaneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LaneMesh"));
	LaneMesh->SetupAttachment(RootComponent);
	// Movable: the zone is spawned at runtime (Hapbeat Showcase zone switching) and its
	// Movable root is moved by FootprintOffset in BeginPlay. Lighting is dynamic.
	// Mobility is set before the mesh assignment so SetStaticMesh never runs on a Static component.
	LaneMesh->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		LaneMesh->SetStaticMesh(CubeMesh);
	}
	LaneMesh->SetRelativeLocation(FVector(LaneLength * 0.5f, 0.0f, -LaneThickness * 0.5f));
	LaneMesh->SetRelativeScale3D(FVector(LaneLength / 100.0f, LaneWidth / 100.0f, LaneThickness / 100.0f));
	LaneMesh->SetCollisionProfileName(TEXT("BlockAll"));

	BallRestRelativeLocation = FVector(0.0f, 0.0f, BallDiameter * 0.5f);
	BallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BallMesh"));
	BallMesh->SetupAttachment(RootComponent);
	BallMesh->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		BallMesh->SetStaticMesh(SphereMesh);
	}
	BallMesh->SetRelativeLocation(BallRestRelativeLocation);
	BallMesh->SetRelativeScale3D(FVector(BallDiameter / 100.0f));
	BallMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	// SetSimulatePhysics() is deferred to BeginPlay (see AHapbeatShowcaseZ1PinActor's
	// header note) -- calling it here, before BallMesh is registered, is order-
	// dependent and can log a spurious "no physics body" warning.

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

void AHapbeatShowcaseZ1BowlingActor::BeginPlay()
{
	Super::BeginPlay();

	if (RootComponent != nullptr)
	{
		RootComponent->SetRelativeLocation(FootprintOffset);
	}

	if (BallMesh != nullptr)
	{
		BallMesh->SetSimulatePhysics(true);
	}

	BuildEventMap();
	SpawnPinRack();
	BindInput();
}

void AHapbeatShowcaseZ1BowlingActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimer);
	}
	for (AHapbeatShowcaseZ1PinActor* Pin : PinActors)
	{
		if (IsValid(Pin))
		{
			Pin->Destroy();
		}
	}
	PinActors.Reset();

	Super::EndPlay(EndPlayReason);
}

void AHapbeatShowcaseZ1BowlingActor::BuildEventMap()
{
	EventMap = EventMapOverride != nullptr ? ToRawPtr(EventMapOverride) : BuildFallbackEventMap();
	if (EventMap == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ1, Warning, TEXT("Z1: no EventMap available; pin hits will not fire."));
		return;
	}

	// Look the id up by event name. The fallback map below authors the same
	// category / name / mode, so both paths go through this one resolution step
	// instead of duplicating the wiring.
	PinHitEntryId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z1_pin_hit"));
}

UHapbeatEventMap* AHapbeatShowcaseZ1BowlingActor::BuildFallbackEventMap()
{
	UHapbeatEventMap* Fallback = NewObject<UHapbeatEventMap>(this);
	Fallback->Entries.Reset(1);

	PinHitClip = FHapbeatSampleLibrary::LoadSampleClip(this, TEXT("Showcase/Kit/showcase-kit/stream-clips/z1_pin_hit.wav"));

	// Mode + intensity taken from the Unity Showcase's ShowcaseEventMap.asset and
	// Content/HapbeatSamples/Showcase/Kit/showcase-kit/showcase-kit-manifest.json
	// (schema 2.0.0) stream_events["showcase-kit.z1_pin_hit"].parameters.intensity:
	// StreamClip mode, gain 1.00 x intensity 0.25 = effective 0.25 (before each
	// pin's own VelocityScaled multiplier is folded in on top). The sibling
	// Samples~/Showcase/EventMaps/ShowcaseEventMap.md said "Command" here, but it
	// is stale -- the .asset is the source of truth and authors every one of its
	// 18 entries as StreamClip.
	Fallback->Entries.Add(FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z1_pin_hit"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.25f, PinHitClip, TEXT("z1_pin_hit")));
	return Fallback;
}

void AHapbeatShowcaseZ1BowlingActor::SpawnPinRack()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Standard 1-2-3 triangle rack (6 pins), apex nearest the ball's spawn point.
	const TArray<FVector2D> RackLayout = {
		FVector2D(RackApexX, 0.0f),
		FVector2D(RackApexX + PinRowSpacing, -PinLaneSpacing * 0.5f),
		FVector2D(RackApexX + PinRowSpacing, PinLaneSpacing * 0.5f),
		FVector2D(RackApexX + PinRowSpacing * 2.0f, -PinLaneSpacing),
		FVector2D(RackApexX + PinRowSpacing * 2.0f, 0.0f),
		FVector2D(RackApexX + PinRowSpacing * 2.0f, PinLaneSpacing),
	};
	const float PinZ = PinHeight * 0.5f;

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	PinActors.Reset(RackLayout.Num());
	for (const FVector2D& LocalXY : RackLayout)
	{
		const FVector SpawnLocation = GetActorLocation()
			+ GetActorRotation().RotateVector(FVector(LocalXY.X, LocalXY.Y, PinZ));

		AHapbeatShowcaseZ1PinActor* Pin = World->SpawnActor<AHapbeatShowcaseZ1PinActor>(
			SpawnLocation, GetActorRotation(), Params);
		if (Pin == nullptr || Pin->HitTrigger == nullptr)
		{
			UE_LOG(LogHapbeatShowcaseZ1, Warning, TEXT("Z1: failed to spawn/wire a pin."));
			continue;
		}
		Pin->HitTrigger->EventMap = EventMap;
		Pin->HitTrigger->EntryId = PinHitEntryId;
		PinActors.Add(Pin);
	}
}

void AHapbeatShowcaseZ1BowlingActor::BindInput()
{
	APlayerController* PC = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ1, Warning,
			TEXT("AHapbeatShowcaseZ1BowlingActor: no PlayerController found; input not bound."));
		return;
	}

	EnableInput(PC);
	if (InputComponent == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ1, Warning,
			TEXT("AHapbeatShowcaseZ1BowlingActor: EnableInput did not create an InputComponent; input not bound."));
		return;
	}

	InputComponent->BindKey(EKeys::B, IE_Pressed, this, &AHapbeatShowcaseZ1BowlingActor::HandleLaunchKey);
}

void AHapbeatShowcaseZ1BowlingActor::HandleLaunchKey()
{
	if (BallMesh == nullptr)
	{
		return;
	}

	ResetBallToSpawn();
	// AddImpulse with bVelChange=true adds directly to velocity (mass-independent),
	// matching Unity BallLauncher.Launch()'s direct `_ball.linearVelocity = dir *
	// _launchSpeed;` assignment onto a ball that was just zeroed by ResetBallToSpawn.
	BallMesh->AddImpulse(GetActorForwardVector() * LaunchSpeed, NAME_None, /*bVelChange=*/true);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimer);
		World->GetTimerManager().SetTimer(RespawnTimer, this,
			&AHapbeatShowcaseZ1BowlingActor::ResetBallToSpawn, RespawnDelaySeconds, /*bLoop=*/false);
	}
}

void AHapbeatShowcaseZ1BowlingActor::ResetBallToSpawn()
{
	if (BallMesh == nullptr)
	{
		return;
	}

	const FVector SpawnWorldLocation = GetActorLocation()
		+ GetActorRotation().RotateVector(BallRestRelativeLocation);
	// ResetPhysics: teleport + fully reset the physics body's velocity/angular
	// state (not just position), so a ball resting downrange -- or one that fell
	// off the lane -- comes back clean rather than carrying stale momentum.
	BallMesh->SetWorldLocation(SpawnWorldLocation, false, nullptr, ETeleportType::ResetPhysics);
	BallMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
	BallMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
}

void AHapbeatShowcaseZ1BowlingActor::Tick(float DeltaSeconds)
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
			TEXT("Z1 Bowling -- B: launch ball (pins fire haptics on their own, velocity-scaled)"),
			FColor::Cyan, HudRefreshIntervalSeconds * 2.0f);
	}
	FHapbeatSampleLibrary::ShowHudLine(StatusHudLineKey,
		FString::Printf(TEXT("Z1: %d pin(s) racked"), PinActors.Num()),
		FColor::Silver, HudRefreshIntervalSeconds * 2.0f);
	// Same reason: the shared HUD has a device / ping footer.
	if (!IsOwnedByShowcaseSwitcher(this))
	{
		FHapbeatSampleLibrary::ShowDeviceStatusLine(this, StatusHudLineKey + 1, HudRefreshIntervalSeconds * 2.0f);
	}
}

FText AHapbeatShowcaseZ1BowlingActor::GetZoneLabel() const
{
	return FText::FromString(TEXT("Bowling"));
}

TArray<FHapbeatShowcaseHudCommand> AHapbeatShowcaseZ1BowlingActor::GetHudCommands() const
{
	// Phase 1A keeps this zone's existing key; the Unity parity pass (LMB to
	// launch, Space to reset) lands with the rest of the zone rework.
	TArray<FHapbeatShowcaseHudCommand> Commands;
	Commands.Add({ FText::FromString(TEXT("B")),
		FText::FromString(TEXT("launch ball (pins fire haptics themselves, velocity-scaled)")) });
	return Commands;
}

FTransform AHapbeatShowcaseZ1BowlingActor::GetPlayerSpawnRelative() const
{
	// Unity Showcase.unity: Z1_Bowling/PlayerSpawn at (0, 0, -1.87) m, i.e.
	// 1.87 m behind the zone origin looking down the lane. Unity -Z is UE -X.
	return FTransform(FRotator::ZeroRotator, FVector(-187.0f, 0.0f, 0.0f));
}

// =============================================================================
// AHapbeatShowcaseZ1PinActor
// =============================================================================

AHapbeatShowcaseZ1PinActor::AHapbeatShowcaseZ1PinActor()
{
	PrimaryActorTick.bCanEverTick = false;

	PinMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PinMesh"));
	RootComponent = PinMesh;
	PinMesh->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		PinMesh->SetStaticMesh(CylinderMesh);
	}
	PinMesh->SetRelativeScale3D(FVector(PinDiameter / 100.0f, PinDiameter / 100.0f, PinHeight / 100.0f));
	PinMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	// SetSimulatePhysics() is deferred to BeginPlay: calling it here, before
	// PinMesh is registered, is order-dependent (it can log a spurious "no
	// physics body" warning against a not-yet-created BodyInstance). The notify
	// flag, however, must be set BEFORE the HitTrigger component's BeginPlay
	// (which runs during Super::BeginPlay and checks it for its setup hint), so
	// set the raw flag here — flag-only, needs no registered body.
	PinMesh->BodyInstance.bNotifyRigidBodyCollision = true;

	HitTrigger = CreateDefaultSubobject<UHapbeatCollisionTriggerComponent>(TEXT("HitTrigger"));
	HitTrigger->TriggerEvent = EHapbeatCollisionEvent::Hit;
	HitTrigger->GainMode = EHapbeatGainMode::VelocityScaled;
	// Unity BowlingPin.prefab HapbeatCollisionTrigger: _velocityThreshold=0.01,
	// _maxVelocity=1 (Unity units = meters/s, _gainMode=1=VelocityScaled, default
	// linear _velocityCurve). UE physics velocity is cm/s, so both figures are
	// scaled x100 (0.01 m/s -> 1 cm/s, 1 m/s -> 100 cm/s). VelocityCurve is left
	// at its constructor default (linear identity), matching Unity's default
	// AnimationCurve.Linear(0,0,1,1).
	HitTrigger->VelocityThreshold = 1.0f;
	HitTrigger->MaxVelocity = 100.0f;
	// EventMap / EntryId are assigned by the owning zone actor right after
	// SpawnActor (see AHapbeatShowcaseZ1BowlingActor::SpawnPinRack).
}

void AHapbeatShowcaseZ1PinActor::BeginPlay()
{
	Super::BeginPlay();

	if (PinMesh != nullptr)
	{
		PinMesh->SetSimulatePhysics(true);
		PinMesh->SetNotifyRigidBodyCollision(true); // required for OnComponentHit to fire
	}
}
