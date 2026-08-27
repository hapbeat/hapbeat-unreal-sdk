// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatStreamer.h"

#include "HapbeatProtocol.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TArray<uint8> MakeStereoPcm(int16 Sample, int32 Frames)
	{
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Frames * 4);
		const uint16 Value = static_cast<uint16>(Sample);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			for (int32 Channel = 0; Channel < 2; ++Channel)
			{
				Bytes[Frame * 4 + Channel * 2] = static_cast<uint8>(Value & 0xFF);
				Bytes[Frame * 4 + Channel * 2 + 1] = static_cast<uint8>((Value >> 8) & 0xFF);
			}
		}
		return Bytes;
	}

	int16 ReadFirstDataSample(const TArray<uint8>& Packet)
	{
		// Header (8) + STREAM_DATA byte offset (4) + first PCM16 sample.
		const uint16 Value = static_cast<uint16>(Packet[12])
			| (static_cast<uint16>(Packet[13]) << 8);
		return static_cast<int16>(Value);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatMultiSourceStreamerTest,
	"Hapbeat.Streaming.MultiSourceMixing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatMultiSourceStreamerTest::RunTest(const FString& Parameters)
{
	uint16 Sequence = 0;
	TArray<TArray<uint8>> Packets;
	auto LoopMirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
	auto TickMirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
	const FGuid LoopId = FGuid::NewGuid();
	const FGuid TickId = FGuid::NewGuid();

	FHapbeatStreamer Streamer(
		/*SampleRate=*/16000,
		/*Channels=*/2,
		FString(),
		LoopMirror,
		[&Sequence]() { return ++Sequence; },
		[&Packets](const TArray<uint8>& Packet) { Packets.Add(Packet); },
		/*SendAheadSeconds=*/0.05f);

	// The loop is the persistent Z4 bed; the one-frame source is a slider detent.
	// Both must appear in the first mixed sample without creating a second BEGIN.
	Streamer.AddSource(LoopId, MakeStereoPcm(1000, 160), /*bLoop=*/true, LoopMirror);
	Streamer.AddSource(TickId, MakeStereoPcm(2000, 1), /*bLoop=*/false, TickMirror);
	Streamer.Start(/*NowSeconds=*/0.0);
	Streamer.Tick(/*NowSeconds=*/0.0);

	int32 BeginCount = 0;
	int32 DataCount = 0;
	const TArray<uint8>* FirstData = nullptr;
	for (const TArray<uint8>& Packet : Packets)
	{
		if (Packet.Num() < FHapbeatProtocol::HeaderSize)
		{
			continue;
		}
		if (Packet[3] == FHapbeatProtocol::CmdStreamBegin)
		{
			++BeginCount;
		}
		else if (Packet[3] == FHapbeatProtocol::CmdStreamData)
		{
			++DataCount;
			if (FirstData == nullptr)
			{
				FirstData = &Packet;
			}
		}
	}

	TestEqual(TEXT("one wire BEGIN"), BeginCount, 1);
	TestTrue(TEXT("mixed data was sent"), DataCount > 0);
	TestNotNull(TEXT("first data packet"), FirstData);
	if (FirstData != nullptr)
	{
		TestEqual(TEXT("loop + tick are summed"), ReadFirstDataSample(*FirstData), static_cast<int16>(3000));
	}
	TArray<FGuid> FinishedSourceIds;
	Streamer.DrainFinishedSourceIds(FinishedSourceIds);
	TestTrue(TEXT("one-shot source finishes independently"), FinishedSourceIds.Contains(TickId));
	TestFalse(TEXT("loop remains active after one-shot"),
		LoopMirror->bStopped.load(std::memory_order_relaxed));
	TestTrue(TEXT("session retains the loop source"), Streamer.HasSources());

	Streamer.SendEnd();
	TestFalse(TEXT("endpoint END does not stop the logical source on sibling endpoints"),
		LoopMirror->bStopped.load(std::memory_order_relaxed));
	int32 EndCount = 0;
	for (const TArray<uint8>& Packet : Packets)
	{
		if (Packet.Num() >= FHapbeatProtocol::HeaderSize
			&& Packet[3] == FHapbeatProtocol::CmdStreamEnd)
		{
			++EndCount;
		}
	}
	TestEqual(TEXT("one wire END"), EndCount, 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
