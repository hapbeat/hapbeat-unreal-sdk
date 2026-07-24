// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZ3FishingActor.h"

#include "HapbeatClip.h"
#include "HapbeatEventMap.h"
#include "HapbeatParameterBinding.h"
#include "HapbeatSampleLibrary.h"
#include "HapbeatSequenceComponent.h"

#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h" // EKeys::H
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatShowcaseZ3, Log, All);

namespace
{
	/** Best-effort tint via a dynamic material instance off the stock BasicShapeMaterial. Harmless
	 * no-op if "Color" isn't the base material's actual parameter name (verified against
	 * UMaterialInstanceDynamic::SetVectorParameterValue -> SetVectorParameterValueInternal, which
	 * just stores a per-instance override keyed by name with no validation against the base
	 * material's real parameter list). */
	void TintMesh(UStaticMeshComponent* Comp, const FLinearColor& Color)
	{
		if (Comp == nullptr)
		{
			return;
		}
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			if (UMaterialInstanceDynamic* Mid = Comp->CreateDynamicMaterialInstance(0, Base))
			{
				Mid->SetVectorParameterValue(TEXT("Color"), Color);
			}
		}
	}
}

AHapbeatShowcaseZ3FishingActor::AHapbeatShowcaseZ3FishingActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// Water: a flat, wide cube standing in for a translucent water surface (see class comment --
	// an opaque blue tint approximates "translucent-ish" without depending on a translucent-blend
	// base material being available at a known path).
	WaterMeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaterMesh"));
	WaterMeshComp->SetupAttachment(RootComponent);
	WaterMeshComp->SetRelativeLocation(FVector::ZeroVector);
	WaterMeshComp->SetRelativeScale3D(FVector(14.0f, 10.0f, 0.06f)); // Cube is ~100 uu/side -> ~14m x 10m x 6cm slab
	WaterMeshComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	WaterMeshComp->SetMobility(EComponentMobility::Static);

	// Rod: a thin, leaning cylinder. Purely decorative -- not geometrically linked to RodTipMeshComp
	// below (which is independently positioned/animated); precise visual alignment between the two
	// is not attempted (engine-primitive visuals are a stand-in, per the design doc's minimal-effort
	// samples principle).
	RodBaseMeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RodBaseMesh"));
	RodBaseMeshComp->SetupAttachment(RootComponent);
	RodBaseMeshComp->SetRelativeLocation(FVector(-600.0f, -350.0f, 60.0f));
	RodBaseMeshComp->SetRelativeRotation(FRotator(-55.0f, 20.0f, 0.0f));
	RodBaseMeshComp->SetRelativeScale3D(FVector(0.045f, 0.045f, 3.0f)); // thin, ~3m tall
	RodBaseMeshComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	RodBaseMeshComp->SetMobility(EComponentMobility::Static);

	// Rod tip marker: kinematic (never simulates physics), moved every Tick via SetRelativeLocation
	// (UpdateRodTipSway) -- Movable mobility is required for that.
	RodTipMeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RodTipMesh"));
	RodTipMeshComp->SetupAttachment(RootComponent);
	RodTipMeshComp->SetRelativeLocation(RodTipBaseRelativeLocation);
	RodTipMeshComp->SetRelativeScale3D(FVector(0.15f));
	RodTipMeshComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	RodTipMeshComp->SetMobility(EComponentMobility::Movable);
}

void AHapbeatShowcaseZ3FishingActor::BeginPlay()
{
	Super::BeginPlay();

	// Shared Showcase convention: a master/layout actor spaces the 5 zones out by setting only
	// FootprintOffset per zone, so apply it to the root here (all 3 decorative meshes above are
	// attached to the root and move with it automatically; SpawnShark() below accounts for it too).
	GetRootComponent()->SetRelativeLocation(FootprintOffset);

	SetupVisuals();
	SpawnShark();
	BuildEventMapAndHaptics();
	BindInput();

	PrevRodTipWorldPos = RodTipMeshComp != nullptr ? RodTipMeshComp->GetComponentLocation() : FVector::ZeroVector;
	TimeToNextWanderImpulse = FMath::FRandRange(WanderIntervalMinSeconds, WanderIntervalMaxSeconds);
}

