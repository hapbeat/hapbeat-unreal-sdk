// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UHapbeatStreamPlayback;

/**
 * Internal, game-thread-only streamer for ONE active StreamClip session. Plain
 * (non-UObject) class owned by UHapbeatSubsystem via a TUniquePtr. Holds a COPY
 * of the clip's PCM16 bytes and paces STREAM_DATA so that audio stays sent
 * roughly StreamSendAheadSeconds ahead of wall-clock — the UE sibling of Unity's
 * single-session MixerCoroutine (no multi-source mixing in v1; replace
 * semantics are handled by the subsystem).
 *
 * WIRE-CRITICAL parity with HapbeatManager.cs:
 *   - STREAM_BEGIN carries gain = 1.0 (the device must NOT re-apply gain).
 *   - Every int16 sample is pre-multiplied by the live Gain x L/R Pan balance
 *     read off the handle, into a SCRATCH buffer (the source bytes are never
 *     mutated), then clamped to the int16 range.
 *
 * Threading: all methods run on the game thread (driven by a per-frame ticker
 * the subsystem registers only while a stream is active). The handle's Gain /
 * Pan are read directly with no atomics because the writer is game-thread too.
 */
class FHapbeatStreamer
{
public:
	/**
	 * @param InPcm16             COPY of the clip's interleaved LE int16 bytes (moved in).
	 * @param InSampleRate        Hz (e.g. 16000).
	 * @param InChannels          1 = mono, 2 = stereo.
	 * @param InTarget            Address filter ("" = broadcast). Sent in STREAM_BEGIN.
	 * @param bInLoop             Loop the clip until the handle stops.
	 * @param InHandle            The caller-facing playback handle (weak; may be GC'd).
	 * @param InNextSeq           Returns the subsystem's next monotonic u16 seq.
	 * @param InSend              Sends a fully-built packet over the subsystem socket.
	 * @param InSendAheadSeconds  Lead the streamer keeps queued (UHapbeatConfig::StreamSendAheadSeconds).
	 */
	FHapbeatStreamer(
		TArray<uint8>&& InPcm16,
		int32 InSampleRate,
		int32 InChannels,
		const FString& InTarget,
		bool bInLoop,
		TWeakObjectPtr<UHapbeatStreamPlayback> InHandle,
		TFunction<uint16()> InNextSeq,
		TFunction<void(const TArray<uint8>&)> InSend,
		float InSendAheadSeconds);

	/** Record the start time and send STREAM_BEGIN (gain = 1.0). */
	void Start(double NowSeconds);

	/** Pace + send pending STREAM_DATA chunks for this frame; finalize when done. */
	void Tick(double NowSeconds);

	/** Force STREAM_END now (idempotent). Used by StopStream(). */
	void SendEnd();

	/** True once STREAM_END has been sent (the streamer can be torn down). */
	bool IsDone() const { return bDone; }

private:
	/** True if the handle says stop, or the handle has been GC'd (null weak ptr). */
	bool IsHandleStopped() const;

	// --- immutable session state ---
	TArray<uint8> Pcm16; // COPY of the clip bytes; premultiply reads from here, never writes.
	int32 SampleRate = 0;
	int32 Channels = 0;
	FString Target;
	bool bLoop = false;
	int32 BytesPerFrame = 2; // 2 * Channels, computed in the ctor.
	float SendAheadSeconds = 0.05f;

	TWeakObjectPtr<UHapbeatStreamPlayback> Handle;
	TFunction<uint16()> NextSeqFn;
	TFunction<void(const TArray<uint8>&)> SendFn;

	// --- per-stream cursor / pacing state ---
	int32 ByteOffset = 0;        // read cursor into Pcm16 (resets to 0 on each loop wrap).
	uint32 WireOffset = 0;       // monotonic STREAM_DATA wire offset; never resets on loop (Unity parity).
	int64 TotalFramesSent = 0;   // frames emitted so far; sentDuration = TotalFramesSent / SampleRate.
	double StartTimeSeconds = 0.0;
	bool bDone = false;

	// Reused scratch buffer for the premultiplied chunk (avoids per-chunk alloc).
	TArray<uint8> Scratch;
};
