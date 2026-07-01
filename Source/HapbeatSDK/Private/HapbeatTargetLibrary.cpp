// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatTargetLibrary.h"

FString UHapbeatTargetLibrary::BuildTarget(int32 Player, const FString& Position, int32 Group)
{
	// Mirrors HapbeatTargetEditorUtil.BuildTargetFromParts (Unity), minus the
	// free-prefix part (the SDK send path composes only player/pos/group).
	TArray<FString> Parts;

	const bool bHasPosition = !Position.IsEmpty();
	const bool bHasGroup = Group >= 1;

	if (Player >= 1)
	{
		Parts.Add(FString::Printf(TEXT("player_%d"), Player));
	}
	else if (bHasPosition || bHasGroup)
	{
		// Wildcard the player slot when only a position / group is specified, so
		// the fixed player/position ordering (device-addressing.md §2.2) holds.
		Parts.Add(TEXT("*"));
	}

	if (bHasPosition)
	{
		Parts.Add(Position);
	}
	else if (bHasGroup)
	{
		// group_{M} must come immediately after the position segment, so wildcard
		// the position slot when only a group is specified.
		Parts.Add(TEXT("*"));
	}

	if (bHasGroup)
	{
		Parts.Add(FString::Printf(TEXT("group_%d"), Group));
	}

	return FString::Join(Parts, TEXT("/"));
}

void UHapbeatTargetLibrary::ParseTarget(const FString& Target, int32& OutPlayer, FString& OutPosition, int32& OutGroup)
{
	OutPlayer = -1;
	OutPosition = FString();
	OutGroup = -1;

	if (Target.IsEmpty())
	{
		return;
	}

	TArray<FString> Segments;
	Target.ParseIntoArray(Segments, TEXT("/"), /*InCullEmpty=*/true);

	for (const FString& Segment : Segments)
	{
		if (Segment.StartsWith(TEXT("player_")))
		{
			const FString NumStr = Segment.RightChop(7); // len("player_")
			if (NumStr.IsNumeric())
			{
				OutPlayer = FCString::Atoi(*NumStr);
			}
		}
		else if (Segment.StartsWith(TEXT("group_")))
		{
			const FString NumStr = Segment.RightChop(6); // len("group_")
			if (NumStr.IsNumeric())
			{
				OutGroup = FCString::Atoi(*NumStr);
			}
		}
		else if (Segment.StartsWith(TEXT("pos_")))
		{
			OutPosition = Segment;
		}
		// '*' wildcards and any free-prefix segments are ignored.
	}
}
