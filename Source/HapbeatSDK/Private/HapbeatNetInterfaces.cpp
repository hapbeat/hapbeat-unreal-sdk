// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatNetInterfaces.h"

#include "IPAddress.h"
#include "SocketSubsystem.h"

#if PLATFORM_WINDOWS
	// iphlpapi's GetAdaptersAddresses is the only documented way to get a
	// per-address prefix length on Windows. winsock2.h must precede iphlpapi.h.
	// Requires "iphlpapi.lib" (added in HapbeatSDK.Build.cs for Win64).
	#include "Windows/AllowWindowsPlatformTypes.h"
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <iphlpapi.h>
	#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_LINUX || PLATFORM_MAC
	#include <ifaddrs.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatNet, Log, All);

bool HapbeatParseIPv4(const FString& Dotted, uint32& OutValue)
{
	TArray<FString> Parts;
	Dotted.ParseIntoArray(Parts, TEXT("."), /*InCullEmpty=*/false);
	if (Parts.Num() != 4)
	{
		return false;
	}
	uint32 Value = 0;
	for (const FString& Part : Parts)
	{
		if (Part.IsEmpty() || !Part.IsNumeric())
		{
			return false;
		}
		const int32 Byte = FCString::Atoi(*Part);
		if (Byte < 0 || Byte > 255)
		{
			return false;
		}
		Value = (Value << 8) | static_cast<uint32>(Byte);
	}
	OutValue = Value;
	return true;
}

FString HapbeatIPv4ToString(uint32 Value)
{
	return FString::Printf(TEXT("%u.%u.%u.%u"),
		(Value >> 24) & 0xFF, (Value >> 16) & 0xFF, (Value >> 8) & 0xFF, Value & 0xFF);
}

#if PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC
namespace
{
	/** Drop loopback here rather than in each platform backend. */
	bool IsUsableIPv4(uint32 Address, uint8 PrefixLength)
	{
		if (PrefixLength == 0 || PrefixLength > 32)
		{
			return false;
		}
		return ((Address >> 24) & 0xFF) != 127;
	}

#if PLATFORM_LINUX || PLATFORM_MAC
	/**
	 * Prefix length for a dotted mask in host byte order. -1 if the mask is not
	 * contiguous -- not something a modern stack hands out, but accepting one
	 * would produce a broadcast address that reaches the wrong hosts.
	 */
	int32 PrefixFromMask(uint32 Mask)
	{
		if (Mask == 0)
		{
			return -1;
		}
		const uint32 Inverted = ~Mask;
		if ((Inverted & (Inverted + 1)) != 0)
		{
			return -1;
		}
		int32 Prefix = 0;
		while (Prefix < 32 && ((Mask >> (31 - Prefix)) & 1u) != 0)
		{
			++Prefix;
		}
		return Prefix;
	}
#endif // PLATFORM_LINUX || PLATFORM_MAC
}
#endif // PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_MAC

