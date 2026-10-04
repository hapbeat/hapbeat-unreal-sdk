// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatStreamer.h"

#include "HapbeatProtocol.h"

namespace
{
	constexpr float TargetChunkSeconds = 0.010f;

	// Limiter (sdk-multi-stream.md §5.8): a chunk whose peak exceeds full scale is
	// scaled down to LimitTarget / peak; values past LimitKnee go through a tanh
	// soft knee that approaches full scale instead of a hard clamp.
	constexpr float FullScale = 32767.0f;
	constexpr float LimitTarget = 0.9f * FullScale;
	constexpr float LimitKnee = 0.95f * FullScale;
	constexpr float LimitReleasePerChunk = 0.05f;

	int16 ReadPcm16Le(const uint8* Bytes)
	{
		return static_cast<int16>(static_cast<uint16>(Bytes[0]) |
			(static_cast<uint16>(Bytes[1]) << 8));
	}
}

FHapbeatStreamer::FHapbeatStreamer(
	int32 InSampleRate,
	int32 InChannels,
	const FString& InTarget,
	TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InInitialMirror,
	TFunction<uint16()> InNextSeq,
	TFunction<void(const TArray<uint8>&)> InSend,
	float InSendAheadSeconds,
	EHapbeatStreamWireFormat InWireFormat,
	const FHapbeatStreamSessionIdentity& InIdentity)
	: SampleRate(16000)
	, Channels(2)
	, Target(InTarget)
	, SendAheadSeconds(InSendAheadSeconds > 0.01f ? InSendAheadSeconds : 0.05f)
	, WireFormat(InWireFormat)
	, Identity(InIdentity)
	, NextSeqFn(MoveTemp(InNextSeq))
	, SendFn(MoveTemp(InSend))
{
	checkf(WireFormat != EHapbeatStreamWireFormat::SessionV2 || Identity.IsValid(),
		TEXT("Hapbeat: a v2 stream session needs a valid boot id / lease ticket / generation"));
	// The hub normalizes every source before it reaches a session. Keeping the
	// wire profile fixed is what lets arbitrary sources join one endpoint mixer.
	SrcBytesPerFrame = 4;
	WireChannels = 2;
	WireBytesPerFrame = 2 * WireChannels;

	const int32 MtuFrames = FMath::Max(1,
		FHapbeatProtocol::StreamDataMaxPayload / WireBytesPerFrame);
	const int32 TargetFrames = FMath::Max(1,
		FMath::RoundToInt(static_cast<float>(SampleRate) * TargetChunkSeconds));
	const int32 FramesPerChunk = FMath::Min(MtuFrames, TargetFrames);
	MixScratch.SetNumUninitialized(FramesPerChunk * WireChannels);
	PcmScratch.SetNumUninitialized(FramesPerChunk * WireBytesPerFrame);
}

void FHapbeatStreamer::AddSource(
	const FGuid& InSourceId,
	TArray<uint8>&& InPcm16,
	TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InMirror)
{
	// Loop is live Playback state. Never overwrite it with the source's authored
	// initial value when this source joins a late or migrated endpoint.
	Sources.Emplace(InSourceId, MoveTemp(InPcm16), InMirror);
}

void FHapbeatStreamer::RemoveSource(const FGuid& SourceId)
{
	Sources.RemoveAllSwap([&SourceId](const FSource& Source)
	{
		return Source.Id == SourceId;
	}, /*bAllowShrinking=*/false);
}

void FHapbeatStreamer::Start(double NowSeconds)
{
	StartTimeSeconds = NowSeconds;
	if (WireFormat == EHapbeatStreamWireFormat::SessionV2)
	{
		SendFn(FHapbeatProtocol::BuildStreamBeginV2(
			NextSeqFn(),
			Identity,
			static_cast<uint16>(SampleRate),
			static_cast<uint8>(WireChannels),
			FHapbeatProtocol::AudioFormatPcm16,
			/*TotalSamples=*/0,
			/*Gain=*/1.0f,
			Target));
		return;
	}
	SendFn(FHapbeatProtocol::BuildStreamBegin(
		NextSeqFn(),
		static_cast<uint16>(SampleRate),
		static_cast<uint8>(WireChannels),
		FHapbeatProtocol::AudioFormatPcm16,
		/*TotalSamples=*/0,
		/*Gain=*/1.0f,
		Target));
}

void FHapbeatStreamer::FinishSource(int32 SourceIndex)
{
	FinishedSourceIds.Add(Sources[SourceIndex].Id);
	Sources.RemoveAtSwap(SourceIndex, 1, /*bAllowShrinking=*/false);
}

