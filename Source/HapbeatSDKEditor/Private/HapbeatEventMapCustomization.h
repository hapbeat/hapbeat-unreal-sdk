// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class UHapbeatEventMap;
struct FHapbeatEventEntry;
class IPropertyUtilities;
template <typename OptionType> class SComboBox;

/**
 * Detail customization for UHapbeatEventMap. Adds a "Hapbeat" category row
 * block above the (unmodified) native Entries array editor:
 *   - [Refresh Intensities] -- re-bakes every entry's CachedManifestIntensity
 *     from every *-manifest.json under Content/ (FHapbeatManifestIntensityBaker).
 *   - An entry picker (by DisplayName / event id / short guid) + [Test Play]
 *     [Stop] [Stop All] [Ping] wired to the editor-only UDP sender
 *     (FHapbeatEditorSender) so a designer can sanity-check gain / wiring
 *     against a real device without entering PIE.
 *
 * StreamClip entries cannot be meaningfully "test played" as a Command PLAY
 * (the device has no local clip for them) -- Test Play is disabled with an
 * explanatory tooltip for those entries; v1 streaming can only be exercised
 * in PIE via the runtime UHapbeatSubsystem::StreamClip API.
 *
 * UE counterpart of the "test-play path" described in
 * Hapbeat.Editor.HapbeatManifestIntensity's class doc comment (Unity SDK) --
 * intentionally NOT a clone of Unity's ~12k-line EventMap window: the native
 * Details panel already gives add/remove/reorder/duplicate/multi-edit on
 * Entries for free (see the design doc §3.6 "minimal effort" principle).
 */
class FHapbeatEventMapCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	// ---- entry picker ----
	void RefreshEntryOptions();
	bool TryGetSelectedEntry(FHapbeatEventEntry& OutEntry) const;
	FText DescribeEntry(const FHapbeatEventEntry& Entry) const;

	TSharedRef<SWidget> OnGenerateEntryWidget(TSharedPtr<FGuid> InId) const;
	void OnEntrySelectionChanged(TSharedPtr<FGuid> NewSelection, ESelectInfo::Type SelectInfo);
	FText GetSelectedEntryLabel() const;

	// ---- Refresh Intensities ----
	FReply OnRefreshIntensitiesClicked();
	FText GetRefreshResultText() const;

	// ---- Test Play transport ----
	FReply OnTestPlayClicked();
	FReply OnStopClicked();
	FReply OnStopAllClicked();
	FReply OnPingClicked();
	bool IsTestPlayEnabled() const;
	FText GetTestPlayTooltip() const;

	TWeakObjectPtr<UHapbeatEventMap> WeakEventMap;
	TWeakPtr<IPropertyUtilities> WeakPropertyUtilities;

	/** Index 0 is always the "(none)" invalid guid; the rest mirror WeakEventMap->Entries at the last CustomizeDetails() call. */
	TArray<TSharedPtr<FGuid>> EntryOptions;
	TSharedPtr<FGuid> SelectedEntryId;
	TSharedPtr<SComboBox<TSharedPtr<FGuid>>> EntryComboBox;

	/**
	 * Static: RequestForceRefresh() rebuilds the panel with a NEW customization
	 * instance, so per-instance state would show the summary for at most one
	 * frame. Shared across EventMap panels (one refresh summary at a time —
	 * acceptable; the same text also goes to the Output Log).
	 */
	static FText LastRefreshSummary;
};
