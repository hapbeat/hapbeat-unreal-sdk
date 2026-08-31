// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZ1BowlingActor.h"

#include "HapbeatClip.h"
#include "HapbeatCollisionTriggerComponent.h"
#include "HapbeatEventMap.h"
#include "HapbeatSampleLibrary.h"
#include "HapbeatShowcaseCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h" // EKeys::*
#include "Kismet/GameplayStatics.h" // PlaySoundAtLocation / GetPlayerPawn
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodyInstance.h" // BodyInstance.bNotifyRigidBodyCollision (pre-BeginPlay flag set)
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatShowcaseZ1, Log, All);

namespace
{
	// Unity Showcase.unity, Z1_Bowling subtree, converted: UE (X, Y, Z) cm =
	// (Unity z, Unity x, Unity y) x 100.

	/** Lane: Unity local (0, 0.85, 3.672), scale (2.26485, 0.1, 9.39381). */
	const FVector LaneCentreCm(367.2f, 0.0f, 85.0f);
	const FVector LaneSizeCm(939.381f, 226.485f, 10.0f); // top face therefore at Z = 90

	/**
	 * Ball: Unity local X/Y, but sized and seated here rather than converted.
	 *
	 * DELIBERATE DEPARTURE FROM UNITY (user call): Unity's ball is a 40 cm
	 * sphere, which is nearly twice a real one. A regulation bowling ball is
	 * 21.6 cm across, so this one is 22. Its mass stays Unity's 4 kg -- the
	 * number the rack's scatter was tuned against.
	 *
	 * The rest height follows from the size instead of from Unity's 129.5: the
	 * lane's top face is at Z = 90 (see LaneCentreCm / LaneSizeCm), so the ball
	 * sits at 90 + its radius and touches the lane exactly. Keeping the converted
	 * 129.5 would have left a 22 cm ball hanging 28 cm in the air.
	 */
	constexpr float BallDiameterCm = 22.0f;
	constexpr float LaneTopCm = 90.0f;
	const FVector BallRestCm(-100.0f, 0.0f, LaneTopCm + BallDiameterCm * 0.5f);
	constexpr float BallMassKg = 4.0f;

	/**
	 * Pins: Unity Pin_1..Pin_6. Unity has them at y = 1.295 m; here they sit half
	 * a centimetre higher, at Z = 130, because the UE pin is a capsule of half
	 * height 40 -- 130 puts its bottom exactly on the lane's top face (Z = 90),
	 * whereas the converted 129.5 buried it 0.5 cm INTO the lane and the solver
	 * answered that penetration with a burst of contacts the moment play
	 * started, i.e. a rack that fired hit haptics before anything touched it.
	 */
	const FVector PinRestCm[] = {
		FVector(600.0f,   0.0f, 130.0f), // Pin_1  Unity (0,    1.295, 6)
		FVector(650.0f, -40.0f, 130.0f), // Pin_2  Unity (-0.4, 1.295, 6.5)
		FVector(650.0f,  40.0f, 130.0f), // Pin_3  Unity (0.4,  1.295, 6.5)
		FVector(700.0f, -80.0f, 130.0f), // Pin_4  Unity (-0.8, 1.295, 7)
		FVector(700.0f,   0.0f, 130.0f), // Pin_5  Unity (0,    1.295, 7)
		FVector(700.0f,  80.0f, 130.0f), // Pin_6  Unity (0.8,  1.295, 7)
	};
	constexpr int32 PinCount = UE_ARRAY_COUNT(PinRestCm);

	constexpr float PinCapsuleRadiusCm = 9.6f;
	constexpr float PinCapsuleHalfHeightCm = 40.0f;
	constexpr float PinMassKg = 0.5f;

	/** Unity Z1_Bowling/PlayerSpawn: local (0, 0, -1.87). */
	const FVector PlayerSpawnCm(-187.0f, 0.0f, 0.0f);
}

// The tag that marks "a thing a pin may react to". Defined here rather than
// inline so the whole zone shares one FName.
const FName AHapbeatShowcaseZ1BowlingActor::ContactTag(TEXT("HapbeatShowcaseZ1Contact"));

// =============================================================================
// AHapbeatShowcaseZ1BowlingActor
// =============================================================================

