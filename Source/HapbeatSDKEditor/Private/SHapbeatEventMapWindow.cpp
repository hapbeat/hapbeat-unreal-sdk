// Copyright (c) 2026 Hapbeat. MIT License.
#include "SHapbeatEventMapWindow.h"

#include "HapbeatClip.h"
#include "HapbeatEditorSender.h"
#include "HapbeatEventMap.h"
#include "HapbeatManifestIntensityBaker.h"
#include "HapbeatTargetLibrary.h"
#include "HapbeatTriggerComponent.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "IAssetTools.h"
#include "IDesktopPlatform.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SHapbeatEventMapWindow"

const FName SHapbeatEventMapWindow::TabId(TEXT("HapbeatEventMap"));
TWeakPtr<SHapbeatEventMapWindow> SHapbeatEventMapWindow::LastCreated;

namespace
{
	/** Width of the label column. Fixed so every row's value edge lines up. */
	constexpr float LabelColumnWidth = 116.0f;

	FText DescribeEntry(const FHapbeatEventEntry& Entry)
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
		return LOCTEXT("UnnamedEntry", "(unnamed)");
	}

	// FIRE / CLIP is the shorthand Studio and the Unity window use, so an author
	// moving between the tools sees one vocabulary. The engine-side enum names
	// are kept in parentheses because they are what appears in Blueprint and C++.
	FText DescribeMode(EHapticMode Mode)
	{
		return Mode == EHapticMode::StreamClip
			? LOCTEXT("ModeStreamClip", "CLIP (Stream Clip)")
			: LOCTEXT("ModeCommand", "FIRE (Command)");
	}

	/** Compact form for the entry list, where the row has no space for the long one. */
	FText DescribeModeShort(EHapticMode Mode)
	{
		return Mode == EHapticMode::StreamClip
			? LOCTEXT("ModeStreamClipShort", "CLIP")
			: LOCTEXT("ModeCommandShort", "FIRE");
	}

	// Button accents, mirroring the Unity window's palette: the primary action
	// stands out, stopping everything reads as destructive, and the rest stay
	// neutral so the eye lands on the two that matter.
	const FLinearColor TestPlayColor(0.20f, 0.55f, 0.30f);
	const FLinearColor StopAllColor(0.60f, 0.20f, 0.20f);
	const FLinearColor NeutralColor(0.25f, 0.25f, 0.28f);
}

// ---------------------------------------------------------------------------
// Tab registration
// ---------------------------------------------------------------------------

void SHapbeatEventMapWindow::RegisterTabSpawner()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&) -> TSharedRef<SDockTab>
		{
			TSharedRef<SHapbeatEventMapWindow> Window = SNew(SHapbeatEventMapWindow);
			LastCreated = Window;
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab)
				[
					Window
				];
		}))
		.SetDisplayName(LOCTEXT("TabTitle", "Hapbeat Event Map"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Browse and edit the entries of a Hapbeat Event Map."))
		// Deliberately NOT placed in the engine's Tools workspace category: that
		// listed the window among the engine's own tools, several screens away
		// from the plugin's other menu entries, so the SDK appeared in two
		// unrelated places at once. It is registered explicitly in the Hapbeat
		// section instead (FHapbeatUpdateCheck::Register), which keeps everything
		// this plugin adds in one group.
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));
}

void SHapbeatEventMapWindow::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

void SHapbeatEventMapWindow::OpenForAsset(UHapbeatEventMap* Map)
{
	// Invoking is also what focuses an already-open tab, so this both opens and
	// raises. The widget is only reachable afterwards -- TryInvokeTab hands back
	// the SDockTab, not its content.
	FGlobalTabmanager::Get()->TryInvokeTab(TabId);

	if (const TSharedPtr<SHapbeatEventMapWindow> Window = LastCreated.Pin())
	{
		Window->SetEventMap(Map);
	}
}

