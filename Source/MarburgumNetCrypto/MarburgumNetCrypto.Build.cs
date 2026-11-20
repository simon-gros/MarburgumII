using UnrealBuildTool;

// The game's packet encryption (ADR-083): the engine's AES-256-GCM component, strict about plaintext once keyed.
public class MarburgumNetCrypto : ModuleRules
{
    public MarburgumNetCrypto(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "PacketHandler", "AESGCMHandlerComponent", "PlatformCrypto", "PlatformCryptoTypes" });
        PrivateDependencyModuleNames.AddRange(new[] { "CoreUObject", "NetCore" });
    }
}
