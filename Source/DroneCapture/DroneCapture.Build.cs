using UnrealBuildTool;

public class DroneCapture : ModuleRules
{
	public DroneCapture(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"Slate",
			"SlateCore",
		});
	}
}