void SHapbeatEventMapWindow::SetEventMap(UHapbeatEventMap* Map)
{
	WeakEventMap = Map;
	SelectedEntryId.Reset();
	RefreshSummary = FText::GetEmpty();
	WiringHits.Reset();
	WiringScannedFor.Invalidate();
	RefreshEntryList();
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void SHapbeatEventMapWindow::Construct(const FArguments& InArgs)
{
	ModeOptions.Add(MakeShared<EHapticMode>(EHapticMode::Command));
	ModeOptions.Add(MakeShared<EHapticMode>(EHapticMode::StreamClip));

	// Leading empty option = "any position" (an empty segment in the spec).
	PositionOptions.Add(MakeShared<FString>(FString()));
	for (const FString& Position : FHapbeatEventEntry::StandardPositions())
	{
		PositionOptions.Add(MakeShared<FString>(Position));
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		// ---- top bar: which asset, and the manifest bake ----
		+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(6.0f, 6.0f, 6.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(STextBlock).Text(LOCTEXT("EventMapLabel", "Event Map"))
					]
				+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(UHapbeatEventMap::StaticClass())
						.ObjectPath(this, &SHapbeatEventMapWindow::GetEventMapPath)
						.OnObjectChanged(this, &SHapbeatEventMapWindow::OnEventMapChanged)
						.AllowClear(true)
						.DisplayUseSelected(true)
						.DisplayBrowse(true)
					]
				+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("RefreshIntensities", "Refresh Intensities"))
						.ToolTipText(LOCTEXT("RefreshIntensitiesTooltip",
							"Re-scan every *-manifest.json under the project and enabled plugins, and bake "
							"each entry's parameters.intensity into CachedManifestIntensity. Run after (re)deploying a Kit."))
						.OnClicked(this, &SHapbeatEventMapWindow::OnRefreshIntensitiesClicked)
					]
			]

		+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(6.0f, 0.0f, 6.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(this, &SHapbeatEventMapWindow::GetRefreshSummary)
				.AutoWrapText(true)
			]

		+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
			]

		// ---- body: entry list | entry detail ----
		+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)

				+ SSplitter::Slot()
					.Value(0.32f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							[
								SAssignNew(EntryListView, SListView<TSharedPtr<FGuid>>)
								.ListItemsSource(&EntryIds)
								.SelectionMode(ESelectionMode::Single)
								.OnGenerateRow(this, &SHapbeatEventMapWindow::OnGenerateEntryRow)
								.OnSelectionChanged(this, &SHapbeatEventMapWindow::OnEntrySelectionChanged)
							]
						+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(4.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
									.AutoWidth()
									.Padding(0.0f, 0.0f, 4.0f, 0.0f)
									[
										SNew(SButton)
										.Text(LOCTEXT("AddEntry", "Add"))
										.ToolTipText(LOCTEXT("AddEntryTooltip", "Append a new entry with a fresh stable id."))
										.OnClicked(this, &SHapbeatEventMapWindow::OnAddEntryClicked)
									]
								+ SHorizontalBox::Slot()
									.AutoWidth()
									[
										SNew(SButton)
										.Text(LOCTEXT("RemoveEntry", "Remove"))
										.ToolTipText(LOCTEXT("RemoveEntryTooltip",
											"Delete the selected entry. Triggers referencing its id stop resolving, so check the wiring first."))
										.OnClicked(this, &SHapbeatEventMapWindow::OnRemoveEntryClicked)
									]
							]
					]

				+ SSplitter::Slot()
					.Value(0.68f)
					[
						BuildDetailPane()
					]
			]
	];

	RefreshEntryList();
}

// ---------------------------------------------------------------------------
// Data access
// ---------------------------------------------------------------------------

FHapbeatEventEntry* SHapbeatEventMapWindow::FindSelectedEntry() const
{
	UHapbeatEventMap* Map = WeakEventMap.Get();
	if (Map == nullptr || !SelectedEntryId.IsValid() || !SelectedEntryId->IsValid())
	{
		return nullptr;
	}
	return Map->Entries.FindByPredicate(
		[this](const FHapbeatEventEntry& Candidate) { return Candidate.Id == *SelectedEntryId; });
}

void SHapbeatEventMapWindow::ModifySelectedEntry(const FText& TransactionLabel, TFunctionRef<void(FHapbeatEventEntry&)> Mutator)
{
	UHapbeatEventMap* Map = WeakEventMap.Get();
	if (Map == nullptr)
	{
		return;
	}
	FHapbeatEventEntry* Entry = FindSelectedEntry();
	if (Entry == nullptr)
	{
		return;
	}

	const FScopedTransaction Transaction(TransactionLabel);
	Map->Modify();
	Mutator(*Entry);
	Map->MarkPackageDirty();
}

void SHapbeatEventMapWindow::RefreshEntryList()
{
	EntryIds.Reset();

	if (UHapbeatEventMap* Map = WeakEventMap.Get())
	{
		for (const FHapbeatEventEntry& Entry : Map->Entries)
		{
			if (Entry.Id.IsValid())
			{
				EntryIds.Add(MakeShared<FGuid>(Entry.Id));
			}
		}
	}

	// Keep pointing at the same entry across a rebuild; the shared pointers are
	// recreated, so match on the guid value rather than pointer identity.
	TSharedPtr<FGuid> Restored;
	if (SelectedEntryId.IsValid() && SelectedEntryId->IsValid())
	{
		if (TSharedPtr<FGuid>* Found = EntryIds.FindByPredicate(
			[this](const TSharedPtr<FGuid>& Candidate) { return Candidate.IsValid() && *Candidate == *SelectedEntryId; }))
		{
			Restored = *Found;
		}
	}
	if (!Restored.IsValid() && EntryIds.Num() > 0)
	{
		Restored = EntryIds[0];
	}
	SelectedEntryId = Restored;

	if (EntryListView.IsValid())
	{
		EntryListView->RequestListRefresh();
		if (SelectedEntryId.IsValid())
		{
			EntryListView->SetSelection(SelectedEntryId, ESelectInfo::Direct);
		}
		else
		{
			EntryListView->ClearSelection();
		}
	}
}

// ---------------------------------------------------------------------------
// Top bar
// ---------------------------------------------------------------------------

FString SHapbeatEventMapWindow::GetEventMapPath() const
{
	const UHapbeatEventMap* Map = WeakEventMap.Get();
	return Map != nullptr ? Map->GetPathName() : FString();
}

void SHapbeatEventMapWindow::OnEventMapChanged(const FAssetData& AssetData)
{
	WeakEventMap = Cast<UHapbeatEventMap>(AssetData.GetAsset());
	SelectedEntryId.Reset();
	RefreshSummary = FText::GetEmpty();
	RefreshEntryList();
}

