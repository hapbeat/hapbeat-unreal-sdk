// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HapbeatShowcaseZone.h" // IHapbeatShowcaseZone: the switcher asks the zone for its label / keys / spawn
#include "HapbeatShowcaseZ3FishingActor.generated.h"

class AHapbeatShowcaseCharacter;
class AHapbeatShowcaseZ3SharkActor;
class UCapsuleComponent;
class UChildActorComponent;
class UHapbeatClip;
class UHapbeatEventMap;
class UHapbeatParameterBinding;
class UHapbeatSequenceComponent;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;

/**
 * Z3 Fishing -- the physics-heavy Showcase zone. The player holds a rod (put in
 * the character's hand mount, exactly where Unity's CameraFollowMount puts it)
 * and a shark hangs in front of them. HOLDING THE LEFT MOUSE BUTTON hooks it and
 * RELEASING lets go -- Unity FishingController.cs's LMB-hold, not a toggle.
 * While the line is taut (distance to the rod tip >= MaxLineLength) the SAME
 * line-tension model as FishingController.cs applies: the rod tip's own velocity
 * transfers into the shark as inertia, and the shark is pulled back toward the
 * max-length sphere (see UpdateHookedLinePhysics).
 *
 * Without an AHapbeatShowcaseCharacter to hold the rod (the zone dropped into a
 * bare level, or before the pawn is possessed) the line hangs from RodTipAnchor
 * instead, so the zone still works -- there is simply no rod on screen, because
 * Unity has no standing rod prop either.
 *
 * NO WATER PLANE: the Phase 2 version drew a big blue slab here. Unity's Z3 has
 * nothing of the sort -- the shark hangs in the room -- so it is gone.
 *
 * Three behaviours this zone once had unconditionally (shark wander, rod-tip
 * sway, line breaking) are UE-side additions Unity does not have, and are off by
 * default -- see the bEnable* switches.
 *
 * Haptics: a UHapbeatSequenceComponent (3-phase: hook-start one-shot / hook
 * loop / hook-release one-shot) plus a UHapbeatParameterBinding
 * (VelocityMagnitude -> StreamGain) both live ON THE SHARK ACTOR -- not this
 * zone actor -- because UHapbeatParameterBinding reads its OWNER's root
 * component velocity (see HapbeatParameterBinding.cpp ReadSourceValue), so the
 * binding only sees the shark's motion if it is actually attached to the shark;
 * UHapbeatTriggerComponent::PreSeedBindings() also only looks at GetOwner()'s
 * components, so the sequence trigger has to share that same owner for the
 * pre-seed-on-stream-start call to reach the binding. Both are default
 * subobjects of AHapbeatShowcaseZ3SharkActor for that reason, and this zone only
 * hands them their EventMap and entry ids.
 *
 * Physics approach chosen: a manual, velocity-domain port of
 * FishingController.cs's FixedUpdate (see UpdateHookedLinePhysics), not a
 * UPhysicsConstraintComponent -- UE has no built-in radial/rope distance joint
 * (a constraint's linear limits are per-axis, not per-radius) and the correction
 * must be conditional on "taut", which a standing constraint can't express.
 *
 * Unit note: FishingController.cs's tunables are authored in Unity's metres
 * (1 unit = 1 m); UE's default world scale is 1 unit = 1 cm. Every
 * distance/speed constant below is the Unity value x100 (documented per field);
 * dimensionless ratios (RodInertiaFactor) are unchanged.
 *
 * Audio: none, matching Unity's Z3, which has no SFX either -- the only thing
 * this zone makes is haptics.
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ3FishingActor : public AActor, public IHapbeatShowcaseZone
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ3FishingActor();

	// ---- IHapbeatShowcaseZone ----
	virtual int32 GetZoneIndex() const override { return 3; }
	virtual FText GetZoneLabel() const override;
	virtual TArray<FHapbeatShowcaseHudCommand> GetHudCommands() const override;
	virtual FTransform GetPlayerSpawnRelative() const override;
	virtual void OnZoneActivated() override;
	virtual void OnZoneDeactivated() override;

	// ---- Line physics (FishingController.cs's [Header("Line physics")] values, x100 for cm) ----

	/** Unity _maxLineLength 2 m -> 200 cm. Distance from the rod tip beyond which the line goes taut. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "30.0", ClampMax = "500.0"))
	float MaxLineLength = 200.0f;

	/** How thick the line is drawn. Unity's LineRenderer width is 0.02 m -> 2 cm. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.1", ClampMax = "20.0"))
	float LineDiameterCm = 2.0f;

	/** Line colour with nothing on the hook -- the blue the debug line used. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line")
	FLinearColor LineSlackColor = FLinearColor(0.05f, 0.15f, 1.0f, 1.0f);

	/** Line colour while the shark is hooked -- the green the debug line used. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line")
	FLinearColor LineHookedColor = FLinearColor(0.05f, 1.0f, 0.15f, 1.0f);

	/** Unity _rodInertiaFactor 0.15. Dimensionless: fraction of rod-tip velocity transferred per reference tick. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float RodInertiaFactor = 0.15f;

	/** Unity _maxTransferSpeed 1 m/s -> 100 cm/s. Caps the rod tip velocity transferred into the shark. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "10.0", ClampMax = "1000.0"))
	float MaxTransferSpeed = 100.0f;

	/** Unity _attachedLinearDamping 2.5, applied to the shark while hooked (restored to SwimLinearDamping on release). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float AttachedLinearDamping = 2.5f;

	/** Unity _attachedAngularDamping 0.5. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float AttachedAngularDamping = 0.5f;

	/**
	 * Hooke's-law restoring accel per uu of overshoot beyond MaxLineLength
	 * (units: 1/s^2). A from-scratch constant, not a literal port of Unity's
	 * position-domain 0.5 Lerp (which does not translate to a UE simulating body
	 * without fighting the solver); tune live in PIE.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0"))
	float LineSpringStiffness = 18.0f;

	/**
	 * Fraction of the outward radial velocity removed per second while taut.
	 * From-scratch, for the same reason as LineSpringStiffness -- Unity's is a
	 * per-tick 0.7 factor.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0"))
	float RadialDampingFactor = 5.0f;

	/** Extra distance (uu) beyond MaxLineLength that snaps the line and auto-releases the hook. Off by default -- see bEnableLineBreak. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0", EditCondition = "bEnableLineBreak"))
	float BreakDistance = 60.0f;

	/** Sway amplitude (uu) for the fallback rod anchor's idle motion. Off by default -- see bEnableRodTipSway. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Line", meta = (ClampMin = "0.0", EditCondition = "bEnableRodTipSway"))
	float RodTipSwayAmplitude = 40.0f;

	// ---- UE-only extras, all OFF by default ----
	//
	// Unity's Z3 is deliberately plainer than this zone grew to be: the rod is
	// held by the player, the shark just hangs there, and nothing snaps. These
	// three switches are UE-side additions that used to be always on; they are
	// kept (they are genuinely nicer to look at when the zone is placed on its
	// own without a player) but default to false so the shipped Showcase behaves
	// exactly like Unity's. Turn them on per instance in the details panel.

	/** Idle wander impulses on the shark. Unity's shark is inert until you pull it. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Extras")
	bool bEnableSharkWander = false;

	/** Procedural sway on the fallback rod anchor. Unity's rod moves only because the player's hand moves. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Extras")
	bool bEnableRodTipSway = false;

	/** Auto-release when the line is overstretched. Unity has no line-break rule. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Extras")
	bool bEnableLineBreak = false;

	// ---- Rod (mounted on the player, Unity CameraFollowMount) ----

	/**
	 * Where the rod sits in the camera's space, from the Unity Showcase's
	 * CameraFollowMount on Rod: _localPosition (0.44, 0.15, 1) m converted to UE
	 * centimetres.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Rod")
	FVector RodMountCameraOffsetCm = FVector(100.0f, 44.0f, 15.0f);

	/**
	 * The same component's _localEulerAngles (-17.5, 16.83, 8.92), in UNITY
	 * degrees -- converted by FHapbeatSampleLibrary::UnityEulerToUERotator so the
	 * number in the details panel is the one you can read off the Unity scene.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Rod")
	FVector RodMountUnityEulerDeg = FVector(-17.5f, 16.83f, 8.92f);

	/** Finished rod length, cm. Unity's instance works out to 35 x 389 x 25 cm. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Rod", meta = (ClampMin = "10.0"))
	float RodLengthCm = 389.0f;

	/**
	 * Extra rotation applied on top of the converted mount pose. Left at zero:
	 * the rod mesh's own axes are handled by the automatic longest-axis
	 * alignment, so this is only here for a model that needs hand-correcting.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Rod")
	FRotator RodMountExtraRotation = FRotator::ZeroRotator;

	/**
	 * Turn the aligned rod 180 degrees about its up axis before the mount pose is
	 * applied. Longest-axis alignment only decides which AXIS runs forward, not
	 * which END of it leads, so a model authored the other way round is held
	 * butt-first. True for SM_FishingRod: the PIE capture read the right way
	 * round, but in the headset the rod is held butt-forward -- the model's
	 * longest axis runs from tip to butt, which alignment alone cannot tell.
	 * Kept editable so a replacement mesh can be corrected without code. The
	 * derived rod-tip offset follows this flag, so the line still hangs from the
	 * end that is actually in front.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Rod")
	bool bFlipRodForward = true;

	/**
	 * Rod-tip position in the mounted rod's local space. Left at zero (the
	 * default) the tip is derived from the mesh: the far end of its longest axis
	 * once fitted and aligned. Set it non-zero to override that.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Rod")
	FVector RodTipLocalOffsetOverride = FVector::ZeroVector;

	// ---- Shark ----

	/** Finished shark size, cm: 96 long (its longest axis, pointed forward) by 69 x 69. Unity's instance size. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark")
	FVector SharkSizeCm = FVector(96.0f, 69.0f, 69.0f);

	/** Speed (uu/s) kicked into the shark's velocity by each periodic wander impulse (bEnableSharkWander only). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark")
	float WanderImpulseSpeed = 90.0f;

	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark", meta = (ClampMin = "0.1"))
	float WanderIntervalMinSeconds = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark", meta = (ClampMin = "0.1"))
	float WanderIntervalMaxSeconds = 3.5f;

	/** Ambient (unhooked) linear damping -- lighter than AttachedLinearDamping so the shark drifts freely. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark")
	float SwimLinearDamping = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark")
	float SwimAngularDamping = 0.4f;

	/** Radius (uu) around the rest pose beyond which an unhooked, wandering shark gets gently pulled home. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark", meta = (ClampMin = "0.0"))
	float HomeLeashRadius = 450.0f;

	/** Accel (uu/s^2) of that pull-home. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Fishing|Shark", meta = (ClampMin = "0.0"))
	float HomeLeashAccel = 60.0f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	// ---- setup (BeginPlay) ----

	/** Cache the child shark actor and hand it its size + EventMap wiring. */
	void SetUpShark();

	/** Resolve the EventMap (asset or fallback) and push the 3 entry ids onto the shark's sequence component. */
	void BuildEventMapAndHaptics();

	/** Build the transient EventMap used when no asset is assigned. */
	UHapbeatEventMap* BuildFallbackEventMap();

	/** EnableInput on the first PlayerController found and bind the mouse button. */
	void BindInput();

	/**
	 * Put SM_FishingRod in the player's hand mount at Unity's CameraFollowMount
	 * pose, fitted to RodLengthCm and turned so its longest axis points forward.
	 * No-op when the possessed pawn is not an AHapbeatShowcaseCharacter or the
	 * mesh is absent -- the line then hangs from RodTipAnchor instead.
	 */
	void MountRodOnCharacter();

	/** Take the rod back out of the player's hand (the character outlives this zone). */
	void UnmountRod();

	/**
	 * One deferred attempt at MountRodOnCharacter(), made on the first Tick that
	 * has a player pawn to look at. WHY NOT IN BeginPlay: possession is not
	 * ordered against actor BeginPlay, and a mount attempt made too early would
	 * silently leave the player empty-handed.
	 */
	void TryDeferredMount();

	/** World position of the rod's tip: the mounted rod's far end, or RodTipAnchor. */
	FVector GetRodTipWorldLocation() const;

	void HandleFirePressed();  // left mouse down -- hook
	void HandleFireReleased(); // left mouse up -- release

	/** Toggle the hook: fires/stops the sequence, swaps the shark's damping, and (on hook) snaps it to tether range. No-op if already in that state. */
	void SetHooked(bool bNewHooked);

	// ---- per-tick simulation ----

	/** Sway the fallback anchor (opt-in) and derive RodTipVelocity from the frame-to-frame world-position delta. */
	void UpdateRodTip(float DeltaSeconds);

	/** Periodic wander impulses + a gentle unhooked pull back toward the rest pose. Both opt-in. */
	void UpdateSharkWander(float DeltaSeconds);

	/** The taut-line tension model (see class comment); a no-op when the line is currently slack. */
	void UpdateHookedLinePhysics(float DeltaSeconds);

	/** Measure the line mesh and build its two tint instances (BeginPlay). */
	void SetUpLineVisual();

	/**
	 * Stretch LineMesh between the rod tip and the shark -- parity with
	 * FishingController.cs's LineRenderer, which is likewise a real, always-drawn
	 * visual rather than a debug one.
	 */
	void UpdateLineVisual();

	void RefreshHud(float DeltaSeconds);

	/** The shark's world rest pose, from SharkRestAnchor. */
	FTransform GetSharkRestWorldTransform() const;

	// ---- Constructor-created default subobjects: the editable scene ----

	/**
	 * Where the line hangs from when nobody is holding the rod. Invisible: Unity
	 * has no standing rod prop, so this is a reference point, not a thing to look
	 * at.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Fishing")
	TObjectPtr<USceneComponent> RodTipAnchor;

	/**
	 * The fishing line: a unit cylinder re-stretched between the rod tip and the
	 * shark every Tick. UE counterpart of the LineRenderer on Unity's rod --
	 * NOT DrawDebugLine, which the previous version used and which a Shipping
	 * build compiles away, taking the line with it. Its transform is written
	 * every frame, so nothing about it is authored in the editor.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Fishing")
	TObjectPtr<UStaticMeshComponent> LineMesh;

	/** Unity FishingObject_RestPose: where Detach() puts the shark back. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Fishing")
	TObjectPtr<USceneComponent> SharkRestAnchor;

	/**
	 * The shark, as a child actor so it owns the sequence + binding components
	 * (which must sit on the body they describe -- see the class comment) while
	 * keeping an editable, saved relative transform. Unity FishingObject.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Fishing")
	TObjectPtr<UChildActorComponent> SharkSlot;

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

	/** Cached SharkSlot->GetChildActor(); the thing the line physics moves. */
	UPROPERTY(Transient)
	TObjectPtr<AHapbeatShowcaseZ3SharkActor> Shark;

	bool bHooked = false;

	/** The character carrying the rod, when there is one; drives GetRodTipWorldLocation(). */
	TWeakObjectPtr<AHapbeatShowcaseCharacter> MountedCharacter;

	/** True once the deferred mount attempt has been made (successful or not). */
	bool bMountAttempted = false;

	/** Rod-tip offset in the mounted rod's local space (bounds-derived, or the override). */
	FVector RodTipLocalOffset = FVector::ZeroVector;

	// The two tint instances the line switches between, and the line mesh's own
	// unscaled size (measured in SetUpLineVisual, so UpdateLineVisual's scale is
	// "wanted size / mesh size" and assumes nothing about the primitive).

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LineSlackMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LineHookedMaterial;

	float LineMeshLocalLengthCm = 0.0f;
	float LineMeshLocalDiameterCm = 0.0f;

	float ElapsedTimeSeconds = 0.0f;
	float TimeToNextWanderImpulse = 0.0f;
	FVector PrevRodTipWorldPos = FVector::ZeroVector;
	FVector RodTipVelocity = FVector::ZeroVector;

	/** Fixed on-screen-message keys, offset into the 300s so they don't collide with other zones' HUD lines. */
	static constexpr int32 KeyGuideHudLineKey = 300;
	static constexpr int32 StatusHudLineKey = 301;
	static constexpr float HudRefreshIntervalSeconds = 0.25f;
	float HudRefreshTimer = 0.0f;
};

