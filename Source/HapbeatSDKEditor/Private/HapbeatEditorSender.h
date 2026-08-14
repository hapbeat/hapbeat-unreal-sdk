// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatNetInterfaces.h"   // FHapbeatBroadcastRoute (held by value in a TArray below)

class FSocket;
class FInternetAddr;

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
 */
class FHapbeatEditorSender
{
public:
	/** Send a PLAY for EventId at Gain (0..1 nominal; already gain x intensity composed by the caller). Immediate (target_time = 0). No-op (warns) if EventId is empty. */
	static void SendPlay(const FString& EventId, float Gain, const FString& Target = TEXT(""));

	/** Send a STOP for EventId. No-op (warns) if EventId is empty. */
	static void SendStop(const FString& EventId, const FString& Target = TEXT(""));

	/** Send a STOP_ALL. Target "" broadcasts to every device. */
	static void SendStopAll(const FString& Target = TEXT(""));

	/** Send a PING. Replies ARE read back (see DrainReplies) so subsequent sends can unicast. */
	static void SendPing();

	/**
	 * Stream a clip from the editor, without entering PIE.
	 *
	 * Command entries can be sanity-checked with a single PLAY, but a Stream
	 * Clip only exists as audio the SDK pushes out over time, so without this
	 * there is no way to feel one while authoring -- which made Stream Clip
	 * entries effectively unverifiable in the editor. Paced from the core
	 * ticker, which runs in the bare editor. Parity with Unity's
	 * HapbeatEditorTransport.StartStream (driven by EditorApplication.update).
	 *
	 * Starting a stream replaces any stream already running: the device mixes a
	 * single ring buffer, so two overlapping editor streams would interleave
	 * into noise rather than layer.
	 */
	static void StartStream(const class UHapbeatClip* Clip, float Gain, const FString& Target, bool bLoop);

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

	/** Unicast to known devices, count sent. Shared by SendRouted and SendStreamPacket. */
	static int32 SendToKnownDevices(const TArray<uint8>& Packet);

	/**
	 * Route one stream packet. Identical to SendRouted minus the trailing
	 * discovery PING: a stream sends ~45 packets a second, and a PING after each
	 * one floods the network badly enough to chop up the very audio it carries.
	 */
	static void SendStreamPacket(const TArray<uint8>& Packet);

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
	 * Collect any PONGs waiting on the socket.
	 *
	 * This sender has no tick, so replies are drained opportunistically just
	 * before each send. That is enough: every send is preceded by a PING, so by
	 * the time a designer clicks Test Play a second time the reply from the
	 * first is already queued.
	 */
	static void DrainReplies();

	/** Devices that replied, and when (FPlatformTime::Seconds). */
	static TMap<FString, double> DevicePongTimes;

	/**
	 * Candidate broadcast destinations, one per local IPv4 subnet plus the
	 * limited-broadcast catch-all. Rebuilt whenever the socket is (re)opened.
	 * See HapbeatNetInterfaces.h for why 255.255.255.255 alone is not enough.
	 */
	static TArray<FHapbeatBroadcastRoute> BroadcastRoutes;

	static uint16 NextSeq();
	/** Unix-epoch microseconds for the PING wire field. Mirrors UHapbeatSubsystem::UnixMicros(). */
	static int64 UnixMicros();

	/** Feeds the running stream; returns false to unregister itself when done. */
	static bool TickStream(float DeltaSeconds);

	/** A stream in progress. Null when idle -- only ever one (see StartStream). */
	struct FStreamState
	{
		/** Already scaled by the requested gain: STREAM_BEGIN carries 1.0 and the
		 *  sender premultiplies, exactly as the runtime streamer does, so the
		 *  device must not apply a second factor. */
		TArray<uint8> Pcm16;
		int32 SampleRate = 0;
		int32 Channels = 0;
		FString Target;
		bool bLoop = false;
		int32 Offset = 0;
		double StartTime = 0.0;
		/** Last discovery PING, so a long loop keeps its unicast destinations. */
		double LastPingTime = 0.0;
	};

	static TUniquePtr<FStreamState> Stream;
	static FTSTicker::FDelegateHandle StreamTickerHandle;

	static FSocket* Socket;
	static TSharedPtr<FInternetAddr> BroadcastAddr;
	static uint16 Seq;
};
