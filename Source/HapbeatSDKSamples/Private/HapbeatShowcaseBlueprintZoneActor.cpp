// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseBlueprintZoneActor.h"

AHapbeatShowcaseBlueprintZoneActor::AHapbeatShowcaseBlueprintZoneActor()
{
	// A Blueprint zone can bind ordinary keyboard events without adding a C++
	// input wrapper. The switcher pops this input while the zone is hidden.
	AutoReceiveInput = EAutoReceiveInput::Player0;
}

void AHapbeatShowcaseBlueprintZoneActor::OnZoneActivated()
{
	ReceiveZoneActivated();
}

void AHapbeatShowcaseBlueprintZoneActor::OnZoneDeactivated()
{
	ReceiveZoneDeactivated();
}
