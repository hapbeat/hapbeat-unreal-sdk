// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatEntryRef.generated.h"

/**
 * A pick-one reference to ONE entry of an Event Map, stored as that entry's
 * stable GUID.
 *
 * Which map the entry belongs to is ALWAYS stated right next to this value --
 * the Map argument of the same function call ("Play Event (Hapbeat)"), or the
 * UHapbeatEventMap property of the same class. This struct deliberately does
 * NOT carry the map itself.
 *
 * Why the map is not bundled in here
 * ----------------------------------
 * It was, once, and it did not work. A Blueprint graph pin serializes its
 * default value as TEXT, so an object reference living INSIDE a struct default
 * is only a path string: the editor could not reliably keep what the author
 * picked (the pin kept reading back "None"), and -- worse -- a path in text
 * does not register as an asset reference, so a cooked build could ship with
 * the map missing entirely.
 *
 * The engine hits the same wall and solves it the same way: K2Node_GetDataTable
 * Row does not define a "data table + row" struct. It exposes the DataTable as
 * an ordinary OBJECT pin (which the graph stores in UEdGraphPin::DefaultObject,
 * and which therefore creates a real asset dependency) and keeps only the
 * RowName in a pin of its own, whose widget reads the neighbouring table pin to
 * populate its dropdown. This struct is the RowName half of that split.
 *
 * Why a GUID and not the event id
 * -------------------------------
 * Event ids ("<category>.<name>") are intentionally NOT unique within a map --
 * the same device-side event is often authored twice with different tuning --
 * so a name could not express WHICH of the two a call site meant. The GUID
 * stays exact across renames, retuning and reordering of the map's array.
 * The author never sees it: the editor module gives this struct a graph pin
 * and a Details row that show the entry NAME while storing the GUID.
 */
USTRUCT(BlueprintType)
struct HAPBEATSDK_API FHapbeatEntryRef
{
	GENERATED_BODY()

	/**
	 * Stable GUID of the entry (FHapbeatEventEntry::Id) inside the Event Map
	 * named next to this value. Chosen by name in the editor; never meant to be
	 * typed by hand.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat",
		meta = (Tooltip = "Entry to fire. Picked by name; stored as the entry's stable GUID so renames and reorders cannot break it."))
	FGuid EntryId;

	/** True once an entry has been chosen. */
	bool IsSet() const { return EntryId.IsValid(); }
};
