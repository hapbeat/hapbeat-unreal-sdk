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
 * GEOMETRY COMES FROM THE FBX, NOT FROM UNITY'S NUMBERS. Door.fbx holds the
 * three pieces -- SM_Door (leaf), SM_DoorFrame, SM_DoorHandle -- modelled
 * against ONE shared origin, so at scale 1 and zero relative offset they
 * already fit each other: the leaf is 133 wide x 11.5 thick x 298 high and the
 * frame 175 x 33 x 314, i.e. the leaf exactly fills the frame's 133 cm opening
 * (175 - 2 x 21). Phase 2 instead fitted the leaf to Unity's 150 x 200 slab and
 * pushed the frame 42.7 cm sideways, which is what left a too-small leaf
 * hanging out of its frame. Both pieces are now placed at their authored
 * transforms and the ASSEMBLY is yawed 90 degrees as a unit, because the FBX
 * runs the leaf's width along X and its thickness along Y while this zone wants
 * width along Y (and therefore the face towards the player on -X).
 *
 * That the three are separate meshes at all is why
 * Scripts/import_showcase_assets.py imports Door.fbx with Combine Meshes OFF:
 * a single combined mesh cannot have its leaf rotated away from its frame.
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
 * shipped Showcase art is assigned -- the audio counterpart of the haptic
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

protected:
	/** Resolve trigger wiring in Editor World as well as at runtime. */
	virtual void OnConstruction(const FTransform& Transform) override;
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
	 * Constructor-only assignment of shipped meshes/material/SFX. This keeps the
	 * actor complete before Play and avoids BeginPlay synchronous loads.
	 */
	void ApplyShowcaseAssets();

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

	// Shipped SFX hard references, assigned on the native CDO.
	UPROPERTY(EditDefaultsOnly, Category = "Hapbeat|Door")
	TObjectPtr<USoundBase> OpenSound;
	UPROPERTY(EditDefaultsOnly, Category = "Hapbeat|Door")
	TObjectPtr<USoundBase> CloseSound;
	UPROPERTY(EditDefaultsOnly, Category = "Hapbeat|Door")
	TObjectPtr<USoundBase> SlamSound;
	UPROPERTY(EditDefaultsOnly, Category = "Hapbeat|Door")
	TObjectPtr<USoundBase> LockSound;
	UPROPERTY(EditDefaultsOnly, Category = "Hapbeat|Door")
	TObjectPtr<USoundBase> UnlockSound;
	UPROPERTY(EditDefaultsOnly, Category = "Hapbeat|Door")
	TObjectPtr<USoundBase> RattleSound;

	// ---- Constructor-created default subobjects: the editable scene ----

	/**
	 * The pivot the leaf swings about, at the leaf's hinge-side edge, and the ONLY
	 * thing that rotates. It also carries the assembly's base yaw (see the class
	 * comment), so its relative yaw is that base plus the swing angle.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Door")
	TObjectPtr<USceneComponent> DoorHinge;

	/** The swinging leaf, offset from the hinge back to the FBX's shared origin. */
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
