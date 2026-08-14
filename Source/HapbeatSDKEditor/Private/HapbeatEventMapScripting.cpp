// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatEventMapScripting.h"

#include "HapbeatClip.h"
#include "HapbeatEventMap.h"

#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatScripting, Log, All);

bool UHapbeatEventMapScripting::ImportWavIntoClip(UHapbeatClip* Clip, const FString& WavFilePath)
{
	if (Clip == nullptr)
	{
		return false;
	}

	TArray<uint8> WavBytes;
	if (!FFileHelper::LoadFileToArray(WavBytes, *WavFilePath))
	{
		UE_LOG(LogHapbeatScripting, Warning, TEXT("[Hapbeat] Could not read %s"), *WavFilePath);
		return false;
	}

	int32 SampleRate = 0;
	int32 Channels = 0;
	TArray<uint8> Pcm16;
	FString Error;
	if (!UHapbeatClip::ParseWav(WavBytes, SampleRate, Channels, Pcm16, Error))
	{
		UE_LOG(LogHapbeatScripting, Warning, TEXT("[Hapbeat] %s: %s"), *WavFilePath, *Error);
		return false;
	}

	Clip->Modify();
	Clip->SampleRate = SampleRate;
	Clip->NumChannels = Channels;
	Clip->Pcm16 = MoveTemp(Pcm16);
	Clip->MarkPackageDirty();
	return true;
}

void UHapbeatEventMapScripting::ClearEntries(UHapbeatEventMap* Map)
{
	if (Map == nullptr)
	{
		return;
	}
	Map->Modify();
	Map->Entries.Reset();
	Map->MarkPackageDirty();
}

FGuid UHapbeatEventMapScripting::AddEntry(UHapbeatEventMap* Map, EHapticMode Mode, const FString& Category,
	const FString& EventName, float Gain, bool bLoop, float Intensity,
	UHapbeatClip* StreamClip, const FString& DisplayName)
{
	if (Map == nullptr)
	{
		return FGuid();
	}

	FHapbeatEventEntry Entry;
	// Minted here because the asset only assigns Ids from PostEditChangeProperty,
	// which a scripted write never goes through.
	Entry.Id = FGuid::NewGuid();
	Entry.Mode = Mode;
	Entry.Category = Category;
	Entry.EventName = EventName;
	Entry.Gain = Gain;
	Entry.bLoop = bLoop;
	Entry.CachedManifestIntensity = Intensity;
	Entry.StreamClip = StreamClip;
	Entry.DisplayName = DisplayName;

	Map->Modify();
	Map->Entries.Add(Entry);
	Map->MarkPackageDirty();
	return Entry.Id;
}
