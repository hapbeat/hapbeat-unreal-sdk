// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZ5ChargeShotActor.h"

#include "HapbeatClip.h"
#include "HapbeatCollisionTriggerComponent.h"
#include "HapbeatEventMap.h"
#include "HapbeatSampleLibrary.h"
#include "HapbeatStreamPlayback.h"
#include "HapbeatShowcaseCharacter.h"
#include "HapbeatSubsystem.h"

#include "Components/AudioComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h" // EKeys::*
#include "Kismet/GameplayStatics.h" // GetPlayerPawn / PlaySound2D / SpawnSoundAttached
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatShowcaseZ5, Log, All);

namespace
{
	// Muzzle / target layout constants (centimeters, UE's world unit). Purely
	// cosmetic scene layout -- no protocol/gain meaning.
	constexpr float MuzzleForwardOffset = 120.0f;
	constexpr float MuzzleUpOffset = 40.0f;
	constexpr float TargetForwardDistance = 500.0f;
	constexpr float TargetSideSpacing = 150.0f;

	/** Finished sizes of the imported props, longest axis, cm (see the Showcase asset ledger). */
	constexpr float BulletLengthCm = 25.0f;   // SM_BulletFoam, 15 x 15 x 25
	constexpr float MissileLengthCm = 129.0f; // SM_Missile, 129 x 53 x 47
	constexpr float TargetBoardSizeCm = 180.0f; // SM_TargetLarge, 53 x 180 x 180

	/** Unity ChargeShooter._chargeBarColorLow / _chargeBarColorHigh. */
	const FLinearColor ChargeBarLowColor(0.3f, 0.6f, 1.0f, 1.0f);
	const FLinearColor ChargeBarHighColor(1.0f, 0.3f, 0.2f, 1.0f);
}

// =============================================================================
// AHapbeatShowcaseZ5ChargeShotActor
// =============================================================================

AHapbeatShowcaseZ5ChargeShotActor::AHapbeatShowcaseZ5ChargeShotActor()
{
	PrimaryActorTick.bCanEverTick = true;

	StandMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StandMesh"));
	RootComponent = StandMesh;
	// Movable: the zone is spawned at runtime (Hapbeat Showcase zone switching) and this root
	// is moved by FootprintOffset in BeginPlay. Lighting is dynamic.
	// Mobility is set before the mesh assignment so SetStaticMesh never runs on a Static component.
	StandMesh->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		StandMesh->SetStaticMesh(CubeMesh);
	}
	// A low, wide blaster stand -- purely cosmetic, no gameplay meaning.
	StandMesh->SetRelativeScale3D(FVector(1.2f, 0.8f, 1.0f));

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

	LoadShowcaseAssets();
	BuildEventMap();
	SpawnTargets();
	BindInput();
	CreateChargeBar();
}

void AHapbeatShowcaseZ5ChargeShotActor::LoadShowcaseAssets()
{
	ProjectileMeshLight = FHapbeatSampleLibrary::LoadShowcaseAsset<UStaticMesh>(TEXT("Meshes"), TEXT("SM_BulletFoam"));
	ProjectileMeshHeavy = FHapbeatSampleLibrary::LoadShowcaseAsset<UStaticMesh>(TEXT("Meshes"), TEXT("SM_Missile"));
	ChargeLoopSound = FHapbeatSampleLibrary::LoadShowcaseAsset<USoundBase>(TEXT("Sounds"), TEXT("S_z5_charge_loop"));
	ShotLightSound = FHapbeatSampleLibrary::LoadShowcaseAsset<USoundBase>(TEXT("Sounds"), TEXT("S_z5_shot_light"));
	ShotHeavySound = FHapbeatSampleLibrary::LoadShowcaseAsset<USoundBase>(TEXT("Sounds"), TEXT("S_z5_shot_heavy"));
}

void AHapbeatShowcaseZ5ChargeShotActor::TryDeferredMount()
{
	if (bMountAttempted)
	{
		return;
	}
	if (UGameplayStatics::GetPlayerPawn(this, 0) == nullptr)
	{
		return; // nothing possessed yet -- try again next frame
	}
	bMountAttempted = true;
	MountBlasterOnCharacter();
}

