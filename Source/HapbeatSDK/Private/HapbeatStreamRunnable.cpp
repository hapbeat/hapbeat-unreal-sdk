// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatStreamRunnable.h"

#include "HapbeatNetworkSafety.h"

#include "HapbeatStreamer.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "Sockets.h"
#include "SocketSubsystem.h" // transitively defines FInternetAddr (IPAddress.h)

// Separate category from the subsystem's LogHapbeat: this one is written from
// the stream thread, and its Verbose per-chunk lines are easier to filter alone.
DEFINE_LOG_CATEGORY_STATIC(LogHapbeatStream, Log, All);

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
 * still a busy-wait: expect the calling thread (the subsystem's shared
 * FHapbeatStreamWorker, or a runnable driven by its own Run()) to hold ~1 core
 * while any session is active. That cost is accepted for glitch-free haptics
 * (and matches the Unity SDK); revisit only with a measured alternative — a
 * plain sleep here reintroduces the original bug. The worker parks on an
 * FEvent only when it has no session at all.
 *
 * Non-Windows platforms (Android / Quest, Linux, Mac) sleep with ~0.1 ms
 * precision, so there the thread sleeps in 1 ms slices until the last
 * ~1.5 ms and only spins that remainder. On a Quest a whole core spun at
 * above-normal priority competes with the game / render / RHI threads.
 * Each slice re-checks the stop flag, so Kill(true) stays fast.
 *
 * Shared by Run() and FHapbeatStreamWorker::Run(); bStopRequested is the
 * caller's own stop flag.
 */
void FHapbeatStreamRunnable::PreciseWaitUntil(double TargetSeconds, const std::atomic<bool>& bStopRequested)
{
#if !PLATFORM_WINDOWS
	constexpr double SpinTailSeconds = 0.0015;
	while (!bStopRequested.load(std::memory_order_relaxed)
		&& FPlatformTime::Seconds() < TargetSeconds - SpinTailSeconds)
	{
		FPlatformProcess::SleepNoStats(0.001f);
	}
#endif
	while (!bStopRequested.load(std::memory_order_relaxed)
		&& FPlatformTime::Seconds() < TargetSeconds)
	{
		FPlatformProcess::Yield();
	}
}

FHapbeatStreamRunnable::FHapbeatStreamRunnable(
	const FGuid& InSourceId,
	TArray<uint8>&& InPcm16,
	int32 InSampleRate,
	int32 InChannels,
	const FString& InTarget,
	TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InMirror,
	TFunction<uint16()> InNextSeq,
	FSocket* InSocket,
	int32 InPort,
	TArray<FString> InUnicastTargetIps,
	float InSendAheadSeconds,
	EHapbeatStreamWireFormat InWireFormat,
	const FHapbeatStreamSessionIdentity& InIdentity)
	// Initializer order matches declaration order in the header (Socket ..
	// Mirror) to avoid -Wreorder; see the header for the full member list.
	: Socket(InSocket)
	, Port(InPort)
	, UnicastTargetIps(MoveTemp(InUnicastTargetIps))
	, SendAheadSeconds(InSendAheadSeconds)
	, NextSeqFn(MoveTemp(InNextSeq))
	, SessionSampleRate(InSampleRate)
	, SessionChannels(InChannels)
	, SessionTarget(InTarget)
	, InitialMirror(InMirror)
	, WireFormat(InWireFormat)
	, SessionIdentity(InIdentity)
{
	PendingSources.Emplace(InSourceId, MoveTemp(InPcm16), InMirror);
}

FHapbeatStreamRunnable::~FHapbeatStreamRunnable() = default;

