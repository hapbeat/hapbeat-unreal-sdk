// Copyright (c) 2026 Hapbeat. MIT License.
using UnrealBuildTool;

public class HapbeatSDK : ModuleRules
{
	public HapbeatSDK(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Sockets",
			"Networking",
			"DeveloperSettings", // UHapbeatConfig : UDeveloperSettings (Project Settings page)
			"PhysicsCore",       // FBodyInstance read in the collision trigger's setup hint
		});
	}
}
