// FmodRender: load Ready or Not's own FMOD banks with its own FMOD 2.03.10 DLLs (copied next to the exe, not in git)
// and either list events / parameters / mixer effects, or render an event to a WAV with FMOD's offline
// (non-real-time) wav writer. Read-only on the game. Private-use research for the Deagle mod.
//   FmodRender list   <bankdir> <filter>
//   FmodRender render <bankdir> <out.wav> <event path> <seconds> [name=value ...] [+ <seconds> name=value ...]
// "+" starts another shot in the same file after the previous one's seconds, with its own parameter values.
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

static class F
{
    const string S = "fmodstudio", C = "fmod";
    [DllImport(S)] public static extern int FMOD_Studio_System_Create(out IntPtr sys, uint headerversion);
    [DllImport(S)] public static extern int FMOD_Studio_System_GetCoreSystem(IntPtr sys, out IntPtr core);
    [DllImport(S)] public static extern int FMOD_Studio_System_Initialize(IntPtr sys, int maxch, uint studioflags, uint flags, IntPtr extra);
    [DllImport(S)] public static extern int FMOD_Studio_System_LoadBankFile(IntPtr sys, byte[] file, uint flags, out IntPtr bank);
    [DllImport(S)] public static extern int FMOD_Studio_System_Update(IntPtr sys);
    [DllImport(S)] public static extern int FMOD_Studio_System_GetEvent(IntPtr sys, byte[] path, out IntPtr desc);
    [DllImport(S)] public static extern int FMOD_Studio_System_GetBus(IntPtr sys, byte[] path, out IntPtr bus);
    [DllImport(S)] public static extern int FMOD_Studio_System_GetBankCount(IntPtr sys, out int n);
    [DllImport(S)] public static extern int FMOD_Studio_System_GetBankList(IntPtr sys, IntPtr[] arr, int cap, out int n);
    [DllImport(S)] public static extern int FMOD_Studio_System_GetParameterDescriptionCount(IntPtr sys, out int n);
    [DllImport(S)] public static extern int FMOD_Studio_System_GetParameterDescriptionList(IntPtr sys, [Out] PDesc[] arr, int cap, out int n);
    [DllImport(S)] public static extern int FMOD_Studio_System_SetParameterByName(IntPtr sys, byte[] name, float v, int ignoreseek);
    [DllImport(S)] public static extern int FMOD_Studio_System_FlushCommands(IntPtr sys);
    [DllImport(S)] public static extern int FMOD_Studio_Bank_GetEventCount(IntPtr bank, out int n);
    [DllImport(S)] public static extern int FMOD_Studio_Bank_GetEventList(IntPtr bank, IntPtr[] arr, int cap, out int n);
    [DllImport(S)] public static extern int FMOD_Studio_Bank_GetBusCount(IntPtr bank, out int n);
    [DllImport(S)] public static extern int FMOD_Studio_Bank_GetBusList(IntPtr bank, IntPtr[] arr, int cap, out int n);
    [DllImport(S)] public static extern int FMOD_Studio_Bank_GetPath(IntPtr bank, byte[] path, int size, out int got);
    [DllImport(S)] public static extern int FMOD_Studio_Bus_GetPath(IntPtr bus, byte[] path, int size, out int got);
    [DllImport(S)] public static extern int FMOD_Studio_Bus_GetVolume(IntPtr bus, out float vol, out float final);
    [DllImport(S)] public static extern int FMOD_Studio_Bus_LockChannelGroup(IntPtr bus);
    [DllImport(S)] public static extern int FMOD_Studio_Bus_GetChannelGroup(IntPtr bus, out IntPtr cg);
    [DllImport(S)] public static extern int FMOD_Studio_EventDescription_GetPath(IntPtr d, byte[] path, int size, out int got);
    [DllImport(S)] public static extern int FMOD_Studio_EventDescription_GetLength(IntPtr d, out int ms);
    [DllImport(S)] public static extern int FMOD_Studio_EventDescription_Is3D(IntPtr d, out int is3d);
    [DllImport(S)] public static extern int FMOD_Studio_EventDescription_GetMinMaxDistance(IntPtr d, out float mn, out float mx);
    [DllImport(S)] public static extern int FMOD_Studio_EventDescription_GetParameterDescriptionCount(IntPtr d, out int n);
    [DllImport(S)] public static extern int FMOD_Studio_EventDescription_GetParameterDescriptionByIndex(IntPtr d, int i, out PDesc p);
    [DllImport(S)] public static extern int FMOD_Studio_EventDescription_GetParameterLabelByIndex(IntPtr d, int i, int li, byte[] label, int size, out int got);
    [DllImport(S)] public static extern int FMOD_Studio_EventDescription_GetUserPropertyCount(IntPtr d, out int n);
    [DllImport(S)] public static extern int FMOD_Studio_EventDescription_CreateInstance(IntPtr d, out IntPtr inst);
    [DllImport(S)] public static extern int FMOD_Studio_EventDescription_LoadSampleData(IntPtr d);
    [DllImport(S)] public static extern int FMOD_Studio_EventDescription_GetSampleLoadingState(IntPtr d, out int state);
    [DllImport(S)] public static extern int FMOD_Studio_System_Release(IntPtr sys);
    [DllImport(S)] public static extern int FMOD_Studio_EventInstance_SetParameterByName(IntPtr i, byte[] name, float v, int ignoreseek);
    [DllImport(S)] public static extern int FMOD_Studio_EventInstance_Start(IntPtr i);
    [DllImport(S)] public static extern int FMOD_Studio_EventInstance_Release(IntPtr i);
    [DllImport(S)] public static extern int FMOD_Studio_EventInstance_GetChannelGroup(IntPtr i, out IntPtr cg);
    [DllImport(S)] public static extern int FMOD_Studio_EventInstance_GetVolume(IntPtr i, out float v, out float final);
    [DllImport(C)] public static extern int FMOD_System_SetOutput(IntPtr core, int type);
    [DllImport(C)] public static extern int FMOD_System_SetSoftwareFormat(IntPtr core, int rate, int speakermode, int raw);
    [DllImport(C)] public static extern int FMOD_System_SetDSPBufferSize(IntPtr core, uint len, int num);
    [DllImport(C)] public static extern int FMOD_System_GetMasterChannelGroup(IntPtr core, out IntPtr cg);
    [DllImport(C)] public static extern int FMOD_ChannelGroup_GetNumDSPs(IntPtr cg, out int n);
    [DllImport(C)] public static extern int FMOD_ChannelGroup_GetDSP(IntPtr cg, int i, out IntPtr dsp);
    [DllImport(C)] public static extern int FMOD_ChannelGroup_GetNumGroups(IntPtr cg, out int n);
    [DllImport(C)] public static extern int FMOD_ChannelGroup_GetGroup(IntPtr cg, int i, out IntPtr child);
    [DllImport(C)] public static extern int FMOD_ChannelGroup_GetNumChannels(IntPtr cg, out int n);
    [DllImport(C)] public static extern int FMOD_ChannelGroup_GetName(IntPtr cg, byte[] name, int len);
    [DllImport(C)] public static extern int FMOD_ChannelGroup_GetVolume(IntPtr cg, out float v);
    [DllImport(C)] public static extern int FMOD_ChannelGroup_SetVolume(IntPtr cg, float v);
    [DllImport(C)] public static extern int FMOD_ChannelGroup_GetChannel(IntPtr cg, int i, out IntPtr ch);
    [DllImport(C)] public static extern int FMOD_Channel_GetCurrentSound(IntPtr ch, out IntPtr snd);
    [DllImport(C)] public static extern int FMOD_Channel_GetVolume(IntPtr ch, out float v);
    [DllImport(C)] public static extern int FMOD_Channel_GetPitch(IntPtr ch, out float p);
    [DllImport(C)] public static extern int FMOD_Channel_GetNumDSPs(IntPtr ch, out int n);
    [DllImport(C)] public static extern int FMOD_Channel_GetDSP(IntPtr ch, int i, out IntPtr dsp);
    [DllImport(C)] public static extern int FMOD_Sound_GetName(IntPtr s, byte[] name, int len);
    [DllImport(C)] public static extern int FMOD_DSP_GetInfo(IntPtr dsp, byte[] name, out uint ver, out int ch, out int w, out int h);
    [DllImport(C)] public static extern int FMOD_DSP_GetNumParameters(IntPtr dsp, out int n);
    [DllImport(C)] public static extern int FMOD_DSP_GetParameterInfo(IntPtr dsp, int i, out IntPtr desc);
    [DllImport(C)] public static extern int FMOD_DSP_GetParameterFloat(IntPtr dsp, int i, out float v, byte[] s, int len);
    [DllImport(C)] public static extern int FMOD_DSP_GetParameterInt(IntPtr dsp, int i, out int v, byte[] s, int len);
    [DllImport(C)] public static extern int FMOD_DSP_GetParameterBool(IntPtr dsp, int i, out int v, byte[] s, int len);
    [DllImport(C)] public static extern int FMOD_DSP_GetBypass(IntPtr dsp, out int b);
    [DllImport(C)] public static extern int FMOD_DSP_GetWetDryMix(IntPtr dsp, out float pre, out float post, out float dry);