void AHapbeatShowcaseZ5ChargeShotActor::MountBlasterOnCharacter()
{
	AHapbeatShowcaseCharacter* Character =
		Cast<AHapbeatShowcaseCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	UStaticMesh* BlasterMesh = FHapbeatSampleLibrary::LoadShowcaseAsset<UStaticMesh>(
		TEXT("Meshes"), TEXT("SM_BlasterG"));
	if (Character == nullptr || BlasterMesh == nullptr)
	{
		return; // bare level or no imported art: shots come from the stand instead
	}

	FTransform MountPose = AHapbeatShowcaseCharacter::GetDefaultHandMountRelativeTransform();
	MountPose.SetRotation((MountPose.Rotator() + BlasterMountExtraRotation).Quaternion());
	// SM_BlasterG is imported at its authored size (x1 in the asset ledger), so
	// unlike Z3's rod there is no scale correction to apply here.
	Character->MountItem(BlasterMesh, MountPose,
		FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("MI_DefaultMaterial")));
	MountedCharacter = Character;
}

FTransform AHapbeatShowcaseZ5ChargeShotActor::GetMuzzleTransform() const
{
	if (const AHapbeatShowcaseCharacter* Character = MountedCharacter.Get())
	{
		// Unity ChargeShooter falls back to Camera.main when no muzzle transform
		// is wired, and its muzzle sits on the camera-mounted blaster anyway --
		// so the view transform is the honest UE equivalent, pushed clear of the
		// near plane so the projectile is visible as it leaves.
		FTransform View = Character->GetViewTransform();
		View.SetLocation(View.GetLocation() + View.GetRotation().GetForwardVector() * 60.0f);
		return View;
	}
	return FTransform(GetActorRotation(),
		GetActorLocation() + GetActorForwardVector() * MuzzleForwardOffset + FVector(0.0f, 0.0f, MuzzleUpOffset));
}

void AHapbeatShowcaseZ5ChargeShotActor::CreateChargeBar()
{
	const UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World != nullptr ? World->GetGameViewport() : nullptr;
	if (Viewport == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ5, Warning, TEXT("Z5: no game viewport; the charge bar was not created."));
		return;
	}

	// Unity ChargeShooter drives a UI Slider + its Fill image colour from
	// chargeT; the same two facts (fraction, and which side of the heavy
	// threshold it is on) drive this bar. Lambdas capture `this` and the widget
	// is removed in EndPlay, so they cannot outlive the actor.
	ChargeBarWidget =
		SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 64.0f))
		[
			SNew(SBorder)
			.Padding(FMargin(12.0f, 8.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						return FText::FromString(FString::Printf(TEXT("Charge  %.0f%%"), LastChargeT * 100.0f));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SBox).WidthOverride(320.0f).HeightOverride(16.0f)
					[
						SNew(SProgressBar)
						.Percent_Lambda([this]() { return LastChargeT; })
						.FillColorAndOpacity_Lambda([this]()
						{
							return FSlateColor(LastChargeT >= HeavyThreshold ? ChargeBarHighColor : ChargeBarLowColor);
						})
					]
				]
			]
		];

	Viewport->AddViewportWidgetContent(ChargeBarWidget.ToSharedRef(), /*ZOrder=*/1);
}

