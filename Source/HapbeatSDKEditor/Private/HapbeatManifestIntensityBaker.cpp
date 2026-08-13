// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatManifestIntensityBaker.h"

#include "HapbeatEventMap.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatEditor, Log, All);

TMap<FHapbeatManifestEventKey, float> FHapbeatManifestIntensityBaker::ScanProjectManifests(int32* OutNumManifestsScanned)
{
	TMap<FHapbeatManifestEventKey, float> Result;

	// Where to look. The project's own Content is the obvious place, but a Kit
	// can equally ship inside a plugin -- this SDK's own sample Kits do, under
	// HapbeatSDK/Content/HapbeatSamples/. Scanning only the project directory
	// made Refresh Intensities report "0 manifests" on a stock install, with
	// every bundled sample sitting right there but out of reach.
	TArray<FString> SearchRoots;
	SearchRoots.Add(FPaths::ProjectContentDir());
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
	{
		SearchRoots.AddUnique(Plugin->GetContentDir());
	}

	// Scan *.json and filter by "manifest" in the filename — the same leniency as
	// Unity (HapbeatEventMapAutoIntensityRefresher.IsManifestPath), so a bare
	// "manifest.json" (kit-format.md §3 diagram) is found, not only
	// "<kit>-manifest.json".
	TArray<FString> ManifestFiles;
	for (const FString& Root : SearchRoots)
	{
		TArray<FString> JsonFiles;
		IFileManager::Get().FindFilesRecursive(JsonFiles, *Root, TEXT("*.json"),
			/*Files=*/true, /*Directories=*/false);
		for (const FString& JsonPath : JsonFiles)
		{
			if (FPaths::GetCleanFilename(JsonPath).Contains(TEXT("manifest"), ESearchCase::IgnoreCase))
			{
				// A plugin mounted inside the project would otherwise be walked
				// twice, double-counting the scan total.
				ManifestFiles.AddUnique(JsonPath);
			}
		}
	}

	for (const FString& FilePath : ManifestFiles)
	{
		ParseManifestFile(FilePath, Result);
	}

	if (OutNumManifestsScanned != nullptr)
	{
		*OutNumManifestsScanned = ManifestFiles.Num();
	}
	return Result;
}

FHapbeatManifestIntensityBaker::FBakeResult FHapbeatManifestIntensityBaker::BakeIntoEventMap(UHapbeatEventMap* Map)
{
	FBakeResult Result;
	if (Map == nullptr)
	{
		return Result;
	}

	const TMap<FHapbeatManifestEventKey, float> ManifestIntensities = ScanProjectManifests(&Result.NumManifestsScanned);

	Map->Modify();
	for (FHapbeatEventEntry& Entry : Map->Entries)
	{
		const FString EventId = Entry.GetEventId();
		const float* Found = EventId.IsEmpty()
			? nullptr
			: ManifestIntensities.Find(FHapbeatManifestEventKey{EventId, Entry.Mode});

		if (Found != nullptr)
		{
			Entry.CachedManifestIntensity = *Found;
			++Result.NumResolved;
		}
		else
		{
			Entry.CachedManifestIntensity = -1.0f;
			++Result.NumUnresolved;
		}
	}
	Map->MarkPackageDirty();

	UE_LOG(LogHapbeatEditor, Log,
		TEXT("[Hapbeat] Refresh Intensities on '%s': scanned %d manifest(s), resolved %d / unresolved %d."),
		*Map->GetName(), Result.NumManifestsScanned, Result.NumResolved, Result.NumUnresolved);

	return Result;
}

void FHapbeatManifestIntensityBaker::ParseManifestFile(const FString& FilePath, TMap<FHapbeatManifestEventKey, float>& OutMap)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
	{
		UE_LOG(LogHapbeatEditor, Warning, TEXT("[Hapbeat] Failed to read manifest '%s'."), *FilePath);
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogHapbeatEditor, Warning, TEXT("[Hapbeat] Failed to parse manifest JSON '%s'."), *FilePath);
		return;
	}

	// schema 2.0.0: "events" (Command) is required by the spec, "stream_events"
	// (StreamClip) is optional. ParseBucket no-ops cleanly if a bucket is absent.
	ParseBucket(Root, TEXT("events"), EHapticMode::Command, OutMap);
	ParseBucket(Root, TEXT("stream_events"), EHapticMode::StreamClip, OutMap);
}

void FHapbeatManifestIntensityBaker::ParseBucket(const TSharedPtr<FJsonObject>& Root, const TCHAR* BucketName,
	EHapticMode Mode, TMap<FHapbeatManifestEventKey, float>& OutMap)
{
	const TSharedPtr<FJsonObject>* BucketObj = nullptr;
	if (!Root->TryGetObjectField(BucketName, BucketObj) || BucketObj == nullptr || !BucketObj->IsValid())
	{
		return;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*BucketObj)->Values)
	{
		if (!Pair.Value.IsValid())
		{
			continue;
		}
		const TSharedPtr<FJsonObject>& EventBody = Pair.Value->AsObject();
		if (!EventBody.IsValid())
		{
			continue;
		}

		// parameters.intensity: default 1.0 if parameters or intensity is absent;
		// TryGetNumberField leaves Intensity untouched on a missing field, so the
		// default survives both "no parameters object" and "parameters present
		// but no intensity key". An authored 0 is honoured (valid silence).
		float Intensity = 1.0f;
		const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
		if (EventBody->TryGetObjectField(TEXT("parameters"), ParamsObj) && ParamsObj != nullptr && ParamsObj->IsValid())
		{
			(*ParamsObj)->TryGetNumberField(TEXT("intensity"), Intensity);
		}

		OutMap.Add(FHapbeatManifestEventKey{Pair.Key, Mode}, Intensity);
	}
}
