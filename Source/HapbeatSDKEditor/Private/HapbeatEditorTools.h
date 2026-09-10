// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"

class UHapbeatEventMap;

/**
 * Small Tools-menu utilities ported from the Unity SDK's editor scripts.
 *
 * These are grouped rather than given a file each because they share nothing
 * but their menu: none carries state, and each is a single action on the
 * current selection or on every Event Map in the project.
 *
 * - Verbose log toggle   (Unity Editor/HapbeatVerboseLogToggle.cs)
 * - Event Map -> Markdown (Unity Editor/HapbeatEventMapMarkdownExport.cs)
 */
class FHapbeatEditorTools
{
public:
	static void RegisterMenus(struct FToolMenuSection& Section);

	/** Turns verbose logging off on every Hapbeat trigger in the open level. */
	static void SetVerboseLogOnAllTriggers(bool bEnabled);

	/**
	 * Writes a Markdown table of an Event Map's entries next to the asset, for
	 * pasting into design docs and reviews.
	 */
	static void ExportEventMapToMarkdown(UHapbeatEventMap* Map);

private:
	/** Opens a picker, then exports the Event Map the user explicitly chooses. */
	static void PromptExportEventMapToMarkdown();
};