    [StructLayout(LayoutKind.Explicit, Size = 56)]
    public struct PDesc
    {
        [FieldOffset(0)] public IntPtr name; [FieldOffset(8)] public uint id1; [FieldOffset(12)] public uint id2;
        [FieldOffset(16)] public float min; [FieldOffset(20)] public float max; [FieldOffset(24)] public float def;
        [FieldOffset(28)] public int type; [FieldOffset(32)] public uint flags;
    }
    public static byte[] Z(string s) => Encoding.UTF8.GetBytes(s + "\0");
    public static string Str(byte[] b) { int n = Array.IndexOf(b, (byte)0); return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n); }
    public static void Ok(int r, string what) { if (r != 0) throw new Exception($"{what}: FMOD error {r}"); }
}

static class P
{
    const uint HEADER = 0x00020310;   // FMOD 2.03.10, the version Ready or Not ships
    static IntPtr sys, core;

    static void Init(string bankdir, string wavOut)
    {
        F.Ok(F.FMOD_Studio_System_Create(out sys, HEADER), "create");
        F.Ok(F.FMOD_Studio_System_GetCoreSystem(sys, out core), "core");
        F.Ok(F.FMOD_System_SetOutput(core, wavOut != null ? 5 : 4), "output");          // WAVWRITER_NRT / NOSOUND_NRT
        F.Ok(F.FMOD_System_SetSoftwareFormat(core, 48000, 3, 0), "format");              // 48 kHz stereo
        F.Ok(F.FMOD_System_SetDSPBufferSize(core, 1024, 4), "dsp buffer");               // one update = 1024 samples
        IntPtr extra = wavOut != null ? Marshal.StringToHGlobalAnsi(wavOut) : IntPtr.Zero;
        F.Ok(F.FMOD_Studio_System_Initialize(sys, 512, 0x2 /*SYNCHRONOUS_UPDATE*/, 0, extra), "init");
        var banks = new List<string> { "Master.bank", "Master.strings.bank", "Weapons.bank" };
        var more = Environment.GetEnvironmentVariable("FMOD_EXTRA_BANKS");      // e.g. "Global Amb.bank;Gas Level.bank" or "*"
        if (more == "*") foreach (var f in Directory.GetFiles(bankdir, "*.bank")) { var n = Path.GetFileName(f); if (!banks.Contains(n)) banks.Add(n); }
        else if (!string.IsNullOrEmpty(more)) banks.AddRange(more.Split(';'));
        foreach (var b in banks)
        {
            int r = F.FMOD_Studio_System_LoadBankFile(sys, F.Z(Path.Combine(bankdir, b)), 0, out _);
            Console.WriteLine($"load {b}: {(r == 0 ? "ok" : "FMOD error " + r)}");
        }
        F.FMOD_Studio_System_FlushCommands(sys);
    }

