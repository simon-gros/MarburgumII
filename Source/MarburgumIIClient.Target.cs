using UnrealBuildTool;

// Client-only package: must never link or depend on MarburgumIIServer.
public class MarburgumIIClientTarget : TargetRules
{
    public MarburgumIIClientTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Client;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.AddRange(new[]
        {
            "MarburgumII", "MarburgumIIShared", "MarburgumIIClient"
        });
    }
}
