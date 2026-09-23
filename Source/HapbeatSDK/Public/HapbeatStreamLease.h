// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatProtocol.h"

/**
 * Per-endpoint stream receiver class (stream-session-v2.md "Legacy receiver
 * fallback (SDK)", DEC-075). Decided ONLY from matched direct replies to this
 * SDK's own extended PING; see HapbeatStreamSessionContract::EvaluatePongLease.
 */
enum class EHapbeatStreamEndpointClass : uint8
{
	/** No matched reply yet: streams for this endpoint defer. */
	Unknown,
	/** Matched reply carried a valid HBS2 tail: v2 wire format under its lease. */
	V2,
	/** Matched reply carried no HBS2 marker: pre-v2 firmware, legacy wire format + 300 ms guard. */
	Legacy,
};

/** Stream eligibility state kept for one PONG-confirmed endpoint. */
struct FHapbeatStreamEndpointLeaseState
{
	EHapbeatStreamEndpointClass Class = EHapbeatStreamEndpointClass::Unknown;
	/** V2 only: the lease the device issued for ClientIncarnation. */
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

	/** The lease may carry v2 BEGIN/DATA/END for the current incarnation. */
	bool IsUsableV2Lease(uint64 CurrentIncarnation) const
	{
		return Class == EHapbeatStreamEndpointClass::V2 && bLeaseValid && !bLeaseSuperseded
			&& ClientIncarnation == CurrentIncarnation && Lease.IsValid();
	}

	/** Sources may be assigned to this endpoint (Legacy, or a usable v2 lease). Unknown / invalid / superseded defer. */
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

/** One outstanding PING. Kept until aged out so every device answering a broadcast PING can correlate. */
struct FHapbeatPendingPing
{
	/** Local monotonic send time (µs), for RTT. */
	int64 SentMonotonicUs = 0;
	/** Timestamp written on the wire; a matched PONG echoes it exactly. */
	int64 TimestampUs = 0;
	/** Client incarnation written on the wire. */
	uint64 ClientIncarnation = 0;
};
