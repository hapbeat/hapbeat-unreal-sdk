// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatStreamer.h"

#include "HapbeatProtocol.h"

namespace
{
	// ~10ms target chunk size (Unity 94ec760 parity): smoother, more even
	// pacing than the old MTU-cap-sized (~44ms mono) chunks. The MTU cap
	// remains an upper BOUND only — never exceeded, but no longer the target.
	constexpr float TargetChunkSeconds = 0.010f;
}

FHapbeatStreamer::FHapbeatStreamer(
	TArray<uint8>&& InPcm16,
	int32 InSampleRate,
	int32 InChannels,
	const FString& InTarget,
	bool bInLoop,
	TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InMirror,
	TFunction<uint16()> InNextSeq,
	TFunction<void(const TArray<uint8>&)> InSend,
	float InSendAheadSeconds)
	: Pcm16(MoveTemp(InPcm16))
	, SampleRate(InSampleRate)
	, Channels(InChannels)
	, Target(InTarget)
	, bLoop(bInLoop)
	, Mirror(InMirror)
	, NextSeqFn(MoveTemp(InNextSeq))
	, SendFn(MoveTemp(InSend))
{
	// Guard against degenerate inputs so the pacing loop can never spin forever
	// (the subsystem already rejects empty clips, but defend in depth).
	Channels = FMath::Max(1, Channels);
	SrcBytesPerFrame = 2 * Channels;
	SampleRate = FMath::Max(1, SampleRate);
	SendAheadSeconds = InSendAheadSeconds > 0.01f ? InSendAheadSeconds : 0.05f;

	// Decide the wire channel count ONCE, here, because STREAM_BEGIN fixes it for
	// the whole session: a mono clip asked to play off-centre is sent as stereo
	// (each sample duplicated into L/R per chunk) so the pan balance has two
	// channels to act on. The pan read here is the one the game thread wrote onto
	// the mirror BEFORE the session was started (UHapbeatSubsystem::StreamClip's
	// InitialPan); a SetPan issued later cannot change the channel count.
	bUpmixMonoToStereo = (Channels == 1)
		&& (Mirror->Pan.load(std::memory_order_relaxed) != 0.0f);
	WireChannels = bUpmixMonoToStereo ? 2 : Channels;
	WireBytesPerFrame = 2 * WireChannels;

	// Size the premultiply scratch buffer ONCE to the maximum possible chunk
	// size (the MTU cap, which is always >= the ~10ms target) and keep it for
	// the stream's lifetime. We then write into it and pass an explicit byte
	// count per chunk — never resizing per-chunk. This avoids the
	// SetNumUninitialized shrink-argument overload, which differs between UE
	// versions (bool bAllowShrinking on 5.3/5.4 vs EAllowShrinking on 5.5+),
	// keeping the streamer source-compatible across 5.3 -> latest.
	// Sized in WIRE bytes: an upmixed chunk is twice the source bytes it came from.
	const int32 MaxFramesPerChunk = FMath::Max(1, FHapbeatProtocol::StreamDataMaxPayload / WireBytesPerFrame);
	Scratch.SetNumUninitialized(MaxFramesPerChunk * WireBytesPerFrame);
}

void FHapbeatStreamer::Start(double NowSeconds)
{
	StartTimeSeconds = NowSeconds;

	// total_samples: 0 = "unknown" (informational, device-ignored). Sent as 0 for
	// exact wire parity with the Unity SDK (HapbeatManager.cs SendStreamBegin(..,0,..)).
	const uint32 TotalSamples = 0;

	// WIRE-CRITICAL: gain = 1.0 (NOT the baseline). The SDK pre-multiplies every
	// PCM sample by the live Gain x Pan balance before sending, so the device
	// must pass the audio through unscaled — sending the baseline here would
	// double-apply it. Exact parity with HapbeatManager.cs (SendStreamBegin(..,
	// 1.0f, target) + per-sample premultiply in MixerCoroutine).
	// WireChannels, not Channels: an upmixed mono clip is announced as stereo
	// because that is what every STREAM_DATA chunk below will carry.
	SendFn(FHapbeatProtocol::BuildStreamBegin(
		NextSeqFn(),
		static_cast<uint16>(SampleRate),
		static_cast<uint8>(WireChannels),
		FHapbeatProtocol::AudioFormatPcm16,
		TotalSamples,
		1.0f,
		Target));
}

