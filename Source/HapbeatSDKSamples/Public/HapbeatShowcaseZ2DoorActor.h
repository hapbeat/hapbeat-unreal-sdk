// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HapbeatEventEntry.h" // EHapticMode
#include "HapbeatShowcaseZone.h" // IHapbeatShowcaseZone: the switcher asks the zone for its label / keys / spawn
#include "HapbeatShowcaseZ2DoorActor.generated.h"

class UHapbeatClip;
class UHapbeatEventMap;
class UHapbeatTriggerComponent;
class USceneComponent;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Door state, driven entirely by Tick -- no Animator/.controller asset. This is
 * the UE stand-in for Unity's Animator + HapbeatStateBehaviour (design doc
 * §3.7, "DROP ... generic UAnimNotifyState StateBehaviour (Z2 fires events
 * imperatively from its own door state machine instead)").
 */
UENUM(BlueprintType)
enum class EHapbeatZ2DoorState : uint8
{
	Closed,
	Opening,
	Open,
	Closing,
	Locked,
};

/**
 * Z2 Swing Door -- the "imperative fire at state transitions" showcase zone. A
 * hinged leaf inside a fixed frame swings open and closed on a Tick-driven yaw
 * tween; six UHapbeatTriggerComponents (one per showcase-kit z2_door_* event)
 * each Fire() at the exact moment their corresponding transition happens.
 *
 * WHAT ROTATES: the DoorHinge scene component ONLY. It sits at the leaf's
 * hinge-side edge, with the leaf mesh offset half a leaf-width from it, so the
 * leaf swings about its edge the way a door does. The frame is a sibling of the
 * hinge under the actor root and therefore never moves -- the Phase 2 version
 * turned the actor root, which took the frame (and everything else) round with
 * it.
 *
 * GEOMETRY, from Unity Showcase.unity's Z2_Door subtree, converted (Unity
 * (x, y, z) m -> UE (z, x, y) cm): the leaf is 150 (Y) x 200 (Z) x 10 (X) cm
 * centred 1 m up, and the frame sits 42.7 cm to +Y of the zone origin. The
 * leaf, frame and handle are three separate imported meshes -- SM_Door,
 * SM_DoorFrame, SM_DoorHandle -- which is why Scripts/import_showcase_assets.py
 * imports Door.fbx with Combine Meshes OFF: a single combined mesh cannot have
 * its leaf rotated away from its frame.
 *
 * Key layout (3 keys reach all 6 showcase-kit events -- Unity's DoorController
 * drives the same 6 transitions off F/G/L too, see
 * Samples~/Showcase/Scripts/DoorController.cs; this actor's state names and
 * exact per-key semantics are this UE sample's own simplified reduction of
 * that Animator condition table, not a byte-for-byte port of it):
 *
 *   F -- context toggle: Closed(unlocked) -> Opening (fires z2_door_open);
 *        Open -> Closing (fires z2_door_close); Locked -> Rattle in place
 *        (fires z2_door_rattle -- Unity parity: DoorController's DoorAction
 *        on a locked door takes the Closed -> LockedRattle transition).
 *        No-op mid-tween (Opening/Closing reject input, same intent as
 *        Unity's IsAnimatorInAcceptedState settle-gate).
 *   G -- context action: Open -> Slam shut (fires z2_door_slam; the same
 *        Closing tween, but over SlamDurationSeconds instead -- a slam is a
 *        fast swing, not a teleport); Locked -> Rattle (alias of F for the
 *        locked case). No-op while Closed(unlocked) or mid-tween.
 *   L -- lock toggle: Closed(unlocked) -> Locked (fires z2_door_lock);
 *        Locked -> Closed(unlocked) (fires z2_door_unlock). No-op while
 *        Open/Opening/Closing (mirrors Unity DoorController: LockToggle only
 *        ever transitions out of the Closed state).
 *
 * Reuses showcase-kit exactly as authored in the Unity Showcase's
 * ShowcaseEventMap.asset: all six z2_door_* events are StreamClip (WAVs shipped
 * under Content/HapbeatSamples/Showcase/Kit/showcase-kit/stream-clips/), so no
 * Kit deployment is needed.
 *
 * Each transition also plays its S_z2_door_* one-shot when the Showcase's
 * optional imported art is present -- the audio counterpart of the haptic
 * event, fired at the same instant Unity's Animation Event calls
 * SoundPlayer.Play("door_open") etc. (the transition start).
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ2DoorActor : public AActor, public IHapbeatShowcaseZone
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ2DoorActor();

	// ---- IHapbeatShowcaseZone ----
	virtual int32 GetZoneIndex() const override { return 2; }
	virtual FText GetZoneLabel() const override;
	virtual TArray<FHapbeatShowcaseHudCommand> GetHudCommands() const override;
	virtual FTransform GetPlayerSpawnRelative() const override;
	virtual void OnZoneActivated() override;
	virtual void OnZoneDeactivated() override;

	/**
	 * CAPTURE AID: ask for the door open or shut without a keypress, by taking
	 * exactly the same path the F key does (so the swing, the sounds and the
	 * haptic triggers all still happen). Used by
	 * Scripts/capture_showcase_views.py to photograph the door in both poses.
	 * No-op unless the door is settled in the opposite state -- mid-swing and
	 * Locked are left alone, as they are for the key.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Door")
	void DebugSetDoorOpen(bool bOpen);

	/** Yaw swing target for the Open state, degrees. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Door", meta = (ClampMin = "10.0", ClampMax = "170.0"))
	float OpenYawDegrees = 90.0f;

	/** Opening tween duration, seconds. Unity's Animator blend for Closed -> Open works out to 2.0 s. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Door", meta = (ClampMin = "0.05"))
	float OpenDurationSeconds = 2.0f;

	/** Closing tween duration, seconds. Unity's Open -> Closed blend works out to 2.2 s. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Door", meta = (ClampMin = "0.05"))
	float CloseDurationSeconds = 2.2f;

	/** Slam duration, seconds -- the same swing as Closing, taken at Unity's slam speed. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Door", meta = (ClampMin = "0.01"))
	float SlamDurationSeconds = 0.117f;

	/** Rattle: three 0.1 s legs (0 -> +A -> -A -> 0), so 0.3 s in all. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Door", meta = (ClampMin = "0.01"))
	float RattleLegSeconds = 0.1f;

	/** Rattle shake amplitude, degrees either side of the Locked (closed) yaw. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Door", meta = (ClampMin = "0.0"))
	float RattleAmplitudeDegrees = 3.0f;

	/**
	 * Extra scale on the imported frame. The frame is imported at its authored
	 * size and expected to need none, but the source model is not this repo's to
	 * control, so the correction is a property rather than a constant.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Door", meta = (ClampMin = "0.01"))
	float FrameScale = 1.0f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Resolve the EventMap (asset or fallback) and wire the 6 trigger components to its z2_door_* entries. */
	void BuildEventMap();

	/** Build the transient EventMap used when no asset is assigned. */
	UHapbeatEventMap* BuildFallbackEventMap();

	/** EnableInput on the first PlayerController found, then BindKey(F/G/L). Warns (no-op) if none exists. */
	void BindInput();

	void HandleToggleKey(); // F
	void HandleActionKey(); // G
	void HandleLockKey();   // L

	/** Set the hinge's relative yaw, degrees. Nothing else in the zone moves. */
	void SetDoorYaw(float Degrees);

	/** Put the door back to Closed at yaw 0 with no tween in progress. */
	void ResetDoorState();

	/**
	 * Swap the primitive slab / frame for the imported SM_Door, SM_DoorFrame and
	 * SM_DoorHandle, and load the six door SFX. Each piece is independent: a
	 * missing one simply keeps its primitive, or is skipped.
	 */
	void ApplyShowcaseAssets();

	/**
	 * Fit one imported door piece into a box stated in the zone's axes
	 * (X = thickness, Y = width, Z = height) and, if the source model runs its
	 * width along X instead of Y, yaw it 90 degrees so it faces the way this
	 * zone's frame does. Returns the yaw applied, so the frame and the leaf can
	 * be turned the same way.
	 */
	float FitDoorPiece(UStaticMeshComponent* Component, UStaticMesh* Mesh, const FVector& TargetSizeCm);

	/** One-shot SFX at the door, mirroring Unity's SoundPlayer.Play(name) calls off the door animations. */
	void PlayDoorSound(USoundBase* Sound) const;

	EHapbeatZ2DoorState State = EHapbeatZ2DoorState::Closed;
	/** Seconds elapsed in the current Opening/Closing tween. */
	float StateElapsedSeconds = 0.0f;
	/** Duration of the Closing tween in progress: CloseDurationSeconds, or SlamDurationSeconds for a slam. */
	float ActiveCloseDurationSeconds = 2.2f;
	/** True while a Locked-state rattle shake is playing out. */
	bool bRattling = false;
	float RattleElapsedSeconds = 0.0f;

	/** Fixed on-screen-message keys, offset into the 200s so they don't collide with other zones' HUD lines. */
	static constexpr int32 KeyGuideHudLineKey = 200;
	static constexpr int32 StatusHudLineKey = 201;
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

	// Strong refs keeping the 6 StreamClip WAVs alive when the code-built
	// fallback map is in use (entries only hold a TSoftObjectPtr -- see
	// FHapbeatSampleLibrary::LoadSampleClip's GC note). Left null when the
	// EM_Showcase asset supplies the clips.
	UPROPERTY(Transient)
	TObjectPtr<UHapbeatClip> OpenClip;
	UPROPERTY(Transient)
	TObjectPtr<UHapbeatClip> CloseClip;
	UPROPERTY(Transient)
	TObjectPtr<UHapbeatClip> SlamClip;
	UPROPERTY(Transient)
	TObjectPtr<UHapbeatClip> LockClip;
	UPROPERTY(Transient)
	TObjectPtr<UHapbeatClip> UnlockClip;
	UPROPERTY(Transient)
	TObjectPtr<UHapbeatClip> RattleClip;

	// Optional imported SFX (S_z2_door_*), resolved at BeginPlay. Null = silent,
	// which is the correct behaviour when the Showcase art was never generated.
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> OpenSound;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CloseSound;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> SlamSound;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> LockSound;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> UnlockSound;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> RattleSound;

	// ---- Constructor-created default subobjects: the editable scene ----

	/** The pivot the leaf swings about, at the leaf's hinge-side edge. The ONLY thing that rotates. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Door")
	TObjectPtr<USceneComponent> DoorHinge;

	/** The swinging leaf, offset half a leaf-width from the hinge. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Door")
	TObjectPtr<UStaticMeshComponent> DoorLeafMesh;

	/** The handle, carried by the leaf. Hidden when SM_DoorHandle is not present. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Door")
	TObjectPtr<UStaticMeshComponent> DoorHandleMesh;

	/** The frame, fixed to the actor root -- it does not swing. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Door")
	TObjectPtr<UStaticMeshComponent> DoorFrameMesh;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Door")
	TObjectPtr<UHapbeatTriggerComponent> OpenTrigger;
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Door")
	TObjectPtr<UHapbeatTriggerComponent> CloseTrigger;
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Door")
	TObjectPtr<UHapbeatTriggerComponent> SlamTrigger;
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Door")
	TObjectPtr<UHapbeatTriggerComponent> LockTrigger;
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Door")
	TObjectPtr<UHapbeatTriggerComponent> UnlockTrigger;
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Door")
	TObjectPtr<UHapbeatTriggerComponent> RattleTrigger;
};
