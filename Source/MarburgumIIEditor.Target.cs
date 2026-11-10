using UnrealBuildTool;

// Editor builds both sides so local multi-process/PIE debugging can exercise them, plus the
// editor-only content tooling (map generator commandlet).
public class MarburgumIIEditorTarget : TargetRules
{
    public MarburgumIIEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.AddRange(new[]
        {
            "MarburgumII", "MarburgumIIShared", "MarburgumIIClient", "MarburgumIIServer", "MarburgumIIEditorTools"
        });
    }
}
