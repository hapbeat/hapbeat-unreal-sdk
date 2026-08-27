// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"

namespace HapbeatStreamSessionContract
{
	inline bool IsMigrationCandidate(
		const FString& ExistingIp,
		const FString& ExistingAddress,
		const FString& NewIp,
		const FString& NewAddress)
	{
		return ExistingIp == NewIp || ExistingAddress == NewAddress;
	}

	inline int32 MigrationPriority(const FString& ExistingAddress, const FString& NewAddress)
	{
		// A reported device address is the stronger identity signal; fall back to
		// the stable IP when the device address itself changed.
		return ExistingAddress == NewAddress ? 2 : 1;
	}

	inline bool ShouldSendEnd(bool bAbandonRequested)
	{
		return !bAbandonRequested;
	}
}
