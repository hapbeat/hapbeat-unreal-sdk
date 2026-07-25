// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatStreamPlayback.h"

void UHapbeatStreamPlayback::Init(float Baseline, float InitialModulator)
{
	BaselineGain = Baseline;
	Gain = FMath::Clamp(Baseline * InitialModulator, 0.0f, 2.0f);
	Pan = 0.0f;
	bStopped = false;

	// GetMirror() lazily creates on first call — always fine here since Init()
	// is only ever called from the game thread (StreamClip), same as every
	// other mutator below.
	TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> M = GetMirror();
	M->Gain.store(Gain, std::memory_order_relaxed);
	M->Pan.store(Pan, std::memory_order_relaxed);
	M->bStopped.store(false, std::memory_order_relaxed);
}

void UHapbeatStreamPlayback::ApplyGainModulation(float Modulator)
{
	// Single source of truth for the gain formula (imperative + declarative
	// callers both route here). Result clamped to the [0, 2] boost ceiling.
	Gain = FMath::Clamp(BaselineGain * Modulator, 0.0f, 2.0f);
	GetMirror()->Gain.store(Gain, std::memory_order_relaxed);
}

void UHapbeatStreamPlayback::SetPan(float NewPan)
{
	Pan = FMath::Clamp(NewPan, -1.0f, 1.0f);
	GetMirror()->Pan.store(Pan, std::memory_order_relaxed);
}

void UHapbeatStreamPlayback::Stop()
{
	bStopped = true;
	GetMirror()->bStopped.store(true, std::memory_order_relaxed);
}

void UHapbeatStreamPlayback::GetStereoChannelGains(float& OutL, float& OutR) const
{
	// Linear balance, NOT equal-power (see header). Center = passthrough.
	// (The stream thread computes this SAME formula independently from the
	// atomic Pan mirror — see FHapbeatStreamer::Tick — rather than calling
	// this const method, to avoid ever touching this UObject off the game
	// thread. Keep both formulas identical if this one ever changes.)
	OutL = Pan <= 0.0f ? 1.0f : 1.0f - Pan;
	OutR = Pan >= 0.0f ? 1.0f : 1.0f + Pan;
}

TSharedRef<FHapbeatStreamGainMirror, ESPMode::ThreadSafe> UHapbeatStreamPlayback::GetMirror()
{
	if (!Mirror.IsValid())
	{
		Mirror = MakeShared<FHapbeatStreamGainMirror, ESPMode::ThreadSafe>();
	}
	return Mirror.ToSharedRef();
}
