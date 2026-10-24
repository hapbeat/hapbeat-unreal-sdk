// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatStreamEndpointInfo.generated.h"

/**
 * Which stream format a device endpoint takes (stream-session.md "Receiver
 * classification"). Decided only from a matched reply to this SDK's own lease
 * request, never from CAPS.
 */
UENUM(BlueprintType)
enum class EHapbeatStreamClass : uint8
{
	/** No matched reply yet: streams for this device wait (Deferred, UnknownClass). */
	Unknown,
	/** Firmware before 0.5.0: Stream Gen 1, with a 300 ms wait between END and the next BEGIN. */
	Gen1,
	/** Stream Gen 2 (session stream under a device-issued lease). */
	Gen2,
};

/** State of this SDK's stream lease on a Stream Gen 2 device (sdk-multi-stream.md §3). */
UENUM(BlueprintType)
enum class EHapbeatStreamLeaseState : uint8
{
	/** Not a Stream Gen 2 device (Gen1 or Unknown). */
	None,
	/** The device issued this SDK a lease it can stream under. */
	Valid,
	/** No usable lease (not issued, expired, or a node that does not receive streams). */
	Unavailable,
	/**
	 * Another application streamed to the device after this one. Streams wait
	 * until the device re-issues a lease or ReacquireStreamOwnership is called.
	 */
	Superseded,
};

/** One device endpoint as the stream side of the SDK sees it (sdk-multi-stream.md §3 StreamEndpointInfo). */
USTRUCT(BlueprintType)
struct HAPBEATSDK_API FHapbeatStreamEndpointInfo
{
	GENERATED_BODY()

	/** Full device address the device reported in its PONG. */
	UPROPERTY(BlueprintReadOnly, Category = "Hapbeat")
	FString Address;

	UPROPERTY(BlueprintReadOnly, Category = "Hapbeat")
	FString Ip;

	UPROPERTY(BlueprintReadOnly, Category = "Hapbeat")
	int32 Port = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Hapbeat")
	EHapbeatStreamClass StreamClass = EHapbeatStreamClass::Unknown;

	/** Gen2 only; None for Gen1 and Unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "Hapbeat")
	EHapbeatStreamLeaseState LeaseState = EHapbeatStreamLeaseState::None;

	/**
	 * Firmware major from the device's capability report (CAPS): -1 = not known
	 * yet, 0 = the device reports none (firmware 0.x).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Hapbeat")
	int32 CapsGeneration = -1;
};
