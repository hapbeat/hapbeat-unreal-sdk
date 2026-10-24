// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatProtocol.h"

/**
 * Per-endpoint stream receiver class (stream-session.md "Receiver
 * classification", DEC-075). Decided ONLY from matched direct replies to this
 * SDK's own extended PING; see HapbeatStreamSessionContract::EvaluatePongLease.
 * The value names predate the Stream Gen 1 / Stream Gen 2 naming and are kept
 * internally; the public API maps them to EHapbeatStreamClass.
 */
enum class EHapbeatStreamEndpointClass : uint8
{
	/** No matched reply yet: streams for this endpoint defer. */
	Unknown,
	/** Matched reply carried a Stream Gen 2 (HBS2) tail: Stream Gen 2 wire format under its lease. */
	V2,
	/** Matched reply carried nothing after volume_wiper: Stream Gen 1 wire format + 300 ms guard. */
	Legacy,
};

/** Stream eligibility state kept for one PONG-confirmed endpoint. */
struct FHapbeatStreamEndpointLeaseState
{
	EHapbeatStreamEndpointClass Class = EHapbeatStreamEndpointClass::Unknown;
	/** Stream Gen 2 only: the lease the device issued for ClientIncarnation. */
	FHapbeatStreamLeaseIdentity Lease;
	bool bLeaseValid = false;
	bool bLeaseSuperseded = false;
	/** Incarnation of the PING whose matched reply produced this state. */
	uint64 ClientIncarnation = 0;
	/**
	 * Echoed timestamp of the last ACCEPTED matched reply for this endpoint,
	 * across both classes. A reply to an older-or-equal request never changes
	 * the class or lease again (late PONGs cannot roll the endpoint back).
	 */
	int64 LastAcceptedTimestampUs = 0;

	/** The lease may carry Stream Gen 2 BEGIN/DATA/END for the current incarnation. */
	bool IsUsableV2Lease(uint64 CurrentIncarnation) const
	{
		return Class == EHapbeatStreamEndpointClass::V2 && bLeaseValid && !bLeaseSuperseded
			&& ClientIncarnation == CurrentIncarnation && Lease.IsValid();
	}

	/** Sources may be assigned to this endpoint (Stream Gen 1, or a usable Stream Gen 2 lease). Unknown / invalid / superseded defer. */
	bool IsStreamable(uint64 CurrentIncarnation) const
	{
		return Class == EHapbeatStreamEndpointClass::Legacy || IsUsableV2Lease(CurrentIncarnation);
	}

	bool operator==(const FHapbeatStreamEndpointLeaseState& Other) const
	{
		return Class == Other.Class && Lease == Other.Lease && bLeaseValid == Other.bLeaseValid
			&& bLeaseSuperseded == Other.bLeaseSuperseded && ClientIncarnation == Other.ClientIncarnation;
	}
	bool operator!=(const FHapbeatStreamEndpointLeaseState& Other) const { return !(*this == Other); }
};

/**
 * One outstanding PING. Kept until aged out so every device answering a
 * broadcast discovery PING can still be matched for RTT.
 */
struct FHapbeatPendingPing
{
	/** Local monotonic send time (µs), for RTT. */
	int64 SentMonotonicUs = 0;
	/** Timestamp written on the wire; a matched PONG echoes it exactly. */
	int64 TimestampUs = 0;
	/** Client incarnation written on the wire (extended PING) or current when sent (8-byte PING). */
	uint64 ClientIncarnation = 0;
	/**
	 * True for an extended (16-byte) PING, which asks for a lease. Only replies
	 * to these classify an endpoint: every firmware answers an 8-byte discovery
	 * PING without a tail, which must not read as Stream Gen 1.
	 */
	bool bLeaseRequest = false;
	/**
	 * Destination of a unicast PING (extended PINGs are always unicast). A reply
	 * from another IPv4 address is not matched against it. Empty for a broadcast.
	 */
	FString TargetIp;

	/** The reply from SenderIp echoing Timestamp answers this request. */
	bool Matches(int64 EchoedTimestampUs, const FString& SenderIp) const
	{
		return EchoedTimestampUs != 0 && TimestampUs == EchoedTimestampUs
			&& (TargetIp.IsEmpty() || TargetIp == SenderIp);
	}
};
