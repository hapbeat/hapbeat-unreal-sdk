// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatVRConfigExampleActor.h"

#include "HapbeatAddressOverridePanelComponent.h"
#include "HapbeatSampleLibrary.h"
#include "HapbeatSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "MotionControllerComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatVRConfigExample, Log, All);

AHapbeatVRConfigExampleActor::AHapbeatVRConfigExampleActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// Plain scene root so the actor has a placeable/transformable icon in the
	// editor viewport; the surface below is what actually moves at runtime.
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	PanelSurface = CreateDefaultSubobject<UWidgetComponent>(TEXT("PanelSurface"));
	PanelSurface->SetupAttachment(Root);
	PanelSurface->SetWidgetSpace(EWidgetSpace::World);
	// Draw size in Slate units; the panel's own layout decides how much of this
	// it fills. Roughly 4:3 so the stepper rows are not stretched.
	PanelSurface->SetDrawSize(FVector2D(840.0f, 460.0f));
	// Two-sided so walking around the surface -- or a follow frame that has not
	// caught up yet -- never leaves the wearer looking at an invisible panel.
	PanelSurface->SetTwoSided(true);
	// Purely a display; it must not block traces, projectiles, or the pawn.
	// WidgetInteraction traces on Visibility. QueryOnly keeps it out of all
	// physics/projectile collision while allowing that one UI ray through.
	PanelSurface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PanelSurface->SetCollisionResponseToAllChannels(ECR_Ignore);
	PanelSurface->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	// The follow rewrites its transform every frame, so it cannot be Static.
	PanelSurface->SetMobility(EComponentMobility::Movable);

	PanelComponent = CreateDefaultSubobject<UHapbeatAddressOverridePanelComponent>(TEXT("PanelComponent"));
	// P toggles the whole surface. A Close button would tear its Slate widget
	// down, leaving a visible but empty surface on the next toggle.
	PanelComponent->bShowCloseButton = false;

	RightHandController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightHandController"));
	RightHandController->SetupAttachment(Root);
	RightHandController->SetTrackingMotionSource(TEXT("Right"));

	WidgetInteraction = CreateDefaultSubobject<UWidgetInteractionComponent>(TEXT("WidgetInteraction"));
	WidgetInteraction->SetupAttachment(RightHandController);
	WidgetInteraction->InteractionSource = EWidgetInteractionSource::World;
	WidgetInteraction->TraceChannel = ECC_Visibility;
	WidgetInteraction->VirtualUserIndex = 1;
	WidgetInteraction->PointerIndex = 1;
}

void AHapbeatVRConfigExampleActor::BeginPlay()
{
	Super::BeginPlay();

	if (PanelComponent != nullptr)
	{
		if (bWorldSpacePanel)
		{
			PanelComponent->AttachToWidgetComponent(PanelSurface);
		}
		else
		{
			PanelComponent->Show();
		}
	}

	BindInput();
	AttachInteractionToPawn();
}

