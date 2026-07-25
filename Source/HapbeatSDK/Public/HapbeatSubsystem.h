// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Common/UdpSocketReceiver.h"         // FUdpSocketReceiver + FArrayReaderPtr typedef
#include "HAL/CriticalSection.h"              // FCriticalSection (SeqLock — shared with the stream thread)
#include "Interfaces/IPv4/IPv4Endpoint.h"     // FIPv4Endpoint
#include "Subsystems/GameInstanceSubsystem.h"
#include "HapbeatSubsystem.generated.h"

class FSocket;
class FInternetAddr;
class FHapbeatStreamRunnable;
class FRunnableThread;
class UHapbeatClip;
class UHapbeatStreamPlayback;

/** Raised on the game thread when the device set goes 0 -> positive (liveness, not socket-open). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FHapbeatOnConnected);
/** Raised on the game thread when the device set goes positive -> 0. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FHapbeatOnDisconnected);
/** Raised on the game thread when a device returns an ERROR (0xFF) packet. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHapbeatOnError, const FString&, Message);
/** Raised on the game thread for every PONG. RttUs is microseconds; the string fields are empty for bridge-form PONGs. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FHapbeatOnPong, const FString&, Endpoint, int64, RttUs, const FString&, DeviceName, const FString&, Address, const FString&, Firmware);

/**
 * Hapbeat game-instance subsystem — the level-1 "fire" surface, callable from
 * C++ and Blueprint. Sends Layer 1 commands over Wi-Fi UDP broadcast and
 * receives PONG / ERROR replies on the same bound socket.
 *
 * Blueprint:  Get Hapbeat Subsystem -> Connect -> Play (event id, gain).
 * C++:        GetGameInstance()->GetSubsystem<UHapbeatSubsystem>()->Play(...);
 *
 * The fire side stays orthogonal to event tuning (default gains live in the kit
 * on the device / a future EventMap asset), matching the Hapbeat Unity SDK.
 *
 * Liveness model: UDP is connectionless, so socket-open is NOT device presence.
 * IsConnected() == "socket is open". Device presence is tracked from PONG
 * replies (AliveDeviceCount / IsAlive). OnConnected / OnDisconnected fire only
 * on a liveness 0<->positive transition (this fixes the Unity double-fire where
 * OnConnected also fired on socket-open).
 *
 * Global address override: SetAddressOverride() forces the player/group on
 * EVERY outgoing send (Play/Stop/StopAll/StreamClip/StopStreamWithFlush)
 * WITHOUT touching triggers or EventMap entries — the intended flow for one
 * identical build deployed to many HMDs, each pinned 1:1 to its own Hapbeat via
 * a per-launch override (optionally persisted). See ResolveTarget (Unity SDK:
 * HapbeatClient.ResolveTarget / HapbeatManager.SetAddressOverride).
 */
