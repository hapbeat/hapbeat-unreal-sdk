// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HapbeatEventEntry.h"
#include "HapbeatEventMap.generated.h"

/**
 * Central registry of haptic events for a project. Editable in the native
 * Details panel (Unreal's array UI gives add / remove / reorder / duplicate /
 * multi-edit for free) or in the dedicated window under Tools > Hapbeat Event
 * Map, which adds a list/detail split, target decomposition and Test Play.
 *
 * Triggers reference entries by stable GUID (FHapbeatEventEntry::Id) via
 * FindById, so reordering / inserting / duplicating entries cannot silently
 * break existing trigger wiring. Create via the Content Browser
 * (right-click > Hapbeat > Hapbeat Event Map).
 *
 * UE counterpart of Hapbeat.HapbeatEventMap (Unity SDK).
 */
UCLASS(BlueprintType)
class HAPBEATSDK_API UHapbeatEventMap : public UDataAsset
{
	GENERATED_BODY()

public:
	/** All haptic event definitions for this project. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat")
	TArray<FHapbeatEventEntry> Entries;

	/**
	 * Look up an entry by its stable Id. Returns true and fills OutEntry when
	 * found; returns false (OutEntry left default) for an invalid / unknown Id.
	 */
	UFUNCTION(BlueprintPure, Category = "Hapbeat", meta = (DisplayName = "Find Event Entry (Hapbeat)"))
	bool FindById(FGuid Id, FHapbeatEventEntry& OutEntry) const;

#if WITH_EDITOR
	// Assign a fresh GUID to any entry whose Id is still invalid (covers add /
	// duplicate / paste in the Details panel). Both overrides funnel through
	// AssignMissingIds so array edits via either path are covered.
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;

private:
	/** Give every entry with an invalid Id a fresh FGuid::NewGuid(). */
	void AssignMissingIds();
#endif
};