FReply SHapbeatEventMapWindow::OnRefreshIntensitiesClicked()
{
	if (UHapbeatEventMap* Map = WeakEventMap.Get())
	{
		const FHapbeatManifestIntensityBaker::FBakeResult Result = FHapbeatManifestIntensityBaker::BakeIntoEventMap(Map);
		RefreshSummary = FText::Format(
			LOCTEXT("RefreshResult", "Scanned {0} manifest(s): {1} resolved, {2} unresolved."),
			FText::AsNumber(Result.NumManifestsScanned),
			FText::AsNumber(Result.NumResolved),
			FText::AsNumber(Result.NumUnresolved));
	}
	return FReply::Handled();
}

FText SHapbeatEventMapWindow::GetRefreshSummary() const
{
	return RefreshSummary;
}

// ---------------------------------------------------------------------------
// Entry list
// ---------------------------------------------------------------------------

TSharedRef<ITableRow> SHapbeatEventMapWindow::OnGenerateEntryRow(TSharedPtr<FGuid> InId, const TSharedRef<STableViewBase>& OwnerTable)
{
	FText Label = LOCTEXT("MissingEntry", "(missing)");
	FText Mode = FText::GetEmpty();
	FText EventId = FText::GetEmpty();

	if (UHapbeatEventMap* Map = WeakEventMap.Get())
	{
		if (const FHapbeatEventEntry* Entry = Map->Entries.FindByPredicate(
			[&InId](const FHapbeatEventEntry& Candidate) { return InId.IsValid() && Candidate.Id == *InId; }))
		{
			Label = DescribeEntry(*Entry);
			Mode = DescribeModeShort(Entry->Mode);
			EventId = FText::FromString(Entry->GetEventId());
		}
	}

	return SNew(STableRow<TSharedPtr<FGuid>>, OwnerTable)
		.Padding(FMargin(4.0f, 3.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(Label)
						]
					+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(EventId)
							.Font(FAppStyle::GetFontStyle("SmallFont"))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(Mode)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
		];
}

void SHapbeatEventMapWindow::OnEntrySelectionChanged(TSharedPtr<FGuid> NewSelection, ESelectInfo::Type /*SelectInfo*/)
{
	SelectedEntryId = NewSelection;

	// Drop the previous entry's results rather than leaving them on screen
	// attached to the wrong entry. Scanning itself stays a deliberate action.
	WiringHits.Reset();
	WiringScannedFor.Invalidate();
	if (WiringListView.IsValid())
	{
		WiringListView->RequestListRefresh();
	}
}

// ---------------------------------------------------------------------------
// Wiring (reverse lookup)
// ---------------------------------------------------------------------------

void SHapbeatEventMapWindow::RefreshWiring()
{
	WiringHits.Reset();
	WiringScannedFor.Invalidate();

	const FHapbeatEventEntry* Entry = FindSelectedEntry();
	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (Entry != nullptr && World != nullptr)
	{
		WiringScannedFor = Entry->Id;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TArray<UHapbeatTriggerComponent*> Triggers;
			It->GetComponents(Triggers);
			for (UHapbeatTriggerComponent* Trigger : Triggers)
			{
				if (Trigger->EntryId == WiringScannedFor)
				{
					TSharedPtr<FHapbeatWiringHit> Hit = MakeShared<FHapbeatWiringHit>();
					Hit->Actor = *It;
					Hit->Component = Trigger;
					Hit->ComponentClass = Trigger->GetClass()->GetName();
					WiringHits.Add(Hit);
				}
			}
		}
	}

	if (WiringListView.IsValid())
	{
		WiringListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SHapbeatEventMapWindow::OnGenerateWiringRow(TSharedPtr<FHapbeatWiringHit> InHit, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FString ActorName = (InHit.IsValid() && InHit->Actor.IsValid())
		? InHit->Actor->GetActorNameOrLabel()
		: TEXT("(missing)");
	const FString ClassName = InHit.IsValid() ? InHit->ComponentClass : FString();

	return SNew(STableRow<TSharedPtr<FHapbeatWiringHit>>, OwnerTable)
		.Padding(FMargin(4.0f, 2.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(ActorName))
				]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(ClassName))
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("SelectActor", "Select"))
					.ToolTipText(LOCTEXT("SelectActorTooltip", "Select this actor in the level."))
					.OnClicked_Lambda([InHit]
					{
						if (InHit.IsValid() && InHit->Actor.IsValid() && GEditor != nullptr)
						{
							GEditor->SelectNone(false, true);
							GEditor->SelectActor(InHit->Actor.Get(), true, true);
						}
						return FReply::Handled();
					})
				]
		];
}

TSharedRef<SWidget> SHapbeatEventMapWindow::BuildWiringSection()
{
	return MakeSection(LOCTEXT("SectionWiring", "Wiring"),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("ScanLevel", "Scan Level"))
						.ToolTipText(LOCTEXT("ScanLevelTooltip",
							"Find every Hapbeat trigger in the open level that fires this entry. Triggers reference entries by id, so a scan is the only way to see who uses one."))
						.OnClicked_Lambda([this] { RefreshWiring(); return FReply::Handled(); })
					]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text_Lambda([this]
						{
							const FHapbeatEventEntry* Entry = FindSelectedEntry();
							if (Entry == nullptr || !WiringScannedFor.IsValid() || Entry->Id != WiringScannedFor)
							{
								return LOCTEXT("WiringNotScanned", "not scanned");
							}
							return WiringHits.Num() == 0
								? LOCTEXT("WiringNone", "no trigger in this level fires this entry")
								: FText::Format(LOCTEXT("WiringCount", "{0} trigger(s)"), FText::AsNumber(WiringHits.Num()));
						})
					]
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.MaxDesiredHeight(140.0f)
				[
					SAssignNew(WiringListView, SListView<TSharedPtr<FHapbeatWiringHit>>)
					.ListItemsSource(&WiringHits)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SHapbeatEventMapWindow::OnGenerateWiringRow)
				]
			]);
}

