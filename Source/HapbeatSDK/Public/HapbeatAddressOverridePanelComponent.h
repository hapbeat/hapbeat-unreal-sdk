// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HapbeatAddressOverridePanelComponent.generated.h"

class SHapbeatAddressOverridePanel;

/**
 * In-game panel for choosing which Hapbeat this build talks to.
 *
 * At an install, a demo booth, or any multi-seat session, the same build runs
 * on several machines and each has to address its own device. Editing a config
 * file per machine does not survive contact with a live event, so the choice
 * has to be reachable from inside the running app -- including from a headset,
 * where there is no keyboard.
 *
 * Port of Hapbeat.HapbeatAddressOverridePanel (Unity SDK). The Unity version
 * hand-builds a uGUI canvas plus its own 2D focus-navigation grid; this uses
 * Slate, whose focus navigation already handles keyboard and gamepad, so the
 * panel is a plain widget and the grid disappears. Deliberately not ported:
 * Unity's world-space LazyFollow anchoring, which exists to serve the VR
 * sample rigs that are outside this SDK's agreed sample scope. Use
 * AttachToWidgetComponent() for a world-space (VR) panel.
 */
UCLASS(ClassGroup = (Hapbeat), meta = (BlueprintSpawnableComponent, DisplayName = "Hapbeat Address Override Panel"))
class HAPBEATSDK_API UHapbeatAddressOverridePanelComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHapbeatAddressOverridePanelComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat",
		meta = (Tooltip = "Show the panel as soon as play begins. Off = call Show() yourself (a pause menu, a debug key)."))
	bool bShowOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat",
		meta = (Tooltip = "Persist the chosen player / group so the next run on this machine starts with it."))
	bool bPersistOnApply = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat",
		meta = (Tooltip = "Event id fired by the panel's Test button. Leave empty to use the SDK's standard sample event."))
	FString TestEventId;

	/** Add the panel to the viewport. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void Show();

	/** Remove the panel from the viewport. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void Hide();

	/** Show if hidden, hide if shown. Bind this to a button for a one-key panel. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void Toggle();

	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	bool IsShown() const { return PanelWidget.IsValid(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	TSharedPtr<SHapbeatAddressOverridePanel> PanelWidget;
};
