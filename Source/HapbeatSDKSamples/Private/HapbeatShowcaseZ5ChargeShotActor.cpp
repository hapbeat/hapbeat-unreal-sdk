// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZ5ChargeShotActor.h"

#include "HapbeatClip.h"
#include "HapbeatCollisionTriggerComponent.h"
#include "HapbeatEventMap.h"
#include "HapbeatSampleLibrary.h"
#include "HapbeatStreamPlayback.h"
#include "HapbeatSubsystem.h"

#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h" // EKeys::*
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatShowcaseZ5, Log, All);

namespace
{
	// Muzzle / target layout constants (centimeters, UE's world unit). Purely
	// cosmetic scene layout -- no protocol/gain meaning.
	constexpr float MuzzleForwardOffset = 120.0f;
	constexpr float MuzzleUpOffset = 40.0f;
	constexpr float TargetForwardDistance = 500.0f;
	constexpr float TargetSideSpacing = 150.0f;
}

// =============================================================================
// AHapbeatShowcaseZ5ChargeShotActor
// =============================================================================

AHapbeatShowcaseZ5ChargeShotActor::AHapbeatShowcaseZ5ChargeShotActor()
{
	PrimaryActorTick.bCanEverTick = true;

	StandMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StandMesh"));
	RootComponent = StandMesh;
	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		StandMesh->SetStaticMesh(CubeMesh);
	}
	// A low, wide blaster stand -- purely cosmetic, no gameplay meaning.
	StandMesh->SetRelativeScale3D(FVector(1.2f, 0.8f, 1.0f));
	StandMesh->SetMobility(EComponentMobility::Static);

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

void AHapbeatShowcaseZ5ChargeShotActor::BeginPlay()
{
	Super::BeginPlay();

	if (RootComponent != nullptr)
	{
		RootComponent->SetRelativeLocation(FootprintOffset);
	}

	BuildEventMap();
	SpawnTargets();
	BindInput();
}

void AHapbeatShowcaseZ5ChargeShotActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ShotDelayTimer);
	}
	if (UHapbeatStreamPlayback* Pb = LoopPlayback.Get())
	{
		if (Pb->IsActive())
		{
			Pb->Stop();
		}
	}
	LoopPlayback.Reset();
	Super::EndPlay(EndPlayReason);
}

void AHapbeatShowcaseZ5ChargeShotActor::BuildEventMap()
{
	EventMap = EventMapOverride != nullptr ? ToRawPtr(EventMapOverride) : BuildFallbackEventMap();
	if (EventMap == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ5, Warning, TEXT("Z5: no EventMap available; the blaster's haptics will not fire."));
		return;
	}

	// Look the ids up by event name. The fallback map below authors the same
	// categories / names / modes, so both paths go through this one resolution
	// step instead of duplicating the wiring.
	ChargeLoopEntryId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z5_charge_loop"));
	ChargeThresholdEntryId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z5_charge_thd"));
	ShotLightEntryId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z5_shot_light"));
	ShotHeavyEntryId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z5_shot_heavy"));
	TarHitLightEntryId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z5_tar_hit_light"));
	TarHitHeavyEntryId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z5_tar_hit_heavy"));
}

