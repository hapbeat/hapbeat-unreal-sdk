// Copyright (c) 2026 Hapbeat. MIT License.
#include "SHapbeatAddressOverridePanel.h"

#include "HapbeatConfig.h"
#include "HapbeatSubsystem.h"
#include "HapbeatTargetLibrary.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SHapbeatAddressOverridePanel"

namespace
{
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 18);
	const FSlateFontInfo BodyFont = FCoreStyle::GetDefaultFontStyle("Regular", 16);
	const FSlateFontInfo AddressValueFont = FCoreStyle::GetDefaultFontStyle("Bold", 18);

	/** A concrete target used only to make the effect of an override visible. */
	const TCHAR* PreviewTarget = TEXT("player_1/pos_chest/group_1");

	/** Event the Test button fires when the component names none. */
	const TCHAR* DefaultTestEventId = TEXT("sample-kit.sine_100hz");

	/** Marks values that are edited but not yet applied. */
	const FLinearColor PendingColor(1.0f, 0.85f, 0.2f);

	FText ResolveTargetPlayerLabel(int32 OverridePlayer, int32 OverrideGroup)
	{
		int32 Player = -1;
		int32 Group = -1;
		FString Position;
		UHapbeatTargetLibrary::ParseTarget(
			UHapbeatTargetLibrary::ResolveTarget(PreviewTarget, OverridePlayer, OverrideGroup),
			Player, Position, Group);
		return Player < 1 ? LOCTEXT("TargetPlayerOff", "off") : FText::AsNumber(Player);
	}

	FText ResolveTargetGroupLabel(int32 OverridePlayer, int32 OverrideGroup)
	{
		int32 Player = -1;
		int32 Group = -1;
		FString Position;
		UHapbeatTargetLibrary::ParseTarget(
			UHapbeatTargetLibrary::ResolveTarget(PreviewTarget, OverridePlayer, OverrideGroup),
			Player, Position, Group);
		return Group < 1 ? LOCTEXT("TargetGroupOff", "off") : FText::AsNumber(Group);
	}
}

