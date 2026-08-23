// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatParameterBinding.h"

#include "HapbeatSubsystem.h"
#include "HapbeatStreamPlayback.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UHapbeatParameterBinding::UHapbeatParameterBinding()
{
	// Per-frame evaluation: read source -> map -> write to the active playback.
	PrimaryComponentTick.bCanEverTick = true;
}

void UHapbeatParameterBinding::BeginPlay()
{
	Super::BeginPlay();

	// Seed the PositionDeltaMagnitude state so the first tick after play starts
	// returns 0 (a velocity computed against a stale/zero prev would spike).
	ResetPositionDeltaState();
}

void UHapbeatParameterBinding::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const float Raw = ReadSourceValue(DeltaTime);
	const float Out = ComputeOutput(Raw);
	WriteToActivePlayback(Out);
}

void UHapbeatParameterBinding::SetValue(float Value)
{
	// Stored for the next tick (External source) — matches Unity SetValue.
	LastExternalValue = Value;
}

float UHapbeatParameterBinding::EvaluateNow()
{
	// One-shot read + write outside the tick, used to pre-seed the first stream
	// chunk so it goes out already modulated (no ~100 ms full-baseline burst).
	// PositionDeltaMagnitude has no meaningful per-call dt here, so pass 0 — the
	// reset path then seeds PrevWorldPos and returns 0 for this evaluation.
	const float Raw = ReadSourceValue(0.0f);
	const float Out = ComputeOutput(Raw);
	WriteToActivePlayback(Out);
	return Out;
}

float UHapbeatParameterBinding::ReadSourceValue(float DeltaTime)
{
	const AActor* Owner = GetOwner();
	USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;

	switch (SourceProperty)
	{
	case EHapbeatBindingSource::LocalPositionX:
		return Root ? static_cast<float>(Root->GetRelativeLocation().X) : 0.0f;
	case EHapbeatBindingSource::LocalPositionY:
		return Root ? static_cast<float>(Root->GetRelativeLocation().Y) : 0.0f;
	case EHapbeatBindingSource::LocalPositionZ:
		return Root ? static_cast<float>(Root->GetRelativeLocation().Z) : 0.0f;

	case EHapbeatBindingSource::VelocityMagnitude:
	{
		// Physics velocity lives on UPrimitiveComponent (Unity reads Rigidbody;
		// the UE analogue is a simulating primitive root).
		// Non-const: GetPhysicsLinearVelocity/AngularVelocity are (oddly) declared
		// non-const on UPrimitiveComponent.
		UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Root);
		if (Prim != nullptr && Prim->IsSimulatingPhysics())
		{
			return static_cast<float>(Prim->GetPhysicsLinearVelocity().Size());
		}
		// Not simulating: fall back to the component's REPORTED velocity, which a
		// kinematic body sets itself (USceneComponent::ComponentVelocity). Without
		// this a code-moved body reads 0 forever even while it visibly moves --
		// the physics velocity of a non-simulating body never leaves zero.
		return Root != nullptr ? static_cast<float>(Root->GetComponentVelocity().Size()) : 0.0f;
	}
	case EHapbeatBindingSource::AngularVelocityMagnitude:
	{
		UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Root);
		return Prim ? static_cast<float>(Prim->GetPhysicsAngularVelocityInDegrees().Size()) : 0.0f;
	}

	case EHapbeatBindingSource::PositionDeltaMagnitude:
	{
		if (Owner == nullptr)
		{
			return 0.0f;
		}
		// World-space speed from frame-to-frame actor-location delta. Works for
		// kinematic / code-moved bodies whose physics velocity stays 0.
		const FVector Current = Owner->GetActorLocation();
		if (bResetPositionDelta || DeltaTime <= 0.0f)
		{
			// First tick after enable (or no dt): seed and emit 0, never a spike.
			PrevWorldPos = Current;
			bResetPositionDelta = false;
			return 0.0f;
		}
		const float Speed = static_cast<float>((Current - PrevWorldPos).Size()) / DeltaTime;
		PrevWorldPos = Current;
		return Speed;
	}

	case EHapbeatBindingSource::External:
		return LastExternalValue;

	default:
		return 0.0f;
	}
}

