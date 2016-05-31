// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class MICRep : ModuleRules
{
	public MICRep(TargetInfo Target)
	{
		PublicIncludePaths.AddRange(new string[]
			{
				"PoseReceiver/Public",
			});
		PrivateIncludePaths.AddRange(new string[]
			{
				"PoseReceiver/Private",
			});

		PublicDependencyModuleNames.AddRange(new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"LevelEditor",
				"UnrealEd",
				"AssetRegistry",
				"Slate",
				"ContentBrowser",
				"AssetTools",
			});
	}
}
