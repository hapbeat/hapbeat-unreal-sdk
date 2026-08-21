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
 * single hinged cube swings open/closed on a Tick-driven yaw tween; six
 * UHapbeatTriggerComponents (one per showcase-kit z2_door_* event) each
 * Fire() at the exact moment their corresponding state transition happens.
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
 *   G -- context action: Open -> instant Slam shut (fires z2_door_slam,
 *        SKIPS the graceful Closing tween entirely -- a slam is abrupt by
 *        definition, not a tween); Locked -> Rattle (alias of F for the
 *        locked case). No-op while Closed(unlocked) or mid-tween.
 *   L -- lock toggle: Closed(unlocked) -> Locked (fires z2_door_lock);
 *        Locked -> Closed(unlocked) (fires z2_door_unlock). No-op while
 *        Open/Opening/Closing (mirrors Unity DoorController: LockToggle only
 *        ever transitions out of the Closed state).
 *
 * Reuses showcase-kit exactly as authored in the Unity Showcase's
 * ShowcaseEventMap.asset: all six z2_door_* events are StreamClip (WAVs shipped
 * under Content/HapbeatSamples/Showcase/Kit/showcase-kit/stream-clips/), so no
 * Kit deployment is needed. Haptics-only (no SFX).
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseZ2DoorActor : public AActor, public IHapbeatShowcaseZone
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseZ2DoorActor();

	// ---- IHapbeatShowcaseZone ----
	virtual FText GetZoneLabel() const override;
	virtual TArray<FHapbeatShowcaseHudCommand> GetHudCommands() const override;
	virtual FTransform GetPlayerSpawnRelative() const override;

	/** See AHapbeatShowcaseZ1BowlingActor::FootprintOffset -- same convention across every Showcase zone. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	FVector FootprintOffset = FVector::ZeroVector;

	/** Yaw swing target for the Open state, degrees. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Door", meta = (ClampMin = "10.0", ClampMax = "170.0"))
	float OpenYawDegrees = 90.0f;

	/** Opening tween duration, seconds. Matches z2_door_open.wav's 2.64 s so the swing tracks the streamed haptic clip. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Door", meta = (ClampMin = "0.1"))
	float OpenDurationSeconds = 2.64f;

	/** Closing tween duration, seconds. Matches z2_door_close.wav's 2.78 s. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Door", meta = (ClampMin = "0.1"))
	float CloseDurationSeconds = 2.78f;

	/** Rattle shake duration, seconds. Matches z2_door_rattle.wav's 0.54 s. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Door", meta = (ClampMin = "0.05"))
	float RattleDurationSeconds = 0.54f;

	/** Rattle shake amplitude, degrees either side of the Locked (closed) yaw. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Door", meta = (ClampMin = "0.0"))
	float RattleAmplitudeDegrees = 6.0f;

	/** Rattle shake frequency, Hz. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Door", meta = (ClampMin = "0.1"))
	float RattleFrequencyHz = 9.0f;

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

	/** Set the hinge (Root)'s relative yaw, degrees. */
	void SetDoorYaw(float Degrees);

	EHapbeatZ2DoorState State = EHapbeatZ2DoorState::Closed;
	/** Seconds elapsed in the current Opening/Closing tween. */
	float StateElapsedSeconds = 0.0f;
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

	// Constructor-created default subobjects (VisibleAnywhere, not Transient).

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Door")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

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
