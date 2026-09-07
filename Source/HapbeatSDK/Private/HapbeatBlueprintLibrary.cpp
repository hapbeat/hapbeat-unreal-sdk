// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatBlueprintLibrary.h"

#include "HapbeatEventMap.h"
#include "HapbeatSubsystem.h"
#include "HapbeatTickEmitterComponent.h"
#include "HapbeatTriggerComponent.h"

#include "Engine/Engine.h"       // GEngine->GetWorldFromContextObject
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeat, Log, All);

namespace
{
	/**
	 * Resolve the subsystem + validate the entry arguments, logging which of the
	 * three things is missing. Shared by Play/Stop so both report the same
	 * failure modes -- and so the two most common authoring mistakes are
	 * distinguishable in the log: a node dropped without picking a map, versus a
	 * map picked but the entry dropdown never touched.
	 */
	UHapbeatSubsystem* ResolveForCall(const UObject* WorldContextObject, const UHapbeatEventMap* Map,
		const FHapbeatEntryRef& Entry, const TCHAR* Caller)
	{
		if (Map == nullptr)
		{
			UE_LOG(LogHapbeat, Warning, TEXT("%s: no Event Map given (the node's Map pin is empty)."), Caller);
			return nullptr;
		}
		if (!Entry.IsSet())
		{
			UE_LOG(LogHapbeat, Warning, TEXT("%s: no entry chosen for Event Map '%s' (the node's Entry dropdown is still \"(none)\")."),
				Caller, *Map->GetName());
			return nullptr;
		}

		// LogAndReturnNull: a null world here means the call site has no world
		// context at all (a CDO, or an editor utility), which is worth a log line
		// rather than a silent no-op.
		UWorld* World = GEngine != nullptr
			? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull)
			: nullptr;
		UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
		UHapbeatSubsystem* Subsystem = GameInstance != nullptr
			? GameInstance->GetSubsystem<UHapbeatSubsystem>()
			: nullptr;
		if (Subsystem == nullptr)
		{
			UE_LOG(LogHapbeat, Warning, TEXT("%s: no Hapbeat subsystem for this world context (not running in a game instance?)."), Caller);
		}
		return Subsystem;
	}
}

UHapbeatStreamPlayback* UHapbeatBlueprintLibrary::PlayHapbeatEvent(const UObject* WorldContextObject,
	UHapbeatEventMap* Map, FHapbeatEntryRef Entry, float GainMultiplier, float Pan, float DelaySeconds)
{
	UHapbeatSubsystem* Subsystem = ResolveForCall(WorldContextObject, Map, Entry, TEXT("PlayHapbeatEvent"));
	if (Subsystem == nullptr)
	{
		return nullptr;
	}
	// Delegate rather than re-implement: PlayEntry owns the whole Command /
	// Stream Clip decision, so the entry points cannot drift apart.
	return Subsystem->PlayEntry(Map, Entry.EntryId, GainMultiplier, /*bForceNonLoop=*/false, Pan, DelaySeconds);
}

void UHapbeatBlueprintLibrary::StopHapbeatEvent(const UObject* WorldContextObject,
	UHapbeatEventMap* Map, FHapbeatEntryRef Entry)
{
	UHapbeatSubsystem* Subsystem = ResolveForCall(WorldContextObject, Map, Entry, TEXT("StopHapbeatEvent"));
	if (Subsystem == nullptr)
	{
		return;
	}
	Subsystem->StopEntry(Map, Entry.EntryId);
}

void UHapbeatBlueprintLibrary::FireHapbeatTickFromValue(UHapbeatTickEmitterComponent* TickEmitter, float Value)
{
	if (TickEmitter != nullptr)
	{
		TickEmitter->FireFromValue(Value);
	}
}
