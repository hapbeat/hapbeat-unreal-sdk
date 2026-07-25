// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatStreamRunnable.h"

#include "HapbeatStreamer.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Sockets.h"
#include "SocketSubsystem.h" // transitively defines FInternetAddr (IPAddress.h)

// Separate category from the subsystem's LogHapbeat: this one is written from
// the stream thread, and its Verbose per-chunk lines are easier to filter alone.
DEFINE_LOG_CATEGORY_STATIC(LogHapbeatStream, Log, All);

namespace
{
	// Target cadence between Run() loop iterations. FHapbeatStreamer's own
	// send-ahead/lead math decides WHETHER a chunk actually goes out each call;
	// this just governs how often we ask.
	constexpr double TickIntervalSeconds = 0.010;

	/**
	 * Busy-wait until TargetSeconds, checking the stop flag as we go.
	 *
	 * DELIBERATELY A SPIN, NOT A SLEEP — this is the whole point of Unity's
	 * 7c0aafc fix, ported. On Windows, Sleep()/SleepNoStats() floors to the OS
	 * timer-resolution period: a requested 1ms can actually sleep ~15.6ms
	 * unless the process raised timer resolution (which a plugin cannot
	 * assume). At our 10ms cadence ANY sleep can therefore overshoot the whole
	 * interval, which is exactly the bug Unity hit — the send rate falls
	 * permanently below real time and the stream degrades into a regular
	 * play/stop stutter. So we spin.
	 *
	 * FPlatformProcess::Yield() is the CPU pause instruction (_mm_pause on x86,
	 * __yield on ARM — verified in GenericPlatformProcess.h), i.e. the direct
	 * equivalent of Unity's Thread.SpinWait: it hints the core to back off
	 * (power / hyperthread-sibling friendly) WITHOUT a scheduler syscall. It is
	 * still a busy-wait: expect this thread to hold ~1 core for the duration of
	 * a stream. That cost is accepted for glitch-free haptics (and matches the
	 * Unity SDK); revisit only with a measured alternative — a plain sleep here
	 * reintroduces the original bug, and an FEvent-based wait would also need
	 * to keep Kill(true) fast (see StopStream()).
	 */
	void PreciseWaitUntil(double TargetSeconds, const std::atomic<bool>& bStopRequested)
	{
		while (!bStopRequested.load(std::memory_order_relaxed)
			&& FPlatformTime::Seconds() < TargetSeconds)
		{
			FPlatformProcess::Yield();
		}
	}
}

FHapbeatStreamRunnable::FHapbeatStreamRunnable(
	TArray<uint8>&& InPcm16,
	int32 InSampleRate,
	int32 InChannels,
	const FString& InTarget,
	bool bInLoop,
	TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InMirror,
	TFunction<uint16()> InNextSeq,
	FSocket* InSocket,
	int32 InPort,
	TArray<FString> InUnicastTargetIps,
	float InSendAheadSeconds)
	// Initializer order matches declaration order in the header (Socket ..
	// Mirror) to avoid -Wreorder; see the header for the full member list.
	: Socket(InSocket)
	, Port(InPort)
	, UnicastTargetIps(MoveTemp(InUnicastTargetIps))
	, SendAheadSeconds(InSendAheadSeconds)
	, NextSeqFn(MoveTemp(InNextSeq))
	, PendingPcm16(MoveTemp(InPcm16))
	, PendingSampleRate(InSampleRate)
	, PendingChannels(InChannels)
	, PendingTarget(InTarget)
	, bPendingLoop(bInLoop)
	, Mirror(InMirror)
{
}

FHapbeatStreamRunnable::~FHapbeatStreamRunnable() = default;

