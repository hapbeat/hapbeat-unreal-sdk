// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatTriggerComponent.h"
#include "Curves/CurveFloat.h" // FRuntimeFloatCurve
#include "HapbeatCollisionTriggerComponent.generated.h"

class UPrimitiveComponent;
struct FHitResult;

/** Which physics event drives the fire. UE is 3D-only (no Unity 2D/3D split). */
UENUM(BlueprintType)
enum class EHapbeatCollisionEvent : uint8
{
	/** OnComponentHit — a blocking collision (requires bNotifyRigidBodyCollision / "Simulation Generates Hit Events"). */
	Hit UMETA(DisplayName = "Hit"),
	/** OnComponentBeginOverlap — an overlap begin (requires "Generate Overlap Events"). */
	BeginOverlap UMETA(DisplayName = "Begin Overlap"),
};

/** How collision gain is derived. Mirrors Hapbeat.HapbeatCollisionTrigger.GainMode. */
UENUM(BlueprintType)
enum class EHapbeatGainMode : uint8
{
	/** Use the EventMap entry gain as-is (fire at full composed gain). */
	Fixed UMETA(DisplayName = "Fixed"),
	/** Map impact speed through VelocityCurve to a [0..1] multiplier. */
	VelocityScaled UMETA(DisplayName = "Velocity Scaled"),
};

/**
 * Fires a haptic event on a physics Hit or BeginOverlap of the owner's primitive
 * component. UE counterpart of Hapbeat.HapbeatCollisionTrigger (3D only).
 *
 * Attach to an actor that has a collider (UPrimitiveComponent). On BeginPlay it
 * binds OnComponentHit or OnComponentBeginOverlap (per TriggerEvent) on the
 * owner's event-capable primitive (root first, then siblings); on EndPlay it
 * unbinds. For Hit the primitive must have
 * "Simulation Generates Hit Events" (bNotifyRigidBodyCollision) enabled — a hint
 * is logged if it isn't.
 *
 * Gain composition is the inherited base composition: Fixed -> Fire() (multiplier
 * 1); VelocityScaled -> FireWithGain(curveOrLinearMultiplier), where the
 * multiplier folds into GainMultiplier exactly as Unity's VelocityScaled path
 * folds the curve value into the entry gain x intensity x trigger-multiplier
 * product.
 */
UCLASS(ClassGroup = (Hapbeat), meta = (BlueprintSpawnableComponent))
class HAPBEATSDK_API UHapbeatCollisionTriggerComponent : public UHapbeatTriggerComponent
{
	GENERATED_BODY()

public:
	UHapbeatCollisionTriggerComponent();

	/** Which physics event to listen for on the owner's primitive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Collision")
	EHapbeatCollisionEvent TriggerEvent = EHapbeatCollisionEvent::Hit;

	/** Fixed gain vs velocity-scaled gain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Collision")
	EHapbeatGainMode GainMode = EHapbeatGainMode::Fixed;

	/** Only fire if the OTHER actor has this tag. NAME_None = any actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Collision")
	FName TagFilter = NAME_None;

	/** Impact speed (cm/s) below which VelocityScaled does not fire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Collision", meta = (ClampMin = "0.0"))
	float VelocityThreshold = 0.0f;

	/** Impact speed (cm/s) at which the curve reaches its normalized max (1.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Collision", meta = (ClampMin = "0.0"))
	float MaxVelocity = 10.0f;

	/**
	 * Maps normalized impact speed (X: (speed-threshold)/(max-threshold) clamped to
	 * [0,1]) to a gain multiplier (Y). With no keys, a linear identity is used.
	 * VelocityScaled only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Collision")
	FRuntimeFloatCurve VelocityCurve;

	/**
	 * Report only the START of a contact, the way Unity's OnCollisionEnter does.
	 *
	 * Chaos reports OnComponentHit for a CONTINUING touch, not just for the
	 * moment of impact: a ball rolling along a wall, or a pin leaning on its
	 * neighbour, keeps producing hits every frame. Unity's OnCollisionEnter fires
	 * once and stays quiet until the two bodies have separated, and every gain /
	 * threshold value in this SDK was authored against that behaviour.
	 *
	 * With this on, hits from the same OTHER COMPONENT that keep arriving inside
	 * ContactSeparationSeconds of each other count as one contact and are
	 * dropped. Turn it off for a trigger that genuinely wants a continuous
	 * stream of contact events. Hit mode only -- BeginOverlap is already an
	 * enter-only event.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Collision")
	bool bEnterOnly = true;

	/**
	 * The quiet gap that ends a contact, in seconds (bEnterOnly only). A hit from
	 * the same component within this long of the previous one is the same touch
	 * continuing; a longer gap makes the next hit a new one.
	 *
	 * INDEPENDENT of Cooldown: this is per other-component and is about what
	 * counts as one contact, while Cooldown is a per-trigger rate limit across
	 * all contacts. A trigger can sensibly use both.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Collision", meta = (ClampMin = "0.0"))
	float ContactSeparationSeconds = 0.2f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** OnComponentHit handler — signature must match FComponentHitSignature exactly. */
	UFUNCTION()
	void HandleHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	/** OnComponentBeginOverlap handler — signature must match FComponentBeginOverlapSignature exactly. */
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Tag-filter + Fixed/VelocityScaled dispatch shared by both handlers. Speed is cm/s (0 if unknown). */
	void HandleCollision(AActor* OtherActor, float ImpactSpeed);

	/** Resolve the owner primitive that can emit TriggerEvent (root first, then sibling primitives). */
	UPrimitiveComponent* ResolveOwnerPrimitive() const;

	/**
	 * Enter-only gate (see bEnterOnly). Returns true when this hit continues a
	 * contact already in progress and must be dropped. Always stamps the
	 * component's last-contact time, so a touch that keeps producing hits keeps
	 * the gate closed until it actually stops.
	 */
	bool IsContinuingContact(UPrimitiveComponent* OtherComp);

	/**
	 * Last hit time per other-component, for IsContinuingContact. Weak keys: the
	 * other actor may be destroyed while its entry is still in here, and this map
	 * must not be what keeps it alive. Pruned in IsContinuingContact.
	 */
	TMap<TWeakObjectPtr<UPrimitiveComponent>, double> LastContactTime;

	/** The primitive we bound delegates on (kept so EndPlay can unbind the exact instance). */
	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> BoundPrimitive = nullptr;
};
