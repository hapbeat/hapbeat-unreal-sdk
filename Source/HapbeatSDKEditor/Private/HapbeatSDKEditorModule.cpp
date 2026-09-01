// Copyright (c) 2026 Hapbeat. MIT License.
#include "HapbeatSDKEditorModule.h"

#include "HapbeatAssetFactories.h"
#include "HapbeatClip.h"
#include "HapbeatEditorSender.h"
#include "HapbeatEventMap.h"
#include "HapbeatClipCustomization.h"
#include "HapbeatEventMapCustomization.h"
#include "HapbeatEntryRef.h"
#include "HapbeatEntryRefCustomization.h"
#include "HapbeatEntryRefPinFactory.h"
#include "HapbeatShowcaseBlueprintBuilder.h"
#include "HapbeatTriggerComponent.h"
#include "HapbeatTriggerComponentCustomization.h"
#include "HapbeatUpdateCheck.h"
#include "SHapbeatEventMapWindow.h"
#include "SHapbeatRuntimeStatusWindow.h"

#include "AssetToolsModule.h"
#include "Editor.h"
#include "IAssetTools.h"
#include "EdGraphUtilities.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "HapbeatSDKEditor"

void FHapbeatSDKEditorModule::StartupModule()
{
	// Give UHapbeatEventMap / UHapbeatClip their own Content Browser entries.
	// IAssetTools allocates the category bit at runtime, so the factories read
	// it back through HapbeatEditor::GetAssetCategory() when the menu is built
	// (they are constructed by the engine, not by us). Registering here is in
	// time because GetMenuCategories() is only queried on menu construction.
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	HapbeatEditor::SetAssetCategory(AssetTools.RegisterAdvancedAssetCategory(
		FName(TEXT("Hapbeat")), LOCTEXT("HapbeatAssetCategory", "Hapbeat")));

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.RegisterCustomClassLayout(UHapbeatEventMap::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FHapbeatEventMapCustomization::MakeInstance));

	// Registered once on the BASE trigger class: PropertyEditor's
	// DetailLayoutHelpers::QueryCustomDetailLayout climbs GetSuperStruct() for
	// every customized object's class and always queries any base class that
	// has a registered layout, even if that base class owns none of the
	// object's own properties -- so this single registration also fires for
	// UHapbeatCollisionTriggerComponent and UHapbeatSequenceComponent
	// instances (verified against Editor/PropertyEditor/Private/
	// DetailLayoutHelpers.cpp, "Ensure that the base class and its parents are
	// always queried" / "Find base classes of queried classes that were not
	// queried"). No per-subclass registration needed.
	PropertyModule.RegisterCustomClassLayout(UHapbeatClip::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FHapbeatClipCustomization::MakeInstance));

	PropertyModule.RegisterCustomClassLayout(UHapbeatTriggerComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FHapbeatTriggerComponentCustomization::MakeInstance));

	// Details-panel row for FHapbeatEntryRef, matching the graph pin below so
	// the struct is edited the same way wherever it is exposed.
	PropertyModule.RegisterCustomPropertyTypeLayout(FHapbeatEntryRef::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FHapbeatEntryRefCustomization::MakeInstance));

	PropertyModule.NotifyCustomizationModuleChanged();

	// Blueprint graph pins of type FHapbeatEntryRef get the by-name entry
	// picker instead of the default struct pin. The factory instance is kept in
	// a member because unregistering requires the SAME shared pointer.
	EntryRefPinFactory = MakeShared<FHapbeatEntryRefPinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(EntryRefPinFactory);

	// Window > Tools > Hapbeat Event Map. A nomad tab (rather than an asset
	// editor) so the window can stay docked while the user switches between
	// several Event Maps, which is how the Unity window is used in practice.
	SHapbeatEventMapWindow::RegisterTabSpawner();
	SHapbeatRuntimeStatusWindow::RegisterTabSpawner();
	GenerateShowcaseCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Hapbeat.GenerateBlueprintShowcase"),
		TEXT("Generate the two Blueprint-authored Showcase zones and replace their map actors."),
		FConsoleCommandDelegate::CreateStatic(&HapbeatShowcaseBlueprintBuilder::Generate));
	GenerateDoorAssetCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Hapbeat.GenerateBlueprintDoorAsset"),
		TEXT("Generate BP_Z2_Door without changing the Showcase map."),
		FConsoleCommandDelegate::CreateStatic(&HapbeatShowcaseBlueprintBuilder::GenerateDoorAsset));

	// Tools menu entries + the once-per-session release-feed notice (DEC-053).
	// Deferred until menus exist: StartupModule can run before UToolMenus is ready.
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateStatic(&FHapbeatUpdateCheck::Register));

	// Belt-and-braces socket cleanup at the end of every PIE session (see
	// FHapbeatEditorSender's class doc for why this can't actually collide
	// with UHapbeatSubsystem's runtime socket even without this).
	EndPieHandle = FEditorDelegates::EndPIE.AddLambda([](bool /*bIsSimulating*/)
	{
		FHapbeatEditorSender::Shutdown();
	});
}

void FHapbeatSDKEditorModule::ShutdownModule()
{
	FEditorDelegates::EndPIE.Remove(EndPieHandle);
	EndPieHandle.Reset();

	SHapbeatEventMapWindow::UnregisterTabSpawner();
	SHapbeatRuntimeStatusWindow::UnregisterTabSpawner();
	if (GenerateShowcaseCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GenerateShowcaseCommand);
		GenerateShowcaseCommand = nullptr;
	}
	if (GenerateDoorAssetCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GenerateDoorAssetCommand);
		GenerateDoorAssetCommand = nullptr;
	}
	FHapbeatUpdateCheck::Unregister();

	if (EntryRefPinFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(EntryRefPinFactory);
		EntryRefPinFactory.Reset();
	}

	FHapbeatEditorSender::Shutdown();

	// PropertyEditor may already be unloaded during engine shutdown teardown.
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(UHapbeatClip::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UHapbeatEventMap::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UHapbeatTriggerComponent::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FHapbeatEntryRef::StaticStruct()->GetFName());
		PropertyModule.NotifyCustomizationModuleChanged();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHapbeatSDKEditorModule, HapbeatSDKEditor);
