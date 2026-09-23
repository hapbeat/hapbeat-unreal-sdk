// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatProtocol.h"
#include "HapbeatStreamLease.h"

namespace HapbeatStreamSessionContract
{
	inline bool IsMigrationCandidate(
		const FString& ExistingIp,
		int32 ExistingPort,
		const FString& ExistingAddress,
		const FString& NewIp,
		int32 NewPort,
		const FString& NewAddress,
		bool bExistingAlive)
	{
		// A live exact endpoint is never collapsed merely because one tuple field
		// matches. Once its liveness expires, a stable route or stable address can
		// identify one unambiguous migration candidate.
		return !bExistingAlive
			&& ((ExistingIp == NewIp && ExistingPort == NewPort)
				|| ExistingAddress == NewAddress);
	}

	/** What a received PONG may do to its endpoint's stream class / lease. */
	enum class EPongLeaseDecision : uint8
	{
		/**
		 * Ordinary discovery only: liveness / address / RTT update, class and
		 * lease stay as they are. Unsolicited (timestamp 0), unmatched, late and
		 * malformed-HBS2 replies land here.
		 */
		NoChange,
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
	 * Pure per-endpoint classification (stream-session-v2.md "Discovery lease" +
	 * "Legacy receiver fallback (SDK)"). Correlation rules mirror Unity
	 * HapbeatClient.EvaluateStreamLeasePong exactly for HBS2 replies; the same
	 * correlation (seq -> pending request whose timestamp equals the echoed one,
	 * issued by the current incarnation, newer than the last accepted request for
	 * this endpoint) gates the legacy decision for replies without a tail.
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
		FHapbeatStreamEndpointLeaseState& OutNext)
	{
		if (Pong.LeaseTail == EHapbeatPongLeaseTail::Malformed)
		{
			// HBS2 marker with a wrong length / version / reserved / flags: not a
			// lease reply and not proof of old firmware. No class or lease change.
			return EPongLeaseDecision::NoChange;
		}

		const FHapbeatPendingPing* Pending = PendingPings.Find(PongSeq);

		if (Pong.LeaseTail == EHapbeatPongLeaseTail::Valid)
		{
			// HBS2 is only valid on a direct reply to the active incarnation.
			if (CurrentIncarnation == 0 || Pong.EchoedClientIncarnation != CurrentIncarnation)
			{
				return EPongLeaseDecision::Discard;
			}
			if (Pong.Timestamp == 0 || Pending == nullptr
				|| Pending->TimestampUs != Pong.Timestamp
				|| Pending->ClientIncarnation != Pong.EchoedClientIncarnation)
			{
				return EPongLeaseDecision::Discard;
			}
			if (Existing != nullptr && Pong.Timestamp <= Existing->LastAcceptedTimestampUs)
			{
				// Structurally valid but older than an accepted reply: must never
				// roll the endpoint back to an older boot / ticket.
				return EPongLeaseDecision::Discard;
			}

			// A superseded writer must not turn a later periodic discovery reply
			// into an implicit takeover. Only a fresh incarnation (Connect) clears
			// the latch.
			const bool bKeepSuperseded = Existing != nullptr
				&& Existing->Class == EHapbeatStreamEndpointClass::V2
				&& Existing->bLeaseSuperseded
				&& Existing->ClientIncarnation == Pending->ClientIncarnation;

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

		// No HBS2 marker. Pre-v2 firmware cannot echo the incarnation, so the
		// request itself must belong to the current incarnation.
		if (Pong.Timestamp == 0)
		{
			return EPongLeaseDecision::NoChange; // unsolicited identity PONG
		}
		if (CurrentIncarnation == 0 || Pending == nullptr
			|| Pending->TimestampUs != Pong.Timestamp
			|| Pending->ClientIncarnation != CurrentIncarnation)
		{
			return EPongLeaseDecision::NoChange; // reply to some other request
		}
		if (Existing != nullptr && Pong.Timestamp <= Existing->LastAcceptedTimestampUs)
		{
			return EPongLeaseDecision::NoChange; // late reply
		}

		OutNext = FHapbeatStreamEndpointLeaseState();
		OutNext.Class = EHapbeatStreamEndpointClass::Legacy;
		OutNext.ClientIncarnation = Pending->ClientIncarnation;
		OutNext.LastAcceptedTimestampUs = Pong.Timestamp;
		return EPongLeaseDecision::Accept;
	}
}
