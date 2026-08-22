// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZ3FishingActor.h"

#include "HapbeatClip.h"
#include "HapbeatEventMap.h"
#include "HapbeatParameterBinding.h"
#include "HapbeatSampleLibrary.h"
#include "HapbeatSequenceComponent.h"
#include "HapbeatShowcaseCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h" // EKeys::LeftMouseButton
#include "Kismet/GameplayStatics.h" // GetPlayerPawn
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h" // FRotationMatrix::MakeFromZ, for aiming the line mesh
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatShowcaseZ3, Log, All);

namespace
{
	// Unity Showcase.unity, Z3_Fishing subtree, converted: UE (X, Y, Z) cm =
	// (Unity z, Unity x, Unity y) x 100.

	/** Unity FishingObject: local (0.5, 0.45, -0.018). */
	const FVector SharkStartCm(-1.8f, 50.0f, 45.0f);
	/** Unity FishingObject_RestPose: local (0.5, 0.15, 0). */
	const FVector SharkRestCm(0.0f, 50.0f, 15.0f);
	/** Unity Z3_Fishing/PlayerSpawn: local (0, 1, -2). Z is the player's feet, hence 0. */
	const FVector PlayerSpawnCm(-200.0f, 0.0f, 0.0f);

	/**
	 * Where the line hangs from with nobody holding the rod: roughly where a
	 * standing player's hand would be, so the shark still tethers sensibly.
	 */
	const FVector RodTipFallbackCm(60.0f, 44.0f, 175.0f);

	constexpr float SharkMassKg = 1.0f;
}

// =============================================================================
// AHapbeatShowcaseZ3FishingActor
// =============================================================================

AHapbeatShowcaseZ3FishingActor::AHapbeatShowcaseZ3FishingActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	RodTipAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("RodTipAnchor"));
	RodTipAnchor->SetupAttachment(RootComponent);
	RodTipAnchor->SetMobility(EComponentMobility::Movable); // swayed each Tick when bEnableRodTipSway
	RodTipAnchor->SetRelativeLocation(RodTipFallbackCm);

	// The fishing line as a real mesh, not DrawDebugLine: debug drawing is
	// compiled out of a Shipping build, and Unity draws this line with a
	// LineRenderer that is always there. A unit cylinder stretched between the
	// two endpoints each Tick is the cheapest equivalent.
	LineMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LineMesh"));
	LineMesh->SetupAttachment(RootComponent);
	LineMesh->SetMobility(EComponentMobility::Movable);
	if (UStaticMesh* CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		LineMesh->SetStaticMesh(CylinderMesh);
	}
	// Nothing may touch it and it casts no shadow: it is a 2 cm thread whose
	// shadow would only be noise, and a collider on it would catch the shark.
	LineMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	LineMesh->SetCastShadow(false);

	SharkRestAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("SharkRestAnchor"));
	SharkRestAnchor->SetupAttachment(RootComponent);
	SharkRestAnchor->SetRelativeLocation(SharkRestCm);

	// The shark as a child actor: an editable relative transform here, and an
	// actor of its own to carry the sequence + binding (see the class comment).
	// The pitch lays the body capsule's axis along the shark's length.
	SharkSlot = CreateDefaultSubobject<UChildActorComponent>(TEXT("Shark"));
	SharkSlot->SetupAttachment(RootComponent);
	SharkSlot->SetMobility(EComponentMobility::Movable);
	SharkSlot->SetChildActorClass(AHapbeatShowcaseZ3SharkActor::StaticClass());
	SharkSlot->SetRelativeLocation(SharkStartCm);
	SharkSlot->SetRelativeRotation(FRotator(AHapbeatShowcaseZ3SharkActor::BodyPitchDegrees, 0.0f, 0.0f));

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

void AHapbeatShowcaseZ3FishingActor::BeginPlay()
{
	Super::BeginPlay();

	SetUpShark();
	SetUpLineVisual();
	BuildEventMapAndHaptics();
	BindInput();

	PrevRodTipWorldPos = GetRodTipWorldLocation();
	TimeToNextWanderImpulse = FMath::FRandRange(WanderIntervalMinSeconds, WanderIntervalMaxSeconds);
}

void AHapbeatShowcaseZ3FishingActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Stop the loop (STREAM_END) + queue the release one-shot before the shark
	// (and its Hapbeat components) get torn down.
	OnZoneDeactivated();
	UnmountRod();
	Super::EndPlay(EndPlayReason);
}

