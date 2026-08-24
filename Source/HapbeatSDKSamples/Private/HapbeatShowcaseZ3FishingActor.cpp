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

	/**
	 * The shark, 3 m in front of the player rather than at Unity's own X.
	 *
	 * Unity's FishingObject sits at local (0.5, 0.45, -0.018) -- essentially on
	 * top of the spawn point, which in first person put it under the camera and
	 * out of frame. UE's spawn is 2 m back of the zone origin, so +100 cm along X
	 * places it 3 m ahead of the player, where a cast lands and where it can
	 * actually be seen. The rest pose follows it, so releasing the fish does not
	 * teleport it somewhere else.
	 */
	const FVector SharkStartCm(100.0f, 50.0f, 45.0f);
	/** Where Detach() puts the shark back: the same spot, at resting height. */
	const FVector SharkRestCm(100.0f, 50.0f, 35.0f);
	/** Unity Z3_Fishing/PlayerSpawn: local (0, 1, -2). Z is the player's feet, hence 0. */
	const FVector PlayerSpawnCm(-200.0f, 0.0f, 0.0f);

	/**
	 * Where the line hangs from with nobody holding the rod: roughly where a
	 * standing player's hand would be, so the shark still tethers sensibly.
	 */
	const FVector RodTipFallbackCm(60.0f, 44.0f, 175.0f);

	/**
	 * The cadence FishingController.cs's per-tick constants were authored at
	 * (Unity's default fixed timestep, 50 Hz). UE ticks at a variable rate, so a
	 * "per tick" fraction has to be re-expressed against this to mean the same
	 * thing at any frame rate.
	 */
	constexpr float UnityReferenceHz = 50.0f;
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
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RodMesh(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_FishingRod.SM_FishingRod"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HeldMaterial(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_DefaultMaterial.MI_DefaultMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> LineMaterial(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/M_ShowcaseBase.M_ShowcaseBase"));
	RodMeshAsset = RodMesh.Object;
	HeldItemMaterial = HeldMaterial.Object;
	LineBaseMaterial = LineMaterial.Object;
	if (LineBaseMaterial != nullptr)
	{
		LineMesh->SetMaterial(0, LineBaseMaterial);
	}
}

void AHapbeatShowcaseZ3FishingActor::BeginPlay()
{
	Super::BeginPlay();

	SetUpShark();
	SetUpLineVisual();
	BuildEventMapAndHaptics();
	BindInput();
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
	Shark->SnapToTransform(GetSharkRestWorldTransform());
	if (UCapsuleComponent* Body = Shark->GetBody())
	{
		OriginalLinearDamping = Body->GetLinearDamping();
		OriginalAngularDamping = Body->GetAngularDamping();
	}

	// The zone writes the body's velocity before the binding samples it.
	if (Shark->HookVelocityBinding != nullptr)
	{
		Shark->HookVelocityBinding->AddTickPrerequisiteActor(this);
	}
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
	// The tick ordering the binding depends on is set up in SetUpShark, with the
	// rest of the shark's wiring.
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
}

