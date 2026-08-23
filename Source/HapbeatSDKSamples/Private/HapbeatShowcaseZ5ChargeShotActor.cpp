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
#include "Components/BoxComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/CollisionProfile.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h" // EKeys::*
#include "Kismet/GameplayStatics.h" // GetPlayerPawn / PlaySound2D / SpawnSoundAttached
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Styling/CoreStyle.h" // FCoreStyle::Get().GetBrush("WhiteBrush")
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatShowcaseZ5, Log, All);

namespace
{
	// Unity Showcase.unity, Z5_ChargeShot subtree, converted: UE (X, Y, Z) cm =
	// (Unity z, Unity x, Unity y) x 100.

	/**
	 * The board: Unity TargetBoard (4, 3, -3.82) with target-large parented at
	 * (-3.83, 0, 9.21), i.e. (0.17, 3, 5.39) from the zone origin.
	 */
	const FVector TargetLocalCm(539.0f, 17.0f, 300.0f);

	/** Unity Z5_ChargeShot/PlayerSpawn: the zone origin. */
	const FVector PlayerSpawnCm(0.0f, 0.0f, 0.0f);

	/** Muzzle fallback with no player holding the blaster. */
	constexpr float FallbackMuzzleForwardCm = 120.0f;
	constexpr float FallbackMuzzleUpCm = 40.0f;

	/** Finished sizes of the imported projectiles, longest axis, cm (see the Showcase asset ledger). */
	constexpr float BulletLengthCm = 25.0f;   // SM_BulletFoam, 15 x 15 x 25
	constexpr float MissileLengthCm = 129.0f; // SM_Missile, 129 x 53 x 47
	constexpr float BulletDiameterCm = 15.0f;
	constexpr float MissileDiameterCm = 53.0f;

	/** Unity ChargeShooter._chargeBarColorLow / _chargeBarColorHigh. */
	const FLinearColor ChargeBarLowColor(0.3f, 0.6f, 1.0f, 1.0f);
	const FLinearColor ChargeBarHighColor(1.0f, 0.3f, 0.2f, 1.0f);
	/** The empty part of the bar: dark enough that either fill colour reads against it. */
	const FLinearColor ChargeBarTrackColor(0.08f, 0.08f, 0.09f, 0.9f);

	/**
	 * Per-model correction for the roll / yaw the longest-axis alignment cannot
	 * decide (see AHapbeatShowcaseZ5ProjectileActor::ProjectileMeshExtraRotation).
	 *
	 * BOTH are now zero: the bullet's -90 degree yaw was added when it looked
	 * sideways in a nose-on capture, but the alignment already turns it the right
	 * way and the extra quarter turn is what put it across the shot. Same as the
	 * missile, which never needed one. Kept as named constants rather than
	 * dropped, because they are the hook a replacement model gets corrected on.
	 */
	const FRotator LightProjectileMeshExtraRotation(FRotator::ZeroRotator);
	const FRotator HeavyProjectileMeshExtraRotation(FRotator::ZeroRotator);

	/** SpawnProjectilePreview() placement / lifetime (capture aid only). */
	constexpr float PreviewForwardCm = 150.0f;
	constexpr float PreviewHeightCm = 120.0f;
	constexpr float PreviewHeavyRightCm = 60.0f;
	constexpr float PreviewLifeSeconds = 10.0f;

	/** Charge bar geometry, shared by the bar and its threshold marker. */
	constexpr float ChargeBarWidthPx = 320.0f;
	constexpr float ChargeBarHeightPx = 16.0f;
}

// =============================================================================
// AHapbeatShowcaseZ5ChargeShotActor
// =============================================================================

