// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Common/UdpSocketReceiver.h"         // FUdpSocketReceiver + FArrayReaderPtr typedef
#include "Engine/TimerHandle.h"               // FTimerHandle (held by the pending-send record below)
#include "HAL/CriticalSection.h"              // FCriticalSection (SeqLock — shared with the stream thread)
#include "HapbeatNetInterfaces.h"             // FHapbeatBroadcastRoute (held by value in a TArray below)
#include "Interfaces/IPv4/IPv4Endpoint.h"     // FIPv4Endpoint
#include "Subsystems/GameInstanceSubsystem.h"
#include "HapbeatSubsystem.generated.h"

class FSocket;
class FInternetAddr;
class FHapbeatStreamRunnable;
class FRunnableThread;
class UHapbeatClip;
class UHapbeatEventMap;
class UHapbeatStreamPlayback;
struct FHapbeatEventEntry;

/** What a deferred (haptic-delay) send does once its timer fires. See FHapbeatPendingSend. */
enum class EHapbeatPendingKind : uint8
{
	/** Command entry: Play(EventId, Gain, Target). */
	PlayCommand,
	/** Command entry: Stop(EventId, Target). */
	StopCommand,
	/** Stream Clip entry: start the session on the handle already handed to the caller. */
	StartStream,
	/** Stream Clip entry: StopStream() (the whole session — v1 streams one at a time). */
	StopStream,
};

/**
 * One send held back by the haptic delay (UHapbeatConfig::HapticDelaySeconds +
 * FHapbeatEventEntry::DelayOffsetSeconds). Created by PlayEntry / StopEntry and
 * consumed by UHapbeatSubsystem::FirePendingSend.
 *
 * A USTRUCT purely so the Clip / Playback pointers are GC-visible: a Stream Clip
 * entry hands its handle back to the caller IMMEDIATELY (the PlayEntry return
 * contract cannot wait out the delay), so both that handle and the clip it will
 * stream must stay alive across the delay even though nothing else references
 * them yet.
 */
USTRUCT()
struct HAPBEATSDK_API FHapbeatPendingSend
{
	GENERATED_BODY()

	/** The clip to stream (StartStream only). Rooted here for the length of the delay. */
	UPROPERTY()
	TObjectPtr<UHapbeatClip> Clip = nullptr;

	/** The handle already returned to the caller (StartStream only). Rooted here until the session starts. */
	UPROPERTY()
	TObjectPtr<UHapbeatStreamPlayback> Playback = nullptr;

	EHapbeatPendingKind Kind = EHapbeatPendingKind::PlayCommand;

	/** Command payload. Target stays UNRESOLVED here: the address override is applied at fire time (Play/Stop), matching Unity. */
	FString EventId;
	FString Target;
	float Gain = 1.0f;
	bool bLoop = false;

