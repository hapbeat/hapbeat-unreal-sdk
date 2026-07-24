// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatEventEntry.h" // EHapticMode

class UHapbeatEventMap;
class FJsonObject;

/**
 * Resolution key for a Kit manifest event: (event id, mode). schema 2.0.0
 * keeps Command-mode events in the "events" bucket and StreamClip-mode events
 * in "stream_events" as two independent dictionaries, and the SAME event id
 * may validly appear in both (Studio's "BOTH" authoring mode) with
 * independently-authored parameters.intensity per bucket. Matching an
 * FHapbeatEventEntry therefore requires the (id, mode) TUPLE, not the id
 * alone -- mirrors HapbeatManifestIntensity.TryMatchByEventIdAndMode (Unity
 * SDK editor).
 */
struct FHapbeatManifestEventKey
{
	FString EventId;
	EHapticMode Mode = EHapticMode::Command;

	bool operator==(const FHapbeatManifestEventKey& Other) const
	{
		return Mode == Other.Mode && EventId.Equals(Other.EventId, ESearchCase::CaseSensitive);
	}

	friend uint32 GetTypeHash(const FHapbeatManifestEventKey& Key)
	{
		return HashCombine(GetTypeHash(Key.EventId), GetTypeHash(static_cast<uint8>(Key.Mode)));
	}
};

/**
 * Editor-only parser for Kit manifest.json (schema 2.0.0, see
 * hapbeat-contracts/specs/kit-format.md) that resolves the
 * <c>parameters.intensity</c> authored per event and bakes it onto each
 * FHapbeatEventEntry::CachedManifestIntensity so the runtime never needs to
 * read a manifest (see the field's doc comment in HapbeatEventEntry.h).
 *
 * UE counterpart of Hapbeat.Editor.HapbeatManifestIntensity (Unity SDK),
 * simplified: real JSON parsing (FJsonSerializer) instead of a hand-rolled
 * brace-matching regex scanner, and resolution is the (eventId, mode) tuple
 * match only -- the Unity StreamClip asset-path fallback match is dropped
 * (kit-format.md confirms stream_events keys use the same
 * <kit-name>.<file-name> eventId form as events, so authoring Category +
 * EventName on a StreamClip entry is sufficient for it to resolve).
 *
 * Scans the WHOLE project Content/ tree (not a "Kits root" convention) for
 * any file named *-manifest.json; every match is parsed and merged into one
 * project-wide (eventId, mode) -> intensity map.
 */
class FHapbeatManifestIntensityBaker
{
public:
	/** Summary of a BakeIntoEventMap() call, surfaced by the Refresh Intensities button. */
	struct FBakeResult
	{
		int32 NumManifestsScanned = 0;
		int32 NumResolved = 0;
		int32 NumUnresolved = 0;
	};

	/**
	 * Scan every *-manifest.json under FPaths::ProjectContentDir() (recursive)
	 * and build the (eventId, mode) -> intensity map. Not cached -- callers that
	 * want caching (there are none in v1; this only runs on an explicit user
	 * click) should cache the result themselves.
	 */
	static TMap<FHapbeatManifestEventKey, float> ScanProjectManifests(int32* OutNumManifestsScanned = nullptr);

	/**
	 * Re-scan every manifest, then resolve + write CachedManifestIntensity on
	 * every entry of Map. Entries with no (eventId, mode) match are set to -1
	 * (unresolved sentinel; runtime falls back to plain Gain). Wraps the write
	 * in Map->Modify() (undo support) and calls Map->MarkPackageDirty() after
	 * (asset needs saving). No-op (default-constructed result) if Map is null.
	 */
	static FBakeResult BakeIntoEventMap(UHapbeatEventMap* Map);

private:
	/** Parse one manifest.json file's "events" + "stream_events" buckets into OutMap (merged in-place). */
	static void ParseManifestFile(const FString& FilePath, TMap<FHapbeatManifestEventKey, float>& OutMap);

	/**
	 * Parse a single bucket ("events" or "stream_events") of an already-parsed
	 * manifest JSON root. Each bucket key is an event id; each value is an
	 * event object whose optional parameters.intensity (default 1.0, 0 is a
	 * valid authored silence) is extracted into OutMap under (key, Mode).
	 */
	static void ParseBucket(const TSharedPtr<FJsonObject>& Root, const TCHAR* BucketName, EHapticMode Mode,
		TMap<FHapbeatManifestEventKey, float>& OutMap);
};