void SHapbeatAddressOverridePanel::Construct(const FArguments& InArgs)
{
	WeakSubsystem = InArgs._Subsystem;
	bPersistOnApply = InArgs._bPersistOnApply;
	bShowCloseButton = InArgs._bShowCloseButton;
	TestEventId = InArgs._TestEventId;
	OnCloseRequested = InArgs._OnCloseRequested;

	// Start from what is actually applied, so opening the panel and closing it
	// again cannot change anything.
	if (const UHapbeatSubsystem* Subsystem = GetSubsystem())
	{
		EditingPlayer = Subsystem->GetOverridePlayer();
		EditingGroup = Subsystem->GetOverrideGroup();
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f))
		.Padding(12.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "Hapbeat -- Device Address"))
					.Font(TitleFont)
					.ColorAndOpacity(FLinearColor::White)
				]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					MakeStepperRow(LOCTEXT("Player", "Player"),
						TAttribute<FText>(this, &SHapbeatAddressOverridePanel::GetPlayerLabel),
						TAttribute<FSlateColor>(this, &SHapbeatAddressOverridePanel::GetPlayerEditColor),
						TAttribute<bool>(this, &SHapbeatAddressOverridePanel::IsPlayerEditable),
						[this](int32 Delta) { StepPlayer(Delta); })
				]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					MakeStepperRow(LOCTEXT("Group", "Group"),
						TAttribute<FText>(this, &SHapbeatAddressOverridePanel::GetGroupLabel),
						TAttribute<FSlateColor>(this, &SHapbeatAddressOverridePanel::GetGroupEditColor),
						TAttribute<bool>(this, &SHapbeatAddressOverridePanel::IsGroupEditable),
						[this](int32 Delta) { StepGroup(Delta); })
				]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)
				[
					MakeTargetRow(
						LOCTEXT("CurrentTarget", "Now"),
						TAttribute<FText>(this, &SHapbeatAddressOverridePanel::GetCurrentTargetPlayerLabel),
						FSlateColor(FLinearColor::White),
						TAttribute<FText>(this, &SHapbeatAddressOverridePanel::GetCurrentTargetGroupLabel),
						FSlateColor(FLinearColor::White))
				]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					MakeTargetRow(
						LOCTEXT("TargetAfterApply", "Next"),
						TAttribute<FText>(this, &SHapbeatAddressOverridePanel::GetPendingTargetPlayerLabel),
						TAttribute<FSlateColor>(this, &SHapbeatAddressOverridePanel::GetPendingTargetPlayerColor),
						TAttribute<FText>(this, &SHapbeatAddressOverridePanel::GetPendingTargetGroupLabel),
						TAttribute<FSlateColor>(this, &SHapbeatAddressOverridePanel::GetPendingTargetGroupColor))
				]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("SavedPrefix", "Saved on this device:  Player ")).Font(BodyFont).ColorAndOpacity(FLinearColor::White)
						]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(this, &SHapbeatAddressOverridePanel::GetSavedPlayerLabel).Font(AddressValueFont).ColorAndOpacity(FLinearColor::White)
						]
					+ SHorizontalBox::Slot().AutoWidth().Padding(16.0f, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("SavedGroupPrefix", "Group ")).Font(BodyFont).ColorAndOpacity(FLinearColor::White)
						]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(this, &SHapbeatAddressOverridePanel::GetSavedGroupLabel).Font(AddressValueFont).ColorAndOpacity(FLinearColor::White)
						]
				]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SButton)
							.IsFocusable(false)
							.Text(LOCTEXT("Apply", "Apply"))
							.ToolTipText(LOCTEXT("ApplyTooltip", "Send every later command to this player / group."))
							.OnClicked(this, &SHapbeatAddressOverridePanel::OnApplyClicked)
						]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SButton)
							.IsFocusable(false)
							.Text(LOCTEXT("Test", "Test"))
							.ToolTipText(LOCTEXT("TestTooltip", "Fire one event so you can feel which device you are addressing."))
							.OnClicked(this, &SHapbeatAddressOverridePanel::OnTestClicked)
						]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(SButton)
							.IsFocusable(false)
							.Text(LOCTEXT("Clear", "Clear"))
							.ToolTipText(LOCTEXT("ClearTooltip", "Turn both axes off and forget the saved choice."))
							.OnClicked(this, &SHapbeatAddressOverridePanel::OnClearClicked)
						]
					+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton)
							.IsFocusable(false)
							.Visibility(bShowCloseButton ? EVisibility::Visible : EVisibility::Collapsed)
							.Text(LOCTEXT("Close", "Close"))
							.OnClicked(this, &SHapbeatAddressOverridePanel::OnCloseClicked)
						]
				]
		]
	];
}

TSharedRef<SWidget> SHapbeatAddressOverridePanel::MakeStepperRow(
	const FText& Label,
	TAttribute<FText> ValueText,
	TAttribute<FSlateColor> ValueColor,
	TAttribute<bool> IsEditable,
	TFunction<void(int32)> OnStep)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(64.0f)
				[
					SNew(STextBlock).Text(Label).Font(BodyFont).ColorAndOpacity(FLinearColor::White)
				]
			]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.IsFocusable(false)
				.IsEnabled(IsEditable)
				.OnClicked_Lambda([OnStep] { OnStep(-1); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(LOCTEXT("Minus", "-")).Font(BodyFont).ColorAndOpacity(FLinearColor::White)
				]
			]
		// Fixed width: the value swings between "off" and two digits, and a
		// row that resized would shove the +/- buttons around under the cursor.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(72.0f).HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(ValueText)
					.Font(BodyFont)
					.ColorAndOpacity(ValueColor)
				]
			]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.IsFocusable(false)
				.IsEnabled(IsEditable)
				.OnClicked_Lambda([OnStep] { OnStep(1); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(LOCTEXT("Plus", "+")).Font(BodyFont).ColorAndOpacity(FLinearColor::White)
				]
			];
}

