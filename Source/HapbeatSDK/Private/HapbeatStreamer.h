// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatProtocol.h"
#include "HapbeatStreamGainMirror.h"

/**
 * One wire STREAM_BEGIN/DATA/END session with any number of compatible local
 * PCM16 sources. FHapbeatStreamRunnable is the only caller and owns the worker
 * thread; source cursors and mixing buffers therefore remain single-writer.
 * The runnable moves newly queued sources into AddSource() between chunks.
 *
 * Every source has its own atomic Gain/Pan/Stopped mirror. Samples are scaled
 * (channel gains ramped per sample from the previous chunk, no zipper), summed
 * locally, passed through the full-scale limiter, saturated once to PCM16, then
 * sent as one STREAM_DATA sequence (sdk-multi-stream.md §5.3, §5.7, §5.8).
 *
 * The wire format (Stream Gen 1 vs Stream Gen 2) is fixed at construction. A
 * Stream Gen 2 session keeps its lease but may move to a new generation of it:
 * after its own send gap of 3 s or more (stream-session.md Sender requirements
 * 3), when the runnable asks for one (STREAM_TIMED_OUT / STREAM_LEASE_INVALID),
 * and for a re-splice (sdk-multi-stream.md §6.14). Each move ENDs the current
 * generation first, keeps every source cursor and BEGINs the next generation.
 */
class FHapbeatStreamer
{
public:
	/** A Stream Gen 2 session whose DATA stopped this long ends and begins a new generation. */
	static constexpr double OwnGapSeconds = 3.0;
	/** BEGIN goes out before DATA blocks 0..2 (the first one in Start) ... */
	static constexpr int32 BeginRepeatInitialBlocks = 3;
	/** ... and then before every 50th block (500 ms of 10 ms blocks). */
	static constexpr int32 BeginRepeatIntervalBlocks = 50;
	/**
	 * Re-splice only when the device is expected to hold more than this much
	 * already-sent audio after the new source's start: the receiver flushes its
	 * ring on a BEGIN only above 32 ms (message-format.md 0x30), plus a margin for
	 * packets in flight. Same value as Unity RespliceMinQueuedSeconds.
	 */
	static constexpr double RespliceMinQueuedSeconds = 0.04;

	FHapbeatStreamer(
		int32 InSampleRate,
		int32 InChannels,
		const FString& InTarget,
		TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InInitialMirror,
		TFunction<uint16()> InNextSeq,
		TFunction<void(const TArray<uint8>&)> InSend,
		float InSendAheadSeconds,
		EHapbeatStreamWireFormat InWireFormat = EHapbeatStreamWireFormat::LegacyV1,
		const FHapbeatStreamSessionIdentity& InIdentity = FHapbeatStreamSessionIdentity(),
		TFunction<uint32()> InAllocateGeneration = TFunction<uint32()>());

	EHapbeatStreamWireFormat GetWireFormat() const { return WireFormat; }
	/** The generation currently on the wire. */
	const FHapbeatStreamSessionIdentity& GetIdentity() const { return Identity; }
	/** The generation this session ended last (invalid until it moved to a new one). */
	const FHapbeatStreamSessionIdentity& GetPreviousIdentity() const { return PreviousIdentity; }

	/**
	 * Worker-thread only: add one source already validated against the session
	 * format/target.
	 *
	 * @param StartFrame          Cursor to start from: 0, or the cursor a source
	 *                            carries over from this device's previous session
	 *                            (stream-session.md Sender requirements 8).
	 * @param RequestedAtSeconds  When the application asked for this source (same
	 *                            clock as NowSeconds), or < 0. A new source that
	 *                            falls inside blocks already sent ahead re-splices
	 *                            the session (sdk-multi-stream.md §6.14).
	 */
	void AddSource(
		const FGuid& InSourceId,
		TArray<uint8>&& InPcm16,
		TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InMirror,
		double StartFrame = 0.0,
		double RequestedAtSeconds = -1.0,
		double NowSeconds = 0.0);
	/** Worker-thread only: detach one endpoint-local cursor without completing the logical source. */
	void RemoveSource(const FGuid& SourceId);

	/** Record the start time and send the session's first STREAM_BEGIN (gain = 1). */
	void Start(double NowSeconds);

	/** Mix and pace pending chunks. Does not close an empty session; the runnable owns that race. */
	void Tick(double NowSeconds);
	void RebasePacing(double NowSeconds);

	/** Send the session's STREAM_END and stop every remaining source. Idempotent. */
	void SendEnd();

	/**
	 * Stream Gen 2: END the current generation and BEGIN the next one under the
	 * same lease, keeping every cursor; pacing restarts at NowSeconds, so no
	 * catch-up DATA follows. Returns false (and ends the session) when no further
	 * generation can be allocated. No-op for Stream Gen 1.
	 */
	bool RestartGeneration(double NowSeconds);