AHapbeatShowcaseZ5ChargeShotActor::AHapbeatShowcaseZ5ChargeShotActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// No stand model: Unity's blaster is camera-mounted and there is nothing else
	// in its Z5 but the board, so the root is just the zone's transform.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// The board as a child actor: an editable relative transform here, and an
	// actor of its own for the two collision triggers to bind to.
	TargetSlot = CreateDefaultSubobject<UChildActorComponent>(TEXT("Target"));
	TargetSlot->SetupAttachment(RootComponent);
	TargetSlot->SetMobility(EComponentMobility::Movable);
	TargetSlot->SetChildActorClass(AHapbeatShowcaseZ5TargetActor::StaticClass());
	TargetSlot->SetRelativeLocation(TargetLocalCm);

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

	static ConstructorHelpers::FObjectFinder<UStaticMesh> LightProjectile(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_BulletFoam.SM_BulletFoam"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HeavyProjectile(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_Missile.SM_Missile"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Blaster(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_BlasterG.SM_BlasterG"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMat(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_DefaultMaterial.MI_DefaultMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ColormapMat(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_Colormap.MI_Colormap"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TargetBase(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_TargetBase.MI_TargetBase"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TargetLight(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_TargetLight.MI_TargetLight"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TargetHeavy(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_TargetHeavy.MI_TargetHeavy"));
	ProjectileMeshLight = LightProjectile.Object;
	ProjectileMeshHeavy = HeavyProjectile.Object;
	BlasterMeshAsset = Blaster.Object;
	DefaultMaterial = DefaultMat.Object;
	LightProjectileMaterial = ColormapMat.Object;
	TargetBaseMaterial = TargetBase.Object;
	TargetLightMaterial = TargetLight.Object;
	TargetHeavyMaterial = TargetHeavy.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> ChargeLoop(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z5_charge_loop.S_z5_charge_loop"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ShotLight(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z5_shot_light.S_z5_shot_light"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ShotHeavy(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z5_shot_heavy.S_z5_shot_heavy"));
	static ConstructorHelpers::FObjectFinder<USoundBase> TargetHitLight(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z5_target_hit_light.S_z5_target_hit_light"));
	static ConstructorHelpers::FObjectFinder<USoundBase> TargetHitHeavy(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z5_target_hit_heavy.S_z5_target_hit_heavy"));
	ChargeLoopSound = ChargeLoop.Object;
	ShotLightSound = ShotLight.Object;
	ShotHeavySound = ShotHeavy.Object;
	TargetLightSound = TargetHitLight.Object;
	TargetHeavySound = TargetHitHeavy.Object;
}

void AHapbeatShowcaseZ5ChargeShotActor::BeginPlay()
{
	Super::BeginPlay();

	// The sound is already hard-referenced; only its playback mode is runtime
	// state. This preserves Unity's looping AudioSource during a long charge.
	if (USoundWave* ChargeWave = Cast<USoundWave>(ChargeLoopSound))
	{
		ChargeWave->bLooping = true;
	}
	BuildEventMap();
	SetUpTarget();
	BindInput();
	CreateChargeBar();
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
	UStaticMesh* BlasterMesh = BlasterMeshAsset;
	if (Character == nullptr || BlasterMesh == nullptr)
	{
		return; // bare level or no imported art: shots come from the zone origin instead
	}

	// Unity's CameraFollowMount on Blaster carries a zero euler offset, so the
	// only rotation the blaster needs is the automatic one that points its
	// longest axis forward. (The Phase 2 version applied a +15 degree pitch from
	// a shared "default held-item pose"; that pose was Unity's ROD value, and it
	// is why the blaster sat nose-up.)
	const FRotator AlignRotation = FHapbeatSampleLibrary::ComputeLongestAxisToForwardRotation(BlasterMesh);
	// Applied BETWEEN the alignment and the mount rotation, i.e. in the aligned
	// mesh's own space: alignment decides which axis runs forward, this decides
	// which end of it leads (see bFlipBlasterForward).
	const FQuat FlipRotation = bFlipBlasterForward
		? FQuat(FRotator(0.0f, 180.0f, 0.0f))
		: FQuat::Identity;

	FTransform MountPose;
	MountPose.SetLocation(BlasterMountCameraOffsetCm);
	MountPose.SetRotation(BlasterMountExtraRotation.Quaternion() * FlipRotation * AlignRotation.Quaternion());
	// SM_BlasterG is imported at its authored size (x1 in the asset ledger), so
	// unlike Z3's rod there is no scale correction to apply here.
	MountPose.SetScale3D(FVector::OneVector);

	Character->MountItem(BlasterMesh, MountPose, DefaultMaterial);
	MountedCharacter = Character;
}

void AHapbeatShowcaseZ5ChargeShotActor::UnmountBlaster()
{
	// The character outlives this zone, so a blaster left mounted would follow
	// the player into the next one.
	if (AHapbeatShowcaseCharacter* Character = MountedCharacter.Get())
	{
		Character->UnmountItem();
	}
	MountedCharacter.Reset();
	bMountAttempted = false;
}

FTransform AHapbeatShowcaseZ5ChargeShotActor::GetMuzzleTransform() const
{
	if (const AHapbeatShowcaseCharacter* Character = MountedCharacter.Get())
	{
		if (const UStaticMeshComponent* Mount = Character->GetHandMount())
		{
			// The muzzle is a point ON THE BLASTER, not on the camera: shots leave
			// the barrel, which is where the player is looking anyway because the
			// blaster is aligned with the view.
			//
			// THE AIM COMES FROM THE VIEW, NOT FROM THE MOUNT'S ROTATION. What the
			// mount carries is a MESH CORRECTION -- the longest-axis alignment plus
			// the 180-degree bFlipBlasterForward turn applied in
			// MountBlasterOnCharacter -- so for SM_BlasterG its forward vector
			// points back at the player. Taking it for a shot direction sent every
			// projectile, and every SpawnProjectilePreview(), out behind the
			// camera. The blaster is camera-aligned by construction, which is why
			// the view rotation IS the barrel's direction and why
			// MuzzleLocalOffsetCm is measured in that same (forward, right, up)
			// frame.
			const FQuat Rotation = Character->GetViewTransform().GetRotation();
			return FTransform(Rotation,
				Mount->GetComponentLocation() + Rotation.RotateVector(MuzzleLocalOffsetCm));
		}
	}
	return FTransform(GetActorRotation(),
		GetActorLocation() + GetActorForwardVector() * FallbackMuzzleForwardCm
		+ FVector(0.0f, 0.0f, FallbackMuzzleUpCm));
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

	if (ChargeBarWidget.IsValid())
	{
		return; // already up (re-entering the zone)
	}

	// Same opaque backing as Z4's slider panel, for the same reason: SBorder's
	// default brush is nearly transparent, so white text on it was unreadable
	// over a bright zone.
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FLinearColor PanelColor(0.0f, 0.0f, 0.0f, 0.75f);

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
			.BorderImage(WhiteBrush)
			.BorderBackgroundColor(PanelColor)
			.Padding(FMargin(12.0f, 8.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FSlateColor(FLinearColor::White))
					.Text_Lambda([this]()
					{
						return FText::FromString(FString::Printf(TEXT("Charge  %.0f%%"), LastChargeT * 100.0f));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SBox).WidthOverride(ChargeBarWidthPx).HeightOverride(ChargeBarHeightPx)
					[
						SNew(SOverlay)
						// The track.
						+ SOverlay::Slot()
						[
							SNew(SImage)
							.Image(WhiteBrush)
							.ColorAndOpacity(FSlateColor(ChargeBarTrackColor))
						]
						// The fill: a plain box whose WIDTH is the charge, drawn
						// in the low / high colour.
						//
						// NOT SProgressBar, which is what this was: its fill is
						// drawn from the active Slate style's own brush, and
						// FillColorAndOpacity only TINTS that brush -- against
						// the editor style's near-white fill the low blue and the
						// high red both came out as the same pale bar, so the
						// crossing of the heavy threshold (the one thing the bar
						// exists to show) was invisible. Drawing the fill here
						// makes the colour the colour.
						+ SOverlay::Slot()
						.HAlign(HAlign_Left)
						[
							SNew(SBox)
							.WidthOverride_Lambda([this]()
							{
								return FOptionalSize(ChargeBarWidthPx * FMath::Clamp(LastChargeT, 0.0f, 1.0f));
							})
							.HeightOverride(ChargeBarHeightPx)
							[
								SNew(SImage)
								.Image(WhiteBrush)
								.ColorAndOpacity_Lambda([this]()
								{
									return FSlateColor(LastChargeT >= HeavyThreshold
										? ChargeBarHighColor : ChargeBarLowColor);
								})
							]
						]
						+ SOverlay::Slot()
						[
							// A thin marker at the heavy threshold. NOT a Unity
							// feature -- Unity only changes the fill colour when the
							// bar crosses it -- but without it there is no way to see
							// where "heavy" starts until you have already passed it.
							SNew(SBox)
							.HAlign(HAlign_Left)
							.Padding_Lambda([this]()
							{
								return FMargin(ChargeBarWidthPx * FMath::Clamp(HeavyThreshold, 0.0f, 1.0f),
									0.0f, 0.0f, 0.0f);
							})
							[
								SNew(SBox).WidthOverride(2.0f).HeightOverride(ChargeBarHeightPx)
								[
									SNew(SImage)
									.Image(WhiteBrush)
									.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.7f)))
								]
							]
						]
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
	OnZoneDeactivated();
	DestroyChargeBar();
	Super::EndPlay(EndPlayReason);
}

void AHapbeatShowcaseZ5ChargeShotActor::OnZoneActivated()
{
	// The blaster goes back into the player's hand (it was handed back on the way
	// out), the board goes back to its resting colour, and the bar starts empty.
	bMountAttempted = false;
	LastChargeT = 0.0f;
	bCharging = false;
	bThresholdReached = false;
	if (Target != nullptr)
	{
		Target->ResetLook();
	}
	CreateChargeBar();
}

void AHapbeatShowcaseZ5ChargeShotActor::OnZoneDeactivated()
{
	// A charge in progress must not survive the switch: the loop would keep
	// streaming and the delayed shot would fire into the next zone.
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
	bCharging = false;
	LastChargeT = 0.0f;

	if (ChargeAudio != nullptr)
	{
		ChargeAudio->Stop();
		ChargeAudio = nullptr;
	}
	DestroyChargeBar();
	UnmountBlaster();
	// In-flight projectiles need no handling -- they carry a 4 s SetLifeSpan.
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

void AHapbeatShowcaseZ5ChargeShotActor::SetUpTarget()
{
	Target = TargetSlot != nullptr
		? Cast<AHapbeatShowcaseZ5TargetActor>(TargetSlot->GetChildActor())
		: nullptr;
	if (Target == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ5, Warning,
			TEXT("Z5: the Target child actor is missing; hits will not register."));
		return;
	}

	// One board, two triggers: light and heavy are a property of what hit it, not
	// of which board was hit (Unity has a single TargetBoard for the same reason).
	if (Target->LightHitTrigger != nullptr)
	{
		Target->LightHitTrigger->EventMap = EventMap;
		Target->LightHitTrigger->EntryId = TarHitLightEntryId;
		Target->LightHitTrigger->TagFilter = FName(TEXT("ProjectileLight"));
	}
	if (Target->HeavyHitTrigger != nullptr)
	{
		Target->HeavyHitTrigger->EventMap = EventMap;
		Target->HeavyHitTrigger->EntryId = TarHitHeavyEntryId;
		Target->HeavyHitTrigger->TagFilter = FName(TEXT("ProjectileHeavy"));
	}

	// The face looks back at the player, who stands at the zone origin -- so the
	// direction is from the board towards that origin, whichever way the imported
	// model's own axes happen to run.
	const FVector BoardLocal = TargetSlot != nullptr ? TargetSlot->GetRelativeLocation() : FVector::ZeroVector;
	const FVector FaceDirection = (-BoardLocal).GetSafeNormal();

	Target->ApplyShowcaseAssets(TargetSizeCm,
		FaceDirection.IsNearlyZero() ? -FVector::ForwardVector : FaceDirection,
		TargetBaseMaterial, TargetLightMaterial, TargetHeavyMaterial,
		TargetLightSound, TargetHeavySound);
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
	// The bar empties the moment the shot leaves, as Unity's does when the slider
	// is driven from a chargeT that is no longer accumulating. Without this it sat
	// at whatever it reached and looked stuck.
	LastChargeT = 0.0f;
	bThresholdReached = false;

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
	// The shot is a one-shot source. By the time it fires, the charge loop has
	// already been stopped+flushed, so it normally opens a fresh wire session.
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
		const float BaseDiameter = bHeavy ? MissileDiameterCm : BulletDiameterCm;
		Projectile->Configure(Direction * Speed, bHeavy, Scale, Mesh, BaseLength, BaseDiameter,
			bHeavy ? HeavyProjectileMeshExtraRotation : LightProjectileMeshExtraRotation,
			bHeavy ? ToRawPtr(DefaultMaterial) : ToRawPtr(LightProjectileMaterial),
			ProjectileRestitution, ProjectileFriction);
	}
}

void AHapbeatShowcaseZ5ChargeShotActor::DebugSetChargeForCapture(float T)
{
	// The displayed value and nothing else -- see the header. The bar reads
	// LastChargeT every frame, so this is all a capture needs.
	LastChargeT = FMath::Clamp(T, 0.0f, 1.0f);
}

void AHapbeatShowcaseZ5ChargeShotActor::DebugFireProjectileForCapture(float ChargeT, bool bHeavy)
{
	SpawnProjectile(FMath::Clamp(ChargeT, 0.0f, 1.0f), bHeavy);
}

bool AHapbeatShowcaseZ5ChargeShotActor::DebugIsChargeSoundLooping() const
{
	const USoundWave* ChargeWave = Cast<USoundWave>(ChargeLoopSound);
	return ChargeWave != nullptr && ChargeWave->bLooping;
}

void AHapbeatShowcaseZ5ChargeShotActor::SpawnProjectilePreview(bool bHeavy, float YawOffsetDeg)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// From the PLAYER's view, not the muzzle's: the preview is a photograph of
	// the model, and hanging it off the blaster made where it landed depend on
	// where the blaster happened to be mounted. Yaw only -- a pitched view must
	// not tilt the pose being examined.
	const AHapbeatShowcaseCharacter* Character = MountedCharacter.Get();
	const FTransform View = Character != nullptr ? Character->GetViewTransform() : GetMuzzleTransform();
	const FQuat Rotation = FRotator(0.0f, View.GetRotation().Rotator().Yaw + YawOffsetDeg, 0.0f).Quaternion();

	// Placed along the view's own yaw, then lifted to a fixed height above the
	// zone floor so the shot frames the same way whatever the player is doing.
	const FVector ViewForward = FRotator(0.0f, View.GetRotation().Rotator().Yaw, 0.0f).Vector();
	FVector Location = View.GetLocation() + ViewForward * PreviewForwardCm;
	Location.Z = GetActorLocation().Z + PreviewHeightCm;
	if (bHeavy)
	{
		// Offset sideways so a light and a heavy preview posed together do not
		// occupy the same spot.
		Location += Rotation.GetRightVector() * PreviewHeavyRightCm;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AHapbeatShowcaseZ5ProjectileActor* Projectile = World->SpawnActor<AHapbeatShowcaseZ5ProjectileActor>(
		Location, Rotation.Rotator(), Params);
	if (Projectile == nullptr)
	{
		return;
	}

	// Configure the real mesh/collider first, then freeze it as capture scenery.
	UStaticMesh* Mesh = bHeavy ? ToRawPtr(ProjectileMeshHeavy) : ToRawPtr(ProjectileMeshLight);
	const float BaseLength = bHeavy ? MissileLengthCm : BulletLengthCm;
	const float BaseDiameter = bHeavy ? MissileDiameterCm : BulletDiameterCm;
	Projectile->Configure(FVector::ZeroVector, bHeavy, /*InScale=*/1.0f, Mesh, BaseLength, BaseDiameter,
		bHeavy ? HeavyProjectileMeshExtraRotation : LightProjectileMeshExtraRotation,
		bHeavy ? ToRawPtr(DefaultMaterial) : ToRawPtr(LightProjectileMaterial),
		ProjectileRestitution, ProjectileFriction);
	Projectile->SetPreviewMode();
	// Longer than Configure()'s BeginPlay lifespan (4 s), which would otherwise
	// take the preview away mid-capture.
	Projectile->SetLifeSpan(PreviewLifeSeconds);

	// The finished WORLD bounds, read after Configure() has swapped in the mesh
	// and applied its fit + nose-first alignment. This is the part that says
	// which way the projectile ended up pointing without anyone having to look at
	// the screenshot: the extent is longest along the axis the model's length
	// runs along, so a projectile aimed down a yaw-0 shot reads X-longest, and a
	// Y-longest one is lying across the shot instead of along it.
	FVector BoundsOrigin = FVector::ZeroVector;
	FVector BoundsExtent = FVector::ZeroVector;
	Projectile->GetActorBounds(/*bOnlyCollidingComponents=*/false, BoundsOrigin, BoundsExtent);

	// Kept, not temporary debugging: the preview exists to be photographed, and
	// this line is what says whether an empty-looking shot means "not spawned",
	// "spawned off-camera" or "spawned at zero size".
	UE_LOG(LogHapbeatShowcaseZ5, Log,
		TEXT("Z5 preview (%s) spawned at %s facing %s; mesh=%s scale=%s worldExtent=%s"),
		bHeavy ? TEXT("heavy") : TEXT("light"),
		*Location.ToCompactString(),
		*Rotation.Rotator().ToCompactString(),
		Mesh != nullptr ? *Mesh->GetName() : TEXT("<none: sphere fallback>"),
		*Projectile->GetActorScale3D().ToCompactString(),
		*BoundsExtent.ToCompactString());
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
			// Mixed as a one-shot source over the still-running charge loop.
			FireOneShotEntry(ChargeThresholdEntryId);
		}
	}

	// The Showcase switcher draws a shared Slate HUD covering the key guide, the
	// zone's own state and the device footer -- and this zone's own charge bar
	// widget already shows the charge -- so a zone under it prints none of the
	// on-screen debug lines. ONE EARLY RETURN, not a guard around each line: the
	// Charge line below used to sit outside the per-line guard and showed on top
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
		TEXT("Z5 Target Range -- hold LMB to charge, release to fire"),
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
	// Unity Z5_ChargeShot/PlayerSpawn is the zone origin, and the board is placed
	// relative to that -- so standing anywhere else would put the board somewhere
	// other than where Unity has it relative to the player.
	return FTransform(FRotator::ZeroRotator, PlayerSpawnCm);
}

// =============================================================================
// AHapbeatShowcaseZ5TargetActor
// =============================================================================

AHapbeatShowcaseZ5TargetActor::AHapbeatShowcaseZ5TargetActor()
{
	PrimaryActorTick.bCanEverTick = false;

	TargetRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = TargetRoot;

	TargetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TargetMesh"));
	TargetMesh->SetupAttachment(TargetRoot);
	// Movable: the switcher hides and shows the zone this board belongs to, so
	// its lighting is dynamic. Mobility is set before the mesh assignment so
	// SetStaticMesh never runs on a Static component.
	TargetMesh->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		TargetMesh->SetStaticMesh(CubeMesh);
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TargetAsset(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_TargetLarge.SM_TargetLarge"));
	if (TargetAsset.Succeeded())
	{
		TargetMesh->SetStaticMesh(TargetAsset.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TargetBase(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_TargetBase.MI_TargetBase"));
	if (TargetBase.Succeeded())
	{
		TargetMesh->SetMaterial(0, TargetBase.Object);
		BaseMaterial = TargetBase.Object;
	}

	// The imported target already has one generated simple-convex hull. Use that
	// same transformed component for both appearance and blocking collision,
	// just as Unity uses a convex MeshCollider on target-large.
	TargetMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TargetMesh->SetCollisionObjectType(ECC_WorldStatic);
	TargetMesh->SetNotifyRigidBodyCollision(true);
	TargetMesh->SetGenerateOverlapEvents(false);

	// TWO triggers on one board: which entry fires is decided by the TAG of the
	// projectile that arrives, not by which of two boards it hit. Both bind to
	// the target mesh and filter independently.
	LightHitTrigger = CreateDefaultSubobject<UHapbeatCollisionTriggerComponent>(TEXT("LightHitTrigger"));
	LightHitTrigger->TriggerEvent = EHapbeatCollisionEvent::Hit;
	LightHitTrigger->bEnterOnly = true;
	LightHitTrigger->GainMode = EHapbeatGainMode::Fixed;
	// A missile sweeping through a 53 cm deep board can report more than one
	// contact in a frame; one physical impact should still fire only once.
	LightHitTrigger->Cooldown = 0.1f;

	HeavyHitTrigger = CreateDefaultSubobject<UHapbeatCollisionTriggerComponent>(TEXT("HeavyHitTrigger"));
	HeavyHitTrigger->TriggerEvent = EHapbeatCollisionEvent::Hit;
	HeavyHitTrigger->bEnterOnly = true;
	HeavyHitTrigger->GainMode = EHapbeatGainMode::Fixed;
	HeavyHitTrigger->Cooldown = 0.1f;
	// TagFilter / EventMap / EntryId are assigned by the owning zone actor (see
	// AHapbeatShowcaseZ5ChargeShotActor::SetUpTarget).
}

void AHapbeatShowcaseZ5TargetActor::BeginPlay()
{
	Super::BeginPlay();

	if (TargetMesh != nullptr)
	{
		TargetMesh->OnComponentHit.AddDynamic(this, &AHapbeatShowcaseZ5TargetActor::HandleTargetHit);
	}
}

void AHapbeatShowcaseZ5TargetActor::ApplyShowcaseAssets(const FVector& SizeCm, const FVector& FaceDirection,
	UMaterialInterface* InBaseMaterial, UMaterialInterface* InLightFlash, UMaterialInterface* InHeavyFlash,
	USoundBase* InLightSound, USoundBase* InHeavySound)
{
	BaseMaterial = InBaseMaterial;
	LightFlashMaterial = InLightFlash;
	HeavyFlashMaterial = InHeavyFlash;
	LightHitSound = InLightSound;
	HeavyHitSound = InHeavySound;
	if (TargetMesh == nullptr)
	{
		return;
	}
	const UStaticMesh* Mesh = TargetMesh->GetStaticMesh();
	if (Mesh != nullptr)
	{
		// Fitted by rank (largest -> 180, then 180, then the 53 cm depth), then
		// turned so the SHORTEST axis -- the depth -- points at the player, which
		// is what puts the printed face towards them whichever way the source
		// model lies.
		TargetMesh->SetRelativeScale3D(FHapbeatSampleLibrary::ComputeAxisFitScale(Mesh, SizeCm));
		// ... and then, if asked, turned end for end about up, because aiming the
		// depth axis at the player says nothing about WHICH of the two faces ends
		// up forward (see bFlipTargetFacing).
		const FQuat Aimed =
			FHapbeatSampleLibrary::ComputeShortestAxisToDirectionRotation(Mesh, FaceDirection).Quaternion();
		const FQuat Facing = bFlipTargetFacing ? FQuat(FRotator(0.0f, 180.0f, 0.0f)) * Aimed : Aimed;
		TargetMesh->SetRelativeRotation(Facing);
		const FVector MeshCentre = FHapbeatSampleLibrary::ComputeFittedBoundsCentre(
			Mesh, TargetMesh->GetRelativeScale3D(), Facing.Rotator());
		TargetMesh->SetRelativeLocation(-MeshCentre);
	}

	if (BaseMaterial != nullptr)
	{
		// Slot 0 only: the board reads as one surface, and Unity's TargetReceiver
		// likewise swaps a single shared material.
		TargetMesh->SetMaterial(0, BaseMaterial);
	}
}

void AHapbeatShowcaseZ5TargetActor::ResetLook()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FlashTimer);
	}
	EndFlash();
}

void AHapbeatShowcaseZ5TargetActor::HandleTargetHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	if (OtherActor == nullptr)
	{
		return;
	}
	const bool bHeavy = OtherActor->ActorHasTag(FName(TEXT("ProjectileHeavy")));
	const bool bLight = OtherActor->ActorHasTag(FName(TEXT("ProjectileLight")));
	if (!bHeavy && !bLight)
	{
		return; // something else drifted through the board
	}
	++DebugHitCount;

	if (USoundBase* HitSound = bHeavy ? ToRawPtr(HeavyHitSound) : ToRawPtr(LightHitSound))
	{
		UGameplayStatics::PlaySoundAtLocation(this, HitSound, GetActorLocation());
	}

	// Flash the colour of the shot that landed -- the Phase 2 version had one
	// fixed colour per board, so with a single board the board never changed
	// colour at all. A repeat hit inside the window just restarts the timer,
	// which is what Unity's `_flashEnd = Time.time + _flashSeconds` does too.
	UMaterialInterface* FlashMaterial = bHeavy ? ToRawPtr(HeavyFlashMaterial) : ToRawPtr(LightFlashMaterial);
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
	PrimaryActorTick.bCanEverTick = false;

	ProjectileBody = CreateDefaultSubobject<UBoxComponent>(TEXT("ProjectileBody"));
	RootComponent = ProjectileBody;
	ProjectileBody->SetMobility(EComponentMobility::Movable);
	ProjectileBody->SetBoxExtent(FVector(12.5f, 7.5f, 7.5f));
	ProjectileBody->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	ProjectileBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ProjectileBody->SetNotifyRigidBodyCollision(true);
	ProjectileBody->SetGenerateOverlapEvents(false);
	ProjectileBody->BodyInstance.bUseCCD = true;
	ProjectileBody->BodyInstance.bSimulatePhysics = true;
	ProjectileBody->BodyInstance.bEnableGravity = true;

	ProjectileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));
	ProjectileMesh->SetupAttachment(ProjectileBody);
	ProjectileMesh->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		ProjectileMesh->SetStaticMesh(SphereMesh);
	}
	ProjectileMesh->SetRelativeScale3D(FVector(0.3f));

	ProjectileMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
}

void AHapbeatShowcaseZ5ProjectileActor::BeginPlay()
{
	Super::BeginPlay();
	// Mirrors Unity's `Destroy(projectile.gameObject, 4f)` -- a fixed lifespan
	// regardless of whether it hit a target. Does not depend on Configure()'s
	// Velocity/tag (safe even though BeginPlay runs before Configure() is
	// called by the spawner -- see the header's Configure doc).
	SetLifeSpan(4.0f);
	if (ProjectileBody != nullptr)
	{
		ProjectileBody->SetSimulatePhysics(true);
		ProjectileBody->SetEnableGravity(true);
	}
}

void AHapbeatShowcaseZ5ProjectileActor::Configure(const FVector& InVelocity, bool bInHeavy, float InScale,
	UStaticMesh* InMesh, float InBaseLengthCm, float InBaseDiameterCm,
	const FRotator& InMeshExtraRotation, UMaterialInterface* InMaterial,
	float InRestitution, float InFriction)
{
	ProjectileMeshExtraRotation = InMeshExtraRotation;
	Tags.Add(bInHeavy ? FName(TEXT("ProjectileHeavy")) : FName(TEXT("ProjectileLight")));
	if (ProjectileBody != nullptr)
	{
		RuntimePhysicalMaterial = NewObject<UPhysicalMaterial>(this);
		RuntimePhysicalMaterial->Restitution = FMath::Clamp(InRestitution, 0.0f, 1.0f);
		RuntimePhysicalMaterial->Friction = FMath::Max(0.0f, InFriction);
		RuntimePhysicalMaterial->bOverrideRestitutionCombineMode = true;
		RuntimePhysicalMaterial->RestitutionCombineMode = EFrictionCombineMode::Max;
		ProjectileBody->SetPhysMaterialOverride(RuntimePhysicalMaterial);

		const float Length = FMath::Max(InBaseLengthCm * InScale, 1.0f);
		const float Diameter = FMath::Max(InBaseDiameterCm * InScale, 1.0f);
		ProjectileBody->SetBoxExtent(FVector(Length, Diameter, Diameter) * 0.5f, true);
		ProjectileBody->SetMassOverrideInKg(NAME_None, bInHeavy ? 1.0f : 0.2f, true);
		ProjectileBody->SetPhysicsLinearVelocity(InVelocity);
		ProjectileBody->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
	if (ProjectileMesh == nullptr)
	{
		return;
	}

	// The charge multiplier rides on top of the mesh's own finished size, so a
	// half-charged missile is half of a missile, not half of a unit sphere.
	if (InMesh != nullptr)
	{
		ProjectileMesh->SetStaticMesh(InMesh);

		// Paint every slot from the spawner's constructor-loaded hard reference.
		if (InMaterial != nullptr)
		{
			for (int32 SlotIndex = 0; SlotIndex < ProjectileMesh->GetNumMaterials(); ++SlotIndex)
			{
				ProjectileMesh->SetMaterial(SlotIndex, InMaterial);
			}
		}

		// Proportional fit (only the length is stated), then the charge multiplier.
		const FVector BaseScale =
			FHapbeatSampleLibrary::ComputeAxisFitScale(InMesh, FVector(InBaseLengthCm, 0.0f, 0.0f));
		ProjectileMesh->SetRelativeScale3D(BaseScale * InScale);
		// Nose-first: turn the longest axis onto the actor's +X, which the spawner
		// has already pointed along the shot direction. Without this a missile flew
		// sideways, since which local axis its length runs along is a property of
		// the source model.
		const FQuat AlignRotation =
			FHapbeatSampleLibrary::ComputeLongestAxisToForwardRotation(InMesh).Quaternion();
		const FQuat FlipRotation = bFlipForward ? FQuat(FRotator(0.0f, 180.0f, 0.0f)) : FQuat::Identity;
		// The per-model correction goes on last, in the actor's space: alignment
		// picks the axis, the flip picks the end, and this fixes the roll / yaw
		// about that axis that neither of them can know (see the property).
		ProjectileMesh->SetRelativeRotation(
			ProjectileMeshExtraRotation.Quaternion() * FlipRotation * AlignRotation);
	}
	else
	{
		ProjectileMesh->SetRelativeScale3D(FVector(0.3f * InScale)); // the primitive sphere's stand-in size
	}
}

void AHapbeatShowcaseZ5ProjectileActor::SetPreviewMode()
{
	if (ProjectileBody != nullptr)
	{
		ProjectileBody->SetPhysicsLinearVelocity(FVector::ZeroVector);
		ProjectileBody->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		ProjectileBody->SetSimulatePhysics(false);
		ProjectileBody->SetEnableGravity(false);
	}
	SetActorEnableCollision(false);
}
