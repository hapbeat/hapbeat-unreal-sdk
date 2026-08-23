// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatCollisionTriggerComponent.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/HitResult.h"             // FHitResult (split out of EngineTypes.h in UE5)
#include "PhysicsEngine/BodyInstance.h"   // FBodyInstance::bNotifyRigidBodyCollision (read for the Hit hint)

DEFINE_LOG_CATEGORY_STATIC(LogHapbeat, Log, All);

UHapbeatCollisionTriggerComponent::UHapbeatCollisionTriggerComponent()
{
	// Defaults: a linear identity velocity curve (X normalized speed -> Y multiplier).
	// Only consulted in VelocityScaled mode; with no keys HandleCollision falls back
	// to using the normalized speed directly.
	if (FRichCurve* Rich = VelocityCurve.GetRichCurve())
	{
		Rich->Reset();
		Rich->AddKey(0.0f, 0.0f);
		Rich->AddKey(1.0f, 1.0f);
	}
}

void UHapbeatCollisionTriggerComponent::BeginPlay()
{
	Super::BeginPlay();

	BoundPrimitive = ResolveOwnerPrimitive();
	if (BoundPrimitive == nullptr)
	{
		UE_LOG(LogHapbeat, Warning,
			TEXT("HapbeatCollisionTrigger on %s found no UPrimitiveComponent to bind; no collision haptics will fire."),
			*GetNameSafe(GetOwner()));
		return;
	}

	if (TriggerEvent == EHapbeatCollisionEvent::Hit)
	{
		BoundPrimitive->OnComponentHit.AddDynamic(this, &UHapbeatCollisionTriggerComponent::HandleHit);
		// Hit events only fire when the primitive is set to generate them. Hint
		// (don't auto-mutate the user's collision setup) if it isn't enabled.
		if (!BoundPrimitive->BodyInstance.bNotifyRigidBodyCollision)
		{
			UE_LOG(LogHapbeat, Warning,
				TEXT("HapbeatCollisionTrigger on %s uses Hit but primitive '%s' has 'Simulation Generates Hit Events' (bNotifyRigidBodyCollision) OFF; OnComponentHit will not fire. Enable it on the collider."),
				*GetNameSafe(GetOwner()), *GetNameSafe(BoundPrimitive));
		}
	}
	else // BeginOverlap
	{
		BoundPrimitive->OnComponentBeginOverlap.AddDynamic(this, &UHapbeatCollisionTriggerComponent::HandleBeginOverlap);
		if (!BoundPrimitive->GetGenerateOverlapEvents())
		{
			UE_LOG(LogHapbeat, Warning,
				TEXT("HapbeatCollisionTrigger on %s uses Begin Overlap but primitive '%s' has 'Generate Overlap Events' OFF; OnComponentBeginOverlap will not fire. Enable it on the collider."),
				*GetNameSafe(GetOwner()), *GetNameSafe(BoundPrimitive));
		}
	}
}

void UHapbeatCollisionTriggerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BoundPrimitive != nullptr)
	{
		// Unbind the exact instance we bound (RemoveDynamic on an unbound delegate
		// is harmless, but matching the bound event keeps intent clear).
		if (TriggerEvent == EHapbeatCollisionEvent::Hit)
		{
			BoundPrimitive->OnComponentHit.RemoveDynamic(this, &UHapbeatCollisionTriggerComponent::HandleHit);
		}
		else
		{
			BoundPrimitive->OnComponentBeginOverlap.RemoveDynamic(this, &UHapbeatCollisionTriggerComponent::HandleBeginOverlap);
		}
		BoundPrimitive = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void UHapbeatCollisionTriggerComponent::HandleHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// Impact speed proxy: relative velocity of the two bodies (mirrors Unity's
	// collision.relativeVelocity.magnitude). When neither body reports a physics
	// velocity (e.g. non-simulating movement), fall back to the contact impulse
	// magnitude so velocity-scaling still has a signal.
	FVector OwnerVel = FVector::ZeroVector;
	FVector OtherVel = FVector::ZeroVector;
	if (HitComp != nullptr && HitComp->IsSimulatingPhysics())
	{
		OwnerVel = HitComp->GetPhysicsLinearVelocity();
	}
	if (OtherComp != nullptr && OtherComp->IsSimulatingPhysics())
	{
		OtherVel = OtherComp->GetPhysicsLinearVelocity();
	}
	float Speed = (OwnerVel - OtherVel).Size();
	if (Speed <= KINDA_SMALL_NUMBER)
	{
		Speed = NormalImpulse.Size();
	}

	// Enter semantics: Chaos keeps reporting a contact that is still touching,
	// where Unity's OnCollisionEnter reports only its start (see bEnterOnly).
	if (IsContinuingContact(OtherComp))
	{
		if (bVerboseLog)
		{
			UE_LOG(LogHapbeat, Log, TEXT("Hit ignored: same contact continuing with '%s' on %s"),
				*GetNameSafe(OtherComp), *GetNameSafe(GetOwner()));
		}
		return;
	}

	// Context for the OnFired broadcast, so a listener (impact SFX) gets the
	// partner and the speed without re-deriving them from its own hit callback.
	FireContextOther = OtherActor;
	FireContextSpeed = Speed;

	HandleCollision(OtherActor, Speed);

	// Cleared here as well as in FireInternal: a fire rejected by the tag filter,
	// the velocity threshold or the cooldown never reaches that broadcast, and
	// the context must not survive into an unrelated later Fire().
	FireContextOther.Reset();
	FireContextSpeed = 0.0f;
}

