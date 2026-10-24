// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatStreamer.h"

#include "HapbeatClip.h"
#include "HapbeatProtocol.h"
#include "HapbeatStreamPlayback.h"
#include "HapbeatStreamRunnable.h"
#include "HapbeatSubsystem.h"
#include "HapbeatTriggerComponent.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "Misc/AutomationTest.h"
#include <thread>

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

	TArray<FString> OneEndpoint(const FString& Ip)
	{
		TArray<FString> Result;
		Result.Add(Ip);
		return Result;
	}

	UHapbeatSubsystem* NewTestSubsystem()
	{
		UGameInstance* GameInstance = NewObject<UGameInstance>();
		return NewObject<UHapbeatSubsystem>(GameInstance);
	}

	class FRecordingStreamRunnable final : public FHapbeatStreamRunnable
	{
	public:
		FRecordingStreamRunnable(const FGuid& SourceId, TArray<uint8>&& Pcm16,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror,
			const FString& Target = TEXT("player_1/pos_l_arm"))
			: FHapbeatStreamRunnable(SourceId, MoveTemp(Pcm16), 16000, 2,
				Target, Mirror, []() { return static_cast<uint16>(1); },
				nullptr, 7700, OneEndpoint(TEXT("192.0.2.10")), 0.05f)
		{
		}

		void BlockFirstDataSend()
		{
			bBlockFirstData.store(true, std::memory_order_release);
			bDataSendEntered.store(false, std::memory_order_release);
			bReleaseBlockedData.store(false, std::memory_order_release);
		}

		void ReleaseBlockedDataSend()
		{
			bReleaseBlockedData.store(true, std::memory_order_release);
		}

		bool SnapshotEndpoint(FString& OutIp, int32& OutPort) const
		{
			return GetSingleEndpoint(OutIp, OutPort);
		}

		std::atomic<int32> BeginCount{0};
		std::atomic<int32> DataCount{0};
		std::atomic<int32> EndCount{0};
		std::atomic<int32> FirstDataOffset{-1};
		std::atomic<bool> bDataSendEntered{false};

	protected:
		virtual void SendRaw(const TArray<uint8>& Packet) override
		{
			if (Packet.Num() < FHapbeatProtocol::HeaderSize)
			{
				return;
			}
			if (Packet[3] == FHapbeatProtocol::CmdStreamBegin)
			{
				BeginCount.fetch_add(1, std::memory_order_relaxed);
			}
			else if (Packet[3] == FHapbeatProtocol::CmdStreamData)
			{
				if (bBlockFirstData.exchange(false, std::memory_order_acq_rel))
				{
					bDataSendEntered.store(true, std::memory_order_release);
					while (!bReleaseBlockedData.load(std::memory_order_acquire))
					{
						FPlatformProcess::Yield();
					}
				}
				if (Packet.Num() >= FHapbeatProtocol::HeaderSize + 4)
				{
					const int32 Offset = static_cast<int32>(Packet[8])
						| (static_cast<int32>(Packet[9]) << 8)
						| (static_cast<int32>(Packet[10]) << 16)
						| (static_cast<int32>(Packet[11]) << 24);
					int32 Expected = -1;
					FirstDataOffset.compare_exchange_strong(Expected, Offset, std::memory_order_relaxed);
				}
				DataCount.fetch_add(1, std::memory_order_relaxed);
			}
			else if (Packet[3] == FHapbeatProtocol::CmdStreamEnd)
			{
				EndCount.fetch_add(1, std::memory_order_relaxed);
			}
		}

	private:
		std::atomic<bool> bBlockFirstData{false};
		std::atomic<bool> bReleaseBlockedData{false};
	};
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
	LoopMirror->bLoop.store(true, std::memory_order_relaxed);
	Streamer.AddSource(LoopId, MakeStereoPcm(1000, 160), LoopMirror);
	Streamer.AddSource(TickId, MakeStereoPcm(2000, 1), TickMirror);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatPlaybackRateStreamerTest,
	"Hapbeat.Streaming.PlaybackRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatPlaybackRateStreamerTest::RunTest(const FString& Parameters)
{
	// A ramp (frame N = N x 100) makes the resampling position visible in the samples.
	auto Ramp = [](int32 Frames)
	{
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Frames * 4);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			const uint16 Value = static_cast<uint16>(static_cast<int16>(Frame * 100));
			for (int32 Channel = 0; Channel < 2; ++Channel)
			{
				Bytes[Frame * 4 + Channel * 2] = static_cast<uint8>(Value & 0xFF);
				Bytes[Frame * 4 + Channel * 2 + 1] = static_cast<uint8>((Value >> 8) & 0xFF);
			}
		}
		return Bytes;
	};
	auto LeftSample = [](const TArray<uint8>& Packet, int32 Frame)
	{
		const int32 At = 12 + Frame * 4; // header (8) + byte offset (4), stereo PCM16
		return static_cast<int16>(static_cast<uint16>(Packet[At]) | (static_cast<uint16>(Packet[At + 1]) << 8));
	};
	auto Run = [&](float Rate, int32 Frames, TArray<TArray<uint8>>& Packets, TArray<FGuid>& Finished)
	{
		uint16 Sequence = 0;
		auto Mirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
		Mirror->PlaybackRate.store(Rate, std::memory_order_relaxed);
		const FGuid Id = FGuid::NewGuid();
		FHapbeatStreamer Streamer(16000, 2, FString(), Mirror, [&Sequence]() { return ++Sequence; },
			[&Packets](const TArray<uint8>& Packet) { Packets.Add(Packet); }, 0.05f);
		Streamer.AddSource(Id, Ramp(Frames), Mirror);
		Streamer.Start(0.0);
		Streamer.Tick(0.0);
		Streamer.DrainFinishedSourceIds(Finished);
		return Id;
	};
	auto FirstData = [](const TArray<TArray<uint8>>& Packets) -> const TArray<uint8>*
	{
		for (const TArray<uint8>& Packet : Packets)
		{
			if (Packet.Num() > 20 && Packet[3] == FHapbeatProtocol::CmdStreamData)
			{
				return &Packet;
			}
		}
		return nullptr;
	};

	TArray<TArray<uint8>> Half; TArray<FGuid> HalfDone;
	Run(0.5f, 64, Half, HalfDone);
	if (const TArray<uint8>* Data = FirstData(Half))
	{
		TestEqual(TEXT("rate 0.5 interpolates between frames"), LeftSample(*Data, 1), static_cast<int16>(50));
		TestEqual(TEXT("rate 0.5 reaches frame 1 on output frame 2"), LeftSample(*Data, 2), static_cast<int16>(100));
	}
	else
	{
		AddError(TEXT("no data at rate 0.5"));
	}

	TArray<TArray<uint8>> Double; TArray<FGuid> DoubleDone;
	const FGuid DoubleId = Run(2.0f, 64, Double, DoubleDone);
	if (const TArray<uint8>* Data = FirstData(Double))
	{
		TestEqual(TEXT("rate 2 skips every other frame"), LeftSample(*Data, 3), static_cast<int16>(600));
	}
	TestTrue(TEXT("rate 2 one-shot ends after half the output frames"), DoubleDone.Contains(DoubleId));

	TArray<TArray<uint8>> Unity; TArray<FGuid> UnityDone;
	Run(1.0f, 64, Unity, UnityDone);
	if (const TArray<uint8>* Data = FirstData(Unity))
	{
		TestEqual(TEXT("rate 1 is a plain copy"), LeftSample(*Data, 5), static_cast<int16>(500));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatRuntimeLoopRejoinTest,
	"Hapbeat.Streaming.RuntimeLoopRejoin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatRuntimeLoopRejoinTest::RunTest(const FString& Parameters)
{
	uint16 Sequence = 0;
	TArray<TArray<uint8>> Packets;
	auto Mirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
	const FGuid SourceId = FGuid::NewGuid();
	FHapbeatStreamer Streamer(
		16000, 2, FString(), Mirror,
		[&Sequence]() { return ++Sequence; },
		[&Packets](const TArray<uint8>& Packet) { Packets.Add(Packet); },
		0.05f);

	Streamer.AddSource(SourceId, MakeStereoPcm(1000, 1), Mirror);
	Streamer.Start(0.0);
	Streamer.Tick(0.0);
	TestFalse(TEXT("non-loop source reaches EOF"), Streamer.HasSources());

	// Runtime state is authoritative when an EOF source is admitted again for
	// a late/rejoined endpoint. AddSource must not restore the authored false.
	Mirror->bLoop.store(true, std::memory_order_relaxed);
	Streamer.AddSource(SourceId, MakeStereoPcm(1000, 1), Mirror);
	Streamer.Tick(0.0);
	TestTrue(TEXT("runtime loop survives EOF rejoin"), Streamer.HasSources());
	TestTrue(TEXT("AddSource preserves live loop mirror"),
		Mirror->bLoop.load(std::memory_order_relaxed));

	Mirror->bLoop.store(false, std::memory_order_relaxed);
	Streamer.Tick(1.0);
	TestFalse(TEXT("runtime loop disable reaches EOF again"), Streamer.HasSources());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatStreamSubsystemRoutingTest,
	"Hapbeat.Streaming.SubsystemRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatStreamSubsystemRoutingTest::RunTest(const FString& Parameters)
{
	// Streams only go to endpoints a matched PONG has classified. These routing
	// cases are about target/route handling, so their endpoints are marked as
	// Stream Gen 1 receivers (firmware before 0.5.0), whose sessions carry no identity.
	auto MarkLegacy = [](UHapbeatSubsystem* Subsystem, const FString& Key)
	{
		Subsystem->StreamEndpoints.FindChecked(Key).LeaseState.Class = EHapbeatStreamEndpointClass::Legacy;
	};

	UHapbeatSubsystem* Live = NewTestSubsystem();
	Live->RegisterStreamEndpoint(TEXT("192.0.2.10"), 7700, TEXT("player_1/pos_l_arm"), 100.0);
	Live->RegisterStreamEndpoint(TEXT("192.0.2.10"), 7701, TEXT("player_2/pos_l_arm"), 101.0);
	Live->RegisterStreamEndpoint(TEXT("192.0.2.11"), 7700, TEXT("player_1/pos_l_arm"), 102.0);
	TestEqual(TEXT("live exact endpoints sharing IP/address remain distinct"),
		Live->StreamEndpoints.Num(), 3);

	// The same route reporting another address is the same device after an
	// address change: it moves to the new key at once, even while alive
	// (sdk-multi-stream.md §4.1 / §4.6), so no old key keeps streaming to it.
	UHapbeatSubsystem* SameRoute = NewTestSubsystem();
	SameRoute->RegisterStreamEndpoint(TEXT("192.0.2.20"), 7700, TEXT("player_1/pos_l_arm"), 100.0);
	SameRoute->RegisterStreamEndpoint(TEXT("192.0.2.20"), 7700, TEXT("player_2/pos_l_arm"), 101.0);
	TestEqual(TEXT("a live same-route address change leaves one endpoint"),
		SameRoute->StreamEndpoints.Num(), 1);
	TestTrue(TEXT("... under the new address"),
		SameRoute->StreamEndpoints.Contains(TEXT("192.0.2.20:7700|player_2/pos_l_arm")));

	UHapbeatSubsystem* Expired = NewTestSubsystem();
	Expired->RegisterStreamEndpoint(TEXT("192.0.2.30"), 7700, TEXT("player_3/pos_l_arm"), 100.0);
	Expired->RegisterStreamEndpoint(TEXT("192.0.2.31"), 7700, TEXT("player_3/pos_l_arm"),
		100.0 + Expired->AliveTimeoutSeconds() + 1.0);
	TestEqual(TEXT("one expired stable-address route migrates"), Expired->StreamEndpoints.Num(), 1);
	TestTrue(TEXT("expired route moved to the new IP"),
		Expired->StreamEndpoints.Contains(TEXT("192.0.2.31:7700|player_3/pos_l_arm")));

	UHapbeatSubsystem* Ambiguous = NewTestSubsystem();
	Ambiguous->RegisterStreamEndpoint(TEXT("192.0.2.50"), 7700, TEXT("player_6/pos_l_arm"), 100.0);
	Ambiguous->RegisterStreamEndpoint(TEXT("192.0.2.51"), 7700, TEXT("player_6/pos_l_arm"), 101.0);
	Ambiguous->RegisterStreamEndpoint(TEXT("192.0.2.52"), 7700, TEXT("player_6/pos_l_arm"),
		101.0 + Ambiguous->AliveTimeoutSeconds() + 1.0);
	TestEqual(TEXT("ambiguous expired candidates remain separate exact tuples"),
		Ambiguous->StreamEndpoints.Num(), 3);

	// Exercise the full subsystem map -> runner UpdateEndpoint -> Reconcile
	// detach path. The old-address source must become Deferred and emit no DATA.
	UHapbeatSubsystem* Migrating = NewTestSubsystem();
	UHapbeatStreamPlayback* Playback = NewObject<UHapbeatStreamPlayback>(Migrating);
	Playback->Init(1.0f, 1.0f);
	const FGuid SourceId = Playback->Id;
	auto Mirror = Playback->GetMirror();
	FRecordingStreamRunnable Runner(SourceId, MakeStereoPcm(1000, 160), Mirror);
	TestTrue(TEXT("migration runner initializes"), Runner.Init());
	const double Now = FPlatformTime::Seconds();
	const FString OldKey = TEXT("192.0.2.40:7700|player_4/pos_l_arm");
	const FString NewKey = TEXT("192.0.2.40:7700|player_5/pos_l_arm");
	Migrating->RegisterStreamEndpoint(TEXT("192.0.2.40"), 7700, TEXT("player_4/pos_l_arm"),
		Now - Migrating->AliveTimeoutSeconds() - 1.0);
	MarkLegacy(Migrating, OldKey);
	UHapbeatSubsystem::FStreamSource& Source = Migrating->StreamSources.Add(SourceId);
	Source.CanonicalPcm16 = MakeStereoPcm(1000, 160);
	Source.ResolvedTarget = TEXT("player_4/pos_l_arm");
	Source.Playback = Playback;
	Source.EndpointKeys.Add(OldKey);
	Migrating->ActivePlaybacks.Add(Playback);
	UHapbeatSubsystem::FStreamSession& Session = Migrating->StreamSessions.Add(OldKey);
	Session.Runnable = &Runner;
	Session.SourceIds.Add(SourceId);
	Playback->SetActive();
	Migrating->RegisterStreamEndpoint(TEXT("192.0.2.40"), 7700, TEXT("player_5/pos_l_arm"), Now + 0.01);
	Migrating->ReconcileStreamSources();
	TestTrue(TEXT("runner migrated without duplicate session"),
		Migrating->StreamSessions.Contains(NewKey) && Migrating->StreamSessions.Num() == 1);
	TestEqual(TEXT("route migration carries the endpoint stream class"),
		Migrating->StreamEndpoints.FindChecked(NewKey).LeaseState.Class, EHapbeatStreamEndpointClass::Legacy);
	TestEqual(TEXT("mismatched migrated source detached from session"),
		Migrating->StreamSessions[NewKey].SourceIds.Num(), 0);
	TestEqual(TEXT("mismatched Playback becomes Deferred"),
		Playback->GetStatus(), EHapbeatStreamPlaybackStatus::Deferred);
	TestEqual(TEXT("mismatched Playback has no endpoint keys"), Source.EndpointKeys.Num(), 0);
	Runner.Run();
	TestEqual(TEXT("detached old-address source emits no DATA"),
		Runner.DataCount.load(std::memory_order_relaxed), 0);
	Migrating->StreamSessions.Empty(); // Runner is stack-owned by this test.

	// Address overrides re-route ACTIVE logical sources without restarting
	// playback. The fake discovery seam proves an unknown target asks for a PING
	// without opening a socket or emitting a real UDP packet.
	UHapbeatSubsystem* OverrideRouting = NewTestSubsystem();
	OverrideRouting->bSuppressStreamDiscoveryForAutomationTest = true;
	const double OverrideNow = FPlatformTime::Seconds();
	const FString P1 = TEXT("player_1/pos_l_arm");
	const FString P2 = TEXT("player_2/pos_l_arm");
	const FString P3 = TEXT("player_3/pos_l_arm");
	const FString P1Key = TEXT("192.0.2.60:7700|player_1/pos_l_arm");
	const FString P2Key = TEXT("192.0.2.61:7700|player_2/pos_l_arm");
	const FString P3Key = TEXT("192.0.2.62:7700|player_3/pos_l_arm");
	OverrideRouting->RegisterStreamEndpoint(TEXT("192.0.2.60"), 7700, P1, OverrideNow);
	OverrideRouting->RegisterStreamEndpoint(TEXT("192.0.2.61"), 7700, P2, OverrideNow);
	MarkLegacy(OverrideRouting, P1Key);
	MarkLegacy(OverrideRouting, P2Key);

	UHapbeatStreamPlayback* OverridePlayback = NewObject<UHapbeatStreamPlayback>(OverrideRouting);
	OverridePlayback->Init(1.0f, 1.0f);
	OverridePlayback->SetActive();
	const FGuid OverrideSourceId = OverridePlayback->Id;
	UHapbeatSubsystem::FStreamSource& OverrideSource = OverrideRouting->StreamSources.Add(OverrideSourceId);
	OverrideSource.CanonicalPcm16 = MakeStereoPcm(1000, 160);
	OverrideSource.AuthoredTarget = P1;
	OverrideSource.ResolvedTarget = P1;
	OverrideSource.Playback = OverridePlayback;
	OverrideSource.EndpointKeys.Add(P1Key);
	OverrideRouting->ActivePlaybacks.Add(OverridePlayback);
	auto OverrideMirror = OverridePlayback->GetMirror();
	FRecordingStreamRunnable OverrideRunner(OverrideSourceId, MakeStereoPcm(1000, 160), OverrideMirror);
	TestTrue(TEXT("override runner initializes"), OverrideRunner.Init());
	UHapbeatSubsystem::FStreamSession& OverrideSession = OverrideRouting->StreamSessions.Add(P1Key);
	OverrideSession.Runnable = &OverrideRunner;
	OverrideSession.SourceIds.Add(OverrideSourceId);

	OverrideRouting->SetAddressOverride(2, UHapbeatSubsystem::AddressOverrideDisabled);
	TestEqual(TEXT("P1 -> known P2 updates effective target"), OverrideSource.ResolvedTarget, P2);
	TestTrue(TEXT("P1 -> known P2 joins immediately"), OverrideSource.EndpointKeys.Contains(P2Key));
	TestFalse(TEXT("P1 -> known P2 leaves old endpoint"), OverrideSource.EndpointKeys.Contains(P1Key));
	OverrideRunner.Run();
	TestEqual(TEXT("P1 -> known P2 stops old endpoint DATA"),
		OverrideRunner.DataCount.load(std::memory_order_relaxed), 0);

	// The known P2 endpoint starts a fresh endpoint session from the logical
	// source's frame zero: one BEGIN, then DATA whose offset is zero. This is the
	// in-memory seam for StartEndpointSession's socket-backed worker.
	FRecordingStreamRunnable P2Rejoin(OverrideSourceId, TArray<uint8>(OverrideSource.CanonicalPcm16),
		OverrideMirror, P2);
	TestTrue(TEXT("known P2 rejoin runner initializes"), P2Rejoin.Init());
	P2Rejoin.Run();
	TestEqual(TEXT("known P2 rejoin sends one BEGIN"),
		P2Rejoin.BeginCount.load(std::memory_order_relaxed), 1);
	TestTrue(TEXT("known P2 rejoin sends DATA"),
		P2Rejoin.DataCount.load(std::memory_order_relaxed) > 0);
	TestEqual(TEXT("known P2 rejoin first DATA starts at frame zero"),
		P2Rejoin.FirstDataOffset.load(std::memory_order_relaxed), 0);

	OverrideRouting->SetAddressOverride(3, UHapbeatSubsystem::AddressOverrideDisabled);
	TestEqual(TEXT("unknown P3 is Deferred"), OverridePlayback->GetStatus(), EHapbeatStreamPlaybackStatus::Deferred);
	TestEqual(TEXT("unknown P3 requests one fake PING"), OverrideRouting->StreamDiscoveryRequestCount, 1);

	OverrideRouting->RegisterStreamEndpoint(TEXT("192.0.2.62"), 7700, P3, OverrideNow + 0.01);
	MarkLegacy(OverrideRouting, P3Key);
	OverrideRouting->ReconcileStreamSources();
	TestTrue(TEXT("PONG-equivalent P3 registration joins without replay"),
		OverrideSource.EndpointKeys.Contains(P3Key));
	TestEqual(TEXT("PONG-equivalent P3 registration activates playback"),
		OverridePlayback->GetStatus(), EHapbeatStreamPlaybackStatus::Active);

	UHapbeatStreamPlayback* SecondPlayback = NewObject<UHapbeatStreamPlayback>(OverrideRouting);
	SecondPlayback->Init(1.0f, 1.0f);
	SecondPlayback->SetActive();
	UHapbeatSubsystem::FStreamSource& SecondSource = OverrideRouting->StreamSources.Add(SecondPlayback->Id);
	SecondSource.CanonicalPcm16 = MakeStereoPcm(500, 160);
	SecondSource.AuthoredTarget = P2;
	SecondSource.ResolvedTarget = P3;
	SecondSource.Playback = SecondPlayback;
	SecondSource.EndpointKeys.Add(P3Key);
	OverrideRouting->ActivePlaybacks.Add(SecondPlayback);
	OverrideRouting->SetAddressOverride(UHapbeatSubsystem::AddressOverrideDisabled,
		UHapbeatSubsystem::AddressOverrideDisabled);
	TestEqual(TEXT("clear restores first authored target"), OverrideSource.ResolvedTarget, P1);
	TestEqual(TEXT("clear restores second authored target"), SecondSource.ResolvedTarget, P2);
	TestTrue(TEXT("clear keeps first source on its authored endpoint"), OverrideSource.EndpointKeys.Contains(P1Key));
	TestTrue(TEXT("clear keeps second source on its authored endpoint"), SecondSource.EndpointKeys.Contains(P2Key));
	TestEqual(TEXT("clear preserves independent logical source routing"),
		OverrideSource.EndpointKeys.Num() + SecondSource.EndpointKeys.Num(), 2);

	// Regression: Z4's Space key must cancel a loop that is Deferred because its
	// selected address has no endpoint. Otherwise restoring P1/P2 later revives
	// an orphaned, full-gain stream that the UI no longer owns.
	UHapbeatSubsystem* DeferredStop = NewTestSubsystem();
	const double DeferredNow = FPlatformTime::Seconds();
	const FString DeferredP1 = TEXT("player_1/pos_l_arm");
	const FString DeferredP1Key = TEXT("192.0.2.70:7700|player_1/pos_l_arm");
	DeferredStop->RegisterStreamEndpoint(TEXT("192.0.2.70"), 7700, DeferredP1, DeferredNow);
	MarkLegacy(DeferredStop, DeferredP1Key);
	UHapbeatStreamPlayback* DeferredPlayback = NewObject<UHapbeatStreamPlayback>(DeferredStop);
	DeferredPlayback->Init(1.0f, 1.0f);
	DeferredPlayback->SetActive();
	UHapbeatSubsystem::FStreamSource& DeferredSource = DeferredStop->StreamSources.Add(DeferredPlayback->Id);
	DeferredSource.CanonicalPcm16 = MakeStereoPcm(1000, 160);
	DeferredSource.AuthoredTarget = DeferredP1;
	DeferredSource.ResolvedTarget = DeferredP1;
	DeferredSource.Playback = DeferredPlayback;
	DeferredSource.EndpointKeys.Add(DeferredP1Key);
	DeferredStop->ActivePlaybacks.Add(DeferredPlayback);
	DeferredStop->SetAddressOverride(3, UHapbeatSubsystem::AddressOverrideDisabled);
	TestEqual(TEXT("unmatched override defers the Z4-style loop"),
		DeferredPlayback->GetStatus(), EHapbeatStreamPlaybackStatus::Deferred);
	UHapbeatTriggerComponent* DeferredTrigger = NewObject<UHapbeatTriggerComponent>(DeferredStop);
	DeferredTrigger->StoredPlayback = DeferredPlayback;
	DeferredTrigger->StopStoredStreamPlayback();
	TestTrue(TEXT("stopping a deferred stream marks its handle stopped"), DeferredPlayback->IsStopped());
	DeferredStop->SetAddressOverride(1, UHapbeatSubsystem::AddressOverrideDisabled);
	TestEqual(TEXT("restoring a known address does not revive a stopped deferred stream"),
		DeferredPlayback->GetStatus(), EHapbeatStreamPlaybackStatus::Stopped);
	TestEqual(TEXT("stopped deferred stream has no rejoined endpoint"), DeferredSource.EndpointKeys.Num(), 0);

	TMap<uint16, FHapbeatPendingPing> UnsolicitedPings;
	FHapbeatPendingPing WrappedPing;
	WrappedPing.SentMonotonicUs = 500;
	WrappedPing.TimestampUs = 1'234;
	WrappedPing.ClientIncarnation = 1;
	UnsolicitedPings.Add(0, WrappedPing);
	TestEqual(TEXT("timestamp-zero unsolicited PONG has no RTT estimate"),
		UHapbeatSubsystem::ResolvePongRttUs(UnsolicitedPings, 0, 1'000, 0, 2'000'000), 0LL);
	TestTrue(TEXT("timestamp-zero unsolicited PONG preserves wrapped pending PING"),
		UnsolicitedPings.Contains(0));
	OverrideRouting->StreamSessions.Empty(); // OverrideRunner is stack-owned by this test.
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatStreamRunnableLifecycleTest,
	"Hapbeat.Streaming.RunnableLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatStreamRunnableLifecycleTest::RunTest(const FString& Parameters)
{
	auto LoopMirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
	LoopMirror->bLoop.store(true, std::memory_order_relaxed);
	FRecordingStreamRunnable Running(FGuid::NewGuid(), MakeStereoPcm(1000, 160), LoopMirror);
	FRunnableThread* Thread = FRunnableThread::Create(&Running, TEXT("HapbeatRunnableContractTest"));
	TestNotNull(TEXT("runner thread starts"), Thread);
	if (Thread != nullptr)
	{
		const double Deadline = FPlatformTime::Seconds() + 1.0;
		while (Running.DataCount.load(std::memory_order_relaxed) == 0
			&& FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Yield();
		}
		TestTrue(TEXT("runner produced fake DATA"),
			Running.DataCount.load(std::memory_order_relaxed) > 0);
		Running.UpdateEndpoint(TEXT("192.0.2.11"), 8800);
		FString Ip;
		int32 Port = 0;
		TestTrue(TEXT("updated endpoint snapshot exists"), Running.SnapshotEndpoint(Ip, Port));
		TestEqual(TEXT("runner route IP migrated in place"), Ip, FString(TEXT("192.0.2.11")));
		TestEqual(TEXT("runner route port migrated in place"), Port, 8800);
		Running.Abandon();
		Thread->Kill(true);
		delete Thread;
		TestEqual(TEXT("Abandon followed by Stop never sends END"),
			Running.EndCount.load(std::memory_order_relaxed), 0);
	}

	auto DetachedMirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
	const FGuid DetachedId = FGuid::NewGuid();
	FRecordingStreamRunnable Detached(DetachedId, MakeStereoPcm(2000, 160), DetachedMirror);
	Detached.DetachSource(DetachedId);
	TestTrue(TEXT("pre-init detached runner initializes"), Detached.Init());
	Detached.Run();
	TestEqual(TEXT("pre-init detached source produces no DATA"),
		Detached.DataCount.load(std::memory_order_relaxed), 0);
	TestEqual(TEXT("pre-init empty session closes normally"),
		Detached.EndCount.load(std::memory_order_relaxed), 1);

	// DetachSource must wait for the packet currently at the send boundary, then
	// guarantee that the looping source produces no later DATA on this endpoint.
	auto RaceMirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
	RaceMirror->bLoop.store(true, std::memory_order_relaxed);
	const FGuid RaceId = FGuid::NewGuid();
	FRecordingStreamRunnable Race(RaceId, MakeStereoPcm(1000, 160), RaceMirror);
	Race.BlockFirstDataSend();
	FRunnableThread* RaceThread = FRunnableThread::Create(&Race, TEXT("HapbeatDetachBarrierTest"));
	TestNotNull(TEXT("race runner thread starts"), RaceThread);
	if (RaceThread != nullptr)
	{
		const double SendDeadline = FPlatformTime::Seconds() + 1.0;
		while (!Race.bDataSendEntered.load(std::memory_order_acquire)
			&& FPlatformTime::Seconds() < SendDeadline)
		{
			FPlatformProcess::Yield();
		}
		TestTrue(TEXT("race runner reaches blocked DATA boundary"),
			Race.bDataSendEntered.load(std::memory_order_acquire));

		std::atomic<bool> bDetachReturned{false};
		std::thread Detacher([&Race, RaceId, &bDetachReturned]()
		{
			Race.DetachSource(RaceId);
			bDetachReturned.store(true, std::memory_order_release);
		});
		Race.ReleaseBlockedDataSend();
		Detacher.join();
		TestTrue(TEXT("detach returns after the packet boundary"),
			bDetachReturned.load(std::memory_order_acquire));
		const int32 DataAtDetachReturn = Race.DataCount.load(std::memory_order_acquire);
		const double QuietDeadline = FPlatformTime::Seconds() + 0.100;
		while (FPlatformTime::Seconds() < QuietDeadline)
		{
			FPlatformProcess::Yield();
		}
		TestEqual(TEXT("no STREAM_DATA follows detach return"),
			Race.DataCount.load(std::memory_order_acquire), DataAtDetachReturn);
		Race.Abandon();
		RaceThread->Kill(true);
		delete RaceThread;
	}
	return true;
}

namespace
{
	/** Drives one FHapbeatStreamer chunk by chunk and keeps the mixed output (interleaved L/R). */
	struct FMixHarness
	{
		uint16 Sequence = 0;
		TArray<int16> Output;
		double Now = 0.0;
		FHapbeatStreamer Streamer;

		FMixHarness()
			: Streamer(16000, 2, FString(), MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>(),
				[this]() { return ++Sequence; },
				[this](const TArray<uint8>& Packet) { Record(Packet); },
				/*SendAheadSeconds=*/0.05f)
		{
		}

		void Record(const TArray<uint8>& Packet)
		{
			// Header (8) + STREAM_DATA byte offset (4), then stereo PCM16.
			if (Packet.Num() <= 12 || Packet[3] != FHapbeatProtocol::CmdStreamData)
			{
				return;
			}
			for (int32 At = 12; At + 1 < Packet.Num(); At += 2)
			{
				Output.Add(static_cast<int16>(static_cast<uint16>(Packet[At])
					| (static_cast<uint16>(Packet[At + 1]) << 8)));
			}
		}

		/** The first Tick fills the send-ahead (several chunks); each later one adds one 10 ms chunk. */
		void Tick()
		{
			Streamer.Tick(Now);
			Now += 0.010;
		}

		TArray<int16> Channel(int32 Index, int32 FromFrame = 0) const
		{
			TArray<int16> Result;
			for (int32 At = FromFrame * 2 + Index; At < Output.Num(); At += 2)
			{
				Result.Add(Output[At]);
			}
			return Result;
		}
	};

	TArray<uint8> MakeStereoSine(float Frequency, int32 Frames, float Amplitude)
	{
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Frames * 4);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			const int16 Sample = static_cast<int16>(Amplitude * 32767.0f
				* FMath::Sin(2.0f * PI * Frequency * static_cast<float>(Frame) / 16000.0f));
			const uint16 Value = static_cast<uint16>(Sample);
			for (int32 Channel = 0; Channel < 2; ++Channel)
			{
				Bytes[Frame * 4 + Channel * 2] = static_cast<uint8>(Value & 0xFF);
				Bytes[Frame * 4 + Channel * 2 + 1] = static_cast<uint8>((Value >> 8) & 0xFF);
			}
		}
		return Bytes;
	}

	int32 MaxStep(const TArray<int16>& Samples)
	{
		int32 Result = 0;
		for (int32 Index = 1; Index < Samples.Num(); ++Index)
		{
			Result = FMath::Max(Result, FMath::Abs(static_cast<int32>(Samples[Index]) - Samples[Index - 1]));
		}
		return Result;
	}

	int32 PeakOf(const TArray<int16>& Samples)
	{
		int32 Result = 0;
		for (const int16 Sample : Samples)
		{
			Result = FMath::Max(Result, FMath::Abs(static_cast<int32>(Sample)));
		}
		return Result;
	}

	constexpr int32 MixChunkFrames = 160; // 10 ms at 16 kHz
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatMixGainRampTest,
	"Hapbeat.Streaming.MixGainRamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatMixGainRampTest::RunTest(const FString& Parameters)
{
	// sdk-multi-stream.md §7 case 8. A constant (DC) source makes any gain step
	// visible: switching 1.0 -> 0.2 at once would be an 18000 step.
	{
		FMixHarness Mix;
		auto Mirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
		Mirror->bLoop.store(true, std::memory_order_relaxed);
		Mix.Streamer.AddSource(FGuid::NewGuid(), MakeStereoPcm(20000, 1600), Mirror);
		Mix.Streamer.Start(0.0);
		Mix.Tick();
		const int32 FramesBefore = Mix.Output.Num() / 2;
		Mirror->Gain.store(0.2f, std::memory_order_relaxed);
		Mirror->Pan.store(0.5f, std::memory_order_relaxed);
		Mix.Tick();
		Mix.Tick();

		const TArray<int16> Left = Mix.Channel(0, FramesBefore - 2);
		const TArray<int16> Right = Mix.Channel(1, FramesBefore - 2);
		TestTrue(TEXT("the later ticks mixed at least two chunks"), Left.Num() >= 2 + 2 * MixChunkFrames);
		// Balance law: L = g (1 - pan) = 0.1, R = g = 0.2, ramped over one chunk.
		TestTrue(TEXT("left gain change is ramped (no zipper step)"),
			MaxStep(Left) <= (20000 - 2000) / MixChunkFrames + 2);
		TestTrue(TEXT("right gain change is ramped (no zipper step)"),
			MaxStep(Right) <= (20000 - 4000) / MixChunkFrames + 2);
		TestTrue(TEXT("left settles at the new gain"), FMath::Abs(Left.Last() - 2000) <= 1);
		TestTrue(TEXT("right settles at the new gain"), FMath::Abs(Right.Last() - 4000) <= 1);
	}

	// A new source starts at its own gain without a ramp; its PCM carries the onset.
	{
		FMixHarness Mix;
		auto Mirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
		Mirror->Gain.store(0.5f, std::memory_order_relaxed);
		Mix.Streamer.AddSource(FGuid::NewGuid(), MakeStereoPcm(10000, 1024), Mirror);
		Mix.Streamer.Start(0.0);
		Mix.Tick();
		TestTrue(TEXT("mixed data was sent"), Mix.Output.Num() >= 2);
		if (Mix.Output.Num() >= 2)
		{
			TestEqual(TEXT("new source first left sample at its gain"), Mix.Output[0], static_cast<int16>(5000));
			TestEqual(TEXT("new source first right sample at its gain"), Mix.Output[1], static_cast<int16>(5000));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatMixLimiterTest,
	"Hapbeat.Streaming.MixLimiter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatMixLimiterTest::RunTest(const FString& Parameters)
{
	// sdk-multi-stream.md §7 case 9: a sum within full scale passes bit-identical,
	// including a peak inside the soft-knee band (0.95..1.0 FS).
	auto RunDc = [this](int16 Sample, int32 Expected, const TCHAR* What)
	{
		FMixHarness Mix;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			Mix.Streamer.AddSource(FGuid::NewGuid(), MakeStereoPcm(Sample, 2048),
				MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>());
		}
		Mix.Streamer.Start(0.0);
		Mix.Tick();
		bool bAllExpected = Mix.Output.Num() > 0;
		for (const int16 Value : Mix.Output)
		{
			bAllExpected &= Value == Expected;
		}
		TestTrue(What, bAllExpected);
	};
	RunDc(15000, 30000, TEXT("sum below the knee is unchanged"));
	RunDc(16000, 32000, TEXT("sum in the knee band but within full scale is unchanged"));

	// §5.8: one full-scale clip containing -32768 (peak 32768) at gain 1 must not
	// trigger the limiter; it passes bit-identical.
	{
		static const int16 Pattern[] = { 32767, -32768, 0, -32768, 16000, -16000 };
		constexpr int32 PatternLength = UE_ARRAY_COUNT(Pattern);
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(1600 * 4);
		for (int32 Frame = 0; Frame < 1600; ++Frame)
		{
			const uint16 Value = static_cast<uint16>(Pattern[Frame % PatternLength]);
			for (int32 Channel = 0; Channel < 2; ++Channel)
			{
				Bytes[Frame * 4 + Channel * 2] = static_cast<uint8>(Value & 0xFF);
				Bytes[Frame * 4 + Channel * 2 + 1] = static_cast<uint8>((Value >> 8) & 0xFF);
			}
		}
		FMixHarness Mix;
		Mix.Streamer.AddSource(FGuid::NewGuid(), MoveTemp(Bytes),
			MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>());
		Mix.Streamer.Start(0.0);
		Mix.Tick();
		Mix.Tick();
		bool bIdentical = Mix.Output.Num() >= 2 * MixChunkFrames;
		for (int32 At = 0; At < Mix.Output.Num(); ++At)
		{
			bIdentical &= Mix.Output[At] == Pattern[(At / 2) % PatternLength];
		}
		TestTrue(TEXT("a single full-scale clip with -32768 passes unchanged"), bIdentical);
	}

	// Four full-gain full-scale sines: limited to the target, never pinned at
	// +-32767, and the waveform stays a sine (no flat tops, no chunk-boundary step).
	constexpr float Frequency = 140.0f;
	FMixHarness Mix;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Mix.Streamer.AddSource(FGuid::NewGuid(), MakeStereoSine(Frequency, 8000, 1.0f),
			MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>());
	}
	Mix.Streamer.Start(0.0);
	for (int32 TickIndex = 0; TickIndex < 16; ++TickIndex)
	{
		Mix.Tick();
	}
	const TArray<int16> Left = Mix.Channel(0);
	TestTrue(TEXT("20 chunks were mixed"), Left.Num() >= 20 * MixChunkFrames);
	TestTrue(TEXT("output stays within PCM16"), PeakOf(Left) <= 32767);

	const TArray<int16> Settled = Mix.Channel(0, 2 * MixChunkFrames);
	const int32 Peak = PeakOf(Settled);
	TestTrue(TEXT("settled peak is limited to the target"),
		Peak >= static_cast<int32>(0.85f * 32767.0f) && Peak <= static_cast<int32>(0.95f * 32767.0f));
	int32 Pinned = 0;
	for (const int16 Value : Settled)
	{
		Pinned += FMath::Abs(static_cast<int32>(Value)) >= 32767 ? 1 : 0;
	}
	TestEqual(TEXT("no settled sample is pinned at full scale"), Pinned, 0);
	const float CleanStep = static_cast<float>(Peak) * 2.0f * FMath::Sin(PI * Frequency / 16000.0f);
	TestTrue(TEXT("limited output keeps sine-like steps (no boundary step)"),
		static_cast<float>(MaxStep(Settled)) <= 1.1f * CleanStep);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHapbeatClipResampleTest,
	"Hapbeat.Streaming.ClipResampleLinear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHapbeatClipResampleTest::RunTest(const FString& Parameters)
{
	// sdk-multi-stream.md §5.9: 8 kHz mono -> 16 kHz stereo interpolates between frames.
	UHapbeatClip* Clip = NewObject<UHapbeatClip>();
	Clip->SampleRate = 8000;
	Clip->NumChannels = 1;
	const int16 Source[] = {0, 1000, 2000, 3000};
	for (const int16 Sample : Source)
	{
		const uint16 Value = static_cast<uint16>(Sample);
		Clip->Pcm16.Add(static_cast<uint8>(Value & 0xFF));
		Clip->Pcm16.Add(static_cast<uint8>((Value >> 8) & 0xFF));
	}

	UHapbeatSubsystem* Subsystem = NewTestSubsystem();
	TArray<uint8> Pcm16;
	TestTrue(TEXT("clip normalises"), Subsystem->NormalizeClipToCanonical(Clip, Pcm16));
	TestEqual(TEXT("8 output frames"), Pcm16.Num(), 8 * 4);
	auto Sample = [&Pcm16](int32 Frame, int32 Channel)
	{
		const int32 At = (Frame * 2 + Channel) * 2;
		return static_cast<int16>(static_cast<uint16>(Pcm16[At]) | (static_cast<uint16>(Pcm16[At + 1]) << 8));
	};
	if (Pcm16.Num() == 8 * 4)
	{
		TestEqual(TEXT("frame 1 is the midpoint (L)"), Sample(1, 0), static_cast<int16>(500));
		TestEqual(TEXT("frame 1 is the midpoint (R)"), Sample(1, 1), static_cast<int16>(500));
		TestEqual(TEXT("frame 2 lands on source frame 1"), Sample(2, 0), static_cast<int16>(1000));
		TestEqual(TEXT("frame 5 is the midpoint"), Sample(5, 0), static_cast<int16>(2500));
		TestEqual(TEXT("last frame holds the final sample"), Sample(7, 0), static_cast<int16>(3000));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