	/** The timer this record is waiting on, so Deinitialize can cancel it. */
	FTimerHandle Handle;
};

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
 * Firing an authored haptic goes through the "Play Hapbeat Event" node
 * (UHapbeatBlueprintLibrary), which lands on PlayEntry below; C++ may call
 * either. Play(event id, gain) stays available for the rare call site that
 * deliberately bypasses the Event Map.
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

	/**
	 * Play an entry of an Event Map. Blueprint does not see this directly: a
	 * graph calls "Play Hapbeat Event"
	 * (UHapbeatBlueprintLibrary::PlayHapbeatEvent), which validates its Map /
	 * Entry pins and lands here; the AnimNotify lands here too.
	 *
	 * THE single runtime decision point for Command vs Stream Clip. The trigger
	 * components (UHapbeatTriggerComponent::DispatchEntry) compose their
	 * per-component multipliers and pre-seed bindings AROUND this call rather
	 * than dispatching themselves, so anything added here — the haptic delay
	 * below, for one — applies to them too. The editor's Test Play is the one
	 * remaining separate sender (FHapbeatEditorSender), by design: it runs
	 * without a game instance.
	 *
	 * Everything the entry defines (Command vs Stream Clip, the clip, gain,
	 * target, loop) comes from the asset, so the caller only says WHICH entry
	 * and WHEN. That is the whole point of the Event Map: the values stay
	 * editable by whoever is tuning the feel, without touching code.
	 *
	 * Prefer this over Play(EventId): a raw event id bypasses the Event Map, and
	 * with it the authored gain and target. UHapbeatTriggerComponent builds on
	 * the same idea and adds cooldown, per-instance gain and stream handles --
	 * use the component when an actor fires the same entry repeatedly, and this
	 * when the call site is one-off.
	 *
	 * Haptic delay: the send is held back by max(0, UHapbeatConfig::
	 * HapticDelaySeconds + entry DelayOffsetSeconds) so the haptic lands with a
	 * slow audio path (Bluetooth headphones) instead of ahead of it. A Stream
	 * Clip entry still returns its handle IMMEDIATELY — only the session start is
	 * deferred (see ComputeEffectiveDelaySeconds / FirePendingSend). At the
	 * default 0 s nothing is scheduled and this behaves exactly as before.
	 *
	 * Gain semantics for a Stream Clip entry: the authored gain
	 * (entry.Gain x manifest intensity) is the BASELINE, frozen at stream start,
	 * and GainMultiplier is the INITIAL MODULATOR — not baked into the baseline.
	 * The handle starts at baseline x multiplier, and a ParameterBinding (or
	 * SetGainMultiplier) replaces the modulator afterwards via
	 * ApplyGainModulation. Same split as Unity's HapbeatTriggerBase.FireHaptic.
	 * For a Command entry there is no modulator to keep separate, so the wire
	 * gain is simply baseline x multiplier.
	 *
	 * @param GainMultiplier Scales the entry's authored gain for this call only.
	 * @param bForceNonLoop  C++ only (deliberately not exposed on the "Play
	 *                       Hapbeat Event" node): stream a Stream Clip entry as a
	 *                       one-shot regardless of its authored loop flag. Used by
	 *                       the sequence component's start / stop shots, which must
	 *                       not leave a loop running (Unity DispatchOneShot).
	 * @return The stream handle for a Stream Clip entry (for live gain / pan
	 *         modulation, or to stop just this playback); null for Command.
	 */
	UHapbeatStreamPlayback* PlayEntry(UHapbeatEventMap* Map, FGuid EntryId, float GainMultiplier = 1.0f,
		bool bForceNonLoop = false);

	/**
	 * Stop an entry started by PlayEntry: STOP for Command, ends the stream for
	 * Stream Clip. Deferred by the SAME haptic delay as PlayEntry, so the
	 * perceived Play->Stop interval is the one the caller asked for (parity with
	 * Unity HapbeatTriggerBase.StopHaptic).
	 */
	void StopEntry(UHapbeatEventMap* Map, FGuid EntryId);

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

	// ---- Haptic delay (PlayEntry / StopEntry only) ----

	/**
	 * Effective deferral for this entry: max(0, global HapticDelaySeconds +
	 * entry DelayOffsetSeconds). Port of Unity
	 * HapbeatTriggerBase.ComputeEffectiveDelaySeconds — a negative per-entry
	 * offset pulls the haptic earlier but can never go below "now".
	 *
	 * The wait itself is a GameInstance timer, so it runs on WORLD time: time
	 * dilation stretches it and a paused game holds it. Unity waits in unscaled
	 * real time (WaitForSecondsRealtime) instead. At the sub-100 ms values this
	 * setting is for, the difference only shows in slow-motion / paused play.
	 *
	 * The global value is read LIVE from GetDefault<UHapbeatConfig>() rather than
	 * seeded into a member in Initialize() like Port / PingInterval: Unity reads
	 * it live too, so editing the delay in Project Settings retunes the feel
	 * without restarting PIE. Already-pending sends are NOT re-timed by such an
	 * edit — Unity's flush machinery for that (HapbeatManager.OnHapticDelayChanged
	 * / FlushPendingDelayCoroutines) is deliberately not ported for v1; the next
	 * fire picks the new value up.
	 */
	float ComputeEffectiveDelaySeconds(const FHapbeatEventEntry& Entry) const;

	/**
	 * Register Pending on the GameInstance timer manager and keep it (with its
	 * timer handle) in PendingSends until it fires or is cancelled. Returns false
	 * if no timer manager was available, in which case nothing was scheduled.
	 */
	bool SchedulePendingSend(FHapbeatPendingSend&& Pending, float DelaySeconds);

	/** Timer callback: pull the record out of PendingSends and perform the send it was holding. */
	void FirePendingSend(uint32 PendingId);

	/** Clear every outstanding delay timer (teardown). Nothing pending is flushed early — a haptic nobody is around to feel is just noise on the wire. */
	void CancelPendingSends();

	/**
	 * Everything StreamClip does once the handle exists: replace any running
	 * session, ensure the socket, resolve the target, snapshot the unicast
	 * destinations and spin up the stream thread. Split out of StreamClip so the
	 * delayed path can create the handle NOW and start the session LATER on the
	 * very same handle. Returns false if the session could not be started
	 * (ActivePlayback is left cleared in that case).
	 */
	bool StartStreamSession(UHapbeatClip* Clip, UHapbeatStreamPlayback* Playback, const FString& Target, bool bLoop);

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
	/**
	 * Send a PLAY / STOP / STOP_ALL packet. Routing (verbatim parity with Unity
	 * HapbeatClient.SendCommandRaw, commit 97c2988):
	 *   (a) command-unicast disabled            -> broadcast
	 *   (b) each device whose last PONG is within the liveness window AND whose
	 *       reported address matches ResolvedTarget -> unicast
	 *   (c) a known device with NO reported address -> unicast anyway (fail-open:
	 *       older firmware, or its first PONG carried no address extension)
	 *   (d) nothing was sent (no live device, or every address mismatched)
	 *       -> BROADCAST fallback
	 * Never both (no double delivery: the same PLAY would fire twice).
	 *
	 * (d) is deliberately a fallback and NOT "skip": the firmware re-applies
	 * addressMatch() to every PLAY/STOP/STOP_ALL it receives, so a broadcast can
	 * never actuate a device the target didn't address — skipping would only save
	 * airtime, at the price of silently losing a command whenever our cached
	 * address is stale (the device's group/player was just changed and its next
	 * PONG hasn't landed). For STOP/STOP_ALL that silent loss means a looping
	 * event never stops. This is the one place where the command path
	 * intentionally differs from the stream path (see SendStreamPacket).
	 */
	void SendCommandPacket(const TArray<uint8>& Packet, const FString& ResolvedTarget);

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
	void RefreshStreamUnicastTargets(const FString& ResolvedTarget);

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

	/**
	 * Ticker period while NO device has answered yet. The keep-alive ticker runs
	 * at this rate and only actually sends every KeepAliveIntervalSeconds(), so a
	 * cold start discovers a device in well under a second instead of waiting a
	 * full PingInterval (devices reply to PING, never to CONNECT_STATUS).
	 * Also makes liveness/aging re-evaluate at this cadence.
	 */
	static constexpr float DiscoveryTickSeconds = 0.25f;

	/** How often to actually send PING + CONNECT_STATUS: fast until a device answers, then PingInterval. */
	float KeepAliveIntervalSeconds() const
	{
		return GetAliveDeviceCount() > 0 ? PingInterval : DiscoveryTickSeconds;
	}

	/** FPlatformTime::Seconds() of the last keep-alive send (gates the tick above). */
	double LastKeepAliveSendTime = 0.0;

	/** Seconds within which a PONG counts a device as alive. */
	double AliveTimeoutSeconds() const { return FMath::Max(5.0, static_cast<double>(PingInterval) * 3.0); }

	/**
	 * Send on every candidate broadcast destination.
	 *
	 * Reserved for PING and CONNECT_STATUS: both are idempotent, so a device
	 * reachable on two of them just receives the message twice with no visible
	 * effect -- whereas duplicating PLAY would fire the haptic twice on firmware
	 * that predates sequence de-duplication. This fan-out is what reaches a
	 * device the limited broadcast never gets to on a multi-homed host, and the
	 * PONG it provokes is what pins playback to the right subnet.
	 */
	void SendDiscoveryPacket(const TArray<uint8>& Packet);

	/** Pin broadcasts to the subnet a device actually replied from. First reply wins. */
	void LockRouteFor(const FString& DeviceIp);

	/** The single address a broadcast currently goes to. */
	const TSharedPtr<FInternetAddr>& CurrentBroadcastAddr() const;

	FSocket* Socket = nullptr;
	FUdpSocketReceiver* Receiver = nullptr;
	TSharedPtr<FInternetAddr> BroadcastAddr;
	/**
	 * Candidate broadcast destinations, rebuilt on every Connect(): a host's
	 * interfaces change when a laptop is docked, a VPN comes up or Wi-Fi moves
	 * to another network. See HapbeatNetInterfaces.h for why this is not just
	 * 255.255.255.255.
	 */
	TArray<FHapbeatBroadcastRoute> BroadcastRoutes;
	/** Index into BroadcastRoutes once a device has answered; INDEX_NONE until then. */
	int32 LockedRouteIndex = INDEX_NONE;
	/**
	 * Whether the current send outage has already been reported. A link that
	 * fails keeps failing, and a clip stream sends roughly 100 packets a second,
	 * so an unguarded warning would bury the log an operator needs. Cleared by
	 * the next successful send, so a later, unrelated outage is still reported.
	 */
	bool bLoggedSendError = false;
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

	// --- haptic delay: sends waiting on a timer (see SchedulePendingSend) ---

	/**
	 * Outstanding deferred sends, keyed by an id that only ever grows. A map
	 * (rather than an array) so a record can be removed by the very callback it
	 * fires — fired and cancelled entries both leave, nothing accumulates over a
	 * session. Empty whenever the delay is 0, which is the default.
	 */
	UPROPERTY()
	TMap<uint32, FHapbeatPendingSend> PendingSends;

	/** Source of the PendingSends keys. Never reused, so a stale timer callback cannot hit a newer record. */
	uint32 NextPendingSendId = 0;

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

	/** Config: unicast PLAY/STOP/STOP_ALL to known devices instead of broadcasting (UHapbeatConfig::bCommandUnicast). */
	bool bCommandUnicast = true;

	/**
	 * Per-session snapshot of the stream's unicast destinations.
	 *
	 * Three states, mirroring Unity's _streamUnicastTargets (029efc1) — note this
	 * differs from the command path on purpose:
	 *   bStreamTargetsSnapshotted == false -> no snapshot (feature off / nobody
	 *       has PONGed): SendStreamPacket BROADCASTS.
	 *   snapshotted && Num() == 0          -> a snapshot WAS taken and every known
	 *       device's address mismatched this session's target: send NOWHERE. Not a
	 *       broadcast — that would defeat the filter, and unlike a one-shot STOP a
	 *       lost stream cannot wedge the device in a looping state.
	 *   snapshotted && Num()  > 0          -> unicast to exactly these endpoints.
	 */
	TArray<TSharedPtr<FInternetAddr>> StreamUnicastTargets;
	bool bStreamTargetsSnapshotted = false;

	/**
	 * Last address each device reported in its PONG extension (device-addressing
	 * §5.4), keyed by sender IP. A device with no entry is "unknown" and is kept
	 * (fail-open) by both unicast filters — firmware predating the extension, or
	 * a PONG that hasn't landed yet, must not silently lose its haptics.
	 * Game-thread only (written from the marshalled PONG handler). Cleared on
	 * Connect(): device knowledge is per-connection, since after a Wi-Fi change
	 * the same IPs may belong to different devices.
	 */
	TMap<FString, FString> DeviceAddresses;
};
