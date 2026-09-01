// Copyright (c) 2026 Hapbeat. MIT License.
using UnrealBuildTool;

public class HapbeatSDKEditor : ModuleRules
{
	public HapbeatSDKEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Sockets",       // FSocket (editor Test Play sender)
			"Networking",    // FUdpSocketBuilder / FIPv4Address
			"Json",          // FJsonSerializer / FJsonObject (manifest.json parsing)
			"UnrealEd",      // FEditorDelegates::EndPIE, FScopedTransaction, UFactory
			"LevelEditor",   // FLevelEditorModule::GetFirstActiveViewport (the PIE target for the capture script)
			"AssetTools",    // IAssetTools::RegisterAdvancedAssetCategory (Content Browser "Hapbeat" category)
			"AssetDefinition", // UAssetDefinition (asset display name / color for the two authored assets)
			"PropertyEditor",// IDetailCustomization, FPropertyEditorModule
			"GraphEditor",   // SGraphPin (the FHapbeatEntryRef by-name entry pin)
			"BlueprintGraph",// UEdGraphSchema_K2::PC_Struct (pin-type match in the pin factory)
			"Slate",         // SButton / SComboBox / SHorizontalBox
			"SlateCore",     // STextBlock, FAppStyle (Runtime/SlateCore)
			"InputCore",     // EKeys::* referenced by SComboBox/SListView templates instantiated in this module
			"DesktopPlatform", // IDesktopPlatform::OpenFileDialog (UHapbeatClip WAV import)
			"Projects",      // IPluginManager (bundled-sample WAV dialog default / plugin Content manifest scan)
			"ToolMenus",     // Tools menu entries (update check)
			"Settings",      // Project Settings > Plugins > Hapbeat shortcut
			"HTTP",          // release-feed query (DEC-053 update notification)
			"ContentBrowser",// selected-asset lookup for the Markdown export
			"AssetRegistry", // single-Event-Map fallback for the Markdown export
			"HapbeatSDK",    // the runtime module this customizes
			"HapbeatSDKSamples", // creates the two Blueprint-authored Showcase zones
		});
	}
}
