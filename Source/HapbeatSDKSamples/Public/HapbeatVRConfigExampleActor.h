// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h" // FKey / EKeys::* for the configurable toggle key
#include "HapbeatVRConfigExampleActor.generated.h"

class UHapbeatAddressOverridePanelComponent;
class UInputAction;
class UWidgetComponent;
struct FInputActionValue;

/**
 * VR config panel demo: pick which Hapbeat this build talks to from inside the
 * running app, without a keyboard.
 *
 * The problem it solves is the one UHapbeatAddressOverridePanelComponent exists
 * for -- at an install or a demo booth the same build runs on several headsets
 * and each has to address its own device -- with the VR half filled in. A
 * viewport overlay is invisible in a headset, so the panel is put on a
 * world-space UWidgetComponent and, by default, kept in front of the wearer.
 *
 * The follow behaviour is a plain head-lock: the surface is placed
 * FollowDistance in front of the camera each frame, interpolated so it settles
 * rather than snapping. The Unity SDK arrived at the same answer the long way
 * around -- it tried an XR composition layer first and ended up on a lazy-follow
 * head-lock -- so this starts there.
 *
	 * Usage: run Scripts/generate_vr_config_input_assets.py once for a project,
	 * then open the shipped VRConfigExample map in VR Preview. The panel follows
	 * the HMD. Tilt either controller stick to move the yellow selection cursor;
	 * pull either trigger to activate it. Stick click recentres the panel. P / R
	 * remain desktop fallbacks for showing and recentring it.
 *
	 * The sample uses OpenXR's project-level Enhanced Input mapping context, not a
	 * vendor SDK and not a controller ray. Its explicit focus grid operates the
	 * same Slate address panel as desktop and Showcase, so there is no duplicate
	 * VR-only address-setting implementation to drift out of sync.
 */
UCLASS()
class HAPBEATSDKSAMPLES_API AHapbeatVRConfigExampleActor : public AActor
{
	GENERATED_BODY()

public:
	AHapbeatVRConfigExampleActor();

	/**
	 * Put the panel on the world-space surface (VR). Off = the ordinary viewport
	 * overlay via Show().
	 *
	 * Both are kept because they are useful at different times: world-space is
	 * the only one a headset can display, but when checking the panel on a
	 * desktop the overlay is the one you can reliably read and click, with no
	 * camera to stand in front of.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat",
		meta = (Tooltip = "On: panel drawn on a world-space surface (VR). Off: viewport overlay (desktop)."))
	bool bWorldSpacePanel = true;

	UPROPERTY(EditAnywhere, Category = "Hapbeat",
		meta = (Tooltip = "Keep the world-space panel in front of the camera. Off = it stays where the actor is placed."))
	bool bFollowCamera = true;

	UPROPERTY(EditAnywhere, Category = "Hapbeat",
		meta = (Tooltip = "How far in front of the camera the panel sits, in cm (1 uu = 1 cm)."))
	float FollowDistance = 190.0f;

	UPROPERTY(EditAnywhere, Category = "Hapbeat",
		meta = (Tooltip = "Interpolation speed for the follow. Higher = tighter to the head; 0 or less = snap instantly."))
	float FollowSpeed = 4.0f;

	/**
	 * Enhanced Input action fired by either controller trigger. The setup
	 * script creates it under /Game because OpenXR only registers Mapping
	 * Contexts from the host project's root content.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|VR Input",
		meta = (Tooltip = "Enhanced Input action used to activate the selected panel control with either controller trigger."))
	TSoftObjectPtr<UInputAction> InteractAction;

	/**
	 * Enhanced Input 2D action driven by either controller thumbstick / trackpad.
	 * A single axis action avoids relying on directional virtual keys, which some
	 * OpenXR runtimes do not publish to an Enhanced Input mapping context.
	 */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|VR Input", meta = (Tooltip = "Enhanced Input 2D action used to move the address-panel selection cursor with either controller stick or trackpad."))
	TSoftObjectPtr<UInputAction> NavigateAction;

	/** Enhanced Input action fired by either controller stick / trackpad click. */
	UPROPERTY(EditAnywhere, Category = "Hapbeat|VR Input",
		meta = (Tooltip = "Enhanced Input action used to recenter the panel with either controller stick or trackpad click."))
	TSoftObjectPtr<UInputAction> RecenterAction;

	UPROPERTY(EditAnywhere, Category = "Hapbeat|VR Input", meta = (ClampMin = "0.05", Tooltip = "Seconds between repeated selection moves while a stick direction stays held."))
	float MoveRepeatIntervalSeconds = 0.4f;

	UPROPERTY(EditAnywhere, Category = "Hapbeat",
		meta = (Tooltip = "Key that hides / shows the panel."))
	FKey ToggleKey = EKeys::P;

	UPROPERTY(EditAnywhere, Category = "Hapbeat",
		meta = (Tooltip = "Desktop fallback key that snaps the panel back in front of the camera."))
	FKey RecenterKey = EKeys::R;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Binds desktop fallback keys and the project's OpenXR Enhanced Input actions. */
	void BindInput();

	void HandleToggleKey();
	void HandleRecenterKey();
	void HandleActivate();
	void HandleNavigate(const FInputActionValue& Value);
	void HandleNavigateReleased(const FInputActionValue& Value);
	void BeginMove(FIntPoint Direction);
	void EndMove();
	void RepeatMove(float DeltaSeconds);

	/** Move/aim PanelSurface to sit in front of the camera this frame. No-op on frames with no camera. */
	void UpdateFollow(float DeltaSeconds);
	/** Put PanelSurface at the current camera-front pose immediately, without follow smoothing. */
	void RecenterPanel();

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<USceneComponent> Root;

	/** The world-space surface the Slate panel is drawn on. Unused when bWorldSpacePanel is off. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UWidgetComponent> PanelSurface;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatAddressOverridePanelComponent> PanelComponent;

	FIntPoint ActiveMoveDirection = FIntPoint::ZeroValue;
	float MoveRepeatTimer = 0.0f;
};
