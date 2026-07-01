// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatStreamPlayback.h"

void UHapbeatStreamPlayback::Init(float Baseline, float InitialModulator)
{
	BaselineGain = Baseline;
	Gain = FMath::Clamp(Baseline * InitialModulator, 0.0f, 2.0f);
	Pan = 0.0f;
	bStopped = false;
}

void UHapbeatStreamPlayback::ApplyGainModulation(float Modulator)
{
	// Single source of truth for the gain formula (imperative + declarative
	// callers both route here). Result clamped to the [0, 2] boost ceiling.
	Gain = FMath::Clamp(BaselineGain * Modulator, 0.0f, 2.0f);
}

void UHapbeatStreamPlayback::SetPan(float NewPan)
{
	Pan = FMath::Clamp(NewPan, -1.0f, 1.0f);
}

void UHapbeatStreamPlayback::Stop()
{
	bStopped = true;
}

void UHapbeatStreamPlayback::GetStereoChannelGains(float& OutL, float& OutR) const
{
	// Linear balance, NOT equal-power (see header). Center = passthrough.
	OutL = Pan <= 0.0f ? 1.0f : 1.0f - Pan;
	OutR = Pan >= 0.0f ? 1.0f : 1.0f + Pan;
}
