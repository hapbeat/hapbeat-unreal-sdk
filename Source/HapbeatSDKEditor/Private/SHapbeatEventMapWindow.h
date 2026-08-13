// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatEventEntry.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"

class SWidget;
class UHapbeatEventMap;

/** One component in the level that fires the scanned entry. */
struct FHapbeatWiringHit
{
	TWeakObjectPtr<class AActor> Actor;
	TWeakObjectPtr<class UHapbeatTriggerComponent> Component;
	FString ComponentClass;
};

/**
 * Dedicated Event Map editor, mirroring the Unity SDK's window.
 *
 * The Details panel alone was a poor fit for this asset: an EventMap is a long
 * list of small records, and the panel renders it as one flat vertical stack of
 * array rows, so picking an entry means scrolling past every other entry's
 * fields. This window instead splits browsing from editing -- entry list on the
 * left, one entry's fields on the right -- and groups those fields the way they
 * are actually reasoned about (identity, then event, then playback, then
 * targeting), which is the same shape the Unity window settled on.
 *
 * The Details customization (FHapbeatEventMapCustomization) is kept: it still
 * serves the raw array and is the fallback when several assets are selected.
 */
class SHapbeatEventMapWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHapbeatEventMapWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Tab id used by the global tab manager (Window menu entry). */
	static const FName TabId;

	/** Registers / unregisters the nomad tab. Called from the editor module. */
	static void RegisterTabSpawner();
	static void UnregisterTabSpawner();

private:
	// ---- data access ----

	/** The entry currently selected in the left list, or null. */
	FHapbeatEventEntry* FindSelectedEntry() const;

	/**
	 * Runs Mutator inside a transaction and flags the package dirty.
	 *
	 * Entries are edited as plain struct fields rather than through property
	 * handles (this window does not own a Details view), so undo support and the
	 * dirty flag have to be arranged here instead of coming for free.
	 */
	void ModifySelectedEntry(const FText& TransactionLabel, TFunctionRef<void(FHapbeatEventEntry&)> Mutator);

	/** Rebuilds the left list from the asset, preserving the selection when possible. */
	void RefreshEntryList();

	// ---- top bar ----

	FString GetEventMapPath() const;
	void OnEventMapChanged(const FAssetData& AssetData);
	FReply OnRefreshIntensitiesClicked();
	FText GetRefreshSummary() const;

	// ---- entry list ----

	TSharedRef<ITableRow> OnGenerateEntryRow(TSharedPtr<FGuid> InId, const TSharedRef<STableViewBase>& OwnerTable);
	void OnEntrySelectionChanged(TSharedPtr<FGuid> NewSelection, ESelectInfo::Type SelectInfo);
	FReply OnAddEntryClicked();
	FReply OnRemoveEntryClicked();

	// ---- detail pane ----

	TSharedRef<SWidget> BuildDetailPane();
	TSharedRef<SWidget> BuildIdentitySection();
	TSharedRef<SWidget> BuildEventSection();
	TSharedRef<SWidget> BuildPlaybackSection();
	TSharedRef<SWidget> BuildTargetingSection();
	TSharedRef<SWidget> BuildNotesSection();
	TSharedRef<SWidget> BuildWiringSection();
	TSharedRef<SWidget> BuildTestSection();

	/** Label + value row; the fixed label column is what gives the pane its two-column look. */
	static TSharedRef<SWidget> MakeRow(const FText& Label, const FText& Tooltip, TSharedRef<SWidget> Value);
	/** Collapsible section wrapper used by every Build*Section(). */
	static TSharedRef<SWidget> MakeSection(const FText& Title, TSharedRef<SWidget> Body);

	/** Detail widgets are hidden wholesale while nothing is selected. */
	EVisibility GetDetailVisibility() const;
	/** StreamClip-only rows. */
	EVisibility GetStreamClipVisibility() const;
	/** Command-only rows. */
	EVisibility GetCommandVisibility() const;

	// ---- targeting helpers ----
	//
	// Target is stored as a single spec string; the three spin/combo boxes below
	// are a decomposed view of it, round-tripped through FHapbeatTargetLibrary
	// so the string stays the single source of truth.
	int32 GetTargetPlayer() const;
	int32 GetTargetGroup() const;
	FString GetTargetPosition() const;
	void SetTargetParts(int32 Player, const FString& Position, int32 Group);

	// ---- state ----

	TWeakObjectPtr<UHapbeatEventMap> WeakEventMap;

	TArray<TSharedPtr<FGuid>> EntryIds;
	TSharedPtr<SListView<TSharedPtr<FGuid>>> EntryListView;
	TSharedPtr<FGuid> SelectedEntryId;

	/** Options for the Mode combo. */
	TArray<TSharedPtr<EHapticMode>> ModeOptions;
	/** Options for the body-position combo (spec vocabulary + an empty "any"). */
	TArray<TSharedPtr<FString>> PositionOptions;

	FText RefreshSummary;

	// ---- wiring (reverse lookup) ----
	//
	// Triggers reference an entry by id, so an EventMap on its own cannot say
	// who fires it. Answering "is this entry actually used, and by what?"
	// requires walking the level, which is why this is an explicit scan rather
	// than something kept live.
	void RefreshWiring();
	TSharedRef<ITableRow> OnGenerateWiringRow(TSharedPtr<FHapbeatWiringHit> InHit, const TSharedRef<STableViewBase>& OwnerTable);

	TArray<TSharedPtr<FHapbeatWiringHit>> WiringHits;
	TSharedPtr<SListView<TSharedPtr<FHapbeatWiringHit>>> WiringListView;
	/** Entry the wiring list was built for, so a stale list is never shown. */
	FGuid WiringScannedFor;
};
