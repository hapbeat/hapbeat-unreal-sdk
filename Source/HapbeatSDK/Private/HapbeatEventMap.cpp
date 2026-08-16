// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEventMap.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeat, Log, All);

bool UHapbeatEventMap::FindById(FGuid Id, FHapbeatEventEntry& OutEntry) const
{
	if (!Id.IsValid())
	{
		return false;
	}
	for (const FHapbeatEventEntry& Entry : Entries)
	{
		if (Entry.Id == Id)
		{
			OutEntry = Entry;
			return true;
		}
	}
	return false;
}

bool UHapbeatEventMap::FindByEventId(const FString& EventId, FHapbeatEventEntry& OutEntry) const
{
	// Scan the whole array even after a hit: the count is what makes an ambiguous
	// lookup reportable. Event ids are not unique by design (see the header), so
	// silently taking the first match would hide a mis-authored duplicate.
	int32 FirstMatch = INDEX_NONE;
	int32 MatchCount = 0;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		if (Entries[Index].GetEventId() == EventId)
		{
			if (FirstMatch == INDEX_NONE)
			{
				FirstMatch = Index;
			}
			++MatchCount;
		}
	}

	if (FirstMatch == INDEX_NONE)
	{
		UE_LOG(LogHapbeat, Warning,
			TEXT("FindByEventId: no entry with event id '%s' in '%s'. Check the entry's Category and Event Name (the id is \"<category>.<name>\" and is case-sensitive)."),
			*EventId, *GetName());
		return false;
	}

	if (MatchCount > 1)
	{
		UE_LOG(LogHapbeat, Warning,
			TEXT("FindByEventId: event id '%s' matches %d entries in '%s'; using the first (index %d, display name '%s'). Event ids are not unique - the same event is often authored twice (e.g. one-shot and looping). To address exactly one entry, reference it by Entry Id (PlayEntry) or pick it from the entry dropdown."),
			*EventId, MatchCount, *GetName(), FirstMatch, *Entries[FirstMatch].DisplayName);
	}

	OutEntry = Entries[FirstMatch];
	return true;
}

#if WITH_EDITOR
void UHapbeatEventMap::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	AssignMissingIds();
}

void UHapbeatEventMap::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
	AssignMissingIds();
}

void UHapbeatEventMap::AssignMissingIds()
{
	bool bChanged = false;
	for (FHapbeatEventEntry& Entry : Entries)
	{
		// A freshly added (default-constructed) or duplicated-with-clear entry
		// has an all-zero, invalid Id. Give it a unique one. Duplicate entries
		// copied via the Details panel keep their source Id, so we also detect
		// collisions below.
		if (!Entry.Id.IsValid())
		{
			Entry.Id = FGuid::NewGuid();
			bChanged = true;
		}
	}

	// Detect duplicate Ids produced by an array-element "duplicate" (which deep-
	// copies the source struct, Id included) and re-issue fresh ones so every
	// entry's Id stays unique. First occurrence keeps its Id; later ones rotate.
	TSet<FGuid> Seen;
	Seen.Reserve(Entries.Num());
	for (FHapbeatEventEntry& Entry : Entries)
	{
		bool bAlreadySeen = false;
		Seen.Add(Entry.Id, &bAlreadySeen);
		if (bAlreadySeen)
		{
			Entry.Id = FGuid::NewGuid();
			Seen.Add(Entry.Id);
			bChanged = true;
		}
	}

	if (bChanged)
	{
		Modify();
	}
}
#endif
