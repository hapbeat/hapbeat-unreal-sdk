// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HapbeatClip.generated.h"

/**
 * A raw PCM16 haptic clip — interleaved little-endian int16 samples plus the
 * sample rate and channel count. Built by parsing a 16 kHz PCM16 .wav header
 * (NO engine audio decode), which keeps streaming version-robust across UE
 * releases (the kit format is already 16 kHz PCM16).
 *
 * Referenced by FHapbeatEventEntry::StreamClip and consumed by the Phase 3
 * streaming runtime + Phase 6 samples.
 */
UCLASS(BlueprintType)
class HAPBEATSDK_API UHapbeatClip : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Sample rate in Hz (typically 16000). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hapbeat")
	int32 SampleRate = 0;

	/** Channel count (1 = mono, 2 = stereo). Samples are interleaved. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hapbeat")
	int32 NumChannels = 0;

	/**
	 * Raw PCM16 audio bytes, interleaved little-endian int16 (copied verbatim
	 * from the WAV 'data' chunk). Not shown in Details (can be large).
	 */
	UPROPERTY()
	TArray<uint8> Pcm16;

	/** Total interleaved int16 sample count across all channels (bytes / 2). */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	int32 NumSamples() const { return Pcm16.Num() / 2; }

	/** Number of sample frames (NumSamples / channels). */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	int32 NumFrames() const
	{
		return NumChannels > 0 ? (NumSamples() / NumChannels) : 0;
	}

	/** Clip duration in seconds (frames / sample rate). 0 if unset. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	float DurationSeconds() const
	{
		return SampleRate > 0 ? (static_cast<float>(NumFrames()) / static_cast<float>(SampleRate)) : 0.0f;
	}

	/**
	 * Parse a RIFF/WAVE byte buffer into PCM16 samples + format. Returns true on
	 * success. Requirements: 'RIFF'/'WAVE' container, fmt audioFormat == 1 (PCM)
	 * and bitsPerSample == 16. Walks chunks honouring each chunk's size and the
	 * RIFF even-byte word alignment, skipping unknown chunks (LIST / fact / etc.)
	 * and any trailing bytes. The 'data' bytes are copied verbatim (already
	 * little-endian int16). On failure OutError describes the problem and the
	 * out-params are left untouched.
	 */
	static bool ParseWav(const TArray<uint8>& WavBytes, int32& OutSampleRate, int32& OutChannels,
		TArray<uint8>& OutPcm16, FString& OutError);

	/**
	 * Create a transient UHapbeatClip from .wav bytes via ParseWav. Returns
	 * nullptr (and logs a warning) on parse failure. Outer scopes the object's
	 * lifetime; pass GetTransientPackage() for a throwaway runtime clip.
	 */
	static UHapbeatClip* CreateFromWavBytes(UObject* Outer, const TArray<uint8>& WavBytes);
};
