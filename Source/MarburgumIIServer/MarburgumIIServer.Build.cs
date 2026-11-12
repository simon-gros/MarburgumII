using UnrealBuildTool;

// Dedicated-authority services. Does not depend on MarburgumIIClient, UMG or rendering.
// May consume the core module's replicated types (ADR-012).
public class MarburgumIIServer : ModuleRules
{
    public MarburgumIIServer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "MarburgumII" });
        PrivateDependencyModuleNames.AddRange(new[] { "HTTP", "Json", "MarburgumIIShared" });
    }
}