void AHapbeatShowcaseZ3FishingActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Stop the loop (STREAM_END) + queue the release one-shot before the shark (and its Hapbeat
	// components) get torn down -- shared Showcase convention: "EndPlay stops its haptics".
	if (bHooked && HookSequenceComp != nullptr)
	{
		HookSequenceComp->Stop();
	}

	if (SharkActor != nullptr)
	{
		SharkActor->Destroy();
		SharkActor = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void AHapbeatShowcaseZ3FishingActor::SetupVisuals()
{
	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		WaterMeshComp->SetStaticMesh(CubeMesh);
	}
	TintMesh(WaterMeshComp, FLinearColor(0.05f, 0.25f, 0.5f, 0.6f));

	if (UStaticMesh* CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		RodBaseMeshComp->SetStaticMesh(CylinderMesh);
	}
	TintMesh(RodBaseMeshComp, FLinearColor(0.25f, 0.16f, 0.08f, 1.0f));

	if (UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		RodTipMeshComp->SetStaticMesh(SphereMesh);
	}
	TintMesh(RodTipMeshComp, FLinearColor(0.9f, 0.85f, 0.2f, 1.0f));
}

void AHapbeatShowcaseZ3FishingActor::SpawnShark()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	UStaticMesh* SharkMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (SharkMesh == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ3, Warning, TEXT("Z3 Fishing: could not load /Engine/BasicShapes/Cube; shark not spawned."));
		return;
	}

	// SharkLocalSpawnOffset is root-relative so the shark lands in the right spot regardless of
	// FootprintOffset (already applied to the root above by the time this runs).
	const FVector SpawnWorldLocation = GetRootComponent()->GetComponentTransform().TransformPosition(SharkLocalSpawnOffset);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	SharkActor = World->SpawnActor<AStaticMeshActor>(SpawnWorldLocation, FRotator::ZeroRotator, SpawnParams);
	if (SharkActor == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ3, Warning, TEXT("Z3 Fishing: failed to spawn the shark actor."));
		return;
	}

	SharkMeshComp = SharkActor->GetStaticMeshComponent();
	// AStaticMeshActor's component defaults to Static mobility (fine for level geometry, but a
	// simulating body must be Movable) -- flip it before enabling physics.
	SharkMeshComp->SetMobility(EComponentMobility::Movable);
	SharkMeshComp->SetStaticMesh(SharkMesh);
	SharkMeshComp->SetRelativeScale3D(FVector(1.6f, 0.55f, 0.45f)); // elongated cube ~1.6 m long -- a "shark" silhouette stand-in
	SharkMeshComp->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	SharkMeshComp->SetSimulatePhysics(true);
	// A swimming creature, not a sinking prop -- deliberate divergence from FishingController.cs's
	// useGravity=true (see class comment); buoyant, so gravity is off and damping + wander impulses
	// alone keep it moving near the water plane.
	SharkMeshComp->SetEnableGravity(false);
	SharkMeshComp->SetLinearDamping(SwimLinearDamping);
	SharkMeshComp->SetAngularDamping(SwimAngularDamping);

	TintMesh(SharkMeshComp, FLinearColor(0.35f, 0.37f, 0.4f, 1.0f));

	SharkHomeWorldLocation = SharkActor->GetActorLocation();
}

