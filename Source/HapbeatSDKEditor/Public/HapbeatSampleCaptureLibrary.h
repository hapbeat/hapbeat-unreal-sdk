// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HapbeatSampleCaptureLibrary.generated.h"

/**
 * Start and stop Play-In-Editor from script.
 *
 * WHY THIS EXISTS: UE exposes no Python entry point for PIE.
 * ULevelEditorSubsystem offers EditorPlaySimulate() (Simulate-In-Editor, which
 * spawns no player pawn and so cannot show the Showcase's first-person view),
 * EditorRequestEndPlay() and IsInPlayInEditor(), but the start of a real play
 * session is UEditorEngine::RequestPlaySession, which is C++ only. Two thin
 * BlueprintCallable wrappers around it are what let
 * Scripts/capture_showcase_views.py drive the Showcase unattended.
 *
 * Editor-only by construction: this class lives in the editor module and calls
 * GEditor, so it is not part of anything a packaged game can reach.
 *
 * It is a developer tool, not part of the SDK's API -- nothing in the plugin
 * calls it, and a project has no reason to.
 */
UCLASS()
class UHapbeatSampleCaptureLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Request a Play-In-Editor session in the active level viewport. Returns
	 * false when there is no editor or no viewport to play in.
	 *
	 * ASYNCHRONOUS: UEditorEngine::RequestPlaySession only queues the request;
	 * the session starts on a later editor tick. Poll IsPlayingInEditor() (or
	 * wait for a game world to appear) before doing anything with it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Samples|Capture")
	static bool StartPlayInEditor();

	/** Ask the editor to end the current PIE session. The EDITOR keeps running. */
	UFUNCTION(BlueprintCallable, Category = "Hapbeat|Samples|Capture")
	static void EndPlayInEditor();

	/** True while a PIE session is up. */
	UFUNCTION(BlueprintPure, Category = "Hapbeat|Samples|Capture")
	static bool IsPlayingInEditor();
};
