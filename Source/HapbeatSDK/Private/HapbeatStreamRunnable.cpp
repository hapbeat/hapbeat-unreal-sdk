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
	float InSendAheadSeconds)
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
{
	PendingSources.Emplace(InSourceId, MoveTemp(InPcm16), InMirror);
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
	// allocation belongs to this thread. Publish it under the same barrier used
	// by DetachSource(), which may have recorded a pre-init tombstone.
	TUniquePtr<FHapbeatStreamer> NewStreamer = MakeUnique<FHapbeatStreamer>(
		SessionSampleRate,
		SessionChannels,
		SessionTarget,
		InitialMirror,
		NextSeqFn,
		[this](const TArray<uint8>& Packet) { SendRaw(Packet); },
		SendAheadSeconds);
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

	{
		FScopeLock Lock(&StreamerMutex);
		Streamer->Start(FPlatformTime::Seconds());
	}

	while (!Streamer->IsDone())
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
			break;
		}

		// Additions and the final empty decision share SourceMutex. If this returns
		// false, admission was closed while holding that lock, so no game-thread
		// AddSource can be accepted behind the worker's back after STREAM_END.
		if (!DrainPendingSourcesOrClose())
		{
			if (!bAbandonRequested.load(std::memory_order_acquire))
			{
				FScopeLock Lock(&StreamerMutex);
				Streamer->SendEnd();
			}
			break;
		}

		TArray<FGuid> Finished;
		const double IterationStart = FPlatformTime::Seconds();
		{
			FScopeLock Lock(&StreamerMutex);
			Streamer->Tick(IterationStart);
			Streamer->DrainFinishedSourceIds(Finished);
		}
		if (Finished.Num() > 0)
		{
			FScopeLock Lock(&SourceMutex);
			FinishedSourceIds.Append(MoveTemp(Finished));
		}

		if (!Streamer->IsDone())
		{
			PreciseWaitUntil(IterationStart + TickIntervalSeconds, bStopRequested);
		}
	}

	// Last statement: by the time a poller observes bFinished, the worker has
	// either sent the ordinary END or deliberately suppressed it for Abandon.
	bFinished.store(true, std::memory_order_release);
	return 0;
}

void FHapbeatStreamRunnable::Stop()
{
	// Called by the game thread (FRunnableThread::Kill -> Runnable->Stop()).
	// Just flags the request; Run() notices it (and sends STREAM_END) at the
	// top of its next loop iteration, or promptly mid-wait (PreciseWaitUntil
	// polls this flag too, instead of sleeping out a stale pacing target).
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

bool FHapbeatStreamRunnable::DrainPendingSourcesOrClose()
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
			Streamer->RebasePacing(FPlatformTime::Seconds());
		}
		EmptySinceSeconds = -1.0;
		return true;
	}
	const double Now = FPlatformTime::Seconds();
	if (EmptySinceSeconds < 0.0)
	{
		EmptySinceSeconds = Now;
		return true;
	}
	if (Now - EmptySinceSeconds < 0.300)
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

	// Worker-thread only. Socket is guaranteed valid for this object's entire
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
