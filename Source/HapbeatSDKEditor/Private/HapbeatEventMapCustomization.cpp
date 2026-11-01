// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEventMapCustomization.h"

#include "HapbeatEditorSender.h"
#include "HapbeatEventMap.h"
#include "HapbeatManifestClipGainBaker.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FHapbeatEventMapCustomization"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatEditor, Log, All);

// Static: survives the RequestForceRefresh instance swap (see header).
FText FHapbeatEventMapCustomization::LastRefreshSummary;

TSharedRef<IDetailCustomization> FHapbeatEventMapCustomization::MakeInstance()
{
	return MakeShared<FHapbeatEventMapCustomization>();
}

void FHapbeatEventMapCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	WeakEventMap = (Objects.Num() == 1) ? Cast<UHapbeatEventMap>(Objects[0].Get()) : nullptr;
	WeakPropertyUtilities = DetailBuilder.GetPropertyUtilities();

	// "Hapbeat" is the same category the Entries array itself declares
	// (UPROPERTY(Category = "Hapbeat")); Important boosts the whole category
	// above Default-priority ones so these rows sit near the top of the panel.
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("Hapbeat", FText::GetEmpty(), ECategoryPriority::Important);

	if (!WeakEventMap.IsValid())
	{
		// Multi-select (or something odd being viewed) -- leave the plain
		// native Entries array editor as the only UI, no tooling rows.
		return;
	}

	RefreshEntryOptions();

	// Row 1: manifest-clip-gain bake action.
	Category.AddCustomRow(LOCTEXT("RefreshFilter", "Refresh Clip Gains Manifest"))
		.WholeRowContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("RefreshClipGains", "Refresh Clip Gains"))
					.ToolTipText(LOCTEXT("RefreshClipGainsTooltip",
						"Re-scan every *-manifest.json under Content/ and bake each entry's "
						"parameters.clip_gain into CachedManifestClipGain. Run after (re)deploying a Kit."))
					.OnClicked(this, &FHapbeatEventMapCustomization::OnRefreshClipGainsClicked)
				]
			+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &FHapbeatEventMapCustomization::GetRefreshResultText)
					.AutoWrapText(true)
				]
		];

	// Row 2: entry picker.
	Category.AddCustomRow(LOCTEXT("TestEntryFilter", "Test Entry"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TestEntry", "Test Entry"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(400.0f)
		[
			SAssignNew(EntryComboBox, SComboBox<TSharedPtr<FGuid>>)
			.OptionsSource(&EntryOptions)
			.OnGenerateWidget(this, &FHapbeatEventMapCustomization::OnGenerateEntryWidget)
			.OnSelectionChanged(this, &FHapbeatEventMapCustomization::OnEntrySelectionChanged)
			[
				SNew(STextBlock)
				.Text(this, &FHapbeatEventMapCustomization::GetSelectedEntryLabel)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];

	// Row 3: Test Play transport buttons.
	Category.AddCustomRow(LOCTEXT("TestPlayFilter", "Test Play Stop Ping"))
		.WholeRowContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("TestPlay", "Test Play"))
					.ClickMethod(EButtonClickMethod::MouseDown)
					.IsEnabled(this, &FHapbeatEventMapCustomization::IsTestPlayEnabled)
					.ToolTipText(this, &FHapbeatEventMapCustomization::GetTestPlayTooltip)
					.OnClicked(this, &FHapbeatEventMapCustomization::OnTestPlayClicked)
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Stop", "Stop"))
					.ToolTipText(LOCTEXT("StopTooltip", "Send STOP for the selected entry's event id."))
					.OnClicked(this, &FHapbeatEventMapCustomization::OnStopClicked)
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("StopAll", "Stop All"))
					.ToolTipText(LOCTEXT("StopAllTooltip", "Broadcast STOP_ALL to every device."))
					.OnClicked(this, &FHapbeatEventMapCustomization::OnStopAllClicked)
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Ping", "Ping"))
					.ToolTipText(LOCTEXT("PingTooltip", "Broadcast a PING (connectivity sanity check; replies are not read back here)."))
					.OnClicked(this, &FHapbeatEventMapCustomization::OnPingClicked)
				]
		];
}

// ---- entry picker ----

void FHapbeatEventMapCustomization::RefreshEntryOptions()
{
	EntryOptions.Reset();
	EntryOptions.Add(MakeShared<FGuid>()); // index 0 = "(none)" / invalid guid

	if (UHapbeatEventMap* Map = WeakEventMap.Get())
	{
		for (const FHapbeatEventEntry& Entry : Map->Entries)
		{
			if (Entry.Id.IsValid())
			{
				EntryOptions.Add(MakeShared<FGuid>(Entry.Id));
			}
		}
	}

	// Keep the current selection if it is still present; otherwise fall back
	// to "(none)" rather than silently pointing Test Play at a stale entry.
	const bool bSelectionStillValid = SelectedEntryId.IsValid() && EntryOptions.ContainsByPredicate(
		[this](const TSharedPtr<FGuid>& Candidate) { return Candidate.IsValid() && *Candidate == *SelectedEntryId; });
	if (!bSelectionStillValid)
	{
		SelectedEntryId = EntryOptions[0];
	}
}

bool FHapbeatEventMapCustomization::TryGetSelectedEntry(FHapbeatEventEntry& OutEntry) const
{
	UHapbeatEventMap* Map = WeakEventMap.Get();
	if (Map == nullptr || !SelectedEntryId.IsValid() || !SelectedEntryId->IsValid())
	{
		return false;
	}
	return Map->FindById(*SelectedEntryId, OutEntry);
}

FText FHapbeatEventMapCustomization::DescribeEntry(const FHapbeatEventEntry& Entry) const
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
	return FText::Format(LOCTEXT("EntryShortGuid", "(unnamed {0})"), FText::FromString(Entry.Id.ToString().Left(8)));
}

