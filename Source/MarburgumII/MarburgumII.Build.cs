using UnrealBuildTool;

public class MarburgumII : ModuleRules
{
    public MarburgumII(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });
        // Common runtime types may consume contracts; never depend on presentation or service modules.
        PrivateDependencyModuleNames.Add("MarburgumIIShared");
    }
}