void AHapbeatShowcaseZ3FishingActor::MountRodOnCharacter()
{
	AHapbeatShowcaseCharacter* Character =
		Cast<AHapbeatShowcaseCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	UStaticMesh* RodMesh = RodMeshAsset;
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

	Character->MountItem(RodMesh, MountPose, HeldItemMaterial);
	MountedCharacter = Character;
	// Where the line hangs from is NOT guessed here: GetRodTipWorldLocation reads
	// the explicit RodTip socket (Unity uses an explicit RodTip Transform too),
	// with a model-local source-coordinate fallback for old generated assets.
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
			if (!RodTipSocketName.IsNone() && Mount->DoesSocketExist(RodTipSocketName))
			{
				return Mount->GetSocketLocation(RodTipSocketName);
			}

			if (bUseRodTipMeshLocalOffsetFallback)
			{
				return Mount->GetComponentTransform().TransformPosition(RodTipMeshLocalOffset);
			}
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

void AHapbeatShowcaseZ3FishingActor::DebugSetHookedForCapture(bool bNewHooked)
{
	SetHooked(bNewHooked);
}

FVector AHapbeatShowcaseZ3FishingActor::DebugGetRodTipWorldLocation() const
{
	return GetRodTipWorldLocation();
}

void AHapbeatShowcaseZ3FishingActor::SetHooked(bool bNewHooked)
{
	if (bNewHooked == bHooked || Shark == nullptr)
	{
		return;
	}
	bHooked = bNewHooked;

	if (bHooked)
	{
		UCapsuleComponent* Body = Shark->GetBody();
		if (Body != nullptr)
		{
			Body->SetSimulatePhysics(true);
			Body->SetEnableGravity(true);
			Body->SetLinearDamping(AttachedLinearDamping);
			Body->SetAngularDamping(AttachedAngularDamping);
		}

		// Instant "hooked!" snap to tether range -- parity with
		// FishingController.cs's Attach(): "_object.position = rodTip.position +
		// Vector3.down * maxLineLength".
		FTransform SnapTransform = Shark->GetActorTransform();
		SnapTransform.SetLocation(GetRodTipWorldLocation() + FVector::DownVector * MaxLineLength);
		Shark->SnapToTransform(SnapTransform);
		PrevRodTipLocation = GetRodTipWorldLocation();
		bHasPrevRodTipLocation = true;

		if (Shark->HookSequence != nullptr)
		{
			Shark->HookSequence->Fire(); // Phase 1+2: hook-start one-shot, then the hook loop
		}
	}
	else
	{
		if (UCapsuleComponent* Body = Shark->GetBody())
		{
			Body->SetLinearDamping(OriginalLinearDamping);
			Body->SetAngularDamping(OriginalAngularDamping);
		}
		// Unity FishingController.Detach() snaps the object back to its rest pose,
		// so every hook starts from the same place instead of from wherever the
		// last one left it.
		Shark->SnapToTransform(GetSharkRestWorldTransform());

		if (Shark->HookSequence != nullptr)
		{
			Shark->HookSequence->Stop(); // Phase 3: stop the loop, then the release one-shot
		}
		bHasPrevRodTipLocation = false;
	}
}

void AHapbeatShowcaseZ3FishingActor::OnZoneActivated()
{
	if (Shark != nullptr)
	{
		Shark->SetPhysicsEnabled(true);
		Shark->SnapToTransform(GetSharkRestWorldTransform());
	}
	bHooked = false;
	bHasPrevRodTipLocation = false;
	// The rod goes back into the player's hand: the zone hands it back on the way
	// out, so it has to be re-mounted on the way in.
	bMountAttempted = false;
}

void AHapbeatShowcaseZ3FishingActor::OnZoneDeactivated()
{
	if (bHooked && Shark != nullptr && Shark->HookSequence != nullptr)
	{
		Shark->HookSequence->Stop();
	}
	bHooked = false;
	bHasPrevRodTipLocation = false;
	if (Shark != nullptr)
	{
		if (UCapsuleComponent* Body = Shark->GetBody())
		{
			Body->SetLinearDamping(OriginalLinearDamping);
			Body->SetAngularDamping(OriginalAngularDamping);
		}
		Shark->SnapToTransform(GetSharkRestWorldTransform());
		Shark->SetPhysicsEnabled(false);
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
}

void AHapbeatShowcaseZ3FishingActor::UpdateHookedLinePhysics(float DeltaSeconds)
{
	UCapsuleComponent* Body = Shark->GetBody();
	if (Body == nullptr || !Body->IsSimulatingPhysics() || DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector RodTipPos = GetRodTipWorldLocation();
	if (!bHasPrevRodTipLocation)
	{
		PrevRodTipLocation = RodTipPos;
		bHasPrevRodTipLocation = true;
	}
	const FVector RodTipDelta = RodTipPos - PrevRodTipLocation;
	PrevRodTipLocation = RodTipPos;

	const FVector SharkPos = Body->GetComponentLocation();
	const FVector ToShark = SharkPos - RodTipPos;
	const float Dist = ToShark.Size();

	if (Dist < MaxLineLength)
	{
		// Slack line: gravity and ordinary rigid-body motion continue untouched.
		return;
	}

	const FVector Direction = ToShark / FMath::Max(Dist, KINDA_SMALL_NUMBER);
	FVector RodTipVelocity = RodTipDelta / DeltaSeconds;
	RodTipVelocity = RodTipVelocity.GetClampedToMaxSize(MaxTransferSpeed);
	FVector SharkVelocity = Body->GetPhysicsLinearVelocity() + RodTipVelocity * RodInertiaFactor;
	const FVector Target = RodTipPos + Direction * MaxLineLength;

	// Unity closes half the remaining distance PER PHYSICS TICK. Applied per
	// FRAME that would be a different pull at every frame rate, so it is
	// converted: keeping (1 - lerp) of the distance per tick means keeping
	// (1 - lerp)^(ticks elapsed) of it over this frame.
	const float Keep = FMath::Clamp(1.0f - LineFollowLerpPerTick, 0.0f, 1.0f);
	const float Alpha = 1.0f - FMath::Pow(Keep, DeltaSeconds * UnityReferenceHz);

	Body->SetWorldLocation(FMath::Lerp(SharkPos, Target, Alpha), /*bSweep=*/false,
		nullptr, ETeleportType::TeleportPhysics);

	// Remove 70% of velocity that is trying to lengthen an already-taut line.
	const float OutwardSpeed = FVector::DotProduct(SharkVelocity, Direction);
	if (OutwardSpeed > 0.0f)
	{
		SharkVelocity -= Direction * OutwardSpeed * 0.7f;
	}
	Body->SetPhysicsLinearVelocity(SharkVelocity);

	// Auto-release when the line is overstretched. Unity has no such rule (you
	// let go when you let go), so this is opt-in.
	if (bEnableLineBreak && (Dist - MaxLineLength) > BreakDistance)
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

	// Two dynamic instances of the constructor-loaded Showcase master, so the line
	// keeps the unhooked-blue / hooked-green distinction the debug line had.
	if (LineBaseMaterial != nullptr)
	{
		LineSlackMaterial = UMaterialInstanceDynamic::Create(LineBaseMaterial, this);
		if (LineSlackMaterial != nullptr)
		{
			LineSlackMaterial->SetVectorParameterValue(TEXT("Tint"), LineSlackColor);
		}
		LineHookedMaterial = UMaterialInstanceDynamic::Create(LineBaseMaterial, this);
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
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->BodyInstance.bSimulatePhysics = true;
	Body->BodyInstance.bEnableGravity = true;
	Body->SetLinearDamping(0.0f);
	Body->SetAngularDamping(0.05f);

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
	HookVelocityBinding->TargetTrigger = HookSequence;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SharkAsset(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Meshes/SM_Shark.SM_Shark"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SharkMain(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_Shark_Main.MI_Shark_Main"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SharkDark(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_Shark_Dark.MI_Shark_Dark"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SharkLight(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_Shark_Light.MI_Shark_Light"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SharkEyes(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_Shark_Eyes.MI_Shark_Eyes"));
	if (SharkAsset.Succeeded())
	{
		SharkMesh->SetStaticMesh(SharkAsset.Object);
		const TArray<FStaticMaterial>& Slots = SharkAsset.Object->GetStaticMaterials();
		for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			const FString SlotName = Slots[SlotIndex].MaterialSlotName.ToString()
				+ TEXT("|") + Slots[SlotIndex].ImportedMaterialSlotName.ToString();
			UMaterialInterface* SlotMaterial = SharkMain.Object;
			if (SlotName.Contains(TEXT("Eyes")))
			{
				SlotMaterial = SharkEyes.Object;
			}
			else if (SlotName.Contains(TEXT("Shark_Dark")))
			{
				SlotMaterial = SharkDark.Object;
			}
			else if (SlotName.Contains(TEXT("Shark_Light")))
			{
				SlotMaterial = SharkLight.Object;
			}
			SharkMesh->SetMaterial(SlotIndex, SlotMaterial);
		}
	}
}

void AHapbeatShowcaseZ3SharkActor::BeginPlay()
{
	Super::BeginPlay();
	if (Body != nullptr)
	{
		Body->SetMassOverrideInKg(NAME_None, 1.0f, true);
		Body->SetSimulatePhysics(true);
		Body->SetEnableGravity(true);
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

	UStaticMesh* Mesh = SharkMesh->GetStaticMesh();
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

	// Fit to the finished size (longest axis = the body length), then point that
	// axis forward IN WORLD TERMS. The actor is pitched by BodyPitchDegrees to
	// lay the capsule along the body, so the mesh divides that back out.
	const FVector Scale = FHapbeatSampleLibrary::ComputeAxisFitScale(Mesh, SizeCm);
	const FQuat AlignToForward =
		FHapbeatSampleLibrary::ComputeLongestAxisToForwardRotation(Mesh).Quaternion();
	const FQuat BodyPitch = FRotator(BodyPitchDegrees, 0.0f, 0.0f).Quaternion();
	// SM_Shark's semantic up is local +Y. Longest-axis alignment points its
	// local +Z length forward but leaves +Y pointing sideways, which makes the
	// hooked shark look rolled 90 degrees onto its side. UE Roll -90 maps that
	// local +Y onto world +Z; divide the body's capsule pitch out as before.
	const FQuat UprightRoll = FRotator(0.0f, 0.0f, 90.0f).Quaternion();
	const FQuat MeshRotation = BodyPitch.Inverse() * UprightRoll * AlignToForward;

	SharkMesh->SetRelativeScale3D(Scale);
	SharkMesh->SetRelativeRotation(MeshRotation);
	// Centre the fitted mesh in the capsule, so an off-centre pivot in the
	// imported model does not leave the shark hanging beside its body.
	SharkMesh->SetRelativeLocation(
		-FHapbeatSampleLibrary::ComputeFittedBoundsCentre(Mesh, Scale, MeshRotation.Rotator()));
}

void AHapbeatShowcaseZ3SharkActor::SnapToTransform(const FTransform& NewTransform)
{
	SetActorTransform(NewTransform, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	if (Body != nullptr)
	{
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}

void AHapbeatShowcaseZ3SharkActor::SetPhysicsEnabled(bool bEnabled)
{
	if (Body == nullptr)
	{
		return;
	}
	if (!bEnabled)
	{
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
	Body->SetSimulatePhysics(bEnabled);
	Body->SetEnableGravity(bEnabled);
}
