// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "HapbeatEventRef.generated.h"

// UHapbeatEventMap is referenced only via TSoftObjectPtr, so a forward
// declaration is sufficient here (same pattern as FHapbeatEventEntry's
// UHapbeatClip). The full definition is included where the pointer is resolved.
class UHapbeatEventMap;

/**
 * A pick-one reference to a single Event Map entry: which map, and which entry
 * inside it.
 *
 * This exists so a Blueprint GRAPH can name an authored haptic event, which is
 * what makes UHapbeatSubsystem::PlayEventRef ("Play Hapbeat Event") usable as
 * THE Blueprint entry point. Neither of the two obvious alternatives works at a
 * call site:
 *
 *  - PlayEntry(Map, FGuid) is exact, but a GUID cannot be typed into a graph
 *    pin by hand, so the entry could only be chosen on a component's Details
 *    panel -- never at the call site. (It remains the C++ entry point.)
 *  - Naming the entry by its event id ("<category>.<name>") can be typed, but
 *    event ids are deliberately NOT unique within a map (the same device-side
 *    event is often authored twice with different tuning), so the first match
 *    would win and the call site could not express WHICH one it meant.
 *
 * Holding the entry's GUID keeps the reference exact and stable: renaming an
 * entry, retuning it, or reordering the map's array cannot silently repoint it
 * at a different event. The editor module gives this struct a custom graph pin
 * (and a matching Details row) that shows the entry NAME while storing that
 * GUID, so the author never sees the GUID at all.
 *
 * EventMap is a TSoftObjectPtr deliberately, for two reasons:
 *  1. A pin's default value is serialized as TEXT, and a soft path round-trips
 *     through that text form exactly, where a hard object reference does not.
 *  2. It matches the runtime's existing convention -- FHapbeatEventEntry
 *     ::StreamClip is also soft and resolved with LoadSynchronous() at fire
 *     time -- so authored references keep a uniform loading story.
 */
USTRUCT(BlueprintType)
struct HAPBEATSDK_API FHapbeatEventRef
{
	GENERATED_BODY()

	/** The Event Map asset that owns the entry. Resolved with LoadSynchronous() when fired. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat",
		meta = (Tooltip = "Event Map asset holding the entry to fire."))
	TSoftObjectPtr<UHapbeatEventMap> EventMap;

	/**
	 * Stable GUID of the entry within EventMap (FHapbeatEventEntry::Id). Chosen
	 * by name in the editor; never meant to be typed by hand.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat",
		meta = (Tooltip = "Entry to fire. Picked by name; stored as the entry's stable GUID so renames and reorders cannot break it."))
	FGuid EntryId;

	/** True once both halves are set. A half-filled reference cannot resolve to an entry. */
	bool IsSet() const { return !EventMap.IsNull() && EntryId.IsValid(); }
};