UHapbeatEventMap* AHapbeatShowcaseZ5ChargeShotActor::BuildFallbackEventMap()
{
	UHapbeatEventMap* Fallback = NewObject<UHapbeatEventMap>(this);
	LoadedClips.Reset(6);

	// Loads the WAV, keeps it alive in LoadedClips (see the header's GC note),
	// and returns the pointer for MakeEntry.
	auto LoadAndKeep = [this](const TCHAR* FileName) -> UHapbeatClip*
	{
		UHapbeatClip* Clip = FHapbeatSampleLibrary::LoadSampleClip(this,
			FString::Printf(TEXT("Showcase/Kit/showcase-kit/stream-clips/%s"), FileName));
		LoadedClips.Add(Clip);
		return Clip;
	};

	// Intensities hardcoded from Content/HapbeatSamples/Showcase/Kit/showcase-kit/
	// showcase-kit-manifest.json (schema 2.0.0) stream_events section, matching
	// Samples~/Showcase/EventMaps/ShowcaseEventMap.md verbatim (all gain 1.00):
	//   z5_charge_loop    intensity 0.14 (loop)
	//   z5_charge_thd     intensity 0.30 (one-shot, threshold cross)
	//   z5_shot_light     intensity 0.30 (one-shot)
	//   z5_shot_heavy     intensity 0.40 (one-shot)
	//   z5_tar_hit_light  intensity 0.45 (one-shot)
	//   z5_tar_hit_heavy  intensity 0.40 (one-shot)
	const FHapbeatEventEntry ChargeLoopEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z5_charge_loop"),
		1.0f, /*bLoop=*/true, 0.14f, LoadAndKeep(TEXT("z5_charge_loop.wav")), TEXT("z5_charge_loop"));
	const FHapbeatEventEntry ChargeThresholdEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z5_charge_thd"),
		1.0f, /*bLoop=*/false, 0.30f, LoadAndKeep(TEXT("z5_charge_thd.wav")), TEXT("z5_charge_thd"));
	const FHapbeatEventEntry ShotLightEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z5_shot_light"),
		1.0f, /*bLoop=*/false, 0.30f, LoadAndKeep(TEXT("z5_shot_light.wav")), TEXT("z5_shot_light"));
	const FHapbeatEventEntry ShotHeavyEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z5_shot_heavy"),
		1.0f, /*bLoop=*/false, 0.40f, LoadAndKeep(TEXT("z5_shot_heavy.wav")), TEXT("z5_shot_heavy"));
	const FHapbeatEventEntry TarHitLightEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z5_tar_hit_light"),
		1.0f, /*bLoop=*/false, 0.45f, LoadAndKeep(TEXT("z5_tar_hit_light.wav")), TEXT("z5_tar_hit_light"));
	const FHapbeatEventEntry TarHitHeavyEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z5_tar_hit_heavy"),
		1.0f, /*bLoop=*/false, 0.40f, LoadAndKeep(TEXT("z5_tar_hit_heavy.wav")), TEXT("z5_tar_hit_heavy"));

	Fallback->Entries.Reset(6);
	Fallback->Entries.Add(ChargeLoopEntry);
	Fallback->Entries.Add(ChargeThresholdEntry);
	Fallback->Entries.Add(ShotLightEntry);
	Fallback->Entries.Add(ShotHeavyEntry);
	Fallback->Entries.Add(TarHitLightEntry);
	Fallback->Entries.Add(TarHitHeavyEntry);
	return Fallback;
}

void AHapbeatShowcaseZ5ChargeShotActor::SpawnTargets()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const FVector Forward = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	const FVector Base = GetActorLocation() + Forward * TargetForwardDistance;
	const FRotator SpawnRotation = GetActorRotation();

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	TargetLight = World->SpawnActor<AHapbeatShowcaseZ5TargetActor>(
		Base - Right * TargetSideSpacing, SpawnRotation, Params);
	TargetHeavy = World->SpawnActor<AHapbeatShowcaseZ5TargetActor>(
		Base + Right * TargetSideSpacing, SpawnRotation, Params);

	if (TargetLight != nullptr && TargetLight->HitTrigger != nullptr)
	{
		TargetLight->HitTrigger->EventMap = EventMap;
		TargetLight->HitTrigger->EntryId = TarHitLightEntryId;
		TargetLight->HitTrigger->TagFilter = FName(TEXT("ProjectileLight"));
	}
	else
	{
		UE_LOG(LogHapbeatShowcaseZ5, Warning, TEXT("Z5: failed to spawn/wire TargetLight."));
	}

	if (TargetHeavy != nullptr && TargetHeavy->HitTrigger != nullptr)
	{
		TargetHeavy->HitTrigger->EventMap = EventMap;
		TargetHeavy->HitTrigger->EntryId = TarHitHeavyEntryId;
		TargetHeavy->HitTrigger->TagFilter = FName(TEXT("ProjectileHeavy"));
	}
	else
	{
		UE_LOG(LogHapbeatShowcaseZ5, Warning, TEXT("Z5: failed to spawn/wire TargetHeavy."));
	}
}

void AHapbeatShowcaseZ5ChargeShotActor::BindInput()
{
	APlayerController* PC = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ5, Warning,
			TEXT("AHapbeatShowcaseZ5ChargeShotActor: no PlayerController found; input not bound."));
		return;
	}

	EnableInput(PC);
	if (InputComponent == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ5, Warning,
			TEXT("AHapbeatShowcaseZ5ChargeShotActor: EnableInput did not create an InputComponent; input not bound."));
		return;
	}

	InputComponent->BindKey(EKeys::V, IE_Pressed, this, &AHapbeatShowcaseZ5ChargeShotActor::HandleChargeBegin);
	InputComponent->BindKey(EKeys::V, IE_Released, this, &AHapbeatShowcaseZ5ChargeShotActor::HandleChargeRelease);
}

