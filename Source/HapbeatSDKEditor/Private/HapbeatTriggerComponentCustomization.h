// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class UHapbeatEventMap;
class IPropertyHandle;
class IDetailCategoryBuilder;
template <typename OptionType> class SComboBox;

/**
 * Detail customization for UHapbeatTriggerComponent (and, via UE's detail
 * customization lookup climbing the class hierarchy when the exact class has
 * no registered customization of its own -- verified against
 * DetailLayoutHelpers.cpp's "Ensure that the base class and its parents are
 * always queried" pass -- also applies to UHapbeatCollisionTriggerComponent
 * and UHapbeatSequenceComponent without a second registration).
 *
 * Replaces the raw FGuid spinner row(s) for EntryId (base) and, when the
 * customized object is a UHapbeatSequenceComponent, StartEntryId/StopEntryId
 * with a dropdown listing the assigned EventMap's entries by DisplayName
 * (falling back to the event id, falling back to a short guid). Closes the
 * TODO(Phase 5) left in HapbeatTriggerComponent.h.
 *
 * Only active for a single selected object -- multi-editing several
 * differently-configured components with a shared picker would be ambiguous,
 * so CustomizeDetails() no-ops (leaving the default per-property FGuid rows)
 * when more than one object is being customized.
 *
 * When EventMap is unset, the picker for that property falls back to the
 * default raw FGuid row too (there is nothing to pick from) -- and this is
 * genuinely LIVE: EventMap's property handle has a change listener that force-
 * refreshes the whole details panel, so assigning / clearing the EventMap
 * asset while the panel is open flips between the raw row and the picker
 * immediately.
 */
class FHapbeatTriggerComponentCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/**
	 * Override the TSharedPtr form too (its IDetailCustomization base default
	 * just forwards to the reference overload above) so we can additionally
	 * capture a weak ref to the layout builder -- per that overload's own doc
	 * comment, this is what "allows property changes to trigger a force
	 * refresh of the detail panel", which HandleEventMapChanged() relies on.
	 */
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;

private:
	/** Per-picker state; one instance per customized FGuid property (EntryId / StartEntryId / StopEntryId). */
	struct FEntryPickerState
	{
		TSharedPtr<IPropertyHandle> GuidHandle;
		TSharedPtr<IPropertyHandle> EventMapHandle; // shared across pickers on the same object
		TWeakObjectPtr<UHapbeatEventMap> WeakMap;
		TArray<TSharedPtr<FGuid>> Options; // index 0 is always the "(none)" invalid guid
	};

	/**
	 * If EventMapHandle currently resolves to a valid UHapbeatEventMap: hide the
	 * property's default row and add a picker row in its place. If it resolves
	 * to null: do nothing, so the default raw FGuid row renders as-is (task
	 * requirement "when EventMap is null show the raw GUID row as-is").
	 */
	void BuildEntryPicker(IDetailLayoutBuilder& DetailBuilder, IDetailCategoryBuilder& Category,
		TSharedRef<IPropertyHandle> EventMapHandle, FName PropertyName, const UClass* OwningClass,
		const FText& DisplayLabel);

	/** Force the whole details panel to rebuild (re-running CustomizeDetails) so a live EventMap assign/clear flips raw-row <-> picker immediately. */
	void HandleEventMapChanged();

	static UHapbeatEventMap* ResolveEventMap(const TSharedPtr<IPropertyHandle>& EventMapHandle);
	// Option list, entry label and the FGuid handle read/write live in
	// HapbeatEntryPicker.h -- the FHapbeatEntryRef graph pin and its Details row
	// present the same dropdown and must label entries identically.

	TSharedRef<SWidget> OnGenerateOptionWidget(TSharedPtr<FGuid> InId, TWeakPtr<FEntryPickerState> WeakState) const;
	void OnOptionSelected(TSharedPtr<FGuid> NewSelection, ESelectInfo::Type SelectInfo, TWeakPtr<FEntryPickerState> WeakState);
	FText GetSelectedLabel(TWeakPtr<FEntryPickerState> WeakState) const;

	/** Keyed by property name so EntryId / StartEntryId / StopEntryId keep independent combo state. Kept alive for the customization instance's lifetime (Slate delegates hold weak refs into it). */
	TMap<FName, TSharedRef<FEntryPickerState>> Pickers;

	TWeakPtr<IDetailLayoutBuilder> WeakDetailBuilder;
};
