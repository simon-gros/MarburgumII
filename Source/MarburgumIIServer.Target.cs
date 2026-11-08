using UnrealBuildTool;

public class MarburgumIIServerTarget : TargetRules
{
    public MarburgumIIServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.Add("MarburgumII");
    }
}
