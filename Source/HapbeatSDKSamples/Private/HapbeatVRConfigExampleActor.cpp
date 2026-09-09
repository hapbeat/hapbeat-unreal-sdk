// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatVRConfigExampleActor.h"

#include "HapbeatAddressOverridePanelComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"

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
	PanelSurface->SetDrawSize(FVector2D(480.0f, 460.0f));
	// World-space widgets interpret DrawSize as centimetres at unit scale. Keep
	// the Slate layout comfortably readable without making it room-sized.
	PanelSurface->SetWorldScale3D(FVector(0.20f));
	// Two-sided so walking around the surface -- or a follow frame that has not
	// caught up yet -- never leaves the wearer looking at an invisible panel.
	PanelSurface->SetTwoSided(true);
	// Purely a display; controller navigation has no scene ray, so it cannot
	// block traces, projectiles, or the pawn.
	PanelSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PanelSurface->SetCollisionResponseToAllChannels(ECR_Ignore);
	// The follow rewrites its transform every frame, so it cannot be Static.
	PanelSurface->SetMobility(EComponentMobility::Movable);

	PanelComponent = CreateDefaultSubobject<UHapbeatAddressOverridePanelComponent>(TEXT("PanelComponent"));
	// P toggles the whole surface. A Close button would tear its Slate widget
	// down, leaving a visible but empty surface on the next toggle.
	PanelComponent->bShowCloseButton = false;

	// Enhanced Input Mapping Contexts must be rooted in /Game for OpenXR to
	// register them before its session attaches. The shipped setup script creates
	// these actions there and records the context in DefaultInput.ini.
	InteractAction = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/HapbeatVRConfig/Input/IA_HapbeatVRInteract.IA_HapbeatVRInteract")));
	NavigateUpAction = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/HapbeatVRConfig/Input/IA_HapbeatVRNavigateUp.IA_HapbeatVRNavigateUp")));
	NavigateDownAction = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/HapbeatVRConfig/Input/IA_HapbeatVRNavigateDown.IA_HapbeatVRNavigateDown")));
	NavigateLeftAction = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/HapbeatVRConfig/Input/IA_HapbeatVRNavigateLeft.IA_HapbeatVRNavigateLeft")));
	NavigateRightAction = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/HapbeatVRConfig/Input/IA_HapbeatVRNavigateRight.IA_HapbeatVRNavigateRight")));
	RecenterAction = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/HapbeatVRConfig/Input/IA_HapbeatVRRecenter.IA_HapbeatVRRecenter")));
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
		PanelComponent->ShowFocusHighlight();
	}

	BindInput();
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

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	UInputAction* Interact = InteractAction.LoadSynchronous();
	UInputAction* NavigateUp = NavigateUpAction.LoadSynchronous();
	UInputAction* NavigateDown = NavigateDownAction.LoadSynchronous();
	UInputAction* NavigateLeft = NavigateLeftAction.LoadSynchronous();
	UInputAction* NavigateRight = NavigateRightAction.LoadSynchronous();
	UInputAction* Recenter = RecenterAction.LoadSynchronous();
	if (EnhancedInput == nullptr || Interact == nullptr || NavigateUp == nullptr || NavigateDown == nullptr ||
		NavigateLeft == nullptr || NavigateRight == nullptr || Recenter == nullptr)
	{
		UE_LOG(LogHapbeatVRConfigExample, Warning,
			TEXT("AHapbeatVRConfigExampleActor: OpenXR controller navigation is not configured. Run Scripts/generate_vr_config_input_assets.py, restart the editor, then start VR Preview."));
		return;
	}

	// A trigger confirms the already-visible selection cursor. This intentionally
	// does not use WidgetInteraction or a controller pose/ray.
	EnhancedInput->BindAction(Interact, ETriggerEvent::Started, this, &AHapbeatVRConfigExampleActor::HandleActivate);
	EnhancedInput->BindAction(NavigateUp, ETriggerEvent::Started, this, &AHapbeatVRConfigExampleActor::HandleMoveUpPressed);
	EnhancedInput->BindAction(NavigateDown, ETriggerEvent::Started, this, &AHapbeatVRConfigExampleActor::HandleMoveDownPressed);
	EnhancedInput->BindAction(NavigateLeft, ETriggerEvent::Started, this, &AHapbeatVRConfigExampleActor::HandleMoveLeftPressed);
	EnhancedInput->BindAction(NavigateRight, ETriggerEvent::Started, this, &AHapbeatVRConfigExampleActor::HandleMoveRightPressed);
	EnhancedInput->BindAction(NavigateUp, ETriggerEvent::Completed, this, &AHapbeatVRConfigExampleActor::HandleMoveUpReleased);
	EnhancedInput->BindAction(NavigateDown, ETriggerEvent::Completed, this, &AHapbeatVRConfigExampleActor::HandleMoveDownReleased);
	EnhancedInput->BindAction(NavigateLeft, ETriggerEvent::Completed, this, &AHapbeatVRConfigExampleActor::HandleMoveLeftReleased);
	EnhancedInput->BindAction(NavigateRight, ETriggerEvent::Completed, this, &AHapbeatVRConfigExampleActor::HandleMoveRightReleased);
	EnhancedInput->BindAction(NavigateUp, ETriggerEvent::Canceled, this, &AHapbeatVRConfigExampleActor::HandleMoveUpReleased);
	EnhancedInput->BindAction(NavigateDown, ETriggerEvent::Canceled, this, &AHapbeatVRConfigExampleActor::HandleMoveDownReleased);
	EnhancedInput->BindAction(NavigateLeft, ETriggerEvent::Canceled, this, &AHapbeatVRConfigExampleActor::HandleMoveLeftReleased);
	EnhancedInput->BindAction(NavigateRight, ETriggerEvent::Canceled, this, &AHapbeatVRConfigExampleActor::HandleMoveRightReleased);
	EnhancedInput->BindAction(Recenter, ETriggerEvent::Started, this, &AHapbeatVRConfigExampleActor::HandleRecenterKey);
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

