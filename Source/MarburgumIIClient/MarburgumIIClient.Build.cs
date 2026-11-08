using UnrealBuildTool;

// Client-only UMG and input presentation. No ServerOnly or persistent backend dependency.
public class MarburgumIIClient : ModuleRules
{
    public MarburgumIIClient(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.Add("Core");
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "CoreUObject", "Engine", "UMG", "Slate", "SlateCore",
            "MarburgumII", "MarburgumIIShared"
        });
    }
}