void AHapbeatShowcaseZ3FishingActor::BuildEventMapAndHaptics()
{
	EventMap = NewObject<UHapbeatEventMap>(this);

	HookStartClip = FHapbeatSampleLibrary::LoadSampleClip(this,
		TEXT("Showcase/Kit/showcase-kit/stream-clips/z3_hook_start.wav"));
	HookLoopClip = FHapbeatSampleLibrary::LoadSampleClip(this,
		TEXT("Showcase/Kit/showcase-kit/stream-clips/z3_hook_loop.wav"));
	HookReleaseClip = FHapbeatSampleLibrary::LoadSampleClip(this,
		TEXT("Showcase/Kit/showcase-kit/stream-clips/z3_hook_release.wav"));

	// Gains + manifest intensities verbatim from ShowcaseEventMap.md / showcase-kit-manifest.json:
	// all 3 are StreamClip mode, gain (authored) = 1.00; intensities 0.60 / 0.50 / 0.55 respectively.
	// Z3_hook_loop is the only one with a Parameter Binding (VelocityMagnitude -> StreamGain,
	// input 0..3 m/s, Linear, output 0..1.5) -- wired onto the shark below.
	const FHapbeatEventEntry HookStartEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z3_hook_start"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.60f, HookStartClip, TEXT("Z3_hook_start"));

	const FHapbeatEventEntry HookLoopEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z3_hook_loop"),
		/*Gain=*/1.0f, /*bLoop=*/true, /*CachedIntensity=*/0.50f, HookLoopClip, TEXT("Z3_hook_loop"));

	const FHapbeatEventEntry HookReleaseEntry = FHapbeatSampleLibrary::MakeEntry(
		EHapticMode::StreamClip, TEXT("showcase-kit"), TEXT("z3_hook_release"),
		/*Gain=*/1.0f, /*bLoop=*/false, /*CachedIntensity=*/0.55f, HookReleaseClip, TEXT("Z3_hook_release"));

	EventMap->Entries.Reset(3);
	EventMap->Entries.Add(HookStartEntry);
	EventMap->Entries.Add(HookLoopEntry);
	EventMap->Entries.Add(HookReleaseEntry);

	if (SharkActor == nullptr)
	{
		UE_LOG(LogHapbeatShowcaseZ3, Warning, TEXT("Z3 Fishing: shark actor missing; haptics not wired."));
		return;
	}

	// Both Hapbeat components live on the shark actor -- see the class comment for why
	// (ParameterBinding reads its OWNER's root velocity; PreSeedBindings() only scans its own
	// owner's components).
	HookSequenceComp = NewObject<UHapbeatSequenceComponent>(SharkActor, TEXT("Z3HookSequence"));
	HookSequenceComp->EventMap = EventMap;
	HookSequenceComp->EntryId = HookLoopEntry.Id;
	HookSequenceComp->StartEntryId = HookStartEntry.Id;
	HookSequenceComp->StopEntryId = HookReleaseEntry.Id;
	HookSequenceComp->RegisterComponent();

	// UE's world scale is 1 uu = 1 cm (Unity: 1 unit = 1 m) and GetPhysicsLinearVelocity() returns
	// uu/s, so the authored 0..3 m/s input range becomes 0..300 uu/s; OutputMin/Max are a gain
	// multiplier (unit-agnostic) and need no conversion.
	constexpr float UuPerMeter = 100.0f;
	HookVelocityBinding = NewObject<UHapbeatParameterBinding>(SharkActor, TEXT("Z3HookLoopVelocityBinding"));
	HookVelocityBinding->SourceProperty = EHapbeatBindingSource::VelocityMagnitude;
	HookVelocityBinding->InputMin = 0.0f;
	HookVelocityBinding->InputMax = 3.0f * UuPerMeter;
	HookVelocityBinding->CurveType = EHapbeatBindingCurve::Linear;
	HookVelocityBinding->OutputParameter = EHapbeatBindingOutput::StreamGain;
	HookVelocityBinding->OutputMin = 0.0f;
	HookVelocityBinding->OutputMax = 1.5f;
	HookVelocityBinding->RegisterComponent();
	// Read this zone actor's manual velocity edits (UpdateHookedLinePhysics /
	// UpdateSharkWander, both run from OUR Tick) before the binding's own TickComponent
	// samples the shark's velocity the same frame -- a same-frame ordering nicety, not a
	// correctness requirement (GetPhysicsLinearVelocity() reflects the current physics
	// state regardless of tick order; this just avoids a possible 1-frame-stale read).
	HookVelocityBinding->AddTickPrerequisiteActor(this);
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

	InputComponent->BindKey(EKeys::H, IE_Pressed, this, &AHapbeatShowcaseZ3FishingActor::HandleHKey);
}

