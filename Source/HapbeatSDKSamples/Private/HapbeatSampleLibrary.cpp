// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatSampleLibrary.h"

#include "HapbeatClip.h"
#include "HapbeatEventMap.h"
#include "HapbeatSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
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

UObject* FHapbeatSampleLibrary::LoadShowcaseObject(UClass* Class, const TCHAR* Folder, const TCHAR* AssetName)
{
	if (Class == nullptr || Folder == nullptr || AssetName == nullptr)
	{
		return nullptr;
	}
	// /Package/Path/Asset.Asset -- UE's package-plus-object form.
	const FString Path = FString::Printf(TEXT("/HapbeatSDK/HapbeatSamples/Showcase/%s/%s.%s"),
		Folder, AssetName, AssetName);
	// LOAD_NoWarn | LOAD_Quiet: a missing Showcase asset is an expected state
	// (the art is script-generated and optional), and the caller falls back to an
	// engine primitive -- so it must not print an error on the way past.
	return StaticLoadObject(Class, nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

namespace
{
	/** Mesh bounds size (full extent, cm) plus the index of its longest axis. */
	bool GetMeshSize(const UStaticMesh* Mesh, FVector& OutSize, int32& OutLongestAxis)
	{
		if (Mesh == nullptr)
		{
			return false;
		}
		OutSize = Mesh->GetBounds().BoxExtent * 2.0f;
		if (OutSize.GetAbsMax() <= KINDA_SMALL_NUMBER)
		{
			return false;
		}
		OutLongestAxis = 0;
		for (int32 Axis = 1; Axis < 3; ++Axis)
		{
			if (OutSize[Axis] > OutSize[OutLongestAxis])
			{
				OutLongestAxis = Axis;
			}
		}
		return true;
	}

	/** Unit vector along the given local axis index. */
	FVector AxisVector(int32 AxisIndex)
	{
		FVector Axis = FVector::ZeroVector;
		Axis[FMath::Clamp(AxisIndex, 0, 2)] = 1.0f;
		return Axis;
	}

	/** Right-handed rotation matrices about the Unity axes, column-vector convention. */
	FMatrix UnityRotX(float Radians)
	{
		const float C = FMath::Cos(Radians), S = FMath::Sin(Radians);
		return FMatrix(FPlane(1, 0, 0, 0), FPlane(0, C, -S, 0), FPlane(0, S, C, 0), FPlane(0, 0, 0, 1));
	}
	FMatrix UnityRotY(float Radians)
	{
		const float C = FMath::Cos(Radians), S = FMath::Sin(Radians);
		return FMatrix(FPlane(C, 0, S, 0), FPlane(0, 1, 0, 0), FPlane(-S, 0, C, 0), FPlane(0, 0, 0, 1));
	}
	FMatrix UnityRotZ(float Radians)
	{
		const float C = FMath::Cos(Radians), S = FMath::Sin(Radians);
		return FMatrix(FPlane(C, -S, 0, 0), FPlane(S, C, 0, 0), FPlane(0, 0, 1, 0), FPlane(0, 0, 0, 1));
	}
}

FVector FHapbeatSampleLibrary::ComputeAxisFitScale(const UStaticMesh* Mesh, const FVector& SortedTargetSizeCm)
{
	FVector Size;
	int32 LongestAxis = 0;
	if (!GetMeshSize(Mesh, Size, LongestAxis))
	{
		return FVector::OneVector;
	}

	// Rank the mesh's own axes largest-first, so target[0] always lands on the
	// mesh's longest axis whichever of X/Y/Z that happens to be.
	int32 Order[3] = { 0, 1, 2 };
	for (int32 A = 0; A < 3; ++A)
	{
		for (int32 B = A + 1; B < 3; ++B)
		{
			if (Size[Order[B]] > Size[Order[A]])
			{
				Swap(Order[A], Order[B]);
			}
		}
	}

	const float LongestSize = FMath::Max(Size[Order[0]], KINDA_SMALL_NUMBER);
	const float LongestTarget = SortedTargetSizeCm[0];
	// A non-positive longest target would leave every axis undefined, so treat
	// it as "no resize at all" rather than guessing.
	const float ProportionalScale = LongestTarget > 0.0f ? LongestTarget / LongestSize : 1.0f;

	FVector Scale = FVector::OneVector;
	for (int32 Rank = 0; Rank < 3; ++Rank)
	{
		const int32 Axis = Order[Rank];
		const float Target = SortedTargetSizeCm[Rank];
		Scale[Axis] = Target > 0.0f
			? Target / FMath::Max(Size[Axis], KINDA_SMALL_NUMBER)
			: ProportionalScale; // "keep the proportion"
	}
	return Scale;
}

FRotator FHapbeatSampleLibrary::ComputeLongestAxisToForwardRotation(const UStaticMesh* Mesh)
{
	FVector Size;
	int32 LongestAxis = 0;
	if (!GetMeshSize(Mesh, Size, LongestAxis))
	{
		return FRotator::ZeroRotator;
	}
	// The shortest rotation that brings the axis onto +X: no arbitrary spin is
	// introduced about the axis itself, which matters for a prop whose other two
	// axes are not interchangeable.
	return FQuat::FindBetweenNormals(AxisVector(LongestAxis), FVector::ForwardVector).Rotator();
}

FRotator FHapbeatSampleLibrary::ComputeLongestAxisToUpRotation(const UStaticMesh* Mesh)
{
	FVector Size;
	int32 LongestAxis = 0;
	if (!GetMeshSize(Mesh, Size, LongestAxis))
	{
		return FRotator::ZeroRotator;
	}
	return FQuat::FindBetweenNormals(AxisVector(LongestAxis), FVector::UpVector).Rotator();
}

FRotator FHapbeatSampleLibrary::ComputeShortestAxisToDirectionRotation(const UStaticMesh* Mesh,
	const FVector& WorldDirection)
{
	FVector Size;
	int32 LongestAxis = 0;
	const FVector Direction = WorldDirection.GetSafeNormal();
	if (!GetMeshSize(Mesh, Size, LongestAxis) || Direction.IsNearlyZero())
	{
		return FRotator::ZeroRotator;
	}
	int32 ShortestAxis = 0;
	for (int32 Axis = 1; Axis < 3; ++Axis)
	{
		if (Size[Axis] < Size[ShortestAxis])
		{
			ShortestAxis = Axis;
		}
	}
	return FQuat::FindBetweenNormals(AxisVector(ShortestAxis), Direction).Rotator();
}

FVector FHapbeatSampleLibrary::ComputeFittedBoundsCentre(const UStaticMesh* Mesh, const FVector& Scale,
	const FRotator& Rotation)
{
	if (Mesh == nullptr)
	{
		return FVector::ZeroVector;
	}
	return Rotation.RotateVector(Mesh->GetBounds().Origin * Scale);
}

FRotator FHapbeatSampleLibrary::UnityEulerToUERotator(const FVector& UnityEulerDeg)
{
	// Unity's Quaternion.Euler(x, y, z) applies Z, then X, then Y: M = Ry*Rx*Rz,
	// each a right-handed rotation about the corresponding Unity axis.
	const FMatrix Unity =
		UnityRotY(FMath::DegreesToRadians(UnityEulerDeg.Y))
		* UnityRotX(FMath::DegreesToRadians(UnityEulerDeg.X))
		* UnityRotZ(FMath::DegreesToRadians(UnityEulerDeg.Z));

	// Column i of that matrix is the image of Unity's i-th basis vector. Push
	// each through the basis permutation (Unity x,y,z) -> (UE y,z,x) to read it
	// as a UE vector, and pick the column that corresponds to each UE axis:
	// UE X is Unity Z (column 2), UE Y is Unity X (column 0), UE Z is Unity Y
	// (column 1).
	auto UnityColumnToUEVector = [&Unity](int32 Column)
	{
		const FVector Col(Unity.M[0][Column], Unity.M[1][Column], Unity.M[2][Column]);
		return FVector(Col.Z, Col.X, Col.Y);
	};

	// FMatrix is row-vector major, so its rows ARE the world-space X / Y / Z axes.
	return FMatrix(
		UnityColumnToUEVector(2),
		UnityColumnToUEVector(0),
		UnityColumnToUEVector(1),
		FVector::ZeroVector).Rotator();
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
