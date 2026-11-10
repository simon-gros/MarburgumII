using UnrealBuildTool;

// Editor-only content tooling: the map generator commandlet (scripts/create-maps.sh). Compiled into the
// Editor target only; Game, Client and Server targets never contain it.
public class MarburgumIIEditorTools : ModuleRules
{
    public MarburgumIIEditorTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // The map builders abort a map on the first missing asset or failed save by throwing.
        bEnableExceptions = true;
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "UnrealEd", "AssetTools", "MarburgumII"
        });
    }
}
