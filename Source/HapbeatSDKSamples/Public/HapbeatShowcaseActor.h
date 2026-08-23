// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HapbeatShowcaseActor.generated.h"

class AHapbeatShowcaseCharacter;
class SHapbeatShowcaseHud;
class UHapbeatClip;
class UHapbeatEventMap;
class UHapbeatSubsystem;
struct FHapbeatShowcaseHudCommand;

/**
 * One switchable Showcase zone: the actor class used by the fallback path when
 * the level holds no zone actors, plus its HUD label.
 */
USTRUCT(BlueprintType)
struct FHapbeatShowcaseZoneEntry
{
	GENERATED_BODY()

	/**
	 * Zone actor class. Only used by the FALLBACK path (a level with no placed
	 * zones), where it is spawned at the switcher's own transform. When the
	 * level holds zone actors -- which the shipped Showcase map does -- those
	 * are used where they stand and this is ignored.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	TSubclassOf<AActor> ZoneClass;

	/** Short HUD label, e.g. "Bowling". A placed zone's own label wins over this. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	FText Label;
};

/**
 * Hapbeat Showcase -- THE ONE ACTOR TO PLACE. Drop this single actor into an
 * empty level, press Play, and switch zones with the 1-5 number keys; exactly
 * one zone exists at a time, spawned at this actor's own transform. UE
 * counterpart of the Unity Showcase's ZoneSwitcher (see
 * Samples~/Showcase/Scripts/ZoneSwitcher.cs).
 *
 * The five Z1..Z5 zone actors can still be placed individually when you want to
 * study one in isolation -- this switcher just saves you from placing five of
 * them and spacing them apart by hand.
 *
 * HOW A SWITCH WORKS: the five zones are PLACED IN THE MAP, each in its own
 * room, and switching hides / shows them. IHapbeatShowcaseZone::
 * SetZoneSceneActive turns visibility, collision, ticking and the zone's
 * input-component push on or off for the zone actor and everything under it,
 * then the zone's own OnZoneActivated / OnZoneDeactivated resets or stops what
 * only it knows about. As a belt-and-braces measure the switcher also calls
 * StopStream() + StopAll() between the two, so nothing a zone failed to stop can
 * outlive it. The input pop is the part that must not be skipped: a merely
 * hidden zone would keep answering the left mouse button, so one click would
 * launch a bowling ball, hook the shark AND charge the blaster.
 *
 * WHY NOT SPAWN / DESTROY: a spawned zone builds its whole scene from code, so
 * nothing about its layout can be nudged in the editor or saved. Placed zones
 * put every prop's transform in the Details panel and in the .umap, which is the
 * point of this arrangement.
 *
 * FALLBACK: a level with a switcher but NO placed zones -- drop this one actor
 * into an empty level and press Play -- still works. The switcher then spawns
 * Zones[k].ZoneClass at its own transform, one at a time, and destroys it on the
 * way out, as it used to.
 *
 * PLAYER: when the possessed pawn is an AHapbeatShowcaseCharacter (which the
 * Showcase game mode spawns), switching zones also teleports it to that zone's
 * IHapbeatShowcaseZone::GetPlayerSpawnRelative() pose and applies the zone's
 * cursor policy -- Unity ZoneSwitcher.TeleportPlayer + unlockCursorOnEnter. Any
 * other pawn (the engine's DefaultPawn, e.g. when this actor is dropped into a
 * bare level) is left alone, so the switcher still works without a player.
 *
 * NOTE ON KEYS: the number keys plus Q (manual fire) and P (ping) are reserved
 * by this actor, and the player character owns WASD / arrows / mouse-look / Tab.
 * The zones' own inputs (left mouse button, Space, F / G / L) never collide with
 * them -- only one zone exists at a time, so Z1's Space and Z4's Space cannot
 * both be live. BasicExample's F does collide with Z2's F, so keep the Showcase
 * in its own level, not alongside BasicExample.
 */
UCLASS(meta = (DisplayName = "Hapbeat Showcase"))
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseActor : public AActor
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseActor();

	/**
	 * The fallback zone list, in key order (element 1 = key "1"). Seeded in the
	 * constructor with the five shipped Showcase zones. Used only when the level
	 * holds no zone actors of its own; otherwise the placed zones -- sorted by
	 * IHapbeatShowcaseZone::GetZoneIndex() -- are what the keys address, and only
	 * the Label column is read from here (and even that loses to a zone's own
	 * label). At most 9 zones are reachable (keys 1-9).
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	TArray<FHapbeatShowcaseZoneEntry> Zones;

	/** Zone shown at BeginPlay, 1-based (clamped to the Zones array). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase", meta = (ClampMin = "1", ClampMax = "9"))
	int32 InitialZone = 1;

	/**
	 * Show the given zone and hide the others (1-based, clamped). No-op when it
	 * is already the active zone.
	 *
	 * BlueprintCallable so Scripts/capture_showcase_views.py can step the
	 * Showcase through its zones from Python while PIE runs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Showcase")
	void ShowZone(int32 OneBasedIndex);

	/** Alias of ShowZone, under the name the capture script and the docs use. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Showcase")
	void SetActiveZone(int32 OneBasedIndex) { ShowZone(OneBasedIndex); }

	/**
	 * CAPTURE AID: stand the player somewhere specific and point the view.
	 *
	 * SetActiveZone always drops the player on the zone's own spawn pose, which
	 * is where a visitor starts but not always where the thing being checked can
	 * be seen (the pin rack is 6 m down the lane). This teleports without
	 * changing zone, so Scripts/capture_showcase_views.py can photograph a zone
	 * from more than one place.
	 *
	 * @param WorldLocation The player's FEET, in world space -- the capsule's half
	 *                      height is added here, exactly as the zone spawn path does.
	 * @param Yaw           View and body yaw, degrees.
	 * @param Pitch         View pitch, degrees; negative looks down.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Showcase")
	void DebugPlacePlayer(FVector WorldLocation, float Yaw, float Pitch);

	/** How many zones are reachable: the placed ones, or the configured fallback list. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat|Showcase")
	int32 GetZoneCount() const;

	/** Currently displayed zone, 1-based; 0 before BeginPlay. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat|Showcase")
	int32 GetCurrentZone() const { return CurrentZone; }

	/**
	 * The Event Map the Q key's manual test fire comes from. Defaults to the
	 * Showcase map that ships with the plugin; a code-built one-entry fallback
	 * is used if the asset is missing, exactly like each zone does.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	TObjectPtr<UHapbeatEventMap> ManualFireEventMapOverride;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** EnableInput on the first PlayerController found, then BindKey(1..9). Warns (no-op) if none exists. */
	void BindInput();