bool FHapbeatStreamRunnable::Init()
{
	// Runs on the SENDING thread (the shared FHapbeatStreamWorker when it admits
	// this session, or this runnable's own thread before Run()). Build its OWN FInternetAddr
	// instances from the plain IP/port values captured at construction — never
	// share the game thread's FInternetAddr objects (see the class doc's
	// threading contract).
	// ISocketSubsystem::Get() is safe to call from this thread: the subsystem is
	// already resolved and cached by the time any StreamClip() call can reach
	// here (the game thread has always opened the socket via Connect() first).
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		// Neither Run() nor Step() is called after Init() returns false (the worker
		// drops the session; FRunnableThread skips Run()), yet nothing propagates
		// Init()'s bool to the game thread (verified in UE 5.4
		// MicrosoftRunnableThread.h CreateInternal: `return Thread != NULL`). So the
		// game-thread watchdog would poll IsFinished() forever and IsStreaming()
		// would be stuck true. Publish completion here so TickStream() reaps it.
		bFinished.store(true, std::memory_order_release);
		return false;
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

	// Build the streamer HERE (sending thread) so its ctor-time scratch-buffer
	// allocation belongs to this thread. Publish it under the same barrier used
	// by DetachSource(), which may have recorded a pre-init tombstone.
	TUniquePtr<FHapbeatStreamer> NewStreamer = MakeUnique<FHapbeatStreamer>(
		SessionSampleRate,
		SessionChannels,
		SessionTarget,
		InitialMirror,
		NextSeqFn,
		[this](const TArray<uint8>& Packet) { SendRaw(Packet); },
		SendAheadSeconds,
		WireFormat,
		SessionIdentity);
	{
		// Keep SourceMutex -> StreamerMutex everywhere structural source state and
		// the mixer meet. A detach can arrive before Init() has made Streamer;
		// retain its tombstone and apply it as soon as the object exists.
		FScopeLock SourceLock(&SourceMutex);
		FScopeLock StreamerLock(&StreamerMutex);
		Streamer = MoveTemp(NewStreamer);
		for (const FGuid& SourceId : DetachedSourceIds)
		{
			Streamer->RemoveSource(SourceId);
		}
	}

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

	// Self-paced mode (editor Test Play, runner contract tests): the same Step()
	// the shared FHapbeatStreamWorker drives, on this runnable's own thread.
	for (;;)
	{
		const double IterationStart = FPlatformTime::Seconds();
		if (!Step(IterationStart))
		{
			break;
		}
		PreciseWaitUntil(IterationStart + TickIntervalSeconds, bStopRequested);
	}
	return 0;
}

bool FHapbeatStreamRunnable::Step(double NowSeconds)
{
	if (!Streamer.IsValid())
	{
		bFinished.store(true, std::memory_order_release);
		return false;
	}

	if (!bStarted)
	{
		FScopeLock Lock(&StreamerMutex);
		Streamer->Start(NowSeconds);
		bStarted = true;
	}

	if (!Streamer->IsDone())
	{
		if (bStopRequested.load(std::memory_order_acquire))
		{
			// Idempotent; the only other caller is Tick()'s own natural-EOF /
			// mirror-stopped path below, and only ONE of the two paths is ever
			// taken per session (see the class doc's END-uniqueness argument).
			if (!bAbandonRequested.load(std::memory_order_acquire))
			{
				FScopeLock Lock(&StreamerMutex);
				Streamer->SendEnd();
			}
		}
		// Additions and the final empty decision share SourceMutex. If this returns
		// false, admission was closed while holding that lock, so no game-thread
		// AddSource can be accepted behind the worker's back after STREAM_END.
		else if (!DrainPendingSourcesOrClose(NowSeconds))
		{
			if (!bAbandonRequested.load(std::memory_order_acquire))
			{
				FScopeLock Lock(&StreamerMutex);
				Streamer->SendEnd();
			}
		}
		else
		{
			TArray<FGuid> Finished;
			{
				FScopeLock Lock(&StreamerMutex);
				Streamer->Tick(NowSeconds);
				Streamer->DrainFinishedSourceIds(Finished);
			}
			if (Finished.Num() > 0)
			{
				FScopeLock Lock(&SourceMutex);
				FinishedSourceIds.Append(MoveTemp(Finished));
			}
			if (!Streamer->IsDone())
			{
				return true;
			}
		}
	}

	// Last statement: by the time a poller observes bFinished, the sending
	// thread has either sent the ordinary END or deliberately suppressed it for
	// Abandon.
	bFinished.store(true, std::memory_order_release);
	return false;
}

void FHapbeatStreamRunnable::Stop()
{
	// Called by the game thread (UHapbeatSubsystem::StopStreamSession, or
	// FRunnableThread::Kill -> Runnable->Stop() when Run() drives this runnable
	// on its own thread). Just flags the request; the next Step() notices it and
	// sends STREAM_END. A self-paced Run() also notices it mid-wait
	// (PreciseWaitUntil polls this flag); on the shared worker it lands on the
	// next 10 ms pass.
	{
		FScopeLock Lock(&SourceMutex);
		bAcceptingSources = false;
	}
	bStopRequested.store(true, std::memory_order_release);
}

void FHapbeatStreamRunnable::Abandon()
{
	{
		FScopeLock Lock(&SourceMutex);
		bAcceptingSources = false;
	}
	// Publish abandon before stop: every stop branch that observes the latter
	// must also observe that END is forbidden for this retired route.
	bAbandonRequested.store(true, std::memory_order_release);
	bStopRequested.store(true, std::memory_order_release);
}