FTransform AHapbeatShowcaseZ3FishingActor::GetSharkRestWorldTransform() const
{
	// Position from the rest anchor, orientation from the slot's authored pitch
	// (which is what lays the body capsule along the shark).
	const FVector Location = SharkRestAnchor != nullptr
		? SharkRestAnchor->GetComponentLocation()
		: GetActorLocation();
	const FQuat Rotation = SharkSlot != nullptr
		? SharkSlot->GetComponentQuat()
		: GetActorQuat();
	return FTransform(Rotation, Location);
}

void AHapbeatShowcaseZ3FishingActor::SetUpShark()
{
	Shark = SharkSlot != nullptr
		? Cast<AHapbeatShowcaseZ3SharkActor>(SharkSlot->GetChildActor())
		: nullptr;
	if (Shark == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ3, Warning,
			TEXT("Z3 Fishing: the Shark child actor is missing; this zone will do nothing."));
		return;
	}
	Shark->ApplySharkSize(SharkSizeCm);
	Shark->SetDamping(SwimLinearDamping, SwimAngularDamping);
}

void AHapbeatShowcaseZ3FishingActor::BuildEventMapAndHaptics()
{
	EventMap = EventMapOverride != nullptr ? ToRawPtr(EventMapOverride) : BuildFallbackEventMap();
	if (EventMap == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ3, Warning, TEXT("Z3 Fishing: no EventMap available; haptics not wired."));
		return;
	}

	// Look the ids up by event name. The fallback map below authors the same
	// categories / names / modes, so both paths go through this one resolution
	// step instead of duplicating the wiring.
	const FGuid HookStartId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z3_hook_start"));
	const FGuid HookLoopId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z3_hook_loop"));
	const FGuid HookReleaseId = FHapbeatSampleLibrary::FindEntryId(
		EventMap, EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z3_hook_release"));

	if (Shark == nullptr || Shark->HookSequence == nullptr)
	{
		return; // already warned in SetUpShark
	}
	Shark->HookSequence->EventMap = EventMap;
	Shark->HookSequence->EntryId = HookLoopId;
	Shark->HookSequence->StartEntryId = HookStartId;
	Shark->HookSequence->StopEntryId = HookReleaseId;

	// Read this zone actor's manual velocity edits (UpdateHookedLinePhysics /
	// UpdateSharkWander, both run from OUR Tick) before the binding's own
	// TickComponent samples the shark's velocity the same frame -- a same-frame
	// ordering nicety, not a correctness requirement.
	if (Shark->HookVelocityBinding != nullptr)
	{
		Shark->HookVelocityBinding->AddTickPrerequisiteActor(this);
	}
}

UHapbeatEventMap* AHapbeatShowcaseZ3FishingActor::BuildFallbackEventMap()
{
	UHapbeatEventMap* Fallback = NewObject<UHapbeatEventMap>(this);

	HookStartClip = FHapbeatSampleLibrary::LoadSampleClip(this,
		TEXT("Showcase/Kit/showcase-kit/stream-clips/z3_hook_start.wav"));
	HookLoopClip = FHapbeatSampleLibrary::LoadSampleClip(this,
		TEXT("Showcase/Kit/showcase-kit/stream-clips/z3_hook_loop.wav"));
	HookReleaseClip = FHapbeatSampleLibrary::LoadSampleClip(this,
		TEXT("Showcase/Kit/showcase-kit/stream-clips/z3_hook_release.wav"));

	// Gains + manifest intensities verbatim from ShowcaseEventMap.md /
	// showcase-kit-manifest.json: all 3 are StreamClip mode, gain (authored) =
	// 1.00; intensities 0.60 / 0.50 / 0.55 respectively. z3_hook_loop is the only
	// one with a Parameter Binding (VelocityMagnitude -> StreamGain, input
	// 0..3 m/s, Linear, output 0..1.5) -- that binding lives on the shark and is
	// configured there, so it applies with the shipped asset too.
	Fallback->Entries.Reset(3);
	Fallback->Entries.Add(FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z3_hook_start"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.60f, HookStartClip, TEXT("Z3_hook_start")));
	Fallback->Entries.Add(FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z3_hook_loop"),
		/*Gain=*/1.0f, /*bLoop=*/true, /*CachedIntensity=*/0.50f, HookLoopClip, TEXT("Z3_hook_loop")));
	Fallback->Entries.Add(FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z3_hook_release"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.55f, HookReleaseClip, TEXT("Z3_hook_release")));
	return Fallback;
}

void AHapbeatShowcaseZ3FishingActor::BindInput()
{
	APlayerController* PC = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ3, Warning,
			TEXT("AHapbeatShowcaseZ3FishingActor: no PlayerController found; input not bound. Make sure the level has one (the default GameMode spawns one for the local player)."));
		return;
	}

	EnableInput(PC);
	if (InputComponent == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ3, Warning,
			TEXT("AHapbeatShowcaseZ3FishingActor: EnableInput did not create an InputComponent; input not bound."));
		return;
	}

	// Hold to hook, release to let go -- Unity FishingController.HandleInput's
	// leftButton.wasPressedThisFrame / wasReleasedThisFrame pair, not a toggle.
	// Bound once; the switcher pushes and pops the whole component.
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AHapbeatShowcaseZ3FishingActor::HandleFirePressed);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AHapbeatShowcaseZ3FishingActor::HandleFireReleased);
}

