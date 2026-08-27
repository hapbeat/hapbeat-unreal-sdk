// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/Runnable.h"
#include "HapbeatStreamGainMirror.h"
#include <atomic>

class FHapbeatStreamer;
class FSocket;
class FInternetAddr;

/**
 * Dedicated background thread that paces and sends one wire StreamClip session.
 * Compatible overlapping local sources are mixed into that session before each
 * STREAM_DATA chunk is sent. Replaces the old game-thread FTSTicker-driven model
 * (see the FHapbeatStreamer class doc / dev-notes/unreal-sdk-v1-design.md §5):
 * frame hitches (GC / render / physics spikes) on the game thread used to
 * starve the device ring buffer and cause irregular, audible dropouts — a
 * dedicated thread with its own precise pacing avoids that entirely. The
 * Unity SDK hit the identical bug in production and fixed it the same way
 * (commits 94ec760 / 7c0aafc); this is that fix, ported.
 *
 * Threading contract (read before touching this class):
 *   - Run() owns ALL of FHapbeatStreamer's mutable session state (byte cursor,
 *     wire offset, frames-sent, done flag) — single-writer, so no locks are
 *     needed for any of it.
 *   - The ONLY state shared with the game thread is: the Gain/Pan/bStopped
 *     ATOMIC mirror (FHapbeatStreamGainMirror — never the UHapbeatStreamPlayback
 *     UObject itself, which is unsafe to read from a non-game thread under GC);
 *     the subsystem's Socket raw pointer (valid for this thread's entire
 *     lifetime by construction — UHapbeatSubsystem::StopStream() always
 *     Kill(true)-joins this thread before the subsystem could ever destroy the
 *     socket); and the subsystem's internally-locked NextSeq() callback (shared
 *     with the game thread's Play/Stop/Ping/etc. sends, exactly like Unity's
 *     single locked _sequenceNumber).
 *   - Unicast targets and the broadcast address are captured as PLAIN VALUES
 *     (IP strings / port) at construction time and turned into this thread's
 *     OWN FInternetAddr instances in Init() — never a TSharedPtr<FInternetAddr>
 *     shared with the game thread. (UE5's TSharedPtr default IS ThreadSafe, so
 *     the refcount itself would be fine; the point is that the pointed-to
 *     FInternetAddr is a mutable object with no documented thread-safety
 *     contract, so each thread simply owns its own.)
 *   - STREAM_END is sent from exactly one place — inside Run(), on EITHER the
 *     natural-EOF path (FHapbeatStreamer::Tick already calls SendEnd() and
 *     IsDone() becomes true) OR the stop-requested path (Run() calls SendEnd()
 *     once after noticing bStopRequested). Run() then ALWAYS sets bFinished
 *     as its LAST statement, so by construction there is no double-END /
 *     missing-END race: only this one thread ever calls SendEnd(), and it
 *     always does so before signalling completion to the game-thread poller.
 */
class HAPBEATSDK_API FHapbeatStreamRunnable : public FRunnable
{
public:
	/**
	 * @param InPcm16              COPY of the clip's interleaved LE int16 bytes (moved in).
	 * @param InSampleRate         Hz.
	 * @param InChannels           1 = mono, 2 = stereo.
	 * @param InTarget             Address filter ("" = broadcast), already address-override-resolved.
	 * @param bInLoop              Loop the clip until stopped.
	 * @param InMirror             Thread-safe Gain/Pan/bStopped mirror (never null).
	 * @param InNextSeq            Thread-safe (internally locked) next-seq callback.
	 * @param InSocket             The subsystem's UDP socket. Valid for this object's entire lifetime
	 *                             (see the class doc's threading contract).
	 * @param InPort               UDP port, for building this thread's own broadcast address.
	 * @param InUnicastTargetIps   Snapshot of known-device IPs (plain strings) for stream unicast, or
	 *                             empty to always broadcast. Captured once at session start (Unity parity).
	 * @param InSendAheadSeconds   FHapbeatStreamer pacing lead (UHapbeatConfig::StreamSendAheadSeconds).
	 */
	FHapbeatStreamRunnable(
		const FGuid& InSourceId,
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
		bool bInHasUnicastSnapshot,
		float InSendAheadSeconds,
		const FString& InBroadcastIp);
	virtual ~FHapbeatStreamRunnable() override;