void AHapbeatShowcaseZ3FishingActor::HandleHKey()
{
	SetHooked(!bHooked);
}

void AHapbeatShowcaseZ3FishingActor::SetHooked(bool bNewHooked)
{
	if (bNewHooked == bHooked || SharkMeshComp == nullptr)
	{
		return;
	}
	bHooked = bNewHooked;

	if (bHooked)
	{
		SharkMeshComp->SetLinearDamping(AttachedLinearDamping);
		SharkMeshComp->SetAngularDamping(AttachedAngularDamping);

		// Instant "hooked!" snap to tether range -- parity with FishingController.cs's Attach():
		// "_object.position = rodTip.position + Vector3.down * maxLineLength". ETeleportType::
		// ResetPhysics re-syncs the simulating body's transform cleanly instead of fighting the
		// solver with an ordinary (non-teleporting) SetWorldLocation.
		const FVector RodTipPos = RodTipMeshComp->GetComponentLocation();
		const FVector SnapPos = RodTipPos + FVector::DownVector * MaxLineLength;
		SharkMeshComp->SetWorldLocation(SnapPos, false, nullptr, ETeleportType::ResetPhysics);
		SharkMeshComp->SetPhysicsLinearVelocity(FVector::ZeroVector);

		if (HookSequenceComp != nullptr)
		{
			HookSequenceComp->Fire(); // Phase 1+2: hook-start one-shot, then start the hook loop
		}
	}
	else
	{
		SharkMeshComp->SetLinearDamping(SwimLinearDamping);
		SharkMeshComp->SetAngularDamping(SwimAngularDamping);

		if (HookSequenceComp != nullptr)
		{
			HookSequenceComp->Stop(); // Phase 3: stop the loop, then (after StopShotDelay) the release one-shot
		}
	}
}

void AHapbeatShowcaseZ3FishingActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (SharkMeshComp == nullptr || RodTipMeshComp == nullptr)
	{
		return; // setup failed (see the BeginPlay warnings above); nothing to simulate
	}

	ElapsedTimeSeconds += DeltaSeconds;
	UpdateRodTipSway(DeltaSeconds);
	UpdateSharkWander(DeltaSeconds);
	if (bHooked)
	{
		UpdateHookedLinePhysics(DeltaSeconds);
	}
	DrawLineVisual();
	RefreshHud(DeltaSeconds);
}

void AHapbeatShowcaseZ3FishingActor::UpdateRodTipSway(float DeltaSeconds)
{
	// A gentle, ever-present idle sway -- gives the taut-line inertia transfer (see
	// UpdateHookedLinePhysics) a non-zero rod-tip velocity to work with even with no player
	// input driving the rod (this zone has no camera/pawn dependency; see the class comment).
	const FVector Sway(
		FMath::Sin(ElapsedTimeSeconds * 0.6f) * RodTipSwayAmplitude,
		FMath::Cos(ElapsedTimeSeconds * 0.45f) * RodTipSwayAmplitude * 0.6f,
		FMath::Sin(ElapsedTimeSeconds * 0.33f) * RodTipSwayAmplitude * 0.35f);
	RodTipMeshComp->SetRelativeLocation(RodTipBaseRelativeLocation + Sway);

	const FVector CurWorldPos = RodTipMeshComp->GetComponentLocation();
	RodTipVelocity = DeltaSeconds > KINDA_SMALL_NUMBER
		? (CurWorldPos - PrevRodTipWorldPos) / DeltaSeconds
		: FVector::ZeroVector;
	PrevRodTipWorldPos = CurWorldPos;
}

