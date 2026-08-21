// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEditorTools.h"

#include "HapbeatEventEntry.h"
#include "HapbeatEventMap.h"
#include "HapbeatTriggerComponent.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "IContentBrowserSingleton.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "HapbeatEditorTools"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatTools, Log, All);

void FHapbeatEditorTools::RegisterMenus(FToolMenuSection& Section)
{
	Section.AddMenuEntry("HapbeatVerboseLogOff",
		LOCTEXT("VerboseOff", "Turn Off Verbose Log on All Triggers"),
		LOCTEXT("VerboseOffTooltip",
			"Clear Verbose Log on every Hapbeat trigger in the open level. Verbose logging is per component and easy to leave on by accident."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([] { SetVerboseLogOnAllTriggers(false); })));

	Section.AddMenuEntry("HapbeatExportMarkdown",
		LOCTEXT("ExportMarkdown", "Export Event Map to Markdown"),
		LOCTEXT("ExportMarkdownTooltip",
			"Write a Markdown table of the selected Event Map's entries next to the asset, for design docs and reviews."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]
		{
			ExportEventMapToMarkdown(ResolveTargetEventMap());
		})));
}

// ---------------------------------------------------------------------------
// Verbose log toggle
// ---------------------------------------------------------------------------

void FHapbeatEditorTools::SetVerboseLogOnAllTriggers(bool bEnabled)
{
	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetVerboseTransaction", "Set Hapbeat Verbose Log"));
	int32 Changed = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TArray<UHapbeatTriggerComponent*> Triggers;
		It->GetComponents(Triggers);
		for (UHapbeatTriggerComponent* Trigger : Triggers)
		{
			if (Trigger->bVerboseLog != bEnabled)
			{
				Trigger->Modify();
				Trigger->bVerboseLog = bEnabled;
				Trigger->MarkPackageDirty();
				++Changed;
			}
		}
	}

	UE_LOG(LogHapbeatTools, Log, TEXT("[Hapbeat] Verbose log %s on %d trigger(s)."),
		bEnabled ? TEXT("enabled") : TEXT("disabled"), Changed);
}

// ---------------------------------------------------------------------------
// Markdown export
// ---------------------------------------------------------------------------

UHapbeatEventMap* FHapbeatEditorTools::ResolveTargetEventMap()
{
	// Prefer whatever the user has selected -- that is the thing they mean.
	TArray<FAssetData> Selected;
	FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ContentBrowser.Get().GetSelectedAssets(Selected);
	for (const FAssetData& Asset : Selected)
	{
		if (UHapbeatEventMap* Map = Cast<UHapbeatEventMap>(Asset.GetAsset()))
		{
			return Map;
		}
	}

	// Nothing selected: fall back only when the project has exactly one, so the
	// command can never silently act on an arbitrary asset.
	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> All;
	AssetRegistry.Get().GetAssetsByClass(UHapbeatEventMap::StaticClass()->GetClassPathName(), All);
	if (All.Num() == 1)
	{
		return Cast<UHapbeatEventMap>(All[0].GetAsset());
	}

	FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoSelection",
		"Select a Hapbeat Event Map in the Content Browser first."));
	return nullptr;
}

void FHapbeatEditorTools::ExportEventMapToMarkdown(UHapbeatEventMap* Map)
{
	if (Map == nullptr)
	{
		return;
	}

	FString Out;
	Out += FString::Printf(TEXT("# %s\n\n"), *Map->GetName());
	Out += TEXT("| # | Name | Event Id | Mode | Gain | Intensity | Effective | Pan | Target | Loop | Notes |\n");
	Out += TEXT("|---|---|---|---|---|---|---|---|---|---|---|\n");

	int32 Index = 0;
	for (const FHapbeatEventEntry& Entry : Map->Entries)
	{
		const FString Intensity = Entry.CachedManifestIntensity < 0.0f
			? TEXT("unresolved")
			: FString::SanitizeFloat(Entry.CachedManifestIntensity);

		// Pipes inside a cell would break the table.
		FString Notes = Entry.Notes.Replace(TEXT("|"), TEXT("\\|"));
		Notes.ReplaceInline(TEXT("\n"), TEXT(" "));

		Out += FString::Printf(TEXT("| %d | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n"),
			Index++,
			*Entry.DisplayName,
			*Entry.GetEventId(),
			Entry.Mode == EHapticMode::StreamClip ? TEXT("StreamClip") : TEXT("Command"),
			*FString::SanitizeFloat(Entry.Gain),
			*Intensity,
			*FString::SanitizeFloat(Entry.GetEffectiveGain()),
			*FString::SanitizeFloat(Entry.Pan),
			Entry.Target.IsEmpty() ? TEXT("(broadcast)") : *Entry.Target,
			Entry.bLoop ? TEXT("yes") : TEXT(""),
			*Notes);
	}

	// Next to the asset's package, so the file lands where the asset lives.
	const FString PackagePath = FPackageName::LongPackageNameToFilename(Map->GetOutermost()->GetName());
	const FString OutPath = PackagePath + TEXT(".md");

	if (FFileHelper::SaveStringToFile(Out, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogHapbeatTools, Log, TEXT("[Hapbeat] Exported %d entries to %s"), Map->Entries.Num(), *OutPath);
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("ExportDone", "Exported {0} entries to:\n{1}"),
			FText::AsNumber(Map->Entries.Num()), FText::FromString(OutPath)));
	}
	else
	{
		UE_LOG(LogHapbeatTools, Warning, TEXT("[Hapbeat] Could not write %s"), *OutPath);
	}
}

#undef LOCTEXT_NAMESPACE
