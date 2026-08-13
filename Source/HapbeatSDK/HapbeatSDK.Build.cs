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
			"Slate",             // in-game address-override panel (works in packaged builds, no UMG asset to author)
			"SlateCore",
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// GetAdaptersAddresses (HapbeatNetInterfaces.cpp) is the only
			// documented way to read a per-address prefix length on Windows,
			// and it is what lets discovery use a subnet-directed broadcast
			// instead of 255.255.255.255 -- see DEC-054. Linux/macOS use
			// getifaddrs, which is in libc and needs no extra library.
			PublicSystemLibraries.Add("iphlpapi.lib");
		}
	}
}
