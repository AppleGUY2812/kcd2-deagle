// FmodRender: load Ready or Not's own FMOD banks with its own FMOD 2.03.10 DLLs (copied next to the exe, not in git)
// and either list events / parameters / mixer effects, or render an event to a WAV with FMOD's offline
// (non-real-time) wav writer. Read-only on the game. Private-use research for the Deagle mod.
//   FmodRender list   <bankdir> <filter>
//   FmodRender render <bankdir> <out.wav> <event path> <seconds> [name=value ...] [+ <seconds> name=value ...]
// "+" starts another shot in the same file after the previous one's seconds, with its own parameter values.
// In a shot's own arguments, "ev:<event path>" plays that event instead (e.g. a gunshot, then an impact the sound's
// travel time later) and "vol:<x>" sets its instance volume (EventInstance::setVolume, like the plugin's play volume).
// "file:<windows path>" plays one of our WAVs instead, like the plugin's "file:" sounds (3D in front, no roll-off), into
// the master or "bus:<bus path>". Before the shots: "send:<bus path>=<level>" adds a send from that bus into the room
// reverb (Reverb_Global, where the gun and the hits send); "send:files=<level>" gives our WAVs a group with that send.
// DUMP_MASTER=1 dumps the whole mixer (bus paths, DSP types) while the first shot plays.
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
    [DllImport(S)] public static extern int FMOD_Studio_EventDescription_GetID(IntPtr d, byte[] guid16);
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
    [DllImport(S)] public static extern int FMOD_Studio_EventInstance_SetVolume(IntPtr i, float v);
    [DllImport(C)] public static extern int FMOD_System_SetOutput(IntPtr core, int type);
    [DllImport(C)] public static extern int FMOD_System_SetSoftwareFormat(IntPtr core, int rate, int speakermode, int raw);
    [DllImport(C)] public static extern int FMOD_System_SetDSPBufferSize(IntPtr core, uint len, int num);
    [DllImport(C)] public static extern int FMOD_System_CreateDSPByType(IntPtr core, int type, out IntPtr dsp);
    [DllImport(C)] public static extern int FMOD_System_PlayDSP(IntPtr core, IntPtr dsp, IntPtr group, int paused, out IntPtr ch);
    [DllImport(C)] public static extern int FMOD_Channel_Stop(IntPtr ch);
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
    [DllImport(C)] public static extern int FMOD_DSP_GetType(IntPtr dsp, out int type);
    [DllImport(C)] public static extern int FMOD_DSP_SetParameterInt(IntPtr dsp, int i, int v);
    [DllImport(C)] public static extern int FMOD_DSP_SetParameterFloat(IntPtr dsp, int i, float v);
    [DllImport(C)] public static extern int FMOD_ChannelGroup_AddDSP(IntPtr cg, int index, IntPtr dsp);
    [DllImport(C)] public static extern int FMOD_System_CreateChannelGroup(IntPtr core, byte[] name, out IntPtr cg);
    [DllImport(C)] public static extern int FMOD_System_CreateSound(IntPtr core, byte[] name, uint mode, IntPtr exinfo, out IntPtr snd);
    [DllImport(C)] public static extern int FMOD_System_PlaySound(IntPtr core, IntPtr snd, IntPtr group, int paused, out IntPtr ch);
    [DllImport(C)] public static extern int FMOD_Channel_SetVolume(IntPtr ch, float v);
    [DllImport(C)] public static extern int FMOD_Channel_SetPaused(IntPtr ch, int paused);
    [DllImport(C)] public static extern int FMOD_Channel_Set3DAttributes(IntPtr ch, float[] pos, float[] vel);
    [DllImport(C)] public static extern int FMOD_Channel_Set3DMinMaxDistance(IntPtr ch, float mn, float mx);

    [StructLayout(LayoutKind.Explicit, Size = 56)]
    public struct PDesc
    {
        [FieldOffset(0)] public IntPtr name; [FieldOffset(8)] public uint id1; [FieldOffset(12)] public uint id2;
        [FieldOffset(16)] public float min; [FieldOffset(20)] public float max; [FieldOffset(24)] public float def;
        [FieldOffset(28)] public int type; [FieldOffset(32)] public uint flags;
        [FieldOffset(36)] public uint g0; [FieldOffset(40)] public uint g1; [FieldOffset(44)] public uint g2; [FieldOffset(48)] public uint g3;
        public byte[] guid { get { var b = new byte[16]; BitConverter.GetBytes(g0).CopyTo(b, 0); BitConverter.GetBytes(g1).CopyTo(b, 4); BitConverter.GetBytes(g2).CopyTo(b, 8); BitConverter.GetBytes(g3).CopyTo(b, 12); return b; } }
    }
    public static byte[] Z(string s) => Encoding.UTF8.GetBytes(s + "\0");
    public static string Str(byte[] b) { int n = Array.IndexOf(b, (byte)0); return Encoding.UTF8.GetString(b, 0, n < 0 ? b.Length : n); }
    public static void Ok(int r, string what) { if (r != 0) throw new Exception($"{what}: FMOD error {r}"); }
}

