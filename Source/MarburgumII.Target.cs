using UnrealBuildTool;

// Local game/listen-server development target. Production players use the Client target.
public class MarburgumIITarget : TargetRules
{
    public MarburgumIITarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.AddRange(new[]
        {
            "MarburgumII", "MarburgumIIShared", "MarburgumIIClient", "MarburgumIIServer"
        });
    }
}
