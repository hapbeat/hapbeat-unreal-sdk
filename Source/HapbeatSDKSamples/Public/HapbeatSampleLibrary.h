// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatEventEntry.h" // FHapbeatEventEntry (by value) + EHapticMode

class UHapbeatClip;
class UHapbeatEventMap;

/**
 * Shared, non-UObject static helpers for the Hapbeat samples (BasicExample +
 * Showcase zones). Not part of the public runtime API (that is the HapbeatSDK
 * module) -- this exists purely so every sample actor builds its content path,
 * loads its clips, and constructs its EventMap entries the same way. Mirrors
 * the plain-static-class idiom the runtime module already uses for
 * FHapbeatProtocol (a stateless helper, no UCLASS/generated header needed --
 * the HAPBEATSDKSAMPLES_API export macro works on any class in this module).
 */
class HAPBEATSDKSAMPLES_API FHapbeatSampleLibrary
{
public:
	/**
	 * Absolute disk path to Content/HapbeatSamples/ inside the installed
	 * HapbeatSDK plugin, resolved via IPluginManager. This is a physical
	 * directory read directly with FFileHelper -- it does NOT depend on the
	 * plugin's CanContainContent / content-mount-point setting (that only
	 * controls whether /HapbeatSDK/... is registered as a UE asset path;
	 * IPlugin::GetContentDir() itself is a plain FPaths join and always
	 * resolves regardless). Returns an empty string (and logs a warning) if
	 * the HapbeatSDK plugin cannot be found by name -- should not happen for
	 * any project that has this plugin enabled.
	 */
	static FString GetSamplesContentDir();

	/**
	 * Load a raw PCM16 .wav from under Content/HapbeatSamples/ and parse it
	 * into a transient UHapbeatClip. RelPathUnderHapbeatSamples is relative to
	 * that folder, e.g.
	 * "BasicExample/Kit/basic-exam-kit/stream-clips/sine_100hz_1s.wav". Returns
	 * nullptr (and logs a warning) if the file is missing or fails to parse as
	 * 16-bit PCM WAV (see UHapbeatClip::ParseWav).
	 *
	 * GC NOTE: the returned clip is a plain new UObject with Outer as its only
	 * owner chain; it has no other strong reference. Store it in a UPROPERTY
	 * on the caller (e.g. a TObjectPtr<UHapbeatClip> actor member) for as long
	 * as it needs to stay alive. Do NOT rely on FHapbeatEventEntry::StreamClip
	 * alone to keep it alive -- that field is a TSoftObjectPtr, which the
	 * garbage collector does not treat as a strong reference.
	 */
	static UHapbeatClip* LoadSampleClip(UObject* Outer, const FString& RelPathUnderHapbeatSamples);

	/**
	 * Build one FHapbeatEventEntry with a fresh stable Id (FGuid::NewGuid())
	 * plus the given fields. DisplayName defaults to EventName when left
	 * empty. Clip is only meaningful for EHapticMode::StreamClip (becomes the
	 * entry's StreamClip soft pointer -- see the GC note on LoadSampleClip,
	 * the caller must keep Clip alive independently of the entry); pass
	 * nullptr for EHapticMode::Command.
	 */
	static FHapbeatEventEntry MakeEntry(EHapticMode Mode, const FString& Category, const FString& EventName,
		float Gain, bool bLoop, float CachedIntensity, UHapbeatClip* Clip = nullptr,
		const FString& DisplayName = FString());

	/**
	 * Resolve an entry's stable Id by (Mode, Category, EventName) -- the sample
	 * actors' way of pointing at an authored asset's entries without hardcoding a
	 * GUID (the generator mints fresh ones on every regenerate). Returns an invalid
	 * FGuid and logs a warning if no entry matches.
	 */
	static FGuid FindEntryId(const UHapbeatEventMap* Map, EHapticMode Mode,
		const FString& Category, const FString& EventName);

	/**
	 * Show (or update in place) one line of a persistent on-screen HUD, e.g. a
	 * key guide or a "devices reachable: N" status line. LineKey is a small
	 * fixed per-purpose id; calling again with the same key replaces that line
	 * in place (GEngine::AddOnScreenDebugMessage's Key parameter) instead of
	 * stacking a new one below it, so a small set of fixed keys gives a stable
	 * multi-line HUD with no Slate/UMG widget. No-op if GEngine is null.
	 */
	static void ShowHudLine(int32 LineKey, const FString& Text, FColor Color = FColor::White, float Duration = 1.5f);

	/**
	 * Show the shared device-liveness HUD line ("Hapbeat devices reachable: N",
	 * green when N > 0). Every sample calls this from its HUD refresh so a zone
	 * run standalone still tells the user whether a device is answering PONGs.
	 * WorldContextObject: any actor/UObject with a world (used to reach the
	 * GameInstance subsystem). No-op if the subsystem is unavailable.
	 */
	static void ShowDeviceStatusLine(const UObject* WorldContextObject, int32 LineKey, float Duration = 1.5f);
};
