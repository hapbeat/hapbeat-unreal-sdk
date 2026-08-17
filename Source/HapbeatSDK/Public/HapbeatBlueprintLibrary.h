// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatEntryRef.h"   // FHapbeatEntryRef is a USTRUCT parameter — UHT needs the full type
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HapbeatBlueprintLibrary.generated.h"

class UHapbeatEventMap;
class UHapbeatStreamPlayback;

/**
 * The call site for firing an authored haptic: "Play Hapbeat Event" /
 * "Stop Hapbeat Event". This is the normal entry point from a Blueprint GRAPH,
 * and C++ may call it too -- it is exactly equivalent to
 * UHapbeatSubsystem::PlayEntry / StopEntry, which it delegates to (so the
 * Command vs Stream Clip decision stays in one place).
 *
 * Why a function library rather than methods on the subsystem: a subsystem
 * method only appears in the graph's node search once its Target is known, so
 * an author had to place "Get Hapbeat Subsystem" FIRST and drag off it before
 * "Play Hapbeat Event" existed as far as the search was concerned. A static
 * library function with meta=(WorldContext=...) is found by name from anywhere
 * in the graph and fills its own context in -- the same arrangement
 * UGameplayStatics uses, and for the same reason.
 *
 * The entry is addressed by TWO pins on purpose: an ordinary object pin for the
 * Event Map, plus FHapbeatEntryRef for the entry inside it. See
 * HapbeatEntryRef.h for why the two cannot be folded into one struct.
 */
UCLASS()
class HAPBEATSDK_API UHapbeatBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Play one entry of an Event Map.
	 *
	 * Everything the entry defines (Command vs Stream Clip, the clip, gain,
	 * target, loop) comes from the asset, so the call site only says WHICH entry
	 * and WHEN -- the whole point of the Event Map is that whoever tunes the
	 * feel never has to touch code.
	 *
	 * Prefer this over UHapbeatSubsystem::Play(EventId): a raw event id bypasses
	 * the Event Map, and with it the authored gain and target.
	 *
	 * @param Map            The Event Map asset holding the entry.
	 * @param Entry          Which entry inside Map (picked by name in the editor).
	 * @param GainMultiplier Scales the entry's authored gain for this call only.
	 * @return The stream handle for a Stream Clip entry (for live gain / pan
	 *         modulation, or to stop just this playback); null for Command, and
	 *         null when the arguments do not resolve to an entry.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Play Hapbeat Event",
			AdvancedDisplay = "3", Keywords = "haptic play event hapbeat"))
	static UHapbeatStreamPlayback* PlayHapbeatEvent(const UObject* WorldContextObject,
		UHapbeatEventMap* Map, FHapbeatEntryRef Entry, float GainMultiplier = 1.0f);

	/** Stop an entry started by Play Hapbeat Event: STOP for Command, ends the stream for Stream Clip. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Stop Hapbeat Event",
			Keywords = "haptic stop event hapbeat"))
	static void StopHapbeatEvent(const UObject* WorldContextObject,
		UHapbeatEventMap* Map, FHapbeatEntryRef Entry);
};
