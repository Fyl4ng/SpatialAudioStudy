// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SpatialAudioStudy : ModuleRules
{
	public SpatialAudioStudy(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"SpatialAudioStudy",
			"SpatialAudioStudy/Variant_Horror",
			"SpatialAudioStudy/Variant_Horror/UI",
			"SpatialAudioStudy/Variant_Shooter",
			"SpatialAudioStudy/Variant_Shooter/AI",
			"SpatialAudioStudy/Variant_Shooter/UI",
			"SpatialAudioStudy/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
