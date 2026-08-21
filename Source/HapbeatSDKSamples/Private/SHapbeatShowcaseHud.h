// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "HapbeatShowcaseZone.h" // FHapbeatShowcaseHudCommand
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;

/**
 * The Showcase's on-screen guide: a KEY / DESCRIPTION table in the top-left
 * corner, the zone list above it, and a status footer. UE counterpart of the
 * Unity Showcase's HudGuide (Samples~/Showcase/Scripts/HudGuide.cs), which
 * splits each "KEY | DESC" line into a left and a right Text column.
 *
 * Slate rather than UMG for the same reason the runtime's address-override
 * panel is Slate: no .uasset to author, so the samples module ships as code
 * only and still draws in a packaged build.
 *
 * The switcher owns exactly one of these for the whole session and calls
 * SetContent on every zone change -- the rows are rebuilt, the widget is not.
 */
class SHapbeatShowcaseHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHapbeatShowcaseHud) {}
		/** Rows shown above every zone's own rows (WASD / mouse / 1-5 / Q / P / Tab). */
		SLATE_ARGUMENT(TArray<FHapbeatShowcaseHudCommand>, GlobalCommands)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Replace the zone-dependent part: the "[2] Door" title, that zone's key
	 * rows, and the one-line zone list with the active entry marked.
	 */
	void SetContent(const FText& InZoneTitle, const TArray<FHapbeatShowcaseHudCommand>& InZoneCommands,
		const FText& InZoneList);

	/** Devices answering PONGs right now; drives the footer's colour too. */
	void SetDeviceCount(int32 InDeviceCount);

	/** Latest round-trip time, from a PONG. */
	void SetRoundTripMs(float InRttMs);

	/** Footer shows "Ping: ..." until the next PONG arrives (Unity GlobalHotkeys.NotifyPingSent). */
	void SetPingPending();

private:
	/** Rebuild the two columns from GlobalCommands + the current zone's rows. */
	void RebuildCommandRows();

	void AddCommandRow(const FHapbeatShowcaseHudCommand& Command);
	/** Blank row, used to separate the global block from the zone block. */
	void AddSpacerRow();

	FText GetStatusText() const;
	FSlateColor GetStatusColor() const;

	TArray<FHapbeatShowcaseHudCommand> GlobalCommands;
	TArray<FHapbeatShowcaseHudCommand> ZoneCommands;

	TSharedPtr<SVerticalBox> KeyColumn;
	TSharedPtr<SVerticalBox> DescriptionColumn;

	FText ZoneTitle;
	FText ZoneList;

	int32 DeviceCount = 0;
	float RttMs = -1.0f;
	bool bPingPending = false;
};
