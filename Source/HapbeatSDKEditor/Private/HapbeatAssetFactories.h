// Copyright (c) 2026 Hapbeat. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "AssetTypeCategories.h"
#include "Factories/Factory.h"

#include "HapbeatAssetFactories.generated.h"

/**
 * Content Browser factories for the plugin's two authored assets.
 *
 * Without these, the only way to create a UHapbeatEventMap / UHapbeatClip is
 * the engine's generic "Miscellaneous > Data Asset" entry, which does NOT list
 * them in the menu at all: UDataAssetFactory::ConfigureProperties opens a
 * separate modal ("Pick Class For Data Asset Instance") and the class is
 * chosen in there. Nothing named "Hapbeat" appears in the right-click menu,
 * so first-time users cannot find these assets by browsing.
 *
 * Each factory below gives its asset a first-class entry under a "Hapbeat"
 * category, making creation a single click with no class picker. The generic
 * Data Asset route still works -- this only adds a shorter path.
 *
 * Menu labels are overridden explicitly. UFactory::GetDisplayName() asks
 * AssetTools for the supported class's registered type first, and only falls
 * back to the class name when nothing is registered -- so leaving it to the
 * default made both entries read "Data Asset", inherited from UDataAsset.
 * HapbeatAssetDefinitions.h now registers proper identities, which would fix
 * this indirectly, but the label is stated here as well so a menu entry can
 * never be at the mercy of when that registration happens to run.
 */

namespace HapbeatEditor
{
	/**
	 * Category the factories advertise from GetMenuCategories().
	 *
	 * IAssetTools hands out category bits at runtime, so the value cannot be a
	 * compile-time constant: the editor module registers "Hapbeat" on startup
	 * and stores the result here. Factories are constructed by the engine (CDO
	 * creation) rather than by us, hence a module-level value instead of a
	 * member. Defaults to Misc so the entries are still reachable if
	 * registration ever fails to run before the menu is built.
	 */
	EAssetTypeCategories::Type GetAssetCategory();
	void SetAssetCategory(EAssetTypeCategories::Type InCategory);
}

/** Creates a UHapbeatEventMap from the Content Browser's Hapbeat category. */
UCLASS()
class UHapbeatEventMapFactory : public UFactory
{
	GENERATED_BODY()

public:
	UHapbeatEventMapFactory();

	//~ Begin UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual uint32 GetMenuCategories() const override;
	virtual FText GetDisplayName() const override;
	//~ End UFactory interface
};

/** Creates a UHapbeatClip from the Content Browser's Hapbeat category. */
UCLASS()
class UHapbeatClipFactory : public UFactory
{
	GENERATED_BODY()

public:
	UHapbeatClipFactory();

	//~ Begin UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual uint32 GetMenuCategories() const override;
	virtual FText GetDisplayName() const override;
	//~ End UFactory interface
};