void AHapbeatVRConfigExampleActor::BindInput()
{
	APlayerController* PC = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)
	{
		UE_LOG(LogHapbeatVRConfigExample, Warning,
			TEXT("AHapbeatVRConfigExampleActor: no PlayerController found; input not bound. Make sure the level has a PlayerController (the default GameMode spawns one for the local player)."));
		return;
	}

	EnableInput(PC);
	if (InputComponent == nullptr)
	{
		UE_LOG(LogHapbeatVRConfigExample, Warning,
			TEXT("AHapbeatVRConfigExampleActor: EnableInput did not create an InputComponent; input not bound."));
		return;
	}

	InputComponent->BindKey(ToggleKey, IE_Pressed, this, &AHapbeatVRConfigExampleActor::HandleToggleKey);
	InputComponent->BindKey(RecenterKey, IE_Pressed, this, &AHapbeatVRConfigExampleActor::HandleRecenterKey);

	// OpenXR's motion source supplies the pose; these standard UE input keys
	// cover Quest/Touch, Vive, Windows MR, Valve Index, and gamepad fallback.
	// Every physical press becomes a left mouse click at the ray hit point.
	const TArray<FKey> PressKeys = {
		EKeys::OculusTouch_Right_Trigger_Click,
		EKeys::Vive_Right_Trigger_Click,
		EKeys::MixedReality_Right_Trigger_Click,
		EKeys::ValveIndex_Right_Trigger_Click,
		EKeys::Gamepad_RightTrigger,
	};
	for (const FKey& Key : PressKeys)
	{
		InputComponent->BindKey(Key, IE_Pressed, this, &AHapbeatVRConfigExampleActor::HandlePointerPressed);
		InputComponent->BindKey(Key, IE_Released, this, &AHapbeatVRConfigExampleActor::HandlePointerReleased);
	}
	const TArray<FKey> RecenterKeys = {
		EKeys::OculusTouch_Right_Thumbstick_Click,
		EKeys::Vive_Right_Trackpad_Click,
		EKeys::MixedReality_Right_Thumbstick_Click,
		EKeys::ValveIndex_Right_Thumbstick_Click,
	};
	for (const FKey& Key : RecenterKeys)
	{
		InputComponent->BindKey(Key, IE_Pressed, this, &AHapbeatVRConfigExampleActor::HandleRecenterKey);
	}
}

void AHapbeatVRConfigExampleActor::HandleToggleKey()
{
	if (bWorldSpacePanel)
	{
		// Toggle the SURFACE, not the panel component: tearing the widget down
		// and re-attaching it would discard the wearer's staged (not yet
		// applied) player / group edits, and in VR re-finding the panel is far
		// more work than in a mouse-driven overlay.
		if (PanelSurface != nullptr)
		{
			PanelSurface->SetVisibility(!PanelSurface->IsVisible());
		}
		return;
	}

	if (PanelComponent != nullptr)
	{
		PanelComponent->Toggle();
	}
}

void AHapbeatVRConfigExampleActor::HandleRecenterKey()
{
	RecenterPanel();
}

void AHapbeatVRConfigExampleActor::HandlePointerPressed()
{
	if (WidgetInteraction != nullptr)
	{
		WidgetInteraction->PressPointerKey(EKeys::LeftMouseButton);
	}
}

void AHapbeatVRConfigExampleActor::HandlePointerReleased()
{
	if (WidgetInteraction != nullptr)
	{
		WidgetInteraction->ReleasePointerKey(EKeys::LeftMouseButton);
	}
}

void AHapbeatVRConfigExampleActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bWorldSpacePanel && bFollowCamera)
	{
		UpdateFollow(DeltaSeconds);
	}
	if (!bInteractionAttachedToPawn)
	{
		AttachInteractionToPawn();
	}
	if (WidgetInteraction != nullptr)
	{
		WidgetInteraction->InteractionDistance = InteractionDistance;
		WidgetInteraction->bShowDebug = bShowInteractionRay;
	}

	HudRefreshTimer -= DeltaSeconds;
	if (HudRefreshTimer > 0.0f)
	{
		return;
	}
	HudRefreshTimer = HudRefreshIntervalSeconds;

	const float HudDuration = HudRefreshIntervalSeconds * 2.0f;

	FHapbeatSampleLibrary::ShowDeviceStatusLine(this, StatusHudLineKey + 1, HudDuration);

	int32 OverridePlayer = UHapbeatSubsystem::AddressOverrideDisabled;
	int32 OverrideGroup = UHapbeatSubsystem::AddressOverrideDisabled;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UHapbeatSubsystem* Subsystem = GameInstance->GetSubsystem<UHapbeatSubsystem>())
		{
			OverridePlayer = Subsystem->GetOverridePlayer();
			OverrideGroup = Subsystem->GetOverrideGroup();
		}
	}

	// -1 means "this axis does not override the target", which reads better as
	// "off" than as a number the wearer could mistake for a device address.
	const FString PlayerText = OverridePlayer == UHapbeatSubsystem::AddressOverrideDisabled
		? TEXT("off") : FString::FromInt(OverridePlayer);
	const FString GroupText = OverrideGroup == UHapbeatSubsystem::AddressOverrideDisabled
		? TEXT("off") : FString::FromInt(OverrideGroup);

	FHapbeatSampleLibrary::ShowHudLine(StatusHudLineKey,
		FString::Printf(TEXT("Hapbeat override: player=%s group=%s"), *PlayerText, *GroupText),
		FColor::Green, HudDuration);

	FHapbeatSampleLibrary::ShowHudLine(KeyGuideHudLineKey,
		FString::Printf(TEXT("Hapbeat VRConfigExample -- %s: toggle panel | panel: %s"),
			*ToggleKey.GetDisplayName().ToString(),
			bWorldSpacePanel ? TEXT("world-space") : TEXT("viewport overlay")),
		FColor::Cyan, HudDuration);
}

