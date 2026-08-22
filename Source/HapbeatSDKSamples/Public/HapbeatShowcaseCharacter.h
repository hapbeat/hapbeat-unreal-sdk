// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "HapbeatShowcaseCharacter.generated.h"

class UCameraComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * The Showcase's first-person player. UE counterpart of the Unity Showcase's
 * SimpleFPSController + CameraFollowMount pair (Samples~/Showcase/Scripts/),
 * with the same numbers: 4 m/s walk, mouse look clamped to +-85 degrees of
 * pitch, Tab toggles the cursor, and a "hand mount" that parks a rod / blaster
 * in front of the camera.
 *
 * NO INPUT ASSETS: every binding is made in code against raw keys
 * (InputComponent::BindKey / BindAxisKey), exactly like the Showcase zones do,
 * so this sample needs no Input Mapping Context or Input Action .uasset and
 * runs in a project that has never opened Enhanced Input.
 *
 * MOUSE-LOOK SENSITIVITY -- where the default comes from:
 *   Unity moves 0.2 degrees per mouse pixel (delta_px x _lookSensitivity 2.0
 *   x PixelsToLegacy 0.1, SimpleFPSController.HandleLook).
 *   UE delivers the MouseX / MouseY axis already scaled by 0.07 per pixel
 *   (Engine/Config/BaseInput.ini: AxisConfig MouseX Sensitivity=0.07), and
 *   AddControllerYawInput adds degrees 1:1 (APlayerController::AddYawInput
 *   multiplies by InputYawScale_DEPRECATED, whose class default is 1.0).
 *   So degrees-per-pixel = 0.07 x LookSensitivity, and 0.2 / 0.07 = 2.857
 *   reproduces Unity's feel.
 * CAVEAT: InputYawScale / InputPitchScale are `config` properties and legacy
 * input scales are still on by default (UInputSettings::
 * bEnableLegacyInputScales = true), so a project whose DefaultInput.ini sets
 * them (the old UE4 templates used 2.5 / -2.5) will feel faster, and a
 * negative pitch scale will invert look. Adjust LookSensitivity in that case.
 */
UCLASS(meta = (DisplayName = "Hapbeat Showcase Character"))
class HAPBEATSDKSAMPLES_API AHapbeatShowcaseCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AHapbeatShowcaseCharacter();

	/** Walk speed, cm/s. 400 = Unity's _moveSpeed 4.0 m/s. Written to CharacterMovement->MaxWalkSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Showcase")
	float MoveSpeed = 400.0f;

	/** Degrees of view rotation per unit of mouse axis; see the class comment for the 2.857 default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Showcase")
	float LookSensitivity = 2.857f;

	/** Pitch clamp, degrees up and down. 85 = Unity's Mathf.Clamp(_pitch, -85f, 85f). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Showcase", meta = (ClampMin = "1.0", ClampMax = "89.9"))
	float ViewPitchLimitDegrees = 85.0f;

	/** Camera height above the capsule CENTRE. 72 puts the eye 160 cm above the feet, matching Unity's 1.6 m camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hapbeat|Showcase")
	float EyeHeightAboveCapsuleCenter = 72.0f;

	// ---- hand mount (Unity CameraFollowMount) ----

	/**
	 * Park a mesh in front of the camera (fishing rod, blaster).
	 *
	 * The pose is the CALLER's business, in full -- position, rotation AND
	 * scale. Each zone converts its own Unity CameraFollowMount offset and fits
	 * its own mesh, so there is no shared "default held-item pose" here to get
	 * out of step with them.
	 *
	 * @param InMesh            What to show; null hides the mount (same as UnmountItem).
	 * @param RelativeToCamera  Camera-local pose, scale included.
	 * @param OptionalMaterial  Applied to element 0 when non-null; the mesh's own material otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Showcase")
	void MountItem(UStaticMesh* InMesh, const FTransform& RelativeToCamera, UMaterialInterface* OptionalMaterial = nullptr);

	/** Hide and clear the hand mount. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Showcase")
	void UnmountItem();

	/** The mount component itself, for a zone that wants to drive it directly (rod tip wobble etc.). */
	UFUNCTION(BlueprintPure, Category = "Hapbeat|Showcase")
	UStaticMeshComponent* GetHandMount() const { return HandMount; }

	// ---- view / cursor ----

	/** World transform of the camera -- the muzzle / cast origin a zone fires from. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat|Showcase")
	FTransform GetViewTransform() const;

	/**
	 * Release the mouse for on-screen UI (and stop mouse-look and movement
	 * while released, as Unity does), or capture it again. Called by the
	 * switcher for a zone whose WantsCursorUnlocked() is true, and by Tab.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Showcase")
	void SetCursorUnlocked(bool bUnlocked);

	UFUNCTION(BlueprintPure, Category = "Hapbeat|Showcase")
	bool IsCursorUnlocked() const { return bCursorUnlocked; }

	/**
	 * Move to a zone's spawn pose: position, yaw, pitch back to level, and
	 * velocity cleared. Unity ZoneSwitcher.TeleportPlayer + ResetLook.
	 * WorldSpawn's location is the CAPSULE CENTRE (the caller has already added
	 * the half-height to the zone's feet-height convention).
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Showcase")
	void TeleportToSpawn(const FTransform& WorldSpawn);

	/**
	 * Point the view at a fixed pitch, degrees (negative looks down). Exists for
	 * Scripts/capture_showcase_views.py, which tips the camera down a little in
	 * Z3 and Z5 so the held rod / blaster is in frame; nothing in the sample
	 * calls it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Showcase")
	void SetViewPitchForCapture(float PitchDegrees);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	/** Called once the pawn is possessed and its input is set up -- where the pitch clamp and cursor mode land. */
	virtual void PawnClientRestart() override;

private:
	// One no-argument handler per key: BindKey cannot pass which key fired, so
	// the movement keys become pressed/released pairs. Same explicit style the
	// zones use for their own keys.
	void OnForwardPressed();
	void OnForwardReleased();
	void OnBackPressed();
	void OnBackReleased();
	void OnLeftPressed();
	void OnLeftReleased();
	void OnRightPressed();
	void OnRightReleased();
	void OnTabPressed();

	void OnMouseX(float AxisValue);
	void OnMouseY(float AxisValue);

	/** Push ViewPitchLimitDegrees into the camera manager, which is what actually clamps pitch. */
	void ApplyViewPitchLimit();

	APlayerController* GetOwningPlayerController() const;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Showcase")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "Hapbeat|Showcase")
	TObjectPtr<UStaticMeshComponent> HandMount;

	// Movement keys are held, not tapped, so pressed/released events set flags
	// and Tick turns them into a movement input -- a key held across a zone
	// switch keeps working, and diagonal input stays normalised.
	bool bForwardHeld = false;
	bool bBackHeld = false;
	bool bLeftHeld = false;
	bool bRightHeld = false;

	bool bCursorUnlocked = false;
};
