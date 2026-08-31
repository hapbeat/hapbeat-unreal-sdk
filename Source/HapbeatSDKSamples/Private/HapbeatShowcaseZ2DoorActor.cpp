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
	// The door's geometry is Door.fbx's own -- see the class comment. In the
	// FBX's frame ("native" below) the pieces share one origin, X is the leaf's
	// width, Y its thickness and Z its height, with the floor already at Z ~ 1.

	/**
	 * Yaw applied to the whole assembly, so the FBX's width axis (X) becomes this
	 * zone's width axis (Y) and its thickness axis becomes X -- which puts the
	 * door's face towards the player, who stands on -X.
	 */
	// -90 puts the hinge on the player's LEFT (-Y) and the handle on their RIGHT
	// (+Y) when they stand at PlayerSpawn and face +X through the doorway.
	constexpr float AssemblyYawDegrees = -90.0f;

	/**
	 * The hinge, in the FBX's frame: the leaf's +X edge
	 * (bounds origin -0.4 + extent 66.5 ~ +66.1). THE HINGE IS THE SIDE OPPOSITE
	 * THE KNOB, and SM_DoorHandle is authored at native x ~ -39.3, i.e. on the -X
	 * half -- so the hinge belongs on +X. Every mesh keeps its authored position
	 * (the three were modelled against one shared origin), which is what fixes the
	 * knob: the previous version gave DoorHandleMesh the leaf's own hinge offset a
	 * second time and pushed the knob out to native x ~ +27.6, onto the hinge side.
	 * The leaf is placed at -66.1 within the hinge, which returns it to that shared
	 * origin; the knob then rides the leaf at a relative zero.
	 */
	constexpr float HingeNativeXCm = 66.1f;

	/** Fallback slab, used only when SM_Door is missing: the FBX leaf's own size. */
	const FVector FallbackLeafSizeCm(133.0f, 11.5f, 298.0f); // native X width / Y thickness / Z height
	constexpr float FallbackLeafCentreZCm = 150.0f;          // native leaf bounds origin, floor at Z ~ 1

	/** Unity Z2_Door/PlayerSpawn: local (0.2, 0.2, -4). Z is the player's feet, hence 0. */
	const FVector PlayerSpawnCm(-400.0f, 20.0f, 0.0f);
}

// =============================================================================
// AHapbeatShowcaseZ2DoorActor
// =============================================================================