    static string DescPath(IntPtr d) { var b = new byte[512]; F.FMOD_Studio_EventDescription_GetPath(d, b, b.Length, out _); return F.Str(b); }

    static void DumpParams(IntPtr d)
    {
        F.FMOD_Studio_EventDescription_GetParameterDescriptionCount(d, out int n);
        for (int i = 0; i < n; i++)
        {
            F.FMOD_Studio_EventDescription_GetParameterDescriptionByIndex(d, i, out var p);
            var labels = new List<string>(); var lb = new byte[256];
            for (int li = 0; li < 64 && F.FMOD_Studio_EventDescription_GetParameterLabelByIndex(d, i, li, lb, lb.Length, out _) == 0; li++) labels.Add(F.Str(lb));
            Console.WriteLine($"    param '{Marshal.PtrToStringAnsi(p.name)}' type {p.type} flags 0x{p.flags:x} range {p.min}..{p.max} default {p.def}" +
                              (labels.Count > 0 ? " labels [" + string.Join(", ", labels) + "]" : ""));
        }
    }

    static void DumpDsp(IntPtr dsp, string indent)
    {
        var nb = new byte[32]; F.FMOD_DSP_GetInfo(dsp, nb, out _, out _, out _, out _); F.FMOD_DSP_GetBypass(dsp, out int byp);
        F.FMOD_DSP_GetWetDryMix(dsp, out float pre, out float post, out float dry);
        var sb = new StringBuilder($"{indent}dsp '{F.Str(nb)}'{(byp != 0 ? " BYPASSED" : "")} wet {post:0.###} dry {dry:0.###}:");
        F.FMOD_DSP_GetNumParameters(dsp, out int np);
        for (int i = 0; i < np; i++)
        {
            F.FMOD_DSP_GetParameterInfo(dsp, i, out IntPtr desc);
            int type = Marshal.ReadInt32(desc); string pname = Marshal.PtrToStringAnsi(desc + 4, 16).TrimEnd('\0');
            var vs = new byte[64]; string val;
            if (type == 0) { F.FMOD_DSP_GetParameterFloat(dsp, i, out float v, vs, vs.Length); val = $"{v:0.###}"; }
            else if (type == 1) { F.FMOD_DSP_GetParameterInt(dsp, i, out int v, vs, vs.Length); val = $"{v}"; }
            else if (type == 2) { F.FMOD_DSP_GetParameterBool(dsp, i, out int v, vs, vs.Length); val = v != 0 ? "on" : "off"; }
            else val = "(data)";
            sb.Append($" {pname}={val}");
        }
        Console.WriteLine(sb.ToString());
    }

