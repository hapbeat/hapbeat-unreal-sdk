// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZ1BowlingActor.h"

#include "HapbeatClip.h"
#include "HapbeatCollisionTriggerComponent.h"
#include "HapbeatEventMap.h"
#include "HapbeatSampleLibrary.h"
#include "HapbeatShowcaseCharacter.h"

#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
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

	ApplyShowcaseAssets();
	BuildEventMap();
	SpawnPinRack();
	BindInput();
}

void AHapbeatShowcaseZ1BowlingActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (AHapbeatShowcaseZ1PinActor* Pin : PinActors)
	{
		if (IsValid(Pin))
		{
			Pin->Destroy();
		}
	}
	PinActors.Reset();
	PinRestTransforms.Reset();

	Super::EndPlay(EndPlayReason);
}

void AHapbeatShowcaseZ1BowlingActor::ApplyShowcaseAssets()
{
	// The lane and the ball keep their engine primitive shapes (a box and a
	// sphere are already the right forms) and only take the imported materials;
	// the pins swap mesh AND material, which happens per pin in SpawnPinRack.
	if (UMaterialInterface* LaneMaterial =
		FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("MI_BowlingLane")))
	{
		if (LaneMesh != nullptr)
		{
			LaneMesh->SetMaterial(0, LaneMaterial);
		}
	}
	if (UMaterialInterface* BallMaterial =
		FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("MI_BowlingBall")))
	{
		if (BallMesh != nullptr)
		{
			BallMesh->SetMaterial(0, BallMaterial);
		}
	}
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
	// Optional imported art, resolved once for the whole rack.
	UStaticMesh* ImportedPin = FHapbeatSampleLibrary::LoadShowcaseAsset<UStaticMesh>(
		TEXT("Meshes"), TEXT("SM_BowlingPin"));
	UMaterialInterface* PinMaterial = FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(
		TEXT("Materials"), TEXT("MI_DefaultMaterial"));
	USoundBase* PinHitSound = FHapbeatSampleLibrary::LoadShowcaseAsset<USoundBase>(
		TEXT("Sounds"), TEXT("S_z1_pin_hit"));

	PinActors.Reset(RackLayout.Num());
	PinRestTransforms.Reset(RackLayout.Num());
	for (const FVector2D& LocalXY : RackLayout)
	{
		// DEFERRED spawn, so the mesh swap and the trigger wiring both land
		// BEFORE the pin's BeginPlay: BeginPlay is where the pin starts simulating
		// physics (swapping the mesh afterwards would rebuild the body it just
		// created) and where its HitTrigger reads the EventMap it was given.
		AHapbeatShowcaseZ1PinActor* Pin = World->SpawnActorDeferred<AHapbeatShowcaseZ1PinActor>(
			AHapbeatShowcaseZ1PinActor::StaticClass(),
			FTransform(GetActorRotation(), GetActorLocation()),
			/*Owner=*/this, /*Instigator=*/nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Pin == nullptr || Pin->HitTrigger == nullptr)
		{
			UE_LOG(LogHapbeatShowcaseZ1, Warning, TEXT("Z1: failed to spawn/wire a pin."));
			continue;
		}
		Pin->HitTrigger->EventMap = EventMap;
		Pin->HitTrigger->EntryId = PinHitEntryId;
		Pin->HitSound = PinHitSound;

		// The mesh swap decides how high the actor origin has to sit for the pin
		// to stand on the floor, so it happens BEFORE the final placement.
		const float PinZ = Pin->ApplyPinMesh(ImportedPin, PinMaterial, PinMeshHeight);
		const FTransform RackPose(GetActorRotation(),
			GetActorLocation() + GetActorRotation().RotateVector(FVector(LocalXY.X, LocalXY.Y, PinZ)));
		Pin->FinishSpawning(RackPose);

		PinActors.Add(Pin);
		PinRestTransforms.Add(RackPose);
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
	if (BallMesh == nullptr)
	{
		return;
	}

	ResetBallToSpawn();
	// AddImpulse with bVelChange=true adds directly to velocity (mass-independent),
	// matching Unity BallLauncher.Launch()'s direct `_ball.linearVelocity = dir *
	// _launchSpeed;` assignment onto a ball that was just zeroed by ResetBallToSpawn.
	BallMesh->AddImpulse(ResolveLaunchDirection() * LaunchSpeed, NAME_None, /*bVelChange=*/true);
}