AHapbeatShowcaseZ2DoorActor::AHapbeatShowcaseZ2DoorActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// The actor root does NOT rotate -- the frame hangs off it and has to stay
	// put. DoorHinge below is the pivot.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// Fixed frame, at the FBX's own origin, carrying the assembly yaw.
	DoorFrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorFrameMesh"));
	DoorFrameMesh->SetupAttachment(RootComponent);
	DoorFrameMesh->SetMobility(EComponentMobility::Movable);
	DoorFrameMesh->SetRelativeLocation(FVector::ZeroVector);
	DoorFrameMesh->SetRelativeRotation(FRotator(0.0f, AssemblyYawDegrees, 0.0f));
	DoorFrameMesh->SetCollisionProfileName(TEXT("BlockAll"));
	// No primitive stand-in: a frame drawn as a cube would be a wall across the
	// doorway. It simply does not appear until SM_DoorFrame is imported.
	DoorFrameMesh->SetVisibility(false);

	// The pivot. SetDoorYaw turns this and nothing else, so the leaf swings
	// about its hinge-side edge while the frame stays where it is.
	DoorHinge = CreateDefaultSubobject<USceneComponent>(TEXT("DoorHinge"));
	DoorHinge->SetupAttachment(RootComponent);
	DoorHinge->SetMobility(EComponentMobility::Movable);
	// The native hinge point, carried through the assembly yaw, and that same yaw
	// as the hinge's own resting rotation (SetDoorYaw adds the swing on top).
	DoorHinge->SetRelativeLocation(
		FRotator(0.0f, AssemblyYawDegrees, 0.0f).RotateVector(FVector(HingeNativeXCm, 0.0f, 0.0f)));
	DoorHinge->SetRelativeRotation(FRotator(0.0f, AssemblyYawDegrees, 0.0f));

	DoorLeafMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorLeafMesh"));
	DoorLeafMesh->SetupAttachment(DoorHinge);
	DoorLeafMesh->SetMobility(EComponentMobility::Movable); // must stay Movable: it is rotated at runtime
	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		DoorLeafMesh->SetStaticMesh(CubeMesh);
	}
	// Stand-in only: a cube the size of the FBX leaf, in the FBX's own axes, sat
	// where that leaf's bounds centre is. ApplyShowcaseAssets replaces both the
	// mesh and this offset when SM_Door is present.
	DoorLeafMesh->SetRelativeLocation(
		FVector(-HingeNativeXCm, 0.0f, FallbackLeafCentreZCm));
	DoorLeafMesh->SetRelativeScale3D(FallbackLeafSizeCm / 100.0f); // engine Cube is 100 cm authored
	DoorLeafMesh->SetCollisionProfileName(TEXT("BlockAll"));

	// Carried by the leaf, so it swings with it. Its own placement comes from
	// the imported model; hidden until that model is present.
	DoorHandleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorHandleMesh"));
	DoorHandleMesh->SetupAttachment(DoorLeafMesh);
	DoorHandleMesh->SetMobility(EComponentMobility::Movable);
	DoorHandleMesh->SetCollisionProfileName(TEXT("NoCollision"));
	DoorHandleMesh->SetVisibility(false);

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

	// Fixed Showcase art is constructor-assigned so the actor is complete in the
	// editor and Play does not synchronously resolve meshes/materials/SFX.
	ApplyShowcaseAssets();
}

void AHapbeatShowcaseZ2DoorActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// These are constructor-created components, so resolving their Event Map
	// here makes the actual trigger wiring inspectable before PIE.
	BuildEventMap();
}

void AHapbeatShowcaseZ2DoorActor::BeginPlay()
{
	Super::BeginPlay();

	BuildEventMap();
	BindInput();

	ResetDoorState();
}

