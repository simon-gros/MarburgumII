using UnrealBuildTool;

// Engine-neutral gameplay vocabulary and shared contracts. No client/server module dependencies.
public class MarburgumIIShared : ModuleRules
{
    public MarburgumIIShared(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.Add("Core");
    }
}
