using System.Text.Json;

if (args.Length != 1 || args[0] != "verify")
{
    Console.Error.WriteLine("Usage: MarburgumII.Tools verify");
    return 2;
}

string? root = Directory.GetCurrentDirectory();
while (root != null && !File.Exists(Path.Combine(root, "MarburgumII.uproject")))
{
    root = Directory.GetParent(root)?.FullName;
}
if (root is null)
{
    Console.Error.WriteLine("Could not locate MarburgumII.uproject.");
    return 1;
}

var errors = new List<string>();
string Read(string path) => File.ReadAllText(Path.Combine(root, path));
void RequireFile(string path)
{
    if (!File.Exists(Path.Combine(root, path))) errors.Add($"Missing file: {path}");
}

if (!File.Exists(Path.Combine(root, ".unreal-engine-version"))
    || Read(".unreal-engine-version").Trim() != "5.8.3")
{
    errors.Add("Required Unreal Engine version must be 5.8.3.");
}

var requiredModules = new Dictionary<string, string>
{
    ["MarburgumII"] = "Runtime",
    ["MarburgumIIShared"] = "Runtime",
    ["MarburgumIIClient"] = "ClientOnly",
    ["MarburgumIIServer"] = "ServerOnly",
    ["MarburgumIIEditorTools"] = "Editor"
};
using (var doc = JsonDocument.Parse(Read("MarburgumII.uproject")))
{
    JsonElement data = doc.RootElement;
    if (!data.TryGetProperty("EngineAssociation", out var association)
        || association.GetString() != "5.8")
    {
        errors.Add("EngineAssociation must be 5.8; the installed patch is checked separately.");
    }

    if (!data.TryGetProperty("Modules", out var modules)
        || modules.ValueKind != JsonValueKind.Array)
    {
        errors.Add("Unreal module manifest is missing.");
    }
    else
    {
        var actual = modules.EnumerateArray().Select(m => new
        {
            Name = m.GetProperty("Name").GetString() ?? "",
            Type = m.GetProperty("Type").GetString() ?? ""
        }).ToList();
        foreach (var entry in requiredModules)
        {
            if (actual.Count(m => m.Name == entry.Key && m.Type == entry.Value) != 1)
                errors.Add($"Module {entry.Key} must appear once with type {entry.Value}.");
        }
    }
}

string[] requiredFiles =
{
    "Source/MarburgumII.Target.cs",
    "Source/MarburgumIIClient.Target.cs",
    "Source/MarburgumIIEditor.Target.cs",
    "Source/MarburgumIIServer.Target.cs",
    "Source/MarburgumII/MarburgumII.Build.cs",
    "Source/MarburgumII/Private/MarburgumII.cpp",
    "Source/MarburgumII/Public/MarburgumGameMode.h",
    "Source/MarburgumII/Public/MarburgumGameState.h",
    "Source/MarburgumII/Private/MarburgumGameState.cpp",
    "Source/MarburgumII/Public/MarburgumPlayerState.h",
    "Source/MarburgumII/Public/MarburgumPlayerController.h",
    "Source/MarburgumII/Private/MarburgumPlayerController.cpp",
    "Source/MarburgumIIShared/MarburgumIIShared.Build.cs",
    "Source/MarburgumIIShared/Private/MarburgumIISharedModule.cpp",
    "Source/MarburgumIIShared/Public/MarburgumNetworkContract.h",
    "Source/MarburgumIIClient/MarburgumIIClient.Build.cs",
    "Source/MarburgumIIClient/Private/MarburgumIIClientModule.cpp",
    "Source/MarburgumIIClient/Public/MarburgumNetSmokeSubsystem.h",
    "Source/MarburgumIIClient/Private/MarburgumNetSmokeSubsystem.cpp",
    "Source/MarburgumIIServer/MarburgumIIServer.Build.cs",
    "Source/MarburgumIIServer/Private/MarburgumIIServerModule.cpp",
    "Docs/NETWORK_ARCHITECTURE.md",
    "Content/Maps/NetSmoke.umap",
    "scripts/package-linux.sh",
    "scripts/net-smoke-linux.sh",
    "native/marburgum-sim-core/Cargo.toml",
    "native/marburgum-sim-core/include/marburgum_sim_core.h",
    "tools/mb2/Cargo.toml",
    "Source/MarburgumIIEditorTools/MarburgumIIEditorTools.Build.cs",
    "Source/MarburgumIIEditorTools/Private/MarburgumCreateMapsCommandlet.cpp"
};
foreach (string path in requiredFiles) RequireFile(path);

string[] targetNames = { "MarburgumII", "MarburgumIIClient", "MarburgumIIEditor", "MarburgumIIServer" };
string[] targetTypes = { "Game", "Client", "Editor", "Server" };
for (int i = 0; i < targetNames.Length; i++)
{
    string path = $"Source/{targetNames[i]}.Target.cs";
    if (!File.Exists(Path.Combine(root, path))) continue;
    string content = Read(path);
    if (!content.Contains($"Type = TargetType.{targetTypes[i]};"))
        errors.Add($"{path} must be TargetType.{targetTypes[i]}.");
    if (!content.Contains("\"MarburgumII\"") || !content.Contains("\"MarburgumIIShared\""))
        errors.Add($"{path} must include core and shared modules.");
    // Content tooling is editor-only; shipped game, client and server binaries never contain it.
    if (content.Contains("\"MarburgumIIEditorTools\"") != (targetTypes[i] == "Editor"))
        errors.Add($"{path}: MarburgumIIEditorTools belongs to the Editor target only.");
}
string clientTargetPath = "Source/MarburgumIIClient.Target.cs";
string serverTargetPath = "Source/MarburgumIIServer.Target.cs";
if (File.Exists(Path.Combine(root, clientTargetPath)))
{
    string content = Read(clientTargetPath);
    if (!content.Contains("\"MarburgumIIClient\"") || content.Contains("\"MarburgumIIServer\""))
        errors.Add("Client target must include client and exclude server module.");
}
if (File.Exists(Path.Combine(root, serverTargetPath)))
{
    string content = Read(serverTargetPath);
    if (!content.Contains("\"MarburgumIIServer\"") || content.Contains("\"MarburgumIIClient\""))
        errors.Add("Server target must include server and exclude client module.");
}
// Side modules may consume core and shared modules; core and shared never depend on a side.
foreach (string module in new[] { "MarburgumII", "MarburgumIIShared", "MarburgumIIClient", "MarburgumIIServer", "MarburgumIIEditorTools" })
{
    string path = $"Source/{module}/{module}.Build.cs";
    if (!File.Exists(Path.Combine(root, path))) continue;
    string content = Read(path);
    if (module != "MarburgumIIClient" && content.Contains("\"MarburgumIIClient\""))
        errors.Add($"{module} must not depend on the client module.");
    if (module != "MarburgumIIServer" && content.Contains("\"MarburgumIIServer\""))
        errors.Add($"{module} must not depend on the server module.");
}
string sharedBuildPath = "Source/MarburgumIIShared/MarburgumIIShared.Build.cs";
if (File.Exists(Path.Combine(root, sharedBuildPath)) && Read(sharedBuildPath).Contains("\"MarburgumII\""))
    errors.Add("MarburgumIIShared must not depend on the core game module.");
foreach (string error in errors) Console.Error.WriteLine($"ERROR: {error}");
if (errors.Count > 0) return 1;

Console.WriteLine("Marburgum II manifests valid — UE 5.8.3, separated shared/client/server modules and targets, editor-only tooling.");
Console.WriteLine("NOTE: This is a source-only check. It does not compile Unreal or prove network connectivity.");
return 0;
