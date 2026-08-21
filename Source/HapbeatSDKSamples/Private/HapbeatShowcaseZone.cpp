// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZone.h"

#include "HapbeatShowcaseActor.h"

bool IHapbeatShowcaseZone::IsOwnedByShowcaseSwitcher(const AActor* ZoneActor)
{
	// The switcher spawns every zone with Owner = itself
	// (AHapbeatShowcaseActor::SpawnActiveZone), so the owner is the whole test:
	// a zone dropped into a level by hand has none.
	return ZoneActor != nullptr && Cast<AHapbeatShowcaseActor>(ZoneActor->GetOwner()) != nullptr;
}
