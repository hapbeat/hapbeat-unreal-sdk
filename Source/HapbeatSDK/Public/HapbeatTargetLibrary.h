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
	 *           (-1,"pos_neck",-1) => "*/pos_neck";
	 *           (2,"",-1) => "player_2";
	 *           (-1,"",1) => "*/*/group_1".
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
};
