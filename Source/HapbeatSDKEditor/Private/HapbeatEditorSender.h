// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HapbeatNetInterfaces.h"   // FHapbeatBroadcastRoute (held by value in a TArray below)
#include "HapbeatStreamGainMirror.h"
#include "HapbeatStreamLease.h"     // per-device stream class / lease (Stream Gen 2)

class FSocket;
class FInternetAddr;
class FRunnableThread;
class FHapbeatStreamRunnable;

/**
 * Minimal editor-only UDP sender used by the EventMap "Test Play" detail
 * customization (HapbeatEventMapCustomization) so a designer can sanity-check
 * an entry's wire gain against a real device without entering PIE.
 *
 * A plain static class (no UObject) holding a lazy singleton FSocket -- kept
 * entirely separate from UHapbeatSubsystem's runtime socket:
 *   - GameInstanceSubsystems (UHapbeatSubsystem) only exist in PIE / a packaged
 *     game, never in the bare editor, so there is nothing to "fight" over the
 *     broadcast destination port (UHapbeatConfig::Port) -- both sockets BIND
 *     to an OS-assigned ephemeral local port (port 0) and only ever TARGET
 *     255.255.255.255:<Port> when sending, so two independent ephemeral binds
 *     never collide.
 *   - Still, the socket is explicitly closed on module ShutdownModule AND on
 *     FEditorDelegates::EndPIE (belt-and-braces per the design doc, and it
 *     also means a stale open socket doesn't linger for an entire editor
 *     session after the last Test Play click) and is re-opened lazily on the
 *     next send.
 *
 * Seq is a local counter independent of any UHapbeatSubsystem instance's Seq
 * space; devices dedup by (source endpoint, seq), and the editor sender uses
 * its own bound port, so its own seq stream cannot collide with the runtime
 * subsystem's.
 *
 * Stream Test Play follows the runtime's stream rules with the same pure parts
 * (HapbeatStreamSessionContract::EvaluatePongLease, FHapbeatStreamRunnable): one
 * session per device whose address matches the target, Stream Gen 2 under the
 * lease the device issued to this sender's incarnation, Stream Gen 1 for firmware
 * before 0.5.0 (with its 300 ms END->BEGIN guard).
 */
class FHapbeatEditorSender
{
public:
	/** What the editor knows about one device (keyed by IPv4). */
	struct FEditorDevice
	{
		FString Ip;
		int32 Port = 0;
		FString Address;
		double LastPongSeconds = -1.0e9;
		/** From matched replies to this sender's own extended PINGs only. */
		FHapbeatStreamEndpointLeaseState LeaseState;
		/** Last Stream Gen 1 END to this device (the 300 ms guard). */
		double LastGen1EndSeconds = -1.0e9;
	};

	/** One stream session a Test Play will open. */
	struct FEditorStreamPlan
	{
		FString Ip;
		int32 Port = 0;
		/** BEGIN target: the device's full address. */
		FString Address;
		EHapbeatStreamWireFormat Format = EHapbeatStreamWireFormat::LegacyV1;
		/** Stream Gen 2 only. */
		FHapbeatStreamLeaseIdentity Lease;
	};

	/**
	 * Which devices a stream Test Play opens a session to, and in which format
	 * (sdk-multi-stream.md §4): a live device whose address matches the target,
	 * Stream Gen 1 if classified so, Stream Gen 2 with a usable lease for
	 * Incarnation. Unknown, lease-less and superseded devices get nothing.
	 * Pure (no socket), so it can be tested.
	 */
	static TArray<FEditorStreamPlan> PlanStreamSessions(const TArray<FEditorDevice>& InDevices,
		const FString& ResolvedTarget, uint64 Incarnation, double NowSeconds);

