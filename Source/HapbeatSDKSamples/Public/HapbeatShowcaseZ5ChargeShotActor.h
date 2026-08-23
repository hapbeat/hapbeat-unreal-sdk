// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HapbeatShowcaseZone.h" // IHapbeatShowcaseZone: the switcher asks the zone for its label / keys / spawn
#include "HapbeatShowcaseZ5ChargeShotActor.generated.h"

class UHapbeatClip;
class UHapbeatEventMap;
class UHapbeatStreamPlayback;
class UHapbeatSubsystem;
class UHapbeatCollisionTriggerComponent;
class UAudioComponent;
class UChildActorComponent;
class UMaterialInterface;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;
class SWidget;
class AHapbeatShowcaseCharacter;
class AHapbeatShowcaseZ5TargetActor;
class AHapbeatShowcaseZ5ProjectileActor;

/**
 * Z5 Target Range -- a blaster and ONE target board, driven
 * entirely IMPERATIVELY (no HapbeatTriggerComponent on the zone actor itself),
 * matching Unity's ChargeShooter.cs design intent exactly: "other zones wire
 * declarative HapbeatXxxTrigger components; Z5 deliberately calls the SDK API
 * straight from script so both patterns are shown side by side." Reuses
 * showcase-kit verbatim (6 events -- see
 * Samples~/Showcase/EventMaps/ShowcaseEventMap.md for the authoritative
 * gain/intensity table).
 *
 * When the possessed pawn is an AHapbeatShowcaseCharacter the blaster is put in
 * its hand mount at Unity's CameraFollowMount offset and shots leave from the
 * blaster's muzzle; without such a pawn the zone fires from its own origin, so
 * it still works dropped into a bare level. There is no stand model -- Unity has
 * none, the blaster is camera-mounted there too.
 *
 * ONE BOARD, NOT TWO: Unity's Z5 has a single TargetBoard, and light / heavy is
 * a property of the PROJECTILE that hits it, not of two separate targets. The
 * board therefore carries TWO collision triggers, one filtered on each
 * projectile tag, and flashes the colour of whichever landed.
 *
 * HOLD THE LEFT MOUSE BUTTON to charge (Unity ChargeShooter's LMB hold):
 *   - press:   starts the z5_charge_loop StreamClip loop (baseline =
 *              entry.GetEffectiveGain(), initial modulator = the charge
 *              curve at t=0 => silent start, race-free).
 *   - held:    each Tick, chargeT = clamp01((now - pressTime) / MaxChargeSeconds);
 *              LoopPlayback->ApplyGainModulation(curve(chargeT)). The curve is
 *              FMath::SmoothStep(0,1,chargeT) -- see FireOneShotEntry doc /
 *              .cpp comment for why this is byte-for-byte Unity's
 *              AnimationCurve.EaseInOut(0,0,1,1).
 *              Crossing HeavyThreshold (default 0.7) fires z5_charge_thd once.
 *   - release: Stop() the loop handle, then Subsystem->StopStreamWithFlush()
 *              (parity with Unity ChargeShooter.Release()); after
 *              ShotDelayAfterLoop seconds (default 0.05, via FTimerManager --
 *              mirrors Unity's _shotDelayAfterLoop, which exists so the
 *              shot's STREAM_BEGIN doesn't collide with the loop-stop flush
 *              burst on the device) fires z5_shot_light or z5_shot_heavy
 *              (chargeT >= HeavyThreshold picks heavy) and spawns a
 *              projectile toward the targets.
 *
 * On projectile-target overlap, the hit target's own
 * UHapbeatCollisionTriggerComponent (BeginOverlap + Fixed gain + a Light/Heavy
 * ActorTag TagFilter) fires z5_tar_hit_light / z5_tar_hit_heavy -- mirroring
 * Unity's TargetReceiver (tag-based light/heavy distinction on the incoming
 * projectile), minus the cosmetic material-flash swap (no binary material
 * assets in this sample; see the class .cpp / handoff note "uncertainties").
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ5ChargeShotActor : public AActor, public IHapbeatShowcaseZone
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ5ChargeShotActor();

	// ---- IHapbeatShowcaseZone ----
	virtual FText GetZoneLabel() const override;
	virtual TArray<FHapbeatShowcaseHudCommand> GetHudCommands() const override;
	virtual FTransform GetPlayerSpawnRelative() const override;
	virtual int32 GetZoneIndex() const override { return 5; }
	virtual void OnZoneActivated() override;
	virtual void OnZoneDeactivated() override;

	/**
	 * CAPTURE AID, not gameplay: put one projectile in front of the muzzle and
	 * leave it there, so its size and which way its nose points can be checked in
	 * a screenshot. A real shot crosses the frame in a few frames, which is why
	 * this exists at all. The spawned actor does not move, has collision off (it
	 * therefore fires no target haptics) and removes itself after 10 seconds.
	 *
	 * Placed 150 cm in front of the player at a fixed 120 cm above the zone
	 * floor -- from the PLAYER's view yaw, not the muzzle's, so the pose does not
	 * move when the blaster does -- with the heavy one another 60 cm to the right
	 * so both can be posed side by side in one shot.
	 *
	 * @param YawOffsetDeg  Added to the view yaw. 0 photographs the projectile
	 *                      nose-on (which says nothing about its roll); 90 turns
	 *                      it broadside, which is the shot that shows whether the
	 *                      nose points the way it flies.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Showcase")
	void SpawnProjectilePreview(bool bHeavy, float YawOffsetDeg = 0.0f);

	/**
	 * CAPTURE AID, not gameplay: park the charge bar at T (0..1) so it can be
	 * photographed part-way -- above HeavyThreshold, the shot that shows the bar
	 * has actually changed colour.
	 *
	 * Writes the displayed value only. It starts no charge, fires nothing and is
	 * overwritten by the next real charge frame, so it cannot be mistaken for a
	 * way to charge the blaster from script. Held (there is no charge running to
	 * update it) until something else moves it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Showcase")
	void DebugSetChargeForCapture(float T);

	/** Charge fraction (0..1) at/above which a shot / hit counts as "heavy". Mirrors Unity's _heavyThreshold (default 0.7). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeavyThreshold = 0.7f;

	/** Seconds of holding LMB to reach full charge (chargeT = 1). Unity Showcase.unity's _maxChargeSeconds = 2. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase", meta = (ClampMin = "0.1"))
	float MaxChargeSeconds = 2.0f;

	/** Projectile launch speed (cm/s) at full charge. Mirrors Unity's _maxLaunchSpeed (18 m/s = 1800 cm/s, UE uses centimeters). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase", meta = (ClampMin = "0.0"))
	float MaxLaunchSpeed = 1800.0f;

	/** Projectile visual scale at chargeT = 0. Mirrors Unity's _minChargeScale (default 0.5). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase", meta = (ClampMin = "0.05"))
	float MinChargeScale = 0.5f;

	/** Projectile visual scale at chargeT = 1. Mirrors Unity's _maxChargeScale (default 1.5). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase", meta = (ClampMin = "0.05"))
	float MaxChargeScale = 1.5f;

	/** Delay (seconds) between the loop-stop flush and the shot one-shot. Mirrors Unity's _shotDelayAfterLoop (default 0.05). 0 = fire immediately. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float ShotDelayAfterLoop = 0.05f;

	/**
	 * Where the blaster sits in the camera's space, from the Unity Showcase's
	 * CameraFollowMount on Blaster: _localPosition (0.3, -0.15, 0.8) m converted
	 * to UE centimetres. Its _localEulerAngles are all zero, which is why there
	 * is no converted-euler field here to match Z3's.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	FVector BlasterMountCameraOffsetCm = FVector(80.0f, 30.0f, -15.0f);

	/**
	 * Muzzle position relative to the blaster's mount point, in the AIM frame
	 * (forward, right, up), i.e. measured along the view axes rather than along
	 * the mount component's own -- the latter carries the mesh-correction
	 * rotation and points backwards for SM_BlasterG. See GetMuzzleTransform.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	FVector MuzzleLocalOffsetCm = FVector(32.6f, 0.0f, 7.6f);

	/**
	 * Extra rotation on top of the mount pose, correcting for whichever way
	 * SM_BlasterG's authored axes point. Left at zero: the mesh's longest axis is
	 * aligned forward automatically, so this is only here for a model that needs
	 * hand-correcting.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	FRotator BlasterMountExtraRotation = FRotator::ZeroRotator;

	/**
	 * Turn the aligned blaster 180 degrees about its up axis before the mount
	 * pose is applied. ComputeLongestAxisToForwardRotation only guarantees that
	 * the mesh's LONGEST axis lies along +X -- not which END of it points that
	 * way -- and SM_BlasterG's muzzle sits on the negative side of that axis, so
	 * without this the blaster is held back-to-front. Default true for that
	 * mesh; clear it for a model whose nose already points +X after alignment.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	bool bFlipBlasterForward = true;

	/** Finished target-board size, largest dimension first: 180 x 180 face, 53 deep. Unity's instance size. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	FVector TargetSizeCm = FVector(180.0f, 180.0f, 53.0f);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Resolve the EventMap (asset or fallback) and cache the 6 entry ids (charge_loop / charge_thd / shot_light / shot_heavy / tar_hit_light / tar_hit_heavy). */
	void BuildEventMap();

	/** Build the transient EventMap used when no asset is assigned. */
	UHapbeatEventMap* BuildFallbackEventMap();

	/** EnableInput on the first PlayerController found, then bind the mouse button (pressed / released). Warns (no-op) if none exists. */
	void BindInput();

	/** Put SM_BlasterG in the player's hand mount; no-op without an AHapbeatShowcaseCharacter or the mesh. */
	void MountBlasterOnCharacter();

	/** Take the blaster back out of the player's hand (the character outlives this zone). */
	void UnmountBlaster();

	/**
	 * One deferred attempt at MountBlasterOnCharacter(), made on the first Tick
	 * that has a player pawn to look at. WHY NOT IN BeginPlay: a zone spawned by
	 * the Showcase switcher can begin play before the pawn is possessed (the
	 * switcher retries its own spawn-pose apply in Tick for the same reason), and
	 * an attempt made too early would leave the player empty-handed.
	 */
	void TryDeferredMount();

	/** Load the projectile meshes / SFX this zone uses; whatever is missing simply stays null. */
	void LoadShowcaseAssets();

	/** Build the charge bar and add it to the viewport; removed again in EndPlay. */
	void CreateChargeBar();
	/** Take the charge bar back out of the viewport. Safe when it was never created. */
	void DestroyChargeBar();

	/** Where a shot starts and which way it goes: the player's view, or this actor's stand. */
	FTransform GetMuzzleTransform() const;

	/** Hand the child target board its size, materials, SFX and the two EventMap entries (light / heavy). */
	void SetUpTarget();

	void HandleChargeBegin();   // left mouse down
	void HandleChargeRelease(); // left mouse up

	/** Timer callback: fires the light/heavy shot ShotDelayAfterLoop seconds after Release(). */
	void FireShotAfterDelay();

	/** Resolve an arbitrary entry id from EventMap and fire it as a StreamClip one-shot at its effective gain (initial modulator = 1). No-op (warn) if unresolvable. */
	void FireOneShotEntry(const FGuid& EntryId);

	/** Spawn a projectile actor from the muzzle point toward the targets, scaled/sped by chargeT. */
	void SpawnProjectile(float ChargeT, bool bHeavy);

	/** GetGameInstance()->GetSubsystem<UHapbeatSubsystem>(), null-guarded. */
	UHapbeatSubsystem* ResolveSubsystem() const;

	/** Fixed on-screen-message keys, offset into the 500s so they don't collide with other zones' HUD lines. */
	static constexpr int32 KeyGuideHudLineKey = 500;
	static constexpr int32 StatusHudLineKey = 501;
	static constexpr float HudRefreshIntervalSeconds = 0.1f;

	// ---- Constructor-created default subobjects: the editable scene ----

	/**
	 * The target board, as a child actor so it owns its own collision triggers
	 * (which bind to their owner's root primitive) while keeping an editable,
	 * saved relative transform. Unity TargetBoard/target-large.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UChildActorComponent> TargetSlot;

	// BeginPlay-time transient data (built fresh each Play session; not serialized).

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

	/**
	 * Strong references keeping the 6 stream-clip WAVs alive when the code-built
	 * fallback map is in use -- entries only hold them via a TSoftObjectPtr (not
	 * GC-strong; see FHapbeatSampleLibrary::LoadSampleClip's GC note), so this
	 * actor-owned array is what actually keeps them resident. Left empty when the
	 * EM_Showcase asset supplies the clips.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHapbeatClip>> LoadedClips;

	/** Cached TargetSlot->GetChildActor(). */
	UPROPERTY(Transient)
	TObjectPtr<AHapbeatShowcaseZ5TargetActor> Target;

	// Optional imported art / SFX; null = keep the primitive or stay silent.

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ProjectileMeshLight;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ProjectileMeshHeavy;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ChargeLoopSound;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ShotLightSound;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ShotHeavySound;

	/** The charge loop's audio voice while the button is held; stopped on release. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ChargeAudio;

	/** The character carrying the blaster, when there is one. */
	TWeakObjectPtr<AHapbeatShowcaseCharacter> MountedCharacter;

	/** True once the deferred mount attempt has been made (successful or not). */
	bool bMountAttempted = false;

	/** The viewport-hosted charge bar, owned for this zone's lifetime. */
	TSharedPtr<SWidget> ChargeBarWidget;

	FGuid ChargeLoopEntryId;
	FGuid ChargeThresholdEntryId;
	FGuid ShotLightEntryId;
	FGuid ShotHeavyEntryId;
	FGuid TarHitLightEntryId;
	FGuid TarHitHeavyEntryId;

	/** Handle to the active z5_charge_loop playback while charging; cleared on Release(). */
	TWeakObjectPtr<UHapbeatStreamPlayback> LoopPlayback;

	bool bCharging = false;
	/** World time (seconds) at which V was pressed. */
	double ChargeStartSeconds = 0.0;
	/** True once the current charge has crossed HeavyThreshold (so z5_charge_thd fires only once per charge). */
	bool bThresholdReached = false;
	/** Most recent chargeT (0..1); also shown on the HUD. */
	float LastChargeT = 0.0f;

	/** Pending delayed-shot state, captured at Release() for the FireShotAfterDelay timer callback. */
	FTimerHandle ShotDelayTimer;
	bool bPendingHeavyShot = false;

	/** Counts down to 0 to throttle the HUD refresh. */
	float HudRefreshTimer = 0.0f;
};