void AHapbeatShowcaseZ3FishingActor::TryDeferredMount()
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
	MountRodOnCharacter();
	// The tip just moved from the fallback anchor to the player's hand; without
	// this the next frame would read that jump as an enormous rod-tip velocity.
	PrevRodTipWorldPos = GetRodTipWorldLocation();
}

void AHapbeatShowcaseZ3FishingActor::MountRodOnCharacter()
{
	AHapbeatShowcaseCharacter* Character =
		Cast<AHapbeatShowcaseCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	UStaticMesh* RodMesh = FHapbeatSampleLibrary::LoadShowcaseAsset<UStaticMesh>(
		TEXT("Meshes"), TEXT("SM_FishingRod"));
	if (Character == nullptr || RodMesh == nullptr)
	{
		// No Showcase character (bare level / default pawn) or no imported rod:
		// the line hangs from RodTipAnchor, which is what it did before there was
		// a player at all.
		return;
	}

	// Fit and align first, because the mount pose is the pose of the FITTED mesh:
	// the rod is turned so its longest axis runs forward (+X), then scaled to
	// Unity's 389 cm length, and only then placed where CameraFollowMount says.
	const FVector Scale = FHapbeatSampleLibrary::ComputeAxisFitScale(RodMesh, FVector(RodLengthCm, 0.0f, 0.0f));
	const FRotator AlignRotation = FHapbeatSampleLibrary::ComputeLongestAxisToForwardRotation(RodMesh);
	const FRotator MountRotation =
		FHapbeatSampleLibrary::UnityEulerToUERotator(RodMountUnityEulerDeg) + RodMountExtraRotation;
	// In the aligned mesh's own space: alignment picks the axis, this picks which
	// end of it leads (see bFlipRodForward).
	const FQuat FlipRotation = bFlipRodForward ? FQuat(FRotator(0.0f, 180.0f, 0.0f)) : FQuat::Identity;

	FTransform MountPose;
	MountPose.SetLocation(RodMountCameraOffsetCm);
	// Align first, then the camera-relative mount rotation on top of it.
	MountPose.SetRotation(MountRotation.Quaternion() * FlipRotation * AlignRotation.Quaternion());
	MountPose.SetScale3D(Scale);

	Character->MountItem(RodMesh, MountPose,
		FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("MI_DefaultMaterial")));
	MountedCharacter = Character;

	// Where the line hangs from, in the MOUNT component's space: the far end of
	// the rod's longest axis once fitted and aligned. Which local axis that is
	// depends on how the source model was authored, so it is read from the bounds
	// rather than assumed.
	// The SAME rotation the mesh was mounted with (alignment plus the optional
	// flip), so flipping the rod moves the line's anchor to the end that is now
	// in front instead of leaving it behind the player's head.
	const FRotator FittedRotation = (FlipRotation * AlignRotation.Quaternion()).Rotator();
	RodTipLocalOffset = !RodTipLocalOffsetOverride.IsNearlyZero()
		? RodTipLocalOffsetOverride
		: FHapbeatSampleLibrary::ComputeFittedTipOffset(RodMesh, Scale, FittedRotation);
}

void AHapbeatShowcaseZ3FishingActor::UnmountRod()
{
	// The character outlives this zone, so a rod left mounted would follow the
	// player into the next one.
	if (AHapbeatShowcaseCharacter* Character = MountedCharacter.Get())
	{
		Character->UnmountItem();
	}
	MountedCharacter.Reset();
	bMountAttempted = false;
}

FVector AHapbeatShowcaseZ3FishingActor::GetRodTipWorldLocation() const
{
	if (const AHapbeatShowcaseCharacter* Character = MountedCharacter.Get())
	{
		if (const UStaticMeshComponent* Mount = Character->GetHandMount())
		{
			// The mount's transform already carries the fit scale, and
			// RodTipLocalOffset is the tip AFTER that scale, so the offset is
			// rotated and translated but must not be scaled a second time.
			return Mount->GetComponentLocation() + Mount->GetComponentQuat().RotateVector(RodTipLocalOffset);
		}
	}
	return RodTipAnchor != nullptr ? RodTipAnchor->GetComponentLocation() : GetActorLocation();
}

