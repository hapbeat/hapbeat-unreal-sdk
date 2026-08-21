// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HapbeatShowcaseActor.generated.h"

class UHapbeatSubsystem;

/** One switchable Showcase zone: the actor class to spawn plus its HUD label. */
USTRUCT(BlueprintType)
struct FHapbeatShowcaseZoneEntry
{
	GENERATED_BODY()

	/** Zone actor class spawned at the switcher's own transform while this zone is active. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	TSubclassOf<AActor> ZoneClass;

	/** Short HUD label, e.g. "Bowling". */
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
 * WHY SPAWN/DESTROY INSTEAD OF HIDE: UE has no direct equivalent of Unity's
 * GameObject.SetActive(false). Merely hiding a zone actor would leave it
 * ticking and still bound to the player's input stack, so Z1's B key would keep
 * launching bowling balls (and firing haptics) while zone 2 is on screen. Every
 * zone already builds its scene in BeginPlay and tears it down + stops its
 * haptics in EndPlay, so destroying the outgoing zone and spawning the incoming
 * one is both the simplest and the only fully correct switch. As a belt-and-
 * braces measure the switcher also calls StopStream() + StopAll() between the
 * two, so nothing a zone failed to stop can outlive it.
 *
 * NOTE ON KEYS: the number keys are reserved by this actor. The zones' own keys
 * (B / F / G / L / H / T / U / J / N / M / V) never collide with them, but
 * BasicExample's F does collide with Z2's F -- keep the Showcase in its own
 * level, not alongside BasicExample.
 */
UCLASS(meta = (DisplayName = "Hapbeat Showcase"))
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseActor : public AActor
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseActor();

	/**
	 * The switchable zones, in key order (element 1 = key "1"). Seeded in the
	 * constructor with the five shipped Showcase zones; reorder / swap / trim
	 * freely. At most 9 entries are reachable (keys 1-9).
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase")
	TArray<FHapbeatShowcaseZoneEntry> Zones;

	/** Zone shown at BeginPlay, 1-based (clamped to the Zones array). */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|Showcase", meta = (ClampMin = "1", ClampMax = "9"))
	int32 InitialZone = 1;

	/**
	 * Destroy the current zone and spawn the given one (1-based, clamped).
	 * No-op when it is already the active zone.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Showcase")
	void ShowZone(int32 OneBasedIndex);

	/** Currently displayed zone, 1-based; 0 before BeginPlay. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat|Showcase")
	int32 GetCurrentZone() const { return CurrentZone; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** EnableInput on the first PlayerController found, then BindKey(1..9). Warns (no-op) if none exists. */
	void BindInput();

	/** Spawn Zones[CurrentZone - 1] at this actor's transform (Owner = this). */
	void SpawnActiveZone();

	/** Destroy the active zone actor (its EndPlay stops that zone's haptics), then StopStream + StopAll. */
	void ClearActiveZone();

	UHapbeatSubsystem* ResolveSubsystem() const;

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

	/** Fixed on-screen-message keys; 700s so they never collide with a zone's own HUD lines (0/1, 100s..600s). */
	static constexpr int32 KeyGuideHudLineKey = 700;
	static constexpr int32 StatusHudLineKey = 701;
	static constexpr float HudRefreshIntervalSeconds = 0.5f;
	float HudRefreshTimer = 0.0f;

	/** 1-based index of the displayed zone; 0 = none. */
	int32 CurrentZone = 0;

	UPROPERTY(Transient)
	TObjectPtr<AActor> ActiveZoneActor;
};