/**
 * THE target board for Z5 -- Unity's single TargetBoard, not one board per
 * projectile type. It carries TWO UHapbeatCollisionTriggerComponents (both
 * BeginOverlap + Fixed gain), one filtered on each projectile tag, so a light
 * bullet and a heavy missile landing on the same board fire different entries.
 * EventMap / EntryIds / materials / SFX are handed over by
 * AHapbeatShowcaseZ5ChargeShotActor::SetUpTarget.
 *
 * Kinematic: no physics simulation, query-only collision. Nothing pushes it, and
 * the projectiles sweep THROUGH it to generate the overlap.
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ5TargetActor : public AActor
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ5TargetActor();

	/** Fires z5_tar_hit_light; TagFilter "ProjectileLight". Wired by the zone. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatCollisionTriggerComponent> LightHitTrigger;

	/** Fires z5_tar_hit_heavy; TagFilter "ProjectileHeavy". Wired by the zone. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatCollisionTriggerComponent> HeavyHitTrigger;

	/**
	 * Turn the aimed board a further 180 degrees about its up axis, so the
	 * PRINTED face looks at the player rather than its back.
	 *
	 * ComputeShortestAxisToDirectionRotation only brings the board's depth axis
	 * onto the given direction; which of the two faces that leaves forward
	 * depends on which end of that axis the model calls positive, and
	 * target-large.fbx puts its back there. True for that model; kept editable so
	 * a replacement can be corrected without a code change.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat")
	bool bFlipTargetFacing = true;

	/**
	 * Give the board its finished size, its look and its sounds. Every asset
	 * argument is optional (null keeps the primitive / stays silent), which is
	 * what makes the zone survive a checkout without the generated art.
	 *
	 * @param SizeCm         Finished size, largest dimension first: 180 x 180 x 53.
	 * @param FaceDirection  World direction the board's face should look along
	 *                       (towards the player).
	 * @param InBaseMaterial The resting look.
	 * @param InLightFlash   Shown for FlashSeconds after a light hit (Unity TargetReceiver's material swap).
	 * @param InHeavyFlash   The same for a heavy hit.
	 * @param InLightSound   One-shot played on a light hit.
	 * @param InHeavySound   One-shot played on a heavy hit.
	 */
	void ApplyShowcaseAssets(const FVector& SizeCm, const FVector& FaceDirection,
		UMaterialInterface* InBaseMaterial, UMaterialInterface* InLightFlash, UMaterialInterface* InHeavyFlash,
		USoundBase* InLightSound, USoundBase* InHeavySound);

	/** Cancel a flash in progress and put the base material back (leaving the zone). */
	void ResetLook();