FReply SHapbeatEventMapWindow::OnAddEntryClicked()
{
	if (UHapbeatEventMap* Map = WeakEventMap.Get())
	{
		const FScopedTransaction Transaction(LOCTEXT("AddEntryTransaction", "Add Hapbeat Event Entry"));
		Map->Modify();

		// Ids are normally handed out by the asset's PostEditChangeProperty, which
		// does not run for a direct array write -- so mint one here, otherwise the
		// new entry would be invisible to a list keyed on valid ids.
		FHapbeatEventEntry NewEntry;
		NewEntry.Id = FGuid::NewGuid();
		Map->Entries.Add(NewEntry);
		Map->MarkPackageDirty();

		SelectedEntryId = MakeShared<FGuid>(NewEntry.Id);
		RefreshEntryList();
	}
	return FReply::Handled();
}

FReply SHapbeatEventMapWindow::OnImportWavClicked()
{
	UHapbeatEventMap* Map = WeakEventMap.Get();
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (Map == nullptr || DesktopPlatform == nullptr || FindSelectedEntry() == nullptr)
	{
		return FReply::Handled();
	}

	// Default to the bundled sample kits: that is where a first-time user's
	// WAVs actually are.
	FString DefaultPath;
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("HapbeatSDK")))
	{
		DefaultPath = FPaths::Combine(Plugin->GetContentDir(), TEXT("HapbeatSamples"));
	}

	TArray<FString> Filenames;
	const bool bPicked = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		LOCTEXT("ImportWavTitle", "Select a 16-bit PCM .wav").ToString(),
		DefaultPath, TEXT(""), TEXT("WAV audio (*.wav)|*.wav"),
		EFileDialogFlags::None, Filenames);
	if (!bPicked || Filenames.Num() == 0)
	{
		return FReply::Handled();
	}

	TArray<uint8> WavBytes;
	if (!FFileHelper::LoadFileToArray(WavBytes, *Filenames[0]))
	{
		RefreshSummary = FText::Format(LOCTEXT("WavReadFailed", "Could not read {0}"),
			FText::FromString(FPaths::GetCleanFilename(Filenames[0])));
		return FReply::Handled();
	}

	// Create the asset beside the Event Map, named after the WAV. A unique name
	// keeps importing the same file twice from silently replacing the first.
	const FString MapPackagePath = FPackageName::GetLongPackagePath(Map->GetOutermost()->GetName());
	const FString BaseName = FString::Printf(TEXT("HC_%s"),
		*FPaths::GetBaseFilename(Filenames[0]));

	FString PackageName;
	FString AssetName;
	FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetTools.Get().CreateUniqueAssetName(MapPackagePath / BaseName, TEXT(""), PackageName, AssetName);

	UPackage* Package = CreatePackage(*PackageName);
	if (Package == nullptr)
	{
		return FReply::Handled();
	}

	int32 SampleRate = 0;
	int32 Channels = 0;
	TArray<uint8> Pcm16;
	FString Error;
	if (!UHapbeatClip::ParseWav(WavBytes, SampleRate, Channels, Pcm16, Error))
	{
		RefreshSummary = FText::Format(LOCTEXT("WavParseFailed", "{0}: {1}"),
			FText::FromString(FPaths::GetCleanFilename(Filenames[0])), FText::FromString(Error));
		return FReply::Handled();
	}

	UHapbeatClip* Clip = NewObject<UHapbeatClip>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	Clip->SampleRate = SampleRate;
	Clip->NumChannels = Channels;
	Clip->Pcm16 = MoveTemp(Pcm16);
	Clip->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Clip);

	ModifySelectedEntry(LOCTEXT("SetStreamClipFromWav", "Import Hapbeat Stream Clip"),
		[Clip](FHapbeatEventEntry& Entry) { Entry.StreamClip = Clip; });

	RefreshSummary = FText::Format(
		LOCTEXT("WavImported", "Created {0} ({1} Hz, {2} ch). Save the new asset to keep it."),
		FText::FromString(AssetName), FText::AsNumber(SampleRate), FText::AsNumber(Channels));
	return FReply::Handled();
}

FReply SHapbeatEventMapWindow::OnRemoveEntryClicked()
{
	UHapbeatEventMap* Map = WeakEventMap.Get();
	if (Map != nullptr && SelectedEntryId.IsValid() && SelectedEntryId->IsValid())
	{
		const FGuid Doomed = *SelectedEntryId;
		const FScopedTransaction Transaction(LOCTEXT("RemoveEntryTransaction", "Remove Hapbeat Event Entry"));
		Map->Modify();
		Map->Entries.RemoveAll([&Doomed](const FHapbeatEventEntry& Candidate) { return Candidate.Id == Doomed; });
		Map->MarkPackageDirty();

		SelectedEntryId.Reset();
		RefreshEntryList();
	}
	return FReply::Handled();
}

// ---------------------------------------------------------------------------
// Detail pane
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SHapbeatEventMapWindow::MakeRow(const FText& Label, const FText& Tooltip, TSharedRef<SWidget> Value)
{
	return SNew(SHorizontalBox)
		.ToolTipText(Tooltip)
		+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(LabelColumnWidth)
				[
					SNew(STextBlock).Text(Label)
				]
			]
		+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				Value
			];
}

