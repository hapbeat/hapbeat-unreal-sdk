// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEntryPicker.h"

#include "HapbeatEventEntry.h"
#include "HapbeatEventMap.h"

#include "PropertyHandle.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "HapbeatEntryPicker"

namespace HapbeatEditor
{

FText DescribeEntryById(const FGuid& Id, const UHapbeatEventMap* Map)
{
	if (!Id.IsValid())
	{
		return LOCTEXT("NoneEntry", "(none)");
	}
	FHapbeatEventEntry Entry;
	if (Map != nullptr && Map->FindById(Id, Entry))
	{
		if (!Entry.DisplayName.IsEmpty())
		{
			return FText::FromString(Entry.DisplayName);
		}
		const FString EventId = Entry.GetEventId();
		if (!EventId.IsEmpty())
		{
			return FText::FromString(EventId);
		}
		return FText::Format(LOCTEXT("EntryShortGuid", "(unnamed {0})"), FText::FromString(Id.ToString().Left(8)));
	}
	return FText::Format(LOCTEXT("StaleEntry", "(stale: {0})"), FText::FromString(Id.ToString().Left(8)));
}

void BuildEntryOptions(const UHapbeatEventMap* Map, TArray<TSharedPtr<FGuid>>& OutOptions)
{
	OutOptions.Reset();
	OutOptions.Add(MakeShared<FGuid>()); // index 0 = "(none)"
	if (Map == nullptr)
	{
		return;
	}
	for (const FHapbeatEventEntry& Entry : Map->Entries)
	{
		// Entries whose Id has not been assigned yet (a brand new array element
		// before PostEditChangeProperty runs) are unaddressable, so offering
		// them would only let the author store an all-zero "(none)".
		if (Entry.Id.IsValid())
		{
			OutOptions.Add(MakeShared<FGuid>(Entry.Id));
		}
	}
}

bool ReadGuidFromHandle(const TSharedPtr<IPropertyHandle>& Handle, FGuid& OutGuid)
{
	if (!Handle.IsValid())
	{
		return false;
	}
	TArray<void*> RawData;
	Handle->AccessRawData(RawData);
	if (RawData.Num() != 1 || RawData[0] == nullptr)
	{
		return false;
	}
	OutGuid = *static_cast<FGuid*>(RawData[0]);
	return true;
}

void WriteGuidToHandle(const TSharedPtr<IPropertyHandle>& Handle, const FGuid& NewGuid)
{
	if (!Handle.IsValid())
	{
		return;
	}
	FScopedTransaction Transaction(LOCTEXT("SetEntryId", "Set Hapbeat Entry Id"));
	for (int32 ChildIndex = 0; ChildIndex < 4; ++ChildIndex)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = Handle->GetChildHandle(ChildIndex);
		if (!ChildHandle.IsValid())
		{
			continue;
		}
		// First 3 components are flagged interactive + non-transactable so only
		// one combined undo transaction (the FScopedTransaction above) is
		// created for the whole 4-component write, and PostEditChange doesn't
		// reinstance anything until the final component lands.
		const EPropertyValueSetFlags::Type Flags = (ChildIndex != 3)
			? (EPropertyValueSetFlags::InteractiveChange | EPropertyValueSetFlags::NotTransactable)
			: EPropertyValueSetFlags::NotTransactable;
		ChildHandle->SetValue(static_cast<int32>(NewGuid[ChildIndex]), Flags);
	}
}

} // namespace HapbeatEditor

#undef LOCTEXT_NAMESPACE
