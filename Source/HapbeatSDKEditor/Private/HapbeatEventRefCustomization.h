// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class IPropertyUtilities;
class SWidget;
class UHapbeatEventMap;
template <typename OptionType> class SComboBox;

/**
 * Details-panel counterpart of SHapbeatEventRefGraphPin: gives a
 * FHapbeatEventRef property the same two rows (Event Map asset, then that
 * map's entries by name) it gets on a Blueprint graph pin.
 *
 * Without this, an FHapbeatEventRef exposed on an actor or component would
 * show the raw FGuid spinners while the same struct on a graph pin showed a
 * name -- the same value edited two different ways depending on where it is.
 * The option list and the entry labels come from HapbeatEntryPicker.h, shared
 * with both the pin and the trigger component's picker.
 *
 * Multi-select is deliberately not supported for the entry dropdown: two
 * selected objects may reference different maps, so a single shared list of
 * entries would be meaningless. In that case the entry row shows "Multiple
 * Values" and the Event Map row keeps its normal multi-edit behaviour.
 */
class FHapbeatEventRefCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
		class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
		class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	/** Load the map currently referenced by the EventMap child handle. Null when unset, missing, or multi-valued. */
	UHapbeatEventMap* ResolveEventMap() const;

	/** Clear the entry (a guid from another map cannot resolve) and rebuild the row. */
	void HandleEventMapChanged();

	TSharedRef<SWidget> OnGenerateEntryWidget(TSharedPtr<FGuid> InId) const;
	void OnEntrySelected(TSharedPtr<FGuid> NewSelection, ESelectInfo::Type SelectInfo);
	FText GetSelectedEntryLabel() const;

	TSharedPtr<IPropertyHandle> EventMapHandle;
	TSharedPtr<IPropertyHandle> EntryIdHandle;

	/** Index 0 is always the "(none)" invalid guid (see HapbeatEditor::BuildEntryOptions). */
	TArray<TSharedPtr<FGuid>> EntryOptions;

	/** Used to rebuild the whole row set when the Event Map changes, so the entry list follows it live. */
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};
