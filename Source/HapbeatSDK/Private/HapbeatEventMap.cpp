// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEventMap.h"

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
