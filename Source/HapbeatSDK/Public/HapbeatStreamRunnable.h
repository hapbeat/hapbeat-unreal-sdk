// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/Runnable.h"
#include "HapbeatProtocol.h"
#include "HapbeatStreamGainMirror.h"
#include <atomic>

class FHapbeatStreamer;
class FSocket;
class FInternetAddr;

/**
 * One wire StreamClip session to one exact endpoint, paced and sent OFF the
 * game thread. Compatible overlapping local sources are mixed into that session
 * before each STREAM_DATA chunk is sent. Replaces the old game-thread
 * FTSTicker-driven model (see the FHapbeatStreamer class doc /
 * dev-notes/unreal-sdk-v1-design.md §5): frame hitches (GC / render / physics
 * spikes) on the game thread used to starve the device ring buffer and cause
 * irregular, audible dropouts — a dedicated sending thread with its own precise
 * pacing avoids that entirely. The Unity SDK hit the identical bug in
 * production and fixed it the same way (commits 94ec760 / 7c0aafc); this is
 * that fix, ported.
 *
 * Execution model: UHapbeatSubsystem hands every session to its ONE persistent
 * FHapbeatStreamWorker thread (created with the UDP socket, not per event). The
 * worker calls Init() when it admits the session and then Step() once per 10 ms
 * pass, in admission order, until Step() returns false. Run() is kept as a
 * self-paced loop over the same Step() for callers that drive a runnable on its
 * own FRunnableThread (editor Test Play, runner contract tests). "Sending
 * thread" below means whichever of the two drives this object.
 *
 * Threading contract (read before touching this class):
 *   - Init()/Step()/Run() (the sending thread) own FHapbeatStreamer's pacing and
 *     wire state. DetachSource() is the one game-thread exception: it takes
 *     StreamerMutex before removing a cursor, which is also held around every
 *     sending-thread tick/send. Therefore when DetachSource() returns, no
 *     STREAM_DATA containing that source can still be sent on this endpoint.
 *   - The ONLY source state shared with the game thread is the
 *     Gain/Pan/Loop/bStopped
 *     ATOMIC mirror (FHapbeatStreamGainMirror — never the UHapbeatStreamPlayback
 *     UObject itself, which is unsafe to read from a non-game thread under GC);
 *     the subsystem's Socket raw pointer (valid for this object's entire sending
 *     life by construction — UHapbeatSubsystem joins its stream worker, after a
 *     final pass that sends every pending END, before it destroys the socket);
 *     and the subsystem's internally-locked NextSeq() callback (shared with the
 *     game thread's Play/Stop/Ping/etc. sends, exactly like Unity's single
 *     locked _sequenceNumber).
 *   - Lifetime is shared (TSharedPtr, ThreadSafe): the subsystem and the worker
 *     each hold a reference, and whichever releases last deletes the object.
 *     The game thread never blocks on the sending thread: Stop()/Abandon() only
 *     set flags, and it later observes IsFinished() to drop its reference.
 *   - Exact unicast targets are captured as PLAIN VALUES (IP strings / port)
 *     at construction time and turned into the sending thread's
 *     OWN FInternetAddr instances in Init() — never a TSharedPtr<FInternetAddr>
 *     shared with the game thread. (UE5's TSharedPtr default IS ThreadSafe, so
 *     the refcount itself would be fine; the point is that the pointed-to
 *     FInternetAddr is a mutable object with no documented thread-safety
 *     contract, so each thread simply owns its own.)
 *   - STREAM_END is owned by Step(). Ordinary stop/natural empty completion
 *     sends it once; Abandon suppresses it on every stop branch because route
 *     retirement must never send END across a migrated/expired path. Step()
 *     publishes bFinished only after that decision is complete.
 */
class HAPBEATSDK_API FHapbeatStreamRunnable : public FRunnable
{
public:
	/**
	 * @param InPcm16              COPY of the clip's interleaved LE int16 bytes (moved in).
	 * @param InSampleRate         Hz.
	 * @param InChannels           1 = mono, 2 = stereo.
	 * @param InTarget             Exact device address reported by PONG.
	 * @param InMirror             Thread-safe Gain/Pan/Loop/bStopped mirror (never null).
	 * @param InNextSeq            Thread-safe (internally locked) next-seq callback.
	 * @param InSocket             The subsystem's UDP socket. Valid for this object's entire lifetime
	 *                             (see the class doc's threading contract).
	 * @param InPort               UDP port for the exact endpoint.
	 * @param InUnicastTargetIps   Exact PONG endpoint IP (plain string), captured for this session.
	 * @param InSendAheadSeconds   FHapbeatStreamer pacing lead (UHapbeatConfig::StreamSendAheadSeconds).
	 * @param InWireFormat         Immutable per session: LegacyV1 for pre-v2 firmware, SessionV2 otherwise.
	 * @param InIdentity           SessionV2 only: the envelope stamped on every BEGIN/DATA/END of this session.
	 */
	FHapbeatStreamRunnable(
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
		EHapbeatStreamWireFormat InWireFormat = EHapbeatStreamWireFormat::LegacyV1,
		const FHapbeatStreamSessionIdentity& InIdentity = FHapbeatStreamSessionIdentity());
	virtual ~FHapbeatStreamRunnable() override;

