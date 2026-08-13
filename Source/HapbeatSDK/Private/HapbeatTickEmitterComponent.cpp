// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatTickEmitterComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatTick, Log, All);

namespace
{
	/**
	 * Ticks emitted per call are capped so a wired handler that feeds back into
	 * its own input source cannot spin here. 64 is Unity's number; anything that
	 * legitimately needs more is a threshold far too small for its input range.
	 */
	constexpr int32 MaxTicksPerCall = 64;
}

UHapbeatTickEmitterComponent::UHapbeatTickEmitterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHapbeatTickEmitterComponent::FireFromValue(float Value)
{
	Process(Value);
}

void UHapbeatTickEmitterComponent::FireFromVector2D(FVector2D Value)
{
	switch (Axis)
	{
	case EHapbeatTickAxis::X:         Process(static_cast<float>(Value.X)); break;
	case EHapbeatTickAxis::Y:         Process(static_cast<float>(Value.Y)); break;
	case EHapbeatTickAxis::Magnitude: Process(static_cast<float>(Value.Size())); break;
	}
}

void UHapbeatTickEmitterComponent::FireNow()
{
	Fire();
}

void UHapbeatTickEmitterComponent::ResetReference()
{
	bHasReference = false;
}

void UHapbeatTickEmitterComponent::OnUnregister()
{
	// Drop the anchor with the component, so a re-register starts from the new
	// value instead of ticking for the gap accumulated while it was gone.
	bHasReference = false;
	Super::OnUnregister();
}

void UHapbeatTickEmitterComponent::Process(float Value)
{
	if (!bTriggerEnabled)
	{
		return;
	}

	if (!bHasReference)
	{
		LastValue = Value;
		bHasReference = true;
		if (bEmitOnInitialValue)
		{
			Fire();
		}
		return;
	}

	// Threshold 0 means "fire on any change" -- same early-out for both modes.
	if (TickThreshold <= 0.0f)
	{
		if (!FMath::IsNearlyEqual(Value, LastValue))
		{
			LastValue = Value;
			Fire();
		}
		return;
	}

	int32 TicksToFire = 0;
	if (TickMode == EHapbeatTickMode::AbsolutePosition)
	{
		// Bucket both positions by the threshold; the index difference is how
		// many fixed marks were crossed. Anchored at zero, so the marks do not
		// depend on the control's starting value.
		const int32 MarksOld = FMath::FloorToInt(LastValue / TickThreshold);
		const int32 MarksNew = FMath::FloorToInt(Value / TickThreshold);
		TicksToFire = FMath::Abs(MarksNew - MarksOld);
		LastValue = Value;
	}
	else // AccumulatedMotion
	{
		// Walk the anchor toward the new value one threshold at a time; each
		// step is a tick, and the anchor keeps whatever remainder is left over.
		float Delta = Value - LastValue;
		while (FMath::Abs(Delta) >= TickThreshold)
		{
			++TicksToFire;
			LastValue += FMath::Sign(Delta) * TickThreshold;
			Delta = Value - LastValue;
			if (TicksToFire >= MaxTicksPerCall)
			{
				break;
			}
		}
	}

	if (TicksToFire > MaxTicksPerCall)
	{
		UE_LOG(LogHapbeatTick, Warning,
			TEXT("[Hapbeat] %s: %d-tick cap hit in one call (would have fired %d). The threshold is probably far too small for the input range, or a wired handler is feeding back into the input source."),
			*GetReadableName(), MaxTicksPerCall, TicksToFire);
		TicksToFire = MaxTicksPerCall;
	}

	for (int32 Index = 0; Index < TicksToFire; ++Index)
	{
		Fire();
	}
}
