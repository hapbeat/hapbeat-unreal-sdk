// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HapbeatTargetLibrary.generated.h"

/**
 * Blueprint-callable helpers for composing / decomposing device-addressing
 * target strings, per hapbeat-contracts device-addressing.md §2:
 *   [player_N/] {pos_X} [/group_M]
 *
 * Mirrors HapbeatTargetEditorUtil (Unity SDK). An empty target = broadcast to
 * all devices. These are also used at the send boundary so the wire encoding
 * stays in sync with the spec.
 */
UCLASS()
class HAPBEATSDK_API UHapbeatTargetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Build a target string from its parts.
	 *   - Player <= 0  => omitted; a '*' wildcard is emitted instead only when a
	 *                     position or group IS present (so the leading segment is
	 *                     the player slot per the spec's fixed player/position order).
	 *   - Position ""  => omitted; a '*' wildcard is emitted instead only when a
	 *                     group IS present (group must sit right after position).
	 *   - Group <= 0   => omitted.
	 * All parts unset => "" (broadcast to all).
	 *
	 * Examples: (1,"pos_chest",-1) => "player_1/pos_chest";
	 *           (2,"",-1) => "player_2";
	 *           (-1,"pos_neck",-1) => wildcard player, i.e. asterisk + "/pos_neck";
	 *           (-1,"",1) => two wildcard segments, then "/group_1".
	 * (Wildcard examples are spelled out in words: the literal target text would
	 *  contain an asterisk-slash pair, which terminates this comment block.)
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Target")
	static FString BuildTarget(int32 Player = -1, const FString& Position = TEXT(""), int32 Group = -1);

	/**
	 * Best-effort inverse of BuildTarget. Splits on '/' and classifies each
	 * segment by prefix so it is tolerant to order / wildcards:
	 *   player_{N} => OutPlayer, group_{N} => OutGroup, pos_* => OutPosition.
	 * Unset parts return sentinels: OutPlayer = -1, OutGroup = -1, OutPosition = "".
	 * '*' and any free-prefix segments are ignored.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Target")
	static void ParseTarget(const FString& Target, int32& OutPlayer, FString& OutPosition, int32& OutGroup);

	/**
	 * Rewrite Target so the player_ / group_ segments carry forced values,
	 * without disturbing anything else in the string. A VERBATIM port of
	 * HapbeatClient.ResolveTarget(string, int, int) (Unity SDK) — same rule
	 * order, same segment-slot placement, same edge cases. Pure / static so it
	 * can be exercised directly (see the .cpp for the rule-by-rule mapping to
	 * Tests/Runtime/ResolveTargetTests.cs, the Unity SDK's authoritative spec).
	 *
	 * This is the mechanism behind UHapbeatSubsystem::SetAddressOverride: one
	 * identical build deployed to many HMDs, each pinned to its own Hapbeat via
	 * a per-launch player/group override, WITHOUT editing every EventMap entry's
	 * authored target string.
	 *
	 * Grammar (hapbeat-contracts/specs/device-addressing.md §2):
	 *   [prefix/] player_{N} / {position} [/group_{M}]
	 *
	 * @param Target         Original EventMap/API target string. May be empty
	 *                       (FString has no null, so Unity's null-target case
	 *                       collapses into the empty-string case here).
	 * @param OverridePlayer Forced player number, or &lt; 1 to leave the player slot alone.
	 * @param OverrideGroup  Forced group number, or &lt; 1 to leave the group slot alone.
	 * @return Target completely unchanged when BOTH overrides are &lt; 1 (disabled) —
	 *         this is what keeps existing projects' behavior byte-for-byte identical
	 *         when the feature isn't used. Otherwise the rewritten target string.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Target")
	static FString ResolveTarget(const FString& Target, int32 OverridePlayer, int32 OverrideGroup);

	/**
	 * Replace the "<p>" / "<g>" placeholders in an app name with the current
	 * address-override player / group number, or "-" when that axis is disabled
	 * (< 1). Lets one templated name (e.g. "Booth <p>/<g>") show each HMD's own
	 * override on the device OLED. Pure; returns AppName unchanged when it is
	 * empty or contains no placeholders. Verbatim port of
	 * HapbeatManager.ApplyAddressPlaceholders (Unity SDK).
	 *
	 * Callers cap the RESULT at FHapbeatProtocol::MaxAppNameLen for the wire
	 * (BuildConnectStatus already does), so substitute BEFORE capping — capping
	 * the raw template first could cut a placeholder in half.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Target")
	static FString ApplyAddressPlaceholders(const FString& AppName, int32 OverridePlayer, int32 OverrideGroup);
};