UHapbeatSubsystem* AHapbeatShowcaseZ5ChargeShotActor::ResolveSubsystem() const
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		return GameInstance->GetSubsystem<UHapbeatSubsystem>();
	}
	return nullptr;
}

void AHapbeatShowcaseZ5ChargeShotActor::HandleChargeBegin()
{
	if (bCharging)
	{
		return; // already charging (defensive against key-repeat)
	}
	bCharging = true;
	bThresholdReached = false;
	LastChargeT = 0.0f;

	const UWorld* World = GetWorld();
	ChargeStartSeconds = World != nullptr ? World->GetTimeSeconds() : 0.0;

	UHapbeatSubsystem* Subsystem = ResolveSubsystem();
	if (Subsystem == nullptr || EventMap == nullptr)
	{
		return;
	}

	FHapbeatEventEntry Entry;
	if (!EventMap->FindById(ChargeLoopEntryId, Entry))
	{
		UE_LOG(LogHapbeatShowcaseZ5, Warning, TEXT("Z5: charge_loop entry missing from EventMap."));
		return;
	}
	UHapbeatClip* Clip = Entry.StreamClip.LoadSynchronous();
	if (Clip == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ5, Warning, TEXT("Z5: charge_loop clip failed to load."));
		return;
	}

	// chargeT = 0 at press time -> curve(0) = 0 -> silent, race-free start
	// (matches Unity's `initialMod = _gainCurve.Evaluate(0f)`).
	const float InitialMod = FMath::SmoothStep(0.0f, 1.0f, 0.0f);
	// Through PlayEntry (the single runtime play path) rather than StreamClip:
	// baseline (entry gain x manifest intensity), target and the authored loop
	// flag all come off the entry, and the haptic delay applies like anywhere
	// else. InitialMod stays the initial modulator, which Tick() then replaces
	// via the handle's ApplyGainModulation.
	LoopPlayback = Subsystem->PlayEntry(EventMap, ChargeLoopEntryId, InitialMod);
}

void AHapbeatShowcaseZ5ChargeShotActor::HandleChargeRelease()
{
	if (!bCharging)
	{
		return;
	}
	bCharging = false;
	const float ChargeT = LastChargeT;

	UHapbeatSubsystem* Subsystem = ResolveSubsystem();

	// Stop the loop handle, then force an immediate device ring-buffer flush --
	// exact parity with Unity ChargeShooter.Release():
	//   _loopPlayback.Stop(); _loopPlayback = null; HapbeatManager.Instance.StopStreamWithFlush();
	if (UHapbeatStreamPlayback* Pb = LoopPlayback.Get())
	{
		if (Pb->IsActive())
		{
			Pb->Stop();
		}
	}
	LoopPlayback.Reset();
	if (Subsystem != nullptr)
	{
		Subsystem->StopStreamWithFlush(TEXT(""));
	}

	const bool bHeavy = ChargeT >= HeavyThreshold;
	bPendingHeavyShot = bHeavy;

	// Shot delay after loop: mirrors Unity's _shotDelayAfterLoop (default 0.05s)
	// so the shot's STREAM_BEGIN/DATA doesn't collide with the loop-stop flush
	// burst on the device (see class doc + FireOneShotEntry).
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ShotDelayTimer);
		if (ShotDelayAfterLoop > 0.0f)
		{
			World->GetTimerManager().SetTimer(ShotDelayTimer, this,
				&AHapbeatShowcaseZ5ChargeShotActor::FireShotAfterDelay, ShotDelayAfterLoop, /*bLoop=*/false);
		}
		else
		{
			FireShotAfterDelay();
		}
	}

	SpawnProjectile(ChargeT, bHeavy);
}

void AHapbeatShowcaseZ5ChargeShotActor::FireShotAfterDelay()
{
	FireOneShotEntry(bPendingHeavyShot ? ShotHeavyEntryId : ShotLightEntryId);
}

