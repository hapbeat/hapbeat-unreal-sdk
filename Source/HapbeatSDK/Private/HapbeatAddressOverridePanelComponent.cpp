// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatAddressOverridePanelComponent.h"

#include "HapbeatSubsystem.h"
#include "SHapbeatAddressOverridePanel.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

UHapbeatAddressOverridePanelComponent::UHapbeatAddressOverridePanelComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHapbeatAddressOverridePanelComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bShowOnBeginPlay)
	{
		Show();
	}
}

void UHapbeatAddressOverridePanelComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Hide();
	Super::EndPlay(EndPlayReason);
}

void UHapbeatAddressOverridePanelComponent::Show()
{
	if (PanelWidget.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	UGameViewportClient* Viewport = World != nullptr ? World->GetGameViewport() : nullptr;
	if (GameInstance == nullptr || Viewport == nullptr)
	{
		return;
	}

	UHapbeatSubsystem* Subsystem = GameInstance->GetSubsystem<UHapbeatSubsystem>();

	TWeakObjectPtr<UHapbeatAddressOverridePanelComponent> WeakThis(this);
	PanelWidget = SNew(SHapbeatAddressOverridePanel)
		.Subsystem(Subsystem)
		.bPersistOnApply(bPersistOnApply)
		.TestEventId(TestEventId)
		.OnCloseRequested(FSimpleDelegate::CreateLambda([WeakThis]
		{
			if (UHapbeatAddressOverridePanelComponent* Self = WeakThis.Get())
			{
				Self->Hide();
			}
		}));

	// ZOrder above ordinary HUD content: the panel is a modal-ish chooser, and
	// half-covering it with gameplay UI would make it unusable at an install.
	Viewport->AddViewportWidgetContent(PanelWidget.ToSharedRef(), /*ZOrder=*/100);
}

void UHapbeatAddressOverridePanelComponent::Hide()
{
	if (!PanelWidget.IsValid())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (UGameViewportClient* Viewport = World != nullptr ? World->GetGameViewport() : nullptr)
	{
		Viewport->RemoveViewportWidgetContent(PanelWidget.ToSharedRef());
	}
	PanelWidget.Reset();
}

void UHapbeatAddressOverridePanelComponent::Toggle()
{
	if (PanelWidget.IsValid())
	{
		Hide();
	}
	else
	{
		Show();
	}
}