bool FHapbeatStreamRunnable::Init()
{
	// Runs on the NEW thread, before Run(). Build this thread's OWN FInternetAddr
	// instances from the plain IP/port values captured at construction — never
	// share the game thread's FInternetAddr objects (see the class doc's
	// threading contract).
	// ISocketSubsystem::Get() is safe to call from this thread: the subsystem is
	// already resolved and cached by the time any StreamClip() call can reach
	// here (the game thread has always opened the socket via Connect() first).
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		// Run() is not called when Init() returns false — but FRunnableThread::Create()
		// still hands back a VALID thread object (verified in UE 5.4
		// MicrosoftRunnableThread.h CreateInternal: `return Thread != NULL`, i.e. the
		// OS-thread result only; Init()'s bool is never propagated). So the game-thread
		// watchdog would poll IsFinished() forever and IsStreaming() would be stuck
		// true. Publish completion here so StopStream() cleans up on the next tick.
		bFinished.store(true, std::memory_order_release);
		return false;
	}

	LocalBroadcastAddr = SocketSubsystem->CreateInternetAddr();
	bool bBroadcastValid = false;
	LocalBroadcastAddr->SetIp(TEXT("255.255.255.255"), bBroadcastValid);
	LocalBroadcastAddr->SetPort(Port);
	if (!bBroadcastValid)
	{
		// Would otherwise degrade into a silent no-op stream (SendRaw's
		// broadcast fallback sends nothing) with no way to tell why.
		UE_LOG(LogHapbeatStream, Warning,
			TEXT("Stream thread: failed to build the broadcast address; the broadcast fallback will not send."));
	}

	LocalUnicastTargets.Reserve(UnicastTargetIps.Num());
	for (const FString& Ip : UnicastTargetIps)
	{
		TSharedPtr<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
		bool bIsValid = false;
		Addr->SetIp(*Ip, bIsValid);
		if (bIsValid)
		{
			Addr->SetPort(Port);
			LocalUnicastTargets.Add(Addr);
		}
	}

	// Build the streamer HERE (worker thread) so its ctor-time scratch-buffer
	// allocation, and every byte of mutable state it subsequently owns, belongs
	// to this thread from the moment it exists (single-writer, no locks).
	Streamer = MakeUnique<FHapbeatStreamer>(
		MoveTemp(PendingPcm16),
		PendingSampleRate,
		PendingChannels,
		PendingTarget,
		bPendingLoop,
		Mirror,
		NextSeqFn,
		[this](const TArray<uint8>& Packet) { SendRaw(Packet); },
		SendAheadSeconds);

	return true;
}

uint32 FHapbeatStreamRunnable::Run()
{
	// Defensive only: FRunnable's contract is that Run() is not called when
	// Init() returned false, and Init() cannot leave Streamer null on its
	// success path (MakeUnique never returns null). Kept so a future edit to
	// Init() can't silently produce a thread that never publishes completion.
	if (!Streamer.IsValid())
	{
		bFinished.store(true, std::memory_order_release);
		return 1;
	}

	Streamer->Start(FPlatformTime::Seconds());

	while (!Streamer->IsDone())
	{
		if (bStopRequested.load(std::memory_order_relaxed))
		{
			// Idempotent; the only other caller is Tick()'s own natural-EOF /
			// mirror-stopped path below, and only ONE of the two paths is ever
			// taken per session (see the class doc's END-uniqueness argument).
			Streamer->SendEnd();
			break;
		}

		const double IterationStart = FPlatformTime::Seconds();
		Streamer->Tick(IterationStart);

		if (!Streamer->IsDone())
		{
			PreciseWaitUntil(IterationStart + TickIntervalSeconds, bStopRequested);
		}
	}

	// Last statement: by the time a poller observes bFinished, STREAM_END has
	// unconditionally already been sent (either by the break above, or inside
	// Streamer->Tick() on the natural-EOF / mirror-stopped path, which is why
	// the while condition re-checks IsDone() before looping again).
	bFinished.store(true, std::memory_order_release);
	return 0;
}

void FHapbeatStreamRunnable::Stop()
{
	// Called by the game thread (FRunnableThread::Kill -> Runnable->Stop()).
	// Just flags the request; Run() notices it (and sends STREAM_END) at the
	// top of its next loop iteration, or promptly mid-wait (PreciseWaitUntil
	// polls this flag too, instead of sleeping out a stale pacing target).
	bStopRequested.store(true, std::memory_order_relaxed);
}

void FHapbeatStreamRunnable::SendRaw(const TArray<uint8>& Packet)
{
	// Worker-thread only. Socket is guaranteed valid for this object's entire
	// lifetime (see the class doc's threading contract).
	if (Socket == nullptr)
	{
		return;
	}

	if (LocalUnicastTargets.Num() == 0)
	{
		if (LocalBroadcastAddr.IsValid())
		{
			int32 BytesSent = 0;
			Socket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *LocalBroadcastAddr);
		}
		return;
	}

	// Same bytes, same seq, to every unicast target (parity with the game-thread
	// SendStreamPacket / Unity SendStreamRaw). A single target failing doesn't
	// stop the others.
	for (const TSharedPtr<FInternetAddr>& Addr : LocalUnicastTargets)
	{
		if (!Addr.IsValid())
		{
			continue;
		}
		int32 BytesSent = 0;
		if (!Socket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *Addr))
		{
			// Verbose (not Warning): this fires per chunk (~100/s) if a device
			// drops off mid-stream, and the session deliberately survives it.
			UE_LOG(LogHapbeatStream, Verbose,
				TEXT("Stream unicast send to %s failed; continuing with the other targets."),
				*Addr->ToString(/*bAppendPort=*/true));
		}
	}
}
