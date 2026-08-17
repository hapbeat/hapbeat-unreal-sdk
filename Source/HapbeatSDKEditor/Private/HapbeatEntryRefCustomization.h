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
 * Details-panel counterpart of SHapbeatEntryRefGraphPin: gives a
 * FHapbeatEntryRef property the same by-name entry dropdown it gets on a
 * Blueprint graph pin, instead of the raw FGuid spinners.
 *
 * Which map's entries to list is a question this struct cannot answer on its
 * own (it holds a GUID and nothing else), so the OWNING class points at its
 * Event Map property by name:
 *
 *     UPROPERTY(EditAnywhere, Category = "Hapbeat")
 *     TObjectPtr<UHapbeatEventMap> EventMap;
 *
 *     UPROPERTY(EditAnywhere, Category = "Hapbeat", meta = (HapbeatEventMap = "EventMap"))
 *     FHapbeatEntryRef Entry;
 *
 * This mirrors how the graph pin reads the neighbouring Map pin: the map is
 * always stated next to the entry, never folded into it (see HapbeatEntryRef.h).
 *
 * Without that meta -- or when it names something unresolvable, or the map is
 * unset, or several objects with different maps are selected -- the row falls
 * back to the default raw GUID widget with a tooltip explaining how to get the
 * dropdown. Failing that way keeps the value editable instead of showing an
 * empty picker the author cannot use.
 */
class FHapbeatEntryRefCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
		class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	/** Nothing to expand: the single EntryId child is what the header row already edits. */
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
		class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}

private:
	/**
	 * Find the sibling Event Map property handle named by the
	 * meta = (HapbeatEventMap = "...") specifier on the customized property.
	 * Invalid when the meta is absent or names a property that is not there.
	 */
	TSharedPtr<IPropertyHandle> FindEventMapHandle(const TSharedRef<IPropertyHandle>& StructPropertyHandle) const;

	/** Load the map the sibling handle currently holds. Null when unset or multi-valued. */
	UHapbeatEventMap* ResolveEventMap() const;

	/** Rebuild the option list from the sibling's CURRENT map (called on every combo open, so a map assigned later is picked up). */
	void RefreshEntryOptions();

	/** Clear the entry (a guid from another map cannot resolve) and rebuild the row. */
	void HandleEventMapChanged();

	TSharedRef<SWidget> OnGenerateEntryWidget(TSharedPtr<FGuid> InId) const;
	void OnEntrySelected(TSharedPtr<FGuid> NewSelection, ESelectInfo::Type SelectInfo);
	FText GetSelectedEntryLabel() const;

	/** The FGuid child of the customized struct. */
	TSharedPtr<IPropertyHandle> EntryIdHandle;

	/** The sibling Event Map property, resolved through the meta specifier. Invalid when unresolvable. */
	TSharedPtr<IPropertyHandle> EventMapHandle;

	/** Index 0 is always the "(none)" invalid guid (see HapbeatEditor::BuildEntryOptions). */
	TArray<TSharedPtr<FGuid>> EntryOptions;

	TSharedPtr<SComboBox<TSharedPtr<FGuid>>> EntryCombo;

	/** Used to rebuild the row when the sibling Event Map changes, so the raw-row <-> picker swap is live. */
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};