AHapbeatShowcaseZ1BowlingActor::AHapbeatShowcaseZ1BowlingActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// Plain scene root: this zone has several sibling pieces (lane, ball, six
	// pin slots), so none of them can be the actor's root on its own.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	LaneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LaneMesh"));
	LaneMesh->SetupAttachment(RootComponent);
	// Movable: the switcher hides and shows this zone, so its lighting is dynamic.
	// Mobility is set before the mesh assignment so SetStaticMesh never runs on a
	// Static component.
	LaneMesh->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		LaneMesh->SetStaticMesh(CubeMesh);
	}
	LaneMesh->SetRelativeLocation(LaneCentreCm);
	LaneMesh->SetRelativeScale3D(LaneSizeCm / 100.0f); // engine Cube is 100 cm authored
	LaneMesh->SetCollisionProfileName(TEXT("BlockAll"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> LaneMaterial(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_BowlingLane.MI_BowlingLane"));
	if (LaneMaterial.Succeeded())
	{
		LaneMesh->SetMaterial(0, LaneMaterial.Object);
	}

	// The ball, like the pins, is a child actor -- see the class comment: it has
	// to be an actor of its own to carry ContactTag without the lane inheriting it.
	BallRestRelativeLocation = BallRestCm;
	BallSlot = CreateDefaultSubobject<UChildActorComponent>(TEXT("Ball"));
	BallSlot->SetupAttachment(RootComponent);
	BallSlot->SetMobility(EComponentMobility::Movable);
	BallSlot->SetChildActorClass(AHapbeatShowcaseZ1BallActor::StaticClass());
	BallSlot->SetRelativeLocation(BallRestRelativeLocation);

	// One child actor per pin: an editable, saved relative transform in this
	// actor's Details panel, and an actor of its own for the collision trigger
	// to bind to (see the class comment).
	PinSlots.Reset(PinCount);
	for (int32 Index = 0; Index < PinCount; ++Index)
	{
		UChildActorComponent* Slot = CreateDefaultSubobject<UChildActorComponent>(
			*FString::Printf(TEXT("Pin%d"), Index + 1));
		Slot->SetupAttachment(RootComponent);
		Slot->SetMobility(EComponentMobility::Movable);
		Slot->SetChildActorClass(AHapbeatShowcaseZ1PinActor::StaticClass());
		Slot->SetRelativeLocation(PinRestCm[Index]);
		PinSlots.Add(Slot);
	}

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
		PinHitEvent.EntryId = FHapbeatSampleLibrary::FindEntryId(
			DefaultEventMap.Object, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z1_pin_hit"));
	}
	static ConstructorHelpers::FObjectFinder<USoundBase> PinHitSound(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z1_pin_hit.S_z1_pin_hit"));
	if (PinHitSound.Succeeded())
	{
		PinHitSoundAsset = PinHitSound.Object;
	}
}

void AHapbeatShowcaseZ1BowlingActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdatePinVisuals();

	// A UChildActorComponent creates its child after its owner's construction
	// pass.  Calling SetUpPins() here therefore races that creation whenever a
	// Details value is edited: GetChildActor() is temporarily null and the
	// editor emits one false "pin will not fire" warning per slot.  The editable
	// Event Map / Pin Hit Event properties above remain visible in Details; the
	// runtime-only child wiring belongs in BeginPlay(), where every pin exists.
}

void AHapbeatShowcaseZ1BowlingActor::BeginPlay()
{
	Super::BeginPlay();

	// Apply the same non-runtime visual state used by the editor before wiring
	// physics, input and haptics below. This keeps PIE identical to the placed
	// preview without moving the author-owned child-actor slots.
	UpdatePinVisuals();
	BuildEventMap();
	SetUpPins();
	SetUpBall();
	BindInput();
}

void AHapbeatShowcaseZ1BowlingActor::SetUpBall()
{
	Ball = BallSlot != nullptr ? Cast<AHapbeatShowcaseZ1BallActor>(BallSlot->GetChildActor()) : nullptr;
	if (Ball == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ1, Warning,
			TEXT("Z1: the Ball child actor is missing; there is nothing to launch."));
		return;
	}

	// What every pin's HitTrigger filters on. AddUnique because a child actor is
	// rebuilt with its component and this runs once per play session either way.
	// The ball paints itself in its own BeginPlay; the zone only tags it and
	// starts it simulating.
	Ball->Tags.AddUnique(ContactTag);
	Ball->SetPhysicsRunning(true);
}

void AHapbeatShowcaseZ1BowlingActor::BuildEventMap()
{
	ResolvedPinHitEventMap = nullptr;
	ResolvedPinHitEntryId.Invalidate();
	ResolvedPinHitEntryName.Empty();
	PinHitEntryId.Invalidate();

	EventMap = EventMapOverride != nullptr ? ToRawPtr(EventMapOverride) : BuildFallbackEventMap();
	if (EventMap == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ1, Warning, TEXT("Z1: no EventMap available; pin hits will not fire."));
		return;
	}

	// An authored map uses the entry selected in the Details panel. The
	// code-built fallback resolves its equivalent built-in entry by name.
	PinHitEntryId = EventMapOverride != nullptr
		? PinHitEvent.EntryId
		: FHapbeatSampleLibrary::FindEntryId(EventMap, EHapticMode::StreamClip,
			TEXT("showcase-kit"), TEXT("z1_pin_hit"));
	ResolvedPinHitEventMap = EventMap;
	ResolvedPinHitEntryId = PinHitEntryId;
	FHapbeatEventEntry PinHitEntry;
	if (EventMap->FindById(PinHitEntryId, PinHitEntry))
	{
		ResolvedPinHitEntryName = PinHitEntry.GetEventId();
	}
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
	// pin's own VelocityScaled multiplier is folded in on top).
	Fallback->Entries.Add(FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z1_pin_hit"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.25f, PinHitClip, TEXT("z1_pin_hit")));
	return Fallback;
}

void AHapbeatShowcaseZ1BowlingActor::SetUpPins()
{
	PinRestRelativeTransforms.Reset(PinSlots.Num());
	for (UChildActorComponent* Slot : PinSlots)
	{
		if (Slot == nullptr)
		{
			continue;
		}
		// The pose Space returns this pin to, kept RELATIVE to the zone actor so
		// it survives the zone being moved in the editor. The component's own
		// relative transform is the authored rack position.
		PinRestRelativeTransforms.Add(Slot->GetRelativeTransform());

		AHapbeatShowcaseZ1PinActor* Pin = Cast<AHapbeatShowcaseZ1PinActor>(Slot->GetChildActor());
		if (Pin == nullptr)
		{
			UE_LOG(LogHapbeatShowcaseZ1, Warning,
				TEXT("Z1: pin slot '%s' has no child actor; that pin will not fire."), *Slot->GetName());
			continue;
		}
		// Both sides of the filter: the pin CARRIES the tag (so a neighbouring pin
		// knocking it counts as a hit) and its trigger only fires ON that tag (so
		// the lane, the floor and the walls do not).
		Pin->Tags.AddUnique(ContactTag);
		if (Pin->HitTrigger != nullptr)
		{
			// The trigger reads these at fire time, so handing them over here is
			// safe whichever order the child actor's own BeginPlay ran in.
			Pin->HitTrigger->EventMap = EventMap;
			Pin->HitTrigger->EntryId = PinHitEntryId;
			Pin->HitTrigger->TagFilter = ContactTag;
		}
		Pin->HitSound = PinHitSoundAsset;
	}
}

void AHapbeatShowcaseZ1BowlingActor::UpdatePinVisuals()
{
	for (UChildActorComponent* Slot : PinSlots)
	{
		if (AHapbeatShowcaseZ1PinActor* Pin = Slot != nullptr
			? Cast<AHapbeatShowcaseZ1PinActor>(Slot->GetChildActor())
			: nullptr)
		{
			Pin->ApplyPinSize(PinHeightCm, PinDiameterCm, bFlipPinUp);
		}
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

	// Bound once. The switcher pushes and pops this whole component with
	// EnableInput / DisableInput (IHapbeatShowcaseZone::SetZoneSceneActive), so
	// these bindings are live only while this zone is the visible one.
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AHapbeatShowcaseZ1BowlingActor::HandleLaunchKey);
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AHapbeatShowcaseZ1BowlingActor::HandleResetKey);
}

