// FmodCoexist: proof that Ready or Not's FMOD 2.03.10 can run in the same process as KCD2's FMOD 2.02.21.
// 1. loads KCD2's fmod.dll + fmodstudio.dll (2.02.21) by full path and starts a 2.02 Studio system, like the game;
// 2. loads RoN's copies renamed fmor.dll / fmorstudio.dll (the 8-byte import name "fmod.dll" -> "fmor.dll" patched in
//    fmorstudio.dll), starts a 2.03 Studio system with RoN's banks and renders the MK V with the room recipe
//    (ReverbMaster + ReverbHALL=1, IsOutside=0) to a WAV with FMOD's offline writer;
// 3. prints which FMOD modules are loaded, so the result can be compared with FmodRender's render.
//   FmodCoexist <kcd dir with fmod.dll, fmodstudio.dll> <ron dir with fmor.dll, fmorstudio.dll> <RoN bank dir> <out.wav>
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

static class N
{
    [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)] public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32", CharSet = CharSet.Ansi)] public static extern IntPtr GetProcAddress(IntPtr mod, string name);
    public const uint ALTERED_SEARCH_PATH = 0x8;
    public static T Fn<T>(IntPtr mod, string name) where T : Delegate
    {
        var p = GetProcAddress(mod, name);
        if (p == IntPtr.Zero) throw new Exception("missing export " + name);
        return Marshal.GetDelegateForFunctionPointer<T>(p);
    }
    public static byte[] Z(string s) => Encoding.UTF8.GetBytes(s + "\0");
    public static void Ok(int r, string what) { if (r != 0) throw new Exception($"{what}: FMOD error {r}"); }
}

delegate int PCreate(out IntPtr sys, uint ver);
delegate int PInit(IntPtr sys, int maxch, uint flags, IntPtr extra);
delegate int PStudioInit(IntPtr sys, int maxch, uint studioflags, uint flags, IntPtr extra);
delegate int PSys(IntPtr sys);
delegate int PSysOut(IntPtr sys, out IntPtr o);
delegate int PSysInt(IntPtr sys, int v);
delegate int PFormat(IntPtr core, int rate, int mode, int raw);
delegate int PDspBuf(IntPtr core, uint len, int num);
delegate int PLoadBank(IntPtr sys, byte[] file, uint flags, out IntPtr bank);
delegate int PGetEvent(IntPtr sys, byte[] path, out IntPtr desc);
delegate int PDescState(IntPtr desc, out int state);
delegate int PSetParam(IntPtr obj, byte[] name, float v, int ignore);
delegate int PGetVersion(IntPtr core, out uint ver, out uint build);

