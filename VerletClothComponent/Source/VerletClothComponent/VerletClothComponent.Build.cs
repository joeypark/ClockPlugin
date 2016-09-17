// Copyright 2016 Moai Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class VerletClothComponent : ModuleRules
	{
        public VerletClothComponent(TargetInfo Target)
		{
            PCHUsage = PCHUsageMode.NoSharedPCHs;

            PrivateIncludePaths.Add("VerletClothComponent/Private");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                    "Engine",
                    "RenderCore",
                    "ShaderCore",
                    "RHI"
				}
				);
		}
	}
}
