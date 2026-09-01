// Copyright (c) 2026 Hapbeat. MIT License.
#include "SHapbeatAddressOverridePanel.h"

#include "HapbeatConfig.h"
#include "HapbeatSubsystem.h"
#include "HapbeatTargetLibrary.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SHapbeatAddressOverridePanel"

namespace
{
	/**
	 * The address the status line resolves for illustration. Carries all three
	 * slots so the wearer can see exactly which one their edit lands in.
	 */
	const TCHAR* PreviewTarget = TEXT("player_1/pos_chest/group_1");

	/** Event the Test button fires when the component names none. */
	const TCHAR* DefaultTestEventId = TEXT("sample-kit.sine_100hz");

	/** Marks values that are edited but not yet applied. */
	const FLinearColor PendingColor(1.0f, 0.85f, 0.2f);
}

void SHapbeatAddressOverridePanel::Construct(const FArguments& InArgs)
{
	WeakSubsystem = InArgs._Subsystem;
	bPersistOnApply = InArgs._bPersistOnApply;
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
		.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f))
		.Padding(12.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("Title", "Hapbeat -- Device Address"))
				]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					MakeStepperRow(LOCTEXT("Player", "Player"),
						TAttribute<FText>(this, &SHapbeatAddressOverridePanel::GetPlayerLabel),
						TAttribute<bool>(this, &SHapbeatAddressOverridePanel::IsPlayerEditable),
						[this](int32 Delta) { StepPlayer(Delta); })
				]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
				[
					MakeStepperRow(LOCTEXT("Group", "Group"),
						TAttribute<FText>(this, &SHapbeatAddressOverridePanel::GetGroupLabel),
						TAttribute<bool>(this, &SHapbeatAddressOverridePanel::IsGroupEditable),
						[this](int32 Delta) { StepGroup(Delta); })
				]

			// Both status lines are ALWAYS present -- neither appears or
			// disappears with the state. A line that came and went would change
			// the panel's height and move the buttons under the cursor.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(this, &SHapbeatAddressOverridePanel::GetStatusLabel)
					.ColorAndOpacity(this, &SHapbeatAddressOverridePanel::GetStatusColor)
				]

			// What the NEXT run on this machine would start with, which is not
			// necessarily what is applied now -- showing both is the whole point
			// at an install, where "did this seat's binding actually stick?" is
			// the question being answered.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(this, &SHapbeatAddressOverridePanel::GetSavedLabel)
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
	TAttribute<bool> IsEditable,
	TFunction<void(int32)> OnStep)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(64.0f)
				[
					SNew(STextBlock).Text(Label)
				]
			]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.IsFocusable(false)
				.Text(LOCTEXT("Minus", "-"))
				.IsEnabled(IsEditable)
				.OnClicked_Lambda([OnStep] { OnStep(-1); return FReply::Handled(); })
			]
		// Fixed width: the value swings between "off" and two digits, and a
		// row that resized would shove the +/- buttons around under the cursor.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(72.0f).HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(ValueText)
					.ColorAndOpacity(FSlateColor(PendingColor))
				]
			]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.IsFocusable(false)
				.Text(LOCTEXT("Plus", "+"))
				.IsEnabled(IsEditable)
				.OnClicked_Lambda([OnStep] { OnStep(1); return FReply::Handled(); })
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

FText SHapbeatAddressOverridePanel::GetStatusLabel() const
{
	const FString Resolved = UHapbeatTargetLibrary::ResolveTarget(PreviewTarget, EditingPlayer, EditingGroup);
	return FText::Format(LOCTEXT("StatusFormat", "{0}  ->  {1}"),
		FText::FromString(PreviewTarget), FText::FromString(Resolved));
}

FText SHapbeatAddressOverridePanel::GetSavedLabel() const
{
	int32 SavedPlayer = -1;
	int32 SavedGroup = -1;
	if (!UHapbeatSubsystem::TryGetPersistedAddressOverride(SavedPlayer, SavedGroup))
	{
		return LOCTEXT("SavedNone", "Saved on this device: none");
	}
	// "off" rather than -1, matching the stepper labels, so the two never have
	// to be read as different vocabularies.
	const FText PlayerText = SavedPlayer < 1 ? LOCTEXT("Off", "off") : FText::AsNumber(SavedPlayer);
	const FText GroupText = SavedGroup < 1 ? LOCTEXT("Off", "off") : FText::AsNumber(SavedGroup);
	return FText::Format(LOCTEXT("SavedFormat", "Saved on this device: player={0}  group={1}"),
		PlayerText, GroupText);
}

FSlateColor SHapbeatAddressOverridePanel::GetStatusColor() const
{
	const UHapbeatSubsystem* Subsystem = GetSubsystem();
	const bool bPending = Subsystem != nullptr &&
		(EditingPlayer != Subsystem->GetOverridePlayer() || EditingGroup != Subsystem->GetOverrideGroup());
	// Same colour the value labels use, so "yellow" consistently reads as
	// "edited, not yet applied" everywhere on the panel.
	return bPending ? FSlateColor(PendingColor) : FSlateColor(FLinearColor::White);
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