TSharedRef<SWidget> SHapbeatEventMapWindow::MakeSection(const FText& Title, TSharedRef<SWidget> Body)
{
	return SNew(SExpandableArea)
		.InitiallyCollapsed(false)
		.AreaTitle(Title)
		.Padding(FMargin(10.0f, 6.0f))
		.BodyContent()
		[
			Body
		];
}

EVisibility SHapbeatEventMapWindow::GetDetailVisibility() const
{
	return FindSelectedEntry() != nullptr ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SHapbeatEventMapWindow::GetStreamClipVisibility() const
{
	const FHapbeatEventEntry* Entry = FindSelectedEntry();
	return (Entry != nullptr && Entry->Mode == EHapticMode::StreamClip) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SHapbeatEventMapWindow::GetCommandVisibility() const
{
	const FHapbeatEventEntry* Entry = FindSelectedEntry();
	return (Entry != nullptr && Entry->Mode == EHapticMode::Command) ? EVisibility::Visible : EVisibility::Collapsed;
}

TSharedRef<SWidget> SHapbeatEventMapWindow::BuildDetailPane()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(10.0f, 10.0f, 10.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoSelection", "Select an entry on the left, or add one."))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Visibility_Lambda([this]
				{
					return FindSelectedEntry() != nullptr ? EVisibility::Collapsed : EVisibility::Visible;
				})
			]
		+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				.Visibility(this, &SHapbeatEventMapWindow::GetDetailVisibility)

				// Test first: while tuning an entry the same button is pressed
				// after every change, so it must not move down the pane as
				// sections above it expand.
				+ SScrollBox::Slot()[BuildTestSection()]
				+ SScrollBox::Slot()[BuildIdentitySection()]
				+ SScrollBox::Slot()[BuildEventSection()]
				+ SScrollBox::Slot()[BuildPlaybackSection()]
				+ SScrollBox::Slot()[BuildTargetingSection()]
				+ SScrollBox::Slot()[BuildWiringSection()]
				+ SScrollBox::Slot()[BuildNotesSection()]
			];
}

TSharedRef<SWidget> SHapbeatEventMapWindow::BuildIdentitySection()
{
	TSharedRef<SWidget> NameBox = SNew(SEditableTextBox)
		.Text_Lambda([this]
		{
			const FHapbeatEventEntry* Entry = FindSelectedEntry();
			return Entry != nullptr ? FText::FromString(Entry->DisplayName) : FText::GetEmpty();
		})
		.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
		{
			ModifySelectedEntry(LOCTEXT("SetDisplayName", "Set Hapbeat Display Name"),
				[&NewText](FHapbeatEventEntry& Entry) { Entry.DisplayName = NewText.ToString(); });
			if (EntryListView.IsValid())
			{
				EntryListView->RequestListRefresh();
			}
		});

	TSharedRef<SWidget> ModeBox = SNew(SComboBox<TSharedPtr<EHapticMode>>)
		.OptionsSource(&ModeOptions)
		.OnGenerateWidget_Lambda([](TSharedPtr<EHapticMode> InMode)
		{
			return SNew(STextBlock).Text(DescribeMode(InMode.IsValid() ? *InMode : EHapticMode::Command));
		})
		.OnSelectionChanged_Lambda([this](TSharedPtr<EHapticMode> NewMode, ESelectInfo::Type SelectInfo)
		{
			// Direct = the programmatic sync below, not a user pick; acting on it
			// would write the entry back onto itself and dirty the package.
			if (SelectInfo == ESelectInfo::Direct || !NewMode.IsValid())
			{
				return;
			}
			const EHapticMode Value = *NewMode;
			ModifySelectedEntry(LOCTEXT("SetMode", "Set Hapbeat Mode"),
				[Value](FHapbeatEventEntry& Entry) { Entry.Mode = Value; });
			if (EntryListView.IsValid())
			{
				EntryListView->RequestListRefresh();
			}
		})
		[
			SNew(STextBlock)
			.Text_Lambda([this]
			{
				const FHapbeatEventEntry* Entry = FindSelectedEntry();
				return Entry != nullptr ? DescribeMode(Entry->Mode) : FText::GetEmpty();
			})
		];

	return MakeSection(LOCTEXT("SectionIdentity", "Identity"),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				MakeRow(LOCTEXT("DisplayName", "Display Name"),
					LOCTEXT("DisplayNameTooltip", "Human-readable label for this event (e.g. \"Landing Impact\")."),
					NameBox)
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				MakeRow(LOCTEXT("Mode", "Mode"),
					LOCTEXT("ModeTooltip",
						"Command: send the event id; the device plays its locally installed clip.\n"
						"Stream Clip: stream a Hapbeat Clip over UDP (no Kit needed on the device)."),
					ModeBox)
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				MakeRow(LOCTEXT("EntryId", "Id"),
					LOCTEXT("EntryIdTooltip", "Stable GUID triggers reference. Assigned automatically; survives reordering."),
					SNew(STextBlock)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([this]
					{
						const FHapbeatEventEntry* Entry = FindSelectedEntry();
						return Entry != nullptr ? FText::FromString(Entry->Id.ToString(EGuidFormats::DigitsWithHyphens)) : FText::GetEmpty();
					}))
			]);
}

