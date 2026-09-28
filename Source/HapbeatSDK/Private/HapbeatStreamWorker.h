// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/Runnable.h"
#include <atomic>

class FEvent;
class FHapbeatStreamRunnable;
class FRunnableThread;

/**
 * The ONE persistent thread that paces and sends every endpoint stream session
 * of a UHapbeatSubsystem. Replaces one FRunnableThread per session: creating a
 * thread (UE waits for its Init handshake) and Kill(true)-joining it cost the
 * game thread a visible hitch at every haptic start/end on Quest.
 *
 * Owned by UHapbeatSubsystem; its thread is created with the UDP socket
 * (Connect), never during a gameplay event.
 *
 * Threading contract:
 *   - AddSession() is the only game-thread entry during play. It takes a short
 *     lock on the incoming queue and wakes the worker; it never waits for it.
 *   - Each pass (worker thread only) moves incoming sessions to the end of the
 *     active list, calls Init() on each newly arrived one (so its streamer and
 *     FInternetAddr objects are created on this thread, see
 *     FHapbeatStreamRunnable), then calls Step() on every active session in list
 *     order, dropping those that return false. List order is admission order,
 *     so a session stopped before its successor on the same endpoint was added
 *     sends its END before the successor's first BEGIN.
 *   - With no active session the worker parks on an FEvent (100 ms timeout);
 *     otherwise it paces to the next 10 ms tick with
 *     FHapbeatStreamRunnable::PreciseWaitUntil.
 *   - Shutdown(): the caller first flags every session Stop()/Abandon(). The
 *     worker then runs ONE final pass, in which every session sends its END (or
 *     suppresses it when abandoned), and the thread is joined once. Only after
 *     Shutdown() returns may the socket the sessions send on be destroyed.
 *   - Session lifetime is shared with the subsystem (ThreadSafe TSharedPtr);
 *     whichever side releases last deletes the session.
 */
class FHapbeatStreamWorker final : public FRunnable
{
public:
	using FSessionPtr = TSharedPtr<FHapbeatStreamRunnable, ESPMode::ThreadSafe>;

	FHapbeatStreamWorker();
	virtual ~FHapbeatStreamWorker() override;

	/** Game thread: create the worker thread (TPri_AboveNormal). Returns false if it could not be created. */
	bool Start();

	/** Game thread: run the final pass and join the thread. Idempotent. Sessions must already be flagged. */
	void Shutdown();

	/** Game thread: hand a new session to the worker. Never blocks beyond a short lock. */
	void AddSession(const FSessionPtr& Session);

	bool IsRunning() const { return Thread != nullptr; }

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	/** Worker thread: admit + Init arrivals, then Step every active session once. */
	void RunPass();

	FRunnableThread* Thread = nullptr;
	/** Auto-reset wake for the idle wait; triggered by AddSession() and Stop(). */
	FEvent* WakeEvent = nullptr;
	std::atomic<bool> bStopRequested{false};

	FCriticalSection IncomingMutex;
	/** Sessions handed over by the game thread, in call order. Guarded by IncomingMutex. */
	TArray<FSessionPtr> Incoming;
	/** Admitted sessions in admission order. Worker thread only. */
	TArray<FSessionPtr> Active;
};
