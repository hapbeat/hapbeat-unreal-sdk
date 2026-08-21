// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HapbeatShowcaseZ1BowlingActor.generated.h"

class UHapbeatClip;
class UHapbeatCollisionTriggerComponent;
class UHapbeatEventMap;
class UStaticMeshComponent;
class AHapbeatShowcaseZ1PinActor;

/**
 * Z1 Bowling Lane -- the "component-only haptics" showcase zone: a lane with a
 * ball and a 1-2-3 triangle rack of 6 pins, where EVERY pin carries its own
 * UHapbeatCollisionTriggerComponent (Hit, VelocityScaled). This actor issues
 * ZERO imperative Hapbeat calls of its own -- once the physics knocks a pin,
 * that pin's own trigger fires on its own. UE counterpart of the Unity
 * Showcase's Z1 (see Samples~/Showcase/Scripts/BallLauncher.cs, which is
 * haptics-inert by design for the identical reason: "haptic = Trigger 任せ"
 * pattern demo).
 *
 * IMPORTANT UE-specific note: UHapbeatCollisionTriggerComponent resolves the
 * primitive it binds to via its OWNING ACTOR's root component (or the first
 * UPrimitiveComponent found on that actor -- see
 * UHapbeatCollisionTriggerComponent::ResolveOwnerPrimitive). Six sibling mesh
 * components living on ONE actor could not each carry their own independently
 * bound trigger under that resolution rule, so every pin is spawned as its OWN
 * AHapbeatShowcaseZ1PinActor (mesh as root + its own trigger), not as a child
 * component of this zone actor. The lane and the ball, which need no trigger,
 * stay as ordinary components on this actor.
 *
 * Geometry is engine primitives only (/Engine/BasicShapes/Cube + Sphere +
 * Cylinder) assembled in the constructor; no imported meshes, no Blueprint
 * assets. Haptics-only (no SFX -- would need a USoundWave import).
 *
 * NOTE: z1_pin_hit is a STREAM_CLIP-mode event (as is every other Showcase
 * entry) -- the waveform is streamed from the project, so no Kit deployment is
 * needed for this zone to produce haptics.
 *
 * Key: B -- reset the ball to the spawn point and launch it down the lane
 * (AddImpulse). There is no separate reset key in this simplified sample, so
 * the ball also auto-resets RespawnDelaySeconds after a launch in case it
 * rolled off the lane or came to rest downrange.
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ1BowlingActor : public AActor
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ1BowlingActor();

	/**
	 * Local offset applied to this zone's own root (lane + ball + pin rack) at
	 * BeginPlay, so a future master/layout actor can nudge each zone into a row
	 * slot without altering the zone actor's own placed transform.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	FVector FootprintOffset = FVector::ZeroVector;

	/** Ball launch speed along the lane's +X (local forward), cm/s. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Bowling", meta = (ClampMin = "0.0"))
	float LaunchSpeed = 900.0f;

	/** Seconds after a launch before the ball is auto-reset to the spawn pose. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Bowling", meta = (ClampMin = "0.1"))
	float RespawnDelaySeconds = 4.0f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Resolve the EventMap (asset or fallback) and wire the shared z1_pin_hit entry id every pin's trigger uses. */
	void BuildEventMap();

	/** Build the transient EventMap used when no asset is assigned. */
	UHapbeatEventMap* BuildFallbackEventMap();

	/** Spawn the 6 AHapbeatShowcaseZ1PinActor instances in a 1-2-3 triangle rack and wire each one's HitTrigger. */
	void SpawnPinRack();

	/** EnableInput on the first PlayerController found, then BindKey(B). Warns (no-op) if none exists. */
	void BindInput();

	void HandleLaunchKey(); // B

	/** Teleport the ball back to its rest pose and zero its physics velocity (ETeleportType::ResetPhysics). */
	void ResetBallToSpawn();

	/** Fixed on-screen-message keys, offset into the 100s so they don't collide with other zones' HUD lines. */
	static constexpr int32 KeyGuideHudLineKey = 100;
	static constexpr int32 StatusHudLineKey = 101;
	static constexpr float HudRefreshIntervalSeconds = 0.5f;
	float HudRefreshTimer = 0.0f;

	/** Pending ResetBallToSpawn() call, RespawnDelaySeconds after a launch. */
	FTimerHandle RespawnTimer;

	/**
	 * The EventMap this zone plays from. Defaults to the plugin's shipped
	 * EM_Showcase asset (assigned in the constructor), so the gains / modes the
	 * zone actually uses are visible and editable in the editor instead of being
	 * buried in code -- that is how a real project works. Point it at your own
	 * asset to re-author them; clear it and the zone builds an equivalent map in
	 * code, so the sample still runs if the asset ever goes missing.
	 *
	 * Entries are resolved by event name, not by order (see BuildEventMap).
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatEventMap> EventMapOverride;

	UPROPERTY(Transient)
	TObjectPtr<UHapbeatEventMap> EventMap;

	/** Shared entry id every pin's HitTrigger references (StreamClip mode, "showcase-kit.z1_pin_hit"). */
	FGuid PinHitEntryId;

	// Strong ref keeping the pin-hit StreamClip WAV alive when the code-built
	// fallback map is in use (entries only hold a TSoftObjectPtr -- see
	// FHapbeatSampleLibrary::LoadSampleClip's GC note). Left null when the
	// EM_Showcase asset supplies the clip.
	UPROPERTY(Transient)
	TObjectPtr<UHapbeatClip> PinHitClip;

	// Constructor-created default subobjects (VisibleAnywhere, not Transient --
	// these ARE part of the CDO / serialized instance, unlike the BeginPlay-time
	// EventMap/pin-actor data below).

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Bowling")
	TObjectPtr<UStaticMeshComponent> LaneMesh;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Bowling")
	TObjectPtr<UStaticMeshComponent> BallMesh;

	// BeginPlay-time transient data (built fresh each Play session; not serialized).

	/** The 6 dynamically-spawned pin actors (see the class comment for why they can't be sibling components). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AHapbeatShowcaseZ1PinActor>> PinActors;

	/** Root-relative resting location the ball is returned to on launch / respawn. */
	FVector BallRestRelativeLocation = FVector::ZeroVector;
};

