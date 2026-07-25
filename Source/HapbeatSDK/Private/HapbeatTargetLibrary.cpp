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

FString UHapbeatTargetLibrary::ResolveTarget(const FString& Target, int32 OverridePlayer, int32 OverrideGroup)
{
	// BothDisabled_PassesThroughUnchanged: full passthrough. This is the
	// backward-compatibility guarantee — projects that never touch the address
	// override must see byte-for-byte identical target strings.
	if (OverridePlayer < 1 && OverrideGroup < 1)
	{
		return Target;
	}

	// Segment prefixes ("player_", "group_", "pos_") are compared case-sensitively
	// (ESearchCase::CaseSensitive), matching the C# port's StringComparison.Ordinal —
	// FString's default StartsWith/operator== are case-INSENSITIVE, so this must be
	// explicit at every comparison below.
	TArray<FString> Segs;
	Target.ParseIntoArray(Segs, TEXT("/"), /*InCullEmpty=*/true);

	if (OverridePlayer >= 1)
	{
		// PlayerOverrideOnly_ReplacesOrInsertsPlayerSegment
		const FString PlayerSeg = FString::Printf(TEXT("player_%d"), OverridePlayer);
		const int32 PlayerIdx = Segs.IndexOfByPredicate([](const FString& S)
			{ return S.StartsWith(TEXT("player_"), ESearchCase::CaseSensitive); });
		if (PlayerIdx != INDEX_NONE)
		{
			Segs[PlayerIdx] = PlayerSeg;
		}
		else
		{
			const int32 PosIdx = Segs.IndexOfByPredicate([](const FString& S)
				{ return S.StartsWith(TEXT("pos_"), ESearchCase::CaseSensitive); });
			if (PosIdx > 0)
			{
				Segs[PosIdx - 1] = PlayerSeg; // replace the placeholder segment (e.g. "*") right before position
			}
			else
			{
				// PosIdx == 0 (position at front) or PosIdx == INDEX_NONE (no position segment).
				Segs.Insert(PlayerSeg, 0);
			}
		}
	}

	if (OverrideGroup >= 1)
	{
		// GroupOverrideOnly_AppendsOrReplacesGroupSegment / BothOverridesActive_*
		//
		// Firmware/spec matching is positional (device-addressing.md §2): the
		// i-th target segment is compared against the i-th address segment
		// only, with "*" consuming exactly one slot. group_ must therefore land
		// in its grammar slot (immediately after {position}); a naive append at
		// the end lands it in whatever slot happens to be next, so firmware
		// never matches.
		const FString GroupSeg = FString::Printf(TEXT("group_%d"), OverrideGroup);
		const int32 GroupIdx = Segs.IndexOfByPredicate([](const FString& S)
			{ return S.StartsWith(TEXT("group_"), ESearchCase::CaseSensitive); });
		if (GroupIdx != INDEX_NONE)
		{
			Segs[GroupIdx] = GroupSeg;
		}
		else
		{
			const int32 PosIdx = Segs.IndexOfByPredicate([](const FString& S)
				{ return S.StartsWith(TEXT("pos_"), ESearchCase::CaseSensitive); });
			if (PosIdx != INDEX_NONE)
			{
				Segs.Insert(GroupSeg, PosIdx + 1);
			}
			else
			{
				// No explicit position segment. Locate the player slot: an
				// explicit player_ segment, or a leading bare "*" acting as the
				// player wildcard.
				int32 PlayerIdx = Segs.IndexOfByPredicate([](const FString& S)
					{ return S.StartsWith(TEXT("player_"), ESearchCase::CaseSensitive); });
				if (PlayerIdx == INDEX_NONE && Segs.Num() > 0 && Segs[0] == TEXT("*"))
				{
					PlayerIdx = 0; // leading wildcard occupies the player slot
				}

				if (PlayerIdx != INDEX_NONE)
				{
					// Position slot is the segment right after the player slot.
					// Only pad a "*" placeholder when that slot is actually
					// empty — if the target already occupies it (e.g. a bare
					// "*" that the player-override step left in the position
					// slot for a target like "*"), reuse it so group_ stays in
					// the 3rd slot instead of being pushed to a 4th, which would
					// make the target longer than the device address and break
					// the positional match entirely.
					// (BothOverridesActive_NoExistingPositionOrGroup_PadsPositionSlot)
					const int32 PosSlot = PlayerIdx + 1;
					if (PosSlot >= Segs.Num())
					{
						Segs.Insert(TEXT("*"), PosSlot); // no position segment yet — pad it
					}
					Segs.Insert(GroupSeg, PosSlot + 1);
				}
				else
				{
					// Everything present (if anything) is a free prefix with no
					// player/position slot. Append player and position
					// placeholders, then group, so group stays after position
					// and the prefix is preserved ahead of it (e.g. ""
					// -> "*/*/group_M", "red" -> "red/*/*/group_M").
					Segs.Add(TEXT("*"));
					Segs.Add(TEXT("*"));
					Segs.Add(GroupSeg);
				}
			}
		}
	}

	// MultiWildcard_DoesNotThrow: no special-cased inputs above throw — every
	// branch degrades to an index-based Insert/Assign on whatever segments are
	// actually present.
	return FString::Join(Segs, TEXT("/"));
}

FString UHapbeatTargetLibrary::ApplyAddressPlaceholders(const FString& AppName, int32 OverridePlayer, int32 OverrideGroup)
{
	if (AppName.IsEmpty())
	{
		return AppName;
	}

	// "-" for a disabled axis, matching Unity ApplyAddressPlaceholders exactly.
	const FString P = OverridePlayer >= 1 ? FString::FromInt(OverridePlayer) : TEXT("-");
	const FString G = OverrideGroup >= 1 ? FString::FromInt(OverrideGroup) : TEXT("-");
	return AppName.Replace(TEXT("<p>"), *P, ESearchCase::CaseSensitive)
				  .Replace(TEXT("<g>"), *G, ESearchCase::CaseSensitive);
}