void FHapbeatStreamRunnable::UpdateEndpoint(const FString& InIp, int32 InPort)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		return;
	}
	TSharedPtr<FInternetAddr> Address = SocketSubsystem->CreateInternetAddr();
	bool bValid = false;
	Address->SetIp(*InIp, bValid);
	if (!bValid)
	{
		return;
	}
	Address->SetPort(InPort);
	FScopeLock Lock(&DestinationMutex);
	LocalUnicastTargets.Reset();
	LocalUnicastTargets.Add(Address);
}

bool FHapbeatStreamRunnable::GetSingleEndpoint(FString& OutIp, int32& OutPort) const
{
	FScopeLock Lock(&DestinationMutex);
	if (LocalUnicastTargets.Num() != 1 || !LocalUnicastTargets[0].IsValid())
	{
		return false;
	}
	OutIp = LocalUnicastTargets[0]->ToString(/*bAppendPort=*/false);
	OutPort = LocalUnicastTargets[0]->GetPort();
	return true;
}

bool FHapbeatStreamRunnable::IsCompatible(
	int32 InSampleRate, int32 InChannels, const FString& InTarget) const
{
	return InSampleRate == SessionSampleRate
		&& InChannels == SessionChannels
		&& InTarget == SessionTarget;
}

bool FHapbeatStreamRunnable::AddSource(
	const FGuid& InSourceId,
	TArray<uint8>&& InPcm16,
	TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InMirror)
{
	FScopeLock Lock(&SourceMutex);
	if (!bAcceptingSources || bStopRequested.load(std::memory_order_relaxed))
	{
		return false;
	}
	// A later explicit AddSource is a real endpoint rejoin, not the stale
	// constructor source which DetachSource tombstoned before Init().
	DetachedSourceIds.Remove(InSourceId);
	PendingSources.Emplace(InSourceId, MoveTemp(InPcm16), InMirror);
	return true;
}

void FHapbeatStreamRunnable::DetachSource(const FGuid& SourceId)
{
	{
		FScopeLock Lock(&SourceMutex);
		PendingSources.RemoveAllSwap([&SourceId](const FPendingSource& Pending)
		{
			return Pending.SourceId == SourceId;
		}, /*bAllowShrinking=*/false);
		DetachedSourceIds.Add(SourceId);
	}

	// Wait for an in-flight Tick()/SendRaw() to complete, then remove the cursor
	// before another packet can be built. SetAddressOverride relies on this being
	// a synchronous boundary: no old-route STREAM_DATA may follow its return.
	FScopeLock StreamerLock(&StreamerMutex);
	if (Streamer.IsValid())
	{
		Streamer->RemoveSource(SourceId);
	}
}

void FHapbeatStreamRunnable::DrainFinishedSourceIds(TArray<FGuid>& OutSourceIds)
{
	FScopeLock Lock(&SourceMutex);
	OutSourceIds.Append(MoveTemp(FinishedSourceIds));
	FinishedSourceIds.Reset();
}

bool FHapbeatStreamRunnable::DrainPendingSourcesOrClose(double NowSeconds)
{
	FScopeLock SourceLock(&SourceMutex);
	FScopeLock StreamerLock(&StreamerMutex);
	for (FPendingSource& Pending : PendingSources)
	{
		if (!DetachedSourceIds.Contains(Pending.SourceId))
		{
			Streamer->AddSource(Pending.SourceId, MoveTemp(Pending.Pcm16), Pending.Mirror);
		}
	}
	PendingSources.Reset();

	if (Streamer->HasSources())
	{
		if (EmptySinceSeconds >= 0.0)
		{
			Streamer->RebasePacing(NowSeconds);
		}
		EmptySinceSeconds = -1.0;
		return true;
	}
	if (EmptySinceSeconds < 0.0)
	{
		EmptySinceSeconds = NowSeconds;
		return true;
	}
	if (NowSeconds - EmptySinceSeconds < 0.300)
	{
		return true;
	}

	bAcceptingSources = false;
	return false;
}

void FHapbeatStreamRunnable::SendRaw(const TArray<uint8>& Packet)
{
	FScopeLock DestinationLock(&DestinationMutex);
	if (HapbeatIsNetworkSuppressedForEditor())
	{
		return;
	}

	// Sending-thread only. Socket is guaranteed valid for this object's entire
	// lifetime (see the class doc's threading contract).
	if (Socket == nullptr)
	{
		return;
	}

	if (LocalUnicastTargets.Num() == 0)
	{
		// Endpoint sessions never fall back to broadcast.
		return;
	}

	// A single endpoint send failure does not stop the logical source.
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