void AHapbeatShowcaseZ3FishingActor::UpdateSharkWander(float DeltaSeconds)
{
	// Discrete, occasional kick -- a fixed real-time cadence (not frame-rate scaled: a "burst"
	// should feel the same regardless of framerate, and the interval itself is already real-time
	// based via the DeltaSeconds countdown).
	TimeToNextWanderImpulse -= DeltaSeconds;
	if (TimeToNextWanderImpulse <= 0.0f)
	{
		FVector RandDir = FMath::VRand();
		RandDir.Z *= 0.3f; // keep the shark mostly swimming on the horizontal, gentle vertical bob only
		RandDir.Normalize();
		SharkMeshComp->AddImpulse(RandDir * WanderImpulseSpeed, NAME_None, /*bVelChange=*/true);
		TimeToNextWanderImpulse = FMath::FRandRange(WanderIntervalMinSeconds, WanderIntervalMaxSeconds);
	}

	// Continuous (per-tick) correction -- expressed as an acceleration and integrated by
	// DeltaSeconds so it stays frame-rate independent (see UpdateHookedLinePhysics's comment for
	// the same pattern applied to the taut-line spring).
	if (!bHooked)
	{
		const FVector ToHome = SharkHomeWorldLocation - SharkMeshComp->GetComponentLocation();
		const float DistFromHome = ToHome.Size();
		if (DistFromHome > HomeLeashRadius)
		{
			const FVector Dir = ToHome / FMath::Max(DistFromHome, KINDA_SMALL_NUMBER);
			SharkMeshComp->AddImpulse(Dir * HomeLeashAccel * DeltaSeconds, NAME_None, /*bVelChange=*/true);
		}
	}
}

