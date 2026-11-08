using UnrealBuildTool;

// Client-only input, UI, camera, audiovisual and local prediction adapters.
// May consume the core module's replicated types; does not depend on MarburgumIIServer.
public class MarburgumIIClient : ModuleRules
{
    public MarburgumIIClient(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "MarburgumII", "MarburgumIIShared", "UMG", "SlateCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "InputCore", "EngineSettings" });
    }
}
