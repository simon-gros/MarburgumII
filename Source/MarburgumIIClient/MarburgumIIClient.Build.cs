using UnrealBuildTool;

// Client-only input, UI, camera, audiovisual and local prediction adapters.
// Does not depend on MarburgumIIServer.
public class MarburgumIIClient : ModuleRules
{
    public MarburgumIIClient(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.Add("Core");
        PrivateDependencyModuleNames.AddRange(new[] { "CoreUObject", "Engine", "MarburgumIIShared" });
    }
}
