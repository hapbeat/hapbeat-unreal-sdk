// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseCharacter.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h" // EKeys::*

AHapbeatShowcaseCharacter::AHapbeatShowcaseCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Capsule left at ACharacter's 34 x 88 default. Unity's controller is a
	// little wider (radius 0.5 m) and taller (height 2 m), but the UE default
	// walks through the engine's own doorway / step-height assumptions, and the
	// difference is invisible in first person.

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	// Eye 160 cm above the feet = Unity's camera at y 1.6 under the player root.
	Camera->SetRelativeLocation(FVector(0.0f, 0.0f, EyeHeightAboveCapsuleCenter));
	// The camera takes the controller's full rotation (pitch included) while
	// ACharacter's default bUseControllerRotationYaw turns the capsule with it,
	// so "forward" for movement is always where the player is looking.
	Camera->bUsePawnControlRotation = true;

	// Held in front of the camera rather than reparented every frame: UE lets a
	// component attach to the camera directly, so Unity's LateUpdate copy
	// (CameraFollowMount) is not needed -- attachment does it for free.
	HandMount = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandMount"));
	HandMount->SetupAttachment(Camera);
	HandMount->SetRelativeTransform(GetDefaultHandMountRelativeTransform());
	// Cosmetic only: it must never block the player's own capsule or a zone's
	// projectiles, and it starts empty until a zone mounts something.
	HandMount->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HandMount->SetHiddenInGame(true);
	HandMount->SetCastShadow(false);

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = MoveSpeed;
		// Gravity, step height, braking: engine defaults, as Unity leaves its
		// own -9.81 gravity alone.
	}

	// First person with no body: the inherited skeletal mesh has nothing
	// assigned, but hiding it keeps a project that assigns one from having a
	// head in the camera.
	if (USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		BodyMesh->SetHiddenInGame(true);
	}
}

void AHapbeatShowcaseCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Re-apply the editable values, so changing them on the placed instance
	// takes effect without recompiling the constructor defaults in.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = MoveSpeed;
	}
	if (Camera != nullptr)
	{
		Camera->SetRelativeLocation(FVector(0.0f, 0.0f, EyeHeightAboveCapsuleCenter));
	}
}

void AHapbeatShowcaseCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();

	// The camera manager only exists once a player controller owns this pawn,
	// which is what this callback marks.
	ApplyViewPitchLimit();
	// Cursor captured by default, like Unity's Start() with _alwaysLook = true.
	SetCursorUnlocked(false);
}

void AHapbeatShowcaseCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (PlayerInputComponent == nullptr)
	{
		return;
	}

	// WASD and the arrow keys both, exactly like Unity's HandleMove.
	PlayerInputComponent->BindKey(EKeys::W, IE_Pressed, this, &AHapbeatShowcaseCharacter::OnForwardPressed);
	PlayerInputComponent->BindKey(EKeys::W, IE_Released, this, &AHapbeatShowcaseCharacter::OnForwardReleased);
	PlayerInputComponent->BindKey(EKeys::Up, IE_Pressed, this, &AHapbeatShowcaseCharacter::OnForwardPressed);
	PlayerInputComponent->BindKey(EKeys::Up, IE_Released, this, &AHapbeatShowcaseCharacter::OnForwardReleased);

	PlayerInputComponent->BindKey(EKeys::S, IE_Pressed, this, &AHapbeatShowcaseCharacter::OnBackPressed);
	PlayerInputComponent->BindKey(EKeys::S, IE_Released, this, &AHapbeatShowcaseCharacter::OnBackReleased);
	PlayerInputComponent->BindKey(EKeys::Down, IE_Pressed, this, &AHapbeatShowcaseCharacter::OnBackPressed);
	PlayerInputComponent->BindKey(EKeys::Down, IE_Released, this, &AHapbeatShowcaseCharacter::OnBackReleased);

	PlayerInputComponent->BindKey(EKeys::A, IE_Pressed, this, &AHapbeatShowcaseCharacter::OnLeftPressed);
	PlayerInputComponent->BindKey(EKeys::A, IE_Released, this, &AHapbeatShowcaseCharacter::OnLeftReleased);
	PlayerInputComponent->BindKey(EKeys::Left, IE_Pressed, this, &AHapbeatShowcaseCharacter::OnLeftPressed);
	PlayerInputComponent->BindKey(EKeys::Left, IE_Released, this, &AHapbeatShowcaseCharacter::OnLeftReleased);

	PlayerInputComponent->BindKey(EKeys::D, IE_Pressed, this, &AHapbeatShowcaseCharacter::OnRightPressed);
	PlayerInputComponent->BindKey(EKeys::D, IE_Released, this, &AHapbeatShowcaseCharacter::OnRightReleased);
	PlayerInputComponent->BindKey(EKeys::Right, IE_Pressed, this, &AHapbeatShowcaseCharacter::OnRightPressed);
	PlayerInputComponent->BindKey(EKeys::Right, IE_Released, this, &AHapbeatShowcaseCharacter::OnRightReleased);

	// Tab, not Esc: the editor swallows Esc to stop PIE (the same reason Unity's
	// SimpleFPSController.HandleEscape uses Tab).
	PlayerInputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AHapbeatShowcaseCharacter::OnTabPressed);

	// Raw mouse axes -- no Input Action asset, no axis mapping in DefaultInput.ini.
	PlayerInputComponent->BindAxisKey(EKeys::MouseX, this, &AHapbeatShowcaseCharacter::OnMouseX);
	PlayerInputComponent->BindAxisKey(EKeys::MouseY, this, &AHapbeatShowcaseCharacter::OnMouseY);
}

void AHapbeatShowcaseCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Cursor released = UI mode: Unity stops moving the player as well as
	// looking, so the same keys can drive on-screen UI (Z4).
	if (bCursorUnlocked)
	{
		return;
	}

	const float Forward = (bForwardHeld ? 1.0f : 0.0f) - (bBackHeld ? 1.0f : 0.0f);
	const float Right = (bRightHeld ? 1.0f : 0.0f) - (bLeftHeld ? 1.0f : 0.0f);
	if (FMath::IsNearlyZero(Forward) && FMath::IsNearlyZero(Right))
	{
		return;
	}

	// Normalised so diagonals are not faster, matching Unity's dir.normalized;
	// speed itself comes from MaxWalkSpeed.
	const FVector Direction =
		(GetActorForwardVector() * Forward + GetActorRightVector() * Right).GetSafeNormal();
	AddMovementInput(Direction, 1.0f);
}

void AHapbeatShowcaseCharacter::OnForwardPressed() { bForwardHeld = true; }
void AHapbeatShowcaseCharacter::OnForwardReleased() { bForwardHeld = false; }
void AHapbeatShowcaseCharacter::OnBackPressed() { bBackHeld = true; }
void AHapbeatShowcaseCharacter::OnBackReleased() { bBackHeld = false; }
void AHapbeatShowcaseCharacter::OnLeftPressed() { bLeftHeld = true; }
void AHapbeatShowcaseCharacter::OnLeftReleased() { bLeftHeld = false; }
void AHapbeatShowcaseCharacter::OnRightPressed() { bRightHeld = true; }
void AHapbeatShowcaseCharacter::OnRightReleased() { bRightHeld = false; }

void AHapbeatShowcaseCharacter::OnTabPressed()
{
	SetCursorUnlocked(!bCursorUnlocked);
}

void AHapbeatShowcaseCharacter::OnMouseX(float AxisValue)
{
	if (bCursorUnlocked || FMath::IsNearlyZero(AxisValue))
	{
		return;
	}
	AddControllerYawInput(AxisValue * LookSensitivity);
}