FVector AHapbeatShowcaseZ1BowlingActor::ResolveLaunchDirection() const
{
	if (const AHapbeatShowcaseCharacter* Character =
		Cast<AHapbeatShowcaseCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		// Flattened onto the ground plane so looking up or down changes where the
		// ball goes, never whether it leaves the lane. Unity:
		// Vector3.ProjectOnPlane(_aimReference.forward, Vector3.up).
		FVector Forward = Character->GetViewTransform().GetRotation().GetForwardVector();
		Forward.Z = 0.0f;
		if (!Forward.Normalize())
		{
			// Looking straight up or down: no horizontal component to aim with.
			return GetActorForwardVector();
		}
		return Forward;
	}
	// Zone placed on its own, with the engine's default pawn or none at all.
	return GetActorForwardVector();
}

void AHapbeatShowcaseZ1BowlingActor::HandleLaunchKey()
{
	if (Ball == nullptr)
	{
		return;
	}

	ResetBallToSpawn();
	Ball->LaunchWithVelocity(ResolveLaunchDirection() * LaunchSpeed);
}

void AHapbeatShowcaseZ1BowlingActor::HandleResetKey()
{
	ResetBallToSpawn();
	ResetPinsToRack();
}

void AHapbeatShowcaseZ1BowlingActor::ResetPinsToRack()
{
	for (int32 Index = 0; Index < PinSlots.Num() && Index < PinRestRelativeTransforms.Num(); ++Index)
	{
		UChildActorComponent* Slot = PinSlots[Index];
		AHapbeatShowcaseZ1PinActor* Pin = Slot != nullptr
			? Cast<AHapbeatShowcaseZ1PinActor>(Slot->GetChildActor())
			: nullptr;
		if (Pin != nullptr)
		{
			// Relative -> world at reset time, so a zone moved in the editor
			// still racks its pins in front of itself.
			Pin->ResetToTransform(PinRestRelativeTransforms[Index] * GetActorTransform());
		}
	}
}