	/** Send the identical BEGIN again before the next DATA (STREAM_NOT_STARTED). */
	void RequestBeginResend() { bResendBeginRequested = true; }
	/** Target of every later (repeated) BEGIN: the endpoint's current device address. */
	void SetTarget(const FString& InTarget) { Target = InTarget; }
	/** Mix without pan (left = right) for a receiver with one motor (sdk-multi-stream.md §6.15). */
	void SetIgnorePan(bool bInIgnorePan) { bIgnorePan = bInIgnorePan; }

	/** Read position of every source (after SendEnd: the positions it ended at), in source frames. */
	void GetCursors(TMap<FGuid, double>& OutCursors) const;

	/** Whether the identical BEGIN goes out again before DATA block DataBlocksSent (0-based). */
	static bool ShouldRepeatBegin(int32 InDataBlocksSent)
	{
		return InDataBlocksSent > 0
			&& (InDataBlocksSent < BeginRepeatInitialBlocks || InDataBlocksSent % BeginRepeatIntervalBlocks == 0);
	}

	bool HasSources() const { return Sources.Num() > 0; }
	bool IsDone() const { return bDone; }
	void DrainFinishedSourceIds(TArray<FGuid>& OutSourceIds);

private:
	struct FSource
	{
		FGuid Id;
		TArray<uint8> Pcm16;
		/**
		 * Fractional read position in source frames (advanced by the playback
		 * rate). Negative = the source starts that many frames later (placed
		 * inside a block by a re-splice).
		 */
		double ReadFrame = 0.0;
		/** Session time the source's StartFrame was due; where a re-splice puts it back. */
		double JoinedAtSeconds = 0.0;
		double StartFrame = 0.0;
		/** Channel gains (gain x balance) applied at the end of the previous chunk; ramp start of the next. */
		float LastGainL = 0.0f;
		float LastGainR = 0.0f;
		/** False until the first mixed chunk: a new source starts at its gains without a ramp (§5.7). */
		bool bHasLastGains = false;
		TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror;

		FSource(const FGuid& InSourceId, TArray<uint8>&& InPcm16,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InMirror)
			: Id(InSourceId), Pcm16(MoveTemp(InPcm16)), Mirror(InMirror)
		{
		}
	};

	/** Mark a source naturally finished and remove it without affecting its siblings. */
	void FinishSource(int32 SourceIndex);
	/** Full-scale limiter over MixScratch (sdk-multi-stream.md §5.8); leaves a block within full scale untouched. */
	void ApplyLimiter(int32 FramesPerChunk);
	void SendBeginPacket();
	void SendEndPacket();
	/** Seconds of the session timeline at which the next unsent block is due. */
	double NextBlockDueSeconds() const;
	/** sdk-multi-stream.md §6.14 for the source just appended at SourceIndex. */
	void TryResplice(int32 SourceIndex, double RequestedAtSeconds, double NowSeconds);

	int32 SampleRate = 0;
	int32 Channels = 0;
	int32 WireChannels = 0;
	FString Target;
	int32 SrcBytesPerFrame = 2;
	int32 WireBytesPerFrame = 2;
	bool bUpmixMonoToStereo = false;
	float SendAheadSeconds = 0.05f;
	EHapbeatStreamWireFormat WireFormat = EHapbeatStreamWireFormat::LegacyV1;
	FHapbeatStreamSessionIdentity Identity;
	FHapbeatStreamSessionIdentity PreviousIdentity;

	TFunction<uint16()> NextSeqFn;
	TFunction<void(const TArray<uint8>&)> SendFn;
	/** Next generation under this session's lease (0 = exhausted / none). */
	TFunction<uint32()> AllocateGenerationFn;
	TArray<FSource> Sources;
	TArray<FGuid> FinishedSourceIds;
	/** Cursors at SendEnd, so a replacement session can continue them. */
	TMap<FGuid, double> FinalCursors;

	uint32 WireOffset = 0;
	int64 TotalFramesSent = 0;
	/** DATA blocks of the current generation (drives the repeated BEGIN). */
	int32 DataBlocksSent = 0;
	double StartTimeSeconds = 0.0;
	/** Time of the last DATA, for the own-gap rule; < 0 before the first one of a generation. */
	double LastDataSentSeconds = -1.0;
	bool bStarted = false;
	bool bDone = false;
	bool bResendBeginRequested = false;
	bool bIgnorePan = false;
	/** Limiter gain reached at the end of the previous chunk (1 = no reduction). */
	float LimiterGain = 1.0f;

	TArray<float> MixScratch;
	TArray<uint8> PcmScratch;
};