void AHapbeatShowcaseCharacter::OnMouseY(float AxisValue)
{
	if (bCursorUnlocked || FMath::IsNearlyZero(AxisValue))
	{
		return;
	}
	// NEGATED: UE's MouseY axis is positive when the mouse moves DOWN the
	// screen (the engine's own ADefaultPawn maps it with a -1 scale before
	// feeding AddControllerPitchInput), while positive UE pitch looks UP. Unity
	// reaches the same result with `_pitch -= my`.
	AddControllerPitchInput(-AxisValue * LookSensitivity);
}

void AHapbeatShowcaseCharacter::ApplyViewPitchLimit()
{
	APlayerController* PC = GetOwningPlayerController();
	if (PC == nullptr || PC->PlayerCameraManager == nullptr)
	{
		return;
	}
	// The camera manager clamps the view rotation every frame in
	// UpdateRotation -> ProcessViewRotation -> LimitViewPitch, so setting the
	// limits here is what makes +-85 real; nothing else needs to clamp.
	PC->PlayerCameraManager->ViewPitchMin = -ViewPitchLimitDegrees;
	PC->PlayerCameraManager->ViewPitchMax = ViewPitchLimitDegrees;
}

void AHapbeatShowcaseCharacter::SetCursorUnlocked(bool bUnlocked)
{
	bCursorUnlocked = bUnlocked;

	APlayerController* PC = GetOwningPlayerController();
	if (PC == nullptr)
	{
		return;
	}

	PC->bShowMouseCursor = bUnlocked;
	if (bUnlocked)
	{
		// GameAndUI rather than UIOnly: the zone switcher's number keys, Q and P
		// must keep working while a UI zone has the cursor.
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void AHapbeatShowcaseCharacter::TeleportToSpawn(const FTransform& WorldSpawn)
{
	// TeleportPhysics so the capsule is not swept through whatever is between
	// here and there (Unity disables the CharacterController for the same
	// reason).
	SetActorLocationAndRotation(WorldSpawn.GetLocation(), WorldSpawn.GetRotation(),
		/*bSweep=*/false, /*OutSweepHitResult=*/nullptr, ETeleportType::TeleportPhysics);

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->Velocity = FVector::ZeroVector;
	}

	// Yaw follows the spawn, pitch goes back to level: Unity's ResetLook only
	// zeroes the pitch and leaves the body rotation to the spawn transform.
	if (APlayerController* PC = GetOwningPlayerController())
	{
		FRotator ViewRotation = WorldSpawn.GetRotation().Rotator();
		ViewRotation.Pitch = 0.0f;
		ViewRotation.Roll = 0.0f;
		PC->SetControlRotation(ViewRotation);
	}
}

FTransform AHapbeatShowcaseCharacter::GetViewTransform() const
{
	if (Camera == nullptr)
	{
		return GetActorTransform();
	}
	return FTransform(Camera->GetComponentQuat(), Camera->GetComponentLocation());
}

void AHapbeatShowcaseCharacter::MountItem(UStaticMesh* InMesh, const FTransform& RelativeToCamera,
	UMaterialInterface* OptionalMaterial)
{
	if (HandMount == nullptr)
	{
		return;
	}

	if (InMesh == nullptr)
	{
		UnmountItem();
		return;
	}

	HandMount->SetStaticMesh(InMesh);
	if (OptionalMaterial != nullptr)
	{
		HandMount->SetMaterial(0, OptionalMaterial);
	}
	// Identity means "no opinion" -- the caller wants the standard held-item
	// pose rather than a mesh sitting inside the camera.
	HandMount->SetRelativeTransform(RelativeToCamera.Equals(FTransform::Identity)
		? GetDefaultHandMountRelativeTransform()
		: RelativeToCamera);
	HandMount->SetHiddenInGame(false);
}

void AHapbeatShowcaseCharacter::UnmountItem()
{
	if (HandMount == nullptr)
	{
		return;
	}
	HandMount->SetHiddenInGame(true);
	HandMount->SetStaticMesh(nullptr);
}

APlayerController* AHapbeatShowcaseCharacter::GetOwningPlayerController() const
{
	return Cast<APlayerController>(GetController());
}
