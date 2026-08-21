// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HapbeatShowcaseGameMode.generated.h"

/**
 * Spawns AHapbeatShowcaseCharacter as the player pawn; everything else is
 * GameModeBase's default. Set it as the Showcase map's World Settings ->
 * GameMode Override (or its Project Settings default) so pressing Play gives
 * the first-person player instead of the engine's flying DefaultPawn.
 *
 * Deliberately its own class rather than a config line: a sample the user can
 * open Play on has to work from the map alone, with nothing to set up first.
 */
UCLASS(meta = (DisplayName = "Hapbeat Showcase Game Mode"))
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseGameMode();
};
