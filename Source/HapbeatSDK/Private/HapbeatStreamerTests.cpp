// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatStreamer.h"

#include "HapbeatProtocol.h"
#include "HapbeatStreamPlayback.h"
#include "HapbeatStreamRunnable.h"
#include "HapbeatSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
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
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror)
			: FHapbeatStreamRunnable(SourceId, MoveTemp(Pcm16), 16000, 2,
				TEXT("player_1/pos_l_arm"), Mirror, []() { return static_cast<uint16>(1); },
				nullptr, 7700, OneEndpoint(TEXT("192.0.2.10")), 0.05f)
		{
		}

		bool SnapshotEndpoint(FString& OutIp, int32& OutPort) const
		{
			return GetSingleEndpoint(OutIp, OutPort);
		}

		std::atomic<int32> BeginCount{0};
		std::atomic<int32> DataCount{0};
		std::atomic<int32> EndCount{0};

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
				DataCount.fetch_add(1, std::memory_order_relaxed);
			}
			else if (Packet[3] == FHapbeatProtocol::CmdStreamEnd)
			{
				EndCount.fetch_add(1, std::memory_order_relaxed);
			}
		}
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
	UHapbeatSubsystem* Live = NewTestSubsystem();
	Live->RegisterStreamEndpoint(TEXT("192.0.2.10"), 7700, TEXT("player_1/pos_l_arm"), 100.0);
	Live->RegisterStreamEndpoint(TEXT("192.0.2.10"), 7701, TEXT("player_2/pos_l_arm"), 101.0);
	Live->RegisterStreamEndpoint(TEXT("192.0.2.11"), 7700, TEXT("player_1/pos_l_arm"), 102.0);
	TestEqual(TEXT("live exact endpoints sharing IP/address remain distinct"),
		Live->StreamEndpoints.Num(), 3);

	UHapbeatSubsystem* SameRoute = NewTestSubsystem();
	SameRoute->RegisterStreamEndpoint(TEXT("192.0.2.20"), 7700, TEXT("player_1/pos_l_arm"), 100.0);
	SameRoute->RegisterStreamEndpoint(TEXT("192.0.2.20"), 7700, TEXT("player_2/pos_l_arm"), 101.0);
	TestEqual(TEXT("live same-route exact tuples remain distinct"),
		SameRoute->StreamEndpoints.Num(), 2);

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

	OverrideRouting->SetAddressOverride(3, UHapbeatSubsystem::AddressOverrideDisabled);
	TestEqual(TEXT("unknown P3 is Deferred"), OverridePlayback->GetStatus(), EHapbeatStreamPlaybackStatus::Deferred);
	TestEqual(TEXT("unknown P3 requests one fake PING"), OverrideRouting->StreamDiscoveryRequestCount, 1);
	OverrideRouting->RegisterStreamEndpoint(TEXT("192.0.2.62"), 7700, P3, OverrideNow + 0.01);
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
	TestTrue(TEXT("detached runner initializes"), Detached.Init());
	Detached.DetachSource(DetachedId);
	Detached.Run();
	TestEqual(TEXT("detached source produces no DATA"),
		Detached.DataCount.load(std::memory_order_relaxed), 0);
	TestEqual(TEXT("empty detached session closes normally"),
		Detached.EndCount.load(std::memory_order_relaxed), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
