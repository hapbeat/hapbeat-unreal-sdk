// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"

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
}