	/**
	 * Find every IHapbeatShowcaseZone actor in the level and sort them by
	 * GetZoneIndex(), so the number keys address Z1..Z5 whatever order the
	 * actors were placed in. Leaves PlacedZones empty when the level has none,
	 * which is what selects the spawn fallback.
	 */
	void CollectPlacedZones();

	/** Fallback path only: spawn Zones[CurrentZone - 1] at this actor's transform (Owner = this). */
	void SpawnActiveZone();

	/** Hide + stop the outgoing zone (destroy it on the fallback path), then StopStream + StopAll. */
	void ClearActiveZone();

	/** The zone actor on screen right now: the placed one, or the spawned fallback. */
	AActor* GetActiveZoneActor() const;

	UHapbeatSubsystem* ResolveSubsystem() const;

	/** The possessed pawn, when it is the Showcase's own character; null otherwise. */
	AHapbeatShowcaseCharacter* ResolveShowcaseCharacter() const;

	/**
	 * Teleport the player to the active zone's spawn pose and apply its cursor
	 * policy. No-op unless the pawn is an AHapbeatShowcaseCharacter.
	 */
	void ApplyZonePlayerState();

	/** Resolve the Q-key test entry (from the asset, or a one-entry fallback map). */
	void BuildManualFireEventMap();

	/** Add the shared Slate guide to the viewport; called once at BeginPlay. */
	void CreateHud();
	/** Push the active zone's label / key rows / zone list into the HUD. */
	void RefreshHudContent();

	void HandleManualFireKey();
	void HandlePingKey();

	/** PONG handler -- feeds the HUD's round-trip readout. Dynamic delegate, so UFUNCTION. */
	UFUNCTION()
	void HandlePong(const FString& Endpoint, int64 RttUs, const FString& DeviceName,
		const FString& Address, const FString& Firmware);

	// BindKey needs a no-argument member function per key, so there is one thin
	// forwarder per digit (same explicit style as Z4's five key handlers).
	void HandleZone1Key();
	void HandleZone2Key();
	void HandleZone3Key();
	void HandleZone4Key();
	void HandleZone5Key();
	void HandleZone6Key();
	void HandleZone7Key();
	void HandleZone8Key();
	void HandleZone9Key();

	static constexpr float HudRefreshIntervalSeconds = 0.5f;
	float HudRefreshTimer = 0.0f;

	/** 1-based index of the displayed zone; 0 = none. */
	int32 CurrentZone = 0;

	/**
	 * Zone whose spawn pose / cursor policy has actually been applied to the
	 * player, 0 = none. The pawn may not be possessed yet when the initial zone
	 * is shown at BeginPlay, so Tick retries until it is -- otherwise the player
	 * would spend the whole session standing at the PlayerStart.
	 */
	int32 PlayerStateAppliedZone = 0;

	/**
	 * The zone actors found in the level, sorted by
	 * IHapbeatShowcaseZone::GetZoneIndex(). Empty in a level that has none,
	 * which is what selects the spawn fallback below.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> PlacedZones;

	/** Fallback path only: the one zone actor this switcher spawned. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnedZoneActor;

	/**
	 * The initial zone is applied on the first Tick, not in BeginPlay: a placed
	 * zone's own BeginPlay (where it binds its input) is not ordered against
	 * this actor's, so a deactivation issued from BeginPlay could be undone by a
	 * zone that begins play afterwards. Every actor's BeginPlay has run by the
	 * time the first Tick arrives.
	 */
	bool bInitialZoneApplied = false;

	/** Resolved manual-fire map (the override asset, or the built fallback). */
	UPROPERTY(Transient)
	TObjectPtr<UHapbeatEventMap> ManualFireEventMap;

	/** Keeps the fallback map's clip alive -- a soft pointer on the entry does not (see FHapbeatSampleLibrary). */
	UPROPERTY(Transient)
	TObjectPtr<UHapbeatClip> ManualFireClip;

	FGuid ManualFireEntryId;

	/** The one guide widget for the session; rows are replaced on zone change, the widget is not. */
	TSharedPtr<SHapbeatShowcaseHud> HudWidget;

	/** True once OnPong was subscribed, so EndPlay only unsubscribes what it added. */
	bool bPongSubscribed = false;
};
