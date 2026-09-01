// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UHapbeatSubsystem;

/**
 * Editor-only companion to UHapbeatConfig. Project-wide connection settings
 * remain in Project Settings; this tab makes the per-machine Address Override
 * visible and editable before PIE, and reports the live value during PIE.
 */
class SHapbeatRuntimeStatusWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHapbeatRuntimeStatusWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static const FName TabId;
	static void RegisterTabSpawner();
	static void UnregisterTabSpawner();

private:
	UHapbeatSubsystem* GetPieSubsystem() const;
	FText GetConnectionText() const;
	FText GetSavedOverrideText() const;
	FText GetBuildPinText() const;
	FText GetLiveOverrideText() const;
	FText GetOverrideHelpText() const;

	void OnPlayerChanged(int32 NewValue);
	void OnGroupChanged(int32 NewValue);
	FReply OnSaveClicked();
	FReply OnClearClicked();
	FReply OnOpenProjectSettingsClicked();

	int32 EditingPlayer = -1;
	int32 EditingGroup = -1;
};
