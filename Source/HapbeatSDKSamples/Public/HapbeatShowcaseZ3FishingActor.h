// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HapbeatShowcaseZone.h" // IHapbeatShowcaseZone: the switcher asks the zone for its label / keys / spawn
#include "HapbeatShowcaseZ3FishingActor.generated.h"

class AHapbeatShowcaseCharacter;
class AStaticMeshActor;
class UHapbeatClip;
class UHapbeatEventMap;
class UHapbeatParameterBinding;
class UHapbeatSequenceComponent;
class UStaticMeshComponent;

/**
 * Z3 Fishing -- the physics-heavy Showcase zone. A rod dangles a line with a
 * fixed max length over a water plane, and a shark hangs in the water nearby.
 * HOLDING THE LEFT MOUSE BUTTON hooks it and RELEASING lets go -- Unity
 * FishingController.cs's LMB-hold, not a toggle. While the line is taut
 * (distance to the rod tip >= MaxLineLength) the SAME line-tension model as
 * FishingController.cs applies: the rod tip's own velocity transfers into the
 * shark as inertia, and the shark is pulled back toward the max-length sphere
 * (see UpdateHookedLinePhysics).
 *
 * When the possessed pawn is an AHapbeatShowcaseCharacter the rod is put in its
 * hand mount (Unity CameraFollowMount) and the rod tip -- the anchor the whole
 * tension model hangs off -- rides with the view. Without such a pawn the zone
 * falls back to its own rod props standing in the scene, so it still works when
 * dropped into a bare level.
 *
 * Three behaviours this zone once had unconditionally (target wander, rod-tip
 * sway, line breaking) are UE-side additions Unity does not have, and are now
 * off by default -- see the bEnable* switches.
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
 * Audio: none, matching Unity's Z3, which has no SFX either -- the only thing
 * this zone makes is haptics. Visuals use the Showcase's imported SM_Shark /
 * SM_FishingRod when that optional content is present and fall back to engine
 * primitives (the shark becomes a scaled Cube) when it is not.
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ3FishingActor : public AActor, public IHapbeatShowcaseZone
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ3FishingActor();

	// ---- IHapbeatShowcaseZone ----
	virtual FText GetZoneLabel() const override;
	virtual TArray<FHapbeatShowcaseHudCommand> GetHudCommands() const override;
	virtual FTransform GetPlayerSpawnRelative() const override;

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

	/** Extra distance (uu) beyond MaxLineLength that snaps the line and auto-releases the hook. Off by default -- see bEnableLineBreak. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0", EditCondition = "bEnableLineBreak"))
	float BreakDistance = 60.0f;

	/** Sway amplitude (uu) for the rod tip's idle motion. Off by default -- see bEnableRodTipSway. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0", EditCondition = "bEnableRodTipSway"))
	float RodTipSwayAmplitude = 40.0f;

	// ---- UE-only extras, all OFF by default ----
	//
	// Unity's Z3 is deliberately plainer than this zone grew to be: the rod is
	// held by the player, the target just hangs there, and nothing snaps. These
	// three switches are UE-side additions that used to be always on; they are
	// kept (they are genuinely nicer to look at when the zone is placed on its
	// own without a player) but default to false so the shipped Showcase behaves
	// exactly like Unity's. Turn them on per instance in the details panel.

	/** Idle wander impulses on the target. Unity's target is inert until you pull it. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Extras")
	bool bEnableSharkWander = false;

	/** Procedural rod-tip sway. Unity's rod moves only because the player's hand moves. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Extras")
	bool bEnableRodTipSway = false;

	/** Auto-release when the line is overstretched. Unity has no line-break rule. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Extras")
	bool bEnableLineBreak = false;

	/**
	 * Extra rotation applied on top of the default hand-mount pose, to correct
	 * for whichever way SM_FishingRod's authored axes point. Exposed rather than
	 * hardcoded because the correction is a property of the imported asset, and
	 * it can be dialled in from the details panel without a rebuild.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Rod")
	FRotator RodMountExtraRotation = FRotator::ZeroRotator;

	/**
	 * Rod-tip position in the mounted rod's local space. Left at zero (the
	 * default) the tip is derived from SM_FishingRod's bounds: the far end along
	 * the mesh's longest axis. Set it non-zero to override that guess.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Rod")
	FVector RodTipLocalOffsetOverride = FVector::ZeroVector;

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

	/** EnableInput on the first PlayerController found (mirrors AHapbeatBasicExampleActor's pattern) and bind the mouse button. */
	void BindInput();

	/**
	 * Put SM_FishingRod in the player's hand mount and hide this zone's own rod
	 * props, so the rod follows the view the way Unity's CameraFollowMount does.
	 * No-op (props stay visible, rod stays where the zone put it) when the
	 * possessed pawn is not an AHapbeatShowcaseCharacter or the mesh is absent.
	 */
	void MountRodOnCharacter();

	/**
	 * One deferred attempt at MountRodOnCharacter(), made on the first Tick that
	 * has a player pawn to look at. WHY NOT IN BeginPlay: a zone spawned by the
	 * Showcase switcher can begin play before the pawn is possessed (the switcher
	 * has the same race for the spawn pose, which is why it retries in Tick), and
	 * a mount attempt made too early would silently leave the rod on the floor.
	 */
	void TryDeferredMount();

	/** World position of the rod's tip: the mounted rod's far end, or the zone's own tip marker. */
	FVector GetRodTipWorldLocation() const;

	void HandleFirePressed();  // left mouse down -- hook
	void HandleFireReleased(); // left mouse up -- release

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

	/** The character carrying the rod, when there is one; drives GetRodTipWorldLocation(). */
	TWeakObjectPtr<AHapbeatShowcaseCharacter> MountedCharacter;

	/** True once the deferred mount attempt has been made (successful or not). */
	bool bMountAttempted = false;

	/** Rod-tip offset in the mounted rod's local space (bounds-derived, or the override). */
	FVector RodTipLocalOffset = FVector::ZeroVector;

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
