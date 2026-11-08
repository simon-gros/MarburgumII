using UnrealBuildTool;

public class MarburgumIITarget : TargetRules
{
    public MarburgumIITarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.Add("MarburgumII");
    }
}