static class P
{
    const uint HEADER = 0x00020310;   // FMOD 2.03.10, the version Ready or Not ships
    static IntPtr sys, core;
    static long blocks; static int BlockLen = 1024;                       // mix blocks rendered so far (NRT: one update = one block)
    static void Tick() { F.FMOD_Studio_System_Update(sys); blocks++; }

    static void Init(string bankdir, string wavOut)
    {
        F.Ok(F.FMOD_Studio_System_Create(out sys, HEADER), "create");
        F.Ok(F.FMOD_Studio_System_GetCoreSystem(sys, out core), "core");
        F.Ok(F.FMOD_System_SetOutput(core, wavOut != null ? 5 : 4), "output");          // WAVWRITER_NRT / NOSOUND_NRT
        F.Ok(F.FMOD_System_SetSoftwareFormat(core, 48000, 3, 0), "format");              // 48 kHz stereo
        var bl = Environment.GetEnvironmentVariable("FMOD_BLOCK");                     // finer start timing, e.g. 256
        BlockLen = string.IsNullOrEmpty(bl) ? 1024 : int.Parse(bl);
        F.Ok(F.FMOD_System_SetDSPBufferSize(core, (uint)BlockLen, 4), "dsp buffer");      // one update = one block
        IntPtr extra = wavOut != null ? Marshal.StringToHGlobalAnsi(wavOut) : IntPtr.Zero;
        F.Ok(F.FMOD_Studio_System_Initialize(sys, 512, 0x2 /*SYNCHRONOUS_UPDATE*/, 0, extra), "init");
        var banks = new List<string> { "Master.bank", "Master.strings.bank", "Weapons.bank" };
        var more = Environment.GetEnvironmentVariable("FMOD_EXTRA_BANKS");      // e.g. "Global Amb.bank;Gas Level.bank" or "*"
        if (more == "*") foreach (var f in Directory.GetFiles(bankdir, "*.bank")) { var n = Path.GetFileName(f); if (!banks.Contains(n)) banks.Add(n); }
        else if (!string.IsNullOrEmpty(more)) banks.AddRange(more.Split(';'));
        foreach (var b in banks)
        {
            var sw = System.Diagnostics.Stopwatch.StartNew();
            int r = F.FMOD_Studio_System_LoadBankFile(sys, F.Z(Path.Combine(bankdir, b)), 0, out _);
            Console.WriteLine($"load {b}: {(r == 0 ? "ok" : "FMOD error " + r)} {sw.ElapsedMilliseconds} ms, process memory {System.Diagnostics.Process.GetCurrentProcess().WorkingSet64 / 1048576} MB");
        }
        F.FMOD_Studio_System_FlushCommands(sys);
    }

