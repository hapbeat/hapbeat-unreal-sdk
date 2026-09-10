// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEditorTools.h"

#include "HapbeatEventEntry.h"
#include "HapbeatEventMap.h"
#include "HapbeatTriggerComponent.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HapbeatEditorTools"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatTools, Log, All);

namespace
{
	/** State shared by the modal picker and its two button callbacks. */
	struct FEventMapExportPickerState
	{
		TWeakObjectPtr<UHapbeatEventMap> SelectedMap;
		TWeakPtr<SWindow> Window;
	};
}

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
			"Choose an Event Map, then write a Markdown table of its entries next to the asset for design docs and reviews."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]
		{
			PromptExportEventMapToMarkdown();
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

void FHapbeatEditorTools::PromptExportEventMapToMarkdown()
{
	const TSharedRef<FEventMapExportPickerState> State = MakeShared<FEventMapExportPickerState>();
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("ChooseEventMapTitle", "Export Hapbeat Event Map"))
		.ClientSize(FVector2D(560.0f, 150.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(16.0f, 16.0f, 16.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ChooseEventMapPrompt", "Event Map to export"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(16.0f, 0.0f, 16.0f, 12.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UHapbeatEventMap::StaticClass())
				.ObjectPath_Lambda([State]()
				{
					return State->SelectedMap.IsValid() ? State->SelectedMap->GetPathName() : FString();
				})
				.AllowClear(true)
				.DisplayBrowse(true)
				.DisplayUseSelected(false)
				.OnObjectChanged_Lambda([State](const FAssetData& Asset)
				{
					State->SelectedMap = Cast<UHapbeatEventMap>(Asset.GetAsset());
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(16.0f, 0.0f, 16.0f, 16.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("CancelExport", "Cancel"))
					.OnClicked_Lambda([State]()
					{
						if (const TSharedPtr<SWindow> PickerWindow = State->Window.Pin())
						{
							PickerWindow->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("ExportSelectedMap", "Export Markdown"))
					.OnClicked_Lambda([State]()
					{
						UHapbeatEventMap* Map = State->SelectedMap.Get();
						if (Map == nullptr)
						{
							FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoMapChosen",
								"Choose a Hapbeat Event Map to export."));
							return FReply::Handled();
						}

						FHapbeatEditorTools::ExportEventMapToMarkdown(Map);
						if (const TSharedPtr<SWindow> PickerWindow = State->Window.Pin())
						{
							PickerWindow->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
			]
		];

	State->Window = Window;
	FSlateApplication::Get().AddWindow(Window);
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
		FNotificationInfo Info(FText::Format(
			LOCTEXT("ExportDone", "Exported {0} entries to {1}"),
			FText::AsNumber(Map->Entries.Num()), FText::FromString(OutPath)));
		Info.ExpireDuration = 5.0f;
		Info.bUseSuccessFailIcons = true;
		if (const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Notification->SetCompletionState(SNotificationItem::CS_Success);
		}
	}
	else
	{
		UE_LOG(LogHapbeatTools, Warning, TEXT("[Hapbeat] Could not write %s"), *OutPath);
	}
}

#undef LOCTEXT_NAMESPACE
