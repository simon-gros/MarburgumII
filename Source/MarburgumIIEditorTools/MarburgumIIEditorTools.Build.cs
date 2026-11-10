using UnrealBuildTool;

// Editor-only content tooling: the map, swim and vehicle content commandlets (scripts/create-*.sh). Compiled into the
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
            "Core", "CoreUObject", "Engine", "UnrealEd", "AssetTools", "MeshDescription", "StaticMeshDescription", "MeshConversion", "Landscape", "Json", "BSPUtils", "MarburgumII", "MarburgumIIShared"
        });
    }
}
