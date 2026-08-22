// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatShowcaseZone.h"

#include "HapbeatShowcaseActor.h"

#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h" // TActorIterator

namespace
{
	/**
	 * ZoneActor plus every actor beneath it -- child actors of
	 * UChildActorComponents (the pins, the shark, the target board) and anything
	 * a zone spawned with itself as Owner. AActor::GetAttachedActors already
	 * walks the attachment tree recursively, and child actors ARE attached to
	 * their component, so it covers both cases.
	 */
	void CollectZoneActors(AActor* ZoneActor, TArray<AActor*>& OutActors)
	{
		OutActors.Reset();
		if (ZoneActor == nullptr)
		{
			return;
		}
		OutActors.Add(ZoneActor);
		ZoneActor->GetAttachedActors(OutActors, /*bResetArray=*/false, /*bRecursivelyIncludeAttachedActors=*/true);
	}
}

void IHapbeatShowcaseZone::SetZoneSceneActive(AActor* ZoneActor, bool bActive)
{
	if (ZoneActor == nullptr)
	{
		return;
	}

	TArray<AActor*> Actors;
	CollectZoneActors(ZoneActor, Actors);
	for (AActor* Actor : Actors)
	{
		if (!IsValid(Actor))
		{
			continue;
		}
		Actor->SetActorHiddenInGame(!bActive);
		// Collision off as well as hidden: a hidden pin would still be something
		// the player could walk into, and a hidden target board would still
		// swallow the next zone's projectiles.
		Actor->SetActorEnableCollision(bActive);
		Actor->SetActorTickEnabled(bActive);
	}

	// Input last, and only on the zone actor itself: every zone binds its keys
	// through its own InputComponent (EnableInput in BeginPlay). Without this
	// pop, all five zones' left-mouse handlers would fire at once -- launching a
	// ball, hooking the shark and charging the blaster on the same click.
	// EnableInput / DisableInput only push and pop the component; the bindings
	// made once at BeginPlay survive, so this is not a rebind.
	const UWorld* World = ZoneActor->GetWorld();
	if (APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr)
	{
		if (bActive)
		{
			ZoneActor->EnableInput(PC);
		}
		else
		{
			ZoneActor->DisableInput(PC);
		}
	}
}

bool IHapbeatShowcaseZone::IsOwnedByShowcaseSwitcher(const AActor* ZoneActor)
{
	if (ZoneActor == nullptr)
	{
		return false;
	}
	// Phase 2b: zones are placed in the map rather than spawned, so the Owner
	// link the previous version tested no longer exists. A switcher anywhere in
	// the same world is what decides whether the shared HUD is on screen.
	const UWorld* World = ZoneActor->GetWorld();
	if (World == nullptr)
	{
		return false;
	}
	for (TActorIterator<AHapbeatShowcaseActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (IsValid(*It))
		{
			return true;
		}
	}
	return false;
}
