// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * Editor module (Type = Editor, see HapbeatSDK.uplugin): registers the
 * Hapbeat detail customizations with FPropertyEditorModule on startup and
 * unregisters them (+ closes the editor Test Play socket) on shutdown.
 *
 * No runtime code depends on this module -- it exists purely to give the
 * native Details panel a manifest-intensity "Refresh" action, a per-entry
 * "Test Play" transport, and an EventMap entry picker (see the design doc
 * §3.6). The plugin's runtime module (HapbeatSDK) never references it.
 */
class FHapbeatSDKEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** FEditorDelegates::EndPIE handle, so the editor Test Play socket is closed when a PIE session ends (belt-and-braces; see FHapbeatEditorSender's class doc for why this can't actually collide with the runtime socket). */
	FDelegateHandle EndPieHandle;
};