TSharedRef<SWidget> SHapbeatAddressOverridePanel::MakeTargetRow(
	const FText& Label,
	TAttribute<FText> PlayerText,
	TAttribute<FSlateColor> PlayerColor,
	TAttribute<FText> GroupText,
	TAttribute<FSlateColor> GroupColor)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(46.0f)
			[
				SNew(STextBlock).Text(Label).Font(BodyFont).ColorAndOpacity(FLinearColor::White)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(30.0f)
			[
				SNew(STextBlock).Text(LOCTEXT("TargetArrow", "->")).Font(BodyFont).ColorAndOpacity(FLinearColor::White)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(LOCTEXT("TargetPlayerPrefix", "player_")).Font(BodyFont).ColorAndOpacity(FLinearColor::White)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(PlayerText).Font(BodyFont).ColorAndOpacity(PlayerColor)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(LOCTEXT("TargetMiddle", "/pos_chest/group_")).Font(BodyFont).ColorAndOpacity(FLinearColor::White)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(GroupText).Font(BodyFont).ColorAndOpacity(GroupColor)
		];
}

UHapbeatSubsystem* SHapbeatAddressOverridePanel::GetSubsystem() const
{
	return WeakSubsystem.Get();
}

int32 SHapbeatAddressOverridePanel::Step(int32 Value, int32 Delta)
{
	// -1 is "off", and the wire vocabulary is 1..99, so stepping walks
	// off -> 1 .. 99 -> off with nothing in between.
	const int32 Next = (Value < 1) ? (Delta > 0 ? 1 : 99) : Value + Delta;
	if (Next < 1 || Next > 99)
	{
		return -1;
	}
	return Next;
}

bool SHapbeatAddressOverridePanel::IsPlayerEditable() const
{
	const UHapbeatConfig* Config = GetDefault<UHapbeatConfig>();
	return Config == nullptr || UHapbeatSubsystem::NormalizeAddressOverride(Config->ForcedOverridePlayer) < 1;
}

bool SHapbeatAddressOverridePanel::IsGroupEditable() const
{
	const UHapbeatConfig* Config = GetDefault<UHapbeatConfig>();
	return Config == nullptr || UHapbeatSubsystem::NormalizeAddressOverride(Config->ForcedOverrideGroup) < 1;
}

void SHapbeatAddressOverridePanel::StepPlayer(int32 Delta)
{
	if (IsPlayerEditable())
	{
		EditingPlayer = Step(EditingPlayer, Delta);
	}
}

void SHapbeatAddressOverridePanel::StepGroup(int32 Delta)
{
	if (IsGroupEditable())
	{
		EditingGroup = Step(EditingGroup, Delta);
	}
}

FText SHapbeatAddressOverridePanel::GetPlayerLabel() const
{
	if (!IsPlayerEditable())
	{
		const UHapbeatSubsystem* Subsystem = GetSubsystem();
		return FText::Format(LOCTEXT("PinnedValue", "{0} (pinned)"),
			FText::AsNumber(Subsystem != nullptr ? Subsystem->GetOverridePlayer() : -1));
	}
	return EditingPlayer < 1 ? LOCTEXT("Off", "off") : FText::AsNumber(EditingPlayer);
}

FText SHapbeatAddressOverridePanel::GetGroupLabel() const
{
	if (!IsGroupEditable())
	{
		const UHapbeatSubsystem* Subsystem = GetSubsystem();
		return FText::Format(LOCTEXT("PinnedValue", "{0} (pinned)"),
			FText::AsNumber(Subsystem != nullptr ? Subsystem->GetOverrideGroup() : -1));
	}
	return EditingGroup < 1 ? LOCTEXT("Off", "off") : FText::AsNumber(EditingGroup);
}

FText SHapbeatAddressOverridePanel::GetCurrentTargetPlayerLabel() const
{
	const UHapbeatSubsystem* Subsystem = GetSubsystem();
	const int32 AppliedPlayer = Subsystem != nullptr ? Subsystem->GetOverridePlayer() : -1;
	const int32 AppliedGroup = Subsystem != nullptr ? Subsystem->GetOverrideGroup() : -1;
	return ResolveTargetPlayerLabel(AppliedPlayer, AppliedGroup);
}

FText SHapbeatAddressOverridePanel::GetCurrentTargetGroupLabel() const
{
	const UHapbeatSubsystem* Subsystem = GetSubsystem();
	const int32 AppliedPlayer = Subsystem != nullptr ? Subsystem->GetOverridePlayer() : -1;
	const int32 AppliedGroup = Subsystem != nullptr ? Subsystem->GetOverrideGroup() : -1;
	return ResolveTargetGroupLabel(AppliedPlayer, AppliedGroup);
}

