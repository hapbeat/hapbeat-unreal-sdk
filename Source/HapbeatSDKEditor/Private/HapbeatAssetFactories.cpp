// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatAssetFactories.h"

#include "HapbeatClip.h"
#include "HapbeatEventMap.h"

namespace HapbeatEditor
{
	static EAssetTypeCategories::Type GAssetCategory = EAssetTypeCategories::Misc;

	EAssetTypeCategories::Type GetAssetCategory()
	{
		return GAssetCategory;
	}

	void SetAssetCategory(EAssetTypeCategories::Type InCategory)
	{
		GAssetCategory = InCategory;
	}
}

UHapbeatEventMapFactory::UHapbeatEventMapFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UHapbeatEventMap::StaticClass();
}

UObject* UHapbeatEventMapFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	// RF_Transactional matches UDataAssetFactory so undo works on the new asset.
	return NewObject<UHapbeatEventMap>(InParent, Class, Name, Flags | RF_Transactional);
}

uint32 UHapbeatEventMapFactory::GetMenuCategories() const
{
	return HapbeatEditor::GetAssetCategory();
}

FText UHapbeatEventMapFactory::GetDisplayName() const
{
	return NSLOCTEXT("HapbeatSDKEditor", "AssetDisplayName_HapbeatEventMap", "Hapbeat Event Map");
}

UHapbeatClipFactory::UHapbeatClipFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UHapbeatClip::StaticClass();
}

UObject* UHapbeatClipFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	return NewObject<UHapbeatClip>(InParent, Class, Name, Flags | RF_Transactional);
}

uint32 UHapbeatClipFactory::GetMenuCategories() const
{
	return HapbeatEditor::GetAssetCategory();
}

FText UHapbeatClipFactory::GetDisplayName() const
{
	return NSLOCTEXT("HapbeatSDKEditor", "AssetDisplayName_HapbeatClip", "Hapbeat Clip");
}