    static void DumpGroup(IntPtr cg, string indent, int depth)
    {
        var nb = new byte[128]; F.FMOD_ChannelGroup_GetName(cg, nb, nb.Length); F.FMOD_ChannelGroup_GetVolume(cg, out float vol);
        F.FMOD_ChannelGroup_GetNumChannels(cg, out int nch);
        Console.WriteLine($"{indent}group '{F.Str(nb)}' volume {vol:0.###} ({20 * Math.Log10(Math.Max(vol, 1e-9)):0.#} dB) channels {nch}");
        F.FMOD_ChannelGroup_GetNumDSPs(cg, out int nd);
        for (int i = 0; i < nd; i++) { F.FMOD_ChannelGroup_GetDSP(cg, i, out IntPtr dsp); DumpDsp(dsp, indent + "  "); }
        for (int c = 0; c < nch; c++)
        {
            if (F.FMOD_ChannelGroup_GetChannel(cg, c, out IntPtr ch) != 0) continue;
            F.FMOD_Channel_GetCurrentSound(ch, out IntPtr snd); var sn = new byte[256]; if (snd != IntPtr.Zero) F.FMOD_Sound_GetName(snd, sn, sn.Length);
            F.FMOD_Channel_GetVolume(ch, out float cv); F.FMOD_Channel_GetPitch(ch, out float cp);
            Console.WriteLine($"{indent}  channel {c}: sound '{F.Str(sn)}' volume {cv:0.###} ({20 * Math.Log10(Math.Max(cv, 1e-9)):0.#} dB) pitch {cp:0.####}");
            F.FMOD_Channel_GetNumDSPs(ch, out int cnd);
            for (int i = 0; i < cnd; i++) { F.FMOD_Channel_GetDSP(ch, i, out IntPtr dsp); DumpDsp(dsp, indent + "    "); }
        }
        if (depth > 12) return;
        F.FMOD_ChannelGroup_GetNumGroups(cg, out int ng);
        for (int i = 0; i < ng; i++) { F.FMOD_ChannelGroup_GetGroup(cg, i, out IntPtr child); DumpGroup(child, indent + "  ", depth + 1); }
    }