void AHapbeatShowcaseZ1BowlingActor::HandleResetKey()
{
	ResetBallToSpawn();
	ResetPinsToRack();
}

void AHapbeatShowcaseZ1BowlingActor::ResetPinsToRack()
{
	for (int32 Index = 0; Index < PinActors.Num() && Index < PinRestTransforms.Num(); ++Index)
	{
		if (AHapbeatShowcaseZ1PinActor* Pin = PinActors[Index])
		{
			Pin->ResetToTransform(PinRestTransforms[Index]);
		}
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
			TEXT("Z1 Bowling -- LMB: launch ball / Space: reset (pins fire haptics on their own, velocity-scaled)"),
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
		// The haptic side is already handled by HitTrigger; this is only the SFX,
		// which Unity keeps in its own CollisionAudio component for the same reason.
		PinMesh->OnComponentHit.AddDynamic(this, &AHapbeatShowcaseZ1PinActor::HandlePinHit);
	}
}

float AHapbeatShowcaseZ1PinActor::ApplyPinMesh(UStaticMesh* Mesh, UMaterialInterface* Material, float DesiredHeight)
{
	if (PinMesh == nullptr)
	{
		return 0.0f;
	}

	if (Mesh != nullptr)
	{
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const float SourceHeight = FMath::Max(Bounds.BoxExtent.Z * 2.0f, KINDA_SMALL_NUMBER);
		// Uniform, so the pin keeps its proportions -- the imported mesh is already
		// pin-shaped, only its absolute size differs from Unity's rack.
		const float Scale = DesiredHeight / SourceHeight;
		PinMesh->SetStaticMesh(Mesh);
		PinMesh->SetRelativeScale3D(FVector(Scale));
		if (Material != nullptr)
		{
			PinMesh->SetMaterial(0, Material);
		}
		return -(Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale;
	}

	// Fallback cylinder: same computation against whatever mesh is on it, which
	// keeps one rule for both paths instead of a hardcoded half-height.
	if (const UStaticMesh* Current = PinMesh->GetStaticMesh())
	{
		const FBoxSphereBounds Bounds = Current->GetBounds();
		const FVector Scale3D = PinMesh->GetRelativeScale3D();
		return -(Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale3D.Z;
	}
	return 0.0f;
}

void AHapbeatShowcaseZ1PinActor::ResetToTransform(const FTransform& RestTransform)
{
	// ResetPhysics: teleport AND clear the body's momentum, so a knocked-over pin
	// comes back upright and still instead of continuing its tumble.
	SetActorTransform(RestTransform, false, nullptr, ETeleportType::ResetPhysics);
	if (PinMesh != nullptr && PinMesh->IsSimulatingPhysics())
	{
		PinMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		PinMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}

void AHapbeatShowcaseZ1PinActor::HandlePinHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	if (HitSound == nullptr || HitComponent == nullptr)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float Now = World != nullptr ? World->GetTimeSeconds() : 0.0f;
	if (Now - LastHitSoundTime < HitSoundCooldownSeconds)
	{
		return;
	}

	// Closing speed between the two bodies -- UE's counterpart of Unity's
	// Collision.relativeVelocity. UE reports the hit after the solver has already
	// resolved it, so this reads slightly lower than Unity's pre-impact value;
	// the velocity-to-volume curve is a feel mapping, so that is acceptable.
	FVector RelativeVelocity = HitComponent->GetPhysicsLinearVelocity();
	if (OtherComponent != nullptr && OtherComponent->IsSimulatingPhysics())
	{
		RelativeVelocity -= OtherComponent->GetPhysicsLinearVelocity();
	}
	const float Speed = RelativeVelocity.Size();
	if (Speed < HitSoundMinSpeed)
	{
		return;
	}

	const float Alpha = FMath::Clamp(
		(Speed - HitSoundMinSpeed) / (HitSoundMaxSpeed - HitSoundMinSpeed), 0.0f, 1.0f);
	const float Volume = FMath::Lerp(HitSoundMinVolume, 1.0f, Alpha);
	UGameplayStatics::PlaySoundAtLocation(this, HitSound, GetActorLocation(), Volume);
	LastHitSoundTime = Now;
}
