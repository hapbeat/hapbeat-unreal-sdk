// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HapbeatAddressOverridePanelComponent.generated.h"

class SHapbeatAddressOverridePanel;
class SWidget;
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
		meta = (Tooltip = "Show a Close button. Disable this for a panel that is owned by a persistent in-game HUD."))
	bool bShowCloseButton = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat",
		meta = (Tooltip = "Event id fired by the panel's Test button. Leave empty to use the SDK's standard sample event."))
	FString TestEventId;

	// ---- Viewport placement (Show() only) ----
	//
	// A viewport widget is stretched over the WHOLE screen unless something
	// constrains it, which is right for a config screen the app opens on its own
	// and wrong for a panel that shares the screen with gameplay UI: it covers
	// the rest of the interface and, worse, swallows the clicks meant for it.
	// These four say where the panel sits and how big it is; the area around it
	// is left click-through.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Placement",
		meta = (Tooltip = "Horizontal placement in the viewport."))
	TEnumAsByte<EHorizontalAlignment> ViewportHAlign = HAlign_Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Placement",
		meta = (Tooltip = "Vertical placement in the viewport."))
	TEnumAsByte<EVerticalAlignment> ViewportVAlign = VAlign_Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Placement",
		meta = (Tooltip = "Screen-edge padding, in pixels, applied to the placement above."))
	FMargin ViewportPadding = FMargin(0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Placement",
		meta = (Tooltip = "Panel size in pixels. X = fixed width (0 = fit the content). Y = MINIMUM height (the panel still grows if its content needs more, so a value that is too small cannot clip the buttons)."))
	FVector2D ViewportSize = FVector2D::ZeroVector;

	/** Add the panel to the viewport. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Address", meta = (DisplayName = "Show Address Panel (Hapbeat)"))
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
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Attach Address Panel (Hapbeat)"))
	void AttachToWidgetComponent(UWidgetComponent* Target);

	/** Take the panel back down, from wherever Show() / AttachToWidgetComponent() put it. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Address", meta = (DisplayName = "Hide Address Panel (Hapbeat)"))
	void Hide();

	/**
	 * Show if hidden, hide if shown. Bind this to a button for a one-key panel.
	 * Note this always shows via Show() (viewport): a caller that put the panel
	 * on a UWidgetComponent should toggle that component's visibility instead,
	 * so the surface keeps its place in the world.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat", meta = (DisplayName = "Toggle Address Panel (Hapbeat)"))
	void Toggle();

	/** True while the panel is live, in either mode -- both keep PanelWidget set. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat", meta = (DisplayName = "Is Address Panel Shown (Hapbeat)"))
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
	 * The placement wrapper actually added to the viewport (Show() only). It is
	 * what has to be handed back to RemoveViewportWidgetContent, so it is kept
	 * separately from the panel itself.
	 */
	TSharedPtr<SWidget> ViewportContent;

	/**
	 * True while the panel lives on AttachedWidgetComponent rather than in the
	 * viewport. Kept separate from the weak pointer below because Hide() must
	 * still know which teardown NOT to run after the target has been destroyed.
	 */
	bool bAttachedToWidgetComponent = false;

	/** The surface the panel was handed to; weak, since the actor owning it can go away first. */
	TWeakObjectPtr<UWidgetComponent> AttachedWidgetComponent;
};