void AHapbeatShowcaseZ5ChargeShotActor::DestroyChargeBar()
{
	if (!ChargeBarWidget.IsValid())
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (UGameViewportClient* Viewport = World != nullptr ? World->GetGameViewport() : nullptr)
	{
		Viewport->RemoveViewportWidgetContent(ChargeBarWidget.ToSharedRef());
	}
	ChargeBarWidget.Reset();
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

	// Tear down the target boards this zone spawned, same as Z1 does with its
	// pins and Z3 with its shark: SpawnActor's Owner link does not cascade
	// destruction, so without this they would outlive the zone (visible, and
	// still hit-reactive) when the Showcase switcher destroys it to change zones.
	// In-flight projectiles need no handling -- they carry a 4 s SetLifeSpan.
	for (AHapbeatShowcaseZ5TargetActor* Target : { TargetLight.Get(), TargetHeavy.Get() })
	{
		if (IsValid(Target))
		{
			Target->Destroy();
		}
	}
	TargetLight = nullptr;
	TargetHeavy = nullptr;

	if (ChargeAudio != nullptr)
	{
		ChargeAudio->Stop();
		ChargeAudio = nullptr;
	}
	DestroyChargeBar();

	// The character outlives this zone, so a blaster left mounted would follow
	// the player into the next one.
	if (AHapbeatShowcaseCharacter* Character = MountedCharacter.Get())
	{
		Character->UnmountItem();
	}
	MountedCharacter.Reset();

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

	// Resolved once for both boards; any of these may legitimately be null.
	UStaticMesh* BoardMesh = FHapbeatSampleLibrary::LoadShowcaseAsset<UStaticMesh>(TEXT("Meshes"), TEXT("SM_TargetLarge"));
	UMaterialInterface* BoardBase = FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("MI_TargetBase"));
	UMaterialInterface* BoardLight = FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("MI_TargetLight"));
	UMaterialInterface* BoardHeavy = FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("MI_TargetHeavy"));
	USoundBase* HitLightSound = FHapbeatSampleLibrary::LoadShowcaseAsset<USoundBase>(TEXT("Sounds"), TEXT("S_z5_target_hit_light"));
	USoundBase* HitHeavySound = FHapbeatSampleLibrary::LoadShowcaseAsset<USoundBase>(TEXT("Sounds"), TEXT("S_z5_target_hit_heavy"));

	if (TargetLight != nullptr && TargetLight->HitTrigger != nullptr)
	{
		TargetLight->HitTrigger->EventMap = EventMap;
		TargetLight->HitTrigger->EntryId = TarHitLightEntryId;
		TargetLight->HitTrigger->TagFilter = FName(TEXT("ProjectileLight"));
		TargetLight->ApplyShowcaseAssets(BoardMesh, BoardBase, BoardLight, HitLightSound,
			FName(TEXT("ProjectileLight")), TargetBoardSizeCm);
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
		TargetHeavy->ApplyShowcaseAssets(BoardMesh, BoardBase, BoardHeavy, HitHeavySound,
			FName(TEXT("ProjectileHeavy")), TargetBoardSizeCm);
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

	// Hold to charge, release to fire -- Unity ChargeShooter's LMB hold.
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AHapbeatShowcaseZ5ChargeShotActor::HandleChargeBegin);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AHapbeatShowcaseZ5ChargeShotActor::HandleChargeRelease);
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

	// Audio counterpart of the haptic charge loop (Unity ChargeShooter._chargeAudio).
	// Attached to this actor so it dies with the zone even if Release is missed;
	// the volume is driven from chargeT in Tick, starting silent.
	if (ChargeLoopSound != nullptr && ChargeAudio == nullptr)
	{
		ChargeAudio = UGameplayStatics::SpawnSoundAttached(ChargeLoopSound, RootComponent, NAME_None,
			FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, /*bStopWhenAttachedToDestroyed=*/true,
			/*VolumeMultiplier=*/0.0f);
	}

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

	if (ChargeAudio != nullptr)
	{
		ChargeAudio->Stop();
		ChargeAudio = nullptr;
	}

	const bool bHeavy = ChargeT >= HeavyThreshold;
	bPendingHeavyShot = bHeavy;

	// Unity ChargeShooter scales the release one-shot by chargeT
	// (_scaleReleaseVolumeByCharge = true), and picks the heavy variant on the
	// same threshold the haptic shot uses.
	USoundBase* ShotSound = bHeavy && ShotHeavySound != nullptr ? ToRawPtr(ShotHeavySound) : ToRawPtr(ShotLightSound);
	if (ShotSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ShotSound, GetMuzzleTransform().GetLocation(),
			FMath::Clamp(ChargeT, 0.0f, 1.0f));
	}

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

	const FTransform Muzzle = GetMuzzleTransform();
	const FVector Direction = Muzzle.GetRotation().GetForwardVector();
	const float Speed = MaxLaunchSpeed * FMath::Lerp(0.3f, 1.0f, ChargeT);
	const float Scale = FMath::Lerp(MinChargeScale, MaxChargeScale, ChargeT);

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AHapbeatShowcaseZ5ProjectileActor* Projectile = World->SpawnActor<AHapbeatShowcaseZ5ProjectileActor>(
		Muzzle.GetLocation(), Muzzle.Rotator(), Params);
	if (Projectile != nullptr)
	{
		// Unity's Z5 uses two distinct projectile prefabs (bullet / Missile); the
		// heavy one is a missile and is correspondingly larger.
		UStaticMesh* Mesh = bHeavy ? ToRawPtr(ProjectileMeshHeavy) : ToRawPtr(ProjectileMeshLight);
		const float BaseLength = bHeavy ? MissileLengthCm : BulletLengthCm;
		Projectile->Configure(Direction * Speed, bHeavy, Scale, Mesh, BaseLength);
	}
}

