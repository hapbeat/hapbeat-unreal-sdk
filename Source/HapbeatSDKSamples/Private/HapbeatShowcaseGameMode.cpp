// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseGameMode.h"

#include "HapbeatShowcaseCharacter.h"

AHapbeatShowcaseGameMode::AHapbeatShowcaseGameMode()
{
	DefaultPawnClass = AHapbeatShowcaseCharacter::StaticClass();
}
