// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"

#include "HapbeatClip.h"
#include "HapbeatEventMap.h"
#include "SHapbeatEventMapWindow.h"

#include "HapbeatAssetDefinitions.generated.h"

/**
 * Asset identity (display name, color) for the plugin's two authored assets.
 *
 * Both assets derive from UDataAsset, and the editor resolves an asset's
 * identity by walking UP the class hierarchy until it finds a registration
 * (UAssetToolsImpl::GetAssetTypeActionsForClass). Without the definitions
 * below, that walk reaches UDataAsset and everything Hapbeat-shaped is
 * labelled "Data Asset" -- both entries in the Content Browser's Hapbeat
 * category came out identically named, and the Type column was equally
 * useless. Registering here fixes the name everywhere at once, because
 * UFactory::GetDisplayName() consults the same lookup.
 */

/** Identity for UHapbeatEventMap. */
UCLASS()
class UAssetDefinition_HapbeatEventMap : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	//~ Begin UAssetDefinition interface
	virtual FText GetAssetDisplayName() const override
	{
		return NSLOCTEXT("HapbeatSDKEditor", "AssetDisplayName_HapbeatEventMap", "Hapbeat Event Map");
	}

	// Arbitrary but distinct from the clip below, so the two are told apart at a
	// glance in the Content Browser.
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(126, 87, 194)); }

	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UHapbeatEventMap::StaticClass(); }

	/**
	 * Double-click opens the Hapbeat Event Map window rather than the engine's
	 * generic Details-panel asset editor.
	 *
	 * Those two look nothing alike, and the generic one is what opened before --
	 * so someone told "the Event Map window is where you edit this" would
	 * double-click the asset, get the old panel, and reasonably conclude the new
	 * window had not taken effect. It did happen.
	 */
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override
	{
		for (UHapbeatEventMap* Map : OpenArgs.LoadObjects<UHapbeatEventMap>())
		{
			SHapbeatEventMapWindow::OpenForAsset(Map);
		}
		return EAssetCommandResult::Handled;
	}
	//~ End UAssetDefinition interface
};

/** Identity for UHapbeatClip. */
UCLASS()
class UAssetDefinition_HapbeatClip : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	//~ Begin UAssetDefinition interface
	virtual FText GetAssetDisplayName() const override
	{
		return NSLOCTEXT("HapbeatSDKEditor", "AssetDisplayName_HapbeatClip", "Hapbeat Clip");
	}

	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(38, 166, 191)); }

	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UHapbeatClip::StaticClass(); }
	//~ End UAssetDefinition interface
};