void AHapbeatShowcaseZ3FishingActor::HandleFirePressed()
{
	SetHooked(true);
}

void AHapbeatShowcaseZ3FishingActor::HandleFireReleased()
{
	SetHooked(false);
}

void AHapbeatShowcaseZ3FishingActor::SetHooked(bool bNewHooked)
{
	if (bNewHooked == bHooked || Shark == nullptr)
	{
		return;
	}
	bHooked = bNewHooked;

	UCapsuleComponent* Body = Shark->GetBody();
	if (Body == nullptr)
	{
		return;
	}

	if (bHooked)
	{
		Shark->SetDamping(AttachedLinearDamping, AttachedAngularDamping);

		// Instant "hooked!" snap to tether range -- parity with
		// FishingController.cs's Attach(): "_object.position = rodTip.position +
		// Vector3.down * maxLineLength". ETeleportType::ResetPhysics re-syncs the
		// simulating body's transform cleanly instead of fighting the solver.
		const FVector SnapPos = GetRodTipWorldLocation() + FVector::DownVector * MaxLineLength;
		Body->SetWorldLocation(SnapPos, false, nullptr, ETeleportType::ResetPhysics);
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);

		if (Shark->HookSequence != nullptr)
		{
			Shark->HookSequence->Fire(); // Phase 1+2: hook-start one-shot, then the hook loop
		}
	}
	else
	{
		Shark->SetDamping(SwimLinearDamping, SwimAngularDamping);
		// Unity FishingController.Detach() snaps the object back to its rest pose,
		// so every hook starts from the same place instead of from wherever the
		// last one left it drifting.
		Shark->ResetToTransform(GetSharkRestWorldTransform());

		if (Shark->HookSequence != nullptr)
		{
			Shark->HookSequence->Stop(); // Phase 3: stop the loop, then the release one-shot
		}
	}
}

void AHapbeatShowcaseZ3FishingActor::OnZoneActivated()
{
	if (Shark != nullptr)
	{
		Shark->SetPhysicsRunning(true);
		Shark->SetDamping(SwimLinearDamping, SwimAngularDamping);
		Shark->ResetToTransform(GetSharkRestWorldTransform());
	}
	bHooked = false;
	// The rod goes back into the player's hand: the zone hands it back on the way
	// out, so it has to be re-mounted on the way in.
	bMountAttempted = false;
	PrevRodTipWorldPos = GetRodTipWorldLocation();
}

void AHapbeatShowcaseZ3FishingActor::OnZoneDeactivated()
{
	if (bHooked && Shark != nullptr && Shark->HookSequence != nullptr)
	{
		Shark->HookSequence->Stop();
	}
	bHooked = false;
	if (Shark != nullptr)
	{
		// A hidden zone has its collision off, so a still-simulating shark would
		// drift (or be pulled) somewhere unrecoverable.
		Shark->SetPhysicsRunning(false);
	}
	UnmountRod();
}

void AHapbeatShowcaseZ3FishingActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Shark == nullptr)
	{
		return; // setup failed (see the BeginPlay warning); nothing to simulate
	}

	TryDeferredMount();

	ElapsedTimeSeconds += DeltaSeconds;
	UpdateRodTip(DeltaSeconds);
	UpdateSharkWander(DeltaSeconds);
	if (bHooked)
	{
		UpdateHookedLinePhysics(DeltaSeconds);
	}
	UpdateLineVisual();
	RefreshHud(DeltaSeconds);
}

void AHapbeatShowcaseZ3FishingActor::UpdateRodTip(float DeltaSeconds)
{
	// Optional idle sway: a rod held by the player already moves because the view
	// moves (Unity's only source of rod-tip velocity), so this is only useful for
	// a zone standing on its own -- hence off by default.
	if (bEnableRodTipSway && RodTipAnchor != nullptr && !MountedCharacter.IsValid())
	{
		const FVector Sway(
			FMath::Sin(ElapsedTimeSeconds * 0.6f) * RodTipSwayAmplitude,
			FMath::Cos(ElapsedTimeSeconds * 0.45f) * RodTipSwayAmplitude * 0.6f,
			FMath::Sin(ElapsedTimeSeconds * 0.33f) * RodTipSwayAmplitude * 0.35f);
		RodTipAnchor->SetRelativeLocation(RodTipFallbackCm + Sway);
	}

	// Measured from the tip's world motion either way, so a hand-held rod feeds
	// the tension model exactly as a swaying anchor does.
	const FVector CurWorldPos = GetRodTipWorldLocation();
	RodTipVelocity = DeltaSeconds > KINDA_SMALL_NUMBER
		? (CurWorldPos - PrevRodTipWorldPos) / DeltaSeconds
		: FVector::ZeroVector;
	PrevRodTipWorldPos = CurWorldPos;
}

