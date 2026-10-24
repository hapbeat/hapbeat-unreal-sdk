// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Common/UdpSocketReceiver.h"         // FUdpSocketReceiver + FArrayReaderPtr typedef
#include "Engine/TimerHandle.h"               // FTimerHandle (held by the pending-send record below)
#include "HAL/CriticalSection.h"              // FCriticalSection (SeqLock — shared with the stream thread)
#include "HapbeatNetInterfaces.h"             // FHapbeatBroadcastRoute (held by value in a TArray below)
#include "HapbeatStreamEndpointInfo.h"        // FHapbeatStreamEndpointInfo (GetStreamEndpoints / OnStreamEndpointChanged)
#include "HapbeatStreamLease.h"               // per-endpoint stream class / lease state (Stream Gen 2)
#include "HapbeatStreamPlayback.h"            // EHapbeatStreamDeferredReason (ComputeDeferredReason)
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
class FHapbeatStreamSubsystemRoutingTest;
struct FHapbeatEventEntry;
class FHapbeatStreamLegacyFallbackSubsystemTest;
class FHapbeatStreamReacquireOwnershipTest;
class FHapbeatStreamIdentityUpdateDiscoveryTest;
class FHapbeatGen2LeasePingTest;
class FHapbeatGen2CapsTest;
class FHapbeatGen2ErrorTest;
class FHapbeatGen2CursorCarryTest;
class FHapbeatGen2StreamStoppedTest;
class FHapbeatGen2DeferredReasonTest;
class FHapbeatGen2SupersededReissueTest;
class FHapbeatGen2IgnoredTailWarningTest;
struct FHapbeatStreamGainMirror;

/** What a deferred (haptic-delay) send does once its timer fires. See FHapbeatPendingSend. */
enum class EHapbeatPendingKind : uint8
{
	/** Command entry: Play(EventId, Gain, Target, Pan). */
	PlayCommand,
	/** Command entry: Stop(EventId, Target). */
	StopCommand,
	/** Stream Clip entry: start the session on the handle already handed to the caller. */
	StartStream,
	/** Stream Clip entry: StopStream() (every source on every endpoint session). */
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
	/**
	 * PlayCommand only: the pan asked for at call time, carried to the wire when
	 * the timer fires. StartStream needs no equivalent -- its pan already sits on
	 * the handle in Playback (the caller holds that handle during the delay).
	 */
	float Pan = 0.0f;
	bool bLoop = false;

	/** The timer this record is waiting on, so Deinitialize can cancel it. */
	FTimerHandle Handle;
};