bool UHapbeatCollisionTriggerComponent::IsContinuingContact(UPrimitiveComponent* OtherComp)
{
	if (!bEnterOnly || ContactSeparationSeconds <= 0.0f || OtherComp == nullptr)
	{
		return false;
	}

	// Same clock as the base cooldown (unscaled real time), so pausing or time
	// dilation cannot turn one touch into a burst of "new" contacts.
	const double Now = NowUnscaledSeconds();
	const TWeakObjectPtr<UPrimitiveComponent> Key(OtherComp);
	double* Previous = LastContactTime.Find(Key);
	const bool bContinuing = Previous != nullptr
		&& (Now - *Previous) <= static_cast<double>(ContactSeparationSeconds);

	// Stamped whether or not the hit is passed on: a contact that keeps
	// producing hits has to keep the gate closed, and only a real gap in the
	// hits may open it again.
	if (Previous != nullptr)
	{
		*Previous = Now;
	}
	else
	{
		LastContactTime.Add(Key, Now);

		// Drop what can no longer match -- entries whose component is gone, and
		// contacts old enough that a fresh hit would count as new anyway. Done on
		// insert only, and only once the map is big enough to be worth walking,
		// so an ordinary two-body contact never pays for it.
		constexpr int32 PruneAtNumEntries = 32;
		if (LastContactTime.Num() > PruneAtNumEntries)
		{
			for (auto It = LastContactTime.CreateIterator(); It; ++It)
			{
				if (!It.Key().IsValid()
					|| (Now - It.Value()) > static_cast<double>(ContactSeparationSeconds))
				{
					It.RemoveCurrent();
				}
			}
		}
	}
	return bContinuing;
}

void UHapbeatCollisionTriggerComponent::HandleBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	// Overlaps carry no impulse; use the owner primitive's current linear speed
	// (Unity passes 0 for trigger-enter, but the owner's speed is the better
	// velocity-scaling signal for an overlap-driven hit on this body).
	const float Speed = OverlappedComp != nullptr ? OverlappedComp->GetPhysicsLinearVelocity().Size() : 0.0f;

	// No enter-only gate here: BeginOverlap already fires once per overlap.
	FireContextOther = OtherActor;
	FireContextSpeed = Speed;
	HandleCollision(OtherActor, Speed);
	FireContextOther.Reset();
	FireContextSpeed = 0.0f;
}

void UHapbeatCollisionTriggerComponent::HandleCollision(AActor* OtherActor, float ImpactSpeed)
{
	// Tag filter against the OTHER actor (NAME_None = any). ActorHasTag checks the
	// actor's Tags array.
	if (TagFilter != NAME_None)
	{
		if (OtherActor == nullptr || !OtherActor->ActorHasTag(TagFilter))
		{
			return;
		}
	}

	if (GainMode == EHapbeatGainMode::Fixed)
	{
		// Fire() == FireInternal(1): base composition uses entry gain x GainMultiplier.
		Fire();
		return;
	}

	// VelocityScaled: VelocityThreshold is purely the fire gate; normalize over
	// [0, MaxVelocity], then sample the curve (or use the normalized value directly
	// when keyless). Unity parity: HapbeatCollisionTrigger.FireWithVelocity =
	// Clamp01(velocity / maxVelocity), with _velocityThreshold gating the fire only
	// (it does NOT shift the normalization origin).
	if (ImpactSpeed < VelocityThreshold)
	{
		if (bVerboseLog)
		{
			UE_LOG(LogHapbeat, Log, TEXT("Collision below VelocityThreshold (%.1f < %.1f) on %s; skipped."),
				ImpactSpeed, VelocityThreshold, *GetNameSafe(GetOwner()));
		}
		return;
	}

	const float Normalized = MaxVelocity > KINDA_SMALL_NUMBER
		? FMath::Clamp(ImpactSpeed / MaxVelocity, 0.0f, 1.0f)
		: 1.0f; // degenerate max -> full

	float Multiplier = Normalized;
	if (const FRichCurve* Rich = VelocityCurve.GetRichCurveConst())
	{
		if (Rich->GetNumKeys() > 0)
		{
			Multiplier = Rich->Eval(Normalized);
		}
	}

	if (bVerboseLog)
	{
		UE_LOG(LogHapbeat, Log, TEXT("Collision VelocityScaled on %s: speed=%.1f normalized=%.2f -> mult=%.2f"),
			*GetNameSafe(GetOwner()), ImpactSpeed, Normalized, Multiplier);
	}

	// FireWithGain folds the multiplier into the base composition exactly as Unity
	// folds curveValue into entry.gain x intensity x trigger-multiplier:
	//   Command    -> wire = entry.GetEffectiveGain() x GainMultiplier x Multiplier
	//   StreamClip -> initialMod = GainMultiplier x Multiplier (baseline kept).
	FireWithGain(Multiplier);
}

UPrimitiveComponent* UHapbeatCollisionTriggerComponent::ResolveOwnerPrimitive() const
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return nullptr;
	}
	// Prefer the root component when it is itself a primitive (the actor's own
	// collider), else the first primitive component on the actor.
	if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
	{
		return Root;
	}
	return Owner->FindComponentByClass<UPrimitiveComponent>();
}