void AHapbeatShowcaseZ3FishingActor::UpdateSharkWander(float DeltaSeconds)
{
	if (!bEnableSharkWander)
	{
		// Unity's shark does nothing until it is pulled; that is the default here too.
		return;
	}
	UCapsuleComponent* Body = Shark->GetBody();
	if (Body == nullptr)
	{
		return;
	}

	// Discrete, occasional kick -- a fixed real-time cadence (not frame-rate
	// scaled: a "burst" should feel the same regardless of framerate, and the
	// interval itself is already real-time based via the DeltaSeconds countdown).
	TimeToNextWanderImpulse -= DeltaSeconds;
	if (TimeToNextWanderImpulse <= 0.0f)
	{
		FVector RandDir = FMath::VRand();
		RandDir.Z *= 0.3f; // mostly horizontal, gentle vertical bob only
		RandDir.Normalize();
		Body->AddImpulse(RandDir * WanderImpulseSpeed, NAME_None, /*bVelChange=*/true);
		TimeToNextWanderImpulse = FMath::FRandRange(WanderIntervalMinSeconds, WanderIntervalMaxSeconds);
	}

	// Continuous (per-tick) correction -- expressed as an acceleration and
	// integrated by DeltaSeconds so it stays frame-rate independent.
	if (!bHooked)
	{
		const FVector Home = GetSharkRestWorldTransform().GetLocation();
		const FVector ToHome = Home - Body->GetComponentLocation();
		const float DistFromHome = ToHome.Size();
		if (DistFromHome > HomeLeashRadius)
		{
			const FVector Dir = ToHome / FMath::Max(DistFromHome, KINDA_SMALL_NUMBER);
			Body->AddImpulse(Dir * HomeLeashAccel * DeltaSeconds, NAME_None, /*bVelChange=*/true);
		}
	}
}

void AHapbeatShowcaseZ3FishingActor::UpdateHookedLinePhysics(float DeltaSeconds)
{
	UCapsuleComponent* Body = Shark->GetBody();
	if (Body == nullptr)
	{
		return;
	}

	const FVector RodTipPos = GetRodTipWorldLocation();
	const FVector SharkPos = Body->GetComponentLocation();
	const FVector ToShark = SharkPos - RodTipPos;
	const float Dist = ToShark.Size();

	if (Dist < MaxLineLength)
	{
		return; // slack: the shark's own physics applies unmodified
		        // (FishingController.cs FixedUpdate, "糸が slack: 物理任せ... 何もしない")
	}

	const FVector Dir = ToShark / FMath::Max(Dist, KINDA_SMALL_NUMBER);

	// FishingController.FixedUpdate() applies these corrections once per Unity
	// physics tick (a fixed ~50 Hz cadence) with no additional dt scaling -- so
	// its 3 additive terms are each implicitly "per 1/50 s". UE's Tick runs at a
	// variable cadence, so the ONE term that is a literal port of a Unity-tuned
	// constant (rod-tip inertia, below) is expressed as an acceleration
	// (constant x UnityReferenceHz) and integrated by DeltaSeconds: summed over
	// any 1 real second this reproduces the same total velocity change as 50
	// discrete Unity ticks would, regardless of UE's actual frame rate. The
	// spring pull-back and radial damping are NOT literal ports (see the class
	// comment) so they are authored directly in per-second units.
	constexpr float UnityReferenceHz = 50.0f;

	FVector Velocity = Body->GetPhysicsLinearVelocity();

	// 1) Rod-tip inertia transfer -- FishingController.cs:
	//    "_object.linearVelocity += rodTipVel * _rodInertiaFactor" (capped rodTipVel).
	FVector CappedRodTipVel = RodTipVelocity;
	const float RodTipSpeed = CappedRodTipVel.Size();
	if (RodTipSpeed > MaxTransferSpeed)
	{
		CappedRodTipVel *= (MaxTransferSpeed / RodTipSpeed);
	}
	Velocity += CappedRodTipVel * RodInertiaFactor * UnityReferenceHz * DeltaSeconds;

	// 2) Hooke's-law restoring force pulling the shark back toward the
	//    max-length sphere -- the velocity-domain replacement for Unity's
	//    per-tick position Lerp ("Vector3.Lerp(pos, snapPos, 0.5f)"): F = -k *
	//    overshoot, applied as a mass-independent velocity change scaled by
	//    DeltaSeconds (semi-implicit Euler spring integration).
	const float Overshoot = Dist - MaxLineLength;
	Velocity += -Dir * Overshoot * LineSpringStiffness * DeltaSeconds;

	// 3) Radial damping: remove part of the outward velocity component so the
	//    shark doesn't keep fighting the tether -- FishingController.cs:
	//    "linearVelocity -= dir*radialSpeed*0.7f". Clamped to never remove MORE
	//    than the current outward speed (defensive against a large DeltaSeconds
	//    flipping the direction of travel).
	const float RadialSpeed = Velocity | Dir; // FVector::operator| is the dot product
	if (RadialSpeed > 0.0f)
	{
		const float DampAmount = FMath::Min(RadialSpeed, RadialSpeed * RadialDampingFactor * DeltaSeconds);
		Velocity -= Dir * DampAmount;
	}

	Body->SetPhysicsLinearVelocity(Velocity);

	// Auto-release when the line is overstretched. Unity has no such rule (you
	// let go when you let go), so this is opt-in.
	if (bEnableLineBreak && Overshoot > BreakDistance)
	{
		SetHooked(false);
	}
}

