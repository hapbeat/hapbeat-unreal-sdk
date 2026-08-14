// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatEventEntry.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "HapbeatEventMapScripting.generated.h"

class UHapbeatClip;
class UHapbeatEventMap;

/**
 * Editor scripting entry points for authoring Event Maps and Clips.
 *
 * Several fields on those assets are deliberately VisibleAnywhere -- an entry's
 * Id, a clip's sample rate, the baked manifest intensity -- because hand-editing
 * them silently breaks the wiring or the audio. That also puts them out of reach
 * of Python's set_editor_property, so a generator script cannot build these
 * assets field by field. These functions are the sanctioned way in: each one
 * sets a coherent group of fields at once, so the invariants those fields share
 * cannot be half-applied.
 *
 * Editor module only -- nothing here ships in a packaged game.
 */
UCLASS()
class UHapbeatEventMapScripting : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Fill a clip from a .wav on disk (16-bit PCM). Returns false and logs why on
	 * a malformed or unsupported file.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Scripting")
	static bool ImportWavIntoClip(UHapbeatClip* Clip, const FString& WavFilePath);

	/** Drop every entry. Call before rebuilding a map so a regenerate is not additive. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Scripting")
	static void ClearEntries(UHapbeatEventMap* Map);

	/**
	 * Append one entry, minting its stable Id.
	 *
	 * @param Intensity Baked manifest intensity; pass a negative value to leave it
	 *                  unresolved (the runtime then falls back to plain Gain).
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Scripting")
	static FGuid AddEntry(UHapbeatEventMap* Map, EHapticMode Mode, const FString& Category,
		const FString& EventName, float Gain, bool bLoop, float Intensity,
		UHapbeatClip* StreamClip, const FString& DisplayName);
};
