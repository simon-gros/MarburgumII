using UnrealBuildTool;

// Headless dedicated authoritative server; no client presentation module.
public class MarburgumIIServerTarget : TargetRules
{
    public MarburgumIIServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.AddRange(new[]
        {
            "MarburgumII", "MarburgumIIShared", "MarburgumIIServer"
        });
    }
}
