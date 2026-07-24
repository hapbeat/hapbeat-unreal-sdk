// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"

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

	/** Send a PING (connectivity sanity-check from the editor; replies are not read back -- fire-and-forget). */
	static void SendPing();

	/** Close the lazy socket, if open. Safe to call repeatedly. Called from module ShutdownModule and FEditorDelegates::EndPIE. */
	static void Shutdown();

private:
	static bool EnsureSocket();
	static void SendPacket(const TArray<uint8>& Packet);
	static uint16 NextSeq();
	/** Unix-epoch microseconds for the PING wire field. Mirrors UHapbeatSubsystem::UnixMicros(). */
	static int64 UnixMicros();

	static FSocket* Socket;
	static TSharedPtr<FInternetAddr> BroadcastAddr;
	static uint16 Seq;
};
