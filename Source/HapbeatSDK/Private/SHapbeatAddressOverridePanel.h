// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UHapbeatSubsystem;

/**
 * The address-override panel's UI. See UHapbeatAddressOverridePanelComponent
 * for why it exists; this half only draws and edits.
 *
 * Edits are staged rather than applied live: the wearer steps Player / Group to
 * the values they want, sees where those would resolve to, and only then
 * commits. Applying on every step would retarget the device mid-adjustment and
 * fire haptics at whoever happened to be on the number passed through.
 */
class SHapbeatAddressOverridePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHapbeatAddressOverridePanel)
		: _Subsystem(nullptr)
		, _bPersistOnApply(true)
		, _bShowCloseButton(true)
	{}
		SLATE_ARGUMENT(UHapbeatSubsystem*, Subsystem)
		SLATE_ARGUMENT(bool, bPersistOnApply)
		/** Whether this host needs a button to dismiss the panel itself. */
		SLATE_ARGUMENT(bool, bShowCloseButton)
		SLATE_ARGUMENT(FString, TestEventId)
		/** Invoked by the Close button so the owner can drop the widget. */
		SLATE_EVENT(FSimpleDelegate, OnCloseRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// ---- staged edits ----
	void StepPlayer(int32 Delta);
	void StepGroup(int32 Delta);
	FReply OnApplyClicked();
	FReply OnClearClicked();
	FReply OnTestClicked();
	FReply OnCloseClicked();

	/** -1 wraps to "off"; the wire vocabulary is 1..99 (device-addressing spec). */
	static int32 Step(int32 Value, int32 Delta);

	// ---- display ----
	FText GetPlayerLabel() const;
	FText GetGroupLabel() const;
	FText GetResolvedTargetLabel() const;
	/** Persisted values for the next run, displayed separately to emphasize the address itself. */
	FText GetSavedPlayerLabel() const;
	FText GetSavedGroupLabel() const;
	FSlateColor GetStatusColor() const;
	bool IsPlayerEditable() const;
	bool IsGroupEditable() const;

	UHapbeatSubsystem* GetSubsystem() const;

	/** Row builder, so the two stepper rows cannot drift apart. */
	TSharedRef<SWidget> MakeStepperRow(
		const FText& Label,
		TAttribute<FText> ValueText,
		TAttribute<bool> IsEditable,
		TFunction<void(int32)> OnStep);

	TWeakObjectPtr<UHapbeatSubsystem> WeakSubsystem;
	bool bPersistOnApply = true;
	bool bShowCloseButton = true;
	FString TestEventId;
	FSimpleDelegate OnCloseRequested;

	int32 EditingPlayer = -1;
	int32 EditingGroup = -1;
};
