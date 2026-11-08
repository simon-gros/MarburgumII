using UnrealBuildTool;

public class MarburgumIIEditorTarget : TargetRules
{
    public MarburgumIIEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.Add("MarburgumII");
    }
}
