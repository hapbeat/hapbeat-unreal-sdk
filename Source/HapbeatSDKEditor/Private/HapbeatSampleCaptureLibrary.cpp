// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatSampleCaptureLibrary.h"

#include "Editor.h"                 // GEditor
#include "IAssetViewport.h"
#include "LevelEditor.h"            // FLevelEditorModule::GetFirstActiveViewport
#include "Modules/ModuleManager.h"
#include "PlayInEditorDataTypes.h"  // FRequestPlaySessionParams

DEFINE_LOG_CATEGORY_STATIC(LogHapbeatSampleCapture, Log, All);

bool UHapbeatSampleCaptureLibrary::StartPlayInEditor()
{
	if (GEditor == nullptr)
	{
		UE_LOG(LogHapbeatSampleCapture, Warning,
			TEXT("StartPlayInEditor: no GEditor -- this only works inside a running editor."));
		return false;
	}
	if (GEditor->IsPlaySessionInProgress())
	{
		return true; // already playing; nothing to request
	}

	FRequestPlaySessionParams Params;
	// Defaults are InProcess + PlayInEditor, i.e. an ordinary Play, which is what
	// separates this from ULevelEditorSubsystem::EditorPlaySimulate.

	// Play inside the level viewport rather than in a new floating window, so the
	// screenshots come out at the viewport's size and no window has to be found.
	FLevelEditorModule& LevelEditorModule =
		FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	const TSharedPtr<IAssetViewport> ActiveViewport = LevelEditorModule.GetFirstActiveViewport();
	if (!ActiveViewport.IsValid())
	{
		UE_LOG(LogHapbeatSampleCapture, Warning,
			TEXT("StartPlayInEditor: no active level viewport to play in."));
		return false;
	}
	Params.DestinationSlateViewport = ActiveViewport;

	// Queued, not started: the session comes up on a later editor tick, which is
	// why the caller has to poll IsPlayingInEditor().
	GEditor->RequestPlaySession(Params);
	return true;
}

void UHapbeatSampleCaptureLibrary::EndPlayInEditor()
{
	if (GEditor != nullptr && GEditor->IsPlaySessionInProgress())
	{
		GEditor->RequestEndPlayMap();
	}
}

bool UHapbeatSampleCaptureLibrary::IsPlayingInEditor()
{
	// IsPlayingSessionInEditor, not IsPlaySessionInProgress: the latter is
	// already true for a session that is merely QUEUED, which would let a caller
	// go looking for a game world that does not exist yet.
	return GEditor != nullptr && GEditor->IsPlayingSessionInEditor();
}