/**
 * One bowling pin -- a root cylinder mesh (simulating physics, notifying rigid
 * body collisions) plus its own UHapbeatCollisionTriggerComponent (Hit,
 * VelocityScaled). EventMap / EntryId are assigned by the owning zone actor
 * right after SpawnActor (see AHapbeatShowcaseZ1BowlingActor::SpawnPinRack) --
 * mirrors the Z5 Showcase zone's AHapbeatShowcaseZ5TargetActor pattern.
 *
 * VelocityThreshold / MaxVelocity are seeded in the constructor from the Unity
 * Showcase's BowlingPin.prefab HapbeatCollisionTrigger values (_velocityThreshold
 * = 0.01, _maxVelocity = 1, both in Unity meters/s), converted x100 to UE's
 * native cm/s physics units (1 cm/s / 100 cm/s).
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ1PinActor : public AActor
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ1PinActor();

	/**
	 * The collision trigger that fires this pin's z1_pin_hit entry. Public (not
	 * just VisibleAnywhere) so the owning zone actor can assign EventMap /
	 * EntryId right after SpawnActor.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatCollisionTriggerComponent> HitTrigger;

protected:
	/**
	 * Enables physics simulation + rigid-body hit notifications. Deferred from
	 * the constructor to BeginPlay so PinMesh is fully registered first
	 * (SetSimulatePhysics on an unregistered component is order-dependent /
	 * can log a spurious "no physics body" warning -- see the .cpp).
	 */
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UStaticMeshComponent> PinMesh;
};