void AHapbeatShowcaseZ1BowlingActor::ResetBallToSpawn()
{
	if (Ball == nullptr)
	{
		return;
	}

	// Relative -> world at reset time, so a zone moved in the editor still puts
	// its ball on the mark in front of itself.
	Ball->ResetToTransform(FTransform(FRotator::ZeroRotator,
		GetActorTransform().TransformPosition(BallRestRelativeLocation)));
}

void AHapbeatShowcaseZ1BowlingActor::SetPhysicsRunning(bool bRunning)
{
	if (Ball != nullptr)
	{
		Ball->SetPhysicsRunning(bRunning);
	}
	for (UChildActorComponent* Slot : PinSlots)
	{
		if (AHapbeatShowcaseZ1PinActor* Pin = Slot != nullptr
			? Cast<AHapbeatShowcaseZ1PinActor>(Slot->GetChildActor())
			: nullptr)
		{
			Pin->SetPhysicsRunning(bRunning);
		}
	}
}

void AHapbeatShowcaseZ1BowlingActor::OnZoneActivated()
{
	// A fresh rack every time the zone is entered -- the switcher's counterpart
	// of Unity's ZoneSwitcher re-enabling a zone that was reset when it left.
	ResetBallToSpawn();
	ResetPinsToRack();
	SetPhysicsRunning(true);
}

void AHapbeatShowcaseZ1BowlingActor::OnZoneDeactivated()
{
	// Stop simulating while hidden: collision is off for a hidden zone, so a
	// still-simulating pin would fall straight through the floor and be a long
	// way down by the time you came back.
	SetPhysicsRunning(false);
}

void AHapbeatShowcaseZ1BowlingActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The Showcase switcher draws a shared Slate HUD covering the key guide, the
	// zone's own state and the device footer, so a zone under it prints none of
	// this. ONE EARLY RETURN, not a guard around each line: the per-line form let
	// the other zones' status lines be written outside it, so they showed on top
	// of the shared HUD. Everything below here is HUD-only.
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
		TEXT("Z1 Bowling -- LMB: launch ball / Space: reset (pins fire haptics on their own, velocity-scaled)"),
		FColor::Cyan, HudRefreshIntervalSeconds * 2.0f);
	FHapbeatSampleLibrary::ShowDeviceStatusLine(this, StatusHudLineKey + 1, HudRefreshIntervalSeconds * 2.0f);
}

FText AHapbeatShowcaseZ1BowlingActor::GetZoneLabel() const
{
	return FText::FromString(TEXT("Bowling"));
}

