// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HapbeatShowcaseZone.h"
#include "HapbeatShowcaseBlueprintZoneActor.generated.h"

/**
 * Minimal Showcase shell for a zone authored in Blueprint.
 *
 * It owns only the switcher's metadata and input lifecycle.  Door movement,
 * UI, and every Hapbeat call stay in the child Blueprint so it can serve as a
 * genuine Blueprint integration example rather than a C++ wrapper.
 */
UCLASS(Abstract, Blueprintable)
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseBlueprintZoneActor
	: public AActor, public IHapbeatShowcaseZone
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseBlueprintZoneActor();

	virtual int32 GetZoneIndex() const override { return ZoneIndex; }
	virtual FText GetZoneLabel() const override { return ZoneLabel; }
	virtual TArray<FHapbeatShowcaseHudCommand> GetHudCommands() const override { return HudCommands; }
	virtual FTransform GetPlayerSpawnRelative() const override { return PlayerSpawnRelative; }
	virtual bool WantsCursorUnlocked() const override { return bUnlockCursorOnEnter; }
	virtual void OnZoneActivated() override;
	virtual void OnZoneDeactivated() override;

	/** Position in the Showcase switcher's 1-9 key sequence. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hapbeat|Showcase")
	int32 ZoneIndex = 0;

	/** Short title used in the shared Showcase HUD. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hapbeat|Showcase")
	FText ZoneLabel;

	/** Zone-specific rows placed below the global key guide. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hapbeat|Showcase")
	TArray<FHapbeatShowcaseHudCommand> HudCommands;

	/** Player feet pose relative to this actor when the zone is selected. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hapbeat|Showcase")
	FTransform PlayerSpawnRelative;

	/** Release the cursor for a screen-space UI zone. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hapbeat|Showcase")
	bool bUnlockCursorOnEnter = false;

	/** Implement reset / widget creation in the child Blueprint. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Hapbeat|Showcase", meta = (DisplayName = "On Showcase Zone Activated"))
	void ReceiveZoneActivated();

	/** Implement stream stop / widget removal in the child Blueprint. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Hapbeat|Showcase", meta = (DisplayName = "On Showcase Zone Deactivated"))
	void ReceiveZoneDeactivated();
};
