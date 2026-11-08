using UnrealBuildTool;

// Dedicated-authority services. Does not depend on MarburgumIIClient, UMG or rendering.
public class MarburgumIIServer : ModuleRules
{
    public MarburgumIIServer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.Add("Core");
        PrivateDependencyModuleNames.AddRange(new[] { "CoreUObject", "Engine", "MarburgumIIShared" });
    }
}