float UHapbeatParameterBinding::ComputeOutput(float Raw)
{
	CurrentInput = Raw;

	// Normalize into 0..1 against the input range, guarding a zero-width range
	// (Unity uses an epsilon of 0.0001 and returns 0 for a degenerate range).
	const float Range = InputMax - InputMin;
	const float Normalized = FMath::Abs(Range) > 0.0001f
		? FMath::Clamp((Raw - InputMin) / Range, 0.0f, 1.0f)
		: 0.0f;
	CurrentNormalized = Normalized;

	const float Curved = ApplyCurve(Normalized);
	// Unity's Mathf.Lerp clamps its alpha to [0, 1]; FMath::Lerp does NOT. Clamp
	// the curve result so a Custom UCurveFloat returning values outside [0, 1]
	// produces the same output as Unity (built-in curves already stay in range).
	const float Out = FMath::Lerp(OutputMin, OutputMax, FMath::Clamp(Curved, 0.0f, 1.0f));
	CurrentOutput = Out;
	return Out;
}

void UHapbeatParameterBinding::WriteToActivePlayback(float Out)
{
	UHapbeatSubsystem* Sub = ResolveSubsystem();
	if (Sub == nullptr)
	{
		return;
	}

	// v1 is single-active-stream, so there is one GetActivePlayback() to look at
	// -- but "one stream" is exactly why the binding must NOT write to it
	// unconditionally: every binding in the level would otherwise modulate
	// whatever happens to be playing, and a slider binding in one zone would
	// overwrite another zone's loop gain every frame. Unity scopes a binding to
	// the playback started by its own linked owner entry; OwnsPlayback below is
	// the UE equivalent of that scope.
	UHapbeatStreamPlayback* Pb = Sub->GetActivePlayback();
	if (Pb == nullptr || !Pb->IsActive() || !OwnsPlayback(Pb))
	{
		return;
	}

	switch (OutputParameter)
	{
	case EHapbeatBindingOutput::StreamGain:
		// Multiplicative on the playback baseline (entry.gain x manifest.intensity):
		//   final = baseline x Out. ApplyGainModulation clamps Gain to [0, 2].
		Pb->ApplyGainModulation(Out);
		break;
	case EHapbeatBindingOutput::StreamPan:
		// Pan is a position (-1..+1), not a multiplier — set directly (clamped inside).
		Pb->SetPan(Out);
		break;
	}
}

bool UHapbeatParameterBinding::OwnsPlayback(const UHapbeatStreamPlayback* Playback) const
{
	if (Playback == nullptr)
	{
		return false;
	}
	const AActor* Origin = Playback->GetOwnerActor();
	if (Origin == nullptr)
	{
		// Unattributed (or the origin has since been destroyed): nothing started
		// it through a trigger -- a Blueprint "Play Hapbeat Event" node, or a
		// sample calling the subsystem straight. There is no origin to compare
		// against, so the pre-scope behaviour stands; refusing here would silently
		// break every binding driven that way.
		return true;
	}
	const AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return false;
	}
	if (Owner == Origin)
	{
		return true;
	}
	// Attachment counts as the same rig: a zone commonly puts the trigger on a
	// child actor (the shark, a pin) and the binding on the actor above or below
	// it. IsAttachedTo walks the whole attach chain, so either direction matches.
	return Owner->IsAttachedTo(Origin) || Origin->IsAttachedTo(Owner);
}

UHapbeatSubsystem* UHapbeatParameterBinding::ResolveSubsystem() const
{
	// Same resolution path as the trigger components: world -> game instance ->
	// subsystem, null-guarded at every hop (no GameInstance in edit-mode, etc.).
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}
	UGameInstance* GameInstance = World->GetGameInstance();
	if (GameInstance == nullptr)
	{
		return nullptr;
	}
	return GameInstance->GetSubsystem<UHapbeatSubsystem>();
}

float UHapbeatParameterBinding::ApplyCurve(float T) const
{
	// Verbatim from Hapbeat.HapbeatParameterBinding.ApplyCurve (Unity SDK).
	switch (CurveType)
	{
	case EHapbeatBindingCurve::Linear:
		return T;
	case EHapbeatBindingCurve::EaseIn:
		return T * T;
	case EHapbeatBindingCurve::EaseOut:
		return 1.0f - (1.0f - T) * (1.0f - T);
	case EHapbeatBindingCurve::Exponential:
		return (FMath::Exp(3.0f * T) - 1.0f) / (FMath::Exp(3.0f) - 1.0f);
	case EHapbeatBindingCurve::Custom:
		return CustomCurve != nullptr ? CustomCurve->GetFloatValue(T) : T;
	default:
		return T;
	}
}

void UHapbeatParameterBinding::ResetPositionDeltaState()
{
	bResetPositionDelta = true;
	PrevWorldPos = FVector::ZeroVector;
}