void FHapbeatStreamer::Tick(double NowSeconds)
{
	if (bDone)
	{
		return;
	}

	// Stop requested via the mirror (e.g. a Blueprint call to the playback
	// handle's own Stop(), independent of the subsystem-level StopStream()
	// path FHapbeatStreamRunnable::Run() also polls) -> finalize this call.
	if (IsMirrorStopped())
	{
		SendEnd();
		return;
	}

	// Nothing to stream (degenerate) -> end immediately.
	if (Pcm16.Num() < SrcBytesPerFrame)
	{
		SendEnd();
		return;
	}

	// Read the live modulation off the mirror once per Tick (never touches the
	// UHapbeatStreamPlayback UObject — see the class doc). Pan-to-L/R-balance is
	// inlined here (duplicating UHapbeatStreamPlayback::GetStereoChannelGains'
	// tiny formula) for the same reason: no UObject touch from this thread.
	const float G = Mirror->Gain.load(std::memory_order_relaxed);
	const float PanValue = Mirror->Pan.load(std::memory_order_relaxed);
	const float GainL = PanValue <= 0.0f ? 1.0f : 1.0f - PanValue;
	const float GainR = PanValue >= 0.0f ? 1.0f : 1.0f + PanValue;

	// Max frames per STREAM_DATA chunk: the smaller of (a) the ~10ms pacing
	// target and (b) the spec's 1400-byte payload budget (StreamDataMaxPayload,
	// NOT the larger MaxStreamPacketSize MTU guardrail), frame-aligned so
	// stereo L/R never splits across packets. Measured in WIRE bytes, so an
	// upmixed session simply fits half as many frames per packet — the ~10ms
	// pacing target below is unchanged, and so is the send cadence.
	const int32 MtuFramesCap = FMath::Max(1, FHapbeatProtocol::StreamDataMaxPayload / WireBytesPerFrame);
	const int32 TargetFrames = FMath::Max(1, FMath::RoundToInt(static_cast<float>(SampleRate) * TargetChunkSeconds));
	const int32 FramesPerChunk = FMath::Min(MtuFramesCap, TargetFrames);

	// Pace: keep audio sent ~SendAheadSeconds ahead of wall-clock. sentDuration
	// is frame-based (TotalFramesSent / SampleRate), equivalent to Unity's
	// byteOffset / (sampleRate*channels*2). lead = how far ahead of real time we
	// already are; send chunks until that lead reaches the configured send-ahead.
	double Elapsed = NowSeconds - StartTimeSeconds;
	double SentDuration = static_cast<double>(TotalFramesSent) / static_cast<double>(SampleRate);
	double Lead = SentDuration - Elapsed;

	// Bound the number of chunks per Tick as a final safety net against a runaway
	// loop (e.g. a pathological clock); the lead/SendAhead condition is the real
	// terminator. 4096 chunks >> any single call ever needs at ~10ms cadence.
	int32 SafetyBudget = 4096;

	while (Lead < SendAheadSeconds && !bDone && --SafetyBudget >= 0)
	{
		// Frames left until the end of the clip from the current read cursor.
		const int32 BytesRemaining = Pcm16.Num() - ByteOffset;
		const int32 FramesAvail = BytesRemaining / SrcBytesPerFrame;

		if (FramesAvail <= 0)
		{
			// Hit the end of the clip.
			if (bLoop)
			{
				// KEEP the session (no new STREAM_BEGIN) and do NOT reset
				// TotalFramesSent — pacing must keep converging across the wrap.
				ByteOffset = 0;
				continue;
			}
			// Non-loop: the clip is fully sent. Close the session.
			SendEnd();
			break;
		}

		// Chunk size = min(per-chunk cap, frames left to clip end). Frame-aligned;
		// never straddles the loop boundary (the wrap is handled by the branch
		// above on the next iteration). chunkBytes <= StreamDataMaxPayload (1400).
		const int32 FramesThisChunk = FMath::Min(FramesPerChunk, FramesAvail);
		const int32 SrcChunkBytes = FramesThisChunk * SrcBytesPerFrame;
		const int32 ChunkBytes = FramesThisChunk * WireBytesPerFrame; // what actually goes out

		// Scratch was pre-sized in the ctor to the max chunk; only the first
		// ChunkBytes are written/sent below (a wrap-boundary chunk may be smaller).

		// Premultiply each int16 sample by G (mono) or G x L/R (stereo) into the
		// scratch buffer, clamping to the int16 range, writing LE. The source
		// Pcm16 is read-only (never mutated).
		const uint8* Src = Pcm16.GetData() + ByteOffset;
		uint8* Dst = Scratch.GetData();

		// Scale the source int16 at SrcIndex by Coeff, clamp to int16 range, write
		// LE into Dst at DstIndex. Source and destination indices are separate
		// because the upmix path reads one sample and writes two. Matches Unity's
		// `(short)Mathf.Clamp(v*scale, -32768, 32767)`: clamp in float, then a
		// truncating (toward-zero) cast — same per-sample math the device-side
		// expects (the gain is baked here, not re-applied on the device).
		auto WriteScaled = [Src, Dst](int32 SrcIndex, int32 DstIndex, float Coeff)
		{
			const int16 In = static_cast<int16>(static_cast<uint16>(Src[SrcIndex * 2]) |
				(static_cast<uint16>(Src[SrcIndex * 2 + 1]) << 8));
			const float Scaled = FMath::Clamp(static_cast<float>(In) * Coeff, -32768.0f, 32767.0f);
			const uint16 Out = static_cast<uint16>(static_cast<int16>(Scaled)); // trunc toward zero
			Dst[DstIndex * 2] = static_cast<uint8>(Out & 0xFF);
			Dst[DstIndex * 2 + 1] = static_cast<uint8>((Out >> 8) & 0xFF);
		};

		if (bUpmixMonoToStereo)
		{
			// One source sample -> an L/R pair, each with its own balance
			// coefficient. This is what makes a mono clip pannable at all.
			for (int32 f = 0; f < FramesThisChunk; ++f)
			{
				WriteScaled(f, f * 2, G * GainL);
				WriteScaled(f, f * 2 + 1, G * GainR);
			}
		}
		else if (Channels == 2)
		{
			// 2 samples per frame: even index = L, odd = R.
			const int32 SampleCount = FramesThisChunk * 2;
			for (int32 i = 0; i < SampleCount; ++i)
			{
				WriteScaled(i, i, ((i & 1) == 0) ? (G * GainL) : (G * GainR));
			}
		}
		else
		{
			// Mono and centred (or any other channel count): no balance to apply,
			// so the frames pass through with the channel layout they came in with.
			const int32 SampleCount = FramesThisChunk * Channels;
			for (int32 i = 0; i < SampleCount; ++i)
			{
				WriteScaled(i, i, G);
			}
		}

		SendFn(FHapbeatProtocol::BuildStreamData(
			NextSeqFn(), WireOffset, Scratch.GetData(), ChunkBytes));

		ByteOffset += SrcChunkBytes;                   // read cursor advances in SOURCE bytes
		WireOffset += static_cast<uint32>(ChunkBytes); // monotonic across loop wraps (Unity parity)
		TotalFramesSent += FramesThisChunk;

		// Recompute lead so the loop stops once we're sufficiently ahead.
		SentDuration = static_cast<double>(TotalFramesSent) / static_cast<double>(SampleRate);
		Lead = SentDuration - (NowSeconds - StartTimeSeconds);
	}
}

void FHapbeatStreamer::SendEnd()
{
	if (bDone)
	{
		return;
	}
	bDone = true;
	SendFn(FHapbeatProtocol::BuildStreamEnd(NextSeqFn()));
}