UCLASS()
class HAPBEATSDK_API UHapbeatSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Sentinel for a disabled address-override axis (player or group). Mirrors HapbeatManager.AddressOverrideDisabled (Unity SDK). */
	static constexpr int32 AddressOverrideDisabled = -1;

	UHapbeatSubsystem();
	// Out-of-line dtor (defined in the .cpp where FHapbeatStreamRunnable is
	// complete) so the owned Streamer/thread pointers can be deleted with only
	// a forward declaration in this header (the generated dtor would otherwise
	// need the full types).
	virtual ~UHapbeatSubsystem() override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Open the UDP broadcast socket (reusable, bound to an OS port so replies arrive here). AppName (<=16 chars) shows on the device OLED. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void Connect(int32 InPort = 7700, const FString& InAppName = TEXT(""));

	/** Play an event id present in the device kit. Gain is 0..1. Target "" = broadcast. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void Play(const FString& EventId, float Gain = 1.0f, const FString& Target = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void Stop(const FString& EventId, const FString& Target = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void StopAll(const FString& Target = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void Ping();

	// ---- Real-time clip streaming (Phase 3) ----

	/**
	 * Start streaming a PCM16 clip to the device as live haptics and return a
	 * handle for real-time gain / pan modulation. The SDK pre-multiplies every
	 * sample by the handle's Gain x Pan before sending, so STREAM_BEGIN carries
	 * gain = 1.0 (the device must not re-apply gain).
	 *
	 * Single active session, REPLACE semantics: a new call first stops any active
	 * stream (STREAM_END) then starts a fresh one (STREAM_BEGIN). Paced by a
	 * DEDICATED background thread (FHapbeatStreamRunnable, since the 2026-07-25
	 * thread migration) rather than the game thread, so frame hitches (GC /
	 * render / physics) cannot starve the device's ring buffer.
	 *
	 * @param Clip         The PCM16 clip (mono or stereo). Null / empty => warn + nullptr.
	 * @param BaselineGain Frozen author gain (entry.gain x manifest.intensity). 0..2.
	 * @param InitialGain  Initial modulator; Gain starts at Baseline x InitialGain.
	 * @param Target       Address filter ("" = broadcast).
	 * @param bLoop        Loop the clip until StopStream() / handle Stop().
	 * @return Per-stream handle, or nullptr if the clip was invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (AdvancedDisplay = "3"))
	UHapbeatStreamPlayback* StreamClip(UHapbeatClip* Clip, float BaselineGain = 1.0f, float InitialGain = 1.0f,
		const FString& Target = TEXT(""), bool bLoop = false);

	/**
	 * Stop the active stream: signal the stream thread to send STREAM_END, JOIN
	 * it (blocks briefly — the thread notices within one ~10ms pacing tick),
	 * then mark the handle stopped and unregister the watchdog ticker. No-op if
	 * nothing is streaming.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void StopStream();

	/**
	 * Stop the active stream AND force the device ring buffer to flush for
	 * immediate silence (a residual tail otherwise drains over ~50-250 ms).
	 * Runs StopStream(), then sends a STREAM_BEGIN(gain=1.0) + STREAM_END pair to
	 * trigger the firmware's flush path. With no target this broadcasts and
	 * flushes every device (it can also cut other sessions) — pass a target for
	 * per-target stop. Parity with Unity StopStreamWithFlush.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void StopStreamWithFlush(const FString& Target = TEXT(""));

	// ---- Global address override (single-app, multi-HMD 1:1 deployments) ----

	/**
	 * Force the player / group applied to EVERY outgoing command (Play/Stop/
	 * StopAll/StreamClip/StopStreamWithFlush) at runtime. Pass
	 * AddressOverrideDisabled (-1) to leave an axis alone — a disabled axis
	 * means "don't override that axis", not "rewrite the target's value to
	 * -1"; it leaves each EventMap/trigger-authored target string exactly as
	 * authored (see UHapbeatTargetLibrary::ResolveTarget). Values outside
	 * 1..99 are normalized to AddressOverrideDisabled (NormalizeAddressOverride).
	 *
	 * When bPersist is true, the values are saved to the platform's
	 * GameUserSettings ini (section "HapbeatSDK") and restored on next launch
	 * (Initialize(), before auto-connect) — the intended flow for "one
	 * identical build deployed to many HMDs, each bound to its own Hapbeat".
	 * Mirrors HapbeatManager.SetAddressOverride (Unity SDK).
	 */
	// NOTE: the group parameter is named InGroup per this class's existing
	// `Connect(int32 InPort, const FString& InAppName)` naming convention.
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Target")
	void SetAddressOverride(int32 Player, int32 InGroup, bool bPersist = false);

	/**
	 * Clear a persisted address override (removes the GameUserSettings ini
	 * keys, if present) and revert the runtime override to disabled on both
	 * axes. There is no config-level default to fall back to — clearing
	 * simply means "stop overriding". Reuses SetAddressOverride (bPersist:
	 * false, so the just-cleared keys aren't immediately re-saved). Mirrors
	 * HapbeatManager.ClearPersistedAddressOverride (Unity SDK).
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Target")
	void ClearPersistedAddressOverride();

	/** Currently effective forced player number, or AddressOverrideDisabled (-1) if this axis doesn't override the target's player. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat|Target")
	int32 GetOverridePlayer() const { return OverridePlayer; }

	/** Currently effective forced group number, or AddressOverrideDisabled (-1) if this axis doesn't override the target's group. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat|Target")
	int32 GetOverrideGroup() const { return OverrideGroup; }

	/**
	 * Clamp an override value to the valid device-addressing range (1..99).
	 * Anything outside that range (including the disabled sentinel -1) is
	 * normalized to AddressOverrideDisabled. Mirrors
	 * HapbeatClient.NormalizeOverride (Unity SDK); exposed as a public static
	 * (not BlueprintCallable — matches the Unity method it mirrors) so any
	 * future tooling reading the persisted ini values can reuse the same clamp.
	 */
	static int32 NormalizeAddressOverride(int32 Value) { return (Value >= 1 && Value <= 99) ? Value : AddressOverrideDisabled; }

	/**
	 * Socket is open and ready to send. UDP is connectionless: this stays true
	 * even with no device powered on. For device presence use IsAlive() /
	 * GetAliveDeviceCount().
	 */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	bool IsConnected() const { return Socket != nullptr; }

	/** Number of distinct devices that returned a PONG within max(5s, PingInterval*3). */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	int32 GetAliveDeviceCount() const;

	/** True if at least one device is responsive. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	bool IsAlive() const { return GetAliveDeviceCount() > 0; }

	/** True while a clip stream session is active. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	bool IsStreaming() const { return StreamRunnable != nullptr; }

	/** Handle to the active stream playback, or nullptr if nothing is streaming. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	UHapbeatStreamPlayback* GetActivePlayback() const { return ActivePlayback; }

	/** Fires when a device first becomes reachable (alive count 0 -> positive). */
	UPROPERTY(BlueprintAssignable, Category = "Hapbeat")
	FHapbeatOnConnected OnConnected;

	/** Fires when the last reachable device ages out (alive count positive -> 0). */
	UPROPERTY(BlueprintAssignable, Category = "Hapbeat")
	FHapbeatOnDisconnected OnDisconnected;

	/** Fires when a device returns an ERROR packet. */
	UPROPERTY(BlueprintAssignable, Category = "Hapbeat")
	FHapbeatOnError OnError;

	/** Fires for every PONG (one per responsive device per ping on broadcast). */
	UPROPERTY(BlueprintAssignable, Category = "Hapbeat")
	FHapbeatOnPong OnPong;