void AHapbeatShowcaseZ5ChargeShotActor::FireOneShotEntry(const FGuid& EntryId)
{
	UHapbeatSubsystem* Subsystem = ResolveSubsystem();
	if (Subsystem == nullptr || EventMap == nullptr)
	{
		return;
	}

	FHapbeatEventEntry Entry;
	if (!EventMap->FindById(EntryId, Entry))
	{
		UE_LOG(LogHapbeatShowcaseZ5, Warning, TEXT("Z5: one-shot entry id '%s' not found in EventMap."), *EntryId.ToString());
		return;
	}
	UHapbeatClip* Clip = Entry.StreamClip.LoadSynchronous();
	if (Clip == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ5, Warning, TEXT("Z5: one-shot entry '%s' has no clip loaded."), *Entry.EventName);
		return;
	}

	// Fixed modulator (1.0): mirrors Unity's default _chargeShotModulator = 1.0 /
	// _chargeThresholdModulator = 1.0 (_shotFollowsChargeT = false), i.e. these
	// one-shots fire at plain entry.GetEffectiveGain(), not scaled by chargeT.
	// This call REPLACES whatever the subsystem is currently streaming (v1
	// single-session model) -- by the time this fires, the charge loop has
	// already been stopped+flushed, so there is nothing to steal from.
	// bForceNonLoop states the one-shot intent at the call site: these entries
	// are authored non-looping, and a shot must never leave a loop running even
	// if someone flips that flag while tuning.
	Subsystem->PlayEntry(EventMap, EntryId, /*GainMultiplier=*/1.0f, /*bForceNonLoop=*/true);
}

void AHapbeatShowcaseZ5ChargeShotActor::SpawnProjectile(float ChargeT, bool bHeavy)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const FVector MuzzleLocation = GetActorLocation()
		+ GetActorForwardVector() * MuzzleForwardOffset
		+ FVector(0.0f, 0.0f, MuzzleUpOffset);
	const float Speed = MaxLaunchSpeed * FMath::Lerp(0.3f, 1.0f, ChargeT);
	const float Scale = FMath::Lerp(MinChargeScale, MaxChargeScale, ChargeT);

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AHapbeatShowcaseZ5ProjectileActor* Projectile = World->SpawnActor<AHapbeatShowcaseZ5ProjectileActor>(
		MuzzleLocation, GetActorRotation(), Params);
	if (Projectile != nullptr)
	{
		Projectile->Configure(GetActorForwardVector() * Speed, bHeavy, Scale);
	}
}

void AHapbeatShowcaseZ5ChargeShotActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bCharging)
	{
		const UWorld* World = GetWorld();
		const double Now = World != nullptr ? World->GetTimeSeconds() : ChargeStartSeconds;
		const float T = MaxChargeSeconds > KINDA_SMALL_NUMBER
			? FMath::Clamp(static_cast<float>(Now - ChargeStartSeconds) / MaxChargeSeconds, 0.0f, 1.0f)
			: 1.0f;
		LastChargeT = T;

		if (UHapbeatStreamPlayback* Pb = LoopPlayback.Get())
		{
			if (Pb->IsActive())
			{
				// Unity: AnimationCurve.EaseInOut(0,0,1,1) is a cubic Hermite with
				// zero in/out tangents at both keys, i.e. exactly the standard
				// smoothstep polynomial 3t^2 - 2t^3 over [0,1] -- FMath::SmoothStep
				// reproduces it byte-for-byte on this domain.
				Pb->ApplyGainModulation(FMath::SmoothStep(0.0f, 1.0f, T));
			}
		}

		if (!bThresholdReached && T >= HeavyThreshold)
		{
			bThresholdReached = true;
			// v1 single-active-stream REPLACE model: firing the threshold one-shot
			// StreamClip here would permanently kill the still-running charge loop
			// (Unity's runtime mixes both). Prefer the continuous charge rumble:
			// skip the ping while the loop is active. The threshold is still felt
			// on release (heavy shot). Documented v1 limitation.
			const UHapbeatStreamPlayback* LoopPb = LoopPlayback.Get();
			if (LoopPb == nullptr || !LoopPb->IsActive())
			{
				FireOneShotEntry(ChargeThresholdEntryId);
			}
		}
	}

	HudRefreshTimer -= DeltaSeconds;
	if (HudRefreshTimer > 0.0f)
	{
		return;
	}
	HudRefreshTimer = HudRefreshIntervalSeconds;

	FHapbeatSampleLibrary::ShowHudLine(KeyGuideHudLineKey,
		TEXT("Z5 Target Range -- Hold V to charge, release to fire"),
		FColor::Cyan, HudRefreshIntervalSeconds * 2.0f);

	FString StatusSuffix = TEXT("");
	FColor StatusColor = FColor::Silver;
	if (bCharging)
	{
		const bool bHeavyNow = LastChargeT >= HeavyThreshold;
		StatusSuffix = bHeavyNow ? TEXT(" [HEAVY]") : TEXT(" [light]");
		StatusColor = bHeavyNow ? FColor::Red : FColor::Yellow;
	}
	FHapbeatSampleLibrary::ShowHudLine(StatusHudLineKey,
		FString::Printf(TEXT("Charge=%.2f%s"), LastChargeT, *StatusSuffix),
		StatusColor, HudRefreshIntervalSeconds * 2.0f);
	FHapbeatSampleLibrary::ShowDeviceStatusLine(this, StatusHudLineKey + 1, HudRefreshIntervalSeconds * 2.0f);
}