TSharedRef<SWidget> FHapbeatEventMapCustomization::OnGenerateEntryWidget(TSharedPtr<FGuid> InId) const
{
	FText Label = LOCTEXT("NoneEntry", "(none)");
	if (InId.IsValid() && InId->IsValid())
	{
		FHapbeatEventEntry Entry;
		if (UHapbeatEventMap* Map = WeakEventMap.Get())
		{
			if (Map->FindById(*InId, Entry))
			{
				Label = DescribeEntry(Entry);
			}
		}
	}
	return SNew(STextBlock)
		.Text(Label)
		.Font(IDetailLayoutBuilder::GetDetailFont());
}

void FHapbeatEventMapCustomization::OnEntrySelectionChanged(TSharedPtr<FGuid> NewSelection, ESelectInfo::Type SelectInfo)
{
	SelectedEntryId = NewSelection.IsValid() ? NewSelection : EntryOptions[0];
}

FText FHapbeatEventMapCustomization::GetSelectedEntryLabel() const
{
	FHapbeatEventEntry Entry;
	if (TryGetSelectedEntry(Entry))
	{
		return DescribeEntry(Entry);
	}
	return LOCTEXT("NoneEntry", "(none)");
}

// ---- Refresh Clip Gains ----

FReply FHapbeatEventMapCustomization::OnRefreshClipGainsClicked()
{
	if (UHapbeatEventMap* Map = WeakEventMap.Get())
	{
		const FHapbeatManifestClipGainBaker::FBakeResult Result = FHapbeatManifestClipGainBaker::BakeIntoEventMap(Map);
		LastRefreshSummary = FText::Format(
			LOCTEXT("RefreshResult", "Scanned {0} manifest(s): {1} resolved, {2} unresolved."),
			FText::AsNumber(Result.NumManifestsScanned),
			FText::AsNumber(Result.NumResolved),
			FText::AsNumber(Result.NumUnresolved));
		UE_LOG(LogHapbeatEditor, Log, TEXT("Refresh Clip Gains (%s): %s"),
			*GetNameSafe(Map), *LastRefreshSummary.ToString());

		// CachedManifestClipGain is VisibleAnywhere inside the native Entries
		// array rows; a plain field write (not via a property handle) does not
		// auto-refresh those rows, so force it explicitly. RequestForceRefresh
		// (not the immediate ForceRefresh) is deliberately used here: we are
		// still inside this button's own OnClicked call stack, and an
		// immediate rebuild could tear down the widget invoking it.
		if (TSharedPtr<IPropertyUtilities> Utilities = WeakPropertyUtilities.Pin())
		{
			Utilities->RequestForceRefresh();
		}
	}
	return FReply::Handled();
}

FText FHapbeatEventMapCustomization::GetRefreshResultText() const
{
	return LastRefreshSummary;
}

// ---- Test Play transport ----

bool FHapbeatEventMapCustomization::IsTestPlayEnabled() const
{
	FHapbeatEventEntry Entry;
	return TryGetSelectedEntry(Entry) && Entry.Mode == EHapticMode::Command;
}

FText FHapbeatEventMapCustomization::GetTestPlayTooltip() const
{
	FHapbeatEventEntry Entry;
	if (!TryGetSelectedEntry(Entry))
	{
		return LOCTEXT("TestPlayNoSelection", "Select an entry above to test.");
	}
	if (Entry.Mode == EHapticMode::StreamClip)
	{
		return LOCTEXT("TestPlayStreamNote",
			"StreamClip entries are test-played from the Event Map window (Tools > Hapbeat Event Map), "
			"which streams the clip in place without entering PIE and stops on its Stop button. "
			"This Details transport covers Command entries.");
	}
	if (Entry.CachedManifestClipGain < 0.0f)
	{
		return LOCTEXT("TestPlayHintUnresolved",
			"Send a PLAY command for this entry's event id. Manifest clip gain is UNRESOLVED (-1): "
			"plain Gain will be sent without the clip gain factor -- run Refresh Clip Gains first.");
	}
	return LOCTEXT("TestPlayHint", "Send a PLAY command for this entry's event id at its effective gain (Gain x CachedManifestClipGain).");
}

FReply FHapbeatEventMapCustomization::OnTestPlayClicked()
{
	FHapbeatEventEntry Entry;
	if (TryGetSelectedEntry(Entry) && Entry.Mode == EHapticMode::Command)
	{
		if (Entry.CachedManifestClipGain < 0.0f)
		{
			// Same designer-facing warning Unity surfaces for a missing manifest
			// clip gain: the wire gain is plain Gain (clip gain factor skipped).
			UE_LOG(LogHapbeatEditor, Warning,
				TEXT("Test Play '%s': manifest clip gain unresolved (-1); sending plain gain=%.2f. Run Refresh Clip Gains and confirm the Kit manifest is under Content/."),
				*Entry.GetEventId(), Entry.Gain);
		}
		FHapbeatEditorSender::SendPlay(Entry.GetEventId(), Entry.GetEffectiveGain(), Entry.Target, Entry.Pan);
	}
	return FReply::Handled();
}

FReply FHapbeatEventMapCustomization::OnStopClicked()
{
	FHapbeatEventEntry Entry;
	if (TryGetSelectedEntry(Entry))
	{
		FHapbeatEditorSender::SendStop(Entry.GetEventId(), Entry.Target);
	}
	return FReply::Handled();
}

FReply FHapbeatEventMapCustomization::OnStopAllClicked()
{
	FHapbeatEditorSender::SendStopAll();
	return FReply::Handled();
}

FReply FHapbeatEventMapCustomization::OnPingClicked()
{
	FHapbeatEditorSender::SendPing();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
