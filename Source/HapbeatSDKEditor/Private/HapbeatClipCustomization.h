// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UHapbeatClip;

/**
 * Details customization for UHapbeatClip: adds an "Import WAV..." button that
 * fills the asset from a 16 kHz PCM16 .wav on disk.
 *
 * Why a button and not a UFactory: `.wav` is already claimed by the engine's
 * USoundFactory (which produces USoundWave). Registering a second importer for
 * the same extension makes the import path ambiguous for every WAV in the
 * project, which is a far worse trade than one explicit button on the asset
 * that needs it. The flow is therefore:
 *   Content Browser -> Miscellaneous -> Data Asset -> Hapbeat Clip
 *   -> open it -> Import WAV... -> pick the file
 *
 * Without this, a UHapbeatClip asset can only ever be created in code
 * (UHapbeatClip::CreateFromWavBytes), which means the StreamClip half of the
 * EventMap authoring flow has no GUI path at all.
 */
class FHapbeatClipCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FReply OnImportWavClicked();
	FText GetStatusText() const;

	/** The asset being edited (single-select only; multi-select falls back to default rows). */
	TWeakObjectPtr<UHapbeatClip> TargetClip;

	/** Result of the last import in this panel, shown next to the button. */
	FText StatusText;
};