TArray<FHapbeatShowcaseHudCommand> AHapbeatShowcaseZ1BowlingActor::GetHudCommands() const
{
	// Unity HudGuide's Z1 row: "LMB | Launch ball" + "Space | Reset".
	TArray<FHapbeatShowcaseHudCommand> Commands;
	Commands.Add({ FText::FromString(TEXT("LMB")),
		FText::FromString(TEXT("launch ball (pins fire haptics themselves, velocity-scaled)")) });
	Commands.Add({ FText::FromString(TEXT("Space")),
		FText::FromString(TEXT("reset ball and pin rack")) });
	return Commands;
}

FTransform AHapbeatShowcaseZ1BowlingActor::GetPlayerSpawnRelative() const
{
	return FTransform(FRotator::ZeroRotator, PlayerSpawnCm);
}

// =============================================================================
// AHapbeatShowcaseZ1PinActor
// =============================================================================

AHapbeatShowcaseZ1PinActor::AHapbeatShowcaseZ1PinActor()
{
	PrimaryActorTick.bCanEverTick = false;

	PinBody = CreateDefaultSubobject<UCapsuleComponent>(TEXT("PinBody"));
	RootComponent = PinBody;
	PinBody->SetMobility(EComponentMobility::Movable);
	PinBody->SetCapsuleSize(PinCapsuleRadiusCm, PinCapsuleHalfHeightCm);
	PinBody->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	// SetSimulatePhysics() is deferred to BeginPlay: calling it here, before the
	// capsule is registered, is order-dependent (it can log a spurious "no
	// physics body" warning against a not-yet-created BodyInstance). The notify
	// flag, however, must be set BEFORE the HitTrigger component's BeginPlay
	// (which runs during Super::BeginPlay and checks it for its setup hint), so
	// set the raw flag here -- flag-only, needs no registered body.
	PinBody->BodyInstance.bNotifyRigidBodyCollision = true;

	PinMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PinMesh"));
	PinMesh->SetupAttachment(PinBody);
	PinMesh->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		PinMesh->SetStaticMesh(CylinderMesh);
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ImportedPin(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_BowlingPin.SM_BowlingPin"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PinMaterial(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_BowlingPin.MI_BowlingPin"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> StripeMaterial(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_BowlingPinStripe.MI_BowlingPinStripe"));
	if (ImportedPin.Succeeded())
	{
		PinMesh->SetStaticMesh(ImportedPin.Object);
		const TArray<FStaticMaterial>& Slots = ImportedPin.Object->GetStaticMaterials();
		for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			const bool bStripe = Slots[SlotIndex].MaterialSlotName == TEXT("mat8")
				|| Slots[SlotIndex].ImportedMaterialSlotName == TEXT("mat8");
			PinMesh->SetMaterial(SlotIndex,
				bStripe && StripeMaterial.Succeeded() ? StripeMaterial.Object : PinMaterial.Object);
		}
	}
	// No collision on the visual: the capsule is the collider, and a second one
	// inside it would fight the solver.
	PinMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	HitTrigger = CreateDefaultSubobject<UHapbeatCollisionTriggerComponent>(TEXT("HitTrigger"));
	HitTrigger->TriggerEvent = EHapbeatCollisionEvent::Hit;
	// UE has no OnCollisionEnter callback: OnComponentHit is contact-based, so
	// this gate makes one fire per newly-started contact like Unity.
	HitTrigger->bEnterOnly = true;
	HitTrigger->GainMode = EHapbeatGainMode::VelocityScaled;
	// Unity BowlingPin.prefab HapbeatCollisionTrigger: _velocityThreshold=0.01,
	// _maxVelocity=1 (Unity m/s), _gainMode=VelocityScaled, default linear curve.
	// MaxVelocity converts straight across (1 m/s -> 100 cm/s).
	HitTrigger->MaxVelocity = 100.0f;
	// VelocityThreshold does NOT: Unity's 0.01 m/s would be 1 cm/s here, and
	// Chaos keeps reporting contacts between a settled pin and its neighbours at
	// speeds around that, so the rack machine-gunned events at rest. 5 cm/s is
	// above that jitter floor and still far below any real knock.
	HitTrigger->VelocityThreshold = 5.0f;
	// Same reason, from the other side: a pin taking several contacts in one
	// collision (capsule against capsule against lane) should feel like one hit,
	// not three. Unity's mixer absorbed that; the v1 single-stream runtime does
	// not, so the trigger rate-limits itself.
	HitTrigger->Cooldown = 0.1f;
	// EventMap / EntryId are assigned by the owning zone actor (see
	// AHapbeatShowcaseZ1BowlingActor::SetUpPins).
}

void AHapbeatShowcaseZ1PinActor::BeginPlay()
{
	Super::BeginPlay();

	if (PinBody != nullptr)
	{
		// Unity's BowlingPin Rigidbody is 0.5 kg. Defer the override until the
		// component is registered; doing it during native CDO construction asks
		// GEngine for a physical material before GEngine exists.
		PinBody->SetMassOverrideInKg(NAME_None, PinMassKg, /*bNewOverrideMass=*/true);
		PinBody->SetSimulatePhysics(true);
		PinBody->SetNotifyRigidBodyCollision(true); // required for OnComponentHit to fire
	}

	// The impact SFX rides on the TRIGGER's own fire event, not on a second
	// subscription to OnComponentHit. Two subscribers meant two sets of
	// thresholds and two cooldowns filtering the same physics stream, so the
	// click and the haptic drifted apart -- one firing on contacts the other had
	// rejected. One gate now: if the pin was felt, it is heard.
	if (HitTrigger != nullptr)
	{
		HitTrigger->OnFired.AddDynamic(this, &AHapbeatShowcaseZ1PinActor::HandleHapticFired);
	}
}

void AHapbeatShowcaseZ1PinActor::ApplyPinSize(float DesiredHeightCm, float DesiredDiameterCm, bool bFlipUp)
{
	if (PinMesh == nullptr)
	{
		return;
	}

	const UStaticMesh* Mesh = PinMesh->GetStaticMesh();
	if (Mesh == nullptr)
	{
		return;
	}

	// 1. Per-axis, NOT uniform: Unity's instance scale is (0.51, 0.68, 0.51),
	//    i.e. the pin is stretched along its length relative to its girth. A
	//    uniform fit made the rack visibly squat.
	const FVector Scale = FHapbeatSampleLibrary::ComputeAxisFitScale(
		Mesh, FVector(DesiredHeightCm, DesiredDiameterCm, DesiredDiameterCm));
	// 2. Stand it up, whichever local axis the source model ran its length along,
	//    then turn it end over end if asked -- that alignment only picks the AXIS,
	//    not which end of it is the top (see bFlipPinUp).
	FRotator Rotation = FHapbeatSampleLibrary::ComputeLongestAxisToUpRotation(Mesh);
	if (bFlipUp)
	{
		Rotation = FRotator(FQuat(FRotator(0.0f, 0.0f, 180.0f)) * Rotation.Quaternion());
	}
	// 3. Centre the fitted mesh on the capsule's centre, so an off-centre pivot
	//    in the imported model does not leave the pin floating beside its body.
	//    Uses the FINAL rotation, flip included: computed against the unflipped
	//    one it would push the mesh off its body by twice the pivot offset.
	const FVector Offset = -FHapbeatSampleLibrary::ComputeFittedBoundsCentre(Mesh, Scale, Rotation);

	PinMesh->SetRelativeScale3D(Scale);
	PinMesh->SetRelativeRotation(Rotation);
	PinMesh->SetRelativeLocation(Offset);
}

void AHapbeatShowcaseZ1PinActor::ResetToTransform(const FTransform& RestTransform)
{
	// ResetPhysics: teleport AND clear the body's momentum, so a knocked-over pin
	// comes back upright and still instead of continuing its tumble.
	SetActorTransform(RestTransform, false, nullptr, ETeleportType::ResetPhysics);
	if (PinBody != nullptr && PinBody->IsSimulatingPhysics())
	{
		PinBody->SetPhysicsLinearVelocity(FVector::ZeroVector);
		PinBody->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}

void AHapbeatShowcaseZ1PinActor::SetPhysicsRunning(bool bRunning)
{
	if (PinBody != nullptr)
	{
		PinBody->SetSimulatePhysics(bRunning);
	}
}

void AHapbeatShowcaseZ1PinActor::HandleHapticFired(AActor* /*Other*/, float Speed)
{
	if (HitSound == nullptr)
	{
		return;
	}

	// No threshold and no cooldown of its own: the trigger already decided this
	// is a real, new contact (VelocityThreshold, the enter-only contact filter
	// and its Cooldown), and the sound's job is only to say how hard it was.
	// Speed is the closing speed the trigger measured, so a below-range hit
	// still clicks -- quietly, at HitSoundMinVolume.
	const float Alpha = FMath::Clamp(
		(Speed - HitSoundMinSpeed) / (HitSoundMaxSpeed - HitSoundMinSpeed), 0.0f, 1.0f);
	const float Volume = FMath::Lerp(HitSoundMinVolume, 1.0f, Alpha);
	UGameplayStatics::PlaySoundAtLocation(this, HitSound, GetActorLocation(), Volume);
}

// =============================================================================
// AHapbeatShowcaseZ1BallActor
// =============================================================================

AHapbeatShowcaseZ1BallActor::AHapbeatShowcaseZ1BallActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// The SPHERE is the root, not the mesh -- see the header. The ball is spawned
	// by a UChildActorComponent, which overwrites its child actor's root transform
	// with the slot's own (scale 1), so any scale put on the root is thrown away
	// at spawn time: the 0.22 sphere read correctly in the editor and came up as
	// the engine's full 100 cm one in PIE. Size therefore lives on the sphere's
	// RADIUS and on the mesh CHILD's scale, which the slot never touches.
	Body = CreateDefaultSubobject<USphereComponent>(TEXT("Body"));
	RootComponent = Body;
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetSphereRadius(BallDiameterCm * 0.5f);
	Body->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	// The pins listen for Hit events, which are only reported when the bodies
	// involved are set to generate them. Flag-only here -- it needs no registered
	// body, unlike SetSimulatePhysics below.
	Body->BodyInstance.bNotifyRigidBodyCollision = true;
	// SetSimulatePhysics() is deferred to BeginPlay (see AHapbeatShowcaseZ1PinActor's
	// header note): before the component is registered it is order-dependent and
	// can log a spurious "no physics body" warning.

	BallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BallMesh"));
	BallMesh->SetupAttachment(Body);
	BallMesh->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		BallMesh->SetStaticMesh(SphereMesh);
	}
	// A CHILD's relative transform, which the parent UChildActorComponent leaves
	// alone. Engine Sphere is 100 cm across, so this fits it to the collider.
	BallMesh->SetRelativeLocation(FVector::ZeroVector);
	BallMesh->SetRelativeScale3D(FVector(BallDiameterCm / 100.0f));
	// No collision on the visual: the sphere is the collider, and a second one
	// inside it would fight the solver.
	BallMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BallMaterial(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_BowlingBall.MI_BowlingBall"));
	if (BallMaterial.Succeeded())
	{
		for (int32 SlotIndex = 0; SlotIndex < BallMesh->GetNumMaterials(); ++SlotIndex)
		{
			BallMesh->SetMaterial(SlotIndex, BallMaterial.Object);
		}
	}
}

void AHapbeatShowcaseZ1BallActor::BeginPlay()
{
	Super::BeginPlay();

	if (Body != nullptr)
	{
		// Unity's ball is a 4 kg Rigidbody. As with the pins, set mass only after
		// registration so native CDO construction stays engine-independent.
		Body->SetMassOverrideInKg(NAME_None, BallMassKg, /*bNewOverrideMass=*/true);
		Body->SetSimulatePhysics(true);
		Body->SetNotifyRigidBodyCollision(true);
	}

}

void AHapbeatShowcaseZ1BallActor::ResetToTransform(const FTransform& RestTransform)
{
	// ResetPhysics: teleport AND clear the body's momentum, so a ball resting
	// downrange -- or one that rolled off the lane -- comes back clean rather
	// than carrying stale velocity into the next launch.
	SetActorTransform(RestTransform, false, nullptr, ETeleportType::ResetPhysics);
	if (Body != nullptr && Body->IsSimulatingPhysics())
	{
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}

void AHapbeatShowcaseZ1BallActor::LaunchWithVelocity(const FVector& Velocity)
{
	if (Body == nullptr)
	{
		return;
	}
	// bVelChange=true adds straight to velocity (mass-independent), matching
	// Unity BallLauncher.Launch()'s `_ball.linearVelocity = dir * _launchSpeed;`
	// assignment onto a ball the caller has just zeroed.
	Body->AddImpulse(Velocity, NAME_None, /*bVelChange=*/true);
}

void AHapbeatShowcaseZ1BallActor::SetPhysicsRunning(bool bRunning)
{
	if (Body != nullptr)
	{
		Body->SetSimulatePhysics(bRunning);
	}
}