	/**
	 * The session for one plan, not started: BEGIN targets the device's address,
	 * the only destination is the device's IPv4, Stream Gen 2 begins at the next
	 * generation of the plan's lease. nullptr when the lease has no generation
	 * left. Public for the automation test (InSocket may be null there).
	 */
	static FHapbeatStreamRunnable* CreateSessionRunnable(const FEditorStreamPlan& Plan, TArray<uint8>&& Pcm16,
		int32 SampleRate, int32 NumChannels, TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> Mirror,
		FSocket* InSocket, float SendAheadSeconds);

	/**
	 * Send a PLAY for EventId at Gain (0..1 nominal; already gain x intensity
	 * composed by the caller). Immediate (target_time = 0). No-op (warns) if
	 * EventId is empty. Pan is the entry's authored balance (-1..+1); a device on
	 * firmware older than DEC-055 ignores it and plays centred.
	 */
	static void SendPlay(const FString& EventId, float Gain, const FString& Target = TEXT(""), float Pan = 0.0f);

	/** Send a STOP for EventId. No-op (warns) if EventId is empty. */
	static void SendStop(const FString& EventId, const FString& Target = TEXT(""));

	/** Send a STOP_ALL. Target "" broadcasts to every device. */
	static void SendStopAll(const FString& Target = TEXT(""));

	/** Send a PING. Replies ARE read back (see DrainReplies) so subsequent sends can unicast. */
	static void SendPing();

	/**
	 * Stream a clip from the editor, without entering PIE.
	 *
	 * Each device whose address matches the target gets its own session: Stream
	 * Gen 2 under the lease it issues to this editor (asked for by unicast right
	 * before the stream starts, a few hundred ms at most), or Stream Gen 1 for
	 * firmware before 0.5.0. A device that superseded this editor's lease is
	 * taken back: Test Play is an explicit request.
	 *
	 * Command entries can be sanity-checked with a single PLAY, but a Stream
	 * Clip only exists as audio the SDK pushes out over time, so without this
	 * there is no way to feel one while authoring -- which made Stream Clip
	 * entries effectively unverifiable in the editor.
	 *
	 * Paced by the SAME dedicated-thread runnable the runtime uses. An earlier
	 * version paced from the editor's core ticker and dropped out irregularly:
	 * the editor tick is not a steady clock (it throttles when the window is not
	 * being interacted with, and stalls outright behind menus and modal
	 * dialogs), so the device's ring buffer ran dry at unpredictable moments.
	 * That is the same failure the runtime already moved off the game thread to
	 * avoid, so the fix is to share that implementation rather than re-tune a
	 * second one.
	 *
	 * Starting a stream replaces any stream already running: the device mixes a
	 * single ring buffer, so two overlapping editor streams would interleave
	 * into noise rather than layer.
	 *
	 * Pan is the entry's authored balance and is placed on the mirror BEFORE the
	 * stream thread starts, so a mono clip gets upmixed to stereo exactly as it
	 * would at runtime -- the audition and the game hear the same thing.
	 */
	static void StartStream(const class UHapbeatClip* Clip, float Gain, const FString& Target, bool bLoop,
		float Pan = 0.0f);

	/** Stop the editor stream and emit STREAM_END. Safe when nothing is streaming. */
	static void StopStream();

	static bool IsStreaming();

	/** Close the lazy socket, if open. Safe to call repeatedly. Called from module ShutdownModule and FEditorDelegates::EndPIE. */
	static void Shutdown();

private:
	static bool EnsureSocket();

	/**
	 * Send one PLAY/STOP/STOP_ALL, unicast to devices that answered recently.
	 *
	 * Broadcast remains the fallback until a device replies. NEVER both: firmware
	 * older than the (source endpoint, seq) de-duplication would fire the same
	 * PLAY twice.
	 */
	static void SendRouted(const TArray<uint8>& Packet);

	/** Unicast to known devices, count sent. Shared by editor command routes. */
	static int32 SendToKnownDevices(const TArray<uint8>& Packet);

