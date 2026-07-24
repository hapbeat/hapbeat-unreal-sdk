// Copyright (c) 2026 Hapbeat. MIT License.
using UnrealBuildTool;

public class HapbeatSDKSamples : ModuleRules
{
	public HapbeatSDKSamples(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore", // EKeys::* for the samples' legacy InputComponent->BindKey wiring
			"Projects",  // IPluginManager (resolve this plugin's Content dir for raw WAV loads)
			"HapbeatSDK", // the runtime module the samples drive
		});
	}
}