/**
 * The Z3 shark: a capsule body carrying the physics, a mesh child for the look,
 * and the zone's two Hapbeat components -- which have to live HERE rather than
 * on the zone actor because UHapbeatParameterBinding reads its owner's root
 * velocity and UHapbeatTriggerComponent::PreSeedBindings() only scans its own
 * owner's components (see the zone's class comment).
 *
 * WHY A CAPSULE ROOT AND NOT THE MESH: the imported shark's collision, axes and
 * pivot are all properties of the source model. A capsule states the body in the
 * zone's own terms and is sized from the finished shark, and the mesh is turned
 * and centred inside it as a plain child transform. The capsule's own axis is
 * its local Z, so the shark ACTOR is placed pitched a quarter turn (see
 * BodyPitchDegrees) to lay that axis along the body -- the mesh child then takes
 * the inverse of that on top of its own alignment, so it still points forward in
 * world space.
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ3SharkActor : public AActor
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ3SharkActor();

	/** The 3-phase hook sequence (start one-shot / loop / release one-shot). Wired by the zone. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatSequenceComponent> HookSequence;

	/** VelocityMagnitude -> StreamGain, so a thrashing shark is felt harder. Wired by the zone. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatParameterBinding> HookVelocityBinding;

	/**
	 * Actor pitch that lays the capsule's local Z axis along the body's length.
	 * The zone applies it to the child-actor slot; the mesh divides it back out.
	 */
	static constexpr float BodyPitchDegrees = 90.0f;

	/** Fit the mesh to SizeCm (longest axis forward) and size the capsule to match. */
	void ApplySharkSize(const FVector& SizeCm);

	/** Start / stop simulating -- the zone stops the shark while it is hidden. */
	void SetPhysicsRunning(bool bRunning);

	/** Damping while hooked / free, mirroring FishingController.cs's Attach / Detach. */
	void SetDamping(float Linear, float Angular);

	/** Teleport back to a pose and clear all momentum (Unity Detach()'s rest-pose snap). */
	void ResetToTransform(const FTransform& RestTransform);

	/** The physics body -- what the line model reads and writes. */
	UCapsuleComponent* GetBody() const { return Body; }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UCapsuleComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UStaticMeshComponent> SharkMesh;
};
