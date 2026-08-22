// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HapbeatShowcaseZone.h" // IHapbeatShowcaseZone: the switcher asks the zone for its label / keys / spawn
#include "HapbeatShowcaseZ1BowlingActor.generated.h"

class UCapsuleComponent;
class UChildActorComponent;
class UHapbeatClip;
class UHapbeatCollisionTriggerComponent;
class UHapbeatEventMap;
class UMaterialInterface;
class USoundBase;
class UStaticMesh;
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
 * WHY THE PINS ARE CHILD ACTORS: UHapbeatCollisionTriggerComponent binds to its
 * OWNING ACTOR's root primitive (UHapbeatCollisionTriggerComponent::
 * ResolveOwnerPrimitive), so six sibling mesh components on one actor could not
 * each carry their own independently bound trigger. A UChildActorComponent gives
 * each pin its own actor -- satisfying that rule -- while keeping its transform
 * an ordinary, editable, SAVED relative transform in this actor's Details panel.
 * (Phase 2 spawned the pins from code instead, which meant their layout could
 * not be adjusted in the editor at all.) The lane and the ball, which need no
 * trigger, stay as ordinary components.
 *
 * LAYOUT: every number below is the Unity Showcase's own, converted -- Unity
 * (x, y, z) metres become UE (z, x, y) centimetres. The lane, the ball's mark
 * and the six pin positions all come straight from Showcase.unity's Z1_Bowling
 * subtree, so the rack sits where it does in Unity rather than at an
 * approximation of it.
 *
 * NOTE: z1_pin_hit is a STREAM_CLIP-mode event (as is every other Showcase
 * entry) -- the waveform is streamed from the project, so no Kit deployment is
 * needed for this zone to produce haptics.
 *
 * Keys (Unity parity, BallLauncher.cs): LEFT MOUSE launches the ball along the
 * player's view direction, SPACE resets the ball AND the pin rack. Unity has no
 * auto-respawn -- the reset key is the only way back -- so neither does this.
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ1BowlingActor : public AActor, public IHapbeatShowcaseZone
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ1BowlingActor();

	// ---- IHapbeatShowcaseZone ----
	virtual int32 GetZoneIndex() const override { return 1; }
	virtual FText GetZoneLabel() const override;
	virtual TArray<FHapbeatShowcaseHudCommand> GetHudCommands() const override;
	virtual FTransform GetPlayerSpawnRelative() const override;
	virtual void OnZoneActivated() override;
	virtual void OnZoneDeactivated() override;

	/** Ball launch speed, cm/s. 800 = Unity BallLauncher._launchSpeed 8 m/s. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Bowling", meta = (ClampMin = "0.0"))
	float LaunchSpeed = 800.0f;

	/** Finished pin height, cm. Unity's rack reads 78 cm tall (bowling_pin.obj at instance scale 0.51/0.68/0.51). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Bowling", meta = (ClampMin = "1.0"))
	float PinHeightCm = 78.0f;

	/** Finished pin diameter, cm. Same source as PinHeightCm. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Bowling", meta = (ClampMin = "1.0"))
	float PinDiameterCm = 19.0f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Resolve the EventMap (asset or fallback) and wire the shared z1_pin_hit entry id every pin's trigger uses. */
	void BuildEventMap();

	/** Build the transient EventMap used when no asset is assigned. */
	UHapbeatEventMap* BuildFallbackEventMap();

	/** Hand each child pin actor its EventMap / entry / SFX and remember the pose Space returns it to. */
	void SetUpPins();

	/** Swap in the imported lane / ball materials when that optional content is present; no-op otherwise. */
	void ApplyShowcaseAssets();

	/** EnableInput on the first PlayerController found, then bind LMB / Space. Warns (no-op) if none exists. */
	void BindInput();

	void HandleLaunchKey(); // left mouse
	void HandleResetKey();  // Space

	/** Teleport the ball back to its rest pose and zero its physics velocity (ETeleportType::ResetPhysics). */
	void ResetBallToSpawn();

	/** Return every pin to the pose it was racked at, physics state cleared. Unity BallLauncher.ResetPose(). */
	void ResetPinsToRack();

	/**
	 * Ball + pins simulate only while this zone is the visible one. A hidden
	 * zone's pins would otherwise keep falling under gravity with their
	 * collision switched off and be somewhere under the floor by the time you
	 * came back to them.
	 */
	void SetPhysicsRunning(bool bRunning);

	/**
	 * Horizontal launch direction: the player's view forward flattened onto the
	 * ground plane, or this actor's forward when no Showcase character is
	 * possessed. Unity: Vector3.ProjectOnPlane(_aimReference.forward, Vector3.up).
	 */
	FVector ResolveLaunchDirection() const;

	/** Fixed on-screen-message keys, offset into the 100s so they don't collide with other zones' HUD lines. */
	static constexpr int32 KeyGuideHudLineKey = 100;
	static constexpr int32 StatusHudLineKey = 101;
	static constexpr float HudRefreshIntervalSeconds = 0.5f;
	float HudRefreshTimer = 0.0f;

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

	// ---- Constructor-created default subobjects: the editable scene ----

	/** Unity Z1_Bowling/Lane: centre (367.2, 0, 85) cm, 939.4 x 226.5 x 10 cm, so its top face is Z = 90. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Bowling")
	TObjectPtr<UStaticMeshComponent> LaneMesh;

	/** Unity Z1_Bowling/Ball: 40 cm sphere on its mark at (-100, 0, 129.5) cm, 4 kg. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Bowling")
	TObjectPtr<UStaticMeshComponent> BallMesh;

	/**
	 * The 6 pins of the rack, each a child actor so it can own its own collision
	 * trigger (see the class comment) while keeping an editable relative
	 * transform. Positions are Unity's Pin_1..Pin_6.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Bowling")
	TArray<TObjectPtr<UChildActorComponent>> PinSlots;

	/** Actor-relative poses the pins were racked at, parallel to PinSlots; Space restores them. */
	TArray<FTransform> PinRestRelativeTransforms;

	/** Root-relative resting location the ball is returned to on launch / respawn. */
	FVector BallRestRelativeLocation = FVector::ZeroVector;
};