void AHapbeatVRConfigExampleActor::HandleActivate()
{
	if (PanelComponent != nullptr)
	{
		PanelComponent->ActivateFocused();
	}
}

void AHapbeatVRConfigExampleActor::HandleMoveUpPressed()
{
	BeginMove(FIntPoint(0, -1));
}

void AHapbeatVRConfigExampleActor::HandleMoveDownPressed()
{
	BeginMove(FIntPoint(0, 1));
}

void AHapbeatVRConfigExampleActor::HandleMoveLeftPressed()
{
	BeginMove(FIntPoint(-1, 0));
}

void AHapbeatVRConfigExampleActor::HandleMoveRightPressed()
{
	BeginMove(FIntPoint(1, 0));
}

void AHapbeatVRConfigExampleActor::HandleMoveUpReleased()
{
	EndMove(FIntPoint(0, -1));
}

void AHapbeatVRConfigExampleActor::HandleMoveDownReleased()
{
	EndMove(FIntPoint(0, 1));
}

void AHapbeatVRConfigExampleActor::HandleMoveLeftReleased()
{
	EndMove(FIntPoint(-1, 0));
}

void AHapbeatVRConfigExampleActor::HandleMoveRightReleased()
{
	EndMove(FIntPoint(1, 0));
}

void AHapbeatVRConfigExampleActor::BeginMove(FIntPoint Direction)
{
	if (PanelComponent == nullptr)
	{
		return;
	}

	// A new direction acts immediately, then repeats only when held. This is
	// intentionally identical to keyboard-navigation behaviour, not an every-
	// frame analog-axis scroll.
	ActiveMoveDirection = Direction;
	MoveRepeatTimer = 0.0f;
	PanelComponent->MoveFocus(Direction.X, Direction.Y);
}

void AHapbeatVRConfigExampleActor::EndMove(FIntPoint Direction)
{
	if (ActiveMoveDirection == Direction)
	{
		ActiveMoveDirection = FIntPoint::ZeroValue;
		MoveRepeatTimer = 0.0f;
	}
}

void AHapbeatVRConfigExampleActor::RepeatMove(float DeltaSeconds)
{
	if (PanelComponent == nullptr || ActiveMoveDirection == FIntPoint::ZeroValue)
	{
		return;
	}

	MoveRepeatTimer += DeltaSeconds;
	if (MoveRepeatTimer >= MoveRepeatIntervalSeconds)
	{
		MoveRepeatTimer = 0.0f;
		PanelComponent->MoveFocus(ActiveMoveDirection.X, ActiveMoveDirection.Y);
	}
}

void AHapbeatVRConfigExampleActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bWorldSpacePanel && bFollowCamera)
	{
		UpdateFollow(DeltaSeconds);
	}
	RepeatMove(DeltaSeconds);
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
