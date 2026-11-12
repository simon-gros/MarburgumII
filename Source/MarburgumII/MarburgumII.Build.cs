using UnrealBuildTool;

public class MarburgumII : ModuleRules
{
    public MarburgumII(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore"
        });
        // Common runtime types may consume contracts; never depend on presentation or service modules.
        PrivateDependencyModuleNames.Add("MarburgumIIShared");
        // The street walk graph (ADR-044) is a JSON file beside the content.
        PrivateDependencyModuleNames.Add("Json");
        // What a tyre or a foot stands on: the landscape's collision component (ADR-061).
        PrivateDependencyModuleNames.Add("Landscape");
    }
}
