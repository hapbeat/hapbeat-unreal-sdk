// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

/**
 * Editor-only hard stop used by unattended Showcase verification.
 *
 * -HapbeatNoNetwork keeps the full subsystem / mixer execution path alive but
 * drops every Hapbeat datagram before FSocket::SendTo. Packaged applications
 * compile this branch out, so the flag cannot disable a shipped experience.
 */
inline bool HapbeatIsNetworkSuppressedForEditor()
{
#if WITH_EDITOR
	static const bool bSuppressed =
		FParse::Param(FCommandLine::Get(), TEXT("HapbeatNoNetwork"));
	return bSuppressed;
#else
	return false;
#endif
}
