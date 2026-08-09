// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"

class FInternetAddr;

/**
 * Local IPv4 subnet discovery, and the broadcast destinations derived from it.
 *
 * Why this exists
 * ---------------
 * `255.255.255.255` (limited broadcast) leaves a multi-homed host through the
 * SINGLE interface with the lowest routing metric. Installing Hyper-V, WSL2 or
 * Docker adds a virtual switch that is permanently "connected" -- no Ethernet
 * cable required -- and Windows may well rank it above Wi-Fi. When that happens
 * the packet never reaches the network the Hapbeat is on and the SDK discovers
 * nothing at all. A subnet-directed address (`192.168.0.255`) is instead
 * resolved through the directly-connected route for that subnet, so the metric
 * never enters into it. See DEC-054.
 *
 * Computing that address needs each interface's real netmask, and UE's public
 * socket API does not expose one (`ISocketSubsystem::GetLocalAdapterAddresses`
 * returns addresses only). So this is the one place in the plugin that talks to
 * the platform directly: `GetAdaptersAddresses` on Windows, `getifaddrs` on
 * Linux/macOS. Everything is best-effort -- a platform this cannot query
 * returns an empty array and callers fall back to the limited broadcast they
 * have always used.
 *
 * All platform-specific code in the plugin lives in the matching .cpp, so a
 * porting problem is contained to one file.
 */

/** One local IPv4 address with its prefix length, in host byte order. */
struct FHapbeatLocalSubnet
{
	uint32 Address = 0;
	uint8 PrefixLength = 0;
};

/**
 * One address the SDK can broadcast to.
 *
 * Discovery (PING / CONNECT_STATUS) fans out across every route; playback
 * (PLAY / STOP / STOP_ALL / STREAM_*) uses exactly one. That split is not
 * cosmetic: firmware older than v0.3.0 does not de-duplicate PLAY/STOP by
 * sequence number, so a device reachable on two routes would fire twice.
 */
struct FHapbeatBroadcastRoute
{
	/** Ready-to-send endpoint (broadcast address + port). */
	TSharedPtr<FInternetAddr> EndPoint;
	/** Host byte order, already masked. Both zero for the limited-broadcast catch-all. */
	uint32 Network = 0;
	uint32 Mask = 0;
	/** True for 255.255.255.255, which matches no subnet and must never win the lock. */
	bool bLimited = false;

	/** Whether `Ip` (host byte order) sits on this route's subnet. */
	bool Contains(uint32 Ip) const
	{
		return !bLimited && Mask != 0 && (Ip & Mask) == Network;
	}
};

/**
 * Every local IPv4 address with its prefix length, loopback excluded.
 * Empty on any platform or failure this cannot handle.
 */
HAPBEATSDK_API TArray<FHapbeatLocalSubnet> HapbeatGetLocalIPv4Subnets();

/**
 * One destination per local IPv4 subnet, plus the limited broadcast.
 *
 * The address comes from each interface's real mask rather than being assumed
 * to end in .255: a /16 broadcasts to x.y.255.255 and a /25 to x.y.z.127, and
 * the subnet itself is whatever the router hands out. Deduplicated by address
 * -- two interfaces on one subnet (a laptop docked over Ethernet while Wi-Fi is
 * still up) would otherwise deliver every packet twice.
 *
 * The limited broadcast is always appended as a catch-all, so SoftAP setups and
 * platforms whose interfaces cannot be enumerated behave exactly as before.
 */
HAPBEATSDK_API TArray<FHapbeatBroadcastRoute> HapbeatEnumerateBroadcastRoutes(int32 Port);

/** Parse a dotted-quad into host byte order. Returns false if it is not IPv4. */
HAPBEATSDK_API bool HapbeatParseIPv4(const FString& Dotted, uint32& OutValue);

/** Dotted-quad for a host-byte-order address. */
HAPBEATSDK_API FString HapbeatIPv4ToString(uint32 Value);
