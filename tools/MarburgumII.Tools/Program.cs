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
string versionFile = Path.Combine(root, ".unreal-engine-version");
string descriptorFile = Path.Combine(root, "MarburgumII.uproject");

if (!File.Exists(versionFile) || File.ReadAllText(versionFile).Trim() != "5.8.3")
{
    errors.Add("Required Unreal Engine version must be 5.8.3.");
}

using (var doc = JsonDocument.Parse(File.ReadAllText(descriptorFile)))
{
    JsonElement data = doc.RootElement;
    if (!data.TryGetProperty("EngineAssociation", out var association)
        || association.GetString() != "5.8")
    {
        errors.Add("EngineAssociation must be 5.8 (the installed patch is checked separately).");
    }

    if (!data.TryGetProperty("Modules", out var modules)
        || modules.ValueKind != JsonValueKind.Array
        || !modules.EnumerateArray().Any(m =>
            m.TryGetProperty("Name", out var name) && name.GetString() == "MarburgumII"))
    {
        errors.Add("MarburgumII C++ module missing.");
    }
}

string[] requiredFiles =
{
    "Source/MarburgumII.Target.cs",
    "Source/MarburgumIIEditor.Target.cs",
    "Source/MarburgumII/MarburgumII.Build.cs",
    "Source/MarburgumII/Private/MarburgumII.cpp",
    "Source/MarburgumII/Public/MarburgumGameMode.h",
    "native/marburgum-sim-core/Cargo.toml",
    "native/marburgum-sim-core/include/marburgum_sim_core.h"
};

foreach (string path in requiredFiles)
{
    if (!File.Exists(Path.Combine(root, path)))
    {
        errors.Add($"Missing file: {path}");
    }
}

foreach (string error in errors)
{
    Console.Error.WriteLine($"ERROR: {error}");
}

if (errors.Count > 0)
{
    return 1;
}

Console.WriteLine("Marburgum II manifest valid — required Unreal Engine: 5.8.3.");
Console.WriteLine("NOTE: This does not compile Unreal or inspect the installed engine.");
return 0;