/**
 * One bowling pin -- a capsule root that carries the physics body and this
 * pin's own UHapbeatCollisionTriggerComponent (Hit, VelocityScaled), plus a
 * non-colliding mesh child for the look.
 *
 * WHY A CAPSULE ROOT AND NOT THE MESH: the imported pin's own axes and pivot
 * are whatever the source model happened to use, so a mesh root would make both
 * the collider shape and the "where is the floor" question depend on the asset.
 * A capsule states the collider in the zone's own terms (9.6 cm radius, 40 cm
 * half height -- an 80 cm capsule around a 78 cm pin) and lets the mesh be
 * turned upright and centred inside it as a plain child transform.
 *
 * EventMap / EntryId / HitSound are assigned by the owning zone actor in its
 * BeginPlay (AHapbeatShowcaseZ1BowlingActor::SetUpPins); the collision trigger
 * reads them at fire time, so their arrival is not ordered against this actor's
 * own BeginPlay.
 *
 * The trigger's numbers come from the Unity Showcase's BowlingPin.prefab
 * (_maxVelocity = 1 Unity m/s -> 100 cm/s), except VelocityThreshold, which is
 * deliberately NOT Unity's 0.01 m/s -- see the .cpp.
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
	 * EntryId to it.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatCollisionTriggerComponent> HitTrigger;

	/**
	 * Impact SFX, assigned by the zone (S_z1_pin_hit when that optional content
	 * is present, null otherwise = silent). Played with the same
	 * velocity-to-volume curve as Unity's CollisionAudio on BowlingPin.
	 */
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> HitSound;

	/** Fit the mesh child to DesiredHeight x DesiredDiameter cm, stood upright and centred in the capsule. */
	void ApplyPinSize(float DesiredHeightCm, float DesiredDiameterCm);

	/** Rack pose restore: teleport back and clear the physics body's momentum. */
	void ResetToTransform(const FTransform& RestTransform);

	/** Start / stop simulating -- the zone stops its pins while it is hidden. */
	void SetPhysicsRunning(bool bRunning);

protected:
	/**
	 * Enables physics simulation + rigid-body hit notifications. Deferred from
	 * the constructor to BeginPlay so the capsule is fully registered first
	 * (SetSimulatePhysics on an unregistered component is order-dependent /
	 * can log a spurious "no physics body" warning -- see the .cpp).
	 */
	virtual void BeginPlay() override;

private:
	/**
	 * Impact SFX driver -- the UE counterpart of Unity's CollisionAudio
	 * component, with its numbers converted to cm/s: play below-threshold hits
	 * not at all, map 50..500 cm/s onto volume 0.2..1.0, and swallow repeats
	 * inside 0.05 s so a pin rattling against its neighbour does not machine-gun.
	 */
	UFUNCTION()
	void HandlePinHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse, const FHitResult& Hit);

	// Unity CollisionAudio: _minVelocity 0.5 m/s, _maxVelocity 5 m/s,
	// _minVolume 0.2, _cooldown 0.05 s. Velocities x100 for UE's cm/s.
	static constexpr float HitSoundMinSpeed = 50.0f;
	static constexpr float HitSoundMaxSpeed = 500.0f;
	static constexpr float HitSoundMinVolume = 0.2f;
	static constexpr float HitSoundCooldownSeconds = 0.05f;

	/** World seconds of the last impact sound, for the cooldown above. */
	float LastHitSoundTime = -1000.0f;

	/** Root: the physics body and the primitive HitTrigger binds to. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UCapsuleComponent> PinBody;

	/** Look only -- no collision, so it never competes with the capsule. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UStaticMeshComponent> PinMesh;
};