protected:
	virtual void BeginPlay() override;

private:
	/**
	 * Visual + audio half of a hit. The haptic half is the two triggers' own, so
	 * this repeats their tag test rather than depending on it -- they are
	 * independent subscribers to the same overlap, exactly as Unity splits
	 * TargetReceiver (flash + SFX) from the haptic trigger.
	 */
	UFUNCTION()
	void HandleTargetOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Put the base material back once the flash has run its course. */
	void EndFlash();

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UStaticMeshComponent> TargetMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LightFlashMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> HeavyFlashMaterial;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> LightHitSound;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> HeavyHitSound;

	FTimerHandle FlashTimer;

	/** Unity TargetReceiver._flashSeconds. */
	static constexpr float FlashSeconds = 0.2f;
};

/**
 * A short-lived, manually-moved projectile for Z5. Not a
 * UProjectileMovementComponent user -- it moves itself via a swept
 * AddActorWorldOffset in Tick() so overlap events are generated against the
 * (QueryOnly, non-simulating) target boards without needing either side to
 * simulate rigid-body physics. Self-destructs after a fixed lifespan
 * (mirrors Unity's `Destroy(projectile.gameObject, 4f)`), independent of
 * whether it hit anything.
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ5ProjectileActor : public AActor
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ5ProjectileActor();

	/**
	 * Configure the projectile right after SpawnActor. Safe to call after
	 * BeginPlay has already run synchronously inside SpawnActor (BeginPlay
	 * here only sets the lifespan, which does not depend on these values --
	 * movement/tagging reads them starting next Tick).
	 *
	 * @param InVelocity World-space velocity (cm/s). Movement is Tick-integrated.
	 * @param bInHeavy   Tags the actor "ProjectileHeavy" (else "ProjectileLight"),
	 *                   read by AHapbeatShowcaseZ5TargetActor's HitTrigger TagFilter.
	 * @param InScale    Charge-driven scale multiplier (visual only; no gameplay effect).
	 * @param InMesh     SM_BulletFoam / SM_Missile, or null to keep the sphere.
	 * @param InBaseLengthCm  Finished length of the mesh's longest axis at scale 1.
	 * @param InMeshExtraRotation  Stored into ProjectileMeshExtraRotation and
	 *                      applied on top of the alignment -- the per-mesh nose
	 *                      correction, which the spawner knows and this actor
	 *                      cannot (one class, two models).
	 *
	 * The mesh is also turned so its longest axis points along InVelocity, so a
	 * missile flies nose-first instead of sideways.
	 */
	void Configure(const FVector& InVelocity, bool bInHeavy, float InScale,
		UStaticMesh* InMesh = nullptr, float InBaseLengthCm = 0.0f,
		const FRotator& InMeshExtraRotation = FRotator::ZeroRotator);

	/**
	 * Turn the aligned mesh 180 degrees about its up axis, for a projectile
	 * model whose nose ends up pointing backwards. Same limitation as the
	 * blaster's flip: alignment only picks the AXIS, not which end leads. Left
	 * false -- SM_BulletFoam and SM_Missile both read nose-first as imported --
	 * and kept editable so that can be checked rather than assumed.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat")
	bool bFlipForward = false;

	/**
	 * Extra rotation on the aligned mesh, in the actor's space.
	 *
	 * Longest-axis alignment leaves the model free to ROLL and YAW about that
	 * axis however the source file happened to lie, which is how SM_BulletFoam
	 * ended up flying 90 degrees off. Distinct from bFlipForward, which is only
	 * the two-way "which end leads" question. Configure() overwrites this with
	 * the value the spawner passes (the per-model correction lives there, since
	 * one class serves both models); the property matters for a projectile placed
	 * or subclassed by hand.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat")
	FRotator ProjectileMeshExtraRotation = FRotator::ZeroRotator;

	/**
	 * Gravity applied to the flight, cm/s^2 along -Z, as a multiple of the
	 * world's own gravity. Unity's projectiles are plain Rigidbodies with gravity
	 * on (bullet 0.2 kg, missile 1 kg), so a UE shot that flew dead straight was
	 * the odd one out. Mass does not enter into it -- gravity is an acceleration.
	 * 0 restores the straight-line flight.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat", meta = (ClampMin = "0.0"))
	float GravityScale = 1.0f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UStaticMeshComponent> ProjectileMesh;

	FVector Velocity = FVector::ZeroVector;
};
