// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseBlueprintZoneActor.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

AHapbeatShowcaseBlueprintZoneActor::AHapbeatShowcaseBlueprintZoneActor()
{
	// A Blueprint zone can bind ordinary keyboard events without adding a C++
	// input wrapper. The switcher pops this input while the zone is hidden.
	AutoReceiveInput = EAutoReceiveInput::Player0;
}

void AHapbeatShowcaseBlueprintZoneActor::BeginPlay()
{
	Super::BeginPlay();

	// AutoReceiveInput is serialized on the generated Blueprint class, but the
	// Showcase switcher can enable a zone after that initial registration pass.
	// Request the Player 0 input component explicitly, as the former native Z2
	// actor did, so its Blueprint InputKey nodes are always registered in PIE.
	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		EnableInput(PlayerController);
	}
}

void AHapbeatShowcaseBlueprintZoneActor::OnZoneActivated()
{
	ReceiveZoneActivated();
}

void AHapbeatShowcaseBlueprintZoneActor::OnZoneDeactivated()
{
	ReceiveZoneDeactivated();
}