void AHapbeatShowcaseZ3FishingActor::SetUpLineVisual()
{
	if (LineMesh == nullptr)
	{
		return;
	}

	// Measure the cylinder rather than assume its size: the scale below is
	// "wanted size / the mesh's own size", so nothing here depends on the engine
	// primitive being a particular number of units tall.
	const UStaticMesh* Mesh = LineMesh->GetStaticMesh();
	if (Mesh == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ3, Warning,
			TEXT("Z3 Fishing: no line mesh; the fishing line will not be drawn."));
		LineMesh->SetVisibility(false);
		return;
	}
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	LineMeshLocalLengthCm = Bounds.BoxExtent.Z * 2.0f;
	LineMeshLocalDiameterCm = FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y) * 2.0f;

	// Two dynamic instances of the Showcase's own master material, so the line
	// keeps the unhooked-blue / hooked-green distinction the debug line had.
	// Absent art leaves the mesh on its default material -- the line is still
	// there, it is just one colour (see LoadShowcaseAsset's doc).
	if (UMaterialInterface* Base =
		FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("M_ShowcaseBase")))
	{
		LineSlackMaterial = UMaterialInstanceDynamic::Create(Base, this);
		if (LineSlackMaterial != nullptr)
		{
			LineSlackMaterial->SetVectorParameterValue(TEXT("Tint"), LineSlackColor);
		}
		LineHookedMaterial = UMaterialInstanceDynamic::Create(Base, this);
		if (LineHookedMaterial != nullptr)
		{
			LineHookedMaterial->SetVectorParameterValue(TEXT("Tint"), LineHookedColor);
		}
	}
}

void AHapbeatShowcaseZ3FishingActor::UpdateLineVisual()
{
	if (LineMesh == nullptr)
	{
		return;
	}

	const FVector RodTipPos = GetRodTipWorldLocation();
	const UCapsuleComponent* Body = Shark != nullptr ? Shark->GetBody() : nullptr;
	const FVector EndPos = (bHooked && Body != nullptr)
		? Body->GetComponentLocation()
		: RodTipPos + FVector::DownVector * MaxLineLength;

	const FVector Delta = EndPos - RodTipPos;
	const float Length = Delta.Size();
	const UStaticMesh* Mesh = LineMesh->GetStaticMesh();
	if (Mesh == nullptr || Length <= KINDA_SMALL_NUMBER
		|| LineMeshLocalLengthCm <= KINDA_SMALL_NUMBER || LineMeshLocalDiameterCm <= KINDA_SMALL_NUMBER)
	{
		// A zero-length line would be a degenerate scale, and a zone with no line
		// mesh has nothing to place; either way, draw nothing this frame.
		LineMesh->SetVisibility(false);
		return;
	}
	LineMesh->SetVisibility(true);

	// The cylinder's own axis is its local Z, so pointing that at the far end
	// lays the mesh along the line. MakeFromZ picks an arbitrary roll about that
	// axis, which a round cross-section does not care about.
	const FRotator Rotation = FRotationMatrix::MakeFromZ(Delta / Length).Rotator();
	const float GirthScale = LineDiameterCm / LineMeshLocalDiameterCm;
	const FVector Scale(GirthScale, GirthScale, Length / LineMeshLocalLengthCm);

	LineMesh->SetWorldScale3D(Scale);
	LineMesh->SetWorldRotation(Rotation);
	// It is the mesh's BOUNDS CENTRE that has to land on the midpoint of the two
	// endpoints, not the component's origin -- a primitive whose pivot sits at
	// one end would otherwise put the line half a length off.
	LineMesh->SetWorldLocation((RodTipPos + EndPos) * 0.5f
		- FHapbeatSampleLibrary::ComputeFittedBoundsCentre(Mesh, Scale, Rotation));

	UMaterialInterface* Wanted = bHooked ? ToRawPtr(LineHookedMaterial) : ToRawPtr(LineSlackMaterial);
	if (Wanted != nullptr && LineMesh->GetMaterial(0) != Wanted)
	{
		LineMesh->SetMaterial(0, Wanted);
	}
}

