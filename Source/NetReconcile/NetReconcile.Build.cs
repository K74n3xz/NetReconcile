// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NetReconcile : ModuleRules
{
	public NetReconcile(ReadOnlyTargetRules Target) : base(Target)
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

		PrivateDependencyModuleNames.AddRange(new string[] { "SlateCore", "PacketHandler" });
		if (Target.bCompileAgainstEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "ApplicationCore" }); // PIE UI test and native input-device mapping.
		}

		PublicIncludePaths.AddRange(new string[] {
			"NetReconcile",
			"NetReconcile/Variant_Platforming",
			"NetReconcile/Variant_Platforming/Animation",
			"NetReconcile/Variant_Combat",
			"NetReconcile/Variant_Combat/AI",
			"NetReconcile/Variant_Combat/Animation",
			"NetReconcile/Variant_Combat/Gameplay",
			"NetReconcile/Variant_Combat/Interfaces",
			"NetReconcile/Variant_Combat/UI",
			"NetReconcile/Variant_SideScrolling",
			"NetReconcile/Variant_SideScrolling/AI",
			"NetReconcile/Variant_SideScrolling/Gameplay",
			"NetReconcile/Variant_SideScrolling/Interfaces",
			"NetReconcile/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