	/**
	 * PING every candidate broadcast destination.
	 *
	 * DISCOVERY ONLY. PING is idempotent, so a device reachable on two routes
	 * simply answers twice. A playback command must never come through here --
	 * firmware older than v0.3.0 has no (source endpoint, seq) de-duplication and
	 * would fire the haptic once per route. Use SendSingleBroadcast for those.
	 */
	static void SendDiscoveryBroadcast(const TArray<uint8>& Packet);

	/** Send to exactly one destination. The playback fallback -- see SendRouted. */
	static void SendSingleBroadcast(const TArray<uint8>& Packet);

	/**
	 * Collect any PONGs / ERRORs waiting on the socket.
	 *
	 * This sender has no tick, so replies are drained opportunistically just
	 * before each send. That is enough: every send is preceded by a PING, so by
	 * the time a designer clicks Test Play a second time the reply from the
	 * first is already queued. PONGs also classify devices (stream class /
	 * lease); an accepted ERROR is logged once per code and device.
	 */
	static void DrainReplies();

	/** An ERROR from a device: accepted ones are logged once per device and code. */
	static void HandleError(const FString& SenderIp, int32 SenderPort, const FHapbeatProtocol::FErrorInfo& Error);

	/** Unicast extended PING (lease request) to one device. */
	static void SendLeasePing(const FEditorDevice& Device);
	/**
	 * Ask these devices for a lease and wait (bounded) until each has answered
	 * a request sent now. Returns false if the wait ran out.
	 */
	static bool RequestLeases(const TArray<FString>& DeviceIps);
	static int64 NextPingTimestampUs();
	static uint32 AllocateGeneration(const FHapbeatStreamLeaseIdentity& Lease);

	/** Devices that replied, and when (FPlatformTime::Seconds). */
	static TMap<FString, double> DevicePongTimes;
	/** Devices that replied with an address (the stream side), keyed by IPv4. */
	static TMap<FString, FEditorDevice> Devices;
	/** Outstanding PINGs, for matching replies (Stream Gen 2 lease). */
	static TMap<uint16, FHapbeatPendingPing> PendingPings;
	/** This sender's client incarnation; renewed to take a superseded device back. */
	static uint64 ClientIncarnation;
	static int64 LastPingTimestampUs;
	/** Next generation per lease, shared with the stream workers (own-gap restart). */
	static TMap<FHapbeatStreamLeaseIdentity, uint32> NextGenerationByLease;
	static FCriticalSection GenerationLock;
	/** "ip|code" of ERRORs already logged. */
	static TSet<FString> LoggedErrors;

	/**
	 * Candidate broadcast destinations, one per local IPv4 subnet plus the
	 * limited-broadcast catch-all. Rebuilt whenever the socket is (re)opened.
	 * See HapbeatNetInterfaces.h for why 255.255.255.255 alone is not enough.
	 */
	static TArray<FHapbeatBroadcastRoute> BroadcastRoutes;

	static uint16 NextSeq();
	/** Unix-epoch microseconds for the PING wire field. Mirrors UHapbeatSubsystem::UnixMicros(). */
	static int64 UnixMicros();

	/**
	 * Blocks briefly for a PONG when no device is known yet, so a stream has
	 * somewhere to go. Streams are never broadcast: each session is unicast to
	 * one device chosen when it starts. A quarter second once, before a test
	 * the user just asked for, is a fair price for that.
	 */
	static void WaitForFirstDevice();

	struct FEditorStream
	{
		FHapbeatStreamRunnable* Runnable = nullptr;
		FRunnableThread* Thread = nullptr;
		FString Ip;
		EHapbeatStreamWireFormat Format = EHapbeatStreamWireFormat::LegacyV1;
	};

	/** Live gain for the running stream (shared by every device's session). */
	static TSharedPtr<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> StreamMirror;
	/** One session per device of the running Test Play. */
	static TArray<FEditorStream> Streams;

	static FSocket* Socket;
	static TSharedPtr<FInternetAddr> BroadcastAddr;
	static uint16 Seq;
	/** Guards Seq: the game thread and the stream worker both draw from it. */
	static FCriticalSection SeqLock;
};
