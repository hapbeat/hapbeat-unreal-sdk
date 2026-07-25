// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatStreamGainMirror.h"

/**
 * Session state + chunk-pacing logic for ONE active StreamClip session. Plain
 * (non-UObject) class, owned and driven by FHapbeatStreamRunnable on a
 * dedicated stream-send thread (2026-07-25 thread migration — see
 * dev-notes/unreal-sdk-v1-design.md §5; before that it was driven by a
 * game-thread FTSTicker, matching Unity's old MonoBehaviour coroutine — an
 * architecture Unity itself abandoned in production for the identical reason
 * this migration exists: frame hitches starved the device ring buffer). Holds
 * a COPY of the clip's PCM16 bytes and paces STREAM_DATA so that audio stays
 * sent roughly StreamSendAheadSeconds ahead of wall-clock (no multi-source
 * mixing in v1; replace semantics are handled by the subsystem).
 *
 * WIRE-CRITICAL parity with HapbeatManager.cs:
 *   - STREAM_BEGIN carries gain = 1.0 (the device must NOT re-apply gain).
 *   - Every int16 sample is pre-multiplied by the live Gain x L/R Pan balance
 *     read off the mirror, into a SCRATCH buffer (the source bytes are never
 *     mutated), then clamped to the int16 range.
 *
 * Threading: ALL methods run on the stream-send thread ONLY (FHapbeatStreamRunnable::Run()
 * is the sole caller). This class owns every byte of its mutable session state
 * (read cursor, wire offset, frames-sent, done flag) — single-writer, so none
 * of it needs a lock. The ONLY thing shared with the game thread is the
 * FHapbeatStreamGainMirror (atomics) — Gain/Pan are read from it directly
 * (never via the UHapbeatStreamPlayback UObject, which is unsafe to touch off
 * the game thread under GC).
 */
class FHapbeatStreamer
{
public:
	/**
	 * @param InPcm16             COPY of the clip's interleaved LE int16 bytes (moved in).
	 * @param InSampleRate        Hz (e.g. 16000).
	 * @param InChannels          1 = mono, 2 = stereo.
	 * @param InTarget            Address filter ("" = broadcast). Sent in STREAM_BEGIN.
	 * @param bInLoop             Loop the clip until the mirror's bStopped is set.
	 * @param InMirror            Thread-safe Gain/Pan/bStopped mirror (never null).
	 * @param InNextSeq           Returns the subsystem's next monotonic u16 seq (internally locked —
	 *                            shared with the game thread's Play/Stop/Ping/etc. sends).
	 * @param InSend              Sends a fully-built packet (unicast fan-out or broadcast; the
	 *                            caller — FHapbeatStreamRunnable — decides which, using addresses
	 *                            it built for itself on this same thread).
	 * @param InSendAheadSeconds  Lead the streamer keeps queued (UHapbeatConfig::StreamSendAheadSeconds).
	 */
	FHapbeatStreamer(
		TArray<uint8>&& InPcm16,
		int32 InSampleRate,
		int32 InChannels,
		const FString& InTarget,
		bool bInLoop,
		TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InMirror,
		TFunction<uint16()> InNextSeq,
		TFunction<void(const TArray<uint8>&)> InSend,
		float InSendAheadSeconds);

	/** Record the start time and send STREAM_BEGIN (gain = 1.0). */
	void Start(double NowSeconds);

	/** Pace + send pending STREAM_DATA chunks since the last call; finalize when done. */
	void Tick(double NowSeconds);

	/** Force STREAM_END now (idempotent). Called by FHapbeatStreamRunnable on a stop request. */
	void SendEnd();

	/** True once STREAM_END has been sent (the streamer can be torn down). */
	bool IsDone() const { return bDone; }

private:
	/** True if the mirror says stop. */
	bool IsMirrorStopped() const { return Mirror->bStopped.load(std::memory_order_relaxed); }

	// --- immutable session state ---
	TArray<uint8> Pcm16; // COPY of the clip bytes; premultiply reads from here, never writes.
	int32 SampleRate = 0;
	int32 Channels = 0;
	FString Target;
	bool bLoop = false;
	int32 BytesPerFrame = 2; // 2 * Channels, computed in the ctor.
	float SendAheadSeconds = 0.05f;

	TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror;
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
