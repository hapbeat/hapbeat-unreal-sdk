// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatProtocol.h"
#include "HapbeatStreamLease.h"

namespace HapbeatStreamSessionContract
{
	/**
	 * The same route (IPv4 + UDP port) reporting another address is the same
	 * device whose address changed: it moves to the new key at once, alive or
	 * not, so the old key cannot keep streaming the old address's sources to it
	 * (sdk-multi-stream.md §4.1 / §4.6). Takes precedence over
	 * IsMigrationCandidate.
	 */
	inline bool IsSameRoute(const FString& ExistingIp, int32 ExistingPort, const FString& NewIp, int32 NewPort)
	{
		return ExistingIp == NewIp && ExistingPort == NewPort;
	}

	inline bool IsMigrationCandidate(
		const FString& ExistingIp,
		int32 ExistingPort,
		const FString& ExistingAddress,
		const FString& NewIp,
		int32 NewPort,
		const FString& NewAddress,
		bool bExistingAlive)
	{
		// A live endpoint on another route is never collapsed merely because its
		// address matches: two live devices may share an address on different IPs.
		// Once its liveness expires, a stable route or stable address can identify
		// one unambiguous migration candidate.
		return !bExistingAlive
			&& ((ExistingIp == NewIp && ExistingPort == NewPort)
				|| ExistingAddress == NewAddress);
	}

	/** What a received PONG may do to its endpoint's stream class / lease. */
	enum class EPongLeaseDecision : uint8
	{
		/**
		 * Ordinary discovery only: liveness / address / RTT update, class and
		 * lease stay as they are. Unsolicited (timestamp 0), unmatched, late
		 * replies and replies to 8-byte discovery PINGs land here.
		 */
		NoChange,
		/**
		 * As NoChange, but the reply was a matched direct reply to this SDK's
		 * extended PING whose tail the [1.x] rule ignores (stream-session.md
		 * "Receiver classification"): the caller warns once for the endpoint.
		 */
		IgnoredMatched,
		/**
		 * Drop the whole PONG (Unity HapbeatClient.EvaluateStreamLeasePong
		 * DiscardPong): a structurally valid HBS2 reply whose correlation fails,
		 * i.e. a reply for a retired request or a previous incarnation. It must
		 * not roll endpoint address / liveness back either.
		 */
		Discard,
		/** Matched direct reply: OutNext replaces the endpoint state. */
		Accept,
	};

	/**
	 * The pending lease request (extended PING) this reply answers, or nullptr.
	 * Extended PINGs are unicast, so a reply from any other IPv4 address does not
	 * match (stream-session.md "[1.x] How SDKs send extended PINGs").
	 */
	inline const FHapbeatPendingPing* FindMatchedLeaseRequest(
		const FHapbeatProtocol::FPongInfo& Pong,
		uint16 PongSeq,
		const TMap<uint16, FHapbeatPendingPing>& PendingPings,
		const FString& SenderIp)
	{
		const FHapbeatPendingPing* Pending = PendingPings.Find(PongSeq);
		if (Pending == nullptr || !Pending->bLeaseRequest || !Pending->Matches(Pong.Timestamp, SenderIp))
		{
			return nullptr;
		}
		return Pending;
	}