    static int Main(string[] a)
    {
        try
        {
            if (a[0] == "list")
            {
                Init(a[1], null); string filt = a[2].ToLowerInvariant();
                F.FMOD_Studio_System_GetParameterDescriptionCount(sys, out int ng);
                var gp = new F.PDesc[Math.Max(ng, 1)]; F.FMOD_Studio_System_GetParameterDescriptionList(sys, gp, gp.Length, out ng);
                Console.WriteLine($"global parameters: {ng}");
                for (int i = 0; i < ng; i++) Console.WriteLine($"    global '{Marshal.PtrToStringAnsi(gp[i].name)}' type {gp[i].type} range {gp[i].min}..{gp[i].max} default {gp[i].def}");
                F.FMOD_Studio_System_GetBankCount(sys, out int nb); var banks = new IntPtr[nb]; F.FMOD_Studio_System_GetBankList(sys, banks, nb, out nb);
                foreach (var bank in banks)
                {
                    var bp = new byte[256]; F.FMOD_Studio_Bank_GetPath(bank, bp, bp.Length, out _);
                    F.FMOD_Studio_Bank_GetEventCount(bank, out int ne); var evs = new IntPtr[Math.Max(ne, 1)]; F.FMOD_Studio_Bank_GetEventList(bank, evs, evs.Length, out ne);
                    Console.WriteLine($"bank {F.Str(bp)}: {ne} events");
                    for (int i = 0; i < ne; i++)
                    {
                        string path = DescPath(evs[i]); if (!path.ToLowerInvariant().Contains(filt)) continue;
                        F.FMOD_Studio_EventDescription_GetLength(evs[i], out int len); F.FMOD_Studio_EventDescription_Is3D(evs[i], out int is3d);
                        F.FMOD_Studio_EventDescription_GetMinMaxDistance(evs[i], out float mn, out float mx);
                        Console.WriteLine($"  {path}  length {len} ms  3D {is3d}  distance {mn}..{mx} m");
                        DumpParams(evs[i]);
                    }
                    F.FMOD_Studio_Bank_GetBusCount(bank, out int nbus);
                    if (nbus > 0 && filt == "bus")
                    {
                        var buses = new IntPtr[nbus]; F.FMOD_Studio_Bank_GetBusList(bank, buses, nbus, out nbus);
                        foreach (var bus in buses) { var p = new byte[256]; F.FMOD_Studio_Bus_GetPath(bus, p, p.Length, out _); F.FMOD_Studio_Bus_GetVolume(bus, out float v, out _); Console.WriteLine($"  bus {F.Str(p)} volume {v}"); }
                    }
                }
                return 0;
            }
            if (a[0] == "render")
            {
                Init(a[1], a[2]); string evPath = a[3];
                // The engine's very last fader (after every bus, limiter and reverb), like the game's volume slider.
                // At 1.0 indoor shots overflow the 16-bit file; FMOD_MASTER_GAIN=0.5 renders 6 dB lower.
                var mg = Environment.GetEnvironmentVariable("FMOD_MASTER_GAIN");
                if (!string.IsNullOrEmpty(mg)) { F.FMOD_System_GetMasterChannelGroup(core, out IntPtr mcg);
                    F.FMOD_ChannelGroup_SetVolume(mcg, float.Parse(mg, System.Globalization.CultureInfo.InvariantCulture)); Console.WriteLine("final fader " + mg); }
                F.Ok(F.FMOD_Studio_System_GetEvent(sys, F.Z(evPath), out IntPtr desc), "event " + evPath);
                var shots = new List<(double secs, List<(string, float)> ps)>(); var cur = (secs: double.Parse(a[4]), ps: new List<(string, float)>());
                var snaps = new List<string>();
                for (int i = 5; i < a.Length; i++)
                {
                    if (a[i].StartsWith("snapshot:/")) { snaps.Add(a[i]); continue; }
                    if (a[i].StartsWith("start:")) { snaps.Add(a[i].Substring(6)); continue; }   // any event, e.g. a ReverbCommand
                    if (a[i].StartsWith("global:")) { var gv = a[i].Substring(7).Split('=');
                        F.FMOD_Studio_System_SetParameterByName(sys, F.Z(gv[0]), float.Parse(gv[1], System.Globalization.CultureInfo.InvariantCulture), 1);
                        Console.WriteLine($"global {gv[0]}={gv[1]}"); continue; }
                    if (a[i] == "+") { shots.Add(cur); cur = (double.Parse(a[++i]), new List<(string, float)>()); continue; }
                    var kv = a[i].Split('='); cur.ps.Add((kv[0], float.Parse(kv[1], System.Globalization.CultureInfo.InvariantCulture)));
                }
                shots.Add(cur);
                const double block = 1024.0 / 48000;                 // one update = one mix block in NRT mode
                foreach (var sp in snaps)                 // e.g. snapshot:/Reverb_OUT_Street01, what a map's room zone turns on
                {
                    F.Ok(F.FMOD_Studio_System_GetEvent(sys, F.Z(sp), out IntPtr sd), sp); F.Ok(F.FMOD_Studio_EventDescription_CreateInstance(sd, out IntPtr si), sp);
                    F.Ok(F.FMOD_Studio_EventInstance_Start(si), sp); Console.WriteLine("snapshot on: " + sp);
                }
                for (int k = 0; k < 200; k++) F.FMOD_Studio_System_Update(sys);   // let snapshots fade in (~4 s of mix time, silent)
                F.Ok(F.FMOD_Studio_EventDescription_LoadSampleData(desc), "load samples");
                for (int k = 0; k < 2000; k++)          // wait for the event's audio to load (NRT: each update is instant)
                {
                    F.FMOD_Studio_System_Update(sys); F.FMOD_Studio_EventDescription_GetSampleLoadingState(desc, out int st);
                    if (st == 3) break; if (st == 4) throw new Exception("sample data failed to load");
                    System.Threading.Thread.Sleep(2);
                }
                bool dumped = false;
                foreach (var (secs, ps) in shots)
                {
                    F.Ok(F.FMOD_Studio_EventDescription_CreateInstance(desc, out IntPtr inst), "instance");
                    foreach (var (n, v) in ps)
                    {
                        int r = F.FMOD_Studio_EventInstance_SetParameterByName(inst, F.Z(n), v, 1);
                        if (r != 0) { r = F.FMOD_Studio_System_SetParameterByName(sys, F.Z(n), v, 1); Console.WriteLine($"  {n}={v} (global) -> {r}"); }
                        else Console.WriteLine($"  {n}={v} -> ok");
                    }
                    F.Ok(F.FMOD_Studio_EventInstance_Start(inst), "start");
                    int steps = (int)Math.Ceiling(secs / block);
                    for (int s = 0; s < steps; s++)
                    {
                        F.FMOD_Studio_System_Update(sys);
                        if (s == 6 && (!dumped || Environment.GetEnvironmentVariable("DUMP_ALL") == "1"))
                        {
                            dumped = true; F.FMOD_Studio_EventInstance_GetVolume(inst, out float iv, out float fv);
                            Console.WriteLine($"while playing (instance volume {iv}, final {fv}):");
                            if (F.FMOD_Studio_EventInstance_GetChannelGroup(inst, out IntPtr ecg) == 0) { Console.WriteLine(" event's own group:"); DumpGroup(ecg, "  ", 0); }
                            if (Environment.GetEnvironmentVariable("DUMP_MASTER") == "1") { F.FMOD_System_GetMasterChannelGroup(core, out IntPtr master); DumpGroup(master, "  ", 0); }
                        }
                    }
                    F.FMOD_Studio_EventInstance_Release(inst);
                }
                for (int k = 0; k < 8; k++) F.FMOD_Studio_System_Update(sys);
                F.FMOD_Studio_System_Release(sys);       // the wav writer finishes the file on release
                Console.WriteLine("wrote " + a[2]);
                return 0;
            }
            Console.WriteLine("usage: list <bankdir> <filter> | render <bankdir> <out.wav> <event> <secs> [p=v..] [+ <secs> p=v..]"); return 2;
        }
        catch (Exception e) { Console.WriteLine("ERROR " + e.Message); return 1; }
    }
}
