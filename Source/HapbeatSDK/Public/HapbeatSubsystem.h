// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Common/UdpSocketReceiver.h"         // FUdpSocketReceiver + FArrayReaderPtr typedef
#include "Interfaces/IPv4/IPv4Endpoint.h"     // FIPv4Endpoint
#include "Subsystems/GameInstanceSubsystem.h"
#include "HapbeatSubsystem.generated.h"

class FSocket;
class FInternetAddr;
class FHapbeatStreamer;
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
 */
UCLASS()
class HAPBEATSDK_API UHapbeatSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UHapbeatSubsystem();
	// Out-of-line dtor (defined in the .cpp where FHapbeatStreamer is complete) so
	// the owned Streamer pointer can be deleted with only a forward
	// declaration in this header (the generated dtor would otherwise need the full type).
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
	 * stream (STREAM_END) then starts a fresh one (STREAM_BEGIN). Game-thread
	 * paced via an internal ticker registered only while streaming.
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
	 * Stop the active stream: send STREAM_END, mark the handle stopped, and
	 * unregister the streaming ticker. No-op if nothing is streaming.
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
	bool IsStreaming() const { return Streamer != nullptr; }

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
	uint16 NextSeq();

	/** Send PING + CONNECT_STATUS, then diff the alive set and raise events. Bound to the FTSTicker. */
	bool TickKeepAlive(float DeltaSeconds);

	/**
	 * Drive the active streamer one frame. Bound to a dedicated FTSTicker that is
	 * registered only while streaming. Returns false (auto-unregisters the ticker)
	 * once the stream finishes; true to keep ticking.
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
	uint8 Group = 0; // header group field (display-only); seeded from UHapbeatConfig::Group in Initialize
	FString AppName;
	uint16 Seq = 0;
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

	// --- streaming (Phase 3); game-thread only ---

	/**
	 * The active stream handle. A UPROPERTY so it is a GC root while streaming
	 * (the caller may not retain it). Cleared when the stream ends.
	 */
	UPROPERTY()
	TObjectPtr<UHapbeatStreamPlayback> ActivePlayback = nullptr;

	/**
	 * The single active streamer (PCM copy + pacing). Null when not streaming.
	 * Owned raw pointer (deleted in StopStream/TickStream/dtor), same pattern as
	 * Receiver above. Deliberately NOT a TUniquePtr: UHT's gen.cpp includes this
	 * header with FHapbeatStreamer still incomplete and instantiates the smart
	 * pointer's deleter there -> C4150 "deletion of incomplete type" as-error.
	 */
	FHapbeatStreamer* Streamer = nullptr;

	/** Per-frame ticker driving TickStream; valid only while a stream is active. */
	FTSTicker::FDelegateHandle StreamTickHandle;

	/** Send-ahead lead for streaming; seeded from UHapbeatConfig in Initialize. */
	float StreamSendAheadSeconds = 0.05f;
};