TSharedRef<SWidget> SHapbeatEventMapWindow::BuildEventSection()
{
	auto MakeStringRow = [this](const FText& Label, const FText& Tooltip, FString FHapbeatEventEntry::* Field, const FText& TransactionLabel)
	{
		return MakeRow(Label, Tooltip,
			SNew(SEditableTextBox)
			.Text_Lambda([this, Field]
			{
				const FHapbeatEventEntry* Entry = FindSelectedEntry();
				return Entry != nullptr ? FText::FromString(Entry->*Field) : FText::GetEmpty();
			})
			.OnTextCommitted_Lambda([this, Field, TransactionLabel](const FText& NewText, ETextCommit::Type)
			{
				ModifySelectedEntry(TransactionLabel,
					[&NewText, Field](FHapbeatEventEntry& Entry) { Entry.*Field = NewText.ToString(); });
				if (EntryListView.IsValid())
				{
					EntryListView->RequestListRefresh();
				}
			}));
	};

	return MakeSection(LOCTEXT("SectionEvent", "Event"),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				MakeStringRow(LOCTEXT("Category", "Category"),
					LOCTEXT("CategoryTooltip", "Event id left segment (the Kit name)."),
					&FHapbeatEventEntry::Category,
					LOCTEXT("SetCategory", "Set Hapbeat Category"))
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				MakeStringRow(LOCTEXT("EventName", "Event Name"),
					LOCTEXT("EventNameTooltip", "Event id right segment (the clip file name without extension)."),
					&FHapbeatEventEntry::EventName,
					LOCTEXT("SetEventName", "Set Hapbeat Event Name"))
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				MakeRow(LOCTEXT("EventIdRow", "Event Id"),
					LOCTEXT("EventIdTooltip", "Computed as <Category>.<Event Name>. This is what goes on the wire in Command mode."),
					SNew(STextBlock)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([this]
					{
						const FHapbeatEventEntry* Entry = FindSelectedEntry();
						if (Entry == nullptr)
						{
							return FText::GetEmpty();
						}
						const FString EventId = Entry->GetEventId();
						return EventId.IsEmpty() ? LOCTEXT("EventIdEmpty", "(none -- set Event Name)") : FText::FromString(EventId);
					}))
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SBox)
				.Visibility(this, &SHapbeatEventMapWindow::GetStreamClipVisibility)
				[
					MakeRow(LOCTEXT("StreamClipRow", "Stream Clip"),
						LOCTEXT("StreamClipTooltip",
							"Audio streamed to the device as PCM16 (Stream Clip mode only). Import a .wav with the button "
							"on the right, or pick a Hapbeat Clip asset you already have."),
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(SObjectPropertyEntryBox)
								.AllowedClass(UHapbeatClip::StaticClass())
								.AllowClear(true)
								.DisplayUseSelected(true)
								.DisplayBrowse(true)
								.ObjectPath_Lambda([this]
								{
									const FHapbeatEventEntry* Entry = FindSelectedEntry();
									return Entry != nullptr ? Entry->StreamClip.ToString() : FString();
								})
								.OnObjectChanged_Lambda([this](const FAssetData& AssetData)
								{
									ModifySelectedEntry(LOCTEXT("SetStreamClip", "Set Hapbeat Stream Clip"),
										[&AssetData](FHapbeatEventEntry& Entry)
										{
											Entry.StreamClip = Cast<UHapbeatClip>(AssetData.GetAsset());
										});
								})
							]
						+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SButton)
								.Text(LOCTEXT("ImportWav", "Import WAV..."))
								.ToolTipText(LOCTEXT("ImportWavTooltip",
									"Pick a 16-bit PCM .wav; a Hapbeat Clip asset is created next to this Event Map and assigned here."))
								.OnClicked(this, &SHapbeatEventMapWindow::OnImportWavClicked)
							]
					)
				]
			]);
}