	// FRunnable
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;

	/** True once Run() has returned (STREAM_END already sent). Game-thread poll. */
	bool IsFinished() const { return bFinished.load(std::memory_order_acquire); }

	/** Format/target equality required to join this wire session (Unity parity). */
	bool IsCompatible(int32 InSampleRate, int32 InChannels, const FString& InTarget) const;

	/**
	 * Queue a compatible source from the game thread. Returns false once the
	 * worker has atomically closed admission for natural session completion.
	 */
	bool AddSource(
		const FGuid& InSourceId,
		TArray<uint8>&& InPcm16,
		bool bInLoop,
		TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InMirror);

	/** Game-thread poll: source ids that reached EOF in this endpoint session. */
	void DrainFinishedSourceIds(TArray<FGuid>& OutSourceIds);

private:
	struct FPendingSource
	{
		TArray<uint8> Pcm16;
		FGuid SourceId;
		bool bLoop = false;
		TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror;

		FPendingSource(const FGuid& InSourceId, TArray<uint8>&& InPcm16, bool bInLoop,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InMirror)
			: Pcm16(MoveTemp(InPcm16)), SourceId(InSourceId), bLoop(bInLoop), Mirror(InMirror)
		{
		}
	};

	/** Send one packet to every unicast target, or broadcast if none are set. Worker-thread only. */
	void SendRaw(const TArray<uint8>& Packet);

	/** Worker-thread: drain game-thread additions, or close admission if the session is empty. */
	bool DrainPendingSourcesOrClose();

	// --- construction-time immutable inputs (never written after the ctor) ---
	FSocket* Socket = nullptr;
	int32 Port = 0;
	TArray<FString> UnicastTargetIps;
	/**
	 * True when the game thread actually took a unicast snapshot for this
	 * session. Distinguishes "no snapshot -> broadcast" from "snapshot whose
	 * targets were all filtered out -> send nowhere" (see SendRaw and
	 * UHapbeatSubsystem's three-state contract).
	 */
	bool bHasUnicastSnapshot = false;
	float SendAheadSeconds = 0.05f;
	/**
	 * Where this session broadcasts when it has no unicast snapshot -- the
	 * subnet a device answered on, or 255.255.255.255 before any has. Passed in
	 * rather than hardcoded so a stream sent with bStreamUnicast=false (many
	 * devices firing in lockstep) still reaches a multi-homed host's real
	 * subnet. See HapbeatNetInterfaces.h.
	 */
	FString BroadcastIp;

	// Built in Init() (on the worker thread itself), from the plain values
	// above — never shared with / touched by the game thread.
	TArray<TSharedPtr<FInternetAddr>> LocalUnicastTargets;
	TSharedPtr<FInternetAddr> LocalBroadcastAddr;

	// Owns ALL per-session pacing/cursor state; single-writer (this thread only).
	// Built in Init() so its ctor-time scratch-buffer allocation, and every byte
	// of state it subsequently owns, belongs to this thread from the moment it exists.
	TUniquePtr<FHapbeatStreamer> Streamer;

	/** Set by Stop() (game thread, via FRunnableThread::Kill); polled by Run(). */
	std::atomic<bool> bStopRequested{false};
	/** Set by Run() as its last statement; polled by the game-thread watchdog. */
	std::atomic<bool> bFinished{false};

	// Session compatibility values. Immutable after construction.
	TFunction<uint16()> NextSeqFn;
	int32 SessionSampleRate = 0;
	int32 SessionChannels = 0;
	FString SessionTarget;
	TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InitialMirror;

	/**
	 * The only cross-thread structural state. AddSource and the worker's final
	 * empty check take the same lock, so an accepted source can never land after
	 * STREAM_END; it is either drained or rejected and started as a new session.
	 */
	mutable FCriticalSection SourceMutex;
	TArray<FPendingSource> PendingSources;
	TArray<FGuid> FinishedSourceIds;
	bool bAcceptingSources = true;
	/** Empty endpoint sessions stay open briefly so adjacent sources share one wire stream. */
	double EmptySinceSeconds = -1.0;
};
