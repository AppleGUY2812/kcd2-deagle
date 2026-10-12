// RonExport: read-only export of Ready or Not assets (private use).
// usage: RonExport <paksDir> <usmap> <outDir> <listFile> [keyFile]   (listFile: one /Game/... package path per line)
// Build and run: tools/ron/ronexport.bat, docs/ron_extraction.md.
using CUE4Parse.Compression;
using CUE4Parse.Encryption.Aes;
using CUE4Parse.FileProvider;
using CUE4Parse.MappingsProvider.Usmap;
using CUE4Parse.UE4.Objects.Core.Misc;
using CUE4Parse.UE4.Versions;
using CUE4Parse.UE4.Assets.Exports.Texture;
using CUE4Parse.UE4.Assets.Exports.Material;
using CUE4Parse_Conversion;
using CUE4Parse_Conversion.Options;
using CUE4Parse_Conversion.Writers.UEFormat.Enums;

var paks = args[0]; var usmap = args[1]; var outDir = args[2]; var list = args[3];
var oodle = Path.Combine(AppContext.BaseDirectory, OodleHelper.OODLE_NAME_CURRENT);
if (!File.Exists(oodle)) { var dl = oodle; OodleHelper.DownloadOodleDll(ref dl); oodle = dl; }
OodleHelper.Initialize(oodle);
var provider = new DefaultFileProvider(paks, SearchOption.TopDirectoryOnly, new VersionContainer(EGame.GAME_UE5_3), StringComparer.OrdinalIgnoreCase);
provider.MappingsContainer = new FileUsmapTypeMappingsProvider(usmap, StringComparer.OrdinalIgnoreCase);
provider.Initialize();
// Ready or Not's paks are not encrypted (index key GUID all zeros), so the all-zero key is enough. If an update
// encrypts them, put the key (one line, "0x" + 64 hex digits) in tools/ron/local_keys.txt, which is gitignored, and
// pass it as the 5th argument (ronexport.bat does when the file exists). Never commit a key.
var aes = "0x" + new string('0', 64);
if (args.Length > 4 && File.Exists(args[4]))
    aes = File.ReadAllLines(args[4]).Select(l => l.Trim()).First(l => l.StartsWith("0x"));
provider.SubmitKey(new FGuid(), new FAesKey(aes));
provider.PostMount();
Console.WriteLine($"mounted {provider.Files.Count} files");
var opts = new ExportOptions(EMeshFormat.ActorX, ENaniteMeshFormat.NoNanite, EMeshQuality.Highest, default(ETexturePlatform), ETextureFormat.Png, 100,
    false, false, default(EMaterialDepth), true, false, ESocketFormat.Bone, default(EFileCompressionFormat));
var session = new ExportSession(null!);
foreach (var line in File.ReadAllLines(list)) {
    var p = line.Trim(); if (p.Length == 0) continue;
    try { var obj = provider.LoadPackageObject(p + "." + p.Split('/').Last()); session.Add(obj); Console.WriteLine($"queued {obj.ExportType} {p}"); }
    catch (Exception e) { Console.WriteLine($"FAIL load {p}: {e.GetType().Name} {e.Message}"); }
}
var results = await session.RunAsync(outDir, opts, null!, CancellationToken.None);
foreach (var r in results) Console.WriteLine((r.Success ? "OK " : "ERR ") + r.ObjectPath + " -> " + string.Join(", ", r.DiskFilePaths ?? new List<string>()) + (r.Error != null ? "  " + r.Error.Message : ""));
