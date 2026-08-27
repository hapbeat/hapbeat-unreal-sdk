// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatStreamGainMirror.h"

/**
 * One wire STREAM_BEGIN/DATA/END session with any number of compatible local
 * PCM16 sources. FHapbeatStreamRunnable is the only caller and owns the worker
 * thread; source cursors and mixing buffers therefore remain single-writer.
 * The runnable moves newly queued sources into AddSource() between chunks.
 *
 * Every source has its own atomic Gain/Pan/Stopped mirror. Samples are scaled,
 * summed locally, clamped once to PCM16, then sent as one STREAM_DATA sequence.
 * This is wire-compatible with existing firmware and matches Unity's
 * HapbeatManager multi-source mixer.
 */
class FHapbeatStreamer
{
public:
	FHapbeatStreamer(
		int32 InSampleRate,
		int32 InChannels,
		const FString& InTarget,
		TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InInitialMirror,
		TFunction<uint16()> InNextSeq,
		TFunction<void(const TArray<uint8>&)> InSend,
		float InSendAheadSeconds);

	/** Worker-thread only: add one source already validated against the session format/target. */
	void AddSource(
		const FGuid& InSourceId,
		TArray<uint8>&& InPcm16,
		bool bInLoop,
		TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InMirror);

	/** Record the start time and send the session's only STREAM_BEGIN (gain = 1). */
	void Start(double NowSeconds);

	/** Mix and pace pending chunks. Does not close an empty session; the runnable owns that race. */
	void Tick(double NowSeconds);

	/** Send the session's only STREAM_END and stop every remaining source. Idempotent. */
	void SendEnd();

	bool HasSources() const { return Sources.Num() > 0; }
	bool IsDone() const { return bDone; }
	void DrainFinishedSourceIds(TArray<FGuid>& OutSourceIds);

private:
	struct FSource
	{
		FGuid Id;
		TArray<uint8> Pcm16;
		int32 ByteOffset = 0;
		bool bLoop = false;
		TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror;

		FSource(const FGuid& InSourceId, TArray<uint8>&& InPcm16, bool bInLoop,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InMirror)
			: Id(InSourceId), Pcm16(MoveTemp(InPcm16)), bLoop(bInLoop), Mirror(InMirror)
		{
		}
	};

	/** Mark a source naturally finished and remove it without affecting its siblings. */
	void FinishSource(int32 SourceIndex);

	int32 SampleRate = 0;
	int32 Channels = 0;
	int32 WireChannels = 0;
	FString Target;
	int32 SrcBytesPerFrame = 2;
	int32 WireBytesPerFrame = 2;
	bool bUpmixMonoToStereo = false;
	float SendAheadSeconds = 0.05f;

	TFunction<uint16()> NextSeqFn;
	TFunction<void(const TArray<uint8>&)> SendFn;
	TArray<FSource> Sources;
	TArray<FGuid> FinishedSourceIds;

	uint32 WireOffset = 0;
	int64 TotalFramesSent = 0;
	double StartTimeSeconds = 0.0;
	bool bDone = false;

	TArray<float> MixScratch;
	TArray<uint8> PcmScratch;
};
