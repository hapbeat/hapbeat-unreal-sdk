// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatConfig.h"

#include "HapbeatProtocol.h" // FHapbeatProtocol::MaxAppNameLen (single source for the 16-char cap)

#if WITH_EDITOR
void UHapbeatConfig::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Enforce the device OLED grid width on AppName (UE string properties have
	// no built-in length cap). Left() is safe for over-length and short strings.
	if (AppName.Len() > FHapbeatProtocol::MaxAppNameLen)
	{
		AppName = AppName.Left(FHapbeatProtocol::MaxAppNameLen);
	}
}
#endif