FText SHapbeatAddressOverridePanel::GetPendingTargetPlayerLabel() const
{
	return ResolveTargetPlayerLabel(EditingPlayer, EditingGroup);
}

FText SHapbeatAddressOverridePanel::GetPendingTargetGroupLabel() const
{
	return ResolveTargetGroupLabel(EditingPlayer, EditingGroup);
}

FText SHapbeatAddressOverridePanel::GetSavedPlayerLabel() const
{
	int32 SavedPlayer = -1;
	int32 SavedGroup = -1;
	if (!UHapbeatSubsystem::TryGetPersistedAddressOverride(SavedPlayer, SavedGroup))
	{
		return LOCTEXT("Off", "off");
	}
	return SavedPlayer < 1 ? LOCTEXT("Off", "off") : FText::AsNumber(SavedPlayer);
}

FText SHapbeatAddressOverridePanel::GetSavedGroupLabel() const
{
	int32 SavedPlayer = -1;
	int32 SavedGroup = -1;
	if (!UHapbeatSubsystem::TryGetPersistedAddressOverride(SavedPlayer, SavedGroup))
	{
		return LOCTEXT("Off", "off");
	}
	return SavedGroup < 1 ? LOCTEXT("Off", "off") : FText::AsNumber(SavedGroup);
}

FSlateColor SHapbeatAddressOverridePanel::GetPlayerEditColor() const
{
	const UHapbeatSubsystem* Subsystem = GetSubsystem();
	const bool bPlayerChanged = Subsystem != nullptr && EditingPlayer != Subsystem->GetOverridePlayer();
	return bPlayerChanged && EditingPlayer >= 1 ? FSlateColor(PendingColor) : FSlateColor(FLinearColor::White);
}

FSlateColor SHapbeatAddressOverridePanel::GetGroupEditColor() const
{
	const UHapbeatSubsystem* Subsystem = GetSubsystem();
	const bool bGroupChanged = Subsystem != nullptr && EditingGroup != Subsystem->GetOverrideGroup();
	return bGroupChanged && EditingGroup >= 1 ? FSlateColor(PendingColor) : FSlateColor(FLinearColor::White);
}

FSlateColor SHapbeatAddressOverridePanel::GetPendingTargetPlayerColor() const
{
	const UHapbeatSubsystem* Subsystem = GetSubsystem();
	return Subsystem != nullptr && EditingPlayer != Subsystem->GetOverridePlayer()
		? FSlateColor(PendingColor) : FSlateColor(FLinearColor::White);
}

FSlateColor SHapbeatAddressOverridePanel::GetPendingTargetGroupColor() const
{
	const UHapbeatSubsystem* Subsystem = GetSubsystem();
	return Subsystem != nullptr && EditingGroup != Subsystem->GetOverrideGroup()
		? FSlateColor(PendingColor) : FSlateColor(FLinearColor::White);
}

FReply SHapbeatAddressOverridePanel::OnApplyClicked()
{
	if (UHapbeatSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->SetAddressOverride(EditingPlayer, EditingGroup, bPersistOnApply);
	}
	return FReply::Handled();
}

FReply SHapbeatAddressOverridePanel::OnClearClicked()
{
	if (UHapbeatSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->SetAddressOverride(-1, -1, /*bPersist=*/false);
		Subsystem->ClearPersistedAddressOverride();
		EditingPlayer = Subsystem->GetOverridePlayer();
		EditingGroup = Subsystem->GetOverrideGroup();
	}
	return FReply::Handled();
}

FReply SHapbeatAddressOverridePanel::OnTestClicked()
{
	if (UHapbeatSubsystem* Subsystem = GetSubsystem())
	{
		// Deliberately fires through the applied override, not the staged edit:
		// the button answers "which device am I addressing right now?".
		Subsystem->Play(TestEventId.IsEmpty() ? DefaultTestEventId : TestEventId, 1.0f);
	}
	return FReply::Handled();
}

FReply SHapbeatAddressOverridePanel::OnCloseClicked()
{
	OnCloseRequested.ExecuteIfBound();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
