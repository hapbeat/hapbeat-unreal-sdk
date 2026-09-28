// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatStreamWorker.h"

#include "HapbeatStreamRunnable.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"

namespace
{
	/** Idle wait between stop-flag checks when no session is active. */
	constexpr uint32 IdleWaitMilliseconds = 100;
}

FHapbeatStreamWorker::FHapbeatStreamWorker()
	: WakeEvent(FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false))
{
}

FHapbeatStreamWorker::~FHapbeatStreamWorker()
{
	Shutdown();
	FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
	WakeEvent = nullptr;
}

bool FHapbeatStreamWorker::Start()
{
	if (Thread != nullptr)
	{
		return true;
	}
	bStopRequested.store(false, std::memory_order_release);
	Thread = FRunnableThread::Create(this, TEXT("HapbeatStreamWorker"), 0, TPri_AboveNormal);
	return Thread != nullptr;
}

void FHapbeatStreamWorker::Shutdown()
{
	if (Thread != nullptr)
	{
		// Kill -> Stop(): Run() leaves its loop, runs the final pass (every session
		// was flagged by the caller, so each sends or suppresses its END) and
		// returns. The one join of the worker's life.
		Thread->Kill(true);
		delete Thread;
		Thread = nullptr;
	}
	FScopeLock Lock(&IncomingMutex);
	Incoming.Reset();
}

void FHapbeatStreamWorker::AddSession(const FSessionPtr& Session)
{
	if (!Session.IsValid())
	{
		return;
	}
	{
		FScopeLock Lock(&IncomingMutex);
		Incoming.Add(Session);
	}
	Wake();
}

void FHapbeatStreamWorker::Wake()
{
	bWorkPending.store(true, std::memory_order_release);
	WakeEvent->Trigger();
}

uint32 FHapbeatStreamWorker::Run()
{
	while (!bStopRequested.load(std::memory_order_acquire))
	{
		const double PassStart = FPlatformTime::Seconds();
		// Cleared BEFORE the pass: work handed over during the pass sets it again
		// and cuts the following wait short.
		bWorkPending.store(false, std::memory_order_release);
		RunPass();
		if (Active.Num() == 0)
		{
			// Auto-reset: a Trigger() issued after RunPass drained the queue is
			// kept and ends this wait at once, so no hand-over is missed.
			WakeEvent->Wait(IdleWaitMilliseconds);
		}
		else
		{
			WaitForNextPass(PassStart + FHapbeatStreamRunnable::TickIntervalSeconds);
		}
	}

	// Final pass: the owner flagged every session Stop()/Abandon() before asking
	// the worker to stop, so a single Step() ends each one (END, or none for
	// Abandon). Sessions still arriving are admitted and ended the same way.
	RunPass();
	Active.Reset();
	return 0;
}

void FHapbeatStreamWorker::Stop()
{
	bStopRequested.store(true, std::memory_order_release);
	WakeEvent->Trigger();
}

void FHapbeatStreamWorker::WaitForNextPass(double TargetSeconds)
{
	// Same pacing as FHapbeatStreamRunnable::PreciseWaitUntil (spin on Windows, whose
	// sleep floors to ~15.6 ms; 1 ms sleep slices then a short spin elsewhere), but a
	// hand-over or stop request ends it at once. An early pass is harmless: every
	// streamer paces its sends from the clock, not from the pass count.
	auto Interrupted = [this]()
	{
		return bStopRequested.load(std::memory_order_relaxed) || bWorkPending.load(std::memory_order_acquire);
	};
#if !PLATFORM_WINDOWS
	while (!Interrupted() && FPlatformTime::Seconds() < TargetSeconds - 0.0015)
	{
		FPlatformProcess::SleepNoStats(0.001f);
	}
#endif
	while (!Interrupted() && FPlatformTime::Seconds() < TargetSeconds)
	{
		FPlatformProcess::Yield();
	}
}

void FHapbeatStreamWorker::RunPass()
{
	TArray<FSessionPtr> Arrived;
	{
		FScopeLock Lock(&IncomingMutex);
		Arrived = MoveTemp(Incoming);
		Incoming.Reset();
	}
	for (FSessionPtr& Session : Arrived)
	{
		// Init() runs HERE so the session's streamer and FInternetAddr objects
		// belong to this thread. A failed Init() already published bFinished.
		if (Session->Init())
		{
			Active.Add(MoveTemp(Session));
		}
	}

	for (int32 Index = 0; Index < Active.Num();)
	{
		if (Active[Index]->Step(FPlatformTime::Seconds()))
		{
			++Index;
		}
		else
		{
			// RemoveAt keeps the remaining sessions in admission order.
			Active.RemoveAt(Index);
		}
	}
}
