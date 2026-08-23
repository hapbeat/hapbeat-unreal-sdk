// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HapbeatShowcaseRoomActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;

/**
 * The room a Showcase zone stands in: a 20 x 20 m floor with a 3 m wall on each
 * side. UE counterpart of the Unity Showcase's `Structure` object
 * (Samples~/Showcase/Showcase.unity), with its numbers converted -- Unity
 * (x, y, z) metres become UE (z, x, y) centimetres.
 *
 * WHY EACH ZONE GETS ITS OWN: Unity puts all five zones inside one room and
 * hides the four that are not showing. Here every zone is a placed actor at its
 * own map origin (see Scripts/generate_showcase_map.py), so it gets a room of
 * its own at that origin -- the walls then read as this zone's walls no matter
 * which one you switch to, and nothing has to move when you do.
 *
 * Everything is an engine Cube scaled into place. Shipped MI_Floor / MI_Wall
 * are constructor-assigned, so the saved level already has its final look
 * before Play and BeginPlay performs no synchronous art loads.
 */
UCLASS(meta = (DisplayName = "Hapbeat Showcase Room"))
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseRoomActor : public AActor
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseRoomActor();

private:
	/** One Cube-based slab of the room (floor or wall), attached to Root. Constructor-time only. */
	UStaticMeshComponent* MakeSlab(USceneComponent* Root, const TCHAR* Name,
		const FVector& CentreCm, const FVector& SizeCm);

	/** Walking surface: top face exactly at Z = 0, which is what every zone's spawn height assumes. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Showcase")
	TObjectPtr<UStaticMeshComponent> Floor;

	/** +X wall (Unity WallN at z = +10 m). */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Showcase")
	TObjectPtr<UStaticMeshComponent> WallNorth;

	/** -X wall (Unity WallS). */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Showcase")
	TObjectPtr<UStaticMeshComponent> WallSouth;

	/** +Y wall (Unity WallE). */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Showcase")
	TObjectPtr<UStaticMeshComponent> WallEast;

	/** -Y wall (Unity WallW). */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Showcase")
	TObjectPtr<UStaticMeshComponent> WallWest;
};
