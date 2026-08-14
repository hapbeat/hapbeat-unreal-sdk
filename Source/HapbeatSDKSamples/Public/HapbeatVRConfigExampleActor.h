// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h" // FKey / EKeys::* for the configurable toggle key
#include "HapbeatVRConfigExampleActor.generated.h"

class UHapbeatAddressOverridePanelComponent;
class UWidgetComponent;

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
 * Usage: drop this actor into a level and hit Play. Press ToggleKey (P by
 * default) to hide/show the panel. On desktop, set bWorldSpacePanel = false to
 * get the ordinary viewport overlay instead. An on-screen HUD shows device
 * liveness and the override currently in effect.
 *
 * Out of scope for this sample: pointing at the panel with a motion controller
 * (UWidgetInteractionComponent). The panel is navigable with Slate's own
 * keyboard / gamepad focus navigation, which is what the runtime component
 * already relies on.
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
	float FollowDistance = 120.0f;

	UPROPERTY(EditAnywhere, Category = "Hapbeat",
		meta = (Tooltip = "Interpolation speed for the follow. Higher = tighter to the head; 0 or less = snap instantly."))
	float FollowSpeed = 4.0f;

	UPROPERTY(EditAnywhere, Category = "Hapbeat",
		meta = (Tooltip = "Key that hides / shows the panel."))
	FKey ToggleKey = EKeys::P;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** EnableInput on the first PlayerController found, then BindKey ToggleKey. Warns (no-op) if none exists. */
	void BindInput();

	void HandleToggleKey();

	/** Move/aim PanelSurface to sit in front of the camera this frame. No-op on frames with no camera. */
	void UpdateFollow(float DeltaSeconds);

	/** Fixed on-screen-message keys, offset into the 600s so they don't collide with the other samples' HUD lines. */
	static constexpr int32 KeyGuideHudLineKey = 600;
	static constexpr int32 StatusHudLineKey = 601;
	static constexpr float HudRefreshIntervalSeconds = 0.5f;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<USceneComponent> Root;

	/** The world-space surface the Slate panel is drawn on. Unused when bWorldSpacePanel is off. */
	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UWidgetComponent> PanelSurface;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat")
	TObjectPtr<UHapbeatAddressOverridePanelComponent> PanelComponent;

	/** Counts down to 0 to throttle the HUD refresh; fires on the first Tick (starts at 0). */
	float HudRefreshTimer = 0.0f;
};
