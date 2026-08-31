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
 *   - Run() owns FHapbeatStreamer's pacing and wire state. DetachSource() is
 *     the one game-thread exception: it takes StreamerMutex before removing a
 *     cursor, which is also held around every worker tick/send. Therefore when
 *     DetachSource() returns, no STREAM_DATA containing that source can still
 *     be sent on this endpoint.
 *   - The ONLY source state shared with the game thread is the
 *     Gain/Pan/Loop/bStopped
 *     ATOMIC mirror (FHapbeatStreamGainMirror — never the UHapbeatStreamPlayback
 *     UObject itself, which is unsafe to read from a non-game thread under GC);
 *     the subsystem's Socket raw pointer (valid for this thread's entire
 *     lifetime by construction — UHapbeatSubsystem::StopStream() always
 *     Kill(true)-joins this thread before the subsystem could ever destroy the
 *     socket); and the subsystem's internally-locked NextSeq() callback (shared
 *     with the game thread's Play/Stop/Ping/etc. sends, exactly like Unity's
 *     single locked _sequenceNumber).
 *   - Exact unicast targets are captured as PLAIN VALUES (IP strings / port)
 *     at construction time and turned into this thread's
 *     OWN FInternetAddr instances in Init() — never a TSharedPtr<FInternetAddr>
 *     shared with the game thread. (UE5's TSharedPtr default IS ThreadSafe, so
 *     the refcount itself would be fine; the point is that the pointed-to
 *     FInternetAddr is a mutable object with no documented thread-safety
 *     contract, so each thread simply owns its own.)
 *   - STREAM_END is owned by Run(). Ordinary stop/natural empty completion
 *     sends it once; Abandon suppresses it on every stop branch because route
 *     retirement must never send END across a migrated/expired path. Run()
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
		float InSendAheadSeconds);
	virtual ~FHapbeatStreamRunnable() override;

	// FRunnable
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	/** Stop without STREAM_END when a PONG endpoint expires. */
	void Abandon();
	void UpdateEndpoint(const FString& InIp, int32 InPort);

	/** True once Run() has returned (END sent unless this route was abandoned). */
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

	/** Worker-thread: drain game-thread additions, or close admission if the session is empty. */
	bool DrainPendingSourcesOrClose();

	// --- construction-time immutable inputs (never written after the ctor) ---
	FSocket* Socket = nullptr;
	int32 Port = 0;
	TArray<FString> UnicastTargetIps;
	float SendAheadSeconds = 0.05f;

	// Built in Init() (on the worker thread itself), from the plain values
	// above — never shared with / touched by the game thread.
	TArray<TSharedPtr<FInternetAddr>> LocalUnicastTargets;

	// Owns ALL per-session pacing/cursor state; single-writer (this thread only).
	// Built in Init() so its ctor-time scratch-buffer allocation, and every byte
	// of state it subsequently owns, belongs to this thread from the moment it exists.
	TUniquePtr<FHapbeatStreamer> Streamer;

	/** Set by Stop() (game thread, via FRunnableThread::Kill); polled by Run(). */
	std::atomic<bool> bStopRequested{false};
	std::atomic<bool> bAbandonRequested{false};
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
	/**
	 * Serializes every FHapbeatStreamer mutation/send with DetachSource(). It is
	 * deliberately a packet boundary barrier, not a queued request: override
	 * routing may return only after the retired endpoint cannot emit more DATA.
	 */
	mutable FCriticalSection StreamerMutex;
	mutable FCriticalSection DestinationMutex;
	TArray<FPendingSource> PendingSources;
	TArray<FGuid> FinishedSourceIds;
	bool bAcceptingSources = true;
	/** Empty endpoint sessions stay open briefly so adjacent sources share one wire stream. */
	double EmptySinceSeconds = -1.0;
};
