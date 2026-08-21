// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"

class IPropertyHandle;
class UHapbeatEventMap;

/**
 * Shared pieces of "choose an Event Map entry by name".
 *
 * Three places in this module now present the same dropdown -- the trigger
 * component's Details panel (FHapbeatTriggerComponentCustomization), the
 * FHapbeatEntryRef graph pin (SHapbeatEntryRefGraphPin) and the same struct's
 * Details row (FHapbeatEntryRefCustomization). They differ only in how they
 * reach the value; the option list and, above all, the LABEL rule must stay
 * identical, or the same entry would read differently depending on where the
 * author is looking at it. So both live here once.
 */
namespace HapbeatEditor
{
	/**
	 * Label shown for an entry id:
	 *   invalid id        -> "(none)"
	 *   found in Map      -> DisplayName, else the event id, else "(unnamed <8 hex>)"
	 *   not found in Map  -> "(stale: <8 hex>)"  (entry deleted after being referenced)
	 */
	FText DescribeEntryById(const FGuid& Id, const UHapbeatEventMap* Map);

	/**
	 * Rebuild a combo's option list from a map: index 0 is always the "(none)"
	 * invalid guid, followed by every entry with a valid Id, in map order.
	 * A null map yields the "(none)" option alone.
	 *
	 * Options already in OutOptions are REUSED for guids that are still present,
	 * rather than replaced by fresh shared pointers. SComboBox tracks its
	 * selection by pointer identity (SListView::SelectedItems), so handing it
	 * brand new pointers for the very same guids drops the current selection on
	 * the next list refresh -- which is how merely opening the dropdown used to
	 * reset a picked entry to "(none)".
	 */
	void BuildEntryOptions(const UHapbeatEventMap* Map, TArray<TSharedPtr<FGuid>>& OutOptions);

	/**
	 * The option holding Id, or the "(none)" option at index 0 when the list has
	 * no such guid (a stale reference to a deleted entry). Null only for an empty
	 * list. Used to restate a combo's selection after its options were rebuilt.
	 */
	TSharedPtr<FGuid> FindOptionForGuid(const TArray<TSharedPtr<FGuid>>& Options, const FGuid& Id);

	/** Read an FGuid through its property handle. False when the handle is invalid or the selection is multi-valued. */
	bool ReadGuidFromHandle(const TSharedPtr<IPropertyHandle>& Handle, FGuid& OutGuid);

	/**
	 * Write an FGuid through its four reflected int32 child handles inside a
	 * single undo transaction -- the mechanism the engine's own FGuid struct
	 * customization uses (Editor/DetailCustomizations/Private/
	 * GuidStructCustomization.cpp, WriteGuidToProperty): FGuid has no direct
	 * IPropertyHandle::SetValue overload, but its A/B/C/D components (see
	 * CoreUObject/Public/UObject/NoExportTypes.h) are ordinary child
	 * properties at indices 0..3 in that order.
	 */
	void WriteGuidToHandle(const TSharedPtr<IPropertyHandle>& Handle, const FGuid& NewGuid);
}