TSharedRef<SWidget> SHapbeatEventMapWindow::BuildPlaybackSection()
{
	return MakeSection(LOCTEXT("SectionPlayback", "Playback"),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				MakeRow(LOCTEXT("Gain", "Gain"),
					LOCTEXT("GainTooltip", "Gain multiplier (0.0 - 2.0). Multiplied by the baked manifest intensity before it goes on the wire."),
					SNew(SSpinBox<float>)
					.MinValue(0.0f).MaxValue(2.0f)
					.MinSliderValue(0.0f).MaxSliderValue(2.0f)
					.Value_Lambda([this]
					{
						const FHapbeatEventEntry* Entry = FindSelectedEntry();
						return Entry != nullptr ? Entry->Gain : 1.0f;
					})
					.OnValueChanged_Lambda([this](float NewValue)
					{
						ModifySelectedEntry(LOCTEXT("SetGain", "Set Hapbeat Gain"),
							[NewValue](FHapbeatEventEntry& Entry) { Entry.Gain = NewValue; });
					}))
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				MakeRow(LOCTEXT("EffectiveGain", "Effective Gain"),
					LOCTEXT("EffectiveGainTooltip",
						"Gain x baked manifest intensity -- the value actually sent. "
						"Shows plain Gain while the intensity is unresolved (-1); run Refresh Intensities."),
					SNew(STextBlock)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([this]
					{
						const FHapbeatEventEntry* Entry = FindSelectedEntry();
						if (Entry == nullptr)
						{
							return FText::GetEmpty();
						}
						if (Entry->CachedManifestIntensity < 0.0f)
						{
							return FText::Format(LOCTEXT("EffectiveGainUnresolved", "{0} (manifest intensity unresolved)"),
								FText::AsNumber(Entry->GetEffectiveGain()));
						}
						return FText::Format(LOCTEXT("EffectiveGainResolved", "{0}  =  {1} x {2}"),
							FText::AsNumber(Entry->GetEffectiveGain()),
							FText::AsNumber(Entry->Gain),
							FText::AsNumber(Entry->CachedManifestIntensity));
					}))
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				SNew(SBox)
				.Visibility(this, &SHapbeatEventMapWindow::GetStreamClipVisibility)
				[
					MakeRow(LOCTEXT("Loop", "Loop"),
						LOCTEXT("LoopTooltip", "Re-stream the clip continuously until Stop() is called. For sustained effects (drag, scrape, charge)."),
						SNew(SCheckBox)
						.IsChecked_Lambda([this]
						{
							const FHapbeatEventEntry* Entry = FindSelectedEntry();
							return (Entry != nullptr && Entry->bLoop) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
						{
							const bool bNewLoop = (NewState == ECheckBoxState::Checked);
							ModifySelectedEntry(LOCTEXT("SetLoop", "Set Hapbeat Loop"),
								[bNewLoop](FHapbeatEventEntry& Entry) { Entry.bLoop = bNewLoop; });
						}))
				]
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				MakeRow(LOCTEXT("DelayOffset", "Delay Offset"),
					LOCTEXT("DelayOffsetTooltip",
						"Seconds added to the global Haptic Delay for this entry only. "
						"Positive fires later, negative earlier; the sum is clamped to >= 0."),
					SNew(SSpinBox<float>)
					.MinValue(-0.2f).MaxValue(0.2f)
					.MinSliderValue(-0.2f).MaxSliderValue(0.2f)
					.Value_Lambda([this]
					{
						const FHapbeatEventEntry* Entry = FindSelectedEntry();
						return Entry != nullptr ? Entry->DelayOffsetSeconds : 0.0f;
					})
					.OnValueChanged_Lambda([this](float NewValue)
					{
						ModifySelectedEntry(LOCTEXT("SetDelayOffset", "Set Hapbeat Delay Offset"),
							[NewValue](FHapbeatEventEntry& Entry) { Entry.DelayOffsetSeconds = NewValue; });
					}))
			]);
}

// ---------------------------------------------------------------------------
// Targeting
// ---------------------------------------------------------------------------

int32 SHapbeatEventMapWindow::GetTargetPlayer() const
{
	const FHapbeatEventEntry* Entry = FindSelectedEntry();
	if (Entry == nullptr)
	{
		return -1;
	}
	int32 Player = -1;
	int32 Group = -1;
	FString Position;
	UHapbeatTargetLibrary::ParseTarget(Entry->Target, Player, Position, Group);
	return Player;
}

int32 SHapbeatEventMapWindow::GetTargetGroup() const
{
	const FHapbeatEventEntry* Entry = FindSelectedEntry();
	if (Entry == nullptr)
	{
		return -1;
	}
	int32 Player = -1;
	int32 Group = -1;
	FString Position;
	UHapbeatTargetLibrary::ParseTarget(Entry->Target, Player, Position, Group);
	return Group;
}

FString SHapbeatEventMapWindow::GetTargetPosition() const
{
	const FHapbeatEventEntry* Entry = FindSelectedEntry();
	if (Entry == nullptr)
	{
		return FString();
	}
	int32 Player = -1;
	int32 Group = -1;
	FString Position;
	UHapbeatTargetLibrary::ParseTarget(Entry->Target, Player, Position, Group);
	return Position;
}

void SHapbeatEventMapWindow::SetTargetParts(int32 Player, const FString& Position, int32 Group)
{
	const FString NewTarget = UHapbeatTargetLibrary::BuildTarget(Player, Position, Group);
	ModifySelectedEntry(LOCTEXT("SetTarget", "Set Hapbeat Target"),
		[&NewTarget](FHapbeatEventEntry& Entry) { Entry.Target = NewTarget; });
}

TSharedRef<SWidget> SHapbeatEventMapWindow::BuildTargetingSection()
{
	return MakeSection(LOCTEXT("SectionTargeting", "Targeting"),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				MakeRow(LOCTEXT("Player", "Player"),
					LOCTEXT("PlayerTooltip", "Player number to address. -1 = any player."),
					SNew(SSpinBox<int32>)
					.MinValue(-1).MaxValue(99)
					.MinSliderValue(-1).MaxSliderValue(99)
					.Value_Lambda([this] { return GetTargetPlayer(); })
					.OnValueChanged_Lambda([this](int32 NewValue)
					{
						SetTargetParts(NewValue, GetTargetPosition(), GetTargetGroup());
					}))
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				MakeRow(LOCTEXT("Position", "Position"),
					LOCTEXT("PositionTooltip", "Body position from the device-addressing vocabulary. Empty = any position."),
					SNew(SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&PositionOptions)
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> InPosition)
					{
						const bool bEmpty = !InPosition.IsValid() || InPosition->IsEmpty();
						return SNew(STextBlock).Text(bEmpty
							? LOCTEXT("AnyPosition", "(any position)")
							: FText::FromString(*InPosition));
					})
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewPosition, ESelectInfo::Type SelectInfo)
					{
						if (SelectInfo == ESelectInfo::Direct || !NewPosition.IsValid())
						{
							return;
						}
						SetTargetParts(GetTargetPlayer(), *NewPosition, GetTargetGroup());
					})
					[
						SNew(STextBlock)
						.Text_Lambda([this]
						{
							const FString Position = GetTargetPosition();
							return Position.IsEmpty()
								? LOCTEXT("AnyPosition", "(any position)")
								: FText::FromString(Position);
						})
					])
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				MakeRow(LOCTEXT("Group", "Group"),
					LOCTEXT("GroupTooltip", "Group number to address. -1 = any group."),
					SNew(SSpinBox<int32>)
					.MinValue(-1).MaxValue(99)
					.MinSliderValue(-1).MaxSliderValue(99)
					.Value_Lambda([this] { return GetTargetGroup(); })
					.OnValueChanged_Lambda([this](int32 NewValue)
					{
						SetTargetParts(GetTargetPlayer(), GetTargetPosition(), NewValue);
					}))
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
			[
				MakeRow(LOCTEXT("TargetSpec", "Target"),
					LOCTEXT("TargetSpecTooltip",
						"The addressing string actually sent. Editable directly for shapes the three "
						"fields above cannot express. Empty = broadcast to every device."),
					SNew(SEditableTextBox)
					.HintText(LOCTEXT("TargetHint", "(broadcast -- all devices)"))
					.Text_Lambda([this]
					{
						const FHapbeatEventEntry* Entry = FindSelectedEntry();
						return Entry != nullptr ? FText::FromString(Entry->Target) : FText::GetEmpty();
					})
					.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
					{
						ModifySelectedEntry(LOCTEXT("SetTargetRaw", "Set Hapbeat Target"),
							[&NewText](FHapbeatEventEntry& Entry) { Entry.Target = NewText.ToString(); });
					}))
			]);
}