void AHapbeatShowcaseZ2DoorActor::ApplyShowcaseAssets()
{
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DoorMaterial(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_DefaultMaterial.MI_DefaultMaterial"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> LeafMesh(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_Door.SM_Door"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FrameMesh(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_DoorFrame.SM_DoorFrame"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HandleMesh(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_DoorHandle.SM_DoorHandle"));

	// Three separate meshes out of Door.fbx (imported with Combine Meshes OFF).
	// The leaf has to be its own asset for any of this to work: a leaf welded to
	// its frame cannot swing.
	//
	// NONE of them is scaled or re-oriented here. They were modelled against one
	// shared origin, so each is placed at scale 1 with the offset that returns it
	// to that origin, and the assembly's yaw lives on the frame and the hinge
	// (see the class comment). Fitting them to invented box sizes -- what Phase 2
	// did -- is exactly what broke the fit between leaf and frame.
	if (LeafMesh.Succeeded())
	{
		if (DoorLeafMesh != nullptr)
		{
			DoorLeafMesh->SetStaticMesh(LeafMesh.Object);
			DoorLeafMesh->SetRelativeScale3D(FVector::OneVector);
			DoorLeafMesh->SetRelativeRotation(FRotator::ZeroRotator);
			// Back to the shared origin: the hinge sits at native X = +66.1, so the
			// leaf sits at -66.1 within it, i.e. exactly where the FBX authored it.
			DoorLeafMesh->SetRelativeLocation(FVector(-HingeNativeXCm, 0.0f, 0.0f));
			DoorLeafMesh->SetVisibility(true);
			if (DoorMaterial.Succeeded())
			{
				DoorLeafMesh->SetMaterial(0, DoorMaterial.Object);
			}
		}
	}

	if (FrameMesh.Succeeded())
	{
		if (DoorFrameMesh != nullptr)
		{
			// Authored size, authored position: the constructor already gave this
			// component the assembly yaw and a zero offset, which is the shared
			// origin. Its own geometry puts the sill on the floor.
			DoorFrameMesh->SetStaticMesh(FrameMesh.Object);
			DoorFrameMesh->SetRelativeScale3D(FVector::OneVector);
			DoorFrameMesh->SetVisibility(true);
			if (DoorMaterial.Succeeded())
			{
				DoorFrameMesh->SetMaterial(0, DoorMaterial.Object);
			}
		}
	}

	if (HandleMesh.Succeeded())
	{
		if (DoorHandleMesh != nullptr)
		{
			// A child of the leaf, so it rides the swing -- and at a RELATIVE ZERO,
			// because the leaf is already back at the shared origin and the knob's
			// own geometry sits where it was authored (native x ~ -39.3, the -X
			// half, opposite the hinge at +66.1). Re-applying the leaf's hinge
			// offset here -- what the previous version did -- moved the knob to
			// native x ~ +27.6, onto the hinge side, which is what PIE showed.
			DoorHandleMesh->SetStaticMesh(HandleMesh.Object);
			DoorHandleMesh->SetRelativeScale3D(FVector::OneVector);
			DoorHandleMesh->SetRelativeRotation(FRotator::ZeroRotator);
			DoorHandleMesh->SetRelativeLocation(FVector::ZeroVector);
			DoorHandleMesh->SetVisibility(true);
			if (DoorMaterial.Succeeded())
			{
				DoorHandleMesh->SetMaterial(0, DoorMaterial.Object);
			}
		}
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> OpenSoundAsset(TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z2_door_open.S_z2_door_open"));
	static ConstructorHelpers::FObjectFinder<USoundBase> CloseSoundAsset(TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z2_door_close.S_z2_door_close"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SlamSoundAsset(TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z2_door_slam.S_z2_door_slam"));
	static ConstructorHelpers::FObjectFinder<USoundBase> LockSoundAsset(TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z2_door_lock.S_z2_door_lock"));
	static ConstructorHelpers::FObjectFinder<USoundBase> UnlockSoundAsset(TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z2_door_unlock.S_z2_door_unlock"));
	static ConstructorHelpers::FObjectFinder<USoundBase> RattleSoundAsset(TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Sounds/S_z2_door_rattle.S_z2_door_rattle"));
	OpenSound = OpenSoundAsset.Object;
	CloseSound = CloseSoundAsset.Object;
	SlamSound = SlamSoundAsset.Object;
	LockSound = LockSoundAsset.Object;
	UnlockSound = UnlockSoundAsset.Object;
	RattleSound = RattleSoundAsset.Object;
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
	// Same teardown the switcher asks for on the way out of the zone.
	OnZoneDeactivated();
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
		ActiveCloseDurationSeconds = CloseDurationSeconds;
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

void AHapbeatShowcaseZ2DoorActor::DebugSetDoorOpen(bool bOpen)
{
	// Deliberately routed through HandleToggleKey rather than setting State
	// directly: a capture must show the door the player would see, tween, sound,
	// haptics and all.
	const bool bWantsToggle = bOpen
		? State == EHapbeatZ2DoorState::Closed
		: State == EHapbeatZ2DoorState::Open;
	if (bWantsToggle)
	{
		HandleToggleKey();
	}
}

void AHapbeatShowcaseZ2DoorActor::HandleActionKey()
{
	switch (State)
	{
	case EHapbeatZ2DoorState::Open:
		// Slam: the same swing as a close, taken at Unity's slam speed
		// (SlamDurationSeconds, ~0.12 s). Snapping straight to Closed -- which is
		// what the previous version did -- meant the haptic fired against a door
		// that had already teleported shut, with nothing on screen to match it.
		State = EHapbeatZ2DoorState::Closing;
		StateElapsedSeconds = 0.0f;
		ActiveCloseDurationSeconds = SlamDurationSeconds;
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
	// The HINGE, not the actor root: turning the root would swing the frame (and
	// the whole zone) with the leaf.
	if (DoorHinge != nullptr)
	{
		// At rest, AssemblyYaw=-90 maps the hinge-side native +X to player-left
		// (-Y), while the free/handle side points to player-right (+Y). Adding the
		// positive swing rotates that free edge from +Y to -X, towards the player.
		// Open / Close / Slam / Rattle all feed this one convention.
		DoorHinge->SetRelativeRotation(FRotator(0.0f, AssemblyYawDegrees + Degrees, 0.0f));
	}
}

void AHapbeatShowcaseZ2DoorActor::ResetDoorState()
{
	State = EHapbeatZ2DoorState::Closed;
	StateElapsedSeconds = 0.0f;
	ActiveCloseDurationSeconds = CloseDurationSeconds;
	bRattling = false;
	RattleElapsedSeconds = 0.0f;
	SetDoorYaw(0.0f);
}

void AHapbeatShowcaseZ2DoorActor::OnZoneActivated()
{
	// A door left half open (or locked) when you last left the zone would be a
	// confusing thing to come back to, so entering resets it -- the counterpart
	// of Z1 re-racking its pins.
	ResetDoorState();
}

void AHapbeatShowcaseZ2DoorActor::OnZoneDeactivated()
{
	// Every entry here is stream-mode, so a transition fired just before the
	// switch would otherwise keep streaming into the next zone.
	for (UHapbeatTriggerComponent* Trigger : { OpenTrigger.Get(), CloseTrigger.Get(), SlamTrigger.Get(),
		LockTrigger.Get(), UnlockTrigger.Get(), RattleTrigger.Get() })
	{
		if (Trigger != nullptr)
		{
			Trigger->Stop();
		}
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
		const float Alpha = FMath::Clamp(
			StateElapsedSeconds / FMath::Max(ActiveCloseDurationSeconds, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
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
			// Three straight legs of RattleLegSeconds each: 0 -> +A -> -A -> 0.
			// A locked door being pushed does not oscillate like a spring, it
			// takes up its slack in one direction, then the other, then stops --
			// and a rattle this short (0.3 s in all) reads as one jolt anyway.
			const float Leg = FMath::Max(RattleLegSeconds, KINDA_SMALL_NUMBER);
			RattleElapsedSeconds += DeltaSeconds;
			if (RattleElapsedSeconds >= Leg * 3.0f)
			{
				bRattling = false;
				SetDoorYaw(0.0f);
			}
			else if (RattleElapsedSeconds < Leg)
			{
				SetDoorYaw(FMath::Lerp(0.0f, RattleAmplitudeDegrees, RattleElapsedSeconds / Leg));
			}
			else if (RattleElapsedSeconds < Leg * 2.0f)
			{
				SetDoorYaw(FMath::Lerp(RattleAmplitudeDegrees, -RattleAmplitudeDegrees,
					(RattleElapsedSeconds - Leg) / Leg));
			}
			else
			{
				SetDoorYaw(FMath::Lerp(-RattleAmplitudeDegrees, 0.0f,
					(RattleElapsedSeconds - Leg * 2.0f) / Leg));
			}
		}
		break;
	}

	default:
		break; // Closed, Open -- static, nothing to tween.
	}

	// The Showcase switcher draws a shared Slate HUD covering the key guide, the
	// zone's own state and the device footer, so a zone under it prints none of
	// this. ONE EARLY RETURN, not a guard around each line: the state line below
	// used to sit outside the per-line guard and showed on top of the shared HUD.
	// Everything below here is HUD-only.
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
	return FTransform(FRotator::ZeroRotator, PlayerSpawnCm);
}
