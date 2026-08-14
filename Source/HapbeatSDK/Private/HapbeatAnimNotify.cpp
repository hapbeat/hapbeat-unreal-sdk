// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatAnimNotify.h"

#include "HapbeatClip.h"
#include "HapbeatEventEntry.h"
#include "HapbeatEventMap.h"
#include "HapbeatStreamPlayback.h"
#include "HapbeatSubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatAnim, Log, All);

namespace
{
	UHapbeatSubsystem* GetSubsystem(const USkeletalMeshComponent* MeshComp)
	{
		const UWorld* World = MeshComp != nullptr ? MeshComp->GetWorld() : nullptr;
		UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
		return GameInstance != nullptr ? GameInstance->GetSubsystem<UHapbeatSubsystem>() : nullptr;
	}

	/** Resolves the entry, or logs why it could not be resolved. */
	bool ResolveEntry(const UHapbeatEventMap* Map, const FGuid& EntryId, const TCHAR* Context, FHapbeatEventEntry& OutEntry)
	{
		if (Map == nullptr || !EntryId.IsValid())
		{
			return false;
		}
		if (!Map->FindById(EntryId, OutEntry))
		{
			UE_LOG(LogHapbeatAnim, Warning,
				TEXT("[Hapbeat] %s: entry %s is not in '%s' -- it was probably deleted after this notify was authored."),
				Context, *EntryId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(Map));
			return false;
		}
		return true;
	}

}

// ---------------------------------------------------------------------------
// UHapbeatAnimNotify
// ---------------------------------------------------------------------------

void UHapbeatAnimNotify::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	UHapbeatSubsystem* Subsystem = GetSubsystem(MeshComp);
	FHapbeatEventEntry Entry;
	if (Subsystem == nullptr || !ResolveEntry(EventMap, EntryId, TEXT("Hapbeat Event notify"), Entry))
	{
		return;
	}

	// A looping Stream Clip fired from a point notify would never be stopped --
	// there is no matching end. Use the sustained notify for that instead.
	if (Entry.Mode == EHapticMode::StreamClip && Entry.bLoop)
	{
		UE_LOG(LogHapbeatAnim, Warning,
			TEXT("[Hapbeat] Entry '%s' loops, but a point notify has no end to stop it at. Use 'Hapbeat Event (Sustained)'."),
			*Entry.GetEventId());
	}

	Subsystem->PlayEntry(EventMap, EntryId, GainMultiplier);
}

FString UHapbeatAnimNotify::GetNotifyName_Implementation() const
{
	FHapbeatEventEntry Entry;
	if (EventMap != nullptr && EntryId.IsValid() && EventMap->FindById(EntryId, Entry))
	{
		const FString Label = Entry.DisplayName.IsEmpty() ? Entry.GetEventId() : Entry.DisplayName;
		if (!Label.IsEmpty())
		{
			return FString::Printf(TEXT("Hapbeat: %s"), *Label);
		}
	}
	return TEXT("Hapbeat: (unassigned)");
}

// ---------------------------------------------------------------------------
// UHapbeatAnimNotifyState
// ---------------------------------------------------------------------------

void UHapbeatAnimNotifyState::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	UHapbeatSubsystem* Subsystem = GetSubsystem(MeshComp);
	FHapbeatEventEntry Entry;
	if (Subsystem == nullptr || !ResolveEntry(EventMap, EntryId, TEXT("Hapbeat sustained notify"), Entry))
	{
		return;
	}

	if (UHapbeatStreamPlayback* Playback = Subsystem->PlayEntry(EventMap, EntryId, GainMultiplier))
	{
		ActivePlaybacks.Add(MeshComp, Playback);
	}
}

void UHapbeatAnimNotifyState::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	UHapbeatSubsystem* Subsystem = GetSubsystem(MeshComp);
	FHapbeatEventEntry Entry;
	const bool bResolved = ResolveEntry(EventMap, EntryId, TEXT("Hapbeat sustained notify"), Entry);

	if (TWeakObjectPtr<UHapbeatStreamPlayback>* Found = ActivePlaybacks.Find(MeshComp))
	{
		if (UHapbeatStreamPlayback* Playback = Found->Get())
		{
			Playback->Stop();
		}
		ActivePlaybacks.Remove(MeshComp);
	}
	else if (Subsystem != nullptr && bResolved && Entry.Mode == EHapticMode::Command)
	{
		// Command mode has no handle -- the device is holding the event, so the
		// stop has to go out as its own command.
		Subsystem->Stop(Entry.GetEventId(), Entry.Target);
	}

	Super::NotifyEnd(MeshComp, Animation, EventReference);
}

FString UHapbeatAnimNotifyState::GetNotifyName_Implementation() const
{
	FHapbeatEventEntry Entry;
	if (EventMap != nullptr && EntryId.IsValid() && EventMap->FindById(EntryId, Entry))
	{
		const FString Label = Entry.DisplayName.IsEmpty() ? Entry.GetEventId() : Entry.DisplayName;
		if (!Label.IsEmpty())
		{
			return FString::Printf(TEXT("Hapbeat: %s"), *Label);
		}
	}
	return TEXT("Hapbeat: (unassigned)");
}