/** Raised on the game thread when the device set goes 0 -> positive (liveness, not socket-open). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FHapbeatOnConnected);
/** Raised on the game thread when the device set goes positive -> 0. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FHapbeatOnDisconnected);
/** Raised on the game thread for an ERROR (0xFF) packet the SDK accepted (message-format.md 0xFF "receiving rules"). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHapbeatOnError, const FString&, Message);
/** Raised on the game thread when a device endpoint's stream class or lease state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHapbeatOnStreamEndpointChanged, const FHapbeatStreamEndpointInfo&, Info);
/** Raised on the game thread for every PONG. RttUs is microseconds; the string fields are empty for bridge-form PONGs. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FHapbeatOnPong, const FString&, Endpoint, int64, RttUs, const FString&, DeviceName, const FString&, Address, const FString&, Firmware);

/**
 * Hapbeat game-instance subsystem — the level-1 "fire" surface, callable from
 * C++ and Blueprint. Sends Layer 1 commands over Wi-Fi UDP broadcast and
 * receives PONG / ERROR replies on the same bound socket.
 *
 * Firing an authored haptic goes through the "Play Event (Hapbeat)" node
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
 * EVERY outgoing send (Play/Stop/StopAll/StreamClip)
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
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Connect (Hapbeat)"))
	void Connect(int32 InPort = 7700, const FString& InAppName = TEXT(""));

	/**
	 * Play an event id present in the device kit. Gain is 0..1. Target "" = broadcast.
	 *
	 * Pan is -1 (left) .. 0 (center) .. +1 (right), applied by the device as a
	 * linear balance on the voice it starts (contracts DEC-055) -- so a FIRE
	 * lands off-center without any stream involved. Devices running firmware
	 * older than DEC-055 ignore it and play centered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Play Event (Hapbeat)"))
	void Play(const FString& EventId, float Gain = 1.0f, const FString& Target = TEXT(""), float Pan = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Stop Event (Hapbeat)"))
	void Stop(const FString& EventId, const FString& Target = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Stop All Events (Hapbeat)"))
	void StopAll(const FString& Target = TEXT(""));

	/**
	 * Play an entry of an Event Map. Blueprint does not see this directly: a
	 * graph calls "Play Event (Hapbeat)"
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
	 * @param Pan            Left/right balance for THIS call, ADDED to the entry's
	 *                       authored Pan and the sum clamped to [-1, 1] (pan is
	 *                       additive where gain is multiplicative, so the
	 *                       0 + 0 = 0 default leaves behaviour unchanged). A
	 *                       Command entry carries the result on the wire (the
	 *                       device expands it per voice, DEC-055); a Stream Clip
	 *                       entry gets it written onto the returned handle BEFORE
	 *                       the session starts, where a later SetPan / binding may
	 *                       still override it.
	 * @param ExtraDelaySeconds Additional deferral for THIS call only, summed with
	 *                       the global HapticDelaySeconds and the entry's
	 *                       DelayOffsetSeconds (the total is clamped at 0). For
	 *                       gameplay timing prefer a Delay node; this exists for
	 *                       per-call latency compensation.
	 * @return The stream handle for a Stream Clip entry (for live gain / pan
	 *         modulation, or to stop just this playback); null for Command.
	 */
	UHapbeatStreamPlayback* PlayEntry(UHapbeatEventMap* Map, FGuid EntryId, float GainMultiplier = 1.0f,
		bool bForceNonLoop = false, float Pan = 0.0f, float ExtraDelaySeconds = 0.0f);

	/**
	 * Stop an entry started by PlayEntry: STOP for Command, ends the stream for
	 * Stream Clip. Deferred by the SAME haptic delay as PlayEntry, so the
	 * perceived Play->Stop interval is the one the caller asked for (parity with
	 * Unity HapbeatTriggerBase.StopHaptic).
	 *
	 * Deliberately no ExtraDelaySeconds counterpart: the per-call extra delay is a
	 * fire-side effect (Unity has no stop-side equivalent either), and applying it
	 * here would need the Stop call site to remember what the Play call site asked
	 * for to keep the interval intact.
	 */
	void StopEntry(UHapbeatEventMap* Map, FGuid EntryId);

	/**
	 * Broadcast one discovery PING (the 8-byte form, which never allocates a
	 * stream lease). Lease requests (extended PINGs) go only by unicast to the
	 * devices this SDK streams to or is about to (stream-session.md "[1.x] How
	 * SDKs send extended PINGs"); the subsystem sends those itself.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Ping (Hapbeat)"))
	void Ping();

	/**
	 * Explicitly request fresh stream-lease ownership (stream-session.md
	 * "Incarnation and takeover"). Required after another writer superseded this
	 * SDK on a Stream Gen 2 device, unless that device re-issues a lease on its
	 * own once the other writer has left: periodic discovery never takes the
	 * stream back. Issues a new client incarnation, forgets every Stream Gen 2 /
	 * unknown lease (running Stream Gen 2 sessions END under their old identity
	 * immediately) and asks the devices it streams to for a new lease; Stream Gen
	 * 1 endpoints keep their class and their streams. Also runs automatically
	 * when the application returns to the foreground / is reactivated. Unlike
	 * Connect() it keeps device knowledge, so Stream Gen 1 streams are not
	 * interrupted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Reacquire Stream Ownership (Hapbeat)"))
	void ReacquireStreamOwnership();

	// ---- Real-time clip streaming (Phase 3) ----

	/**
	 * Start streaming a PCM16 clip to the device as live haptics and return a
	 * handle for real-time gain / pan modulation. The SDK pre-multiplies every
	 * sample by the handle's Gain x Pan before sending, so STREAM_BEGIN carries
	 * gain = 1.0 (the device must not re-apply gain).
	 *
	 * The clip is normalized to 16 kHz stereo and mixed per endpoint: every
	 * alive device whose address matches the resolved target gets the source in
	 * its own STREAM_BEGIN/END session. Sources with different targets therefore
	 * play concurrently on their devices, and sources sharing a device are mixed.
	 * Each returned handle has independent gain, pan, loop and stop state. The
	 * mixers are paced by DEDICATED background threads, so frame hitches
	 * (GC / render / physics) cannot starve the device's ring buffer.
	 *
	 * @param Clip         The PCM16 clip (mono or stereo). Null / empty => warn + nullptr.
	 * @param BaselineGain Frozen author gain (entry.gain x manifest.intensity). 0..2.
	 * @param InitialGain  Initial modulator; Gain starts at Baseline x InitialGain.
	 * @param Target       Address filter ("" = broadcast).
	 * @param bLoop        Loop the clip until StopStream() / handle Stop().
	 * @param InitialPan   Pan written onto the handle BEFORE the session starts.
	 *                     It has to be set here rather than on the returned handle
	 *                     because a MONO clip is upmixed to stereo only when the
	 *                     pan is already non-zero at session start — STREAM_BEGIN
	 *                     fixes the channel count for the whole session, so a pan
	 *                     applied afterwards would have nothing to steer.
	 * @return Per-stream handle, or nullptr if the clip was invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Play Stream Clip (Hapbeat)", AdvancedDisplay = "3"))
	UHapbeatStreamPlayback* StreamClip(UHapbeatClip* Clip, float BaselineGain = 1.0f, float InitialGain = 1.0f,
		const FString& Target = TEXT(""), bool bLoop = false, float InitialPan = 0.0f);

	/**
	 * Stop every source on every endpoint session: signal each stream thread to send STREAM_END, JOIN
	 * it (blocks briefly — the thread notices within one ~10ms pacing tick),
	 * then mark the handle stopped and unregister the watchdog ticker. No-op if
	 * nothing is streaming.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Stop Streams (Hapbeat)"))
	void StopStream();

	// ---- Global address override (single-app, multi-HMD 1:1 deployments) ----

	/**
	 * Force the player / group applied to EVERY outgoing command (Play/Stop/
	 * StopAll/StreamClip) at runtime. Pass
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
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Target", meta = (DisplayName = "Set Address Override (Hapbeat)"))
	void SetAddressOverride(int32 Player, int32 InGroup, bool bPersist = false);

	/**
	 * Clear a persisted address override (removes the GameUserSettings ini
	 * keys, if present) and revert the runtime override to disabled on both
	 * axes. There is no config-level default to fall back to — clearing
	 * simply means "stop overriding". Reuses SetAddressOverride (bPersist:
	 * false, so the just-cleared keys aren't immediately re-saved). Mirrors
	 * HapbeatManager.ClearPersistedAddressOverride (Unity SDK).
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Target", meta = (DisplayName = "Clear Saved Address Override (Hapbeat)"))
	void ClearPersistedAddressOverride();

	/**
	 * Read the SAVED address override without applying it — what the next launch
	 * on this machine would start with, which is not necessarily what is running
	 * now (SetAddressOverride with bPersist false changes one and not the other).
	 * Both out params come back as AddressOverrideDisabled (-1) when their key is
	 * absent. Returns true if either key is present.
	 *
	 * Static, and free of any subsystem state, so tooling and UI can ask before a
	 * session exists. Mirrors HapbeatManager.TryGetPersistedAddressOverride
	 * (Unity SDK); like NormalizeAddressOverride it is a plain public static
	 * rather than a UFUNCTION, matching the Unity method it mirrors.
	 */
	static bool TryGetPersistedAddressOverride(int32& OutPlayer, int32& OutGroup);

	/**
	 * Save the per-machine address override without requiring a running game
	 * instance. Editor tooling uses this to prepare the next PIE or packaged
	 * launch. Build-pinned axes are deliberately left out of the saved value:
	 * their value always comes from UHapbeatConfig instead.
	 *
	 * When a game is already running, call SetAddressOverride(..., true)
	 * instead so the new value takes effect immediately as well as persisting.
	 */
	static void SavePersistedAddressOverride(int32 Player, int32 InGroup);

	/** Remove the per-machine override written by SavePersistedAddressOverride.
	 * This does not change an already-running subsystem; use
	 * ClearPersistedAddressOverride() for that case. */
	static void RemovePersistedAddressOverride();

	/** Currently effective forced player number, or AddressOverrideDisabled (-1) if this axis doesn't override the target's player. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat|Target", meta = (DisplayName = "Get Override Player (Hapbeat)"))
	int32 GetOverridePlayer() const { return OverridePlayer; }

	/** Currently effective forced group number, or AddressOverrideDisabled (-1) if this axis doesn't override the target's group. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat|Target", meta = (DisplayName = "Get Override Group (Hapbeat)"))
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
	UFUNCTION(BlueprintPure, Category = "Hapbeat", meta = (DisplayName = "Is Connected (Hapbeat)"))
	bool IsConnected() const { return Socket != nullptr; }

	/** Number of distinct devices that returned a PONG within max(5s, PingInterval*3). */
	UFUNCTION(BlueprintPure, Category = "Hapbeat", meta = (DisplayName = "Get Alive Device Count (Hapbeat)"))
	int32 GetAliveDeviceCount() const;

	/** True if at least one device is responsive. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat", meta = (DisplayName = "Is Device Alive (Hapbeat)"))
	bool IsAlive() const { return GetAliveDeviceCount() > 0; }

	/**
	 * Every device endpoint the stream side knows, with its stream class, lease
	 * state and reported firmware major (sdk-multi-stream.md §3). Use it, with a
	 * Playback's DeferredReason, to explain why a stream is silent.
	 */
	UFUNCTION(BlueprintPure, Category = "Hapbeat", meta = (DisplayName = "Get Stream Endpoints (Hapbeat)"))
	TArray<FHapbeatStreamEndpointInfo> GetStreamEndpoints() const;

	/** True while at least one endpoint-scoped clip stream session is active. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat", meta = (DisplayName = "Is Streaming (Hapbeat)"))
	bool IsStreaming() const { return StreamSessions.Num() > 0; }

	/** First active local source, or nullptr. Hold StreamClip's return value for source-specific control. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat", meta = (DisplayName = "Get Active Stream Playback (Hapbeat)"))
	UHapbeatStreamPlayback* GetActivePlayback() const;

	/** Fires when a device first becomes reachable (alive count 0 -> positive). */
	UPROPERTY(BlueprintAssignable, Category = "Hapbeat", meta = (DisplayName = "On Connected (Hapbeat)"))
	FHapbeatOnConnected OnConnected;

	/** Fires when the last reachable device ages out (alive count positive -> 0). */
	UPROPERTY(BlueprintAssignable, Category = "Hapbeat", meta = (DisplayName = "On Disconnected (Hapbeat)"))
	FHapbeatOnDisconnected OnDisconnected;

	/**
	 * Fires for an ERROR packet the SDK accepted: from a known device's port, and
	 * for stream errors only when it names a session this SDK runs there.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Hapbeat", meta = (DisplayName = "On Error (Hapbeat)"))
	FHapbeatOnError OnError;

	/** Fires when a device endpoint's stream class or lease state changes (see GetStreamEndpoints). */
	UPROPERTY(BlueprintAssignable, Category = "Hapbeat", meta = (DisplayName = "On Stream Endpoint Changed (Hapbeat)"))
	FHapbeatOnStreamEndpointChanged OnStreamEndpointChanged;

	/** Fires for every PONG (one per responsive device per ping on broadcast). */
	UPROPERTY(BlueprintAssignable, Category = "Hapbeat", meta = (DisplayName = "On Pong (Hapbeat)"))
	FHapbeatOnPong OnPong;

