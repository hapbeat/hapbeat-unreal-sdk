// Copyright (c) 2026 Hapbeat. MIT License.
#include "SHapbeatShowcaseHud.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SHapbeatShowcaseHud"

namespace
{
	/** Key column width: wide enough for "1-5" / "U/J" without the descriptions drifting apart. */
	constexpr float KeyColumnWidth = 96.0f;

	const FLinearColor KeyColor(1.0f, 0.85f, 0.2f);        // same yellow the zones' HUD lines use
	const FLinearColor DescriptionColor(0.92f, 0.92f, 0.92f);
	const FLinearColor TitleColor(1.0f, 1.0f, 1.0f);
	const FLinearColor ZoneListColor(0.75f, 0.85f, 1.0f);
	const FLinearColor DeviceOkColor(0.5f, 1.0f, 0.5f);    // Unity HudGuide: green when a device answers
	const FLinearColor DeviceNoneColor(1.0f, 0.7f, 0.4f);  // Unity HudGuide: amber when none does
}

void SHapbeatShowcaseHud::Construct(const FArguments& InArgs)
{
	GlobalCommands = InArgs._GlobalCommands;

	ChildSlot
	[
		// Pinned to the top-left corner, sized to its content: the guide must
		// not stretch across the viewport or move as rows are added.
		SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(16.0f, 16.0f, 0.0f, 0.0f))
		[
			SNew(SBorder)
			.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f))
			.Padding(10.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return ZoneTitle; })
						.ColorAndOpacity(FSlateColor(TitleColor))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
					]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return ZoneList; })
						.ColorAndOpacity(FSlateColor(ZoneListColor))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					]

				// The KEY / DESCRIPTION table: two columns whose rows are added
				// in lockstep, so row N of one always lines up with row N of the
				// other (Unity builds the same pair of text blobs line by line).
				+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SBox).WidthOverride(KeyColumnWidth)
								[
									SAssignNew(KeyColumn, SVerticalBox)
								]
							]

						+ SHorizontalBox::Slot().AutoWidth()
							[
								SAssignNew(DescriptionColumn, SVerticalBox)
							]
					]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(this, &SHapbeatShowcaseHud::GetStatusText)
						.ColorAndOpacity(this, &SHapbeatShowcaseHud::GetStatusColor)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
					]
			]
		]
	];

	RebuildCommandRows();
}

void SHapbeatShowcaseHud::SetContent(const FText& InZoneTitle,
	const TArray<FHapbeatShowcaseHudCommand>& InZoneCommands, const FText& InZoneList)
{
	ZoneTitle = InZoneTitle;
	ZoneList = InZoneList;
	ZoneCommands = InZoneCommands;
	RebuildCommandRows();
}

void SHapbeatShowcaseHud::SetDeviceCount(int32 InDeviceCount)
{
	DeviceCount = InDeviceCount;
}

void SHapbeatShowcaseHud::SetRoundTripMs(float InRttMs)
{
	RttMs = InRttMs;
	bPingPending = false;
}

void SHapbeatShowcaseHud::SetPingPending()
{
	bPingPending = true;
}

void SHapbeatShowcaseHud::RebuildCommandRows()
{
	if (!KeyColumn.IsValid() || !DescriptionColumn.IsValid())
	{
		return;
	}

	KeyColumn->ClearChildren();
	DescriptionColumn->ClearChildren();

	for (const FHapbeatShowcaseHudCommand& Command : GlobalCommands)
	{
		AddCommandRow(Command);
	}

	if (GlobalCommands.Num() > 0 && ZoneCommands.Num() > 0)
	{
		AddSpacerRow();
	}

	for (const FHapbeatShowcaseHudCommand& Command : ZoneCommands)
	{
		AddCommandRow(Command);
	}
}

void SHapbeatShowcaseHud::AddCommandRow(const FHapbeatShowcaseHudCommand& Command)
{
	KeyColumn->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(Command.Key)
			.ColorAndOpacity(FSlateColor(KeyColor))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
		];

	DescriptionColumn->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(Command.Description)
			.ColorAndOpacity(FSlateColor(DescriptionColor))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
		];
}

void SHapbeatShowcaseHud::AddSpacerRow()
{
	// A blank row in BOTH columns, never a padding on one of them: the columns
	// stay row-for-row aligned only if every row exists on both sides.
	KeyColumn->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Text(FText::GetEmpty()).Font(FCoreStyle::GetDefaultFontStyle("Regular", 5))
		];
	DescriptionColumn->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Text(FText::GetEmpty()).Font(FCoreStyle::GetDefaultFontStyle("Regular", 5))
		];
}

FText SHapbeatShowcaseHud::GetStatusText() const
{
	// One line, always the same shape, so the panel never changes height as the
	// ping resolves.
	FString Ping;
	if (bPingPending)
	{
		Ping = TEXT("...");
	}
	else if (RttMs >= 0.0f)
	{
		Ping = FString::Printf(TEXT("%.1f ms"), RttMs);
	}
	else
	{
		Ping = TEXT("--");
	}

	return FText::FromString(FString::Printf(
		TEXT("devices reachable: %d   |   ping: %s"), DeviceCount, *Ping));
}

FSlateColor SHapbeatShowcaseHud::GetStatusColor() const
{
	return FSlateColor(DeviceCount > 0 ? DeviceOkColor : DeviceNoneColor);
}

#undef LOCTEXT_NAMESPACE