void AHapbeatShowcaseZ3FishingActor::RefreshHud(float DeltaSeconds)
{
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
		TEXT("Hapbeat Showcase Z3 Fishing -- hold LMB: hook the shark, release: let go"),
		FColor::Cyan, HudRefreshIntervalSeconds * 2.0f);

	float Distance = 0.0f;
	if (const UCapsuleComponent* Body = Shark != nullptr ? Shark->GetBody() : nullptr)
	{
		Distance = FVector::Dist(Body->GetComponentLocation(), GetRodTipWorldLocation());
	}
	FHapbeatSampleLibrary::ShowHudLine(StatusHudLineKey,
		FString::Printf(TEXT("Hooked: %s | line: %.0f / %.0f uu"),
			bHooked ? TEXT("yes") : TEXT("no"), Distance, MaxLineLength),
		bHooked ? FColor::Orange : FColor::Silver, HudRefreshIntervalSeconds * 2.0f);
	FHapbeatSampleLibrary::ShowDeviceStatusLine(this, StatusHudLineKey + 1, HudRefreshIntervalSeconds * 2.0f);
}

FText AHapbeatShowcaseZ3FishingActor::GetZoneLabel() const
{
	return FText::FromString(TEXT("Fishing"));
}

TArray<FHapbeatShowcaseHudCommand> AHapbeatShowcaseZ3FishingActor::GetHudCommands() const
{
	TArray<FHapbeatShowcaseHudCommand> Commands;
	Commands.Add({ FText::FromString(TEXT("LMB (hold)")),
		FText::FromString(TEXT("hook the shark; release to let go")) });
	return Commands;
}

FTransform AHapbeatShowcaseZ3FishingActor::GetPlayerSpawnRelative() const
{
	return FTransform(FRotator::ZeroRotator, PlayerSpawnCm);
}

// =============================================================================
// AHapbeatShowcaseZ3SharkActor
// =============================================================================

AHapbeatShowcaseZ3SharkActor::AHapbeatShowcaseZ3SharkActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Body = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Body"));
	RootComponent = Body;
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	// Unity's FishingObject Rigidbody is 1 kg; without the override UE would
	// derive a mass from the capsule's volume, which changes how the line feels.
	Body->SetMassOverrideInKg(NAME_None, SharkMassKg, /*bNewOverrideMass=*/true);
	// A hanging creature, not a sinking prop: Unity's FishingController turns
	// gravity ON at Attach, but the shark here is held by the line the whole time
	// and gravity only fights the tether, so it stays off and the damping +
	// spring do the work.
	Body->SetEnableGravity(false);
	// SetSimulatePhysics() is deferred to BeginPlay -- calling it on an
	// unregistered component is order-dependent.

	SharkMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SharkMesh"));
	SharkMesh->SetupAttachment(Body);
	SharkMesh->SetMobility(EComponentMobility::Movable);
	// The capsule is the collider; a second one inside it would fight the solver.
	SharkMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	// Both on the shark, not on the zone: the binding reads its OWNER's root
	// velocity and PreSeedBindings() only scans its own owner (see the zone's
	// class comment).
	HookSequence = CreateDefaultSubobject<UHapbeatSequenceComponent>(TEXT("HookSequence"));

	// UE's world scale is 1 uu = 1 cm (Unity: 1 unit = 1 m) and
	// GetPhysicsLinearVelocity() returns uu/s, so the authored 0..3 m/s input
	// range becomes 0..300 uu/s; OutputMin/Max are a gain multiplier
	// (unit-agnostic) and need no conversion.
	HookVelocityBinding = CreateDefaultSubobject<UHapbeatParameterBinding>(TEXT("HookVelocityBinding"));
	HookVelocityBinding->SourceProperty = EHapbeatBindingSource::VelocityMagnitude;
	HookVelocityBinding->InputMin = 0.0f;
	HookVelocityBinding->InputMax = 300.0f;
	HookVelocityBinding->CurveType = EHapbeatBindingCurve::Linear;
	HookVelocityBinding->OutputParameter = EHapbeatBindingOutput::StreamGain;
	HookVelocityBinding->OutputMin = 0.0f;
	HookVelocityBinding->OutputMax = 1.5f;
}