void AHapbeatShowcaseZ5ChargeShotActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TryDeferredMount();

	if (bCharging)
	{
		const UWorld* World = GetWorld();
		const double Now = World != nullptr ? World->GetTimeSeconds() : ChargeStartSeconds;
		const float T = MaxChargeSeconds > KINDA_SMALL_NUMBER
			? FMath::Clamp(static_cast<float>(Now - ChargeStartSeconds) / MaxChargeSeconds, 0.0f, 1.0f)
			: 1.0f;
		LastChargeT = T;

		if (ChargeAudio != nullptr)
		{
			// Rises with the charge, as the haptic loop's gain does.
			ChargeAudio->SetVolumeMultiplier(T);
		}

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

	// The Showcase switcher draws a shared Slate key guide covering this, so
	// only print the line when this zone is running on its own.
	if (!IsOwnedByShowcaseSwitcher(this))
	{
		FHapbeatSampleLibrary::ShowHudLine(KeyGuideHudLineKey,
			TEXT("Z5 Target Range -- hold LMB to charge, release to fire"),
			FColor::Cyan, HudRefreshIntervalSeconds * 2.0f);
	}

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
	// Same reason: the shared HUD has a device / ping footer.
	if (!IsOwnedByShowcaseSwitcher(this))
	{
		FHapbeatSampleLibrary::ShowDeviceStatusLine(this, StatusHudLineKey + 1, HudRefreshIntervalSeconds * 2.0f);
	}
}

FText AHapbeatShowcaseZ5ChargeShotActor::GetZoneLabel() const
{
	return FText::FromString(TEXT("Charge Shot"));
}

TArray<FHapbeatShowcaseHudCommand> AHapbeatShowcaseZ5ChargeShotActor::GetHudCommands() const
{
	TArray<FHapbeatShowcaseHudCommand> Commands;
	Commands.Add({ FText::FromString(TEXT("LMB (hold)")),
		FText::FromString(TEXT("charge the blaster; release to fire")) });
	return Commands;
}

FTransform AHapbeatShowcaseZ5ChargeShotActor::GetPlayerSpawnRelative() const
{
	// As with Z4, Unity's spawn sits at the zone origin because its blaster is
	// elsewhere in the zone; here the stand is at the origin, so stand back.
	return FTransform(FRotator::ZeroRotator, FVector(-250.0f, 0.0f, 0.0f));
}

// =============================================================================
// AHapbeatShowcaseZ5TargetActor
// =============================================================================

AHapbeatShowcaseZ5TargetActor::AHapbeatShowcaseZ5TargetActor()
{
	PrimaryActorTick.bCanEverTick = false;

	TargetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TargetMesh"));
	RootComponent = TargetMesh;
	// Movable: this actor is spawned at runtime by the zone (SpawnTargets) and placed with the
	// zone's FootprintOffset applied. Lighting is dynamic.
	// Mobility is set before the mesh assignment so SetStaticMesh never runs on a Static component.
	TargetMesh->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		TargetMesh->SetStaticMesh(CubeMesh);
	}
	// A flat board -- purely cosmetic, no gameplay meaning.
	TargetMesh->SetRelativeScale3D(FVector(0.8f, 0.2f, 0.8f));

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

void AHapbeatShowcaseZ5TargetActor::BeginPlay()
{
	Super::BeginPlay();

	if (TargetMesh != nullptr)
	{
		TargetMesh->OnComponentBeginOverlap.AddDynamic(this, &AHapbeatShowcaseZ5TargetActor::HandleTargetOverlap);
	}
}

