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
class UMaterialInterface;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;
class SWidget;
class AHapbeatShowcaseCharacter;
class AHapbeatShowcaseZ5TargetActor;
class AHapbeatShowcaseZ5ProjectileActor;

/**
 * Z5 Target Range -- a blaster stand + 2 targets (light / heavy), driven
 * entirely IMPERATIVELY (no HapbeatTriggerComponent on the stand itself),
 * matching Unity's ChargeShooter.cs design intent exactly: "other zones wire
 * declarative HapbeatXxxTrigger components; Z5 deliberately calls the SDK API
 * straight from script so both patterns are shown side by side." Reuses
 * showcase-kit verbatim (6 events -- see
 * Samples~/Showcase/EventMaps/ShowcaseEventMap.md for the authoritative
 * gain/intensity table).
 *
 * When the possessed pawn is an AHapbeatShowcaseCharacter the blaster is put in
 * its hand mount and shots leave along the view direction (Unity
 * CameraFollowMount + ChargeShooter._muzzle); without such a pawn the zone fires
 * from its own stand, so it still works dropped into a bare level.
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

	/**
	 * Local offset applied to this zone's own root (mesh + spawned targets +
	 * projectile muzzle point) at BeginPlay, so a future master/layout actor
	 * can nudge each zone into a row slot without altering the zone actor's
	 * own placed transform.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	FVector FootprintOffset = FVector::ZeroVector;

	/** Charge fraction (0..1) at/above which a shot / hit counts as "heavy". Mirrors Unity's _heavyThreshold (default 0.7). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeavyThreshold = 0.7f;

	/** Seconds of holding V to reach full charge (chargeT = 1). Mirrors Unity's _maxChargeSeconds (default 1.5). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase", meta = (ClampMin = "0.1"))
	float MaxChargeSeconds = 1.5f;

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
	 * Extra rotation on top of the default hand-mount pose, correcting for
	 * whichever way SM_BlasterG's authored axes point. Exposed rather than
	 * hardcoded for the same reason as Z3's rod: it is a property of the
	 * imported asset and wants dialling in from the details panel.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	FRotator BlasterMountExtraRotation = FRotator::ZeroRotator;

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

	/** Spawn TargetLight / TargetHeavy in front of the stand and wire each one's HitTrigger to the matching EventMap entry + ActorTag filter. */
	void SpawnTargets();

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

	// Constructor-created default subobject (VisibleAnywhere, not Transient).
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UStaticMeshComponent> StandMesh;

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

	UPROPERTY(Transient)
	TObjectPtr<AHapbeatShowcaseZ5TargetActor> TargetLight;

	UPROPERTY(Transient)
	TObjectPtr<AHapbeatShowcaseZ5TargetActor> TargetHeavy;

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
 * One target board for Z5 -- a small static cube with a
 * UHapbeatCollisionTriggerComponent (BeginOverlap + Fixed gain). The owning
 * AHapbeatShowcaseZ5ChargeShotActor spawns 2 instances (light / heavy) and
 * assigns each one's EventMap/EntryId/TagFilter after spawn (TagFilter =
 * "ProjectileLight" or "ProjectileHeavy", matching the tag
 * AHapbeatShowcaseZ5ProjectileActor::Configure adds to itself) -- see
 * AHapbeatShowcaseZ5ChargeShotActor::SpawnTargets.
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ5TargetActor : public AActor
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ5TargetActor();

	/**
	 * The collision trigger that fires this target's z5_tar_hit_* entry.
	 * Public (not just VisibleAnywhere) so the owning zone actor can assign
	 * EventMap / EntryId / TagFilter right after SpawnActor.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatCollisionTriggerComponent> HitTrigger;

	/**
	 * Give this board the Showcase's imported look and sound, and tell it which
	 * projectile tag counts as a hit. Called by the zone right after SpawnActor;
	 * every argument is optional (null keeps the primitive / stays silent), which
	 * is what makes the zone survive a checkout without the generated art.
	 *
	 * @param Mesh           SM_TargetLarge, or null to keep the cube.
	 * @param InBaseMaterial The resting look.
	 * @param InFlashMaterial Shown for FlashSeconds after a hit (Unity TargetReceiver's material swap).
	 * @param InHitSound     One-shot played on a hit.
	 * @param InAcceptTag    Projectile actor tag this board reacts to; matches HitTrigger.TagFilter.
	 * @param DesiredLongestAxisCm Finished size of the board's longest axis.
	 */
	void ApplyShowcaseAssets(UStaticMesh* Mesh, UMaterialInterface* InBaseMaterial, UMaterialInterface* InFlashMaterial,
		USoundBase* InHitSound, FName InAcceptTag, float DesiredLongestAxisCm);

protected:
	virtual void BeginPlay() override;

private:
	/**
	 * Visual + audio half of a hit. The haptic half is HitTrigger's own, so this
	 * repeats HitTrigger's tag test rather than depending on it -- the two are
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
	TObjectPtr<UMaterialInterface> FlashMaterial;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> HitSound;

	/** Projectile tag that counts as a hit on this board. */
	FName AcceptTag;

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
	 */
	void Configure(const FVector& InVelocity, bool bInHeavy, float InScale,
		UStaticMesh* InMesh = nullptr, float InBaseLengthCm = 0.0f);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UStaticMeshComponent> ProjectileMesh;

	FVector Velocity = FVector::ZeroVector;
};