TSharedRef<SWidget> SHapbeatEventMapWindow::BuildNotesSection()
{
	return MakeSection(LOCTEXT("SectionNotes", "Notes"),
		SNew(SBox)
		.HeightOverride(72.0f)
		[
			SNew(SMultiLineEditableTextBox)
			.HintText(LOCTEXT("NotesHint", "Designer notes (never sent to devices)."))
			.Text_Lambda([this]
			{
				const FHapbeatEventEntry* Entry = FindSelectedEntry();
				return Entry != nullptr ? FText::FromString(Entry->Notes) : FText::GetEmpty();
			})
			.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
			{
				ModifySelectedEntry(LOCTEXT("SetNotes", "Set Hapbeat Notes"),
					[&NewText](FHapbeatEventEntry& Entry) { Entry.Notes = NewText.ToString(); });
			})
		]);
}

TSharedRef<SWidget> SHapbeatEventMapWindow::BuildTestSection()
{
	return MakeSection(LOCTEXT("SectionTest", "Test"),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("TestPlay", "Test Play"))
						.ButtonColorAndOpacity(TestPlayColor)
						.IsEnabled_Lambda([this] { return FindSelectedEntry() != nullptr; })
						.ToolTipText_Lambda([this]
						{
							const FHapbeatEventEntry* Entry = FindSelectedEntry();
							if (Entry == nullptr)
							{
								return LOCTEXT("TestPlayNoSelection", "Select an entry to test.");
							}
							if (Entry->Mode == EHapticMode::StreamClip)
							{
								return LOCTEXT("TestPlayStreamHint",
									"Stream this entry's clip to the device from here -- no need to enter Play.");
							}
							return LOCTEXT("TestPlayHint", "Send a PLAY command for this entry at its effective gain.");
						})
						.OnClicked_Lambda([this]
						{
							const FHapbeatEventEntry* Entry = FindSelectedEntry();
							if (Entry == nullptr)
							{
								return FReply::Handled();
							}
							if (Entry->Mode == EHapticMode::StreamClip)
							{
								// LoadSynchronous: the reference is soft, and the
								// designer clicking Test expects the clip now.
								FHapbeatEditorSender::StartStream(
									Entry->StreamClip.LoadSynchronous(),
									Entry->GetEffectiveGain(), Entry->Target, Entry->bLoop);
							}
							else
							{
								FHapbeatEditorSender::SendPlay(Entry->GetEventId(), Entry->GetEffectiveGain(), Entry->Target);
							}
							return FReply::Handled();
						})
					]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("Stop", "Stop"))
						.ButtonColorAndOpacity(NeutralColor)
						.ToolTipText(LOCTEXT("StopTooltip", "Stop this entry: STOP for a Command entry, STREAM_END for a stream."))
						.OnClicked_Lambda([this]
						{
							const FHapbeatEventEntry* Entry = FindSelectedEntry();
							if (Entry == nullptr)
							{
								return FReply::Handled();
							}
							if (Entry->Mode == EHapticMode::StreamClip)
							{
								FHapbeatEditorSender::StopStream();
							}
							else
							{
								FHapbeatEditorSender::SendStop(Entry->GetEventId(), Entry->Target);
							}
							return FReply::Handled();
						})
					]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("StopAll", "Stop All"))
						.ButtonColorAndOpacity(StopAllColor)
						.ToolTipText(LOCTEXT("StopAllTooltip", "Send STOP_ALL to every device."))
						.OnClicked_Lambda([]
						{
							FHapbeatEditorSender::SendStopAll();
							return FReply::Handled();
						})
					]
				+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("Ping", "Ping"))
						.ButtonColorAndOpacity(NeutralColor)
						.ToolTipText(LOCTEXT("PingTooltip", "Send a PING (connectivity check; replies are not read back here)."))
						.OnClicked_Lambda([]
						{
							FHapbeatEditorSender::SendPing();
							return FReply::Handled();
						})
					]
			]);
}

#undef LOCTEXT_NAMESPACE