void FHapbeatStreamer::DrainFinishedSourceIds(TArray<FGuid>& OutSourceIds)
{
	OutSourceIds.Append(MoveTemp(FinishedSourceIds));
	FinishedSourceIds.Reset();
}

void FHapbeatStreamer::Tick(double NowSeconds)
{
	if (bDone || Sources.Num() == 0)
	{
		return;
	}

	const int32 FramesPerChunk = MixScratch.Num() / WireChannels;
	double SentDuration = static_cast<double>(TotalFramesSent) / static_cast<double>(SampleRate);
	double Lead = SentDuration - (NowSeconds - StartTimeSeconds);
	int32 SafetyBudget = 4096;

	while (Lead < SendAheadSeconds && Sources.Num() > 0 && --SafetyBudget >= 0)
	{
		FMemory::Memzero(MixScratch.GetData(), MixScratch.Num() * sizeof(float));

		// Each source starts at output frame zero for this chunk. A short one-shot
		// contributes its remaining tail and leaves the rest for its siblings.
		for (int32 SourceIndex = Sources.Num() - 1; SourceIndex >= 0; --SourceIndex)
		{
			FSource& Source = Sources[SourceIndex];
			if (Source.Mirror->bStopped.load(std::memory_order_relaxed)
				|| Source.Pcm16.Num() < SrcBytesPerFrame)
			{
				FinishSource(SourceIndex);
				continue;
			}

			const float Gain = Source.Mirror->Gain.load(std::memory_order_relaxed);
			const float Pan = Source.Mirror->Pan.load(std::memory_order_relaxed);
			const float TargetGainL = Gain * (Pan <= 0.0f ? 1.0f : 1.0f - Pan);
			const float TargetGainR = Gain * (Pan >= 0.0f ? 1.0f : 1.0f + Pan);

			// §5.7: a changed gain / pan ramps linearly from the previous chunk's
			// channel gains over this chunk (no zipper step at the chunk boundary).
			// A new source starts at its own gains; its PCM carries the onset.
			const float StartGainL = Source.bHasLastGains ? Source.LastGainL : TargetGainL;
			const float StartGainR = Source.bHasLastGains ? Source.LastGainR : TargetGainR;
			Source.LastGainL = TargetGainL;
			Source.LastGainR = TargetGainR;
			Source.bHasLastGains = true;
			const float StepL = (TargetGainL - StartGainL) / static_cast<float>(FramesPerChunk);
			const float StepR = (TargetGainR - StartGainR) / static_cast<float>(FramesPerChunk);
			const bool bRamp = StepL != 0.0f || StepR != 0.0f;

			// Resampling read: the fractional position advances by the playback rate
			// and samples are linearly interpolated. At rate 1 the position stays on
			// whole frames, so the output is bit-identical to a plain copy.
			const double Rate = FMath::Clamp(
				static_cast<double>(Source.Mirror->PlaybackRate.load(std::memory_order_relaxed)), 0.25, 4.0);
			const bool bLoopNow = Source.Mirror->bLoop.load(std::memory_order_relaxed);
			const int32 NumFrames = Source.Pcm16.Num() / SrcBytesPerFrame;
			const uint8* Base = Source.Pcm16.GetData();
			auto Read = [&](int32 Frame, int32 Channel)
			{
				return static_cast<float>(ReadPcm16Le(Base + (Frame * Channels + Channel) * 2));
			};
			bool bFinished = false;
			for (int32 OutFrame = 0; OutFrame < FramesPerChunk; ++OutFrame)
			{
				if (Source.ReadFrame >= NumFrames)
				{
					if (!bLoopNow)
					{
						bFinished = true;
						break;
					}
					Source.ReadFrame = FMath::Fmod(Source.ReadFrame, static_cast<double>(NumFrames));
				}
				const int32 I0 = static_cast<int32>(Source.ReadFrame);
				const float Frac = static_cast<float>(Source.ReadFrame - I0);
				const int32 I1 = I0 + 1 < NumFrames ? I0 + 1 : (bLoopNow ? 0 : I0);
				const float GainL = bRamp ? StartGainL + StepL * static_cast<float>(OutFrame + 1) : TargetGainL;
				const float GainR = bRamp ? StartGainR + StepR * static_cast<float>(OutFrame + 1) : TargetGainR;
				if (Channels == 1)
				{
					const float Sample = FMath::Lerp(Read(I0, 0), Read(I1, 0), Frac);
					if (WireChannels == 2)
					{
						MixScratch[OutFrame * 2] += Sample * GainL;
						MixScratch[OutFrame * 2 + 1] += Sample * GainR;
					}
					else
					{
						MixScratch[OutFrame] += Sample * Gain;
					}
				}
				else
				{
					for (int32 Channel = 0; Channel < Channels; ++Channel)
					{
						const float ChannelGain = Channel == 0 ? GainL : (Channel == 1 ? GainR : Gain);
						MixScratch[OutFrame * WireChannels + Channel] +=
							FMath::Lerp(Read(I0, Channel), Read(I1, Channel), Frac) * ChannelGain;
					}
				}
				Source.ReadFrame += Rate;
			}
			if (!bLoopNow && Source.ReadFrame >= NumFrames)
			{
				bFinished = true;
			}

			if (bFinished)
			{
				FinishSource(SourceIndex);
			}
		}

		ApplyLimiter(FramesPerChunk);

		// §5.3: the single final saturation; the limiter keeps the sum in range.
		for (int32 SampleIndex = 0; SampleIndex < MixScratch.Num(); ++SampleIndex)
		{
			const float Clamped = FMath::Clamp(MixScratch[SampleIndex], -32768.0f, 32767.0f);
			const uint16 Pcm = static_cast<uint16>(static_cast<int16>(Clamped));
			PcmScratch[SampleIndex * 2] = static_cast<uint8>(Pcm & 0xFF);
			PcmScratch[SampleIndex * 2 + 1] = static_cast<uint8>((Pcm >> 8) & 0xFF);
		}

		SendFn(WireFormat == EHapbeatStreamWireFormat::SessionV2
			? FHapbeatProtocol::BuildStreamDataV2(
				NextSeqFn(), Identity, WireOffset, PcmScratch.GetData(), PcmScratch.Num())
			: FHapbeatProtocol::BuildStreamData(
				NextSeqFn(), WireOffset, PcmScratch.GetData(), PcmScratch.Num()));
		WireOffset += static_cast<uint32>(PcmScratch.Num());
		TotalFramesSent += FramesPerChunk;

		SentDuration = static_cast<double>(TotalFramesSent) / static_cast<double>(SampleRate);
		Lead = SentDuration - (NowSeconds - StartTimeSeconds);
	}
}