static class P
{
    static int Main(string[] a)
    {
        try
        {
            string kcd = a[0], ron = a[1], banks = a[2], wav = a[3];
            // ---- 1. the game's FMOD 2.02.21
            IntPtr kCore = N.LoadLibraryExW(Path.Combine(kcd, "fmod.dll"), IntPtr.Zero, N.ALTERED_SEARCH_PATH);
            IntPtr kStudio = N.LoadLibraryExW(Path.Combine(kcd, "fmodstudio.dll"), IntPtr.Zero, N.ALTERED_SEARCH_PATH);
            if (kCore == IntPtr.Zero || kStudio == IntPtr.Zero) throw new Exception("KCD2 FMOD load failed " + Marshal.GetLastWin32Error());
            N.Ok(N.Fn<PCreate>(kStudio, "FMOD_Studio_System_Create")(out IntPtr kSys, 0x00020221), "kcd studio create");
            N.Ok(N.Fn<PSysOut>(kStudio, "FMOD_Studio_System_GetCoreSystem")(kSys, out IntPtr kSysCore), "kcd core");
            N.Ok(N.Fn<PSysInt>(kCore, "FMOD_System_SetOutput")(kSysCore, 2), "kcd output nosound");
            N.Ok(N.Fn<PStudioInit>(kStudio, "FMOD_Studio_System_Initialize")(kSys, 64, 0, 0, IntPtr.Zero), "kcd studio init");
            N.Ok(N.Fn<PCreate>(kCore, "FMOD_System_Create")(out IntPtr kPlugin, 0x00020221), "kcd 2nd core create (like the plugin)");
            N.Ok(N.Fn<PSysInt>(kCore, "FMOD_System_SetOutput")(kPlugin, 2), "kcd 2nd core output");
            N.Ok(N.Fn<PInit>(kCore, "FMOD_System_Init")(kPlugin, 32, 0, IntPtr.Zero), "kcd 2nd core init");
            N.Fn<PGetVersion>(kCore, "FMOD_System_GetVersion")(kPlugin, out uint kv, out uint kb);
            Console.WriteLine($"KCD2 FMOD running: version {kv:x8} build {kb}");

            // ---- 2. Ready or Not's FMOD 2.03.10 under the renamed files
            IntPtr rCore = N.LoadLibraryExW(Path.Combine(ron, "fmor.dll"), IntPtr.Zero, N.ALTERED_SEARCH_PATH);
            IntPtr rStudio = N.LoadLibraryExW(Path.Combine(ron, "fmorstudio.dll"), IntPtr.Zero, N.ALTERED_SEARCH_PATH);
            if (rCore == IntPtr.Zero || rStudio == IntPtr.Zero) throw new Exception("RoN FMOD load failed " + Marshal.GetLastWin32Error());
            Console.WriteLine($"modules: KCD2 core {kCore:x}, RoN core {rCore:x} ({(kCore == rCore ? "SAME - clash!" : "separate")})");
            N.Ok(N.Fn<PCreate>(rStudio, "FMOD_Studio_System_Create")(out IntPtr rSys, 0x00020310), "ron studio create");
            N.Ok(N.Fn<PSysOut>(rStudio, "FMOD_Studio_System_GetCoreSystem")(rSys, out IntPtr rSysCore), "ron core");
            N.Ok(N.Fn<PSysInt>(rCore, "FMOD_System_SetOutput")(rSysCore, 5), "ron output wavwriter_nrt");
            N.Ok(N.Fn<PFormat>(rCore, "FMOD_System_SetSoftwareFormat")(rSysCore, 48000, 3, 0), "ron format");
            N.Ok(N.Fn<PDspBuf>(rCore, "FMOD_System_SetDSPBufferSize")(rSysCore, 1024, 4), "ron dsp buffer");
            N.Ok(N.Fn<PStudioInit>(rStudio, "FMOD_Studio_System_Initialize")(rSys, 512, 0x2, 0, Marshal.StringToHGlobalAnsi(wav)), "ron studio init");
            N.Fn<PGetVersion>(rCore, "FMOD_System_GetVersion")(rSysCore, out uint rv, out uint rb);
            Console.WriteLine($"RoN FMOD running: version {rv:x8} build {rb}");
            var load = N.Fn<PLoadBank>(rStudio, "FMOD_Studio_System_LoadBankFile");
            foreach (var b in new[] { "Master.bank", "Master.strings.bank", "Weapons.bank", "Global Amb.bank" })
                N.Ok(load(rSys, N.Z(Path.Combine(banks, b)), 0, out _), "load " + b);
            var update = N.Fn<PSys>(rStudio, "FMOD_Studio_System_Update");
            var getEvent = N.Fn<PGetEvent>(rStudio, "FMOD_Studio_System_GetEvent");
            var create = N.Fn<PSysOut>(rStudio, "FMOD_Studio_EventDescription_CreateInstance");
            var start = N.Fn<PSys>(rStudio, "FMOD_Studio_EventInstance_Start");
            var release = N.Fn<PSys>(rStudio, "FMOD_Studio_EventInstance_Release");
            var setGlobal = N.Fn<PSetParam>(rStudio, "FMOD_Studio_System_SetParameterByName");
            var setParam = N.Fn<PSetParam>(rStudio, "FMOD_Studio_EventInstance_SetParameterByName");
            var loadSamples = N.Fn<PSys>(rStudio, "FMOD_Studio_EventDescription_LoadSampleData");
            var sampleState = N.Fn<PDescState>(rStudio, "FMOD_Studio_EventDescription_GetSampleLoadingState");
            // same order as FmodRender: globals, start ReverbMaster, 200 silent updates, load the shot, fire
            N.Ok(setGlobal(rSys, N.Z("ReverbHALL"), 1f, 1), "global ReverbHALL");
            N.Ok(getEvent(rSys, N.Z("event:/Levels/Reverbs/ReverbMaster"), out IntPtr rmDesc), "ReverbMaster");
            N.Ok(create(rmDesc, out IntPtr rm), "ReverbMaster instance"); N.Ok(start(rm), "ReverbMaster start");
            for (int k = 0; k < 200; k++) update(rSys);
            N.Ok(getEvent(rSys, N.Z("event:/Weapons/Grizzly/1P Grizzly Fire"), out IntPtr fire), "fire event");
            N.Ok(loadSamples(fire), "load samples");
            for (int k = 0; k < 2000; k++) { update(rSys); sampleState(fire, out int st); if (st == 3) break; System.Threading.Thread.Sleep(2); }
            for (int shot = 0; shot < 4; shot++)
            {
                N.Ok(create(fire, out IntPtr inst), "instance"); N.Ok(setParam(inst, N.Z("IsOutside"), 0f, 1), "IsOutside");
                N.Ok(start(inst), "start");
                for (int s = 0; s < (int)Math.Ceiling(4.0 / (1024.0 / 48000)); s++) update(rSys);
                release(inst);
            }
            for (int k = 0; k < 8; k++) update(rSys);
            N.Ok(N.Fn<PSys>(rStudio, "FMOD_Studio_System_Release")(rSys), "ron release");
            // the game's FMOD still fine?
            N.Ok(N.Fn<PSys>(kStudio, "FMOD_Studio_System_Update")(kSys), "kcd studio update after");
            N.Ok(N.Fn<PSys>(kCore, "FMOD_System_Update")(kPlugin), "kcd 2nd core update after");
            foreach (System.Diagnostics.ProcessModule m in System.Diagnostics.Process.GetCurrentProcess().Modules)
                if (m.ModuleName.StartsWith("fmo", StringComparison.OrdinalIgnoreCase)) Console.WriteLine($"  loaded: {m.ModuleName}  {m.FileName}  {m.FileVersionInfo.FileVersion}");
            Console.WriteLine("wrote " + wav + "; both FMOD versions ran in one process");
            return 0;
        }
        catch (Exception e) { Console.WriteLine("ERROR " + e.Message); return 1; }
    }
}
