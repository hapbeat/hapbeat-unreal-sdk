// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HapbeatAddressOverridePanelComponent.generated.h"

class SHapbeatAddressOverridePanel;
class UWidgetComponent;

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
 * panel is a plain widget and the grid disappears.
 *
 * Two ways to put it on screen. Show() adds it to the viewport, which is what a
 * desktop build wants. In VR there is no viewport to overlay -- the panel has to
 * be a surface in the world -- so AttachToWidgetComponent() hands the same
 * widget to a UWidgetComponent instead. Deliberately not ported: Unity's
 * LazyFollow anchoring. Where a world-space panel should sit, and whether it
 * trails the head, is a property of the rig rather than of the panel; the VR
 * sample (AHapbeatVRConfigExampleActor) owns that.
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

	/**
	 * Render the panel on a world-space UWidgetComponent instead of the viewport.
	 *
	 * VR has no viewport overlay to add to: whatever the headset shows is the
	 * stereo scene, so a panel that must be readable in a headset has to exist as
	 * geometry in the world. UWidgetComponent::SetSlateWidget takes a raw Slate
	 * widget, so the same SHapbeatAddressOverridePanel goes straight onto that
	 * surface -- there is no need to re-author the panel as a UMG UUserWidget
	 * just to change where it is drawn.
	 *
	 * Replaces whatever is currently shown (viewport or another surface). Pass a
	 * component that is set to EWidgetSpace::World; nullptr is a logged no-op.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void AttachToWidgetComponent(UWidgetComponent* Target);

	/** Take the panel back down, from wherever Show() / AttachToWidgetComponent() put it. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void Hide();

	/**
	 * Show if hidden, hide if shown. Bind this to a button for a one-key panel.
	 * Note this always shows via Show() (viewport): a caller that put the panel
	 * on a UWidgetComponent should toggle that component's visibility instead,
	 * so the surface keeps its place in the world.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat")
	void Toggle();

	/** True while the panel is live, in either mode -- both keep PanelWidget set. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat")
	bool IsShown() const { return PanelWidget.IsValid(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/**
	 * Build the panel widget with this component's settings. Both display paths
	 * go through here so the argument list cannot drift between them -- a
	 * difference in bPersistOnApply or TestEventId depending on whether you are
	 * in VR would be a silent behaviour change.
	 */
	TSharedRef<SHapbeatAddressOverridePanel> CreatePanel();

	TSharedPtr<SHapbeatAddressOverridePanel> PanelWidget;

	/**
	 * True while the panel lives on AttachedWidgetComponent rather than in the
	 * viewport. Kept separate from the weak pointer below because Hide() must
	 * still know which teardown NOT to run after the target has been destroyed.
	 */
	bool bAttachedToWidgetComponent = false;

	/** The surface the panel was handed to; weak, since the actor owning it can go away first. */
	TWeakObjectPtr<UWidgetComponent> AttachedWidgetComponent;
};