// =============================================================================
// AHapbeatShowcaseZ5TargetActor
// =============================================================================

AHapbeatShowcaseZ5TargetActor::AHapbeatShowcaseZ5TargetActor()
{
	PrimaryActorTick.bCanEverTick = false;

	TargetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TargetMesh"));
	RootComponent = TargetMesh;
	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		TargetMesh->SetStaticMesh(CubeMesh);
	}
	// A flat board -- purely cosmetic, no gameplay meaning.
	TargetMesh->SetRelativeScale3D(FVector(0.8f, 0.2f, 0.8f));
	TargetMesh->SetMobility(EComponentMobility::Static);

	// QueryOnly + Overlap-all + GenerateOverlapEvents: no physics simulation
	// needed on either side (the target never moves; the projectile sweeps
	// through it via a swept AddActorWorldOffset). Mirrors the design doc's
	// Z1 "BeginOverlap + query-only" faithfulness-ledger choice.
	TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TargetMesh->SetCollisionObjectType(ECC_WorldStatic);
	TargetMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
	TargetMesh->SetGenerateOverlapEvents(true);

	HitTrigger = CreateDefaultSubobject<UHapbeatCollisionTriggerComponent>(TEXT("HitTrigger"));
	HitTrigger->TriggerEvent = EHapbeatCollisionEvent::BeginOverlap;
	HitTrigger->GainMode = EHapbeatGainMode::Fixed;
	// TagFilter / EventMap / EntryId are assigned by the owning zone actor right
	// after SpawnActor (see AHapbeatShowcaseZ5ChargeShotActor::SpawnTargets).
}

// =============================================================================
// AHapbeatShowcaseZ5ProjectileActor
// =============================================================================

AHapbeatShowcaseZ5ProjectileActor::AHapbeatShowcaseZ5ProjectileActor()
{
	PrimaryActorTick.bCanEverTick = true;

	ProjectileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));
	RootComponent = ProjectileMesh;
	if (UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		ProjectileMesh->SetStaticMesh(SphereMesh);
	}
	ProjectileMesh->SetRelativeScale3D(FVector(0.3f));
	ProjectileMesh->SetMobility(EComponentMobility::Movable);

	// QueryOnly + Overlap-all + GenerateOverlapEvents: matches the target's
	// collision setup so a swept move (AddActorWorldOffset(.., bSweep=true) in
	// Tick) generates BeginOverlap without either side simulating physics.
	ProjectileMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ProjectileMesh->SetCollisionObjectType(ECC_WorldDynamic);
	ProjectileMesh->SetCollisionResponseToAllChannels(ECR_Overlap);
	ProjectileMesh->SetGenerateOverlapEvents(true);
}

void AHapbeatShowcaseZ5ProjectileActor::BeginPlay()
{
	Super::BeginPlay();
	// Mirrors Unity's `Destroy(projectile.gameObject, 4f)` -- a fixed lifespan
	// regardless of whether it hit a target. Does not depend on Configure()'s
	// Velocity/tag (safe even though BeginPlay runs before Configure() is
	// called by the spawner -- see the header's Configure doc).
	SetLifeSpan(4.0f);
}

void AHapbeatShowcaseZ5ProjectileActor::Configure(const FVector& InVelocity, bool bInHeavy, float InScale)
{
	Velocity = InVelocity;
	Tags.Add(bInHeavy ? FName(TEXT("ProjectileHeavy")) : FName(TEXT("ProjectileLight")));
	if (ProjectileMesh != nullptr)
	{
		ProjectileMesh->SetWorldScale3D(FVector(InScale));
	}
}

void AHapbeatShowcaseZ5ProjectileActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Swept move so BeginOverlap fires against the (non-simulating) target
	// boards along the path, not just at the final resting position.
	AddActorWorldOffset(Velocity * DeltaSeconds, /*bSweep=*/true);
}
