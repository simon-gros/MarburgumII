using UnrealBuildTool;

// Client-only input, UI, camera, audiovisual and local prediction adapters.
// May consume the core module's replicated types; does not depend on MarburgumIIServer.
public class MarburgumIIClient : ModuleRules
{
    public MarburgumIIClient(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "MarburgumII", "MarburgumIIShared", "UMG", "SlateCore" });
        // RHI and Projects let the render-feature subsystem detect optional NVIDIA plugins at runtime;
        // those plugins are never a build dependency (ADR-029).
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "InputCore", "EngineSettings", "RHI", "Projects" });
        // Every widget and test driver keeps its layout constants in a file-local namespace; unity
        // blobs would merge those and clash whenever a new file reshuffles the grouping.
        bUseUnity = false;
    }
}
