// Copyright (c) 2026 Hapbeat. MIT License.
#include "SHapbeatRuntimeStatusWindow.h"

#include "HapbeatConfig.h"
#include "HapbeatSubsystem.h"

#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Docking/TabManager.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HapbeatRuntimeStatus"

const FName SHapbeatRuntimeStatusWindow::TabId(TEXT("HapbeatRuntimeStatus"));

void SHapbeatRuntimeStatusWindow::RegisterTabSpawner()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
		{
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab)
				[
					SNew(SHapbeatRuntimeStatusWindow)
				];
		}))
		.SetDisplayName(LOCTEXT("TabTitle", "Hapbeat Runtime Status"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void SHapbeatRuntimeStatusWindow::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

void SHapbeatRuntimeStatusWindow::Construct(const FArguments& InArgs)
{
	UHapbeatSubsystem::TryGetPersistedAddressOverride(EditingPlayer, EditingGroup);

	ChildSlot
	[
		SNew(SBorder)
		.Padding(16.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ConnectionTitle", "Connection"))
				.Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 12.0f)
			[
				SNew(STextBlock).Text(this, &SHapbeatRuntimeStatusWindow::GetConnectionText)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.Text(LOCTEXT("ProjectSettings", "Open Project Settings"))
				.ToolTipText(LOCTEXT("ProjectSettingsTooltip", "Open UDP port, app name, unicast, timing, and build-pinned addressing settings."))
				.OnClicked(this, &SHapbeatRuntimeStatusWindow::OnOpenProjectSettingsClicked)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 12.0f)
			[
				SNew(SSeparator)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OverrideTitle", "Address Override (this machine)"))
				.Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(STextBlock).Text(this, &SHapbeatRuntimeStatusWindow::GetSavedOverrideText)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock).Text(this, &SHapbeatRuntimeStatusWindow::GetBuildPinText)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("Player", "Player")).MinDesiredWidth(48.0f)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 18.0f, 0.0f)
				[
					SNew(SSpinBox<int32>)
					.MinValue(-1).MaxValue(99).Value_Lambda([this] { return EditingPlayer; })
					.OnValueChanged(this, &SHapbeatRuntimeStatusWindow::OnPlayerChanged)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("Group", "Group")).MinDesiredWidth(48.0f)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SSpinBox<int32>)
					.MinValue(-1).MaxValue(99).Value_Lambda([this] { return EditingGroup; })
					.OnValueChanged(this, &SHapbeatRuntimeStatusWindow::OnGroupChanged)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock).Text(this, &SHapbeatRuntimeStatusWindow::GetOverrideHelpText)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton).Text(LOCTEXT("Save", "Save to This Machine"))
					.OnClicked(this, &SHapbeatRuntimeStatusWindow::OnSaveClicked)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(LOCTEXT("Clear", "Clear Saved Override"))
					.OnClicked(this, &SHapbeatRuntimeStatusWindow::OnClearClicked)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).Text(this, &SHapbeatRuntimeStatusWindow::GetLiveOverrideText)
			]
		]
	];
}

UHapbeatSubsystem* SHapbeatRuntimeStatusWindow::GetPieSubsystem() const
{
	if (GEditor == nullptr || GEditor->PlayWorld == nullptr)
	{
		return nullptr;
	}
	if (UGameInstance* GameInstance = GEditor->PlayWorld->GetGameInstance())
	{
		return GameInstance->GetSubsystem<UHapbeatSubsystem>();
	}
	return nullptr;
}

FText SHapbeatRuntimeStatusWindow::GetConnectionText() const
{
	const UHapbeatConfig* Config = GetDefault<UHapbeatConfig>();
	const int32 Port = Config != nullptr ? Config->Port : 7700;
	const bool bUnicast = Config == nullptr || Config->bCommandUnicast;
	return FText::Format(LOCTEXT("ConnectionValue", "UDP port: {0}    Command unicast: {1}"),
		FText::AsNumber(Port), bUnicast ? LOCTEXT("On", "On") : LOCTEXT("Off", "Off"));
}

FText SHapbeatRuntimeStatusWindow::GetSavedOverrideText() const
{
	int32 Player = -1;
	int32 Group = -1;
	const bool bSaved = UHapbeatSubsystem::TryGetPersistedAddressOverride(Player, Group);
	return bSaved
		? FText::Format(LOCTEXT("SavedOverride", "Saved for next launch: Player {0}, Group {1}"), FText::AsNumber(Player), FText::AsNumber(Group))
		: LOCTEXT("NoSavedOverride", "Saved for next launch: none");
}

FText SHapbeatRuntimeStatusWindow::GetBuildPinText() const
{
	const UHapbeatConfig* Config = GetDefault<UHapbeatConfig>();
	const int32 Player = Config != nullptr ? UHapbeatSubsystem::NormalizeAddressOverride(Config->ForcedOverridePlayer) : -1;
	const int32 Group = Config != nullptr ? UHapbeatSubsystem::NormalizeAddressOverride(Config->ForcedOverrideGroup) : -1;
	return FText::Format(LOCTEXT("BuildPin", "Build-pinned: Player {0}, Group {1}"), FText::AsNumber(Player), FText::AsNumber(Group));
}

FText SHapbeatRuntimeStatusWindow::GetLiveOverrideText() const
{
	if (UHapbeatSubsystem* Subsystem = GetPieSubsystem())
	{
		return FText::Format(LOCTEXT("LiveOverride", "PIE effective now: Player {0}, Group {1}    Socket: {2}"),
			FText::AsNumber(Subsystem->GetOverridePlayer()), FText::AsNumber(Subsystem->GetOverrideGroup()),
			Subsystem->IsConnected() ? LOCTEXT("SocketOpen", "open") : LOCTEXT("SocketClosed", "closed"));
	}
	return LOCTEXT("NoPie", "PIE is not running. Save prepares the next PIE or packaged launch.");
}

FText SHapbeatRuntimeStatusWindow::GetOverrideHelpText() const
{
	return LOCTEXT("OverrideHelp", "Use -1 to leave an axis unchanged. A build-pinned axis cannot be changed here. During PIE, Save also applies the value immediately.");
}

void SHapbeatRuntimeStatusWindow::OnPlayerChanged(int32 NewValue)
{
	EditingPlayer = NewValue;
}

void SHapbeatRuntimeStatusWindow::OnGroupChanged(int32 NewValue)
{
	EditingGroup = NewValue;
}

FReply SHapbeatRuntimeStatusWindow::OnSaveClicked()
{
	if (UHapbeatSubsystem* Subsystem = GetPieSubsystem())
	{
		Subsystem->SetAddressOverride(EditingPlayer, EditingGroup, /*bPersist=*/true);
	}
	else
	{
		UHapbeatSubsystem::SavePersistedAddressOverride(EditingPlayer, EditingGroup);
	}
	UHapbeatSubsystem::TryGetPersistedAddressOverride(EditingPlayer, EditingGroup);
	return FReply::Handled();
}

FReply SHapbeatRuntimeStatusWindow::OnClearClicked()
{
	if (UHapbeatSubsystem* Subsystem = GetPieSubsystem())
	{
		Subsystem->ClearPersistedAddressOverride();
	}
	else
	{
		UHapbeatSubsystem::RemovePersistedAddressOverride();
	}
	EditingPlayer = -1;
	EditingGroup = -1;
	return FReply::Handled();
}

FReply SHapbeatRuntimeStatusWindow::OnOpenProjectSettingsClicked()
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Plugins", "Hapbeat");
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
