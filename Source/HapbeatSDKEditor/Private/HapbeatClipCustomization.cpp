// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatClipCustomization.h"

#include "HapbeatClip.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FHapbeatClipCustomization"

TSharedRef<IDetailCustomization> FHapbeatClipCustomization::MakeInstance()
{
	return MakeShared<FHapbeatClipCustomization>();
}

void FHapbeatClipCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1)
	{
		return; // multi-select: leave the default rows alone
	}
	TargetClip = Cast<UHapbeatClip>(Objects[0].Get());
	if (!TargetClip.IsValid())
	{
		return;
	}

	IDetailCategoryBuilder& Category =
		DetailBuilder.EditCategory("Hapbeat", FText::GetEmpty(), ECategoryPriority::Important);

	Category.AddCustomRow(LOCTEXT("ImportWavFilter", "Import WAV"))
		.WholeRowContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 2.0f, 6.0f, 2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ImportWav", "Import WAV..."))
				.ToolTipText(LOCTEXT("ImportWavTooltip",
					"Fill this clip from a .wav file on disk.\n"
					"Requirements: PCM, 16-bit. The Hapbeat kit format is 16 kHz;\n"
					"other sample rates are accepted but are streamed at their own rate."))
				.OnClicked(this, &FHapbeatClipCustomization::OnImportWavClicked)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &FHapbeatClipCustomization::GetStatusText)
				.AutoWrapText(true)
			]
		];
}

FText FHapbeatClipCustomization::GetStatusText() const
{
	if (!StatusText.IsEmpty())
	{
		return StatusText;
	}
	if (const UHapbeatClip* Clip = TargetClip.Get())
	{
		if (Clip->Pcm16.Num() > 0 && Clip->SampleRate > 0 && Clip->NumChannels > 0)
		{
			const double Seconds =
				static_cast<double>(Clip->Pcm16.Num()) / (2.0 * Clip->NumChannels * Clip->SampleRate);
			return FText::FromString(FString::Printf(TEXT("%d Hz, %d ch, %.2f s"),
				Clip->SampleRate, Clip->NumChannels, Seconds));
		}
	}
	return LOCTEXT("EmptyClip", "Empty - import a .wav to use this clip.");
}

FReply FHapbeatClipCustomization::OnImportWavClicked()
{
	UHapbeatClip* Clip = TargetClip.Get();
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (Clip == nullptr || DesktopPlatform == nullptr)
	{
		return FReply::Handled();
	}

	// Default the dialog to the bundled sample kits — that's where the WAVs a
	// first-time user wants to import actually live.
	FString DefaultPath;
	if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("HapbeatSDK")))
	{
		DefaultPath = FPaths::Combine(Plugin->GetContentDir(), TEXT("HapbeatSamples"));
	}

	const void* ParentHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	TArray<FString> Filenames;
	const bool bPicked = DesktopPlatform->OpenFileDialog(
		ParentHandle,
		LOCTEXT("ImportWavTitle", "Select a 16-bit PCM .wav").ToString(),
		DefaultPath,
		TEXT(""),
		TEXT("WAV audio (*.wav)|*.wav"),
		EFileDialogFlags::None,
		Filenames);

	if (!bPicked || Filenames.Num() == 0)
	{
		return FReply::Handled();
	}

	TArray<uint8> WavBytes;
	if (!FFileHelper::LoadFileToArray(WavBytes, *Filenames[0]))
	{
		StatusText = FText::Format(LOCTEXT("ReadFailed", "Could not read {0}"),
			FText::FromString(FPaths::GetCleanFilename(Filenames[0])));
		return FReply::Handled();
	}

	int32 NewSampleRate = 0;
	int32 NewChannels = 0;
	TArray<uint8> NewPcm16;
	FString Error;
	if (!UHapbeatClip::ParseWav(WavBytes, NewSampleRate, NewChannels, NewPcm16, Error))
	{
		StatusText = FText::Format(LOCTEXT("ParseFailed", "Not a usable WAV: {0}"), FText::FromString(Error));
		return FReply::Handled();
	}

	{
		const FScopedTransaction Transaction(LOCTEXT("ImportWavTransaction", "Import Hapbeat Clip WAV"));
		Clip->Modify();
		Clip->SampleRate = NewSampleRate;
		Clip->NumChannels = NewChannels;
		Clip->Pcm16 = MoveTemp(NewPcm16);
	}
	Clip->MarkPackageDirty();

	const double Seconds =
		static_cast<double>(Clip->Pcm16.Num()) / (2.0 * FMath::Max(1, Clip->NumChannels) * FMath::Max(1, Clip->SampleRate));
	StatusText = FText::Format(
		LOCTEXT("ImportOk", "Imported {0} - {1} Hz, {2} ch, {3} s (save the asset to keep it)"),
		FText::FromString(FPaths::GetCleanFilename(Filenames[0])),
		FText::AsNumber(Clip->SampleRate),
		FText::AsNumber(Clip->NumChannels),
		FText::AsNumber(Seconds));

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