	/**
	 * Pure per-endpoint classification (stream-session.md "Discovery lease",
	 * "Receiver classification" and "Incarnation and takeover"). Correlation
	 * rules mirror Unity HapbeatClient.EvaluateStreamLeasePong for HBS2 replies;
	 * the same correlation (seq -> pending lease request whose timestamp equals
	 * the echoed one, sent to this IPv4 address by the current incarnation, newer
	 * than the last accepted request for this endpoint) gates the Stream Gen 1
	 * decision for replies without a tail.
	 *
	 * @param Existing  Current state of the endpoint the PONG will register into,
	 *                  or nullptr if this PONG creates a new endpoint.
	 */
	inline EPongLeaseDecision EvaluatePongLease(
		const FHapbeatProtocol::FPongInfo& Pong,
		uint16 PongSeq,
		const TMap<uint16, FHapbeatPendingPing>& PendingPings,
		uint64 CurrentIncarnation,
		const FHapbeatStreamEndpointLeaseState* Existing,
		FHapbeatStreamEndpointLeaseState& OutNext,
		const FString& SenderIp)
	{
		const FHapbeatPendingPing* Pending = FindMatchedLeaseRequest(Pong, PongSeq, PendingPings, SenderIp);
		const bool bCurrentRequest = CurrentIncarnation != 0 && Pending != nullptr
			&& Pending->ClientIncarnation == CurrentIncarnation;
		const bool bNewerThanAccepted = Existing == nullptr || Pong.Timestamp > Existing->LastAcceptedTimestampUs;

		if (Pong.LeaseTail == EHapbeatPongLeaseTail::Ignored)
		{
			// A tail of a later major (or a cut one): no class or lease change. A
			// matched one is worth one warning, an unmatched one is just noise.
			return bCurrentRequest && bNewerThanAccepted
				? EPongLeaseDecision::IgnoredMatched
				: EPongLeaseDecision::NoChange;
		}

		if (Pong.LeaseTail == EHapbeatPongLeaseTail::Valid)
		{
			// HBS2 is only valid on a direct reply to the active incarnation.
			if (CurrentIncarnation == 0 || Pong.EchoedClientIncarnation != CurrentIncarnation)
			{
				return EPongLeaseDecision::Discard;
			}
			if (Pending == nullptr || Pending->ClientIncarnation != Pong.EchoedClientIncarnation)
			{
				return EPongLeaseDecision::Discard;
			}
			if (!bNewerThanAccepted)
			{
				// Structurally valid but older than an accepted reply: must never
				// roll the endpoint back to an older boot / ticket.
				return EPongLeaseDecision::Discard;
			}

			// A superseded writer must not turn a later periodic reply into an
			// implicit takeover. A fresh incarnation (Connect or
			// ReacquireStreamOwnership, incl. foreground resume) clears the latch,
			// and so does a reply with lease_valid, !superseded and lease_reissued
			// [1.x]: the device re-issued this key a ticket because the other
			// writer left. A new ticket without lease_reissued (this SDK's own
			// entry expired) keeps it.
			const bool bReissued = Pong.bLeaseValid && !Pong.bLeaseSuperseded && Pong.bLeaseReissued;
			const bool bKeepSuperseded = Existing != nullptr
				&& Existing->Class == EHapbeatStreamEndpointClass::V2
				&& Existing->bLeaseSuperseded
				&& Existing->ClientIncarnation == Pending->ClientIncarnation
				&& !bReissued;

			OutNext = FHapbeatStreamEndpointLeaseState();
			OutNext.Class = EHapbeatStreamEndpointClass::V2;
			OutNext.ClientIncarnation = Pending->ClientIncarnation;
			OutNext.LastAcceptedTimestampUs = Pong.Timestamp;
			if (bKeepSuperseded)
			{
				OutNext.Lease = Existing->Lease;
				OutNext.bLeaseValid = false;
				OutNext.bLeaseSuperseded = true;
			}
			else
			{
				OutNext.Lease = Pong.Lease;
				OutNext.bLeaseValid = Pong.bLeaseValid;
				OutNext.bLeaseSuperseded = Pong.bLeaseSuperseded;
			}
			return EPongLeaseDecision::Accept;
		}

		// Nothing after volume_wiper. Firmware before 0.5.0 cannot echo the
		// incarnation, so the request itself must be a lease request of the
		// current incarnation: replies to 8-byte discovery PINGs never carry a
		// tail on any firmware and must not classify (they are NoChange).
		if (!bCurrentRequest || !bNewerThanAccepted)
		{
			return EPongLeaseDecision::NoChange;
		}

		OutNext = FHapbeatStreamEndpointLeaseState();
		OutNext.Class = EHapbeatStreamEndpointClass::Legacy;
		OutNext.ClientIncarnation = Pending->ClientIncarnation;
		OutNext.LastAcceptedTimestampUs = Pong.Timestamp;
		return EPongLeaseDecision::Accept;
	}
}
