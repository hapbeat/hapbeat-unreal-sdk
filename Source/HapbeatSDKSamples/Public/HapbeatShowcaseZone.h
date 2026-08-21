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
	 * True when this zone actor was spawned by the Showcase switcher (which is
	 * then its Owner). The switcher draws one shared Slate HUD covering the key
	 * guide and the device status, so a zone inside it skips its own
	 * ShowHudLine key-guide / device lines instead of printing them twice. A
	 * zone placed in a level by itself has no such owner and keeps drawing them.
	 */
	static bool IsOwnedByShowcaseSwitcher(const AActor* ZoneActor);
};