void FHapbeatStreamer::ApplyLimiter(int32 FramesPerChunk)
{
	float Peak = 0.0f;
	for (const float Value : MixScratch)
	{
		Peak = FMath::Max(Peak, FMath::Abs(Value));
	}
	const float Want = Peak > FullScale ? LimitTarget / Peak : 1.0f;
	const float Start = LimiterGain;
	// Attack (lower gain) lands within this chunk; release rises at most +0.05 per chunk.
	const float End = Want < Start ? Want : FMath::Min(Want, Start + LimitReleasePerChunk);
	LimiterGain = End;
	if (Start == 1.0f && End == 1.0f)
	{
		// Peak is within full scale and no reduction is active: bit-identical pass-through.
		return;
	}

	const float Step = (End - Start) / static_cast<float>(FramesPerChunk);
	constexpr float KneeSpan = FullScale - LimitKnee;
	for (int32 Frame = 0; Frame < FramesPerChunk; ++Frame)
	{
		const float FrameGain = Start + Step * static_cast<float>(Frame + 1);
		for (int32 Channel = 0; Channel < WireChannels; ++Channel)
		{
			float& Sample = MixScratch[Frame * WireChannels + Channel];
			const float Scaled = Sample * FrameGain;
			const float Magnitude = FMath::Abs(Scaled);
			Sample = Magnitude > LimitKnee
				? FMath::Sign(Scaled) * (LimitKnee + KneeSpan * FMath::Tanh((Magnitude - LimitKnee) / KneeSpan))
				: Scaled;
		}
	}
}

void FHapbeatStreamer::RebasePacing(double NowSeconds)
{
	StartTimeSeconds = NowSeconds - static_cast<double>(TotalFramesSent) / static_cast<double>(SampleRate)
		+ SendAheadSeconds;
}

void FHapbeatStreamer::SendEnd()
{
	if (bDone)
	{
		return;
	}
	Sources.Empty();
	bDone = true;
	SendFn(WireFormat == EHapbeatStreamWireFormat::SessionV2
		? FHapbeatProtocol::BuildStreamEndV2(NextSeqFn(), Identity)
		: FHapbeatProtocol::BuildStreamEnd(NextSeqFn()));
}
