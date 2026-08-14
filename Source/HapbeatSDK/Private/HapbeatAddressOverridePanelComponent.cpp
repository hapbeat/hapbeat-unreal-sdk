// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatAddressOverridePanelComponent.h"

#include "HapbeatSubsystem.h"
#include "SHapbeatAddressOverridePanel.h"

#include "Components/WidgetComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatAddressPanel, Log, All);

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

TSharedRef<SHapbeatAddressOverridePanel> UHapbeatAddressOverridePanelComponent::CreatePanel()
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	// A null subsystem is tolerated by the widget (it holds a weak pointer and
	// draws a "no subsystem" status), so this does not need to be a hard failure.
	UHapbeatSubsystem* Subsystem = GameInstance != nullptr ? GameInstance->GetSubsystem<UHapbeatSubsystem>() : nullptr;

	TWeakObjectPtr<UHapbeatAddressOverridePanelComponent> WeakThis(this);
	return SNew(SHapbeatAddressOverridePanel)
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
}

void UHapbeatAddressOverridePanelComponent::Show()
{
	if (PanelWidget.IsValid())
	{
		return;
	}

	const UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World != nullptr ? World->GetGameViewport() : nullptr;
	if (Viewport == nullptr)
	{
		return;
	}

	PanelWidget = CreatePanel();
	bAttachedToWidgetComponent = false;

	// ZOrder above ordinary HUD content: the panel is a modal-ish chooser, and
	// half-covering it with gameplay UI would make it unusable at an install.
	Viewport->AddViewportWidgetContent(PanelWidget.ToSharedRef(), /*ZOrder=*/100);
}

void UHapbeatAddressOverridePanelComponent::AttachToWidgetComponent(UWidgetComponent* Target)
{
	if (Target == nullptr)
	{
		UE_LOG(LogHapbeatAddressPanel, Warning,
			TEXT("AttachToWidgetComponent called with no target; the address-override panel was not shown."));
		return;
	}

	// Take the panel down from wherever it currently is first, so the old host
	// (viewport, or a previous surface) does not keep drawing an orphaned copy.
	Hide();

	PanelWidget = CreatePanel();
	bAttachedToWidgetComponent = true;
	AttachedWidgetComponent = Target;
	Target->SetSlateWidget(PanelWidget);
}

void UHapbeatAddressOverridePanelComponent::Hide()
{
	if (!PanelWidget.IsValid())
	{
		return;
	}

	if (bAttachedToWidgetComponent)
	{
		// The surface may already be gone (its actor destroyed before this
		// component tore down), in which case there is nothing left to clear --
		// but the mode flag still had to be right to get here, which is why it
		// is tracked separately from the weak pointer.
		if (UWidgetComponent* Surface = AttachedWidgetComponent.Get())
		{
			Surface->SetSlateWidget(nullptr);
		}
		AttachedWidgetComponent.Reset();
		bAttachedToWidgetComponent = false;
	}
	else
	{
		const UWorld* World = GetWorld();
		if (UGameViewportClient* Viewport = World != nullptr ? World->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(PanelWidget.ToSharedRef());
		}
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
