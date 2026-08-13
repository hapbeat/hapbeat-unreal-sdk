// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"

/**
 * Tells the user when a newer Hapbeat Unreal SDK has been published.
 *
 * Implements the cross-product update-notification policy (DEC-053): every
 * client reads the same release feed, and the feed is generated from the actual
 * distribution channels rather than from git tags, so a version that was tagged
 * but never published is never advertised as available.
 *
 * Port of Editor/HapbeatUpdateCheck.cs (Unity SDK), same feed and same
 * "notify at most once per editor session" frequency rule -- the notice has no
 * dismiss affordance, so per DEC-053 it fires once per run rather than once per
 * version. Unity needs SessionState to survive domain reloads; an editor
 * session here is just the process, so a static flag is enough.
 */
class FHapbeatUpdateCheck
{
public:
	/** Hooks the startup check and registers the Tools menu entries. */
	static void Register();
	static void Unregister();

	/** Runs a check now and always reports the outcome, even when up to date. */
	static void CheckNow();

private:
	static void CheckIfDue(bool bUserInitiated);
	static void HandleFeedResponse(const FString& Payload, bool bUserInitiated);

	/** Version string from the plugin descriptor, e.g. "0.1.0". */
	static FString GetInstalledVersion();

	/** True when the startup check is enabled (persisted per user, default on). */
	static bool IsAutoCheckEnabled();
	static void SetAutoCheckEnabled(bool bEnabled);

	/** Set once a notice has been emitted, so a session never nags twice. */
	static bool bNotifiedThisSession;
};