private:
	void SendPacket(const TArray<uint8>& Packet);

	/**
	 * Send a STREAM_* packet on the GAME THREAD: unicast to each device
	 * snapshotted at session start when stream-unicast is on and at least one
	 * device is known, else fall back to the normal broadcast SendPacket.
	 * Wi-Fi AP power-save (DTIM) batching can hold BROADCAST frames for a whole
	 * beacon interval, which shows up as periodic ~100-200 ms stutter in
	 * streamed haptics; unicast dodges that.
	 *
	 * Since the 2026-07-25 thread migration, the ACTIVE stream's own
	 * STREAM_BEGIN/DATA/END no longer go through here — FHapbeatStreamRunnable
	 * sends those itself from its dedicated thread (via its own locally-owned
	 * FInternetAddr targets, never these). This game-thread path now serves
	 * only StopStreamWithFlush()'s flush BEGIN+END pair, sent AFTER the stream
	 * thread has already been joined (see StopStream()). Unity SDK parity:
	 * SendStreamRaw, commit db6fd31. A per-target send failure is logged and
	 * skipped; it never kills the session.
	 */
	void SendStreamPacket(const TArray<uint8>& Packet);

	/**
	 * AppName as it goes on the wire: the stored (raw, templated) name with the
	 * "<p>" / "<g>" address-override placeholders substituted for the CURRENT
	 * override, so a templated name tracks SetAddressOverride live on the device
	 * OLED. BuildConnectStatus applies the 16-char cap afterwards. Parity with
	 * HapbeatManager.AppName (Unity SDK).
	 */
	FString AppNameForWire() const;

	/**
	 * Snapshot the currently-alive device IPs into StreamUnicastTargets (called
	 * once per stream session start, mirroring Unity's SetStreamUnicastTargets
	 * seeding). Clears the list when unicast is disabled or nobody has PONGed —
	 * SendStreamPacket then broadcasts. A device whose first PONG lands mid-session
	 * is picked up by the NEXT session, exactly like Unity.
	 */
	void RefreshStreamUnicastTargets();

	/**
	 * Thread-safe: guarded by SeqLock so both the game thread (Play/Stop/
	 * StopAll/Ping/CONNECT_STATUS/StopStreamWithFlush) and the dedicated stream
	 * thread's STREAM_BEGIN/DATA/END draw from the SAME monotonic counter,
	 * matching Unity's single locked _sequenceNumber (HapbeatClient.cs
	 * _seqLock) shared across its main + background mixer threads.
	 */
	uint16 NextSeq();

	/** Send PING + CONNECT_STATUS, then diff the alive set and raise events. Bound to the FTSTicker. */
	bool TickKeepAlive(float DeltaSeconds);

	/**
	 * Game-thread watchdog: polls whether the active FHapbeatStreamRunnable has
	 * finished on its own (natural EOF on a non-loop clip, or the handle's own
	 * Stop() was called) and, if so, calls StopStream() to join + clean up
	 * (harmlessly idempotent if the thread is already gone). Bound to a
	 * dedicated FTSTicker registered only while streaming. Returns false
	 * (auto-unregisters the ticker) once nothing is streaming; true to keep polling.
	 */
	bool TickStream(float DeltaSeconds);

	/** FUdpSocketReceiver callback — runs on the receiver worker thread. */
	void HandleReceivedData(const FArrayReaderPtr& Reader, const FIPv4Endpoint& Sender);

	/** Apply a parsed PONG/ERROR result on the game thread (marshalled from the worker thread). */
	void OnPongGameThread(const FString& Endpoint, int64 RttUs, const FString& DeviceName, const FString& Address, const FString& Firmware);
	void OnErrorGameThread(const FString& Message);

	/** Re-evaluate the alive set and fire OnConnected/OnDisconnected on a 0<->positive change. Game thread only. */
	void EvaluateLivenessTransition();

	/** Local monotonic microseconds (for PING send time / RTT). */
	int64 NowMicros() const;
	/** Unix-epoch microseconds (the PING timestamp field on the wire). */
	int64 UnixMicros() const;

	/** Seconds within which a PONG counts a device as alive. */
	double AliveTimeoutSeconds() const { return FMath::Max(5.0, static_cast<double>(PingInterval) * 3.0); }

	FSocket* Socket = nullptr;
	FUdpSocketReceiver* Receiver = nullptr;
	TSharedPtr<FInternetAddr> BroadcastAddr;
	int32 Port = 7700;
	/**
	 * Group byte for CONNECT_STATUS (device OLED display only, never routing):
	 * the active override group when set, or 0 otherwise. Verbatim port of
	 * HapbeatManager.ConnectStatusGroupByte (Unity SDK) — the config-level
	 * UHapbeatConfig::Group is deliberately NOT sent here (parity with Unity,
	 * where the OLED group display tracks the address override exclusively).
	 */
	uint8 ConnectStatusGroupByte() const { return OverrideGroup >= 1 ? static_cast<uint8>(OverrideGroup) : 0; }
	FString AppName;
	uint16 Seq = 0;
	/** Guards Seq — shared by the game thread and the stream thread since 2026-07-25 (see NextSeq()). */
	FCriticalSection SeqLock;
	float PingInterval = 5.0f; // seeded from UHapbeatConfig::PingInterval in Initialize

	FTSTicker::FDelegateHandle KeepAliveHandle;

	// --- liveness / RTT state (game-thread mutated, but the receiver thread also
	//     touches DevicePongTimes via a marshalled game-thread task, never directly) ---

	/** Last PONG time (FPlatformTime::Seconds) keyed by sender IP string. */
	TMap<FString, double> DevicePongTimes;
	/** seq -> local send micros for outstanding PINGs (RTT = now - sent). */
	TMap<uint16, int64> PendingPings;
	/** Previous alive count, for edge detection. -1 = "never evaluated". */
	int32 PrevAliveCount = -1;
	/** Set once Deinitialize starts so SendPacket cannot lazily re-open the socket during teardown. */
	bool bShuttingDown = false;

	// --- global address override (see SetAddressOverride / ResolveTarget) ---

	/** Effective (already-normalized) forced player. AddressOverrideDisabled (-1) = disabled.
	 * Populated from GConfig (if persisted) in Initialize(), before auto-connect. */
	int32 OverridePlayer = AddressOverrideDisabled;
	/** Effective (already-normalized) forced group. AddressOverrideDisabled (-1) = disabled. */
	int32 OverrideGroup = AddressOverrideDisabled;

	// --- streaming; game-thread-owned handles, but the actual pacing/sending
	//     runs on a dedicated stream thread since the 2026-07-25 migration
	//     (see FHapbeatStreamRunnable's class doc for the full threading contract) ---

	/**
	 * The active stream handle. A UPROPERTY so it is a GC root while streaming
	 * (the caller may not retain it). Cleared when the stream ends. Its
	 * Gain/Pan/bStopped are mirrored (GetMirror()) for the stream thread to
	 * read — this UObject itself is never touched off the game thread.
	 */
	UPROPERTY()
	TObjectPtr<UHapbeatStreamPlayback> ActivePlayback = nullptr;

	/**
	 * The active stream's dedicated FRunnable + thread. Null when not
	 * streaming. Owned raw pointers (deleted in StopStream/dtor), same pattern
	 * as Receiver above. Deliberately not TUniquePtr: UHT's gen.cpp includes
	 * this header with FHapbeatStreamRunnable still forward-declared and would
	 * instantiate the smart pointer's deleter there -> C4150 "deletion of
	 * incomplete type" as-error (identical reasoning to the earlier Streamer
	 * pointer this replaces).
	 */
	FHapbeatStreamRunnable* StreamRunnable = nullptr;
	FRunnableThread* StreamThread = nullptr;

	/** Game-thread watchdog ticker driving TickStream; valid only while a stream is active. */
	FTSTicker::FDelegateHandle StreamTickHandle;

	/** Send-ahead lead for streaming; seeded from UHapbeatConfig in Initialize. */
	float StreamSendAheadSeconds = 0.05f;

	/** Config: unicast STREAM_* to known devices instead of broadcasting (UHapbeatConfig::bStreamUnicast). */
	bool bStreamUnicast = true;

	/** Per-session snapshot of alive device addresses for stream unicast. Empty => broadcast. */
	TArray<TSharedPtr<FInternetAddr>> StreamUnicastTargets;
};