void AHapbeatShowcaseZ3SharkActor::BeginPlay()
{
	Super::BeginPlay();

	if (Body != nullptr)
	{
		Body->SetSimulatePhysics(true);
	}
}

void AHapbeatShowcaseZ3SharkActor::ApplySharkSize(const FVector& SizeCm)
{
	if (Body != nullptr)
	{
		// The capsule is stated in the FINISHED shark's terms: half its length
		// along the capsule axis, and half its girth as the radius. The actor's
		// BodyPitchDegrees is what lays that axis along the body.
		Body->SetCapsuleSize(FMath::Max(SizeCm.Y, SizeCm.Z) * 0.5f, SizeCm.X * 0.5f);
	}

	if (SharkMesh == nullptr)
	{
		return;
	}

	UStaticMesh* Mesh = FHapbeatSampleLibrary::LoadShowcaseAsset<UStaticMesh>(TEXT("Meshes"), TEXT("SM_Shark"));
	if (Mesh == nullptr)
	{
		// No imported art: an engine cube standing in for the silhouette, fitted
		// the same way so the zone is the same size either way.
		Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (Mesh == nullptr)
		{
			return;
		}
	}
	SharkMesh->SetStaticMesh(Mesh);

	// SM_Shark comes in with four slots named after the source .mtl
	// (Shark_Main / Shark_Dark / Shark_Light / Eyes); match on the name, and fall
	// back to the .obj's declaration order if an importer renamed them.
	FHapbeatSampleLibrary::AssignMaterialBySlotName(SharkMesh, TEXT("Dark"), 0,
		FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("MI_Shark_Dark")));
	FHapbeatSampleLibrary::AssignMaterialBySlotName(SharkMesh, TEXT("Main"), 1,
		FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("MI_Shark_Main")));
	FHapbeatSampleLibrary::AssignMaterialBySlotName(SharkMesh, TEXT("Light"), 2,
		FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("MI_Shark_Light")));
	FHapbeatSampleLibrary::AssignMaterialBySlotName(SharkMesh, TEXT("Eyes"), 3,
		FHapbeatSampleLibrary::LoadShowcaseAsset<UMaterialInterface>(TEXT("Materials"), TEXT("MI_Shark_Eyes")));

	// Fit to the finished size (longest axis = the body length), then point that
	// axis forward IN WORLD TERMS. The actor is pitched by BodyPitchDegrees to
	// lay the capsule along the body, so the mesh divides that back out.
	const FVector Scale = FHapbeatSampleLibrary::ComputeAxisFitScale(Mesh, SizeCm);
	const FQuat AlignToForward =
		FHapbeatSampleLibrary::ComputeLongestAxisToForwardRotation(Mesh).Quaternion();
	const FQuat BodyPitch = FRotator(BodyPitchDegrees, 0.0f, 0.0f).Quaternion();
	const FQuat MeshRotation = BodyPitch.Inverse() * AlignToForward;

	SharkMesh->SetRelativeScale3D(Scale);
	SharkMesh->SetRelativeRotation(MeshRotation);
	// Centre the fitted mesh in the capsule, so an off-centre pivot in the
	// imported model does not leave the shark hanging beside its body.
	SharkMesh->SetRelativeLocation(
		-FHapbeatSampleLibrary::ComputeFittedBoundsCentre(Mesh, Scale, MeshRotation.Rotator()));
}

void AHapbeatShowcaseZ3SharkActor::SetPhysicsRunning(bool bRunning)
{
	if (Body != nullptr)
	{
		Body->SetSimulatePhysics(bRunning);
	}
}

void AHapbeatShowcaseZ3SharkActor::SetDamping(float Linear, float Angular)
{
	if (Body != nullptr)
	{
		Body->SetLinearDamping(Linear);
		Body->SetAngularDamping(Angular);
	}
}

void AHapbeatShowcaseZ3SharkActor::ResetToTransform(const FTransform& RestTransform)
{
	SetActorTransform(RestTransform, false, nullptr, ETeleportType::ResetPhysics);
	if (Body != nullptr && Body->IsSimulatingPhysics())
	{
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}