void AHapbeatShowcaseZ5TargetActor::ApplyShowcaseAssets(UStaticMesh* Mesh, UMaterialInterface* InBaseMaterial,
	UMaterialInterface* InFlashMaterial, USoundBase* InHitSound, FName InAcceptTag, float DesiredLongestAxisCm)
{
	AcceptTag = InAcceptTag;
	BaseMaterial = InBaseMaterial;
	FlashMaterial = InFlashMaterial;
	HitSound = InHitSound;

	if (TargetMesh == nullptr)
	{
		return;
	}
	if (Mesh != nullptr)
	{
		TargetMesh->SetStaticMesh(Mesh);
		TargetMesh->SetRelativeScale3D(FVector(
			FHapbeatSampleLibrary::ComputeUniformScaleForLength(Mesh, DesiredLongestAxisCm)));
	}
	if (BaseMaterial != nullptr)
	{
		// Slot 0 only: the board reads as one surface, and Unity's TargetReceiver
		// likewise swaps a single shared material.
		TargetMesh->SetMaterial(0, BaseMaterial);
	}
}

void AHapbeatShowcaseZ5TargetActor::HandleTargetOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (OtherActor == nullptr || AcceptTag.IsNone() || !OtherActor->ActorHasTag(AcceptTag))
	{
		return;
	}

	if (HitSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, HitSound, GetActorLocation());
	}

	// Material swap for FlashSeconds, then back -- Unity TargetReceiver's flash.
	// A repeat hit inside the window just restarts the timer, which is what its
	// `_flashEnd = Time.time + _flashSeconds` does too.
	if (FlashMaterial != nullptr && TargetMesh != nullptr)
	{
		TargetMesh->SetMaterial(0, FlashMaterial);
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(FlashTimer);
			World->GetTimerManager().SetTimer(FlashTimer, this,
				&AHapbeatShowcaseZ5TargetActor::EndFlash, FlashSeconds, /*bLoop=*/false);
		}
	}
}

void AHapbeatShowcaseZ5TargetActor::EndFlash()
{
	if (TargetMesh != nullptr && BaseMaterial != nullptr)
	{
		TargetMesh->SetMaterial(0, BaseMaterial);
	}
}

// =============================================================================
// AHapbeatShowcaseZ5ProjectileActor
// =============================================================================

AHapbeatShowcaseZ5ProjectileActor::AHapbeatShowcaseZ5ProjectileActor()
{
	PrimaryActorTick.bCanEverTick = true;

	ProjectileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));
	RootComponent = ProjectileMesh;
	ProjectileMesh->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		ProjectileMesh->SetStaticMesh(SphereMesh);
	}
	ProjectileMesh->SetRelativeScale3D(FVector(0.3f));

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

void AHapbeatShowcaseZ5ProjectileActor::Configure(const FVector& InVelocity, bool bInHeavy, float InScale,
	UStaticMesh* InMesh, float InBaseLengthCm)
{
	Velocity = InVelocity;
	Tags.Add(bInHeavy ? FName(TEXT("ProjectileHeavy")) : FName(TEXT("ProjectileLight")));
	if (ProjectileMesh == nullptr)
	{
		return;
	}

	// The charge multiplier rides on top of the mesh's own finished size, so a
	// half-charged missile is half of a missile, not half of a unit sphere.
	float BaseScale = 1.0f;
	if (InMesh != nullptr)
	{
		ProjectileMesh->SetStaticMesh(InMesh);
		BaseScale = FHapbeatSampleLibrary::ComputeUniformScaleForLength(InMesh, InBaseLengthCm);
	}
	else
	{
		BaseScale = 0.3f; // the primitive sphere's stand-in size
	}
	ProjectileMesh->SetWorldScale3D(FVector(BaseScale * InScale));
}

void AHapbeatShowcaseZ5ProjectileActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Swept move so BeginOverlap fires against the (non-simulating) target
	// boards along the path, not just at the final resting position.
	AddActorWorldOffset(Velocity * DeltaSeconds, /*bSweep=*/true);
}
