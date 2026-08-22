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
	// Unity Showcase.unity, Z2_Door subtree, converted: UE (X, Y, Z) cm =
	// (Unity z, Unity x, Unity y) x 100.

	/** Unity Door: local (0, 1, 0) with scale (1.5, 2, 0.1) -- a 1.5 x 2 x 0.1 m slab. */
	constexpr float LeafWidthCm = 150.0f;    // along UE Y
	constexpr float LeafHeightCm = 200.0f;   // along UE Z
	constexpr float LeafThicknessCm = 10.0f; // along UE X
	constexpr float LeafCentreZCm = 100.0f;  // Unity y = 1 m

	/** The hinge sits at the leaf's -Y edge, so the leaf centre is half a width away from it. */
	const FVector HingeLocalCm(0.0f, -LeafWidthCm * 0.5f, 0.0f);
	const FVector LeafCentreFromHingeCm(0.0f, LeafWidthCm * 0.5f, LeafCentreZCm);

	/** Unity DoorFrame: local (0.427, 0, 0). Native size, so no fit -- only FrameScale. */
	const FVector FrameLocalCm(0.0f, 42.7f, 0.0f);

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

	// Fixed frame, at Unity's DoorFrame offset.
	DoorFrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorFrameMesh"));
	DoorFrameMesh->SetupAttachment(RootComponent);
	DoorFrameMesh->SetMobility(EComponentMobility::Movable);
	DoorFrameMesh->SetRelativeLocation(FrameLocalCm);
	DoorFrameMesh->SetCollisionProfileName(TEXT("BlockAll"));
	// No primitive stand-in: a frame drawn as a cube would be a wall across the
	// doorway. It simply does not appear until SM_DoorFrame is imported.
	DoorFrameMesh->SetVisibility(false);

	// The pivot. SetDoorYaw turns this and nothing else, so the leaf swings
	// about its hinge-side edge while the frame stays where it is.
	DoorHinge = CreateDefaultSubobject<USceneComponent>(TEXT("DoorHinge"));
	DoorHinge->SetupAttachment(RootComponent);
	DoorHinge->SetMobility(EComponentMobility::Movable);
	DoorHinge->SetRelativeLocation(HingeLocalCm);

	DoorLeafMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorLeafMesh"));
	DoorLeafMesh->SetupAttachment(DoorHinge);
	DoorLeafMesh->SetMobility(EComponentMobility::Movable); // must stay Movable: it is rotated at runtime
	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		DoorLeafMesh->SetStaticMesh(CubeMesh);
	}
	DoorLeafMesh->SetRelativeLocation(LeafCentreFromHingeCm);
	DoorLeafMesh->SetRelativeScale3D(
		FVector(LeafThicknessCm, LeafWidthCm, LeafHeightCm) / 100.0f); // engine Cube is 100 cm authored
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
}

void AHapbeatShowcaseZ2DoorActor::BeginPlay()
{
	Super::BeginPlay();

	ApplyShowcaseAssets();
	BuildEventMap();
	BindInput();

	ResetDoorState();
}

float AHapbeatShowcaseZ2DoorActor::FitDoorPiece(UStaticMeshComponent* Component, UStaticMesh* Mesh,
	const FVector& TargetSizeCm)
{
	if (Component == nullptr || Mesh == nullptr)
	{
		return 0.0f;
	}

	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const FVector Size = Bounds.BoxExtent * 2.0f;

	// Which way round the source model runs its width is a property of the FBX,
	// not something this zone can assume: measure it. A door leaf is wide in one
	// horizontal axis and thin in the other, so whichever of X/Y is larger IS the
	// width -- and if that is X, the piece needs a quarter turn to face the way
	// this zone's doorway does (width along Y).
	const bool bWidthRunsAlongX = Size.X > Size.Y;
	const float YawDegrees = bWidthRunsAlongX ? 90.0f : 0.0f;

	// Fit in the MESH's own axes, so the target box has to be stated the same
	// way round as the mesh is before the yaw above turns it.
	const FVector MeshTarget = bWidthRunsAlongX
		? FVector(TargetSizeCm.Y, TargetSizeCm.X, TargetSizeCm.Z)
		: TargetSizeCm;
	const FVector Scale(
		MeshTarget.X / FMath::Max(Size.X, KINDA_SMALL_NUMBER),
		MeshTarget.Y / FMath::Max(Size.Y, KINDA_SMALL_NUMBER),
		MeshTarget.Z / FMath::Max(Size.Z, KINDA_SMALL_NUMBER));

	Component->SetStaticMesh(Mesh);
	Component->SetRelativeScale3D(Scale);
	Component->SetRelativeRotation(FRotator(0.0f, YawDegrees, 0.0f));
	Component->SetVisibility(true);
	return YawDegrees;
}