    // every bus's channel group -> its path (the mixer dump names them), after LockBuses and a few updates
    static readonly Dictionary<IntPtr, string> busOf = new Dictionary<IntPtr, string>();
    const string ROOM_REVERB = "bus:/Headphones/Reverbs/2D/Reverb_Global";
    const int DSP_SEND = 21;          // FMOD_DSP_TYPE_SEND in 2.03 (the dump: RoN's 'FMOD Send' DSPs report 21, 'FMOD Return' 22)
    static IntPtr filesGroup = IntPtr.Zero;   // send:files=<level>: our WAVs go through a group of their own with that send
    static readonly Dictionary<string, IntPtr> busCg = new Dictionary<string, IntPtr>();
    static void LockBuses()
    {
        F.FMOD_Studio_System_GetBankCount(sys, out int nb); var banks = new IntPtr[Math.Max(nb, 1)]; F.FMOD_Studio_System_GetBankList(sys, banks, banks.Length, out nb);
        var all = new List<IntPtr>();
        for (int b = 0; b < nb; b++)
        {
            F.FMOD_Studio_Bank_GetBusCount(banks[b], out int n); if (n <= 0) continue;
            var arr = new IntPtr[n]; F.FMOD_Studio_Bank_GetBusList(banks[b], arr, n, out n);
            for (int i = 0; i < n; i++) { F.FMOD_Studio_Bus_LockChannelGroup(arr[i]); all.Add(arr[i]); }
        }
        for (int k = 0; k < 4; k++) Tick();
        foreach (var bus in all)
        {
            var p = new byte[256]; F.FMOD_Studio_Bus_GetPath(bus, p, p.Length, out _);
            if (F.FMOD_Studio_Bus_GetChannelGroup(bus, out IntPtr cg) == 0 && cg != IntPtr.Zero) { busOf[cg] = F.Str(p); busCg[F.Str(p)] = cg; }
        }
    }
    // the first DSP of a type in a group (FMOD_DSP_TYPE: SEND / RETURN numbers as this FMOD reports them, see the dump)
    static IntPtr FindDsp(IntPtr cg, string name)
    {
        F.FMOD_ChannelGroup_GetNumDSPs(cg, out int nd);
        for (int i = 0; i < nd; i++) { F.FMOD_ChannelGroup_GetDSP(cg, i, out IntPtr dsp); var nb = new byte[32]; F.FMOD_DSP_GetInfo(dsp, nb, out _, out _, out _, out _); if (F.Str(nb) == name) return dsp; }
        return IntPtr.Zero;
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
        F.FMOD_DSP_GetWetDryMix(dsp, out float pre, out float post, out float dry); F.FMOD_DSP_GetType(dsp, out int dtype);
        var sb = new StringBuilder($"{indent}dsp '{F.Str(nb)}' type {dtype}{(byp != 0 ? " BYPASSED" : "")} wet {post:0.###} dry {dry:0.###}:");
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
        Console.WriteLine($"{indent}group '{F.Str(nb)}'{(busOf.TryGetValue(cg, out var bp) ? " = " + bp : "")} volume {vol:0.###} ({20 * Math.Log10(Math.Max(vol, 1e-9)):0.#} dB) channels {nch}");
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
                for (int i = 0; i < ng; i++) Console.WriteLine($"    global '{Marshal.PtrToStringAnsi(gp[i].name)}' type {gp[i].type} range {gp[i].min}..{gp[i].max} default {gp[i].def} id {gp[i].id1:x8}{gp[i].id2:x8} guid {BitConverter.ToString(gp[i].guid).Replace("-", "")}");
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
                        var g16 = new byte[16]; F.FMOD_Studio_EventDescription_GetID(evs[i], g16);
                        Console.WriteLine($"  {path}  length {len} ms  3D {is3d}  distance {mn}..{mx} m  guid {BitConverter.ToString(g16).Replace("-", "")}");
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
                var shots = new List<(double secs, List<(string, float)> ps, IntPtr desc, float vol, string file, string bus)>();
                var cur = (secs: double.Parse(a[4]), ps: new List<(string, float)>(), desc: desc, vol: 1f, file: (string)null, bus: (string)null);
                var sends = new List<(string bus, float level)>();
                var descs = new List<IntPtr> { desc };
                var snaps = new List<string>();
                for (int i = 5; i < a.Length; i++)
                {
                    if (a[i].StartsWith("snapshot:/")) { snaps.Add(a[i]); continue; }
                    if (a[i].StartsWith("start:")) { snaps.Add(a[i].Substring(6)); continue; }   // any event, e.g. a ReverbCommand
                    if (a[i].StartsWith("global:")) { var gv = a[i].Substring(7).Split('=');
                        F.FMOD_Studio_System_SetParameterByName(sys, F.Z(gv[0]), float.Parse(gv[1], System.Globalization.CultureInfo.InvariantCulture), 1);
                        Console.WriteLine($"global {gv[0]}={gv[1]}"); continue; }
                    if (a[i] == "+") { shots.Add(cur); cur = (double.Parse(a[++i]), new List<(string, float)>(), desc, 1f, null, null); continue; }
                    if (a[i].StartsWith("file:")) { cur.file = a[i].Substring(5); continue; }   // a WAV of ours instead of an event (the plugin's "file:")
                    if (a[i].StartsWith("bus:")) { cur.bus = a[i]; continue; }                // ... played into this bus (default: the master)
                    if (a[i].StartsWith("send:")) { var sv2 = a[i].Substring(5).Split('=');   // send:<bus path>=<level>: an extra send from that bus
                        sends.Add((sv2[0], float.Parse(sv2[1], System.Globalization.CultureInfo.InvariantCulture))); continue; }  // into the shared room reverb
                    if (a[i].StartsWith("ev:")) { F.Ok(F.FMOD_Studio_System_GetEvent(sys, F.Z(a[i].Substring(3)), out cur.desc), a[i]);
                        if (!descs.Contains(cur.desc)) descs.Add(cur.desc); continue; }
                    if (a[i].StartsWith("vol:")) { cur.vol = float.Parse(a[i].Substring(4), System.Globalization.CultureInfo.InvariantCulture); continue; }
                    var kv = a[i].Split('='); cur.ps.Add((kv[0], float.Parse(kv[1], System.Globalization.CultureInfo.InvariantCulture)));
                }
                shots.Add(cur);
                double block = BlockLen / 48000.0;                    // one update = one mix block in NRT mode
                foreach (var sp in snaps)                 // e.g. snapshot:/Reverb_OUT_Street01, what a map's room zone turns on
                {
                    F.Ok(F.FMOD_Studio_System_GetEvent(sys, F.Z(sp), out IntPtr sd), sp); F.Ok(F.FMOD_Studio_EventDescription_CreateInstance(sd, out IntPtr si), sp);
                    var sv = Environment.GetEnvironmentVariable("SNAP_VOLUME");      // test: does instance volume = snapshot intensity?
                    if (!string.IsNullOrEmpty(sv)) F.FMOD_Studio_EventInstance_SetVolume(si, float.Parse(sv, System.Globalization.CultureInfo.InvariantCulture));
                    F.Ok(F.FMOD_Studio_EventInstance_Start(si), sp); Console.WriteLine("snapshot on: " + sp + (string.IsNullOrEmpty(sv) ? "" : " volume " + sv));
                }
                for (int k = 0; k < 200; k++) Tick();   // let snapshots fade in (~4 s of mix time, silent)
                LockBuses();
                foreach (var (sb, lv) in sends)
                {   // into the room reverb's return (Reverb_Global: the one the gun and the hits send to)
                    IntPtr ret = IntPtr.Zero; string rp = null;
                    rp = ROOM_REVERB; ret = busCg.ContainsKey(rp) ? FindDsp(busCg[rp], "FMOD Return") : IntPtr.Zero;
                    if (ret == IntPtr.Zero) throw new Exception("no reverb return bus");
                    F.FMOD_DSP_GetParameterInt(ret, 0, out int rid, null, 0);
                    IntPtr grp;
                    if (sb == "files") { F.Ok(F.FMOD_System_CreateChannelGroup(core, F.Z("deagle files"), out grp), "files group"); filesGroup = grp; }
                    else grp = busCg[sb];
                    F.Ok(F.FMOD_System_CreateDSPByType(core, DSP_SEND, out IntPtr snd), "send dsp");
                    F.Ok(F.FMOD_DSP_SetParameterInt(snd, 0, rid), "send return id"); F.Ok(F.FMOD_DSP_SetParameterFloat(snd, 1, lv), "send level");
                    F.Ok(F.FMOD_ChannelGroup_AddDSP(grp, 0, snd), "add send");   // 0 = head: post-fader, like RoN's own
                    Console.WriteLine($"extra send {sb} -> {rp} (return {rid}) level {lv}");
                }
                var lsw = System.Diagnostics.Stopwatch.StartNew();
                foreach (var d in descs)
                {
                    F.Ok(F.FMOD_Studio_EventDescription_LoadSampleData(d), "load samples");
                    for (int k = 0; k < 2000; k++)      // wait for the event's audio to load (NRT: each update is instant)
                    {
                        Tick(); F.FMOD_Studio_EventDescription_GetSampleLoadingState(d, out int st);
                        if (st == 3) break; if (st == 4) throw new Exception("sample data failed to load");
                        System.Threading.Thread.Sleep(2);
                    }
                }
                Console.WriteLine($"event audio loaded in {lsw.ElapsedMilliseconds} ms, process memory {System.Diagnostics.Process.GetCurrentProcess().WorkingSet64 / 1048576} MB");
                bool dumped = false;
                if (Environment.GetEnvironmentVariable("CLICK") == "1")   // engine latency: a core oscillator started at a logged block
                {
                    F.Ok(F.FMOD_System_CreateDSPByType(core, 2, out IntPtr osc), "oscillator");
                    for (int k = 0; k < 20; k++) Tick();
                    Console.WriteLine($"click starts at block {blocks} = sample {blocks * BlockLen}");
                    F.Ok(F.FMOD_System_PlayDSP(core, osc, IntPtr.Zero, 0, out IntPtr och), "play osc");
                    for (int k = 0; k < 5; k++) Tick();
                    F.FMOD_Channel_Stop(och);
                    for (int k = 0; k < 40; k++) Tick();
                }
                foreach (var (secs, ps, sdesc, svol, sfile, sbus) in shots)
                {
                    if (sfile != null)
                    {   // like the plugin's PlayFile: 3D, FMOD's roll-off off (min distance 10 km), in front of the listener
                        F.Ok(F.FMOD_System_CreateSound(core, F.Z(sfile), 0x1 | 0x10 | 0x100, IntPtr.Zero, out IntPtr fs), sfile);
                        IntPtr grp = sbus != null ? busCg[sbus.Substring(4)] : filesGroup;   // default: the master (or send:files=)
                        F.Ok(F.FMOD_System_PlaySound(core, fs, grp, 1, out IntPtr fch), "play " + sfile);
                        F.FMOD_Channel_Set3DAttributes(fch, new float[] { 0, 0, 1 }, new float[] { 0, 0, 0 }); F.FMOD_Channel_Set3DMinMaxDistance(fch, 10000f, 20000f);
                        F.FMOD_Channel_SetVolume(fch, svol);
                        Console.WriteLine($"shot starts at block {blocks} = sample {blocks * BlockLen} (file {sfile} into {sbus ?? "master"}, volume {svol})");
                        F.FMOD_Channel_SetPaused(fch, 0);
                        int fsteps = (int)Math.Ceiling(secs / block);
                        for (int s = 0; s < fsteps; s++) Tick();
                        continue;
                    }
                    F.Ok(F.FMOD_Studio_EventDescription_CreateInstance(sdesc, out IntPtr inst), "instance");
                    if (svol != 1f) { F.FMOD_Studio_EventInstance_SetVolume(inst, svol); Console.WriteLine($"  volume {svol}"); }
                    foreach (var (n, v) in ps)
                    {
                        int r = F.FMOD_Studio_EventInstance_SetParameterByName(inst, F.Z(n), v, 1);
                        if (r != 0) { r = F.FMOD_Studio_System_SetParameterByName(sys, F.Z(n), v, 1); Console.WriteLine($"  {n}={v} (global) -> {r}"); }
                        else Console.WriteLine($"  {n}={v} -> ok");
                    }
                    Console.WriteLine($"shot starts at block {blocks} = sample {blocks * BlockLen}");
                    F.Ok(F.FMOD_Studio_EventInstance_Start(inst), "start");
                    int steps = (int)Math.Ceiling(secs / block);
                    for (int s = 0; s < steps; s++)
                    {
                        Tick();
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
                for (int k = 0; k < 8; k++) Tick();
                F.FMOD_Studio_System_Release(sys);       // the wav writer finishes the file on release
                Console.WriteLine("wrote " + a[2]);
                return 0;
            }
            Console.WriteLine("usage: list <bankdir> <filter> | render <bankdir> <out.wav> <event> <secs> [p=v..] [ev:<event>] [vol:<x>] [+ <secs> p=v..]"); return 2;
        }
        catch (Exception e) { Console.WriteLine("ERROR " + e.Message); return 1; }
    }
}
