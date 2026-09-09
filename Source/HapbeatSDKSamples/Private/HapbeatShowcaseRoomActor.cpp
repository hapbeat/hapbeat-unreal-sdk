// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseRoomActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Unity `Structure`, converted. Floor 20 x 20 m; walls 20 m long, 3 m tall,
	// 0.4 m thick, centred 1.5 m up. Engine Cube is 100 cm authored, so a scale
	// component is the size in metres.
	constexpr float RoomHalfSpanCm = 1000.0f;   // Unity walls sit at +-10 m
	constexpr float RoomSpanCm = 2000.0f;
	constexpr float WallHeightCm = 300.0f;
	constexpr float WallThicknessCm = 40.0f;
	constexpr float WallCentreZCm = 150.0f;
	constexpr float FloorThicknessCm = 20.0f;
}

UStaticMeshComponent* AHapbeatShowcaseRoomActor::MakeSlab(USceneComponent* Root, const TCHAR* Name,
	const FVector& CentreCm, const FVector& SizeCm)
{
	UStaticMeshComponent* Slab = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Slab->SetupAttachment(Root);
	// The room never moves, so its geometry can retain the cheaper static
	// transform path. Lighting remains fully dynamic at map level; the zones'
	// own props are Movable because the switcher hides and shows them. Mobility
	// is set before the mesh assignment so SetStaticMesh never runs on a
	// component whose mobility is about to change.
	Slab->SetMobility(EComponentMobility::Static);
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Slab->SetStaticMesh(Cube);
	}
	Slab->SetRelativeLocation(CentreCm);
	Slab->SetRelativeScale3D(SizeCm / 100.0f);
	Slab->SetCollisionProfileName(TEXT("BlockAll"));
	return Slab;
}

AHapbeatShowcaseRoomActor::AHapbeatShowcaseRoomActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// STATIC ROOT, and this is load-bearing: a Static component cannot be
	// attached to a Movable parent, so with the default (Movable) root every
	// slab below was REJECTED at attach time ("AttachTo: Root is not static ...")
	// and ended up unparented at the world origin -- which is why the room
	// appeared collapsed onto the map origin instead of around its zone. The room
	// never moves, so Static is also what it wants to be.
	RootComponent->SetMobility(EComponentMobility::Static);

	// Sunk by half its thickness so the walking surface is exactly Z = 0.
	Floor = MakeSlab(RootComponent, TEXT("Floor"),
		FVector(0.0f, 0.0f, -FloorThicknessCm * 0.5f),
		FVector(RoomSpanCm, RoomSpanCm, FloorThicknessCm));

	WallNorth = MakeSlab(RootComponent, TEXT("WallNorth"),
		FVector(RoomHalfSpanCm, 0.0f, WallCentreZCm),
		FVector(WallThicknessCm, RoomSpanCm, WallHeightCm));
	WallSouth = MakeSlab(RootComponent, TEXT("WallSouth"),
		FVector(-RoomHalfSpanCm, 0.0f, WallCentreZCm),
		FVector(WallThicknessCm, RoomSpanCm, WallHeightCm));
	WallEast = MakeSlab(RootComponent, TEXT("WallEast"),
		FVector(0.0f, RoomHalfSpanCm, WallCentreZCm),
		FVector(RoomSpanCm, WallThicknessCm, WallHeightCm));
	WallWest = MakeSlab(RootComponent, TEXT("WallWest"),
		FVector(0.0f, -RoomHalfSpanCm, WallCentreZCm),
		FVector(RoomSpanCm, WallThicknessCm, WallHeightCm));

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FloorMaterial(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_Floor.MI_Floor"));
	if (FloorMaterial.Succeeded())
	{
		Floor->SetMaterial(0, FloorMaterial.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WallMaterial(
		TEXT("/HapbeatSDK/HapbeatSamples/Showcase/Materials/MI_Wall.MI_Wall"));
	if (WallMaterial.Succeeded())
	{
		for (UStaticMeshComponent* Wall : { WallNorth.Get(), WallSouth.Get(), WallEast.Get(), WallWest.Get() })
		{
			Wall->SetMaterial(0, WallMaterial.Object);
		}
	}
}