void AHapbeatShowcaseZ2DoorActor::ApplyShowcaseAssets()
{
	UMaterialInterface* DoorMaterial =
		FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("MI_DefaultMaterial"));

	// Three separate meshes out of Door.fbx (imported with Combine Meshes OFF).
	// The leaf has to be its own asset for any of this to work: a leaf welded to
	// its frame cannot swing.
	if (UStaticMesh* LeafMesh =
		FHapbeatSampleLibrary::LoadShowcaseAsset<UStaticMesh>(TEXT("Meshes"), TEXT("SM_Door")))
	{
		const float LeafYaw = FitDoorPiece(DoorLeafMesh, LeafMesh,
			FVector(LeafThicknessCm, LeafWidthCm, LeafHeightCm));
		if (DoorLeafMesh != nullptr)
		{
			// Sit the fitted leaf's centre exactly on the component's origin (which
			// the constructor already placed half a width from the hinge), so an
			// off-centre pivot in the source model does not push the leaf out of
			// its frame.
			DoorLeafMesh->SetRelativeLocation(LeafCentreFromHingeCm
				- FHapbeatSampleLibrary::ComputeFittedBoundsCentre(
					LeafMesh, DoorLeafMesh->GetRelativeScale3D(), FRotator(0.0f, LeafYaw, 0.0f)));
			if (DoorMaterial != nullptr)
			{
				DoorLeafMesh->SetMaterial(0, DoorMaterial);
			}
		}
	}

	if (UStaticMesh* FrameMesh =
		FHapbeatSampleLibrary::LoadShowcaseAsset<UStaticMesh>(TEXT("Meshes"), TEXT("SM_DoorFrame")))
	{
		if (DoorFrameMesh != nullptr)
		{
			// The frame is used at its authored size (only FrameScale adjusts it),
			// so it is placed rather than fitted: same yaw rule as the leaf, and its
			// bottom sat on the floor.
			const FVector Size = FrameMesh->GetBounds().BoxExtent * 2.0f;
			const float YawDegrees = Size.X > Size.Y ? 90.0f : 0.0f;
			const FRotator Rotation(0.0f, YawDegrees, 0.0f);
			const FVector Scale(FrameScale);

			DoorFrameMesh->SetStaticMesh(FrameMesh);
			DoorFrameMesh->SetRelativeScale3D(Scale);
			DoorFrameMesh->SetRelativeRotation(Rotation);
			const FVector Centre = FHapbeatSampleLibrary::ComputeFittedBoundsCentre(FrameMesh, Scale, Rotation);
			const float HalfHeight = FrameMesh->GetBounds().BoxExtent.Z * Scale.Z;
			DoorFrameMesh->SetRelativeLocation(FVector(
				FrameLocalCm.X - Centre.X,
				FrameLocalCm.Y - Centre.Y,
				FrameLocalCm.Z - Centre.Z + HalfHeight)); // bottom on the floor
			DoorFrameMesh->SetVisibility(true);
			if (DoorMaterial != nullptr)
			{
				DoorFrameMesh->SetMaterial(0, DoorMaterial);
			}
		}
	}

	if (UStaticMesh* HandleMesh =
		FHapbeatSampleLibrary::LoadShowcaseAsset<UStaticMesh>(TEXT("Meshes"), TEXT("SM_DoorHandle")))
	{
		if (DoorHandleMesh != nullptr)
		{
			// Left at its authored transform relative to the leaf -- the handle's
			// position on the door is the model's own business, and it rides the
			// leaf's swing through the attachment -- but with the leaf's
			// non-uniform fit scale divided back out, so a leaf squashed to 10 cm
			// thick does not take the handle with it.
			const FVector LeafScale = DoorLeafMesh != nullptr
				? DoorLeafMesh->GetRelativeScale3D()
				: FVector::OneVector;
			DoorHandleMesh->SetRelativeScale3D(FVector(
				1.0f / FMath::Max(FMath::Abs(LeafScale.X), KINDA_SMALL_NUMBER),
				1.0f / FMath::Max(FMath::Abs(LeafScale.Y), KINDA_SMALL_NUMBER),
				1.0f / FMath::Max(FMath::Abs(LeafScale.Z), KINDA_SMALL_NUMBER)));
			DoorHandleMesh->SetStaticMesh(HandleMesh);
			DoorHandleMesh->SetVisibility(true);
			if (DoorMaterial != nullptr)
			{
				DoorHandleMesh->SetMaterial(0, DoorMaterial);
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
		DoorHinge->SetRelativeRotation(FRotator(0.0f, Degrees, 0.0f));
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
