// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatSampleLibrary.h"

#include "HapbeatClip.h"
#include "HapbeatEventMap.h"
#include "HapbeatSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatSample, Log, All);

FString FHapbeatSampleLibrary::GetSamplesContentDir()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("HapbeatSDK"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogHapbeatSample, Warning,
			TEXT("HapbeatSDK plugin not found via IPluginManager; sample content cannot be located."));
		return FString();
	}
	return Plugin->GetContentDir() / TEXT("HapbeatSamples");
}

UHapbeatClip* FHapbeatSampleLibrary::LoadSampleClip(UObject* Outer, const FString& RelPathUnderHapbeatSamples)
{
	const FString Dir = GetSamplesContentDir();
	if (Dir.IsEmpty())
	{
		return nullptr;
	}

	const FString FullPath = Dir / RelPathUnderHapbeatSamples;
	TArray<uint8> WavBytes;
	if (!FFileHelper::LoadFileToArray(WavBytes, *FullPath))
	{
		UE_LOG(LogHapbeatSample, Warning, TEXT("LoadSampleClip: could not read '%s' (resolved to '%s')."),
			*RelPathUnderHapbeatSamples, *FullPath);
		return nullptr;
	}

	UHapbeatClip* Clip = UHapbeatClip::CreateFromWavBytes(Outer, WavBytes);
	if (Clip == nullptr)
	{
		// UHapbeatClip::CreateFromWavBytes already logged the parse error under
		// LogHapbeatClip; add the relative path here since that log doesn't know
		// which sample file it came from.
		UE_LOG(LogHapbeatSample, Warning, TEXT("LoadSampleClip: '%s' failed to parse as 16-bit PCM WAV."),
			*RelPathUnderHapbeatSamples);
	}
	return Clip;
}

FHapbeatEventEntry FHapbeatSampleLibrary::MakeEntry(EHapticMode Mode, const FString& Category, const FString& EventName,
	float Gain, bool bLoop, float CachedIntensity, UHapbeatClip* Clip, const FString& DisplayName)
{
	FHapbeatEventEntry Entry;
	Entry.Id = FGuid::NewGuid();
	Entry.Mode = Mode;
	Entry.Category = Category;
	Entry.EventName = EventName;
	Entry.Gain = Gain;
	Entry.bLoop = bLoop;
	Entry.CachedManifestIntensity = CachedIntensity;
	if (Mode == EHapticMode::StreamClip)
	{
		Entry.StreamClip = Clip; // TSoftObjectPtr from a live UObject* caches a resolvable weak ptr immediately
	}
	Entry.DisplayName = DisplayName.IsEmpty() ? EventName : DisplayName;
	return Entry;
}

FGuid FHapbeatSampleLibrary::FindEntryId(const UHapbeatEventMap* Map, EHapticMode Mode,
	const FString& Category, const FString& EventName)
{
	// Match on the same <category>.<name> string the protocol itself uses, so an
	// asset authored with the category folded into EventName still resolves.
	const FString WantedEventId = Category.IsEmpty() ? EventName : Category + TEXT(".") + EventName;

	if (Map == nullptr)
	{
		UE_LOG(LogHapbeatSample, Warning,
			TEXT("FindEntryId('%s'): null EventMap; that event will not fire."), *WantedEventId);
		return FGuid();
	}

	for (const FHapbeatEventEntry& Entry : Map->Entries)
	{
		// Mode is part of the key, not just a sanity check: an event id can legally
		// exist twice in one map (a Command variant and a StreamClip variant), and
		// the caller wires a specific one.
		if (Entry.Mode == Mode && Entry.GetEventId() == WantedEventId)
		{
			return Entry.Id;
		}
	}

	UE_LOG(LogHapbeatSample, Warning,
		TEXT("FindEntryId: EventMap '%s' has no entry for '%s' in the requested mode; that event will not fire."),
		*GetNameSafe(Map), *WantedEventId);
	return FGuid();
}

void FHapbeatSampleLibrary::ShowHudLine(int32 LineKey, const FString& Text, FColor Color, float Duration)
{
	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(LineKey, Duration, Color, Text);
	}
}

void FHapbeatSampleLibrary::ShowDeviceStatusLine(const UObject* WorldContextObject, int32 LineKey, float Duration)
{
	if (WorldContextObject == nullptr)
	{
		return;
	}
	const UWorld* World = WorldContextObject->GetWorld();
	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	const UHapbeatSubsystem* Subsystem = GameInstance != nullptr
		? GameInstance->GetSubsystem<UHapbeatSubsystem>()
		: nullptr;
	if (Subsystem == nullptr)
	{
		return;
	}
	const int32 Alive = Subsystem->GetAliveDeviceCount();
	ShowHudLine(LineKey,
		FString::Printf(TEXT("Hapbeat devices reachable: %d"), Alive),
		Alive > 0 ? FColor::Green : FColor::Silver, Duration);
}
