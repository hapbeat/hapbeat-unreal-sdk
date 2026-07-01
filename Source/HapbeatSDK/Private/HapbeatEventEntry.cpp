// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEventEntry.h"

// Note: HapbeatClip.h is intentionally NOT included here. The StreamClip member
// is a TSoftObjectPtr<UHapbeatClip>, which needs only the forward declaration in
// the header; this translation unit never dereferences a UHapbeatClip.

TArray<FString> FHapbeatEventEntry::StandardPositions()
{
	// Mirrors HapbeatEventEntry.StandardPositions (Unity SDK) and the
	// device-addressing spec §3 Position vocabulary, in the same order.
	return TArray<FString>{
		TEXT("pos_neck"), TEXT("pos_chest"), TEXT("pos_abd"),
		TEXT("pos_l_arm"), TEXT("pos_r_arm"), TEXT("pos_l_wrist"), TEXT("pos_r_wrist"),
		TEXT("pos_hip"), TEXT("pos_l_thigh"), TEXT("pos_r_thigh"), TEXT("pos_l_ankle"), TEXT("pos_r_ankle")
	};
}