void AHapbeatShowcaseZ3FishingActor::UpdateHookedLinePhysics(float DeltaSeconds)
{
	const FVector RodTipPos = RodTipMeshComp->GetComponentLocation();
	const FVector SharkPos = SharkMeshComp->GetComponentLocation();
	const FVector ToShark = SharkPos - RodTipPos;
	const float Dist = ToShark.Size();

	if (Dist < MaxLineLength)
	{
		return; // slack: the shark's own wander/buoyant swim physics applies unmodified
		        // (FishingController.cs FixedUpdate, "糸が slack: 物理任せ... 何もしない")
	}

	const FVector Dir = ToShark / FMath::Max(Dist, KINDA_SMALL_NUMBER);

	// FishingController.FixedUpdate() applies these corrections once per Unity physics tick (a
	// fixed ~50 Hz cadence) with no additional dt scaling -- so its 3 additive terms are each
	// implicitly "per 1/50 s". UE's Tick runs at a variable cadence, so the ONE term that is a
	// literal port of a Unity-tuned constant (rod-tip inertia, below) is expressed as an
	// acceleration (constant x UnityReferenceHz) and integrated by DeltaSeconds: summed over any
	// 1 real second this reproduces the exact same total velocity change as 50 discrete Unity
	// ticks would, regardless of UE's actual frame rate. The spring pull-back and radial damping
	// are NOT literal ports (see class comment) so they are simply authored directly in
	// per-second units, with no extra Hz factor.
	constexpr float UnityReferenceHz = 50.0f;

	FVector Velocity = SharkMeshComp->GetPhysicsLinearVelocity();

	// 1) Rod-tip inertia transfer -- FishingController.cs:
	//    "_object.linearVelocity += rodTipVel * _rodInertiaFactor" (capped rodTipVel).
	FVector CappedRodTipVel = RodTipVelocity;
	const float RodTipSpeed = CappedRodTipVel.Size();
	if (RodTipSpeed > MaxTransferSpeed)
	{
		CappedRodTipVel *= (MaxTransferSpeed / RodTipSpeed);
	}
	Velocity += CappedRodTipVel * RodInertiaFactor * UnityReferenceHz * DeltaSeconds;

	// 2) Hooke's-law restoring force pulling the shark back toward the max-length sphere -- the
	//    "manual Hooke spring force in Tick" alternative to a physics constraint (see class
	//    comment for why a UPhysicsConstraintComponent doesn't fit). Replaces Unity's per-tick
	//    position Lerp ("Vector3.Lerp(pos, snapPos, 0.5f)") with an equivalent velocity-domain
	//    pull: F = -k * overshoot, applied as a mass-independent (bVelChange) velocity impulse
	//    scaled by DeltaSeconds (semi-implicit Euler spring integration).
	const float Overshoot = Dist - MaxLineLength;
	Velocity += -Dir * Overshoot * LineSpringStiffness * DeltaSeconds;

	// 3) Radial damping: remove part of the outward velocity component so the shark doesn't keep
	//    fighting the tether -- FishingController.cs: "linearVelocity -= dir*radialSpeed*0.7f".
	//    Clamped to never remove MORE than the current outward speed (defensive against a large
	//    DeltaSeconds flipping the direction of travel).
	const float RadialSpeed = Velocity | Dir; // FVector::operator| is the dot product
	if (RadialSpeed > 0.0f)
	{
		const float DampAmount = FMath::Min(RadialSpeed, RadialSpeed * RadialDampingFactor * DeltaSeconds);
		Velocity -= Dir * DampAmount;
	}

	SharkMeshComp->SetPhysicsLinearVelocity(Velocity);

	// Auto-release: the line snaps under too much tension (master spec: "tension exceeds a break threshold").
	if (Overshoot > BreakDistance)
	{
		SetHooked(false);
	}
}

void AHapbeatShowcaseZ3FishingActor::DrawLineVisual() const
{
	if (RodTipMeshComp == nullptr)
	{
		return;
	}
	const FVector RodTipPos = RodTipMeshComp->GetComponentLocation();
	const FVector EndPos = (bHooked && SharkMeshComp != nullptr)
		? SharkMeshComp->GetComponentLocation()
		: RodTipPos + FVector::DownVector * MaxLineLength;
	DrawDebugLine(GetWorld(), RodTipPos, EndPos, bHooked ? FColor::Green : FColor::Blue, false, -1.0f, 0, 2.0f);
}

void AHapbeatShowcaseZ3FishingActor::RefreshHud(float DeltaSeconds)
{
	HudRefreshTimer -= DeltaSeconds;
	if (HudRefreshTimer > 0.0f)
	{
		return;
	}
	HudRefreshTimer = HudRefreshIntervalSeconds;

	FHapbeatSampleLibrary::ShowHudLine(KeyGuideHudLineKey,
		TEXT("Hapbeat Showcase Z3 Fishing -- H: hook / release the shark"),
		FColor::Cyan, HudRefreshIntervalSeconds * 2.0f);

	float Distance = 0.0f;
	if (SharkMeshComp != nullptr)
	{
		Distance = FVector::Dist(SharkMeshComp->GetComponentLocation(), RodTipMeshComp->GetComponentLocation());
	}
	FHapbeatSampleLibrary::ShowHudLine(StatusHudLineKey,
		FString::Printf(TEXT("Hooked: %s | line: %.0f / %.0f uu"), bHooked ? TEXT("yes") : TEXT("no"), Distance, MaxLineLength),
		bHooked ? FColor::Orange : FColor::Silver, HudRefreshIntervalSeconds * 2.0f);
	FHapbeatSampleLibrary::ShowDeviceStatusLine(this, StatusHudLineKey + 1, HudRefreshIntervalSeconds * 2.0f);
}
