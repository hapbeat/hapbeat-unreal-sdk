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
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	bool FindById(FGuid Id, FHapbeatEventEntry& OutEntry) const;

	/**
	 * Look up an entry by its computed event id ("<category>.<name>", see
	 * FHapbeatEventEntry::GetEventId). This is the only way to name an entry
	 * from a Blueprint graph or from code without hard-coding a GUID, so a call
	 * site can pick an event without an authored asset reference per event.
	 *
	 * Event ids are deliberately NOT unique: the same device-side event is often
	 * authored twice with different tuning (one-shot vs looping, different gain
	 * or target). Consequently this returns the FIRST entry whose event id
	 * matches (case-sensitive) and logs a warning listing how many matched and
	 * which one was used, so an ambiguous lookup is visible rather than silently
	 * arbitrary. When exactly one entry must be addressed, reference it by Id
	 * (FindById / UHapbeatSubsystem::PlayEntry) instead.
	 *
	 * Returns true and fills OutEntry when found; returns false (OutEntry left
	 * default) and warns when no entry carries that event id.
	 */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	bool FindByEventId(const FString& EventId, FHapbeatEventEntry& OutEntry) const;

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
