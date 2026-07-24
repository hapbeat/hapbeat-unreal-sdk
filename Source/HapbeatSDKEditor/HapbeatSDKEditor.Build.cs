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
			"UnrealEd",      // FEditorDelegates::EndPIE, FScopedTransaction
			"PropertyEditor",// IDetailCustomization, FPropertyEditorModule
			"Slate",         // SButton / SComboBox / SHorizontalBox
			"SlateCore",     // STextBlock, FAppStyle (Runtime/SlateCore)
			"InputCore",     // EKeys::* referenced by SComboBox/SListView templates instantiated in this module
			"HapbeatSDK",    // the runtime module this customizes
		});
	}
}