private:
	friend class FHapbeatStreamSubsystemRoutingTest;
	friend class FHapbeatStreamLegacyFallbackSubsystemTest;
	friend class FHapbeatStreamReacquireOwnershipTest;
	friend class FHapbeatStreamIdentityUpdateDiscoveryTest;
	friend class FHapbeatClipResampleTest;
	friend class FHapbeatGen2LeasePingTest;
	friend class FHapbeatGen2CapsTest;
	friend class FHapbeatGen2ErrorTest;
	friend class FHapbeatGen2CursorCarryTest;
	friend class FHapbeatGen2StreamStoppedTest;
	friend class FHapbeatGen2DeferredReasonTest;
	friend class FHapbeatGen2SupersededReissueTest;
	friend class FHapbeatGen2IgnoredTailWarningTest;
	void SendPacket(const TArray<uint8>& Packet);
	/** Exact unicast to one device (lease PING, CAPS_REQ). Never opens a socket. */
	void SendUnicastPacket(const FString& Ip, int32 InPort, const TArray<uint8>& Packet);

	// ---- Haptic delay (PlayEntry / StopEntry only) ----

	/**
	 * Effective deferral for this entry: max(0, global HapticDelaySeconds +
	 * entry DelayOffsetSeconds + the call site's ExtraDelaySeconds). Port of Unity
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
	float ComputeEffectiveDelaySeconds(const FHapbeatEventEntry& Entry, float ExtraDelaySeconds = 0.0f) const;

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
	 * Everything StreamClip does once the handle exists: join a compatible active
	 * session or create a new one, ensure the socket, resolve the target, snapshot
	 * the unicast destinations and spin up the stream thread. Split out so the
	 * delayed path can create the handle NOW and start the session LATER on the
	 * very same handle. Returns false if the session could not be started
	 * (the playback is not retained in ActivePlaybacks in that case).
	 */
	bool StartStreamSession(UHapbeatClip* Clip, UHapbeatStreamPlayback* Playback, const FString& Target, bool bLoop);
	/**
	 * Record a PONG-confirmed exact endpoint (migrating one unambiguous expired
	 * route/address, together with its END time and stream class/lease state).
	 * Returns the endpoint key the PONG now lives under.
	 */
	FString RegisterStreamEndpoint(const FString& Ip, int32 InPort, const FString& Address, double NowSeconds);
	/** Recompute each active source's effective target from its authored target and the live override. */
	void RefreshStreamSourceTargets();
	/** Request immediate discovery only while an active source has no matching exact endpoint. */
	void RequestStreamDiscoveryForDeferredSources();
	void ReconcileStreamSources();
	/**
	 * END the endpoint's session and join its thread. bCarryCursors keeps each
	 * source's cursor for the next session on the same device (a retired lease or
	 * class, stream-session.md Sender requirements 8); a stop or a removal does not.
	 */
	void StopStreamSession(const FString& EndpointKey, bool bCarryCursors = false);
	void AbandonStreamSession(const FString& EndpointKey);
	void StartEndpointSession(const FString& EndpointKey);
	bool NormalizeClipToCanonical(const UHapbeatClip* Clip, TArray<uint8>& OutPcm16) const;

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
	 * event never stops.
	 */
	void SendCommandPacket(const TArray<uint8>& Packet, const FString& ResolvedTarget);


	/**
	 * AppName as it goes on the wire: the stored (raw, templated) name with the
	 * "<p>" / "<g>" address-override placeholders substituted for the CURRENT
	 * override, so a templated name tracks SetAddressOverride live on the device
	 * OLED. BuildConnectStatus applies the 16-char cap afterwards. Parity with
	 * HapbeatManager.AppName (Unity SDK).
	 */
	FString AppNameForWire() const;

	/**
	 * Thread-safe: guarded by SeqLock so both the game thread (Play/Stop/
	 * StopAll/Ping/CONNECT_STATUS) and the dedicated stream
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
	/**
	 * Resolve PONG RTT on the game thread. An unsolicited PONG with timestamp 0
	 * has no clock sample, so it intentionally reports 0 rather than treating
	 * the Unix epoch as a multi-decade round trip. The pending entry is NOT
	 * consumed: a broadcast PING is answered by every device, and each reply has
	 * to be able to correlate. Entries age out in PrunePendingPings().
	 */
	static int64 ResolvePongRttUs(const TMap<uint16, FHapbeatPendingPing>& InPendingPings,
		uint16 PongSeq, int64 NowMonotonicUs, int64 EchoedTimestampUs, int64 NowUnixUs);

	/**
	 * Game-thread half of PONG handling (marshalled from HandleReceivedData):
	 * stream class / lease classification (HapbeatStreamSessionContract::
	 * EvaluatePongLease), liveness, endpoint registration and reconciliation.
	 */
	void HandlePongGameThread(const FString& SenderIp, int32 SenderPort, uint16 PongSeq, int64 NowMonotonicUs,
		const FHapbeatProtocol::FPongInfo& Pong);

	/** Fresh nonzero random client incarnation (stream-session.md "Discovery lease"). */
	static uint64 CreateClientIncarnation();

	/** Apply a parsed PONG result on the game thread (marshalled from the worker thread). */
	void OnPongGameThread(const FString& Endpoint, int64 RttUs, const FString& DeviceName, const FString& Address, const FString& Firmware);

	/**
	 * Game-thread half of ERROR handling (message-format.md 0xFF "receiving
	 * rules", stream-session.md Sender requirements 5): accept only from a known
	 * device's IP and the device port, stream errors only for a session this SDK
	 * tracks there, and act per endpoint.
	 */
	void HandleErrorGameThread(const FString& SenderIp, int32 SenderPort, const FHapbeatProtocol::FErrorInfo& Error);

	/** Game-thread half of CAPS handling: match the request by route and seq, then record. */
	void HandleCapsGameThread(const FString& SenderIp, int32 SenderPort, uint16 CapsSeq,
		const FHapbeatProtocol::FCapsInfo& Caps);

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

	/** Stream lease TTL of the receivers (stream-session.md "Lease table"). */
	static constexpr double LeaseTtlSeconds = 15.0;

	/**
	 * Unicast extended-PING interval for an endpoint with a matched reply (any
	 * class): one third of the lease TTL (stream-session.md "[1.x] How SDKs send
	 * extended PINGs").
	 */
	static constexpr double LeaseRefreshSeconds = 5.0;

	/** Retry interval of the extended PING to an endpoint that has not answered one yet. */
	static constexpr double UnknownLeasePingSeconds = 1.0;

	/** Interval of the next unicast extended PING to an endpoint of this class. */
	static double ComputeLeasePingIntervalSeconds(EHapbeatStreamEndpointClass Class)
	{
		return Class == EHapbeatStreamEndpointClass::Unknown ? UnknownLeasePingSeconds : LeaseRefreshSeconds;
	}

	/**
	 * How often to actually send the discovery PING + CONNECT_STATUS: fast until
	 * a device answers, then PingInterval. Lease refresh has its own unicast
	 * cadence (PumpLeasePings), so the broadcast no longer needs a cap.
	 */
	float KeepAliveIntervalSeconds() const
	{
		return ComputeKeepAliveIntervalSeconds(GetAliveDeviceCount(), PingInterval);
	}
	static float ComputeKeepAliveIntervalSeconds(int32 AliveDeviceCount, float InPingInterval)
	{
		return AliveDeviceCount <= 0 ? DiscoveryTickSeconds : InPingInterval;
	}

	/**
	 * Send one discovery PING if a socket is open (never opens one). The
	 * automation seam records the request instead of sending.
	 */
	void RequestDiscoveryPing();

	/** Strictly increasing PING wire timestamp (lease adoption is ordered by the echoed one). */
	int64 NextPingTimestampUs();
	/** Drop pending PINGs older than one lease TTL. */
	void PrunePendingPings(int64 NowMonotonicUs);

	/**
	 * The endpoints this SDK streams to or is about to: a live endpoint whose
	 * address matches the target of a Deferred / Active source, one with a
	 * session, or one whose last session ended less than one TTL ago.
	 */
	bool IsLeaseTarget(const FString& EndpointKey, double NowSeconds) const;
	/** Send the unicast extended PINGs that are due (a new lease target at once). */
	void PumpLeasePings(double NowSeconds);
	/** Make every lease target due now (reacquisition, re-resolution), then pump. */
	void RequestLeasePingsNow();
	/** One unicast extended PING (lease request) to this endpoint. */
	void SendLeasePing(const FString& EndpointKey);

	/** Ask this route for CAPS if it is new to us or its firmware / boot changed (message-format.md 0x12). */
	void MaybeRequestCaps(const FString& Ip, int32 InPort, const FString& Firmware, uint64 DeviceBootId);
	void SendCapsRequest(const FString& RouteKey);
	/** Retry CAPS_REQ every 500 ms, three times in all; then the device reports none. */
	void TickCapsRequests(double NowSeconds);

	/** Next generation under this lease, or 0 when exhausted. Thread-safe (stream workers re-splice / restart). */
	uint32 AllocateGeneration(const FHapbeatStreamLeaseIdentity& Lease);

	/** sdk-multi-stream.md §3: why a source with no endpoint waits. */
	EHapbeatStreamDeferredReason ComputeDeferredReason(const FGuid& SourceId, double NowSeconds) const;
	/** Raise OnStreamEndpointChanged for every endpoint whose class or lease state changed. */
	void NotifyEndpointChanges();

	/** Foreground / reactivation hook: ReacquireStreamOwnership, debounced, on the game thread. */
	void HandleApplicationForeground();
	void BindApplicationLifecycle();
	void UnbindApplicationLifecycle();
	FDelegateHandle EnteredForegroundHandle;
	FDelegateHandle ReactivatedHandle;
	FDelegateHandle SlateActivationHandle;
	/** FPlatformTime::Seconds() of the last ReacquireStreamOwnership (debounces duplicate lifecycle events). */
	double LastReacquireSeconds = -1.0e9;

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
	/** seq -> outstanding PING (send time for RTT, wire timestamp + incarnation for lease correlation). */
	TMap<uint16, FHapbeatPendingPing> PendingPings;
	/**
	 * Client incarnation written into every extended PING. Regenerated (nonzero)
	 * in Connect() and ReacquireStreamOwnership() (explicit, or on foreground
	 * resume); periodic discovery keeps it unchanged. Game thread only.
	 */
	uint64 ClientIncarnation = 0;
	/** Last PING wire timestamp; PING timestamps are made strictly increasing so reply order is total. */
	int64 LastPingTimestampUs = 0;
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
	 * Active local source handles. UPROPERTY roots every source while the shared
	 * wire session is alive; finished one-shots are pruned by TickStream while a
	 * looping sibling continues. The worker only touches their atomic mirrors.
	 */
	UPROPERTY()
	TArray<TObjectPtr<UHapbeatStreamPlayback>> ActivePlaybacks;

	struct FStreamSource
	{
		TArray<uint8> CanonicalPcm16;
		/** Immutable target supplied by StreamClip/EventMap; overrides never mutate this authored value. */
		FString AuthoredTarget;
		/** Effective target used to assign this logical source to PONG-confirmed endpoint sessions. */
		FString ResolvedTarget;
		TWeakObjectPtr<UHapbeatStreamPlayback> Playback;
		TSet<FString> EndpointKeys;
		TSet<FString> CompletedEndpointKeys;
		/**
		 * Endpoint key -> cursor (source frames) to continue from when this device's
		 * next session starts (a lease / ticket / class change retired the previous
		 * one; stream-session.md Sender requirements 8). Consumed when used.
		 */
		TMap<FString, double> CarriedCursors;
		/**
		 * Endpoints whose session STOP_ALL ended (ERROR STREAM_STOPPED): this
		 * Playback never rejoins them (sdk-multi-stream.md §6.13).
		 */
		TSet<FString> StoppedEndpointKeys;
		/** Registered by the application since the last reconcile: may re-splice a running session (§6.14). */
		bool bNew = true;
	};

	struct FStreamEndpoint
	{
		FString Ip;
		int32 Port = 0;
		FString Address;
		double LastPongSeconds = 0.0;
		/** Unknown until a matched reply to this SDK's extended PING classifies it (Stream Gen 2 or 1). */
		FHapbeatStreamEndpointLeaseState LeaseState;
		/** When the next unicast extended PING is due (FPlatformTime::Seconds()). */
		double NextLeasePingSeconds = 0.0;
		/** When a matched reply last confirmed the class / lease (FPlatformTime::Seconds()). */
		double LeaseConfirmedSeconds = -1.0e9;
		/** Was a lease target at the last pump (a new target is asked at once). */
		bool bLeaseTarget = false;
		/** When the last session to it ended; keeps the lease refreshed for one TTL after. */
		double LastSessionEndedSeconds = -1.0e9;
		/** STREAM_LEASE_INVALID: the next matched reply with the same lease begins a new generation. */
		bool bRestartOnLeaseReply = false;
	};

	struct FStreamSession
	{
		FHapbeatStreamRunnable* Runnable = nullptr;
		FRunnableThread* Thread = nullptr;
		TSet<FGuid> SourceIds;
		/** Immutable for the session: the END goes out in this format even after a class change. */
		EHapbeatStreamWireFormat Format = EHapbeatStreamWireFormat::LegacyV1;
		/**
		 * SessionV2 only: the identity the session began with. The worker may move
		 * it to later generations of the same lease (Runnable->GetCurrentIdentity()).
		 */
		FHapbeatStreamSessionIdentity Identity;
	};

	/** CAPS exchange state per device route "ip:port" (message-format.md 0x12 / 0x13). */
	struct FCapsState
	{
		enum class EStatus : uint8 { Requesting, Received, None };
		EStatus Status = EStatus::Requesting;
		FString Ip;
		int32 Port = 0;
		/** The firmware_version / boot the request was made for; a change asks again. */
		FString Firmware;
		uint64 DeviceBootId = 0;
		/** seq of every CAPS_REQ sent in this round; a CAPS must echo one of them. */
		TArray<uint16> RequestSeqs;
		int32 RequestsSent = 0;
		double NextRequestSeconds = 0.0;
		FHapbeatProtocol::FCapsInfo Info;
	};

	/**
	 * Old key the PONG of (Ip, Port, Address) moves from, or empty: the same
	 * route under another address (the device's address changed), else exactly
	 * one expired route / address match.
	 */
	FString FindMigrationSourceKey(const FString& Ip, int32 InPort, const FString& Address, double NowSeconds) const;
	FHapbeatStreamEndpointInfo MakeEndpointInfo(const FStreamEndpoint& Endpoint) const;
	/** CAPS haptic_channels is 1 for this route: mix without pan (sdk-multi-stream.md §6.15). */
	bool IsSingleMotorRoute(const FString& Ip, int32 InPort) const;
	/** Bookkeeping when an endpoint's session goes away: last identity (for ERROR) and end time (lease refresh). */
	void RecordEndedSession(const FString& EndpointKey, const FStreamSession& Session);

	/**
	 * The endpoint state a PONG from (Ip, Port, Address) will be registered into:
	 * the exact endpoint, or the one unambiguous migration candidate that
	 * RegisterStreamEndpoint would move. nullptr for a brand-new endpoint.
	 */
	const FStreamEndpoint* FindEndpointForRegistration(
		const FString& Ip, int32 InPort, const FString& Address, double NowSeconds) const;

	/** Sources may be assigned to this endpoint: Stream Gen 1, or a usable Stream Gen 2 lease for the current incarnation. */
	bool IsEndpointStreamable(const FStreamEndpoint& Endpoint) const;
	/** A Stream Gen 2 lease was confirmed within one TTL, or its session is running. */
	bool IsLeaseConfirmed(const FStreamEndpoint& Endpoint) const;

	/** The running session still matches its endpoint's class (and, for Stream Gen 2, its lease). */
	bool IsSessionCurrentForEndpoint(const FStreamSession& Session, const FStreamEndpoint& Endpoint) const;

	/**
	 * Next generation per lease (Unity HapbeatEndpointStreamMixer._nextGenerationByLease).
	 * Never reset while the subsystem lives, so a generation is never reused under a lease.
	 * Guarded by GenerationLock: stream workers allocate too (re-splice, own gap).
	 */
	TMap<FHapbeatStreamLeaseIdentity, uint32> NextGenerationByLease;
	FCriticalSection GenerationLock;

	/** Endpoint key -> identity of the session that ended there last (an ERROR may still name it). */
	TMap<FString, FHapbeatStreamSessionIdentity> LastEndedIdentityByEndpoint;
	/** Route "ip:port" -> CAPS exchange. */
	TMap<FString, FCapsState> CapsByRoute;
	/** versioning.md §3.3: the "newer device major" warning is given once per subsystem. */
	bool bWarnedNewerDeviceMajor = false;
	/** Endpoint keys already warned about a matched reply whose tail was ignored. */
	TSet<FString> WarnedIgnoredTailEndpoints;
	/** "ip:port|code" of ERRORs already logged (one log line per code and endpoint). */
	TSet<FString> LoggedErrorKeys;
	/** Endpoint key -> class / lease state last published through OnStreamEndpointChanged. */
	TMap<FString, TPair<EHapbeatStreamClass, EHapbeatStreamLeaseState>> PublishedEndpointStates;

	/** PONG-confirmed exact endpoints; STREAM packets are never broadcast. */
	TMap<FString, FStreamEndpoint> StreamEndpoints;
	/** Logical sources, each independently assigned to matching endpoint sessions. */
	TMap<FGuid, FStreamSource> StreamSources;
	/** One runnable/thread per exact PONG endpoint. */
	TMap<FString, FStreamSession> StreamSessions;
	/**
	 * Last Stream Gen 1 END time per endpoint / route+address; enforces the 300 ms
	 * END->BEGIN guard for Stream Gen 1 receivers only. Stream Gen 2 sessions never
	 * record here and never wait (stream-session.md "Legacy receiver fallback (SDK)").
	 */
	TMap<FString, double> StreamSessionEndedAt;
	TMap<FString, double> StreamRouteEndedAt;

	/** Game-thread watchdog ticker driving TickStream; valid while sources are active or Deferred. */
	FTSTicker::FDelegateHandle StreamTickHandle;

	/** Send-ahead lead for streaming; seeded from UHapbeatConfig in Initialize. */
	float StreamSendAheadSeconds = 0.05f;

	/** Config: unicast PLAY/STOP/STOP_ALL to known devices instead of broadcasting (UHapbeatConfig::bCommandUnicast). */
	bool bCommandUnicast = true;

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

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * Test-only network seam: discovery PINGs are counted, lease PINGs and
	 * CAPS_REQs recorded (with their pending request), and nothing is sent.
	 */
	bool bSuppressStreamDiscoveryForAutomationTest = false;
	int32 StreamDiscoveryRequestCount = 0;
	struct FAutomationUnicastRecord
	{
		/** Endpoint key (lease PING) or route "ip:port" (CAPS_REQ). */
		FString Key;
		uint16 Seq = 0;
		int64 TimestampUs = 0;
	};
	TArray<FAutomationUnicastRecord> LeasePingsForAutomationTest;
	TArray<FAutomationUnicastRecord> CapsRequestsForAutomationTest;
	/**
	 * Test-only session seam: when bound, StartEndpointSession builds its runnable
	 * here (a recording runner whose SendRaw never touches a socket) and no socket
	 * is required. Arguments: first source id, its PCM, BEGIN target, mirror,
	 * wire format and session identity.
	 */
	TFunction<FHapbeatStreamRunnable*(const FGuid&, TArray<uint8>&&, const FString&,
		TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>, EHapbeatStreamWireFormat,
		const FHapbeatStreamSessionIdentity&)> StreamRunnableFactoryForAutomationTest;
#endif
};
