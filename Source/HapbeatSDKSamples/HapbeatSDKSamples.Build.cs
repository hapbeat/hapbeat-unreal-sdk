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
			"EnhancedInput", // UInputAction / UInputMappingContext used by the VR Config sample
			"InputCore", // EKeys::* for the samples' legacy InputComponent->BindKey wiring
			"Projects",  // IPluginManager (resolve this plugin's Content dir for raw WAV loads)
			"PhysicsCore", // UPhysicalMaterial for editor-adjustable Z5 projectile bounce
			"Slate",     // SHapbeatShowcaseHud: the Showcase key guide, drawn as a widget so it has
			"SlateCore", // real columns -- and as code, so the samples ship with no UI .uasset

			"UMG",       // UWidgetComponent: the world-space surface the VR sample puts the address panel on
			"HeadMountedDisplay", // UMotionControllerComponent: the VR sample's OpenXR controller ray
			"HapbeatSDK", // the runtime module the samples drive
		});
	}
}