	EHapbeatStreamWireFormat GetWireFormat() const { return WireFormat; }
	const FHapbeatStreamSessionIdentity& GetSessionIdentity() const { return SessionIdentity; }

	/** Target cadence between Step() calls (FHapbeatStreamer decides whether a chunk actually goes out). */
	static constexpr double TickIntervalSeconds = 0.010;

	/**
	 * Wait until TargetSeconds or until bStopRequested is set: spin on Windows,
	 * sleep-then-spin elsewhere. Shared by Run() and FHapbeatStreamWorker.
	 */
	static void PreciseWaitUntil(double TargetSeconds, const std::atomic<bool>& bStopRequested);

	// FRunnable
	/** Sending thread: build this thread's addresses and the streamer. Returns false (and publishes bFinished) on failure. */
	virtual bool Init() override;
	/** Self-paced loop: Step() every TickIntervalSeconds until it returns false. */
	virtual uint32 Run() override;
	virtual void Stop() override;

	/**
	 * Sending thread: one pacing iteration. The first call sends STREAM_BEGIN.
	 * Returns false once the session is over (END sent, or suppressed for
	 * Abandon) and bFinished is published; it must not be called again then.
	 */
	bool Step(double NowSeconds);
	/** Stop without STREAM_END when a PONG endpoint expires. */
	void Abandon();
	void UpdateEndpoint(const FString& InIp, int32 InPort);

	/** True once Step() has finished the session (END sent unless this route was abandoned). */
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
		TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InMirror);
	/** Detach one source cursor from this endpoint without stopping its Playback. */
	void DetachSource(const FGuid& SourceId);

	/** Game-thread poll: source ids that reached EOF in this endpoint session. */
	void DrainFinishedSourceIds(TArray<FGuid>& OutSourceIds);

protected:
	/** Packet boundary overridden only by in-memory runner tests. */
	virtual void SendRaw(const TArray<uint8>& Packet);
	/** Snapshot the exact destination currently owned by the runner. */
	bool GetSingleEndpoint(FString& OutIp, int32& OutPort) const;

private:
	struct FPendingSource
	{
		TArray<uint8> Pcm16;
		FGuid SourceId;
		TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror;

		FPendingSource(const FGuid& InSourceId, TArray<uint8>&& InPcm16,
			TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InMirror)
			: Pcm16(MoveTemp(InPcm16)), SourceId(InSourceId), Mirror(InMirror)
		{
		}
	};

	/** Sending thread: drain game-thread additions, or close admission if the session is empty. */
	bool DrainPendingSourcesOrClose(double NowSeconds);

	// --- construction-time immutable inputs (never written after the ctor) ---
	FSocket* Socket = nullptr;
	int32 Port = 0;
	TArray<FString> UnicastTargetIps;
	float SendAheadSeconds = 0.05f;

	// Built in Init() (on the sending thread itself), from the plain values
	// above — never shared with / touched by the game thread.
	TArray<TSharedPtr<FInternetAddr>> LocalUnicastTargets;

	// Owns ALL per-session pacing/cursor state; single-writer (the sending thread only).
	// Built in Init() so its ctor-time scratch-buffer allocation, and every byte
	// of state it subsequently owns, belongs to this thread from the moment it exists.
	TUniquePtr<FHapbeatStreamer> Streamer;

	/** Set by Stop()/Abandon() (game thread); polled by Step() and the self-paced Run() wait. */
	std::atomic<bool> bStopRequested{false};
	std::atomic<bool> bAbandonRequested{false};
	/** Published by Step() once the session is over; polled by the game-thread watchdog. */
	std::atomic<bool> bFinished{false};
	/** Sending thread only: Streamer->Start() (STREAM_BEGIN) has run. */
	bool bStarted = false;

	// Session compatibility values. Immutable after construction.
	TFunction<uint16()> NextSeqFn;
	int32 SessionSampleRate = 0;
	int32 SessionChannels = 0;
	FString SessionTarget;
	TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> InitialMirror;
	EHapbeatStreamWireFormat WireFormat = EHapbeatStreamWireFormat::LegacyV1;
	FHapbeatStreamSessionIdentity SessionIdentity;

	/**
	 * The only cross-thread structural state. AddSource and the worker's final
	 * empty check take the same lock, so an accepted source can never land after
	 * STREAM_END; it is either drained or rejected and started as a new session.
	 */
	mutable FCriticalSection SourceMutex;
	/**
	 * Serializes every FHapbeatStreamer mutation/send with DetachSource(). It is
	 * deliberately a packet boundary barrier, not a queued request: override
	 * routing may return only after the retired endpoint cannot emit more DATA.
	 */
	mutable FCriticalSection StreamerMutex;
	mutable FCriticalSection DestinationMutex;
	TArray<FPendingSource> PendingSources;
	/**
	 * Source ids detached before Init() creates Streamer. The tombstone survives
	 * until an explicit later AddSource() re-joins this endpoint, so startup can
	 * never admit the retired constructor source.
	 */
	TSet<FGuid> DetachedSourceIds;
	TArray<FGuid> FinishedSourceIds;
	bool bAcceptingSources = true;
	/** Empty endpoint sessions stay open briefly so adjacent sources share one wire stream. */
	double EmptySinceSeconds = -1.0;
};