void AHapbeatVRConfigExampleActor::AttachInteractionToPawn()
{
	if (bInteractionAttachedToPawn || RightHandController == nullptr || GetWorld() == nullptr)
	{
		return;
	}

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	APawn* Pawn = PC != nullptr ? PC->GetPawn() : nullptr;
	USceneComponent* PawnRoot = Pawn != nullptr ? Pawn->GetRootComponent() : nullptr;
	if (PawnRoot == nullptr)
	{
		return;
	}

	RightHandController->AttachToComponent(PawnRoot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	bInteractionAttachedToPawn = true;
}

void AHapbeatVRConfigExampleActor::UpdateFollow(float DeltaSeconds)
{
	if (PanelSurface == nullptr)
	{
		return;
	}

	const APlayerController* PC = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	const APlayerCameraManager* Camera = PC != nullptr ? PC->PlayerCameraManager : nullptr;
	if (Camera == nullptr)
	{
		// Normal for the first frames of PIE, before the camera manager exists.
		// Leaving the panel where it is beats snapping it to a guessed pose.
		return;
	}

	const FVector CameraLocation = Camera->GetCameraLocation();
	const FRotator CameraRotation = Camera->GetCameraRotation();

	const FVector TargetLocation = CameraLocation + CameraRotation.Vector() * FollowDistance;
	// Aim the panel's +X back AT the camera, which is not the camera's own
	// rotation -- that would point +X away and show the viewer the back of the
	// panel (mirrored text; two-sided rendering makes it visible, not correct).
	// The readable face is on +X: UWidgetComponent::GetLocalHitLocation maps
	// widget-space (right, down) to component (-Y, -Z), and the only viewer whose
	// right-hand direction is component -Y is one looking along -X, i.e. standing
	// on the +X side. Deriving the rotation from the offset also leaves roll at 0,
	// so a head tilt slides the panel instead of rolling it -- which is what you
	// want on something worn on the face.
	const FRotator TargetRotation = (CameraLocation - TargetLocation).Rotation();

	const FVector CurrentLocation = PanelSurface->GetComponentLocation();
	const FRotator CurrentRotation = PanelSurface->GetComponentRotation();

	// Interpolating rather than hard-locking is what makes this bearable to
	// wear: a panel welded to the head cannot be looked at, only looked past.
	const bool bSnap = FollowSpeed <= 0.0f;
	const FVector NewLocation = bSnap
		? TargetLocation
		: FMath::VInterpTo(CurrentLocation, TargetLocation, DeltaSeconds, FollowSpeed);
	const FRotator NewRotation = bSnap
		? TargetRotation
		: FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaSeconds, FollowSpeed);

	PanelSurface->SetWorldLocationAndRotation(NewLocation, NewRotation);
}

void AHapbeatVRConfigExampleActor::RecenterPanel()
{
	if (PanelSurface == nullptr)
	{
		return;
	}

	const APlayerController* PC = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
	const APlayerCameraManager* Camera = PC != nullptr ? PC->PlayerCameraManager : nullptr;
	if (Camera == nullptr)
	{
		return;
	}

	const FVector CameraLocation = Camera->GetCameraLocation();
	const FVector TargetLocation = CameraLocation + Camera->GetCameraRotation().Vector() * FollowDistance;
	PanelSurface->SetWorldLocationAndRotation(TargetLocation, (CameraLocation - TargetLocation).Rotation());
}