TArray<FHapbeatLocalSubnet> HapbeatGetLocalIPv4Subnets()
{
	TArray<FHapbeatLocalSubnet> Out;

#if PLATFORM_WINDOWS
	// Documented protocol: the call reports the size it needs on overflow.
	// Bounded so a pathological answer cannot spin here.
	ULONG BufferSize = 15 * 1024;
	TArray<uint8> Buffer;
	ULONG Result = ERROR_BUFFER_OVERFLOW;
	for (int32 Attempt = 0; Attempt < 5; ++Attempt)
	{
		Buffer.SetNumUninitialized(static_cast<int32>(BufferSize));
		Result = GetAdaptersAddresses(
			AF_INET,
			/*Flags=*/0,
			nullptr,
			reinterpret_cast<IP_ADAPTER_ADDRESSES*>(Buffer.GetData()),
			&BufferSize);
		if (Result != ERROR_BUFFER_OVERFLOW)
		{
			break;
		}
	}
	if (Result != NO_ERROR)
	{
		UE_LOG(LogHapbeatNet, Verbose,
			TEXT("GetAdaptersAddresses failed (%u); limited broadcast only."), static_cast<uint32>(Result));
		return Out;
	}

	// Adapters with no configured IPv4 address are simply absent from an
	// AF_INET enumeration, so no operational-status filtering is needed here.
	for (const IP_ADAPTER_ADDRESSES* Adapter = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(Buffer.GetData());
		 Adapter != nullptr; Adapter = Adapter->Next)
	{
		for (const IP_ADAPTER_UNICAST_ADDRESS* Unicast = Adapter->FirstUnicastAddress;
			 Unicast != nullptr; Unicast = Unicast->Next)
		{
			const sockaddr* Addr = Unicast->Address.lpSockaddr;
			if (Addr == nullptr || Addr->sa_family != AF_INET)
			{
				continue;
			}
			const sockaddr_in* In = reinterpret_cast<const sockaddr_in*>(Addr);
			// s_addr is network byte order; ntohl gives us host order.
			const uint32 Value = static_cast<uint32>(ntohl(In->sin_addr.s_addr));
			const uint8 Prefix = static_cast<uint8>(Unicast->OnLinkPrefixLength);
			if (IsUsableIPv4(Value, Prefix))
			{
				Out.Add(FHapbeatLocalSubnet{ Value, Prefix });
			}
		}
	}

#elif PLATFORM_LINUX || PLATFORM_MAC
	ifaddrs* Head = nullptr;
	if (getifaddrs(&Head) != 0 || Head == nullptr)
	{
		UE_LOG(LogHapbeatNet, Verbose, TEXT("getifaddrs failed; limited broadcast only."));
		return Out;
	}
	for (const ifaddrs* Node = Head; Node != nullptr; Node = Node->ifa_next)
	{
		// getifaddrs also reports link-layer and IPv6 entries.
		if (Node->ifa_addr == nullptr || Node->ifa_addr->sa_family != AF_INET
			|| Node->ifa_netmask == nullptr)
		{
			continue;
		}
		const uint32 Value = static_cast<uint32>(
			ntohl(reinterpret_cast<const sockaddr_in*>(Node->ifa_addr)->sin_addr.s_addr));
		const uint32 Mask = static_cast<uint32>(
			ntohl(reinterpret_cast<const sockaddr_in*>(Node->ifa_netmask)->sin_addr.s_addr));
		const int32 Prefix = PrefixFromMask(Mask);
		if (Prefix > 0 && IsUsableIPv4(Value, static_cast<uint8>(Prefix)))
		{
			Out.Add(FHapbeatLocalSubnet{ Value, static_cast<uint8>(Prefix) });
		}
	}
	freeifaddrs(Head);

#else
	// Consoles / mobile / anything else: no enumeration. The limited broadcast
	// appended by HapbeatEnumerateBroadcastRoutes still covers these, which is
	// the behaviour that predates this file.
#endif

	return Out;
}

TArray<FHapbeatBroadcastRoute> HapbeatEnumerateBroadcastRoutes(int32 Port)
{
	TArray<FHapbeatBroadcastRoute> Routes;

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		return Routes;
	}

	TSet<uint32> Seen;
	for (const FHapbeatLocalSubnet& Subnet : HapbeatGetLocalIPv4Subnets())
	{
		// (ip & mask) | ~mask -- NOT an assumed .255.
		const uint32 Mask = (Subnet.PrefixLength == 0)
			? 0u
			: (0xFFFFFFFFu << (32 - Subnet.PrefixLength));
		const uint32 Network = Subnet.Address & Mask;
		const uint32 Broadcast = Network | (~Mask);
		if (Seen.Contains(Broadcast))
		{
			continue;
		}
		Seen.Add(Broadcast);

		TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
		bool bValid = false;
		Addr->SetIp(*HapbeatIPv4ToString(Broadcast), bValid);
		if (!bValid)
		{
			continue;
		}
		Addr->SetPort(Port);

		FHapbeatBroadcastRoute Route;
		Route.EndPoint = Addr;
		Route.Network = Network;
		Route.Mask = Mask;
		Route.bLimited = false;
		Routes.Add(Route);
	}

	// Always keep the original behaviour available. On a single-NIC host this is
	// effectively the only route, so nothing changes there.
	TSharedRef<FInternetAddr> Limited = SocketSubsystem->CreateInternetAddr();
	bool bLimitedValid = false;
	Limited->SetIp(TEXT("255.255.255.255"), bLimitedValid);
	if (bLimitedValid)
	{
		Limited->SetPort(Port);
		FHapbeatBroadcastRoute Route;
		Route.EndPoint = Limited;
		Route.bLimited = true;
		Routes.Add(Route);
	}

	return Routes;
}
