// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "HapbeatShowcaseZone.generated.h"

/**
 * One row of the Showcase HUD's KEY / DESCRIPTION table, e.g. { "B", "launch
 * ball" }. UE counterpart of one line of Unity ZoneEntry.commands, which is a
 * text blob split on "|" (Samples~/Showcase/Scripts/HudGuide.cs) -- here the
 * split is already done, so a zone cannot get the separator wrong.
 */
USTRUCT(BlueprintType)
struct FHapbeatShowcaseHudCommand
{
	GENERATED_BODY()

	/** Key cap text shown in the left column, e.g. "F" or "U/J". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Showcase")
	FText Key;

	/** What that key does, shown in the right column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Showcase")
	FText Description;

	FHapbeatShowcaseHudCommand() = default;
	FHapbeatShowcaseHudCommand(const FText& InKey, const FText& InDescription)
		: Key(InKey), Description(InDescription)
	{
	}
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UHapbeatShowcaseZone : public UInterface
{
	GENERATED_BODY()
};

/**
 * What the Showcase switcher needs to know about a zone that the zone actor
 * itself is the only one able to answer: its label, its key guide, where the
 * player should stand, and whether it needs the mouse cursor.
 *
 * WHY AN INTERFACE AND NOT A SHARED BASE CLASS: the five zone actors already
 * derive from AActor directly and build wildly different scenes (a bowling
 * lane, a door, a fishing rig, a console, a target range) -- they share no
 * state worth inheriting. Unity carries the same four facts as serialized
 * fields on ZoneSwitcher.ZoneEntry (label / playerSpawn / commands /
 * unlockCursorOnEnter); putting them on the zone instead of on the switcher
 * keeps a zone self-describing when it is dropped into a level on its own.
 *
 * Every function has a working default, so a new zone only overrides what it
 * actually differs on.
 */
class HAPBEATSDKSAMPLES_API IHapbeatShowcaseZone
{
	GENERATED_BODY()

public:
	/**
	 * This zone's slot in the Showcase, 1-based (Z1 = 1 ... Z5 = 5). The
	 * switcher sorts the zones it finds in the level by this, so the number
	 * keys mean the same thing no matter what order the actors were placed in.
	 * 0 = "unnumbered", which sorts last.
	 */
	virtual int32 GetZoneIndex() const { return 0; }

	/** Short HUD label, e.g. "Bowling". */
	virtual FText GetZoneLabel() const { return FText::GetEmpty(); }

	/** The zone's own key rows for the HUD table; the switcher prepends the global ones. */
	virtual TArray<FHapbeatShowcaseHudCommand> GetHudCommands() const { return TArray<FHapbeatShowcaseHudCommand>(); }

	/**
	 * Where the player starts when this zone is shown, RELATIVE TO THE ZONE's
	 * own transform (the switcher's transform, since that is where it spawns
	 * zones). Yaw only -- the switcher resets pitch to level, as Unity's
	 * ZoneSwitcher does via SimpleFPSController.ResetLook().
	 *
	 * HEIGHT CONVENTION: the Z of this location is the player's FEET, not the
	 * capsule centre. The switcher adds the character's capsule half-height, so
	 * a zone that wants the player standing on the floor returns Z = 0 and does
	 * not need to know how tall the character is.
	 *
	 * Default: 2.5 m behind the zone origin, facing +X. Every zone builds its
	 * geometry forward along +X from its own origin, so this looks straight at
	 * whatever the zone put there.
	 */
	virtual FTransform GetPlayerSpawnRelative() const
	{
		return FTransform(FRotator::ZeroRotator, FVector(-250.0f, 0.0f, 0.0f));
	}

	/**
	 * True for a zone whose interaction is on-screen UI rather than the world,
	 * so the switcher releases the mouse (and mouse-look stops) on entry and
	 * re-locks it on the way out. Unity: ZoneEntry.unlockCursorOnEnter.
	 */
	virtual bool WantsCursorUnlocked() const { return false; }

	/**
	 * Called when the switcher makes this zone the visible one. The switcher has
	 * already un-hidden it, re-enabled its collision and ticking, and pushed its
	 * input component back onto the player's stack (see SetZoneSceneActive), so
	 * an override is only for state a zone wants reset -- a ball back on its
	 * mark, a door back to closed.
	 */
	virtual void OnZoneActivated() {}

	/**
	 * Called when the switcher hides this zone. The switcher does the generic
	 * teardown around it, so an override is only for what a zone must stop
	 * itself: an in-flight stream, an audio voice, a pending timer.
	 */
	virtual void OnZoneDeactivated() {}

	/**
	 * The generic half of a zone switch: show / hide, collision on / off, tick
	 * on / off, applied to the zone actor and everything under it (child actors
	 * included, which is where the pins, the shark and the target board live),
	 * plus push / pop of its input component so only the visible zone's keys are
	 * live. Called by the switcher on both sides of a change; a zone's own
	 * OnZoneActivated / OnZoneDeactivated runs around it.
	 *
	 * WHY THIS AND NOT SPAWN / DESTROY (which is what Phase 2 did): the zones are
	 * now PLACED in the map, so their component transforms are editable and
	 * saved. Destroying one would throw that away and rebuild it from code, which
	 * is exactly what made the layout un-authorable.
	 */
	static void SetZoneSceneActive(AActor* ZoneActor, bool bActive);

	/**
	 * True when this zone actor is being driven by a Showcase switcher in the
	 * same level. The switcher draws one shared Slate HUD covering the key guide
	 * and the device status, so a zone under it skips ITS WHOLE on-screen debug
	 * HUD -- key guide, its own state line and the device footer alike -- instead
	 * of printing over the shared one. A zone placed in a level on its own has no
	 * switcher and keeps drawing them.
	 *
	 * Each zone tests this ONCE, as an early return at the top of its HUD block.
	 * Guarding line by line is what let the state lines ("Gain=... Pan=...",
	 * "Charge=...", "Z2 state: ...") be written outside the guard and show on top
	 * of the shared HUD.
	 */
	static bool IsOwnedByShowcaseSwitcher(const AActor* ZoneActor);
};
