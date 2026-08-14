// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HapbeatShowcaseZ3FishingActor.generated.h"

class AStaticMeshActor;
class UHapbeatClip;
class UHapbeatEventMap;
class UHapbeatParameterBinding;
class UHapbeatSequenceComponent;
class UStaticMeshComponent;

/**
 * Z3 Fishing -- the physics-heavy Showcase zone. A rod dangles a line with a
 * fixed max length over a water plane; a "shark" swims freely nearby (periodic
 * wander impulses -- see UpdateSharkWander). Pressing H hooks it: while the
 * line is taut (distance to the rod tip >= MaxLineLength) the SAME line-
 * tension model as the Unity SDK's FishingController.cs applies -- the rod
 * tip's own velocity transfers into the shark as inertia, and the shark is
 * pulled back toward the max-length sphere (see UpdateHookedLinePhysics).
 * Pressing H again, or the line breaking under too much tension
 * (Dist - MaxLineLength > BreakDistance), releases it.
 *
 * Haptics: a UHapbeatSequenceComponent (3-phase: hook-start one-shot / hook
 * loop / hook-release one-shot) plus a UHapbeatParameterBinding
 * (VelocityMagnitude -> StreamGain) both live ON THE SHARK ACTOR -- not this
 * zone actor -- because UHapbeatParameterBinding reads its OWNER's root
 * component velocity (see HapbeatParameterBinding.cpp ReadSourceValue), so
 * the binding only sees the shark's motion if it is actually attached to the
 * shark; UHapbeatTriggerComponent::PreSeedBindings() also only looks at
 * GetOwner()'s components, so the sequence trigger has to share that same
 * owner for the pre-seed-on-stream-start call to reach the binding. All three
 * events + gains + the binding's numbers are taken verbatim from
 * Samples~/Showcase/EventMaps/ShowcaseEventMap.md (Z3_hook_start /
 * Z3_hook_loop / Z3_hook_release) and showcase-kit-manifest.json.
 *
 * Physics approach chosen: a manual, velocity-domain port of
 * FishingController.cs's FixedUpdate (see UpdateHookedLinePhysics), not a
 * UPhysicsConstraintComponent -- UE has no built-in radial/rope distance
 * joint (a constraint's linear limits are per-axis, not per-radius) and the
 * correction must be conditional on "taut", which a standing constraint
 * can't express. The rod-tip-inertia term is a literal, frame-rate-
 * normalized port of Unity's tuned constant (_rodInertiaFactor); the spring
 * pull-back and radial damping are a from-scratch Hooke's-law restoring force
 * (the "manual spring in Tick" alternative offered instead) since Unity's
 * position-domain Lerp doesn't translate to a UE SimulatePhysics body without
 * fighting the solver.
 *
 * Unit note: FishingController.cs's tunables are authored in Unity's meters
 * (1 unit = 1 m); UE's default world scale is 1 unit = 1 cm. Every
 * distance/speed constant below is the Unity value x100 (documented per
 * field); dimensionless ratios (RodInertiaFactor) are unchanged.
 *
 * Audio is intentionally out of scope (haptics only) -- see
 * AHapbeatBasicExampleActor's class comment for the same USoundWave-import
 * rationale. Visuals are engine-primitive meshes (Cube/Sphere/Cylinder), per
 * the design doc's Faithfulness ledger -- the shark is a scaled Cube.
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ3FishingActor : public AActor
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ3FishingActor();

	/**
	 * Added to this actor's own placed transform (via the root component) in
	 * BeginPlay so a master/layout actor can space the 5 Showcase zones out in
	 * a row by setting only this field per zone, without needing a distinct
	 * spawn transform per zone (shared Showcase convention).
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	FVector FootprintOffset = FVector::ZeroVector;

	// ---- Line physics (mirrors FishingController.cs's [Header("Line physics")]; see the unit note above) ----

	/** Unity 1.2 m x100 -> 120 uu. Distance from the rod tip beyond which the line goes taut. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "30.0", ClampMax = "500.0"))
	float MaxLineLength = 120.0f;

	/** Dimensionless; unchanged from Unity's _rodInertiaFactor (fraction of rod-tip velocity transferred per reference tick). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float RodInertiaFactor = 0.25f;

	/** Unity 2 m/s x100 -> 200 uu/s. Caps the rod tip velocity transferred into the shark. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "50.0", ClampMax = "1000.0"))
	float MaxTransferSpeed = 200.0f;

	/** Linear damping applied to the shark body while hooked (restored to SwimLinearDamping on release). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float AttachedLinearDamping = 1.5f;

	/** Angular damping applied to the shark body while hooked. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float AttachedAngularDamping = 1.0f;

	/**
	 * Hooke's-law restoring accel per uu of overshoot beyond MaxLineLength
	 * (units: 1/s^2). A from-scratch constant (see class comment) -- not a
	 * literal port of Unity's position-domain 0.5 Lerp; tune live in PIE.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0"))
	float LineSpringStiffness = 18.0f;

	/**
	 * Fraction of the outward radial velocity removed per second while taut.
	 * A from-scratch constant (see class comment) -- not a literal port of
	 * Unity's per-tick 0.7 factor.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0"))
	float RadialDampingFactor = 5.0f;

	/** Extra distance (uu) beyond MaxLineLength that snaps the line and auto-releases the hook ("tension exceeds a break threshold"). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0"))
	float BreakDistance = 60.0f;

	/** Sway amplitude (uu) for the rod tip's idle motion -- gives the taut line rod-tip velocity to transfer even with no player input. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0"))
	float RodTipSwayAmplitude = 40.0f;

	// ---- Shark "swims" behavior (not in FishingController.cs -- this zone's own addition per the master spec: "swims (randomized wander force in Tick)") ----

	/** Speed (uu/s) kicked into the shark's velocity by each periodic wander impulse. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark")
	float WanderImpulseSpeed = 90.0f;

	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark", meta = (ClampMin = "0.1"))
	float WanderIntervalMinSeconds = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark", meta = (ClampMin = "0.1"))
	float WanderIntervalMaxSeconds = 3.5f;

	/** Ambient (unhooked) linear damping -- lighter than AttachedLinearDamping so the shark swims freely. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark")
	float SwimLinearDamping = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark")
	float SwimAngularDamping = 0.4f;

	/** Radius (uu) around the shark's spawn point beyond which an unhooked shark gets gently pulled home (keeps the random walk bounded). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark", meta = (ClampMin = "0.0"))
	float HomeLeashRadius = 450.0f;

	/** Accel (uu/s^2) of the gentle pull-home when unhooked and beyond HomeLeashRadius. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark", meta = (ClampMin = "0.0"))
	float HomeLeashAccel = 60.0f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	// ---- setup (BeginPlay) ----

	/** Assign static meshes + best-effort color tints to the 3 decorative components created in the constructor. */
	void SetupVisuals();

	/** Spawn the shark as its own AStaticMeshActor (see class comment for why it must be a separate actor). */
	void SpawnShark();

	/** Resolve the EventMap (asset or fallback), then attach UHapbeatSequenceComponent + UHapbeatParameterBinding to the shark. */
	void BuildEventMapAndHaptics();

	/** Build the transient EventMap used when no asset is assigned. */
	UHapbeatEventMap* BuildFallbackEventMap();

	/** EnableInput on the first PlayerController found (mirrors AHapbeatBasicExampleActor's pattern) and bind H. */
	void BindInput();

	void HandleHKey();

	/** Toggle the hook: fires/stops the sequence, swaps the shark's damping, and (on hook) snaps it to tether range. No-op if already in that state. */
	void SetHooked(bool bNewHooked);

	// ---- per-tick simulation ----

	/** Sway RodTipMeshComp's relative location and derive RodTipVelocity from the frame-to-frame world-position delta. */
	void UpdateRodTipSway(float DeltaSeconds);

	/** Periodic wander impulses (always) + a gentle unhooked pull back toward SharkHomeWorldLocation beyond HomeLeashRadius. */
	void UpdateSharkWander(float DeltaSeconds);

	/** The taut-line tension model (see class comment); a no-op when the line is currently slack. */
	void UpdateHookedLinePhysics(float DeltaSeconds);

	/** DrawDebugLine from the rod tip to the shark (or a default hang-down point when unhooked) -- parity with FishingController.cs's LineRenderer. */
	void DrawLineVisual() const;

	void RefreshHud(float DeltaSeconds);

	// ---- decorative visuals (root-relative; move together with FootprintOffset) ----

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UStaticMeshComponent> WaterMeshComp;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UStaticMeshComponent> RodBaseMeshComp;

	/** Kinematic marker at the rod's tip; its code-driven sway each Tick doubles as the "rod tip" physics reference point. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UStaticMeshComponent> RodTipMeshComp;

	/** RodTipMeshComp's relative location before the per-frame sway offset is added. */
	FVector RodTipBaseRelativeLocation = FVector(-600.0f, -350.0f, 300.0f);

	/** Local (root-relative) spawn offset for the shark actor -- converted to a world location via the root's component transform in SpawnShark(). */
	FVector SharkLocalSpawnOffset = FVector(200.0f, 300.0f, -10.0f);

	// ---- the shark (a separate actor -- see class comment) ----

	UPROPERTY(Transient)
	TObjectPtr<AStaticMeshActor> SharkActor;

	/** Cached SharkActor->GetStaticMeshComponent() for the per-tick physics update. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SharkMeshComp;

	UPROPERTY(Transient)
	TObjectPtr<UHapbeatSequenceComponent> HookSequenceComp;

	UPROPERTY(Transient)
	TObjectPtr<UHapbeatParameterBinding> HookVelocityBinding;

	// ---- haptics data ----

	/**
	 * The EventMap this zone plays from. Defaults to the plugin's shipped
	 * EM_Showcase asset (assigned in the constructor), so the gains / modes the
	 * zone actually uses are visible and editable in the editor instead of being
	 * buried in code -- that is how a real project works. Point it at your own
	 * asset to re-author them; clear it and the zone builds an equivalent map in
	 * code, so the sample still runs if the asset ever goes missing.
	 *
	 * Entries are resolved by event name, not by order (see BuildEventMapAndHaptics).
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatEventMap> EventMapOverride;

	UPROPERTY(Transient)
	TObjectPtr<UHapbeatEventMap> EventMap;

	// Strong refs keeping the 3 StreamClip WAVs alive when the code-built
	// fallback map is in use; left null when the EM_Showcase asset supplies them.

	UPROPERTY(Transient)
	TObjectPtr<UHapbeatClip> HookStartClip;

	UPROPERTY(Transient)
	TObjectPtr<UHapbeatClip> HookLoopClip;

	UPROPERTY(Transient)
	TObjectPtr<UHapbeatClip> HookReleaseClip;

	// ---- runtime state ----

	bool bHooked = false;
	float ElapsedTimeSeconds = 0.0f;
	float TimeToNextWanderImpulse = 0.0f;
	FVector PrevRodTipWorldPos = FVector::ZeroVector;
	FVector RodTipVelocity = FVector::ZeroVector;
	FVector SharkHomeWorldLocation = FVector::ZeroVector;

	/**
	 * Fixed on-screen-message keys (see AHapbeatBasicExampleActor -- it already
	 * uses 0/1). Cross-zone HUD key coordination isn't specified by the shared
	 * Showcase conventions, so this zone assumes a `zone_number * 100 + index`
	 * scheme; confirm the other 4 zone actors agree, or have the level-
	 * integration pass assign key ranges centrally.
	 */
	static constexpr int32 KeyGuideHudLineKey = 300;
	static constexpr int32 StatusHudLineKey = 301;
	static constexpr float HudRefreshIntervalSeconds = 0.25f;
	float HudRefreshTimer = 0.0f;
};
